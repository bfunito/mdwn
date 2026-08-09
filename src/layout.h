#ifndef MDWN_LAYOUT_H
#define MDWN_LAYOUT_H

#include "arena.h"
#include "font.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct mdwn_document;
struct mdwn_font_system;

struct mdwn_color {
    uint8_t r, g, b, a;
};

enum mdwn_draw_type {
    MDWN_DRAW_TEXT,
    MDWN_DRAW_RECT,
    MDWN_DRAW_LINE,
};

struct mdwn_draw_item {
    enum mdwn_draw_type type;
    struct mdwn_draw_item *next;

    union {
        struct {
            struct mdwn_font *font;
            struct mdwn_shaped_glyph *glyphs;
            size_t glyph_count;
            float x;
            float baseline;
            float width;
            float line_height;
            struct mdwn_color color;
            bool strike;
            bool underline;
        } text;

        struct {
            float x, y, w, h;
            struct mdwn_color color;
        } rect;

        struct {
            float x1, y1, x2, y2;
            struct mdwn_color color;
        } line;
    } as;
};

struct mdwn_layout {
    struct mdwn_arena arena;
    struct mdwn_draw_item *first;
    struct mdwn_draw_item *last;
    float content_height;
    int viewport_width;
    int viewport_height;
};

void mdwn_layout_init(struct mdwn_layout *layout);
void mdwn_layout_destroy(struct mdwn_layout *layout);
int mdwn_layout_build(struct mdwn_layout *layout,
                      const struct mdwn_document *doc,
                      struct mdwn_font_system *fonts,
                      int viewport_width, int viewport_height,
                      char *err, size_t err_size);

struct mdwn_color mdwn_layout_background_color(void);

#endif
