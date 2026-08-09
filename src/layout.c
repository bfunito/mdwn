#include "layout.h"

#include "layout_internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
mdwn_layout_set_error(struct build_context *ctx, const char *message)
{
    if (!ctx->failed && ctx->err && ctx->err_size)
        snprintf(ctx->err, ctx->err_size, "%s", message);
    ctx->failed = 1;
}

struct mdwn_draw_item *
mdwn_layout_new_item(struct build_context *ctx, enum mdwn_draw_type type)
{
    struct mdwn_draw_item *item;

    if (ctx->failed)
        return NULL;

    item = mdwn_arena_alloc(&ctx->layout->arena, sizeof(*item));
    if (!item) {
        mdwn_layout_set_error(ctx, "out of memory while building layout");
        return NULL;
    }

    memset(item, 0, sizeof(*item));
    item->type = type;

    if (ctx->layout->last)
        ctx->layout->last->next = item;
    else
        ctx->layout->first = item;
    ctx->layout->last = item;
    return item;
}

void
mdwn_layout_add_rect_with_radius(struct build_context *ctx,
                                 float x, float y, float w, float h,
                                 float radius, struct mdwn_color color)
{
    struct mdwn_draw_item *item;

    if (w <= 0.0f || h <= 0.0f)
        return;

    item = mdwn_layout_new_item(ctx, MDWN_DRAW_RECT);
    if (!item)
        return;

    item->as.rect.x = x;
    item->as.rect.y = y;
    item->as.rect.w = w;
    item->as.rect.h = h;
    item->as.rect.radius = radius;
    item->as.rect.color = color;
}

static void
add_rect(struct build_context *ctx, float x, float y, float w, float h,
         struct mdwn_color color)
{
    mdwn_layout_add_rect_with_radius(ctx, x, y, w, h, 0.0f, color);
}

static void
add_line(struct build_context *ctx, float x1, float y1, float x2, float y2,
         struct mdwn_color color)
{
    struct mdwn_draw_item *item = mdwn_layout_new_item(ctx, MDWN_DRAW_LINE);

    if (!item)
        return;

    item->as.line.x1 = x1;
    item->as.line.y1 = y1;
    item->as.line.x2 = x2;
    item->as.line.y2 = y2;
    item->as.line.color = color;
}

struct mdwn_font *
mdwn_layout_font_for_style(struct build_context *ctx,
                           struct inline_style style)
{
    struct mdwn_font_spec spec;
    struct mdwn_font *font;

    spec.family = style.family;
    spec.size_px = style.size_px;
    spec.weight = style.weight;
    spec.italic = style.italic;

    font = mdwn_font_get(ctx->fonts, spec, ctx->err, ctx->err_size);
    if (!font)
        ctx->failed = 1;
    return font;
}

static size_t
text_length_recursive(const struct mdwn_node *node)
{
    const struct mdwn_node *child;
    size_t total = 0;

    if (node->type == MDWN_NODE_TEXT)
        return node->as.text.length;
    if (node->type == MDWN_NODE_SOFT_BREAK || node->type == MDWN_NODE_HARD_BREAK)
        return 1;

    for (child = node->first_child; child; child = child->next) {
        size_t n = text_length_recursive(child);
        if (SIZE_MAX - total < n)
            return SIZE_MAX;
        total += n;
    }
    return total;
}

static char *
copy_text_recursive(const struct mdwn_node *node, char *dst)
{
    const struct mdwn_node *child;

    if (node->type == MDWN_NODE_TEXT) {
        memcpy(dst, node->as.text.data, node->as.text.length);
        return dst + node->as.text.length;
    }
    if (node->type == MDWN_NODE_SOFT_BREAK || node->type == MDWN_NODE_HARD_BREAK) {
        *dst++ = '\n';
        return dst;
    }

    for (child = node->first_child; child; child = child->next)
        dst = copy_text_recursive(child, dst);
    return dst;
}

