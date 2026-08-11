#include "selection.h"

#include "layout.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int
compare_positions(struct mdwn_text_position a, struct mdwn_text_position b)
{
    if (a.item->as.text.order < b.item->as.text.order)
        return -1;
    if (a.item->as.text.order > b.item->as.text.order)
        return 1;
    if (a.offset < b.offset)
        return -1;
    if (a.offset > b.offset)
        return 1;
    return 0;
}

static bool
selection_range(const struct mdwn_selection *selection,
                struct mdwn_text_position *start,
                struct mdwn_text_position *end)
{
    if (!selection->valid ||
        compare_positions(selection->anchor, selection->focus) == 0)
        return false;

    if (compare_positions(selection->anchor, selection->focus) < 0) {
        *start = selection->anchor;
        *end = selection->focus;
    } else {
        *start = selection->focus;
        *end = selection->anchor;
    }
    return true;
}

bool
mdwn_selection_item_range(const struct mdwn_selection *selection,
                          const struct mdwn_draw_item *item,
                          size_t *start_offset, size_t *end_offset)
{
    struct mdwn_text_position start, end;
    size_t order = item->as.text.order;

    if (!selection_range(selection, &start, &end) ||
        order < start.item->as.text.order ||
        order > end.item->as.text.order)
        return false;

    *start_offset = order == start.item->as.text.order ? start.offset : 0;
    *end_offset = order == end.item->as.text.order
        ? end.offset
        : item->as.text.text_length;
    return *start_offset < *end_offset;
}

static bool
substring_at_x(const struct mdwn_draw_item *item, float x,
               TTF_SubString *substring, int *local_x)
{
    int point = (int)roundf(
        (x - mdwn_layout_text_x(item)) / item->as.text.layout_x_scale);

    if (local_x)
        *local_x = point;
    return TTF_GetTextSubStringForPoint(item->as.text.object,
                                        point, 0, substring);
}

static size_t
cursor_offset_at(const struct mdwn_draw_item *item, float x)
{
    TTF_SubString substring;
    int local_x;
    size_t offset;
    bool before;

    if (!substring_at_x(item, x, &substring, &local_x))
        return 0;
    if (substring.flags &
        (TTF_SUBSTRING_LINE_END | TTF_SUBSTRING_TEXT_END))
        return (size_t)substring.offset;

    before = local_x < substring.rect.x + substring.rect.w / 2;
    if ((substring.flags & TTF_SUBSTRING_DIRECTION_MASK) == TTF_DIRECTION_RTL)
        before = !before;

    offset = (size_t)substring.offset;
    if (!before)
        offset += (size_t)substring.length;
    return offset < item->as.text.text_length
        ? offset
        : item->as.text.text_length;
}

static bool
find_text_position(const struct mdwn_layout *layout, float x, float y,
                   bool nearest, struct mdwn_text_position *position)
{
    const struct mdwn_draw_item *item;
    const struct mdwn_draw_item *best = NULL;
    float best_dx = FLT_MAX;
    float best_dy = FLT_MAX;

    for (item = layout->first; item; item = item->next) {
        float left, right, top, bottom;
        float dx, dy;

        if (item->type != MDWN_DRAW_TEXT)
            continue;

        left = mdwn_layout_text_x(item);
        right = left + item->as.text.width;
        top = item->as.text.top;
        bottom = top + item->as.text.line_height;

        if (item->as.text.code_block) {
            const struct mdwn_code_block *block = item->as.text.code_block;

            left = fmaxf(left, block->clip_x);
            right = fminf(right, block->clip_x + block->clip_w);
            if (left > right)
                continue;
        }

        dx = x < left ? left - x : x > right ? x - right : 0.0f;
        dy = y < top ? top - y : y > bottom ? y - bottom : 0.0f;
        if (!nearest && dy > 0.0f)
            continue;

        if (dy < best_dy || (dy == best_dy && dx < best_dx)) {
            best = item;
            best_dx = dx;
            best_dy = dy;
        }
    }

    if (!best)
        return false;

    position->item = best;
    position->offset = cursor_offset_at(best, x);
    return true;
}

