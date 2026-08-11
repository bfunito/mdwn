#ifndef MDWN_SELECTION_H
#define MDWN_SELECTION_H

#include <stdbool.h>
#include <stddef.h>

struct mdwn_draw_item;
struct mdwn_layout;

struct mdwn_text_position {
    const struct mdwn_draw_item *item;
    size_t offset;
};

enum mdwn_selection_mode {
    MDWN_SELECTION_CHARACTER,
    MDWN_SELECTION_WORD,
    MDWN_SELECTION_LINE,
};

struct mdwn_selection {
    struct mdwn_text_position anchor;
    struct mdwn_text_position focus;
    struct mdwn_text_position origin_start;
    struct mdwn_text_position origin_end;
    enum mdwn_selection_mode mode;
    bool valid;
    bool dragging;
};

bool mdwn_selection_begin(struct mdwn_selection *selection,
                          const struct mdwn_layout *layout,
                          float x, float y, unsigned clicks);
bool mdwn_selection_update(struct mdwn_selection *selection,
                           const struct mdwn_layout *layout,
                           float x, float y);
bool mdwn_selection_select_all(struct mdwn_selection *selection,
                               const struct mdwn_layout *layout);
bool mdwn_selection_item_range(const struct mdwn_selection *selection,
                               const struct mdwn_draw_item *item,
                               size_t *start_offset, size_t *end_offset);
bool mdwn_selection_text_bounds(const struct mdwn_draw_item *item,
                                size_t offset, size_t length,
                                float *x, float *width);
char *mdwn_selection_text(const struct mdwn_selection *selection,
                          const struct mdwn_layout *layout);

#endif