static char *
gather_text(struct build_context *ctx, const struct mdwn_node *node, size_t *len)
{
    char *text;
    size_t n = text_length_recursive(node);

    if (n == SIZE_MAX) {
        mdwn_layout_set_error(ctx, "text block is too large");
        return NULL;
    }

    text = mdwn_arena_alloc(&ctx->layout->arena, n + 1);
    if (!text) {
        mdwn_layout_set_error(ctx, "out of memory while building text block");
        return NULL;
    }

    copy_text_recursive(node, text);
    text[n] = '\0';
    *len = n;
    return text;
}

static char *
expand_tabs(struct build_context *ctx, const char *line, size_t len, size_t *out_len)
{
    size_t tabs = 0;
    size_t i, col = 0, n = 0;
    char *out;

    for (i = 0; i < len; ++i) {
        if (line[i] == '\t')
            ++tabs;
    }

    if (tabs == 0) {
        *out_len = len;
        return (char *)line;
    }

    if (tabs > (SIZE_MAX - len - 1) / 3) {
        mdwn_layout_set_error(ctx, "code line is too large");
        return NULL;
    }

    out = mdwn_arena_alloc(&ctx->layout->arena, len + tabs * 3 + 1);
    if (!out) {
        mdwn_layout_set_error(ctx, "out of memory while expanding tabs");
        return NULL;
    }

    for (i = 0; i < len; ++i) {
        if (line[i] == '\t') {
            size_t spaces = 4 - (col % 4);
            while (spaces--) {
                out[n++] = ' ';
                ++col;
            }
        } else {
            out[n++] = line[i];
            ++col;
        }
    }

    out[n] = '\0';
    *out_len = n;
    return out;
}

static int
add_direct_text(struct build_context *ctx,
                const char *text, size_t len,
                struct inline_style style,
                float x, float baseline, float line_height)
{
    struct mdwn_font *font = mdwn_layout_font_for_style(ctx, style);
    struct mdwn_shaped_glyph *glyphs;
    size_t glyph_count;
    float width;
    struct mdwn_draw_item *item;
    char *stored_text;

    if (!font)
        return -1;

    if (mdwn_font_shape(font, text, len, &ctx->layout->arena,
                        &glyphs, &glyph_count, &width,
                        ctx->err, ctx->err_size) < 0) {
        ctx->failed = 1;
        return -1;
    }

    if (glyph_count == 0)
        return 0;

    item = mdwn_layout_new_item(ctx, MDWN_DRAW_TEXT);
    if (!item)
        return -1;

    stored_text = mdwn_arena_alloc(&ctx->layout->arena, len);
    if (!stored_text) {
        mdwn_layout_set_error(ctx, "out of memory while storing text layout");
        return -1;
    }
    memcpy(stored_text, text, len);

    item->as.text.font = font;
    item->as.text.glyphs = glyphs;
    item->as.text.glyph_count = glyph_count;
    item->as.text.text = stored_text;
    item->as.text.text_length = len;
    item->as.text.order = ctx->layout->text_count++;
    item->as.text.x = x;
    item->as.text.top = baseline
        - (line_height - mdwn_font_ascender(font)
           - mdwn_font_descender(font)) * 0.5f
        - mdwn_font_ascender(font);
    item->as.text.baseline = baseline;
    item->as.text.width = width;
    item->as.text.line_height = line_height;
    item->as.text.color = style.color;
    item->as.text.strike = style.strike;
    item->as.text.underline = style.underline;
    return 0;
}