static bool
same_text_line(const struct mdwn_draw_item *a,
               const struct mdwn_draw_item *b)
{
    return a->as.text.top < b->as.text.top + b->as.text.line_height &&
           b->as.text.top < a->as.text.top + a->as.text.line_height;
}

static size_t
next_character(const char *text, size_t length, size_t offset)
{
    if (offset < length)
        ++offset;
    while (offset < length &&
           ((unsigned char)text[offset] & 0xc0u) == 0x80u)
        ++offset;
    return offset;
}

static size_t
previous_character(const char *text, size_t offset)
{
    if (offset > 0)
        --offset;
    while (offset > 0 &&
           ((unsigned char)text[offset] & 0xc0u) == 0x80u)
        --offset;
    return offset;
}

static bool
is_word_character(const char *text, size_t length, size_t offset)
{
    unsigned char c;

    if (offset >= length)
        return false;

    c = (unsigned char)text[offset];
    return c >= 0x80u ||
           (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') ||
           c == '_';
}

static size_t
character_at_x(const struct mdwn_draw_item *item, float x)
{
    TTF_SubString substring;
    size_t offset;

    if (!substring_at_x(item, x, &substring, NULL))
        return 0;
    offset = (size_t)substring.offset;
    if (offset >= item->as.text.text_length && offset > 0) {
        if (!TTF_GetTextSubString(item->as.text.object, (int)offset - 1,
                                  &substring))
            return 0;
        offset = (size_t)substring.offset;
    }
    return offset;
}

static bool
word_range(const struct mdwn_draw_item *item, float x,
           struct mdwn_text_position *start, struct mdwn_text_position *end)
{
    const char *text = item->as.text.object->text;
    size_t length = item->as.text.text_length;
    size_t offset = character_at_x(item, x);
    size_t start_offset, end_offset;

    if (!is_word_character(text, length, offset)) {
        if (offset > 0 &&
            is_word_character(text, length,
                              previous_character(text, offset))) {
            offset = previous_character(text, offset);
        } else {
            offset = next_character(text, length, offset);
            while (offset < length &&
                   !is_word_character(text, length, offset))
                offset = next_character(text, length, offset);
        }
    }
    if (offset >= length)
        return false;

    start_offset = offset;
    while (start_offset > 0) {
        size_t previous = previous_character(text, start_offset);

        if (!is_word_character(text, length, previous))
            break;
        start_offset = previous;
    }

    end_offset = next_character(text, length, offset);
    while (end_offset < length &&
           is_word_character(text, length, end_offset))
        end_offset = next_character(text, length, end_offset);

    start->item = item;
    start->offset = start_offset;
    end->item = item;
    end->offset = end_offset;
    return true;
}

static void
line_range(const struct mdwn_layout *layout,
           const struct mdwn_draw_item *clicked,
           struct mdwn_text_position *start, struct mdwn_text_position *end)
{
    const struct mdwn_draw_item *item;
    const struct mdwn_draw_item *first = NULL;
    const struct mdwn_draw_item *last = NULL;
    bool found = false;

    for (item = layout->first; item; item = item->next) {
        if (item->type != MDWN_DRAW_TEXT)
            continue;

        if (item == clicked) {
            if (!first)
                first = item;
            last = item;
            found = true;
        } else if (!found && same_text_line(clicked, item)) {
            if (!first)
                first = item;
        } else if (!found) {
            first = NULL;
        } else if (same_text_line(clicked, item)) {
            last = item;
        } else {
            break;
        }
    }

    start->item = first;
    start->offset = 0;
    end->item = last;
    end->offset = last->as.text.text_length;
}

bool
mdwn_selection_begin(struct mdwn_selection *selection,
                     const struct mdwn_layout *layout,
                     float x, float y, unsigned clicks)
{
    struct mdwn_text_position position;
    struct mdwn_text_position start, end;

    selection->dragging = false;
    if (!find_text_position(layout, x, y, false, &position)) {
        selection->valid = false;
        return false;
    }

    if (clicks >= 3) {
        line_range(layout, position.item, &start, &end);
        selection->mode = MDWN_SELECTION_LINE;
    } else if (clicks == 2) {
        if (!word_range(position.item, x, &start, &end)) {
            selection->valid = false;
            return false;
        }
        selection->mode = MDWN_SELECTION_WORD;
    } else {
        start = position;
        end = position;
        selection->mode = MDWN_SELECTION_CHARACTER;
    }

    selection->anchor = start;
    selection->focus = end;
    selection->origin_start = start;
    selection->origin_end = end;
    selection->valid = true;
    selection->dragging = true;
    return true;
}

bool
mdwn_selection_update(struct mdwn_selection *selection,
                      const struct mdwn_layout *layout,
                      float x, float y)
{
    struct mdwn_text_position position;
    struct mdwn_text_position start, end;

    if (!find_text_position(layout, x, y, true, &position))
        return false;

    switch (selection->mode) {
    case MDWN_SELECTION_WORD:
        if (!word_range(position.item, x, &start, &end))
            return false;
        break;
    case MDWN_SELECTION_LINE:
        line_range(layout, position.item, &start, &end);
        break;
    default:
        start = position;
        end = position;
        break;
    }

    if (compare_positions(start, selection->origin_start) < 0) {
        selection->anchor = selection->origin_end;
        selection->focus = start;
    } else {
        selection->anchor = selection->origin_start;
        selection->focus = end;
    }
    return true;
}

bool
mdwn_selection_select_all(struct mdwn_selection *selection,
                          const struct mdwn_layout *layout)
{
    const struct mdwn_draw_item *item;
    const struct mdwn_draw_item *first = NULL;
    const struct mdwn_draw_item *last = NULL;

    for (item = layout->first; item; item = item->next) {
        if (item->type == MDWN_DRAW_TEXT) {
            if (!first)
                first = item;
            last = item;
        }
    }

    if (!first)
        return false;

    selection->anchor.item = first;
    selection->anchor.offset = 0;
    selection->focus.item = last;
    selection->focus.offset = last->as.text.text_length;
    selection->valid = true;
    return true;
}

char *
mdwn_selection_text(const struct mdwn_selection *selection,
                    const struct mdwn_layout *layout)
{
    const struct mdwn_draw_item *item;
    const struct mdwn_draw_item *previous = NULL;
    size_t total = 0;
    char *text, *dst;

    for (item = layout->first; item; item = item->next) {
        size_t start, end;

        if (item->type != MDWN_DRAW_TEXT ||
            !mdwn_selection_item_range(selection, item, &start, &end))
            continue;
        if (previous && !same_text_line(previous, item)) {
            if (total == SIZE_MAX)
                return NULL;
            ++total;
        }
        if (end - start > SIZE_MAX - total)
            return NULL;
        total += end - start;
        previous = item;
    }

    if (total == 0 || total == SIZE_MAX)
        return NULL;

    text = malloc(total + 1);
    if (!text)
        return NULL;

    dst = text;
    previous = NULL;
    for (item = layout->first; item; item = item->next) {
        size_t start, end;

        if (item->type != MDWN_DRAW_TEXT ||
            !mdwn_selection_item_range(selection, item, &start, &end))
            continue;
        if (previous && !same_text_line(previous, item))
            *dst++ = '\n';
        memcpy(dst, item->as.text.object->text + start, end - start);
        dst += end - start;
        previous = item;
    }
    *dst = '\0';
    return text;
}

bool
mdwn_selection_text_bounds(const struct mdwn_draw_item *item,
                           size_t offset, size_t length,
                           float *x, float *width)
{
    TTF_SubString **substrings;
    int i;
    int left = INT_MAX;
    int right = INT_MIN;

    if (offset > INT_MAX || length > (size_t)INT_MAX - offset)
        return false;

    substrings = TTF_GetTextSubStringsForRange(
        item->as.text.object, (int)offset, (int)length, NULL);
    if (!substrings)
        return false;

    for (i = 0; substrings[i]; ++i) {
        if (substrings[i]->rect.x < left)
            left = substrings[i]->rect.x;
        if (substrings[i]->rect.x + substrings[i]->rect.w > right)
            right = substrings[i]->rect.x + substrings[i]->rect.w;
    }
    SDL_free(substrings);

    if (left == INT_MAX)
        return false;
    *x = mdwn_layout_text_x(item)
        + (float)left * item->as.text.layout_x_scale;
    *width = (float)(right - left) * item->as.text.layout_x_scale;
    return true;
}
