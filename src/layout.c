#include "layout.h"

#include "document.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct build_context {
    struct mdwn_layout *layout;
    struct mdwn_font_system *fonts;
    const struct mdwn_theme *theme;
    char *err;
    size_t err_size;
    int failed;
};

struct inline_style {
    enum mdwn_font_family family;
    unsigned size_px;
    bool bold;
    bool italic;
    bool strike;
    bool underline;
    bool code_background;
    bool preserve_spaces;
    struct mdwn_color color;
};

struct inline_flow {
    struct build_context *ctx;
    float x0;
    float width;
    float x;
    float top;
    float line_top;
    float line_height;
    float baseline_offset;
    bool emit;
    bool has_content;
};

static void
set_error(struct build_context *ctx, const char *message)
{
    if (!ctx->failed && ctx->err && ctx->err_size)
        snprintf(ctx->err, ctx->err_size, "%s", message);
    ctx->failed = 1;
}

static struct mdwn_draw_item *
new_item(struct build_context *ctx, enum mdwn_draw_type type)
{
    struct mdwn_draw_item *item;

    if (ctx->failed)
        return NULL;

    item = mdwn_arena_alloc(&ctx->layout->arena, sizeof(*item));
    if (!item) {
        set_error(ctx, "out of memory while building layout");
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

static void
add_rect(struct build_context *ctx, float x, float y, float w, float h,
         struct mdwn_color color)
{
    struct mdwn_draw_item *item;

    if (w <= 0.0f || h <= 0.0f)
        return;

    item = new_item(ctx, MDWN_DRAW_RECT);
    if (!item)
        return;

    item->as.rect.x = x;
    item->as.rect.y = y;
    item->as.rect.w = w;
    item->as.rect.h = h;
    item->as.rect.color = color;
}

static void
add_line(struct build_context *ctx, float x1, float y1, float x2, float y2,
         struct mdwn_color color)
{
    struct mdwn_draw_item *item = new_item(ctx, MDWN_DRAW_LINE);

    if (!item)
        return;

    item->as.line.x1 = x1;
    item->as.line.y1 = y1;
    item->as.line.x2 = x2;
    item->as.line.y2 = y2;
    item->as.line.color = color;
}

static struct mdwn_font *
font_for_style(struct build_context *ctx, struct inline_style style)
{
    struct mdwn_font_spec spec;
    struct mdwn_font *font;

    spec.family = style.family;
    spec.size_px = style.size_px;
    spec.bold = style.bold;
    spec.italic = style.italic;

    font = mdwn_font_get(ctx->fonts, spec, ctx->err, ctx->err_size);
    if (!font)
        ctx->failed = 1;
    return font;
}

static float
style_line_height(struct build_context *ctx, struct inline_style style,
                  float *baseline_offset)
{
    struct mdwn_font *font = font_for_style(ctx, style);
    float natural;
    float line_height;
    float ascender;
    float descender;

    if (!font)
        return 0.0f;

    natural = mdwn_font_line_height(font);
    line_height = fmaxf(natural,
                        (float)style.size_px * ctx->theme->line_height_scale);
    ascender = mdwn_font_ascender(font);
    descender = mdwn_font_descender(font);
    *baseline_offset = (line_height - ascender - descender) * 0.5f + ascender;
    return line_height;
}

static int
flow_init(struct inline_flow *flow, struct build_context *ctx,
          float x, float y, float width, struct inline_style base, bool emit)
{
    memset(flow, 0, sizeof(*flow));
    flow->ctx = ctx;
    flow->x0 = x;
    flow->width = fmaxf(width, 1.0f);
    flow->x = x;
    flow->top = y;
    flow->line_top = y;
    flow->emit = emit;
    flow->line_height = style_line_height(ctx, base, &flow->baseline_offset);
    return ctx->failed ? -1 : 0;
}

static void
flow_newline(struct inline_flow *flow)
{
    flow->line_top += flow->line_height;
    flow->x = flow->x0;
    flow->has_content = false;
}

static int
flow_place_token(struct inline_flow *flow,
                 const char *text, size_t len,
                 struct inline_style style,
                 bool is_space)
{
    struct mdwn_font *font;
    struct mdwn_shaped_glyph *glyphs = NULL;
    size_t glyph_count = 0;
    float width = 0.0f;
    float baseline;
    struct mdwn_draw_item *item;

    if (len == 0 || flow->ctx->failed)
        return flow->ctx->failed ? -1 : 0;

    if (is_space && !flow->has_content)
        return 0;

    font = font_for_style(flow->ctx, style);
    if (!font)
        return -1;

    if (flow->emit) {
        if (mdwn_font_shape(font, text, len, &flow->ctx->layout->arena,
                            &glyphs, &glyph_count, &width,
                            flow->ctx->err, flow->ctx->err_size) < 0) {
            flow->ctx->failed = 1;
            return -1;
        }
    } else {
        if (mdwn_font_measure(font, text, len, &width,
                              flow->ctx->err, flow->ctx->err_size) < 0) {
            flow->ctx->failed = 1;
            return -1;
        }
    }

    if (!is_space && flow->has_content &&
        flow->x + width > flow->x0 + flow->width) {
        flow_newline(flow);
    }

    if (is_space && flow->x + width > flow->x0 + flow->width) {
        flow_newline(flow);
        return 0;
    }

    baseline = flow->line_top + flow->baseline_offset;

    if (flow->emit && style.code_background)
        add_rect(flow->ctx, flow->x - 2.0f, flow->line_top + 2.0f,
                 width + 4.0f, flow->line_height - 4.0f,
                 flow->ctx->theme->code_background);

    if (flow->emit && glyph_count) {
        item = new_item(flow->ctx, MDWN_DRAW_TEXT);
        if (!item)
            return -1;

        item->as.text.font = font;
        item->as.text.glyphs = glyphs;
        item->as.text.glyph_count = glyph_count;
        item->as.text.x = flow->x;
        item->as.text.baseline = baseline;
        item->as.text.width = width;
        item->as.text.line_height = flow->line_height;
        item->as.text.color = style.color;
        item->as.text.strike = style.strike;
        item->as.text.underline = style.underline;
    }

    flow->x += width;
    if (!is_space)
        flow->has_content = true;
    return flow->ctx->failed ? -1 : 0;
}

static int
flow_text(struct inline_flow *flow, const char *text, size_t len,
          struct inline_style style)
{
    size_t i = 0;

    if (style.preserve_spaces)
        return flow_place_token(flow, text, len, style, false);

    while (i < len) {
        size_t start;

        if (text[i] == '\n' || text[i] == '\r') {
            if (text[i] == '\r' && i + 1 < len && text[i + 1] == '\n')
                ++i;
            flow_newline(flow);
            ++i;
            continue;
        }

        if (text[i] == ' ' || text[i] == '\t') {
            while (i < len && (text[i] == ' ' || text[i] == '\t'))
                ++i;
            if (flow_place_token(flow, " ", 1, style, true) < 0)
                return -1;
            continue;
        }

        start = i;
        while (i < len && text[i] != ' ' && text[i] != '\t' &&
               text[i] != '\n' && text[i] != '\r')
            ++i;

        if (flow_place_token(flow, text + start, i - start, style, false) < 0)
            return -1;
    }

    return 0;
}

static int
layout_inline_node(struct inline_flow *flow, const struct mdwn_node *node,
                   struct inline_style style)
{
    const struct mdwn_node *child;

    switch (node->type) {
    case MDWN_NODE_TEXT:
        return flow_text(flow, node->as.text.data, node->as.text.length, style);
    case MDWN_NODE_SOFT_BREAK:
        return flow_place_token(flow, " ", 1, style, true);
    case MDWN_NODE_HARD_BREAK:
        flow_newline(flow);
        return 0;
    case MDWN_NODE_EMPHASIS:
        style.italic = true;
        break;
    case MDWN_NODE_STRONG:
        style.bold = true;
        break;
    case MDWN_NODE_STRIKETHROUGH:
        style.strike = true;
        break;
    case MDWN_NODE_CODE_SPAN:
        style.family = MDWN_FONT_MONO;
        style.code_background = true;
        style.preserve_spaces = true;
        break;
    case MDWN_NODE_LINK:
        style.color = flow->ctx->theme->link;
        style.underline = true;
        break;
    case MDWN_NODE_IMAGE:
        style.italic = true;
        style.color = flow->ctx->theme->muted;
        break;
    case MDWN_NODE_RAW_HTML_SPAN:
        style.family = MDWN_FONT_MONO;
        style.color = flow->ctx->theme->muted;
        style.size_px = style.size_px > 1 ? style.size_px - 1 : style.size_px;
        break;
    default:
        break;
    }

    for (child = node->first_child; child; child = child->next) {
        if (layout_inline_node(flow, child, style) < 0)
            return -1;
    }
    return 0;
}

static float
layout_inline_box(struct build_context *ctx, const struct mdwn_node *container,
                  float x, float y, float width,
                  struct inline_style base, bool emit)
{
    struct inline_flow flow;
    const struct mdwn_node *child;

    if (flow_init(&flow, ctx, x, y, width, base, emit) < 0)
        return 0.0f;

    for (child = container->first_child; child; child = child->next) {
        if (layout_inline_node(&flow, child, base) < 0)
            return 0.0f;
    }

    return (flow.line_top - flow.top) + flow.line_height;
}

static struct inline_style
make_style(unsigned size_px, struct mdwn_color color)
{
    struct inline_style style;

    memset(&style, 0, sizeof(style));
    style.family = MDWN_FONT_SANS;
    style.size_px = size_px;
    style.color = color;
    return style;
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
        set_error(ctx, "text block is too large");
        return NULL;
    }

    text = mdwn_arena_alloc(&ctx->layout->arena, n + 1);
    if (!text) {
        set_error(ctx, "out of memory while building text block");
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
        set_error(ctx, "code line is too large");
        return NULL;
    }

    out = mdwn_arena_alloc(&ctx->layout->arena, len + tabs * 3 + 1);
    if (!out) {
        set_error(ctx, "out of memory while expanding tabs");
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
    struct mdwn_font *font = font_for_style(ctx, style);
    struct mdwn_shaped_glyph *glyphs;
    size_t glyph_count;
    float width;
    struct mdwn_draw_item *item;

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

    item = new_item(ctx, MDWN_DRAW_TEXT);
    if (!item)
        return -1;

    item->as.text.font = font;
    item->as.text.glyphs = glyphs;
    item->as.text.glyph_count = glyph_count;
    item->as.text.x = x;
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
    struct inline_style style = make_style(ctx->theme->code_size_px,
        html ? ctx->theme->muted : ctx->theme->text);
    struct mdwn_font *font;
    char *text;
    size_t len;
    size_t lines = 1;
    size_t i, start;
    float line_height;
    float baseline_offset;
    float box_height;

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

    font = font_for_style(ctx, style);
    if (!font)
        return y;

    line_height = fmaxf(mdwn_font_line_height(font),
                        ctx->theme->code_line_height);
    baseline_offset = (line_height - mdwn_font_ascender(font) -
                       mdwn_font_descender(font)) * 0.5f + mdwn_font_ascender(font);
    box_height = 24.0f + line_height * (float)lines;

    add_rect(ctx, x, y, width, box_height, ctx->theme->code_background);

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
                float baseline = y + 12.0f + baseline_offset +
                                 line_height * (float)line_index;
                if (add_direct_text(ctx, expanded, expanded_len, style,
                                    x + 12.0f, baseline, line_height) < 0)
                    return y;
            }

            ++line_index;
            if (end >= len)
                break;
            start = end + 1;
        }
    }

    return y + box_height + 16.0f;
}

