#ifndef MDWN_RENDER_H
#define MDWN_RENDER_H

#include <stddef.h>

struct mdwn_flavor;
struct mdwn_theme;
struct mdwn_viewer_config;

int mdwn_viewer_run(const char *title, const char *document_path,
                    const struct mdwn_flavor *flavor,
                    const struct mdwn_theme *theme,
                    const struct mdwn_viewer_config *config,
                    char *err, size_t err_size);

#endif
