#include "render.h"

#include "document.h"
#include "font.h"
#include "layout.h"
#include "selection.h"
#include "theme.h"

#include <SDL3/SDL.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_WIDTH 960
#define INITIAL_HEIGHT 720
#define SCROLL_STEP 48.0f
#define MIN_ZOOM 1.0f
#define MAX_ZOOM 5.0f
#define WHEEL_ZOOM_STEP 1.1f
#define ZOOM_SENSITIVITY 0.5f
#define RASTER_ZOOM_STEP 0.25f

struct glyph_texture {
    struct mdwn_font *font;
    uint32_t glyph_index;
    SDL_Texture *texture;
    int width;
    int height;
    int left;
    int top;
};

struct glyph_cache {
    struct glyph_texture *items;
    size_t count;
    size_t capacity;
};

struct viewer {
    SDL_Window *window;
    SDL_Renderer *renderer;
    struct mdwn_font_system *fonts;
    struct mdwn_layout layout;
    struct glyph_cache glyphs;
    struct mdwn_selection selection;
    const struct mdwn_document *doc;
    const struct mdwn_theme *theme;
    float zoom;
    float raster_zoom;
    float scroll_x;
    float scroll_y;
    int width;
    int height;
    bool dirty;
    bool quit;
    const char *pressed_link;
    SDL_Cursor *default_cursor;
    SDL_Cursor *text_cursor;
    SDL_Cursor *link_cursor;
    char *err;
    size_t err_size;
};

static void set_sdl_error(struct viewer *, const char *);

static float
document_x(const struct viewer *viewer, float x)
{
    return viewer->scroll_x + x / viewer->zoom;
}

static float
document_y(const struct viewer *viewer, float y)
{
    return viewer->scroll_y + y / viewer->zoom;
}

static const struct mdwn_draw_item *
text_at(const struct mdwn_layout *layout, float x, float y)
{
    const struct mdwn_draw_item *item;
    const struct mdwn_draw_item *text = NULL;

    for (item = layout->first; item; item = item->next) {
        if (item->type == MDWN_DRAW_TEXT &&
            x >= item->as.text.x &&
            x <= item->as.text.x + item->as.text.width &&
            y >= item->as.text.top &&
            y <= item->as.text.top + item->as.text.line_height) {
            if (item->as.text.href)
                return item;
            text = item;
        }
    }
    return text;
}

static const char *
link_at(const struct mdwn_layout *layout, float x, float y)
{
    const struct mdwn_draw_item *item = text_at(layout, x, y);

    return item ? item->as.text.href : NULL;
}

static void
update_cursor(struct viewer *viewer, float x, float y)
{
    const struct mdwn_draw_item *item = text_at(
        &viewer->layout, document_x(viewer, x), document_y(viewer, y));
    SDL_Cursor *cursor = viewer->default_cursor;

    if (item)
        cursor = item->as.text.href
            ? viewer->link_cursor
            : viewer->text_cursor;
    if (cursor != SDL_GetCursor())
        (void)SDL_SetCursor(cursor);
}

static void
update_cursor_at_mouse(struct viewer *viewer)
{
    float x, y;

    (void)SDL_GetMouseState(&x, &y);
    update_cursor(viewer, x, y);
}

static bool
selection_is_empty(const struct mdwn_selection *selection)
{
    return selection->valid &&
           selection->anchor.item == selection->focus.item &&
           selection->anchor.offset == selection->focus.offset;
}

static int
copy_selection(struct viewer *viewer, bool primary)
{
    char *text = mdwn_selection_text(&viewer->selection, &viewer->layout);
    bool copied;

    if (!text)
        return 0;

    copied = primary
        ? SDL_SetPrimarySelectionText(text)
        : SDL_SetClipboardText(text);
    free(text);

    if (!copied) {
        set_sdl_error(viewer, "could not copy selected text");
        return -1;
    }
    return 0;
}