static float layout_block(struct build_context *, const struct mdwn_node *,
                          float, float, float, struct mdwn_color, bool);

static bool
is_inline_node(const struct mdwn_node *node)
{
    switch (node->type) {
    case MDWN_NODE_EMPHASIS:
    case MDWN_NODE_STRONG:
    case MDWN_NODE_LINK:
    case MDWN_NODE_IMAGE:
    case MDWN_NODE_CODE_SPAN:
    case MDWN_NODE_STRIKETHROUGH:
    case MDWN_NODE_RAW_HTML_SPAN:
    case MDWN_NODE_TEXT:
    case MDWN_NODE_SOFT_BREAK:
    case MDWN_NODE_HARD_BREAK:
        return true;

    default:
        return false;
    }
}

static float
layout_inline_sequence(struct build_context *ctx,
                       const struct mdwn_node **node,
                       float x, float y, float width,
                       struct mdwn_color color, bool compact)
{
    struct inline_style style = make_style(ctx->theme->text_size_px, color);
    struct inline_flow flow;
    const struct mdwn_node *child = *node;

    if (flow_init(&flow, ctx, x, y, width, style, true) < 0)
        return y;

    while (child && is_inline_node(child)) {
        if (layout_inline_node(&flow, child, style) < 0)
            return y;

        child = child->next;
    }

