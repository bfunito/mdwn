#ifndef MDWN_THEME_H
#define MDWN_THEME_H

#include <stdint.h>

struct mdwn_color {
    uint8_t r, g, b, a;
};

struct mdwn_theme {
    struct mdwn_color background;
    struct mdwn_color text;
    struct mdwn_color muted;
    struct mdwn_color link;
    struct mdwn_color border;
    struct mdwn_color code_background;
    struct mdwn_color table_header_background;

    float content_max_width;
    float outer_margin;
    float top_margin;
    float bottom_margin;
    float line_height_scale;
    float code_line_height;

    unsigned text_size_px;
    unsigned code_size_px;
    unsigned table_size_px;
    unsigned heading_size_px[6];
};

extern const struct mdwn_theme mdwn_theme_github;

#endif