static void
set_error(struct viewer *viewer, const char *message)
{
    if (viewer->err && viewer->err_size)
        snprintf(viewer->err, viewer->err_size, "%s", message);
}

static void
set_sdl_error(struct viewer *viewer, const char *operation)
{
    if (viewer->err && viewer->err_size)
        snprintf(viewer->err, viewer->err_size, "%s: %s", operation, SDL_GetError());
}

static void
set_draw_color(SDL_Renderer *renderer, struct mdwn_color color)
{
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

static bool
fill_rounded_rect(SDL_Renderer *renderer, const SDL_FRect *rect, float radius)
{
    SDL_FRect middle;
    int rows;
    int i;

    radius = fminf(radius, fminf(rect->w, rect->h) * 0.5f);
    if (radius < 1.0f)
        return SDL_RenderFillRect(renderer, rect);

    middle.x = rect->x;
    middle.y = rect->y + radius;
    middle.w = rect->w;
    middle.h = rect->h - radius * 2.0f;
    if (middle.h > 0.0f && !SDL_RenderFillRect(renderer, &middle))
        return false;

    rows = (int)ceilf(radius);
    for (i = 0; i < rows; ++i) {
        float offset_y = (float)i + 0.5f;
        float distance = fmaxf(radius - offset_y, 0.0f);
        float inset = radius - sqrtf(radius * radius - distance * distance);
        SDL_FRect row;

        row.x = rect->x + inset;
        row.y = rect->y + (float)i;
        row.w = rect->w - inset * 2.0f;
        row.h = 1.0f;
        if (!SDL_RenderFillRect(renderer, &row))
            return false;

        row.y = rect->y + rect->h - (float)i - 1.0f;
        if (!SDL_RenderFillRect(renderer, &row))
            return false;
    }
    return true;
}

static void
glyph_cache_destroy(struct glyph_cache *cache)
{
    size_t i;

    for (i = 0; i < cache->count; ++i) {
        if (cache->items[i].texture)
            SDL_DestroyTexture(cache->items[i].texture);
    }

    free(cache->items);
    memset(cache, 0, sizeof(*cache));
}

static struct glyph_texture *
glyph_cache_find(struct glyph_cache *cache, struct mdwn_font *font,
                 uint32_t glyph_index)
{
    size_t i;

    for (i = 0; i < cache->count; ++i) {
        if (cache->items[i].font == font &&
            cache->items[i].glyph_index == glyph_index)
            return &cache->items[i];
    }
    return NULL;
}

static int
grow_glyph_cache(struct viewer *viewer)
{
    size_t capacity = viewer->glyphs.capacity ? viewer->glyphs.capacity * 2 : 128;
    struct glyph_texture *items;

    if (capacity < viewer->glyphs.capacity ||
        capacity > SIZE_MAX / sizeof(*items)) {
        set_error(viewer, "glyph cache is too large");
        return -1;
    }

    items = realloc(viewer->glyphs.items, capacity * sizeof(*items));
    if (!items) {
        set_error(viewer, "out of memory while growing glyph cache");
        return -1;
    }

    viewer->glyphs.items = items;
    viewer->glyphs.capacity = capacity;
    return 0;
}

static unsigned char
gray_alpha(const struct mdwn_glyph_bitmap *bitmap, const unsigned char *row, int x)
{
    if (bitmap->format == MDWN_BITMAP_MONO) {
        unsigned char byte = row[x >> 3];
        unsigned mask = 0x80u >> (unsigned)(x & 7);
        return (byte & mask) ? 255 : 0;
    }

    if (bitmap->num_grays > 1 && bitmap->num_grays != 256)
        return (unsigned char)((unsigned)row[x] * 255u / (bitmap->num_grays - 1u));
    return row[x];
}

static struct glyph_texture *
glyph_cache_create(struct viewer *viewer, struct mdwn_font *font,
                   uint32_t glyph_index)
{
    struct mdwn_glyph_bitmap bitmap;
    struct glyph_texture *entry;
    unsigned char *pixels = NULL;
    SDL_Texture *texture = NULL;
    size_t pixel_count;
    int y, x;

    if (mdwn_font_render_glyph(font, glyph_index, &bitmap,
                               viewer->err, viewer->err_size) < 0)
        return NULL;

    if (viewer->glyphs.count == viewer->glyphs.capacity && grow_glyph_cache(viewer) < 0)
        return NULL;

    entry = &viewer->glyphs.items[viewer->glyphs.count++];
    memset(entry, 0, sizeof(*entry));
    entry->font = font;
    entry->glyph_index = glyph_index;
    entry->width = bitmap.width;
    entry->height = bitmap.height;
    entry->left = bitmap.left;
    entry->top = bitmap.top;

    if (bitmap.width <= 0 || bitmap.height <= 0)
        return entry;

    if ((size_t)bitmap.width > SIZE_MAX / (size_t)bitmap.height / 4u) {
        set_error(viewer, "glyph bitmap is too large");
        return NULL;
    }

    pixel_count = (size_t)bitmap.width * (size_t)bitmap.height;
    pixels = malloc(pixel_count * 4u);
    if (!pixels) {
        set_error(viewer, "out of memory while rasterizing glyph");
        return NULL;
    }

    for (y = 0; y < bitmap.height; ++y) {
        const unsigned char *row;

        if (bitmap.pitch >= 0)
            row = bitmap.buffer + (size_t)y * (size_t)bitmap.pitch;
        else
            row = bitmap.buffer + (size_t)(bitmap.height - 1 - y) * (size_t)(-bitmap.pitch);

        for (x = 0; x < bitmap.width; ++x) {
            size_t p = ((size_t)y * (size_t)bitmap.width + (size_t)x) * 4u;
            unsigned char alpha = gray_alpha(&bitmap, row, x);
            pixels[p + 0] = 255;
            pixels[p + 1] = 255;
            pixels[p + 2] = 255;
            pixels[p + 3] = alpha;
        }
    }

    texture = SDL_CreateTexture(viewer->renderer, SDL_PIXELFORMAT_RGBA32,
                                SDL_TEXTUREACCESS_STATIC,
                                bitmap.width, bitmap.height);
    if (!texture) {
        free(pixels);
        set_sdl_error(viewer, "could not create glyph texture");
        return NULL;
    }

    if (!SDL_UpdateTexture(texture, NULL, pixels, bitmap.width * 4)) {
        SDL_DestroyTexture(texture);
        free(pixels);
        set_sdl_error(viewer, "could not upload glyph texture");
        return NULL;
    }
    free(pixels);

    if (!SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND)) {
        SDL_DestroyTexture(texture);
        set_sdl_error(viewer, "could not configure glyph texture");
        return NULL;
    }

    entry->texture = texture;
    return entry;
}

