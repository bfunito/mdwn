#include "layout_internal.h"

#include <float.h>
#include <math.h>
#include <string.h>

struct inline_flow {
    struct build_context *ctx;
    float x0;
    float width;
    float x;
    float top;
    float line_top;
    float line_height;
    float baseline_offset;
    float max_width;
    bool emit;
    bool has_content;
};

static float
style_line_height(struct build_context *ctx, struct inline_style style,
                  float *baseline_offset)
{
    struct mdwn_font *font = mdwn_layout_font_for_style(ctx, style);
    float line_height;
    float ascender;
    float descender;

    if (!font)
        return 0.0f;

    line_height = (float)style.size_px * style.line_height;
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
    flow->max_width = fmaxf(flow->max_width, flow->x - flow->x0);
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
    float padding = style.code_background
        ? flow->ctx->theme->inline_code_padding
        : 0.0f;
    float advance;
    float text_x;
    float baseline;
    struct mdwn_draw_item *item;

    if (len == 0 || flow->ctx->failed)
        return flow->ctx->failed ? -1 : 0;

    if (is_space && !flow->has_content)
        return 0;

    font = mdwn_layout_font_for_style(flow->ctx, style);
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
        if (mdwn_font_shape(font, text, len, NULL, NULL, &glyph_count,
                            &width,
                            flow->ctx->err, flow->ctx->err_size) < 0) {
            flow->ctx->failed = 1;
            return -1;
        }
    }

    if (glyph_count && style.letter_spacing_em != 0.0f) {
        float spacing = style.letter_spacing_em * (float)style.size_px;
        size_t i;

        width += spacing * (float)glyph_count;
        for (i = 0; glyphs && i < glyph_count; ++i)
            glyphs[i].x_advance += spacing;
    }

    advance = width + padding * 2.0f;

    if (!is_space && flow->has_content &&
        flow->x + advance > flow->x0 + flow->width) {
        flow_newline(flow);
    }

    if (is_space && flow->x + advance > flow->x0 + flow->width) {
        flow_newline(flow);
        return 0;
    }

    if (style.code_background) {
        baseline = flow->line_top
            + (flow->line_height - mdwn_font_ascender(font)
               - mdwn_font_descender(font)) * 0.5f
            + mdwn_font_ascender(font);
    } else {
        baseline = flow->line_top + flow->baseline_offset;
    }
    text_x = flow->x + padding;

    if (flow->emit && style.code_background)
        mdwn_layout_add_rect_with_radius(
            flow->ctx, flow->x, flow->line_top + 2.0f,
            advance, flow->line_height - 4.0f,
            flow->ctx->theme->border_radius,
            flow->ctx->theme->inline_code_background);

    if (flow->emit && glyph_count) {
        char *stored_text;

        item = mdwn_layout_new_item(flow->ctx, MDWN_DRAW_TEXT);
        if (!item)
            return -1;

        stored_text = mdwn_arena_alloc(&flow->ctx->layout->arena, len);
        if (!stored_text) {
            mdwn_layout_set_error(
                flow->ctx, "out of memory while storing text layout");
            return -1;
        }
        memcpy(stored_text, text, len);

        item->as.text.font = font;
        item->as.text.glyphs = glyphs;
        item->as.text.glyph_count = glyph_count;
        item->as.text.text = stored_text;
        item->as.text.href = style.href;
        item->as.text.text_length = len;
        item->as.text.order = flow->ctx->layout->text_count++;
        item->as.text.x = text_x;
        item->as.text.top = flow->line_top;
        item->as.text.baseline = baseline;
        item->as.text.width = width;
        item->as.text.line_height = flow->line_height;
        item->as.text.color = style.color;
        item->as.text.strike = style.strike;
        item->as.text.underline = style.underline;
    }

    flow->x += advance;
    flow->max_width = fmaxf(flow->max_width, flow->x - flow->x0);
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
        style.weight = flow->ctx->theme->strong_weight;
        break;
    case MDWN_NODE_STRIKETHROUGH:
        style.strike = true;
        break;
    case MDWN_NODE_CODE_SPAN:
        style.family = MDWN_FONT_MONO;
        if (!style.heading)
            style.size_px = flow->ctx->theme->inline_code_size_px;
        style.color = flow->ctx->theme->inline_code_text;
        style.code_background = true;
        style.preserve_spaces = true;
        break;
    case MDWN_NODE_LINK:
        style.color = flow->ctx->theme->link;
        style.href = node->as.link.href;
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

float
mdwn_layout_inline_box(struct build_context *ctx,
                       const struct mdwn_node *container,
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

float
mdwn_layout_measure_inline_width(struct build_context *ctx,
                                 const struct mdwn_node *container,
                                 struct inline_style base)
{
    struct inline_flow flow;
    const struct mdwn_node *child;

    if (flow_init(&flow, ctx, 0.0f, 0.0f, FLT_MAX, base, false) < 0)
        return 0.0f;

    for (child = container->first_child; child; child = child->next) {
        if (layout_inline_node(&flow, child, base) < 0)
            return 0.0f;
    }

    return flow.max_width;
}

struct inline_style
mdwn_layout_make_style(struct build_context *ctx, unsigned size_px,
                       struct mdwn_color color)
{
    struct inline_style style;

    memset(&style, 0, sizeof(style));
    style.family = MDWN_FONT_SANS;
    style.size_px = size_px;
    style.weight = 400;
    style.line_height = ctx->theme->text_line_height_scale;
    style.color = color;
    return style;
}

bool
mdwn_layout_is_inline_node(const struct mdwn_node *node)
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

float
mdwn_layout_inline_sequence(struct build_context *ctx,
                            const struct mdwn_node **node,
                            float x, float y, float width,
                            struct mdwn_color color, float line_height)
{
    struct inline_style style = mdwn_layout_make_style(
        ctx, ctx->theme->text_size_px, color);
    struct inline_flow flow;
    const struct mdwn_node *child = *node;

    style.line_height = line_height;

    if (flow_init(&flow, ctx, x, y, width, style, true) < 0)
        return y;

    while (child && mdwn_layout_is_inline_node(child)) {
        if (layout_inline_node(&flow, child, style) < 0)
            return y;

        child = child->next;
    }

    *node = child;

    return y
        + (flow.line_top - flow.top)
        + flow.line_height;
}

int
mdwn_layout_inline_text_right(struct build_context *ctx,
                              const char *text, size_t len,
                              float x, float y, float width,
                              struct inline_style style)
{
    struct inline_flow flow;
    struct mdwn_draw_item *item;

    if (flow_init(&flow, ctx, x, y, width, style, true) < 0)
        return -1;
    if (flow_place_token(&flow, text, len, style, false) < 0)
        return -1;

    item = ctx->layout->last;
    item->as.text.x += flow.width - item->as.text.width;
    return 0;
}
