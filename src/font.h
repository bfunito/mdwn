#ifndef MDWN_FONT_H
#define MDWN_FONT_H

#include <SDL3_ttf/SDL_ttf.h>

#include <stdbool.h>
#include <stddef.h>

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
    bool strike;
    bool underline;
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
TTF_Font *mdwn_font_handle(struct mdwn_font *font);
float mdwn_font_ascender(const struct mdwn_font *font);
float mdwn_font_descender(const struct mdwn_font *font);

TTF_Text *mdwn_font_create_text(struct mdwn_font *font,
                                const char *text, size_t len,
                                float *width, size_t *cluster_count,
                                char *err, size_t err_size);

#endif
