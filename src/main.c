#include "config.h"
#include "flavor.h"
#include "render.h"
#include "version.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct options {
    const char *path;
    const char *flavor;
    const char *theme;
    bool no_config;
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
        "      --no-config    ignore system, user, and local configuration\n"
        "  -h, --help         show this help\n"
        "  -V, --version      show version\n");
}

static int
valid_theme(const char *name)
{
    return strcmp(name, "light") == 0 || strcmp(name, "dark") == 0;
}

static int
parse_options(struct options *options, int argc, char **argv)
{
    int i;

    memset(options, 0, sizeof(*options));

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

        if (strcmp(arg, "--no-config") == 0) {
            options->no_config = true;
            continue;
        }

        if (strcmp(arg, "-f") == 0 || strcmp(arg, "--flavor") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "mdwn: %s requires an argument\n", arg);
                return -1;
            }
            if (!mdwn_flavor_find(argv[i])) {
                fprintf(stderr, "mdwn: unsupported flavor '%s'\n", argv[i]);
                return -1;
            }
            options->flavor = argv[i];
            continue;
        }

        if (strncmp(arg, "--flavor=", 9) == 0) {
            if (!mdwn_flavor_find(arg + 9)) {
                fprintf(stderr, "mdwn: unsupported flavor '%s'\n", arg + 9);
                return -1;
            }
            options->flavor = arg + 9;
            continue;
        }

        if (strcmp(arg, "-t") == 0 || strcmp(arg, "--theme") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "mdwn: %s requires an argument\n", arg);
                return -1;
            }
            if (!valid_theme(argv[i])) {
                fprintf(stderr, "mdwn: unsupported theme '%s'\n", argv[i]);
                return -1;
            }
            options->theme = argv[i];
            continue;
        }

        if (strncmp(arg, "--theme=", 8) == 0) {
            if (!valid_theme(arg + 8)) {
                fprintf(stderr, "mdwn: unsupported theme '%s'\n", arg + 8);
                return -1;
            }
            options->theme = arg + 8;
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
    struct mdwn_config config;
    const struct mdwn_theme *theme;
    char error[512] = {0};
    char *title = NULL;
    int status = 1;

    if (parse_options(&options, argc, argv) < 0) {
        usage(stderr);
        return 2;
    }

    mdwn_config_init(&config);
    if (!options.no_config &&
        mdwn_config_load(&config, options.path, error, sizeof(error)) < 0) {
        fprintf(stderr, "mdwn: %s\n", error[0] ? error
                                                : "could not load configuration");
        return 1;
    }
    if (options.flavor)
        config.flavor = mdwn_flavor_find(options.flavor);
    if (options.theme)
        config.dark_theme = strcmp(options.theme, "dark") == 0;
    if (config.dark_theme && !config.flavor->dark_theme) {
        fprintf(stderr, "mdwn: flavor '%s' does not provide a dark theme\n",
                config.flavor->name);
        return 1;
    }

    theme = config.dark_theme ? config.flavor->dark_theme
                              : config.flavor->theme;

    title = make_title(options.path);
    if (!title) {
        fprintf(stderr, "mdwn: out of memory while creating window title\n");
        goto out;
    }

    error[0] = '\0';
    if (mdwn_viewer_run(title, options.path, config.flavor,
                        theme, &config.viewer,
                        error, sizeof(error)) < 0) {
        fprintf(stderr, "mdwn: %s\n", error[0] ? error : "viewer failed");
        goto out;
    }

    status = 0;
out:
    free(title);
    return status;
}
