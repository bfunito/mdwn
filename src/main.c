#include "document.h"
#include "flavor.h"
#include "markdown.h"
#include "render.h"
#include "version.h"

#include <SDL3/SDL.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct options {
    const char *path;
    const struct mdwn_flavor *flavor;
    bool dark_theme;
};

static void
usage(FILE *stream)
{
    fprintf(stream,
        "usage: mdwn [options] FILE\n"
        "\n"
        "Options:\n"
        "  -f, --flavor NAME  markdown flavor (github, gitlab, or codeberg)\n"
        "  -t, --theme NAME   visual theme (light or dark; default: light)\n"
        "  -h, --help         show this help\n"
        "  -V, --version      show version\n");
}

static int
set_theme(struct options *options, const char *name)
{
    if (strcmp(name, "light") == 0) {
        options->dark_theme = false;
        return 0;
    }
    if (strcmp(name, "dark") == 0) {
        options->dark_theme = true;
        return 0;
    }

    fprintf(stderr, "mdwn: unsupported theme '%s'\n", name);
    return -1;
}

static int
parse_options(struct options *options, int argc, char **argv)
{
    int i;

    memset(options, 0, sizeof(*options));
    options->flavor = mdwn_flavor_default();

    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (strcmp(arg, "--") == 0) {
            ++i;
            if (i >= argc) {
                fprintf(stderr, "mdwn: missing file after --\n");
                return -1;
            }
            if (options->path) {
                fprintf(stderr, "mdwn: only one input file is supported\n");
                return -1;
            }
            options->path = argv[i++];
            if (i != argc) {
                fprintf(stderr, "mdwn: only one input file is supported\n");
                return -1;
            }
            break;
        }

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            usage(stdout);
            exit(0);
        }

        if (strcmp(arg, "-V") == 0 || strcmp(arg, "--version") == 0) {
            printf("mdwn %s\n", MDWN_VERSION);
            exit(0);
        }

        if (strcmp(arg, "-f") == 0 || strcmp(arg, "--flavor") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "mdwn: %s requires an argument\n", arg);
                return -1;
            }
            options->flavor = mdwn_flavor_find(argv[i]);
            if (!options->flavor) {
                fprintf(stderr, "mdwn: unsupported flavor '%s'\n", argv[i]);
                return -1;
            }
            continue;
        }

        if (strncmp(arg, "--flavor=", 9) == 0) {
            options->flavor = mdwn_flavor_find(arg + 9);
            if (!options->flavor) {
                fprintf(stderr, "mdwn: unsupported flavor '%s'\n", arg + 9);
                return -1;
            }
            continue;
        }

        if (strcmp(arg, "-t") == 0 || strcmp(arg, "--theme") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "mdwn: %s requires an argument\n", arg);
                return -1;
            }
            if (set_theme(options, argv[i]) < 0)
                return -1;
            continue;
        }

        if (strncmp(arg, "--theme=", 8) == 0) {
            if (set_theme(options, arg + 8) < 0)
                return -1;
            continue;
        }

        if (arg[0] == '-' && arg[1] != '\0') {
            fprintf(stderr, "mdwn: unknown option '%s'\n", arg);
            return -1;
        }

        if (options->path) {
            fprintf(stderr, "mdwn: only one input file is supported\n");
            return -1;
        }
        options->path = arg;
    }

    if (!options->path) {
        fprintf(stderr, "mdwn: missing markdown file\n");
        return -1;
    }

    if (options->dark_theme && !options->flavor->dark_theme) {
        fprintf(stderr, "mdwn: flavor '%s' does not provide a dark theme\n",
                options->flavor->name);
        return -1;
    }

    return 0;
}

static char *
make_title(const char *path)
{
    const char *base = strrchr(path, '/');
    const char *name = base ? base + 1 : path;
    size_t name_len = strlen(name);
    const char suffix[] = " — mdwn";
    char *title = malloc(name_len + sizeof(suffix));

    if (!title)
        return NULL;

    memcpy(title, name, name_len);
    memcpy(title + name_len, suffix, sizeof(suffix));
    return title;
}

int
main(int argc, char **argv)
{
    struct options options;
    struct mdwn_document document;
    const struct mdwn_theme *theme;
    char *file;
    size_t file_size;
    char error[512] = {0};
    char *title = NULL;
    int status = 1;

    if (parse_options(&options, argc, argv) < 0) {
        usage(stderr);
        return 2;
    }

    theme = options.dark_theme ? options.flavor->dark_theme
                               : options.flavor->theme;

    file = SDL_LoadFile(options.path, &file_size);
    if (!file) {
        fprintf(stderr, "mdwn: %s\n", SDL_GetError());
        return 1;
    }

    if (mdwn_document_init(&document) < 0) {
        fprintf(stderr, "mdwn: out of memory while creating document\n");
        SDL_free(file);
        return 1;
    }

    if (mdwn_markdown_parse(&document, file, file_size,
                            options.flavor, error, sizeof(error)) < 0) {
        fprintf(stderr, "mdwn: %s\n", error[0] ? error : "could not parse markdown");
        goto out;
    }

    title = make_title(options.path);
    if (!title) {
        fprintf(stderr, "mdwn: out of memory while creating window title\n");
        goto out;
    }

    error[0] = '\0';
    if (mdwn_viewer_run(title, &document, theme,
                        error, sizeof(error)) < 0) {
        fprintf(stderr, "mdwn: %s\n", error[0] ? error : "viewer failed");
        goto out;
    }

    status = 0;
out:
    free(title);
    mdwn_document_destroy(&document);
    SDL_free(file);
    return status;
}
