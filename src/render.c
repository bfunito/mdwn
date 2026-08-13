#include "render.h"

#include "config.h"
#include "document.h"
#include "flavor.h"
#include "font.h"
#include "layout.h"
#include "markdown.h"
#include "selection.h"
#include "theme.h"
#include "watch.h"

#include <SDL3/SDL.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_WIDTH 960
#define INITIAL_HEIGHT 720
#define WHEEL_ZOOM_STEP 1.1f
#define WHEEL_ZOOM_SENSITIVITY 0.05f
#define RASTER_ZOOM_STEP 0.125f
#define WATCH_INTERVAL_MS 100

static const struct mdwn_color scrollbar_color = { 139, 148, 158, 120 };

struct viewer {
    SDL_Window *window;
    SDL_Renderer *renderer;
    TTF_TextEngine *text_engine;
    struct mdwn_font_system *fonts;
    struct mdwn_document document;
    struct mdwn_layout layout;
    struct mdwn_selection selection;
    struct mdwn_watcher watcher;
    char *source;
    size_t source_size;
    const char *document_path;
    const struct mdwn_flavor *flavor;
    const struct mdwn_theme *theme;
    const struct mdwn_viewer_config *config;
    float zoom;
    float raster_zoom;
    float pinch_scale;
    float scroll_x;
    float scroll_y;
    int width;
    int height;
    bool dirty;
    bool quit;
    const char *pressed_link;
    struct mdwn_code_block *dragged_code_block;
    float code_drag_offset;
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

static float
code_block_max_scroll(const struct mdwn_code_block *block)
{
    return fmaxf(block->content_width - block->viewport_width, 0.0f);
}

static bool
scrollbar_metrics(float length, float viewport, float content, float offset,
                  float *position, float *size)
{
    if (content <= viewport || length <= 0.0f)
        return false;

    *size = fminf(length, fmaxf(32.0f, length * viewport / content));
    *position = offset / (content - viewport) * (length - *size);
    return true;
}

static struct mdwn_code_block *
code_block_at(const struct mdwn_layout *layout, float x, float y)
{
    struct mdwn_code_block *block;

    for (block = layout->first_code_block; block; block = block->next) {
        if (code_block_max_scroll(block) > 0.0f &&
            x >= block->x && x <= block->x + block->w &&
            y >= block->y && y <= block->y + block->h)
            return block;
    }
    return NULL;
}

static bool
code_scrollbar_thumb(const struct mdwn_code_block *block, SDL_FRect *thumb)
{
    float max_scroll = code_block_max_scroll(block);
    float track_x = block->clip_x + 2.0f;
    float track_width = block->clip_w - 4.0f;
    float position;

    if (max_scroll <= 0.0f ||
        !scrollbar_metrics(track_width, block->viewport_width,
                           block->content_width, block->scroll_x,
                           &position, &thumb->w))
        return false;

    thumb->x = track_x + position;
    thumb->y = block->y + block->h - 6.0f;
    thumb->h = 4.0f;
    return true;
}

static struct mdwn_code_block *
code_scrollbar_at(const struct mdwn_layout *layout, float x, float y,
                  SDL_FRect *thumb)
{
    struct mdwn_code_block *block;

    for (block = layout->first_code_block; block; block = block->next) {
        if (x >= block->clip_x && x <= block->clip_x + block->clip_w &&
            y >= block->y + block->h - 10.0f &&
            y <= block->y + block->h &&
            code_scrollbar_thumb(block, thumb))
            return block;
    }
    return NULL;
}

static void
set_code_scrollbar_position(struct mdwn_code_block *block, float thumb_x)
{
    SDL_FRect thumb;
    float track_x = block->clip_x + 2.0f;
    float travel;

    if (!code_scrollbar_thumb(block, &thumb))
        return;
    travel = block->clip_w - 4.0f - thumb.w;
    if (travel <= 0.0f)
        return;
    block->scroll_x = fminf(fmaxf(
        (thumb_x - track_x) / travel * code_block_max_scroll(block),
        0.0f), code_block_max_scroll(block));
}

static const struct mdwn_draw_item *
text_at(const struct mdwn_layout *layout, float x, float y)
{
    const struct mdwn_draw_item *item;
    const struct mdwn_draw_item *text = NULL;

    for (item = layout->first; item; item = item->next) {
        float text_x;

        if (item->type != MDWN_DRAW_TEXT)
            continue;
        text_x = mdwn_layout_text_x(item);
        if (x >= text_x &&
            x <= text_x + item->as.text.width &&
            y >= item->as.text.top &&
            y <= item->as.text.top + item->as.text.line_height) {
            if (item->as.text.code_block &&
                (x < item->as.text.code_block->clip_x ||
                 x > item->as.text.code_block->clip_x
                    + item->as.text.code_block->clip_w))
                continue;
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
    const struct mdwn_draw_item *item;
    const struct mdwn_draw_item *text = text_at(layout, x, y);

    if (text && text->as.text.href)
        return text->as.text.href;
    for (item = layout->first; item; item = item->next) {
        if (item->type == MDWN_DRAW_IMAGE && item->as.image.href &&
            x >= item->as.image.x &&
            x <= item->as.image.x + item->as.image.w &&
            y >= item->as.image.y &&
            y <= item->as.image.y + item->as.image.h)
            return item->as.image.href;
    }
    return NULL;
}

static void
update_cursor(struct viewer *viewer, float x, float y)
{
    float doc_x = document_x(viewer, x);
    float doc_y = document_y(viewer, y);
    const struct mdwn_draw_item *item = text_at(
        &viewer->layout, doc_x, doc_y);
    SDL_Cursor *cursor = viewer->default_cursor;

    if (link_at(&viewer->layout, doc_x, doc_y))
        cursor = viewer->link_cursor;
    else if (item)
        cursor = viewer->text_cursor;
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

static int
set_code_clip(struct viewer *viewer, const struct mdwn_code_block *block,
              float x_scale, float y_scale)
{
    SDL_Rect rect;
    const SDL_Rect *clip = NULL;

    if (block) {
        float left = (block->clip_x - viewer->scroll_x) * x_scale;
        float top = (block->y - viewer->scroll_y) * y_scale;

        rect.x = (int)floorf(left);
        rect.y = (int)floorf(top);
        rect.w = (int)ceilf(left + block->clip_w * x_scale) - rect.x;
        rect.h = (int)ceilf(top + block->h * y_scale) - rect.y;
        clip = &rect;
    }
    if (!SDL_SetRenderClipRect(viewer->renderer, clip)) {
        set_sdl_error(viewer, "could not set code block clipping");
        return -1;
    }
    return 0;
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

static int
draw_text(struct viewer *viewer, struct mdwn_draw_item *item)
{
    const struct mdwn_color color = item->as.text.color;
    float scale = item->as.text.raster_scale;
    float x_scale = item->as.text.raster_x_scale;
    float render_x_scale, render_y_scale;
    float x, y;

    if (!item->as.text.render_object) {
        struct mdwn_font *font = mdwn_font_get_scaled(
            viewer->fonts, item->as.text.font, viewer->raster_zoom,
            &scale, viewer->err, viewer->err_size);
        int width;

        if (!font)
            return -1;
        item->as.text.render_object = TTF_CreateText(
            viewer->text_engine, mdwn_font_handle(font),
            item->as.text.object->text, item->as.text.text_length);
        if (!item->as.text.render_object ||
            !TTF_SetTextColor(item->as.text.render_object,
                              color.r, color.g, color.b, color.a) ||
            !TTF_GetTextSize(item->as.text.render_object, &width, NULL)) {
            set_sdl_error(viewer, "could not prepare text");
            return -1;
        }
        x_scale = width > 0
            ? item->as.text.width * scale / (float)width
            : 1.0f;
        item->as.text.raster_scale = scale;
        item->as.text.raster_x_scale = x_scale;
    }

    render_x_scale = viewer->zoom * x_scale / scale;
    render_y_scale = viewer->zoom / scale;
    if (!SDL_SetRenderScale(viewer->renderer,
                            render_x_scale, render_y_scale)) {
        set_sdl_error(viewer, "could not set text scale");
        return -1;
    }
    if (item->as.text.code_block &&
        set_code_clip(viewer, item->as.text.code_block,
                      scale / x_scale, scale) < 0)
        return -1;

    x = (mdwn_layout_text_x(item) - viewer->scroll_x) * scale / x_scale;
    y = (item->as.text.baseline - viewer->scroll_y) * scale
        - (float)TTF_GetFontAscent(
            TTF_GetTextFont(item->as.text.render_object));
    x = roundf(x * render_x_scale) / render_x_scale;
    y = roundf(y * render_y_scale) / render_y_scale;
    if (!TTF_DrawRendererText(item->as.text.render_object, x, y)) {
        set_sdl_error(viewer, "could not render text");
        return -1;
    }
    if (!SDL_SetRenderScale(viewer->renderer, viewer->zoom, viewer->zoom)) {
        set_sdl_error(viewer, "could not restore document scale");
        return -1;
    }
    if (item->as.text.code_block &&
        set_code_clip(viewer, item->as.text.code_block, 1.0f, 1.0f) < 0)
        return -1;
    return 0;
}

static int
draw_text_selection(struct viewer *viewer,
                    const struct mdwn_draw_item *item)
{
    struct mdwn_color color = { 51, 132, 255, 110 };
    size_t start, end;
    float x, width;
    SDL_FRect rect;

    if (!mdwn_selection_item_range(&viewer->selection, item, &start, &end))
        return 0;

    if (!mdwn_selection_text_bounds(item, start, end - start, &x, &width))
        return 0;
    rect.x = x - viewer->scroll_x;
    rect.y = item->as.text.top - viewer->scroll_y;
    rect.w = width;
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
    float position;
    SDL_FRect rect;

    if (!scrollbar_metrics(window_size, viewport, content, offset,
                           &position, &bar_size))
        return 0;

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

    set_draw_color(viewer->renderer, scrollbar_color);
    if (!SDL_RenderFillRect(viewer->renderer, &rect)) {
        set_sdl_error(viewer, "could not render scrollbar");
        return -1;
    }
    return 0;
}

static int
draw_code_scrollbars(struct viewer *viewer)
{
    struct mdwn_code_block *block;

    set_draw_color(viewer->renderer, scrollbar_color);
    for (block = viewer->layout.first_code_block; block; block = block->next) {
        SDL_FRect rect;

        if (!code_scrollbar_thumb(block, &rect))
            continue;
        rect.x -= viewer->scroll_x;
        rect.y -= viewer->scroll_y;
        if (!SDL_RenderFillRect(viewer->renderer, &rect)) {
            set_sdl_error(viewer, "could not render code block scrollbar");
            return -1;
        }
    }
    return 0;
}

static int
render_frame(struct viewer *viewer)
{
    struct mdwn_draw_item *item;
    struct mdwn_color background = viewer->theme->background;
    const struct mdwn_code_block *clip = NULL;
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
        const struct mdwn_code_block *next_clip = item->type == MDWN_DRAW_TEXT
            ? item->as.text.code_block
            : NULL;

        if (next_clip != clip) {
            if (set_code_clip(viewer, next_clip, 1.0f, 1.0f) < 0)
                return -1;
            clip = next_clip;
        }
        switch (item->type) {
        case MDWN_DRAW_IMAGE: {
            SDL_FRect rect;
            rect.x = item->as.image.x - viewer->scroll_x;
            rect.y = item->as.image.y - viewer->scroll_y;
            rect.w = item->as.image.w;
            rect.h = item->as.image.h;

            if (rect.x + rect.w < 0.0f || rect.x > viewport_width ||
                rect.y + rect.h < 0.0f || rect.y > viewport_height)
                break;
            if (!SDL_RenderTexture(viewer->renderer, item->as.image.texture,
                                   NULL, &rect)) {
                set_sdl_error(viewer, "could not render image");
                return -1;
            }
            break;
        }
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
            if (mdwn_layout_text_x(item) - viewer->scroll_x
                    + item->as.text.width < 0.0f ||
                mdwn_layout_text_x(item) - viewer->scroll_x > viewport_width ||
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

    if (clip && set_code_clip(viewer, NULL, 1.0f, 1.0f) < 0)
        return -1;

    if (draw_code_scrollbars(viewer) < 0)
        return -1;

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

static void
layout_changed(struct viewer *viewer)
{
    if (viewer->selection.dragging || viewer->dragged_code_block)
        (void)SDL_CaptureMouse(false);
    viewer->dragged_code_block = NULL;
    viewer->selection.valid = false;
    viewer->selection.dragging = false;
    clamp_scroll(viewer);
    viewer->dirty = true;
    update_cursor_at_mouse(viewer);
}

static int
rebuild_layout(struct viewer *viewer)
{
    if (viewer->selection.dragging || viewer->dragged_code_block)
        (void)SDL_CaptureMouse(false);
    viewer->dragged_code_block = NULL;

    if (!SDL_GetWindowSize(viewer->window, &viewer->width, &viewer->height)) {
        set_sdl_error(viewer, "could not query window size");
        return -1;
    }

    if (mdwn_layout_build(&viewer->layout, &viewer->document, viewer->fonts,
                          viewer->theme, viewer->renderer,
                          viewer->document_path,
                          viewer->width, viewer->height,
                          viewer->err, viewer->err_size) < 0)
        return -1;

    layout_changed(viewer);
    return 0;
}

static void
report_reload_error(const char *message)
{
    fprintf(stderr, "mdwn: could not reload document: %s\n", message);
}

static void
reload_document(struct viewer *viewer)
{
    struct mdwn_document document;
    struct mdwn_document old_document;
    struct mdwn_layout layout;
    struct mdwn_layout old_layout;
    char error[512] = {0};
    char *source;
    char *old_source;
    size_t source_size;

    source = SDL_LoadFile(viewer->document_path, &source_size);
    if (!source) {
        report_reload_error(SDL_GetError());
        return;
    }
    if (source_size == viewer->source_size &&
        memcmp(source, viewer->source, source_size) == 0) {
        SDL_free(source);
        return;
    }

    if (mdwn_document_init(&document) < 0) {
        SDL_free(source);
        report_reload_error("out of memory while creating document");
        return;
    }
    if (mdwn_markdown_parse(&document, source, source_size, viewer->flavor,
                            error, sizeof(error)) < 0) {
        mdwn_document_destroy(&document);
        SDL_free(source);
        report_reload_error(error[0] ? error : "could not parse markdown");
        return;
    }

    mdwn_layout_init(&layout);
    mdwn_layout_take_images(&layout, &viewer->layout);
    if (mdwn_layout_build(&layout, &document, viewer->fonts, viewer->theme,
                          viewer->renderer, viewer->document_path,
                          viewer->width, viewer->height,
                          error, sizeof(error)) < 0) {
        mdwn_layout_take_images(&viewer->layout, &layout);
        mdwn_layout_destroy(&layout);
        mdwn_document_destroy(&document);
        SDL_free(source);
        report_reload_error(error[0] ? error : "could not build layout");
        return;
    }

    old_document = viewer->document;
    viewer->document = document;
    old_layout = viewer->layout;
    viewer->layout = layout;
    old_source = viewer->source;
    viewer->source = source;
    viewer->source_size = source_size;

    layout_changed(viewer);
    mdwn_layout_destroy(&old_layout);
    mdwn_document_destroy(&old_document);
    SDL_free(old_source);
}

static void
check_document_updates(struct viewer *viewer)
{
    char error[512] = {0};
    bool changed;

    if (mdwn_watcher_poll(&viewer->watcher, &changed,
                          error, sizeof(error)) < 0) {
        fprintf(stderr, "mdwn: %s\n", error[0] ? error
                                                : "filesystem watcher failed");
        mdwn_watcher_destroy(&viewer->watcher);
        return;
    }
    if (changed)
        reload_document(viewer);
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
scroll_code_block(struct viewer *viewer, struct mdwn_code_block *block,
                  float amount)
{
    float old_x = block->scroll_x;

    block->scroll_x = fminf(fmaxf(
        block->scroll_x + amount / viewer->zoom, 0.0f),
        code_block_max_scroll(block));
    if (block->scroll_x != old_x) {
        viewer->dirty = true;
        update_cursor_at_mouse(viewer);
    }
}

static void
zoom_at(struct viewer *viewer, float factor, float x, float y)
{
    float old_zoom = viewer->zoom;
    float zoom = fminf(fmaxf(old_zoom * factor, viewer->config->min_zoom),
                       viewer->config->max_zoom);
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
        mdwn_layout_clear_render_text(&viewer->layout);
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
                                 dy * WHEEL_ZOOM_SENSITIVITY
                                    * viewer->config->wheel_zoom_speed),
                    event->wheel.mouse_x, event->wheel.mouse_y);
        } else {
            struct mdwn_code_block *block;

            if (modifiers & SDL_KMOD_SHIFT) {
                dx += dy;
                dy = 0.0f;
            }
            block = code_block_at(
                &viewer->layout,
                document_x(viewer, event->wheel.mouse_x),
                document_y(viewer, event->wheel.mouse_y));
            if (block && dx != 0.0f) {
                scroll_code_block(viewer, block,
                                  dx * viewer->config->scroll_step);
                dx = 0.0f;
            }
            scroll_by(viewer, dx * viewer->config->scroll_step,
                      -dy * viewer->config->scroll_step);
        }
        break;
    }

#if SDL_MINOR_VERSION >= 4
    case SDL_EVENT_PINCH_BEGIN:
        viewer->pinch_scale = 1.0f;
        break;

    case SDL_EVENT_PINCH_UPDATE: {
        float factor = event->pinch.scale / viewer->pinch_scale;
        float x, y;

        viewer->pinch_scale = event->pinch.scale;
        (void)SDL_GetMouseState(&x, &y);
        zoom_at(viewer, factor, x, y);
        break;
    }

    case SDL_EVENT_PINCH_END:
        viewer->pinch_scale = 1.0f;
        break;
#endif

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event->button.button == SDL_BUTTON_LEFT) {
            SDL_FRect thumb;
            float x = document_x(viewer, event->button.x);
            float y = document_y(viewer, event->button.y);
            struct mdwn_code_block *block = code_scrollbar_at(
                &viewer->layout, x, y, &thumb);

            if (block) {
                if (x < thumb.x || x > thumb.x + thumb.w) {
                    set_code_scrollbar_position(block, x - thumb.w * 0.5f);
                    (void)code_scrollbar_thumb(block, &thumb);
                    viewer->dirty = true;
                }
                viewer->dragged_code_block = block;
                viewer->code_drag_offset = x - thumb.x;
                (void)SDL_CaptureMouse(true);
                break;
            }
            viewer->pressed_link = link_at(
                &viewer->layout, x, y);
            if (mdwn_selection_begin(&viewer->selection, &viewer->layout,
                                     x, y, event->button.clicks))
                (void)SDL_CaptureMouse(true);
            viewer->dirty = true;
        }
        break;

    case SDL_EVENT_MOUSE_MOTION:
        update_cursor(viewer, event->motion.x, event->motion.y);
        if (viewer->dragged_code_block) {
            set_code_scrollbar_position(
                viewer->dragged_code_block,
                document_x(viewer, event->motion.x)
                    - viewer->code_drag_offset);
            viewer->dirty = true;
        } else if (viewer->selection.dragging) {
            if (mdwn_selection_update(&viewer->selection, &viewer->layout,
                                      document_x(viewer, event->motion.x),
                                      document_y(viewer, event->motion.y)))
                viewer->dirty = true;
        }
        break;

    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event->button.button == SDL_BUTTON_LEFT) {
            if (viewer->dragged_code_block) {
                viewer->dragged_code_block = NULL;
                (void)SDL_CaptureMouse(false);
                update_cursor_at_mouse(viewer);
                break;
            }
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
            scroll_by(viewer, 0.0f, viewer->config->scroll_step);
            break;
        case SDLK_UP:
        case SDLK_K:
            scroll_by(viewer, 0.0f, -viewer->config->scroll_step);
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
    mdwn_watcher_destroy(&viewer->watcher);
    mdwn_layout_destroy(&viewer->layout);
    mdwn_document_destroy(&viewer->document);
    SDL_free(viewer->source);
    if (viewer->text_engine)
        TTF_DestroyRendererTextEngine(viewer->text_engine);
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
mdwn_viewer_run(const char *title, const char *document_path,
                const struct mdwn_flavor *flavor,
                const struct mdwn_theme *theme,
                const struct mdwn_viewer_config *config,
                char *err, size_t err_size)
{
    struct viewer viewer;
    struct mdwn_color background = theme->background;
    SDL_Event event;
    char watcher_error[512] = {0};
    int rc = -1;

    memset(&viewer, 0, sizeof(viewer));
    viewer.watcher.fd = -1;
    viewer.document_path = document_path;
    viewer.flavor = flavor;
    viewer.theme = theme;
    viewer.config = config;
    viewer.zoom = config->initial_zoom;
    viewer.raster_zoom = roundf(config->initial_zoom / RASTER_ZOOM_STEP)
                       * RASTER_ZOOM_STEP;
    viewer.pinch_scale = 1.0f;
    viewer.err = err;
    viewer.err_size = err_size;
    viewer.width = INITIAL_WIDTH;
    viewer.height = INITIAL_HEIGHT;
    mdwn_layout_init(&viewer.layout);

    (void)mdwn_watcher_init(&viewer.watcher, document_path,
                            watcher_error, sizeof(watcher_error));

    viewer.source = SDL_LoadFile(document_path, &viewer.source_size);
    if (!viewer.source) {
        if (err && err_size)
            snprintf(err, err_size, "%s", SDL_GetError());
        mdwn_watcher_destroy(&viewer.watcher);
        mdwn_layout_destroy(&viewer.layout);
        return -1;
    }
    if (mdwn_document_init(&viewer.document) < 0) {
        if (err && err_size)
            snprintf(err, err_size, "out of memory while creating document");
        mdwn_watcher_destroy(&viewer.watcher);
        mdwn_layout_destroy(&viewer.layout);
        SDL_free(viewer.source);
        return -1;
    }
    if (mdwn_markdown_parse(&viewer.document,
                            viewer.source, viewer.source_size, flavor,
                            err, err_size) < 0) {
        mdwn_watcher_destroy(&viewer.watcher);
        mdwn_layout_destroy(&viewer.layout);
        mdwn_document_destroy(&viewer.document);
        SDL_free(viewer.source);
        return -1;
    }
    if (viewer.watcher.fd < 0)
        fprintf(stderr, "mdwn: warning: %s\n", watcher_error[0] ? watcher_error
                                      : "could not watch document for changes");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        if (err && err_size)
            snprintf(err, err_size, "SDL initialization failed: %s", SDL_GetError());
        viewer_cleanup(&viewer);
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
    viewer.text_engine = TTF_CreateRendererTextEngine(viewer.renderer);
    if (!viewer.text_engine) {
        set_sdl_error(&viewer, "could not create text engine");
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

        if (SDL_WaitEventTimeout(&event, WATCH_INTERVAL_MS)) {
            if (handle_event(&viewer, &event) < 0)
                goto out;

            while (SDL_PollEvent(&event)) {
                if (handle_event(&viewer, &event) < 0)
                    goto out;
            }
        }
        check_document_updates(&viewer);
    }

    rc = 0;
out:
    viewer_cleanup(&viewer);
    return rc;
}