static struct glyph_texture *
get_glyph(struct viewer *viewer, struct mdwn_font *font, uint32_t glyph_index)
{
    struct glyph_texture *glyph = glyph_cache_find(&viewer->glyphs, font, glyph_index);
    return glyph ? glyph : glyph_cache_create(viewer, font, glyph_index);
}

static int
draw_text(struct viewer *viewer, const struct mdwn_draw_item *item)
{
    const struct mdwn_color color = item->as.text.color;
    struct mdwn_font *font;
    float font_scale;
    float pen_x = item->as.text.x - viewer->scroll_x;
    float pen_y = item->as.text.baseline - viewer->scroll_y;
    float viewport_width = (float)viewer->width / viewer->zoom;
    float viewport_height = (float)viewer->height / viewer->zoom;
    size_t i;

    font = mdwn_font_get_scaled(viewer->fonts, item->as.text.font,
                                viewer->raster_zoom, &font_scale,
                                viewer->err, viewer->err_size);
    if (!font)
        return -1;

    for (i = 0; i < item->as.text.glyph_count; ++i) {
        const struct mdwn_shaped_glyph *g = &item->as.text.glyphs[i];
        if (pen_x + item->as.text.line_height >= 0.0f &&
            pen_x <= viewport_width) {
            struct glyph_texture *texture = get_glyph(viewer, font, g->index);

            if (!texture)
                return -1;

            if (texture->texture && texture->width > 0 && texture->height > 0) {
                SDL_FRect dst;

                dst.x = roundf((pen_x + g->x_offset
                                + (float)texture->left / font_scale)
                               * viewer->zoom) / viewer->zoom;
                dst.y = roundf((pen_y - g->y_offset
                                - (float)texture->top / font_scale)
                               * viewer->zoom) / viewer->zoom;
                dst.w = (float)texture->width / font_scale;
                dst.h = (float)texture->height / font_scale;

                if (dst.y + dst.h >= 0.0f && dst.y <= viewport_height) {
                    if (!SDL_SetTextureColorMod(texture->texture,
                                                color.r, color.g, color.b) ||
                        !SDL_SetTextureAlphaMod(texture->texture, color.a) ||
                        !SDL_RenderTexture(viewer->renderer,
                                           texture->texture, NULL, &dst)) {
                        set_sdl_error(viewer, "could not render glyph");
                        return -1;
                    }
                }
            }
        }

        pen_x += g->x_advance;
        pen_y -= g->y_advance;
    }

    set_draw_color(viewer->renderer, color);
    if (item->as.text.strike) {
        float y = item->as.text.baseline - viewer->scroll_y - item->as.text.line_height * 0.20f;
        if (!SDL_RenderLine(viewer->renderer,
                            item->as.text.x - viewer->scroll_x, y,
                            item->as.text.x + item->as.text.width
                                - viewer->scroll_x, y)) {
            set_sdl_error(viewer, "could not render strikethrough");
            return -1;
        }
    }
    if (item->as.text.underline) {
        float y = item->as.text.baseline - viewer->scroll_y + 2.0f;
        if (!SDL_RenderLine(viewer->renderer,
                            item->as.text.x - viewer->scroll_x, y,
                            item->as.text.x + item->as.text.width
                                - viewer->scroll_x, y)) {
            set_sdl_error(viewer, "could not render underline");
            return -1;
        }
    }

    return 0;
}

