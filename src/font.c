#include "font.h"

#include "theme.h"

#include <fontconfig/fontconfig.h>

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
    TTF_Font *handle;
};

struct mdwn_font_system {
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

static void
set_ttf_error(char *err, size_t err_size, const char *operation)
{
    if (err && err_size)
        snprintf(err, err_size, "%s: %s", operation, SDL_GetError());
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

    *out = NULL;

    if (!FcInit()) {
        set_error(err, err_size, "fontconfig initialization failed");
        return -1;
    }
    if (!TTF_Init()) {
        set_ttf_error(err, err_size, "SDL_ttf initialization failed");
        FcFini();
        return -1;
    }

    system = calloc(1, sizeof(*system));
    if (!system) {
        set_error(err, err_size, "out of memory while initializing fonts");
        TTF_Quit();
        FcFini();
        return -1;
    }

    system->theme = theme;
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
        TTF_CloseFont(font->handle);
        free(font);
        font = next;
    }

    for (family = 0; family < 2; ++family) {
        for (weight = 0; weight < 3; ++weight) {
            for (italic = 0; italic < 2; ++italic)
                free(system->sources[family][weight][italic].path);
        }
    }

    free(system);
    TTF_Quit();
    FcFini();
}

static bool
same_spec(struct mdwn_font_spec a, struct mdwn_font_spec b)
{
    return a.family == b.family &&
           a.size_px == b.size_px &&
           a.weight == b.weight &&
           a.italic == b.italic &&
           a.strike == b.strike &&
           a.underline == b.underline;
}

struct mdwn_font *
mdwn_font_get(struct mdwn_font_system *system, struct mdwn_font_spec spec,
              char *err, size_t err_size)
{
    struct font_source *source;
    struct mdwn_font *font;
    SDL_PropertiesID props;
    TTF_FontStyleFlags style = TTF_STYLE_NORMAL;

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

    props = SDL_CreateProperties();
    if (!props ||
        !SDL_SetStringProperty(props, TTF_PROP_FONT_CREATE_FILENAME_STRING,
                               source->path) ||
        !SDL_SetFloatProperty(props, TTF_PROP_FONT_CREATE_SIZE_FLOAT,
                              (float)spec.size_px) ||
        !SDL_SetNumberProperty(props, TTF_PROP_FONT_CREATE_FACE_NUMBER,
                               source->face_index)) {
        set_ttf_error(err, err_size, "could not configure font");
        if (props)
            SDL_DestroyProperties(props);
        free(font);
        return NULL;
    }

    font->handle = TTF_OpenFontWithProperties(props);
    SDL_DestroyProperties(props);
    if (!font->handle) {
        if (err && err_size)
            snprintf(err, err_size, "could not open font '%s': %s",
                     source->path, SDL_GetError());
        free(font);
        return NULL;
    }

    if (spec.strike)
        style |= TTF_STYLE_STRIKETHROUGH;
    if (spec.underline)
        style |= TTF_STYLE_UNDERLINE;
    TTF_SetFontStyle(font->handle, style);

    font->spec = spec;
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

TTF_Font *
mdwn_font_handle(struct mdwn_font *font)
{
    return font->handle;
}

float
mdwn_font_ascender(const struct mdwn_font *font)
{
    return (float)TTF_GetFontAscent(font->handle);
}

float
mdwn_font_descender(const struct mdwn_font *font)
{
    return -(float)TTF_GetFontDescent(font->handle);
}

TTF_Text *
mdwn_font_create_text(struct mdwn_font *font,
                      const char *text, size_t len,
                      float *width, size_t *cluster_count,
                      char *err, size_t err_size)
{
    TTF_Text *object;
    TTF_SubString **substrings;
    int w;
    int count;

    if (len > INT_MAX) {
        set_error(err, err_size, "text run is too large to lay out");
        return NULL;
    }

    object = TTF_CreateText(NULL, font->handle, text, len);
    if (!object || !TTF_GetTextSize(object, &w, NULL)) {
        set_ttf_error(err, err_size, "could not lay out text");
        if (object)
            TTF_DestroyText(object);
        return NULL;
    }

    *width = (float)w;
    if (cluster_count) {
        substrings = TTF_GetTextSubStringsForRange(object, 0, (int)len,
                                                   &count);
        if (!substrings) {
            set_ttf_error(err, err_size, "could not inspect text layout");
            TTF_DestroyText(object);
            return NULL;
        }
        SDL_free(substrings);
        *cluster_count = (size_t)count;
    }
    return object;
}
