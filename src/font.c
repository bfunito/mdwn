#include "font.h"

#include "theme.h"

#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb-ft.h>
#include <hb.h>

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct font_source {
    char *path;
    int face_index;
    bool resolved;
};

struct mdwn_font {
    struct mdwn_font *next;
    struct mdwn_font_spec spec;
    FT_Face face;
    hb_font_t *hb_font;
    float ascender;
    float descender;
};

struct mdwn_font_system {
    FT_Library ft;
    struct font_source sources[2][3][2];
    struct mdwn_font *fonts;
    const struct mdwn_theme *theme;
};

static size_t
weight_index(unsigned weight)
{
    if (weight >= 700)
        return 2;
    if (weight >= 600)
        return 1;
    return 0;
}

static void
set_error(char *err, size_t err_size, const char *message)
{
    if (err && err_size)
        snprintf(err, err_size, "%s", message);
}

static char *
copy_string(const char *s)
{
    size_t len = strlen(s);
    char *copy = malloc(len + 1);

    if (!copy)
        return NULL;
    memcpy(copy, s, len + 1);
    return copy;
}

static int
resolve_source(struct mdwn_font_system *system,
               enum mdwn_font_family family, unsigned weight, bool italic,
               struct font_source **out,
               char *err, size_t err_size)
{
    struct font_source *source =
        &system->sources[family][weight_index(weight)][italic ? 1 : 0];
    FcPattern *pattern = NULL;
    FcPattern *match = NULL;
    FcResult result;
    FcChar8 *file = NULL;
    int index = 0;
    const char *const *family_names = family == MDWN_FONT_MONO
        ? system->theme->mono_fonts
        : system->theme->sans_fonts;
    const char *family_name = family == MDWN_FONT_MONO ? "monospace" : "sans-serif";

    if (source->resolved) {
        *out = source;
        return 0;
    }

    pattern = FcPatternCreate();
    if (!pattern)
        goto oom;

    while (*family_names) {
        if (!FcPatternAddString(pattern, FC_FAMILY,
                                (const FcChar8 *)*family_names))
            goto oom;
        ++family_names;
    }
    if (!FcPatternAddInteger(pattern, FC_WEIGHT,
                             weight >= 700 ? FC_WEIGHT_BOLD :
                             weight >= 600 ? FC_WEIGHT_DEMIBOLD :
                                             FC_WEIGHT_REGULAR))
        goto oom;
    if (!FcPatternAddInteger(pattern, FC_SLANT, italic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN))
        goto oom;

    FcConfigSubstitute(NULL, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);

    match = FcFontMatch(NULL, pattern, &result);
    if (!match) {
        if (err && err_size)
            snprintf(err, err_size, "fontconfig could not find a %s font", family_name);
        FcPatternDestroy(pattern);
        return -1;
    }

    if (FcPatternGetString(match, FC_FILE, 0, &file) != FcResultMatch) {
        if (err && err_size)
            snprintf(err, err_size, "fontconfig match for %s has no font file", family_name);
        FcPatternDestroy(match);
        FcPatternDestroy(pattern);
        return -1;
    }

    (void)FcPatternGetInteger(match, FC_INDEX, 0, &index);

    source->path = copy_string((const char *)file);
    if (!source->path)
        goto oom;
    source->face_index = index;
    source->resolved = true;

    FcPatternDestroy(match);
    FcPatternDestroy(pattern);
    *out = source;
    return 0;

oom:
    set_error(err, err_size, "out of memory while resolving fonts");
    if (match)
        FcPatternDestroy(match);
    if (pattern)
        FcPatternDestroy(pattern);
    return -1;
}

int
mdwn_font_system_create(struct mdwn_font_system **out,
                        const struct mdwn_theme *theme,
                        char *err, size_t err_size)
{
    struct mdwn_font_system *system;
    FT_Error ft_error;

    *out = NULL;

    if (!FcInit()) {
        set_error(err, err_size, "fontconfig initialization failed");
        return -1;
    }

    system = calloc(1, sizeof(*system));
    if (!system) {
        set_error(err, err_size, "out of memory while initializing fonts");
        FcFini();
        return -1;
    }

    system->theme = theme;

    ft_error = FT_Init_FreeType(&system->ft);
    if (ft_error) {
        if (err && err_size)
            snprintf(err, err_size, "FreeType initialization failed (%d)", ft_error);
        free(system);
        FcFini();
        return -1;
    }

    *out = system;
    return 0;
}