static int
draw_text_selection(struct viewer *viewer,
                    const struct mdwn_draw_item *item)
{
    struct mdwn_color color = { 51, 132, 255, 110 };
    size_t start, end;
    float x1, x2;
    SDL_FRect rect;

    if (!mdwn_selection_item_range(&viewer->selection, item, &start, &end))
        return 0;

    x1 = mdwn_selection_text_x_at(item, start);
    x2 = mdwn_selection_text_x_at(item, end);
    rect.x = fminf(x1, x2) - viewer->scroll_x;
    rect.y = item->as.text.top - viewer->scroll_y;
    rect.w = fabsf(x2 - x1);
    rect.h = item->as.text.line_height;

    if (rect.w <= 0.0f || rect.y + rect.h < 0.0f ||
        rect.y > (float)viewer->height / viewer->zoom)
        return 0;

    set_draw_color(viewer->renderer, color);
    if (!SDL_RenderFillRect(viewer->renderer, &rect)) {
        set_sdl_error(viewer, "could not render text selection");
        return -1;
    }
    return 0;
}

static int
draw_scrollbar(struct viewer *viewer, bool horizontal)
{
    float window_size = horizontal
        ? (float)viewer->width
        : (float)viewer->height;
    float content = horizontal
        ? viewer->layout.content_width
        : viewer->layout.content_height;
    float viewport = window_size / viewer->zoom;
    float offset = horizontal ? viewer->scroll_x : viewer->scroll_y;
    float bar_size;
    float travel;
    float max_scroll;
    float position;
    SDL_FRect rect;
    struct mdwn_color color = { 139, 148, 158, 120 };

    if (content <= viewport || window_size <= 0.0f)
        return 0;

    max_scroll = content - viewport;
    bar_size = fmaxf(32.0f, window_size * viewport / content);
    travel = window_size - bar_size;
    position = offset / max_scroll * travel;

    if (horizontal) {
        rect.x = position + 2.0f;
        rect.y = (float)viewer->height - 6.0f;
        rect.w = fmaxf(bar_size - 4.0f, 4.0f);
        rect.h = 4.0f;
    } else {
        rect.x = (float)viewer->width - 6.0f;
        rect.y = position + 2.0f;
        rect.w = 4.0f;
        rect.h = fmaxf(bar_size - 4.0f, 4.0f);
    }

    set_draw_color(viewer->renderer, color);
    if (!SDL_RenderFillRect(viewer->renderer, &rect)) {
        set_sdl_error(viewer, "could not render scrollbar");
        return -1;
    }
    return 0;
}

