#include "flavor.h"

#include <md4c.h>
#include <stddef.h>
#include <string.h>

static const struct mdwn_flavor flavors[] = {
    {
        .name = "github",
        .parser_flags = MD_DIALECT_GITHUB,
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
