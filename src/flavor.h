#ifndef MDWN_FLAVOR_H
#define MDWN_FLAVOR_H

struct mdwn_theme;

struct mdwn_flavor {
    const char *name;
    unsigned parser_flags;
    const struct mdwn_theme *theme;
    const struct mdwn_theme *dark_theme;
};

const struct mdwn_flavor *mdwn_flavor_default(void);
const struct mdwn_flavor *mdwn_flavor_find(const char *name);

#endif