static float
layout_code_like_block(struct build_context *ctx, const struct mdwn_node *node,
                       float x, float width, float y, bool html)
{
    struct inline_style style = mdwn_layout_make_style(
        ctx, ctx->theme->code_size_px,
        html ? ctx->theme->muted : ctx->theme->text);
    struct mdwn_font *font;
    char *text;
    size_t len;
    size_t lines = 1;
    size_t i, start;
    float line_height;
    float baseline_offset;
    float box_height;
    float border = ctx->theme->code_border_width;

    style.family = MDWN_FONT_MONO;
    text = gather_text(ctx, node, &len);
    if (!text)
        return y;

    for (i = 0; i < len; ++i) {
        if (text[i] == '\n')
            ++lines;
    }
    if (len && text[len - 1] == '\n' && lines > 1)
        --lines;

    font = mdwn_layout_font_for_style(ctx, style);
    if (!font)
        return y;

    line_height = (float)style.size_px
        * ctx->theme->code_line_height_scale;
    baseline_offset = (line_height - mdwn_font_ascender(font) -
                       mdwn_font_descender(font)) * 0.5f + mdwn_font_ascender(font);
    box_height = border * 2.0f
        + ctx->theme->code_block_padding * 2.0f
        + line_height * (float)lines;

    if (border > 0.0f) {
        mdwn_layout_add_rect_with_radius(ctx, x, y, width, box_height,
                                         ctx->theme->border_radius,
                                         ctx->theme->code_border);
    }
    mdwn_layout_add_rect_with_radius(
        ctx, x + border, y + border,
        width - border * 2.0f,
        box_height - border * 2.0f,
        fmaxf(ctx->theme->border_radius - border, 0.0f),
        ctx->theme->code_background);

    start = 0;
    {
        size_t line_index = 0;
        while (start <= len && line_index < lines) {
            size_t end = start;
            size_t expanded_len;
            char *expanded;

            while (end < len && text[end] != '\n')
                ++end;

            expanded = expand_tabs(ctx, text + start, end - start, &expanded_len);
            if (!expanded)
                return y;

            if (expanded_len) {
                float baseline = y + border + ctx->theme->code_block_padding
                    + baseline_offset +
                                 line_height * (float)line_index;
                if (add_direct_text(ctx, expanded, expanded_len, style,
                                    x + border + ctx->theme->code_block_padding,
                                    baseline, line_height) < 0)
                    return y;
            }

            ++line_index;
            if (end >= len)
                break;
            start = end + 1;
        }
    }

    return y + box_height + ctx->theme->block_spacing;
}

static float layout_block(struct build_context *, const struct mdwn_node *,
                          float, float, float, struct mdwn_color, bool);

static float
layout_list(struct build_context *ctx, const struct mdwn_node *list,
            float x, float width, float y, struct mdwn_color color)
{
    const struct mdwn_node *item;
    unsigned index = list->as.list.start;
    bool ordered = list->type == MDWN_NODE_ORDERED_LIST;
    bool nested = list->parent && list->parent->type == MDWN_NODE_LIST_ITEM;

    for (item = list->first_child; item; item = item->next) {
        const struct mdwn_node *child;
        char prefix[32];
        int prefix_len;
        struct inline_style prefix_style = mdwn_layout_make_style(
            ctx, ctx->theme->text_size_px, color);
        float item_y = y;
        float child_y = item_y;
        float indent = ctx->theme->list_indent;

        prefix_style.line_height = ctx->theme->list_line_height_scale;

        if (item->type != MDWN_NODE_LIST_ITEM)
            continue;

        if (item->as.list_item.task) {
            prefix_len = snprintf(prefix, sizeof(prefix), "[%c]",
                                  item->as.list_item.checked ? 'x' : ' ');
            prefix_style.family = MDWN_FONT_MONO;
        } else if (ordered) {
            prefix_len = snprintf(prefix, sizeof(prefix), "%u.", index++);
        } else {
            memcpy(prefix, "\xe2\x80\xa2", 3);
            prefix[3] = '\0';
            prefix_len = 3;
            prefix_style.size_px = (ctx->theme->text_size_px * 4 + 2) / 3;
            prefix_style.line_height =
                (float)ctx->theme->text_size_px
                * ctx->theme->list_line_height_scale
                / (float)prefix_style.size_px;
        }

        if (prefix_len > 0 &&
            mdwn_layout_inline_text_right(
                ctx, prefix, (size_t)prefix_len,
                x, item_y, indent - 6.0f, prefix_style) < 0)
            return y;

        child = item->first_child;

        while (child) {
            if (mdwn_layout_is_inline_node(child)) {
                child_y = mdwn_layout_inline_sequence(
                    ctx,
                    &child,
                    x + indent,
                    child_y,
                    width - indent,
                    color,
                    ctx->theme->list_line_height_scale
                );
            } else {
                child_y = layout_block(
                    ctx,
                    child,
                    x + indent,
                    width - indent,
                    child_y,
                    color,
                    list->as.list.tight
                );

                child = child->next;
            }

            if (ctx->failed)
                return y;
        }

        if (child_y < item_y + (float)ctx->theme->text_size_px
                                * ctx->theme->list_line_height_scale) {
            child_y = item_y + (float)ctx->theme->text_size_px
                               * ctx->theme->list_line_height_scale;
        }
        y = child_y;
        if (item->next)
            y += ctx->theme->list_item_spacing;
    }

    return y + (nested ? 0.0f : ctx->theme->block_spacing);
}