static int
render_frame(struct viewer *viewer)
{
    const struct mdwn_draw_item *item;
    struct mdwn_color background = viewer->theme->background;
    float viewport_width = (float)viewer->width / viewer->zoom;
    float viewport_height = (float)viewer->height / viewer->zoom;

    set_draw_color(viewer->renderer, background);
    if (!SDL_RenderClear(viewer->renderer)) {
        set_sdl_error(viewer, "could not clear window");
        return -1;
    }

    if (!SDL_SetRenderScale(viewer->renderer, viewer->zoom, viewer->zoom)) {
        set_sdl_error(viewer, "could not set document zoom");
        return -1;
    }

    for (item = viewer->layout.first; item; item = item->next) {
        switch (item->type) {
        case MDWN_DRAW_RECT: {
            SDL_FRect rect;
            rect.x = item->as.rect.x - viewer->scroll_x;
            rect.y = item->as.rect.y - viewer->scroll_y;
            rect.w = item->as.rect.w;
            rect.h = item->as.rect.h;

            if (rect.y + rect.h < 0.0f || rect.y > viewport_height)
                break;
            set_draw_color(viewer->renderer, item->as.rect.color);
            if (!fill_rounded_rect(viewer->renderer, &rect,
                                   item->as.rect.radius)) {
                set_sdl_error(viewer, "could not render rectangle");
                return -1;
            }
            break;
        }
        case MDWN_DRAW_LINE: {
            float y1 = item->as.line.y1 - viewer->scroll_y;
            float y2 = item->as.line.y2 - viewer->scroll_y;
            float x1 = item->as.line.x1 - viewer->scroll_x;
            float x2 = item->as.line.x2 - viewer->scroll_x;
            if (fmaxf(y1, y2) < 0.0f || fminf(y1, y2) > viewport_height)
                break;
            set_draw_color(viewer->renderer, item->as.line.color);
            if (!SDL_RenderLine(viewer->renderer,
                                x1, y1, x2, y2)) {
                set_sdl_error(viewer, "could not render line");
                return -1;
            }
            break;
        }
        case MDWN_DRAW_TEXT:
            if (item->as.text.x - viewer->scroll_x
                    + item->as.text.width < 0.0f ||
                item->as.text.x - viewer->scroll_x > viewport_width ||
                item->as.text.baseline - viewer->scroll_y + item->as.text.line_height < 0.0f ||
                item->as.text.baseline - viewer->scroll_y - item->as.text.line_height > viewport_height)
                break;
            if (draw_text_selection(viewer, item) < 0)
                return -1;
            if (draw_text(viewer, item) < 0)
                return -1;
            break;
        }
    }

    if (!SDL_SetRenderScale(viewer->renderer, 1.0f, 1.0f)) {
        set_sdl_error(viewer, "could not reset document zoom");
        return -1;
    }

    if (draw_scrollbar(viewer, false) < 0 ||
        draw_scrollbar(viewer, true) < 0)
        return -1;

    SDL_RenderPresent(viewer->renderer);
    return 0;
}

