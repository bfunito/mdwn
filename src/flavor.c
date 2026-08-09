#include "flavor.h"

#include "theme.h"

#include <md4c.h>
#include <stddef.h>
#include <string.h>

static const struct mdwn_flavor flavors[] = {
    {
        .name = "github",
        .parser_flags = MD_DIALECT_GITHUB,
        .theme = &mdwn_theme_github,
        .dark_theme = &mdwn_theme_github_dark,
    },
    {
        .name = "gitlab",
        .parser_flags = MD_DIALECT_GITHUB |
                        MD_FLAG_LATEXMATHSPANS |
                        MD_FLAG_WIKILINKS,
        .theme = &mdwn_theme_gitlab,
    },
    {
        .name = "codeberg",
        .parser_flags = MD_DIALECT_GITHUB |
                        MD_FLAG_LATEXMATHSPANS,
        .theme = &mdwn_theme_codeberg,
        .dark_theme = &mdwn_theme_codeberg_dark,
    },
};

const struct mdwn_flavor *
mdwn_flavor_default(void)
{
    return &flavors[0];
}

const struct mdwn_flavor *
mdwn_flavor_find(const char *name)
{
    size_t i;

    for (i = 0; i < sizeof(flavors) / sizeof(flavors[0]); ++i) {
        if (strcmp(name, flavors[i].name) == 0)
            return &flavors[i];
    }

    return NULL;
}