static unsigned
count_row_cells(const struct mdwn_node *row)
{
    const struct mdwn_node *cell;
    unsigned count = 0;

    for (cell = row->first_child; cell; cell = cell->next) {
        if (cell->type == MDWN_NODE_TABLE_CELL ||
            cell->type == MDWN_NODE_TABLE_HEADER_CELL)
            ++count;
    }
    return count;
}

static float
layout_table_row(struct build_context *ctx, const struct mdwn_node *row,
                 unsigned columns, const float *col_widths,
                 unsigned row_index, float x, float table_width, float y)
{
    const struct mdwn_node *cell;
    unsigned col = 0;
    float padding_x = ctx->theme->table_cell_padding_x;
    float padding_y = ctx->theme->table_cell_padding_y;
    float row_height = 0.0f;
    float cx = x;

    for (cell = row->first_child; cell && col < columns; cell = cell->next) {
        struct inline_style style;
        float col_width;
        float h;

        if (cell->type != MDWN_NODE_TABLE_CELL &&
            cell->type != MDWN_NODE_TABLE_HEADER_CELL)
            continue;

        col_width = col_widths[col];
        style = mdwn_layout_make_style(
            ctx, ctx->theme->table_size_px,
            cell->type == MDWN_NODE_TABLE_HEADER_CELL
                ? ctx->theme->heading
                : ctx->theme->text);
        style.line_height = ctx->theme->table_line_height_scale;
        if (cell->type == MDWN_NODE_TABLE_HEADER_CELL)
            style.weight = ctx->theme->table_header_weight;
        h = mdwn_layout_inline_box(
            ctx, cell, 0.0f, 0.0f,
            fmaxf(col_width - padding_x * 2.0f, 1.0f),
            style, false);
        row_height = fmaxf(row_height, h + padding_y * 2.0f);
        ++col;
    }

    if (row_index % 2 == 0 && ctx->theme->table_alt_background.a != 0)
        add_rect(ctx, x, y, table_width, row_height,
                 ctx->theme->table_alt_background);

    col = 0;
    for (cell = row->first_child; cell && col < columns; cell = cell->next) {
        struct inline_style style;
        float col_width;
        float content_width;
        float content_x;

        if (cell->type != MDWN_NODE_TABLE_CELL &&
            cell->type != MDWN_NODE_TABLE_HEADER_CELL)
            continue;

        col_width = col_widths[col];
        add_line(ctx, cx, y, cx + col_width, y, ctx->theme->border);
        add_line(ctx, cx, y, cx, y + row_height, ctx->theme->border);

        style = mdwn_layout_make_style(
            ctx, ctx->theme->table_size_px,
            cell->type == MDWN_NODE_TABLE_HEADER_CELL
                ? ctx->theme->heading
                : ctx->theme->text);
        style.line_height = ctx->theme->table_line_height_scale;
        if (cell->type == MDWN_NODE_TABLE_HEADER_CELL)
            style.weight = ctx->theme->table_header_weight;
        content_width = fmaxf(col_width - padding_x * 2.0f, 1.0f);
        content_x = cx + padding_x;
        if (cell->as.table_cell.align != MDWN_ALIGN_DEFAULT &&
            cell->as.table_cell.align != MDWN_ALIGN_LEFT) {
            float text_width = fminf(
                mdwn_layout_measure_inline_width(ctx, cell, style),
                content_width);
            float free_width = content_width - text_width;

            if (cell->as.table_cell.align == MDWN_ALIGN_CENTER)
                content_x += free_width * 0.5f;
            else if (cell->as.table_cell.align == MDWN_ALIGN_RIGHT)
                content_x += free_width;
        }
        (void)mdwn_layout_inline_box(ctx, cell, content_x, y + padding_y,
                                     content_width, style, true);
        cx += col_width;
        ++col;
    }

    add_line(ctx, x + table_width, y,
             x + table_width, y + row_height,
             ctx->theme->border);
    add_line(ctx, x, y + row_height,
             x + table_width, y + row_height,
             ctx->theme->border);
    return y + row_height;
}

