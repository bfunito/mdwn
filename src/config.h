#ifndef MDWN_CONFIG_H
#define MDWN_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

struct mdwn_flavor;

struct mdwn_viewer_config {
    float initial_zoom;
    float min_zoom;
    float max_zoom;
    float wheel_zoom_speed;
    float scroll_step;
};

struct mdwn_config {
    const struct mdwn_flavor *flavor;
    struct mdwn_viewer_config viewer;
    bool dark_theme;
};

#ifdef __cplusplus
extern "C" {
#endif

void mdwn_config_init(struct mdwn_config *config);
int mdwn_config_load(struct mdwn_config *config, const char *document_path,
                     char *err, size_t err_size);

#ifdef __cplusplus
}
#endif

#endif
