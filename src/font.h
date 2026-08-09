#ifndef MDWN_FONT_H
#define MDWN_FONT_H

#include "arena.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct mdwn_font_system;
struct mdwn_font;
struct mdwn_theme;

enum mdwn_font_family {
    MDWN_FONT_SANS,
    MDWN_FONT_MONO,
};

struct mdwn_font_spec {
    enum mdwn_font_family family;
    unsigned size_px;
    unsigned weight;
    bool italic;
};

struct mdwn_shaped_glyph {
    uint32_t index;
    uint32_t cluster;
    float x_advance;
    float y_advance;
    float x_offset;
    float y_offset;
};

enum mdwn_bitmap_format {
    MDWN_BITMAP_GRAY,
    MDWN_BITMAP_MONO,
};

struct mdwn_glyph_bitmap {
    const unsigned char *buffer;
    int width;
    int height;
    int pitch;
    int left;
    int top;
    unsigned num_grays;
    enum mdwn_bitmap_format format;
};

int mdwn_font_system_create(struct mdwn_font_system **out,
                            const struct mdwn_theme *theme,
                            char *err, size_t err_size);
void mdwn_font_system_destroy(struct mdwn_font_system *system);

struct mdwn_font *mdwn_font_get(struct mdwn_font_system *system,
                                struct mdwn_font_spec spec,
                                char *err, size_t err_size);
struct mdwn_font *mdwn_font_get_scaled(struct mdwn_font_system *system,
                                       const struct mdwn_font *font,
                                       float scale, float *actual_scale,
                                       char *err, size_t err_size);
float mdwn_font_ascender(const struct mdwn_font *font);
float mdwn_font_descender(const struct mdwn_font *font);

int mdwn_font_shape(struct mdwn_font *font,
                    const char *text, size_t len,
                    struct mdwn_arena *arena,
                    struct mdwn_shaped_glyph **glyphs,
                    size_t *glyph_count,
                    float *width,
                    char *err, size_t err_size);

int mdwn_font_render_glyph(struct mdwn_font *font, uint32_t glyph_index,
                           struct mdwn_glyph_bitmap *bitmap,
                           char *err, size_t err_size);

#endif
