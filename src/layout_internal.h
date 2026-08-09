#ifndef MDWN_LAYOUT_INTERNAL_H
#define MDWN_LAYOUT_INTERNAL_H

#include "document.h"
#include "layout.h"

struct build_context {
    struct mdwn_layout *layout;
    struct mdwn_font_system *fonts;
    const struct mdwn_theme *theme;
    char *err;
    size_t err_size;
    int failed;
};

struct inline_style {
    enum mdwn_font_family family;
    unsigned size_px;
    unsigned weight;
    float line_height;
    float letter_spacing_em;
    bool italic;
    bool heading;
    bool strike;
    bool underline;
    bool code_background;
    bool preserve_spaces;
    const char *href;
    struct mdwn_color color;
};

void mdwn_layout_set_error(struct build_context *ctx, const char *message);
struct mdwn_draw_item *mdwn_layout_new_item(struct build_context *ctx,
                                            enum mdwn_draw_type type);
void mdwn_layout_add_rect_with_radius(struct build_context *ctx,
                                      float x, float y, float w, float h,
                                      float radius, struct mdwn_color color);
struct mdwn_font *mdwn_layout_font_for_style(struct build_context *ctx,
                                             struct inline_style style);

struct inline_style mdwn_layout_make_style(struct build_context *ctx,
                                           unsigned size_px,
                                           struct mdwn_color color);
float mdwn_layout_inline_box(struct build_context *ctx,
                             const struct mdwn_node *container,
                             float x, float y, float width,
                             struct inline_style base, bool emit);
float mdwn_layout_measure_inline_width(struct build_context *ctx,
                                       const struct mdwn_node *container,
                                       struct inline_style base);
bool mdwn_layout_is_inline_node(const struct mdwn_node *node);
float mdwn_layout_inline_sequence(struct build_context *ctx,
                                  const struct mdwn_node **node,
                                  float x, float y, float width,
                                  struct mdwn_color color, float line_height);
int mdwn_layout_inline_text_right(struct build_context *ctx,
                                  const char *text, size_t len,
                                  float x, float y, float width,
                                  struct inline_style style);

#endif