static float
measure_table_row_widths(struct build_context *ctx,
                         const struct mdwn_node *row,
                         unsigned columns, float *col_widths)
{
    const struct mdwn_node *cell;
    unsigned col = 0;
    float total = 0.0f;

    for (cell = row->first_child; cell && col < columns; cell = cell->next) {
        struct inline_style style;
        float width;

        if (cell->type != MDWN_NODE_TABLE_CELL &&
            cell->type != MDWN_NODE_TABLE_HEADER_CELL)
            continue;

        style = mdwn_layout_make_style(
            ctx, ctx->theme->table_size_px,
            cell->type == MDWN_NODE_TABLE_HEADER_CELL
                ? ctx->theme->heading
                : ctx->theme->text);
        style.line_height = ctx->theme->table_line_height_scale;
        if (cell->type == MDWN_NODE_TABLE_HEADER_CELL)
            style.weight = ctx->theme->table_header_weight;
        width = mdwn_layout_measure_inline_width(ctx, cell, style)
            + ctx->theme->table_cell_padding_x * 2.0f;
        col_widths[col] = fmaxf(col_widths[col], width);
        ++col;
    }

    for (col = 0; col < columns; ++col)
        total += col_widths[col];
    return total;
}

static float
layout_table_section(struct build_context *ctx, const struct mdwn_node *section,
                     unsigned columns, const float *col_widths,
                     float x, float table_width, float y)
{
    const struct mdwn_node *row;
    unsigned row_index = 1;

    for (row = section->first_child; row; row = row->next) {
        if (row->type == MDWN_NODE_TABLE_ROW) {
            y = layout_table_row(ctx, row, columns, col_widths,
                                 row_index, x, table_width, y);
            ++row_index;
        }
    }
    return y;
}

static float
layout_table(struct build_context *ctx, const struct mdwn_node *table,
             float x, float width, float y)
{
    const struct mdwn_node *section;
    float *col_widths;
    float table_width = 0.0f;
    unsigned columns = table->as.table.columns;
    unsigned row_index = 1;
    unsigned col;

    if (columns == 0) {
        for (section = table->first_child; section && columns == 0; section = section->next) {
            const struct mdwn_node *row = section->first_child;
            while (row && row->type != MDWN_NODE_TABLE_ROW)
                row = row->next;
            if (row)
                columns = count_row_cells(row);
        }
    }
    if (columns == 0)
        return y;

    col_widths = mdwn_arena_alloc(&ctx->layout->arena,
                                  columns * sizeof(*col_widths));
    if (!col_widths) {
        mdwn_layout_set_error(ctx, "out of memory while measuring table");
        return y;
    }
    memset(col_widths, 0, columns * sizeof(*col_widths));

    for (section = table->first_child; section; section = section->next) {
        if (section->type == MDWN_NODE_TABLE_HEAD ||
            section->type == MDWN_NODE_TABLE_BODY) {
            const struct mdwn_node *row;
            for (row = section->first_child; row; row = row->next) {
                if (row->type == MDWN_NODE_TABLE_ROW)
                    table_width = measure_table_row_widths(
                        ctx, row, columns, col_widths);
            }
        } else if (section->type == MDWN_NODE_TABLE_ROW) {
            table_width = measure_table_row_widths(
                ctx, section, columns, col_widths);
        }
    }

    if (table_width <= 0.0f)
        return y;
    if (table_width > width) {
        float scale = width / table_width;
        table_width = width;
        for (col = 0; col < columns; ++col)
            col_widths[col] *= scale;
    }

    for (section = table->first_child; section; section = section->next) {
        if (section->type == MDWN_NODE_TABLE_HEAD ||
            section->type == MDWN_NODE_TABLE_BODY)
            y = layout_table_section(ctx, section, columns, col_widths,
                                     x, table_width, y);
        else if (section->type == MDWN_NODE_TABLE_ROW)
            y = layout_table_row(ctx, section, columns, col_widths,
                                 row_index++, x, table_width, y);
    }

    return y + ctx->theme->block_spacing;
}