void
mdwn_font_system_destroy(struct mdwn_font_system *system)
{
    size_t family, weight, italic;
    struct mdwn_font *font;

    if (!system)
        return;

    font = system->fonts;
    while (font) {
        struct mdwn_font *next = font->next;
        hb_font_destroy(font->hb_font);
        FT_Done_Face(font->face);
        free(font);
        font = next;
    }

    for (family = 0; family < 2; ++family) {
        for (weight = 0; weight < 3; ++weight) {
            for (italic = 0; italic < 2; ++italic)
                free(system->sources[family][weight][italic].path);
        }
    }

    FT_Done_FreeType(system->ft);
    free(system);
    FcFini();
}

static bool
same_spec(struct mdwn_font_spec a, struct mdwn_font_spec b)
{
    return a.family == b.family &&
           a.size_px == b.size_px &&
           a.weight == b.weight &&
           a.italic == b.italic;
}

struct mdwn_font *
mdwn_font_get(struct mdwn_font_system *system, struct mdwn_font_spec spec,
              char *err, size_t err_size)
{
    struct font_source *source;
    struct mdwn_font *font;
    FT_Error ft_error;

    for (font = system->fonts; font; font = font->next) {
        if (same_spec(font->spec, spec))
            return font;
    }

    if (spec.size_px == 0) {
        set_error(err, err_size, "invalid font size");
        return NULL;
    }

    if (resolve_source(system, spec.family, spec.weight, spec.italic,
                       &source, err, err_size) < 0)
        return NULL;

    font = calloc(1, sizeof(*font));
    if (!font) {
        set_error(err, err_size, "out of memory while creating font");
        return NULL;
    }

    ft_error = FT_New_Face(system->ft, source->path, source->face_index, &font->face);
    if (ft_error) {
        if (err && err_size)
            snprintf(err, err_size, "could not open font '%s' (%d)", source->path, ft_error);
        free(font);
        return NULL;
    }

    ft_error = FT_Set_Pixel_Sizes(font->face, 0, spec.size_px);
    if (ft_error) {
        if (err && err_size)
            snprintf(err, err_size, "could not set font size %u (%d)", spec.size_px, ft_error);
        FT_Done_Face(font->face);
        free(font);
        return NULL;
    }

    font->hb_font = hb_ft_font_create_referenced(font->face);
    if (!font->hb_font) {
        set_error(err, err_size, "could not create HarfBuzz font");
        FT_Done_Face(font->face);
        free(font);
        return NULL;
    }

    font->spec = spec;
    font->ascender = (float)font->face->size->metrics.ascender / 64.0f;
    font->descender = -(float)font->face->size->metrics.descender / 64.0f;

    font->next = system->fonts;
    system->fonts = font;
    return font;
}

struct mdwn_font *
mdwn_font_get_scaled(struct mdwn_font_system *system,
                     const struct mdwn_font *font, float scale,
                     float *actual_scale, char *err, size_t err_size)
{
    struct mdwn_font_spec spec = font->spec;
    double size = round((double)spec.size_px * (double)scale);

    if (!(size >= 1.0) || size > (double)UINT_MAX) {
        set_error(err, err_size, "invalid scaled font size");
        return NULL;
    }

    spec.size_px = (unsigned)size;
    *actual_scale = (float)spec.size_px / (float)font->spec.size_px;
    return mdwn_font_get(system, spec, err, err_size);
}

float
mdwn_font_ascender(const struct mdwn_font *font)
{
    return font->ascender;
}

float
mdwn_font_descender(const struct mdwn_font *font)
{
    return font->descender;
}