static void
clamp_scroll(struct viewer *viewer)
{
    float max_scroll_x = fmaxf(
        0.0f, viewer->layout.content_width
            - (float)viewer->width / viewer->zoom);
    float max_scroll_y = fmaxf(
        0.0f, viewer->layout.content_height
            - (float)viewer->height / viewer->zoom);

    if (viewer->scroll_x < 0.0f)
        viewer->scroll_x = 0.0f;
    if (viewer->scroll_x > max_scroll_x)
        viewer->scroll_x = max_scroll_x;
    if (viewer->scroll_y < 0.0f)
        viewer->scroll_y = 0.0f;
    if (viewer->scroll_y > max_scroll_y)
        viewer->scroll_y = max_scroll_y;
}

static int
rebuild_layout(struct viewer *viewer)
{
    if (viewer->selection.dragging)
        (void)SDL_CaptureMouse(false);

    if (!SDL_GetWindowSize(viewer->window, &viewer->width, &viewer->height)) {
        set_sdl_error(viewer, "could not query window size");
        return -1;
    }

    if (mdwn_layout_build(&viewer->layout, viewer->doc, viewer->fonts,
                          viewer->theme,
                          viewer->width, viewer->height,
                          viewer->err, viewer->err_size) < 0)
        return -1;

    viewer->selection.valid = false;
    viewer->selection.dragging = false;
    clamp_scroll(viewer);
    viewer->dirty = true;
    update_cursor_at_mouse(viewer);
    return 0;
}

static void
scroll_by(struct viewer *viewer, float x, float y)
{
    float old_x = viewer->scroll_x;
    float old_y = viewer->scroll_y;

    viewer->scroll_x += x / viewer->zoom;
    viewer->scroll_y += y / viewer->zoom;
    clamp_scroll(viewer);
    if (viewer->scroll_x != old_x || viewer->scroll_y != old_y) {
        viewer->dirty = true;
        update_cursor_at_mouse(viewer);
    }
}

static void
zoom_at(struct viewer *viewer, float factor, float x, float y)
{
    float old_zoom = viewer->zoom;
    float zoom = fminf(fmaxf(old_zoom * factor, MIN_ZOOM), MAX_ZOOM);
    float raster_zoom;
    float anchor_x;
    float anchor_y;

    if (zoom == old_zoom)
        return;

    anchor_x = document_x(viewer, x);
    anchor_y = document_y(viewer, y);
    viewer->zoom = zoom;
    viewer->scroll_x = anchor_x - x / zoom;
    viewer->scroll_y = anchor_y - y / zoom;
    raster_zoom = roundf(zoom / RASTER_ZOOM_STEP) * RASTER_ZOOM_STEP;
    if (raster_zoom != viewer->raster_zoom) {
        glyph_cache_destroy(&viewer->glyphs);
        viewer->raster_zoom = raster_zoom;
    }
    clamp_scroll(viewer);
    viewer->dirty = true;
    update_cursor_at_mouse(viewer);
}

