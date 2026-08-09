#include "theme.h"

#include <stddef.h>

static const char *const github_sans_fonts[] = {
    "Mona Sans VF",
    "Segoe UI",
    "Noto Sans",
    "Helvetica",
    "Arial",
    "sans-serif",
    NULL,
};

static const char *const github_mono_fonts[] = {
    "SFMono-Regular",
    "SF Mono",
    "Menlo",
    "Consolas",
    "Liberation Mono",
    "monospace",
    NULL,
};

/* GitHub's Primer CSS markdown bundle in its standard light color mode. */
const struct mdwn_theme mdwn_theme_github = {
    .sans_fonts = github_sans_fonts,
    .mono_fonts = github_mono_fonts,

    .background = { 255, 255, 255, 255 },
    .text = { 31, 35, 40, 255 },
    .muted = { 89, 99, 110, 255 },
    .link = { 9, 105, 218, 255 },
    .border = { 209, 217, 224, 255 },
    .border_muted = { 209, 217, 224, 179 },
    .inline_code_background = { 129, 139, 152, 31 },
    .code_background = { 246, 248, 250, 255 },
    .table_alt_background = { 246, 248, 250, 255 },

    .content_max_width = 1012.0f,
    .outer_margin = 32.0f,
    .top_margin = 32.0f,
    .bottom_margin = 32.0f,
    .text_line_height_scale = 1.5f,
    .heading_line_height_scale = 1.25f,
    .code_line_height_scale = 1.45f,
    .block_spacing = 16.0f,
    .heading_margin_top = 24.0f,
    .heading_margin_bottom = 16.0f,
    .heading_padding_bottom_em = 0.3f,
    .border_radius = 6.0f,
    .inline_code_padding = 5.5f,
    .code_block_padding = 16.0f,
    .blockquote_padding = 16.0f,
    .list_indent = 32.0f,
    .list_item_spacing = 4.0f,
    .rule_margin = 24.0f,
    .rule_height = 4.0f,
    .table_cell_padding_x = 13.0f,
    .table_cell_padding_y = 6.0f,

    .text_size_px = 16,
    .code_size_px = 14,
    .table_size_px = 16,
    .heading_size_px = { 32, 24, 20, 16, 14, 14 },
};
