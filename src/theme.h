#ifndef MDWN_THEME_H
#define MDWN_THEME_H

#include <stdint.h>

struct mdwn_color {
    uint8_t r, g, b, a;
};

struct mdwn_syntax_colors {
    struct mdwn_color comment;
    struct mdwn_color keyword;
    struct mdwn_color type;
    struct mdwn_color string;
    struct mdwn_color regexp;
    struct mdwn_color special_char;
    struct mdwn_color number;
    struct mdwn_color preprocessor;
    struct mdwn_color symbol;
    struct mdwn_color function;
    struct mdwn_color class_name;
    struct mdwn_color variable;
    struct mdwn_color builtin;
};

struct mdwn_theme {
    const char *const *sans_fonts;
    const char *const *mono_fonts;

    struct mdwn_color background;
    struct mdwn_color text;
    struct mdwn_color heading;
    struct mdwn_color heading_small;
    struct mdwn_color inline_code_text;
    struct mdwn_color muted;
    struct mdwn_color link;
    struct mdwn_color border;
    struct mdwn_color border_muted;
    struct mdwn_color blockquote_border;
    struct mdwn_color inline_code_background;
    struct mdwn_color code_background;
    struct mdwn_color code_border;
    struct mdwn_color table_alt_background;
    struct mdwn_syntax_colors syntax;

    float content_max_width;
    float outer_margin;
    float top_margin;
    float bottom_margin;
    float text_line_height_scale;
    float heading_line_height_scale;
    float code_line_height_scale;
    float list_line_height_scale;
    float table_line_height_scale;
    float block_spacing;
    float heading_margin_top[6];
    float heading_letter_spacing_em[6];
    float heading_margin_bottom;
    float heading_padding_bottom_em;
    float border_radius;
    float inline_code_padding;
    float code_block_padding;
    float code_border_width;
    float blockquote_padding;
    float blockquote_padding_right;
    float blockquote_padding_y;
    float blockquote_border_width;
    float blockquote_margin;
    float blockquote_margin_right;
    float list_indent;
    float list_item_spacing;
    float rule_margin;
    float rule_height;
    float table_cell_padding_x;
    float table_cell_padding_y;

    unsigned text_size_px;
    unsigned inline_code_size_px;
    unsigned code_size_px;
    unsigned table_size_px;
    unsigned heading_size_px[6];
    unsigned strong_weight;
    unsigned heading_weight;
    unsigned table_header_weight;
};

extern const struct mdwn_theme mdwn_theme_github;
extern const struct mdwn_theme mdwn_theme_github_dark;
extern const struct mdwn_theme mdwn_theme_gitlab;
extern const struct mdwn_theme mdwn_theme_gitlab_dark;
extern const struct mdwn_theme mdwn_theme_codeberg;
extern const struct mdwn_theme mdwn_theme_codeberg_dark;

#endif