static int
handle_event(struct viewer *viewer, const SDL_Event *event)
{
    switch (event->type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        viewer->quit = true;
        break;

    case SDL_EVENT_WINDOW_RESIZED:
        if (rebuild_layout(viewer) < 0)
            return -1;
        break;

    case SDL_EVENT_WINDOW_EXPOSED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        viewer->dirty = true;
        break;

    case SDL_EVENT_MOUSE_WHEEL: {
        SDL_Keymod modifiers = SDL_GetModState();
        float dx = event->wheel.x;
        float dy = event->wheel.y;
        if (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
            dx = -dx;
            dy = -dy;
        }
        if (modifiers & SDL_KMOD_CTRL) {
            zoom_at(viewer, powf(WHEEL_ZOOM_STEP,
                                 dy * ZOOM_SENSITIVITY),
                    event->wheel.mouse_x, event->wheel.mouse_y);
        } else {
            if (modifiers & SDL_KMOD_SHIFT) {
                dx += dy;
                dy = 0.0f;
            }
            scroll_by(viewer, dx * SCROLL_STEP, -dy * SCROLL_STEP);
        }
        break;
    }

#if SDL_MINOR_VERSION >= 4
    case SDL_EVENT_PINCH_UPDATE: {
        float x, y;

        (void)SDL_GetMouseState(&x, &y);
        zoom_at(viewer, powf(event->pinch.scale, ZOOM_SENSITIVITY), x, y);
        break;
    }
#endif

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event->button.button == SDL_BUTTON_LEFT) {
            viewer->pressed_link = link_at(
                &viewer->layout, document_x(viewer, event->button.x),
                document_y(viewer, event->button.y));
            if (mdwn_selection_begin(&viewer->selection, &viewer->layout,
                                     document_x(viewer, event->button.x),
                                     document_y(viewer, event->button.y),
                                     event->button.clicks))
                (void)SDL_CaptureMouse(true);
            viewer->dirty = true;
        }
        break;

    case SDL_EVENT_MOUSE_MOTION:
        update_cursor(viewer, event->motion.x, event->motion.y);
        if (viewer->selection.dragging) {
            if (mdwn_selection_update(&viewer->selection, &viewer->layout,
                                      document_x(viewer, event->motion.x),
                                      document_y(viewer, event->motion.y)))
                viewer->dirty = true;
        }
        break;

    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event->button.button == SDL_BUTTON_LEFT) {
            const char *released_link = link_at(
                &viewer->layout, document_x(viewer, event->button.x),
                document_y(viewer, event->button.y));

            if (viewer->selection.dragging) {
                (void)mdwn_selection_update(
                    &viewer->selection, &viewer->layout,
                    document_x(viewer, event->button.x),
                    document_y(viewer, event->button.y));
                viewer->selection.dragging = false;
                (void)SDL_CaptureMouse(false);
            }
            viewer->dirty = true;
            if (copy_selection(viewer, true) < 0)
                return -1;
            if (released_link == viewer->pressed_link && released_link &&
                selection_is_empty(&viewer->selection) &&
                !SDL_OpenURL(released_link)) {
                set_sdl_error(viewer, "could not open link");
                return -1;
            }
            viewer->pressed_link = NULL;
        }
        break;

    case SDL_EVENT_KEY_DOWN:
        if ((event->key.mod & SDL_KMOD_CTRL) && event->key.key == SDLK_C) {
            if (copy_selection(viewer, false) < 0)
                return -1;
            break;
        }
        if ((event->key.mod & SDL_KMOD_CTRL) && event->key.key == SDLK_A) {
            if (mdwn_selection_select_all(&viewer->selection,
                                          &viewer->layout))
                viewer->dirty = true;
            break;
        }
        switch (event->key.key) {
        case SDLK_ESCAPE:
        case SDLK_Q:
            viewer->quit = true;
            break;
        case SDLK_DOWN:
        case SDLK_J:
            scroll_by(viewer, 0.0f, SCROLL_STEP);
            break;
        case SDLK_UP:
        case SDLK_K:
            scroll_by(viewer, 0.0f, -SCROLL_STEP);
            break;
        case SDLK_PAGEDOWN:
            scroll_by(viewer, 0.0f, (float)viewer->height * 0.85f);
            break;
        case SDLK_PAGEUP:
            scroll_by(viewer, 0.0f, -(float)viewer->height * 0.85f);
            break;
        case SDLK_HOME:
            viewer->scroll_y = 0.0f;
            viewer->dirty = true;
            update_cursor_at_mouse(viewer);
            break;
        case SDLK_END:
            viewer->scroll_y = fmaxf(0.0f,
                viewer->layout.content_height
                    - (float)viewer->height / viewer->zoom);
            viewer->dirty = true;
            update_cursor_at_mouse(viewer);
            break;
        default:
            break;
        }
        break;

    default:
        break;
    }

    return 0;
}