static float
block_bottom_margin(struct build_context *ctx, const struct mdwn_node *node,
                    bool compact)
{
    switch (node->type) {
    case MDWN_NODE_PARAGRAPH:
        return compact ? 0.0f : ctx->theme->block_spacing;
    case MDWN_NODE_HORIZONTAL_RULE:
        return ctx->theme->rule_margin;
    case MDWN_NODE_HEADING:
        return ctx->theme->block_spacing;
    case MDWN_NODE_BLOCKQUOTE:
        return ctx->theme->blockquote_margin;
    case MDWN_NODE_CODE_BLOCK:
    case MDWN_NODE_RAW_HTML_BLOCK:
    case MDWN_NODE_TABLE:
        return ctx->theme->block_spacing;
    case MDWN_NODE_UNORDERED_LIST:
    case MDWN_NODE_ORDERED_LIST:
        return node->parent && node->parent->type == MDWN_NODE_LIST_ITEM
            ? 0.0f
            : ctx->theme->block_spacing;
    default:
        return 0.0f;
    }
}

static float
layout_block(struct build_context *ctx, const struct mdwn_node *node,
             float x, float width, float y, struct mdwn_color color, bool compact)
{
    const struct mdwn_node *child;

    if (ctx->failed)
        return y;

    switch (node->type) {
    case MDWN_NODE_PARAGRAPH: {
        struct inline_style style = mdwn_layout_make_style(
            ctx, ctx->theme->text_size_px, color);
        float h = mdwn_layout_inline_box(
            ctx, node, x, y, width, style, true);
        return y + h + (compact ? 0.0f : ctx->theme->block_spacing);
    }

    case MDWN_NODE_HEADING: {
        unsigned level = node->as.heading.level;
        struct inline_style style;
        float h;
        float border_y;

        if (level < 1 || level > 6)
            level = 1;

        if (node != node->parent->first_child) {
            y += fmaxf(ctx->theme->heading_margin_top[level - 1]
                       - ctx->theme->block_spacing, 0.0f);
        }
        color = level == 6
            ? ctx->theme->heading_small
            : ctx->theme->heading;
        style = mdwn_layout_make_style(
            ctx, ctx->theme->heading_size_px[level - 1], color);
        style.line_height = ctx->theme->heading_line_height_scale;
        style.letter_spacing_em =
            ctx->theme->heading_letter_spacing_em[level - 1];
        style.weight = ctx->theme->heading_weight;
        style.heading = true;
        h = mdwn_layout_inline_box(ctx, node, x, y, width, style, true);
        y += h;

        if (level <= 2 && ctx->theme->border_muted.a != 0) {
            border_y = y + (float)style.size_px
                * ctx->theme->heading_padding_bottom_em;
            add_line(ctx, x, border_y, x + width, border_y,
                     ctx->theme->border_muted);
            y = border_y + 1.0f;
        }
        return y + ctx->theme->heading_margin_bottom;
    }

    case MDWN_NODE_BLOCKQUOTE: {
        float start = y;
        float child_y = start + ctx->theme->blockquote_padding_y;
        float border_width = ctx->theme->blockquote_border_width;
        float inner_x = x + border_width + ctx->theme->blockquote_padding;
        float inner_width = fmaxf(
            width - border_width - ctx->theme->blockquote_padding
                - ctx->theme->blockquote_padding_right
                - ctx->theme->blockquote_margin_right,
            1.0f);

        for (child = node->first_child; child; child = child->next)
            child_y = layout_block(ctx, child, inner_x, inner_width,
                                   child_y, ctx->theme->muted, false);

        if (node->last_child)
            child_y -= block_bottom_margin(ctx, node->last_child, false);

        child_y += ctx->theme->blockquote_padding_y;

        add_rect(ctx, x, start, border_width,
                 fmaxf(child_y - start, 1.0f),
                 ctx->theme->blockquote_border);
        return child_y + ctx->theme->blockquote_margin;
    }

    case MDWN_NODE_UNORDERED_LIST:
    case MDWN_NODE_ORDERED_LIST:
        return layout_list(ctx, node, x, width, y, color);

    case MDWN_NODE_CODE_BLOCK:
        return layout_code_like_block(ctx, node, x, width, y, false);

    case MDWN_NODE_RAW_HTML_BLOCK:
        return layout_code_like_block(ctx, node, x, width, y, true);

    case MDWN_NODE_HORIZONTAL_RULE:
        if (node != node->parent->first_child) {
            y += fmaxf(ctx->theme->rule_margin
                       - ctx->theme->block_spacing, 0.0f);
        }
        add_rect(ctx, x, y, width, ctx->theme->rule_height,
                 ctx->theme->border);
        return y + ctx->theme->rule_height + ctx->theme->rule_margin;

    case MDWN_NODE_TABLE:
        return layout_table(ctx, node, x, width, y);

    case MDWN_NODE_DOCUMENT:
        for (child = node->first_child; child; child = child->next)
            y = layout_block(ctx, child, x, width, y, color, false);
        if (node->last_child)
            y -= block_bottom_margin(ctx, node->last_child, false);
        return y;

    case MDWN_NODE_LIST_ITEM:
        for (child = node->first_child; child; child = child->next)
            y = layout_block(ctx, child, x, width, y, color, compact);
        return y;

    default:
        return y;
    }
}