static int
shape_internal(struct mdwn_font *font,
               const char *text, size_t len,
               struct mdwn_arena *arena,
               struct mdwn_shaped_glyph **glyphs_out,
               size_t *count_out,
               float *width_out,
               char *err, size_t err_size)
{
    hb_buffer_t *buffer;
    hb_glyph_info_t *infos;
    hb_glyph_position_t *positions;
    unsigned count;
    float width = 0.0f;
    size_t i;
    struct mdwn_shaped_glyph *glyphs = NULL;

    if (len > INT_MAX) {
        set_error(err, err_size, "text run is too large to shape");
        return -1;
    }

    buffer = hb_buffer_create();
    if (!buffer || !hb_buffer_allocation_successful(buffer)) {
        if (buffer)
            hb_buffer_destroy(buffer);
        set_error(err, err_size, "out of memory while shaping text");
        return -1;
    }

    hb_buffer_add_utf8(buffer, text, (int)len, 0, (int)len);
    hb_buffer_guess_segment_properties(buffer);
    hb_shape(font->hb_font, buffer, NULL, 0);

    infos = hb_buffer_get_glyph_infos(buffer, &count);
    positions = hb_buffer_get_glyph_positions(buffer, &count);

    if (arena && count) {
        glyphs = mdwn_arena_alloc(arena, (size_t)count * sizeof(*glyphs));
        if (!glyphs) {
            hb_buffer_destroy(buffer);
            set_error(err, err_size, "out of memory while storing shaped text");
            return -1;
        }
    }

    for (i = 0; i < count; ++i) {
        float advance = (float)positions[i].x_advance / 64.0f;
        width += advance;

        if (glyphs) {
            glyphs[i].index = infos[i].codepoint;
            glyphs[i].cluster = infos[i].cluster;
            glyphs[i].x_advance = advance;
            glyphs[i].y_advance = (float)positions[i].y_advance / 64.0f;
            glyphs[i].x_offset = (float)positions[i].x_offset / 64.0f;
            glyphs[i].y_offset = (float)positions[i].y_offset / 64.0f;
        }
    }

    hb_buffer_destroy(buffer);

    if (glyphs_out)
        *glyphs_out = glyphs;
    if (count_out)
        *count_out = count;
    if (width_out)
        *width_out = width;
    return 0;
}

int
mdwn_font_shape(struct mdwn_font *font,
                const char *text, size_t len,
                struct mdwn_arena *arena,
                struct mdwn_shaped_glyph **glyphs,
                size_t *glyph_count,
                float *width,
                char *err, size_t err_size)
{
    return shape_internal(font, text, len, arena, glyphs, glyph_count,
                          width, err, err_size);
}

int
mdwn_font_render_glyph(struct mdwn_font *font, uint32_t glyph_index,
                       struct mdwn_glyph_bitmap *bitmap,
                       char *err, size_t err_size)
{
    FT_Error ft_error;
    FT_GlyphSlot slot;

    ft_error = FT_Load_Glyph(font->face, glyph_index, FT_LOAD_DEFAULT);
    if (ft_error) {
        if (err && err_size)
            snprintf(err, err_size, "FreeType could not load glyph %u (%d)", glyph_index, ft_error);
        return -1;
    }

    slot = font->face->glyph;
    if (slot->format != FT_GLYPH_FORMAT_BITMAP) {
        ft_error = FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL);
        if (ft_error) {
            if (err && err_size)
                snprintf(err, err_size, "FreeType could not render glyph %u (%d)", glyph_index, ft_error);
            return -1;
        }
    }

    bitmap->buffer = slot->bitmap.buffer;
    bitmap->width = (int)slot->bitmap.width;
    bitmap->height = (int)slot->bitmap.rows;
    bitmap->pitch = slot->bitmap.pitch;
    bitmap->left = slot->bitmap_left;
    bitmap->top = slot->bitmap_top;
    bitmap->num_grays = slot->bitmap.num_grays;
    if (slot->bitmap.pixel_mode == FT_PIXEL_MODE_GRAY)
        bitmap->format = MDWN_BITMAP_GRAY;
    else if (slot->bitmap.pixel_mode == FT_PIXEL_MODE_MONO)
        bitmap->format = MDWN_BITMAP_MONO;
    else {
        set_error(err, err_size, "unsupported FreeType bitmap format");
        return -1;
    }
    return 0;
}
