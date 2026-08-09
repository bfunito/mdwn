#include "render.h"

#include "document.h"
#include "font.h"
#include "layout.h"
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
    const struct mdwn_document *doc;
    const struct mdwn_theme *theme;
    float scroll_y;
    int width;
    int height;
    bool dirty;
    bool quit;
    char *err;
    size_t err_size;
};

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
    float pen_x = item->as.text.x;
    float pen_y = item->as.text.baseline - viewer->scroll_y;
    size_t i;

    for (i = 0; i < item->as.text.glyph_count; ++i) {
        const struct mdwn_shaped_glyph *g = &item->as.text.glyphs[i];
        struct glyph_texture *texture = get_glyph(viewer, item->as.text.font, g->index);

        if (!texture)
            return -1;

        if (texture->texture && texture->width > 0 && texture->height > 0) {
            SDL_FRect dst;

            dst.x = roundf(pen_x + g->x_offset + (float)texture->left);
            dst.y = roundf(pen_y - g->y_offset - (float)texture->top);
            dst.w = (float)texture->width;
            dst.h = (float)texture->height;

            if (dst.y + dst.h >= 0.0f && dst.y <= (float)viewer->height) {
                if (!SDL_SetTextureColorMod(texture->texture, color.r, color.g, color.b) ||
                    !SDL_SetTextureAlphaMod(texture->texture, color.a) ||
                    !SDL_RenderTexture(viewer->renderer, texture->texture, NULL, &dst)) {
                    set_sdl_error(viewer, "could not render glyph");
                    return -1;
                }
            }
        }

        pen_x += g->x_advance;
        pen_y -= g->y_advance;
    }

    set_draw_color(viewer->renderer, color);
    if (item->as.text.strike) {
        float y = item->as.text.baseline - viewer->scroll_y - item->as.text.line_height * 0.30f;
        if (!SDL_RenderLine(viewer->renderer, item->as.text.x, y,
                            item->as.text.x + item->as.text.width, y)) {
            set_sdl_error(viewer, "could not render strikethrough");
            return -1;
        }
    }
    if (item->as.text.underline) {
        float y = item->as.text.baseline - viewer->scroll_y + 2.0f;
        if (!SDL_RenderLine(viewer->renderer, item->as.text.x, y,
                            item->as.text.x + item->as.text.width, y)) {
            set_sdl_error(viewer, "could not render underline");
            return -1;
        }
    }

    return 0;
}

static int
draw_scrollbar(struct viewer *viewer)
{
    float content = viewer->layout.content_height;
    float viewport = (float)viewer->height;
    float bar_height;
    float travel;
    float max_scroll;
    float y;
    SDL_FRect rect;
    struct mdwn_color color = { 139, 148, 158, 120 };

    if (content <= viewport || viewport <= 0.0f)
        return 0;

    max_scroll = content - viewport;
    bar_height = fmaxf(32.0f, viewport * viewport / content);
    travel = viewport - bar_height;
    y = max_scroll > 0.0f ? viewer->scroll_y / max_scroll * travel : 0.0f;

    rect.x = (float)viewer->width - 6.0f;
    rect.y = y + 2.0f;
    rect.w = 4.0f;
    rect.h = fmaxf(bar_height - 4.0f, 4.0f);

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

    set_draw_color(viewer->renderer, background);
    if (!SDL_RenderClear(viewer->renderer)) {
        set_sdl_error(viewer, "could not clear window");
        return -1;
    }

    for (item = viewer->layout.first; item; item = item->next) {
        switch (item->type) {
        case MDWN_DRAW_RECT: {
            SDL_FRect rect;
            rect.x = item->as.rect.x;
            rect.y = item->as.rect.y - viewer->scroll_y;
            rect.w = item->as.rect.w;
            rect.h = item->as.rect.h;

            if (rect.y + rect.h < 0.0f || rect.y > (float)viewer->height)
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
            if (fmaxf(y1, y2) < 0.0f || fminf(y1, y2) > (float)viewer->height)
                break;
            set_draw_color(viewer->renderer, item->as.line.color);
            if (!SDL_RenderLine(viewer->renderer,
                                item->as.line.x1, y1,
                                item->as.line.x2, y2)) {
                set_sdl_error(viewer, "could not render line");
                return -1;
            }
            break;
        }
        case MDWN_DRAW_TEXT:
            if (item->as.text.baseline - viewer->scroll_y + item->as.text.line_height < 0.0f ||
                item->as.text.baseline - viewer->scroll_y - item->as.text.line_height > (float)viewer->height)
                break;
            if (draw_text(viewer, item) < 0)
                return -1;
            break;
        }
    }

    if (draw_scrollbar(viewer) < 0)
        return -1;

    SDL_RenderPresent(viewer->renderer);
    return 0;
}

static void
clamp_scroll(struct viewer *viewer)
{
    float max_scroll = fmaxf(0.0f, viewer->layout.content_height - (float)viewer->height);

    if (viewer->scroll_y < 0.0f)
        viewer->scroll_y = 0.0f;
    if (viewer->scroll_y > max_scroll)
        viewer->scroll_y = max_scroll;
}

static int
rebuild_layout(struct viewer *viewer)
{
    if (!SDL_GetWindowSize(viewer->window, &viewer->width, &viewer->height)) {
        set_sdl_error(viewer, "could not query window size");
        return -1;
    }

    if (mdwn_layout_build(&viewer->layout, viewer->doc, viewer->fonts,
                          viewer->theme,
                          viewer->width, viewer->height,
                          viewer->err, viewer->err_size) < 0)
        return -1;

    clamp_scroll(viewer);
    viewer->dirty = true;
    return 0;
}

static void
scroll_by(struct viewer *viewer, float amount)
{
    float old = viewer->scroll_y;
    viewer->scroll_y += amount;
    clamp_scroll(viewer);
    if (viewer->scroll_y != old)
        viewer->dirty = true;
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
        float dy = event->wheel.y;
        if (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
            dy = -dy;
        scroll_by(viewer, -dy * SCROLL_STEP);
        break;
    }

    case SDL_EVENT_KEY_DOWN:
        switch (event->key.key) {
        case SDLK_ESCAPE:
        case SDLK_Q:
            viewer->quit = true;
            break;
        case SDLK_DOWN:
        case SDLK_J:
            scroll_by(viewer, SCROLL_STEP);
            break;
        case SDLK_UP:
        case SDLK_K:
            scroll_by(viewer, -SCROLL_STEP);
            break;
        case SDLK_PAGEDOWN:
            scroll_by(viewer, (float)viewer->height * 0.85f);
            break;
        case SDLK_PAGEUP:
            scroll_by(viewer, -(float)viewer->height * 0.85f);
            break;
        case SDLK_HOME:
            viewer->scroll_y = 0.0f;
            viewer->dirty = true;
            break;
        case SDLK_END:
            viewer->scroll_y = fmaxf(0.0f,
                viewer->layout.content_height - (float)viewer->height);
            viewer->dirty = true;
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
