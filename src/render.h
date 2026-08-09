#ifndef MDWN_RENDER_H
#define MDWN_RENDER_H

#include <stddef.h>

struct mdwn_document;
struct mdwn_theme;

int mdwn_viewer_run(const char *title, const struct mdwn_document *doc,
                    const struct mdwn_theme *theme,
                    char *err, size_t err_size);

#endif