    *node = child;

    return y
        + (flow.line_top - flow.top)
        + flow.line_height
        + (compact ? 4.0f : 16.0f);
}

static float
layout_list(struct build_context *ctx, const struct mdwn_node *list,
            float x, float width, float y, struct mdwn_color color)
{
    const struct mdwn_node *item;
    unsigned index = list->as.list.start;
    bool ordered = list->type == MDWN_NODE_ORDERED_LIST;

    y += 2.0f;

    for (item = list->first_child; item; item = item->next) {
        const struct mdwn_node *child;
        char prefix[32];
        int prefix_len;
        struct inline_style prefix_style = make_style(
            ctx->theme->text_size_px, color);
        float item_y = y;
        float child_y = item_y;
        float indent = 30.0f;

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
        }

        if (prefix_len > 0) {
            struct inline_flow flow;
            if (flow_init(&flow, ctx, x, item_y, indent - 6.0f,
                          prefix_style, true) < 0)
                return y;
            if (flow_place_token(&flow, prefix, (size_t)prefix_len,
                                 prefix_style, false) < 0)
                return y;
        }

        child = item->first_child;

        while (child) {
            if (is_inline_node(child)) {
                child_y = layout_inline_sequence(
                    ctx,
                    &child,
                    x + indent,
                    child_y,
                    width - indent,
                    color,
                    list->as.list.tight
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

        if (child_y < item_y + 24.0f)
            child_y = item_y + 24.0f;
        y = child_y + (list->as.list.tight ? 0.0f : 3.0f);
    }

    return y + 8.0f;
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
                 unsigned columns, float x, float width, float y)
{
    const struct mdwn_node *cell;
    unsigned col = 0;
    float col_width = width / (float)columns;
    float row_height = 34.0f;

    for (cell = row->first_child; cell && col < columns; cell = cell->next) {
        struct inline_style style;
        float h;

        if (cell->type != MDWN_NODE_TABLE_CELL &&
            cell->type != MDWN_NODE_TABLE_HEADER_CELL)
            continue;

        style = make_style(ctx->theme->table_size_px, ctx->theme->text);
        style.bold = cell->type == MDWN_NODE_TABLE_HEADER_CELL;
        h = layout_inline_box(ctx, cell, 0.0f, 0.0f,
                              fmaxf(col_width - 16.0f, 1.0f), style, false);
        row_height = fmaxf(row_height, h + 16.0f);
        ++col;
    }

    col = 0;
    for (cell = row->first_child; cell && col < columns; cell = cell->next) {
        struct inline_style style;
        float cx;

        if (cell->type != MDWN_NODE_TABLE_CELL &&
            cell->type != MDWN_NODE_TABLE_HEADER_CELL)
            continue;

        cx = x + col_width * (float)col;
        if (cell->type == MDWN_NODE_TABLE_HEADER_CELL)
            add_rect(ctx, cx, y, col_width, row_height,
                     ctx->theme->table_header_background);

        add_line(ctx, cx, y, cx + col_width, y, ctx->theme->border);
        add_line(ctx, cx, y, cx, y + row_height, ctx->theme->border);

        style = make_style(ctx->theme->table_size_px, ctx->theme->text);
        style.bold = cell->type == MDWN_NODE_TABLE_HEADER_CELL;
        (void)layout_inline_box(ctx, cell, cx + 8.0f, y + 8.0f,
                                fmaxf(col_width - 16.0f, 1.0f), style, true);
        ++col;
    }

    add_line(ctx, x + width, y, x + width, y + row_height,
             ctx->theme->border);
    add_line(ctx, x, y + row_height, x + width, y + row_height,
             ctx->theme->border);
    return y + row_height;
}

static float
layout_table_section(struct build_context *ctx, const struct mdwn_node *section,
                     unsigned columns, float x, float width, float y)
{
    const struct mdwn_node *row;

    for (row = section->first_child; row; row = row->next) {
        if (row->type == MDWN_NODE_TABLE_ROW)
            y = layout_table_row(ctx, row, columns, x, width, y);
    }
    return y;
}

static float
layout_table(struct build_context *ctx, const struct mdwn_node *table,
             float x, float width, float y)
{
    const struct mdwn_node *section;
    unsigned columns = table->as.table.columns;

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

    y += 4.0f;
    for (section = table->first_child; section; section = section->next) {
        if (section->type == MDWN_NODE_TABLE_HEAD ||
            section->type == MDWN_NODE_TABLE_BODY)
            y = layout_table_section(ctx, section, columns, x, width, y);
        else if (section->type == MDWN_NODE_TABLE_ROW)
            y = layout_table_row(ctx, section, columns, x, width, y);
    }

    return y + 16.0f;
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
        struct inline_style style = make_style(ctx->theme->text_size_px, color);
        float h = layout_inline_box(ctx, node, x, y, width, style, true);
        return y + h + (compact ? 4.0f : 16.0f);
    }

    case MDWN_NODE_HEADING: {
        unsigned level = node->as.heading.level;
        struct inline_style style;
        float h;
        float border_y;

        if (level < 1 || level > 6)
            level = 1;

        y += 10.0f;
        style = make_style(ctx->theme->heading_size_px[level - 1], color);
        style.bold = true;
        h = layout_inline_box(ctx, node, x, y, width, style, true);
        y += h;

        if (level <= 2) {
            border_y = y + 7.0f;
            add_line(ctx, x, border_y, x + width, border_y,
                     ctx->theme->border);
            y = border_y + 11.0f;
        } else {
            y += 12.0f;
        }
        return y;
    }

    case MDWN_NODE_BLOCKQUOTE: {
        float start = y + 2.0f;
        float child_y = start;
        float inner_x = x + 18.0f;
        float inner_width = fmaxf(width - 18.0f, 1.0f);

        for (child = node->first_child; child; child = child->next)
            child_y = layout_block(ctx, child, inner_x, inner_width,
                                   child_y, ctx->theme->muted, false);

        add_rect(ctx, x, start, 4.0f,
                 fmaxf(child_y - start - 4.0f, 8.0f), ctx->theme->border);
        return child_y + 6.0f;
    }

    case MDWN_NODE_UNORDERED_LIST:
    case MDWN_NODE_ORDERED_LIST:
        return layout_list(ctx, node, x, width, y, color);

    case MDWN_NODE_CODE_BLOCK:
        return layout_code_like_block(ctx, node, x, width, y + 4.0f, false);

    case MDWN_NODE_RAW_HTML_BLOCK:
        return layout_code_like_block(ctx, node, x, width, y + 4.0f, true);

    case MDWN_NODE_HORIZONTAL_RULE:
        y += 10.0f;
        add_line(ctx, x, y, x + width, y, ctx->theme->border);
        return y + 20.0f;

    case MDWN_NODE_TABLE:
        return layout_table(ctx, node, x, width, y);

    case MDWN_NODE_DOCUMENT:
        for (child = node->first_child; child; child = child->next)
            y = layout_block(ctx, child, x, width, y, color, false);
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
