#ifndef MDWN_LAYOUT_H
#define MDWN_LAYOUT_H

#include "arena.h"
#include "font.h"
#include "theme.h"

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

struct mdwn_document;
struct mdwn_font_system;
struct mdwn_highlight_cache;
struct mdwn_loaded_image;

struct mdwn_code_block {
    struct mdwn_code_block *next;
    float x, y, w, h;
    float clip_x, clip_w;
    float content_x, viewport_width, content_width;
    float scroll_x;
};

enum mdwn_draw_type {
    MDWN_DRAW_TEXT,
    MDWN_DRAW_IMAGE,
    MDWN_DRAW_RECT,
    MDWN_DRAW_LINE,
};

struct mdwn_draw_item {
    enum mdwn_draw_type type;
    struct mdwn_draw_item *next;

    union {
        struct {
            struct mdwn_font *font;
            TTF_Text *object;
            TTF_Text *render_object;
            const char *href;
            size_t text_length;
            size_t order;
            float x;
            float top;
            float baseline;
            float width;
            float line_height;
            float layout_x_scale;
            float raster_scale;
            float raster_x_scale;
            bool inline_code;
            bool list_marker;
            struct mdwn_color color;
            struct mdwn_code_block *code_block;
        } text;

        struct {
            SDL_Texture *texture;
            const char *href;
            float x, y, w, h;
        } image;

        struct {
            float x, y, w, h;
            float radius;
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
    struct mdwn_code_block *first_code_block;
    struct mdwn_code_block *last_code_block;
    struct mdwn_loaded_image *images;
    size_t text_count;
    float content_width;
    float content_height;
    int viewport_width;
    int viewport_height;
};

struct mdwn_layout_profile {
    uint64_t highlight_ns;
    uint64_t highlight_emit_ns;
    uint64_t text_ns;
    uint64_t image_ns;
    size_t text_runs;
    size_t highlighted_lines;
    size_t highlighted_blocks;
    size_t highlight_cache_hits;
    size_t highlight_cache_misses;
    size_t images_loaded;
    size_t image_cache_hits;
};

float mdwn_layout_text_x(const struct mdwn_draw_item *item);

void mdwn_layout_init(struct mdwn_layout *layout);
void mdwn_layout_destroy(struct mdwn_layout *layout);
void mdwn_layout_take_images(struct mdwn_layout *layout,
                             struct mdwn_layout *source);
void mdwn_layout_clear_render_text(struct mdwn_layout *layout);
int mdwn_layout_build(struct mdwn_layout *layout,
                      const struct mdwn_document *doc,
                      struct mdwn_font_system *fonts,
                      const struct mdwn_theme *theme,
                      SDL_Renderer *renderer, const char *document_path,
                      int viewport_width, int viewport_height,
                      struct mdwn_highlight_cache *highlights,
                      struct mdwn_layout_profile *profile,
                      char *err, size_t err_size);

#endif