void
mdwn_layout_init(struct mdwn_layout *layout)
{
    memset(layout, 0, sizeof(*layout));
    mdwn_arena_init(&layout->arena, 64 * 1024);
}

void
mdwn_layout_destroy(struct mdwn_layout *layout)
{
    mdwn_arena_destroy(&layout->arena);
    memset(layout, 0, sizeof(*layout));
}

int
mdwn_layout_build(struct mdwn_layout *layout,
                  const struct mdwn_document *doc,
                  struct mdwn_font_system *fonts,
                  const struct mdwn_theme *theme,
                  int viewport_width, int viewport_height,
                  char *err, size_t err_size)
{
    struct build_context ctx;
    float available;
    float content_width;
    float content_x;
    float y;

    mdwn_arena_destroy(&layout->arena);
    mdwn_arena_init(&layout->arena, 64 * 1024);
    layout->first = NULL;
    layout->last = NULL;
    layout->text_count = 0;
    layout->viewport_width = viewport_width;
    layout->viewport_height = viewport_height;
    layout->content_height = 0.0f;

    memset(&ctx, 0, sizeof(ctx));
    ctx.layout = layout;
    ctx.fonts = fonts;
    ctx.theme = theme;
    ctx.err = err;
    ctx.err_size = err_size;

    available = (float)viewport_width - theme->outer_margin * 2.0f;
    if (available < 64.0f)
        available = 64.0f;
    content_width = fminf(available, theme->content_max_width);
    content_x = ((float)viewport_width - content_width) * 0.5f;
    if (content_x < 8.0f)
        content_x = 8.0f;

    y = layout_block(&ctx, doc->root, content_x, content_width,
                     theme->top_margin, theme->text, false);

    if (ctx.failed)
        return -1;

    layout->content_height = y + theme->bottom_margin;
    if (layout->content_height < (float)viewport_height)
        layout->content_height = (float)viewport_height;
    return 0;
}
