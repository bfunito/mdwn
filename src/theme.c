#include "theme.h"

const struct mdwn_theme mdwn_theme_github = {
    .background = { 255, 255, 255, 255 },
    .text = { 31, 35, 40, 255 },
    .muted = { 89, 99, 110, 255 },
    .link = { 9, 105, 218, 255 },
    .border = { 208, 215, 222, 255 },
    .code_background = { 246, 248, 250, 255 },
    .table_header_background = { 246, 248, 250, 255 },

    .content_max_width = 900.0f,
    .outer_margin = 32.0f,
    .top_margin = 36.0f,
    .bottom_margin = 40.0f,
    .line_height_scale = 1.45f,
    .code_line_height = 20.0f,

    .text_size_px = 16,
    .code_size_px = 14,
    .table_size_px = 14,
    .heading_size_px = { 32, 24, 20, 18, 16, 16 },
};
