#ifndef MDWN_THEME_H
#define MDWN_THEME_H

#include <stdint.h>

struct mdwn_color {
    uint8_t r, g, b, a;
};

struct mdwn_theme {
    const char *const *sans_fonts;
    const char *const *mono_fonts;

    struct mdwn_color background;
    struct mdwn_color text;
    struct mdwn_color muted;
    struct mdwn_color link;
    struct mdwn_color border;
    struct mdwn_color border_muted;
    struct mdwn_color inline_code_background;
    struct mdwn_color code_background;
    struct mdwn_color table_alt_background;

    float content_max_width;
    float outer_margin;
    float top_margin;
    float bottom_margin;
    float text_line_height_scale;
    float heading_line_height_scale;
    float code_line_height_scale;
    float block_spacing;
    float heading_margin_top;
    float heading_margin_bottom;
    float heading_padding_bottom_em;
    float border_radius;
    float inline_code_padding;
    float code_block_padding;
    float blockquote_padding;
    float list_indent;
    float list_item_spacing;
    float rule_margin;
    float rule_height;
    float table_cell_padding_x;
    float table_cell_padding_y;

    unsigned text_size_px;
    unsigned code_size_px;
    unsigned table_size_px;
    unsigned heading_size_px[6];
};

extern const struct mdwn_theme mdwn_theme_github;

#endif