static void
viewer_cleanup(struct viewer *viewer)
{
    glyph_cache_destroy(&viewer->glyphs);
    mdwn_layout_destroy(&viewer->layout);
    mdwn_font_system_destroy(viewer->fonts);
    if (viewer->link_cursor)
        SDL_DestroyCursor(viewer->link_cursor);
    if (viewer->text_cursor)
        SDL_DestroyCursor(viewer->text_cursor);
    if (viewer->default_cursor)
        SDL_DestroyCursor(viewer->default_cursor);
    if (viewer->renderer)
        SDL_DestroyRenderer(viewer->renderer);
    if (viewer->window)
        SDL_DestroyWindow(viewer->window);
    SDL_Quit();
}

int
mdwn_viewer_run(const char *title, const struct mdwn_document *doc,
                const struct mdwn_theme *theme,
                char *err, size_t err_size)
{
    struct viewer viewer;
    struct mdwn_color background = theme->background;
    SDL_Event event;
    int rc = -1;

    memset(&viewer, 0, sizeof(viewer));
    viewer.doc = doc;
    viewer.theme = theme;
    viewer.zoom = 1.0f;
    viewer.raster_zoom = 1.0f;
    viewer.err = err;
    viewer.err_size = err_size;
    viewer.width = INITIAL_WIDTH;
    viewer.height = INITIAL_HEIGHT;
    mdwn_layout_init(&viewer.layout);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        if (err && err_size)
            snprintf(err, err_size, "SDL initialization failed: %s", SDL_GetError());
        mdwn_layout_destroy(&viewer.layout);
        return -1;
    }

    if (!SDL_CreateWindowAndRenderer(title, INITIAL_WIDTH, INITIAL_HEIGHT,
                                     SDL_WINDOW_RESIZABLE,
                                     &viewer.window, &viewer.renderer)) {
        set_sdl_error(&viewer, "could not create window");
        viewer_cleanup(&viewer);
        return -1;
    }

    (void)SDL_SetWindowMinimumSize(viewer.window, 320, 240);
    (void)SDL_SetRenderVSync(viewer.renderer, 1);
    (void)SDL_SetRenderDrawBlendMode(viewer.renderer, SDL_BLENDMODE_BLEND);

    viewer.default_cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    viewer.text_cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT);
    viewer.link_cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
    if (!viewer.default_cursor || !viewer.text_cursor || !viewer.link_cursor) {
        set_sdl_error(&viewer, "could not create system cursor");
        viewer_cleanup(&viewer);
        return -1;
    }
    (void)SDL_SetCursor(viewer.default_cursor);

    /* Present an empty native window before font discovery and layout work. */
    set_draw_color(viewer.renderer, background);
    (void)SDL_RenderClear(viewer.renderer);
    SDL_RenderPresent(viewer.renderer);

    if (mdwn_font_system_create(&viewer.fonts, theme, err, err_size) < 0) {
        viewer_cleanup(&viewer);
        return -1;
    }

    if (rebuild_layout(&viewer) < 0) {
        viewer_cleanup(&viewer);
        return -1;
    }

    while (!viewer.quit) {
        if (viewer.dirty) {
            if (render_frame(&viewer) < 0)
                goto out;
            viewer.dirty = false;
        }

        if (!SDL_WaitEvent(&event)) {
            set_sdl_error(&viewer, "could not wait for window event");
            goto out;
        }

        if (handle_event(&viewer, &event) < 0)
            goto out;

        while (SDL_PollEvent(&event)) {
            if (handle_event(&viewer, &event) < 0)
                goto out;
        }
    }

    rc = 0;
out:
    viewer_cleanup(&viewer);
    return rc;
}
