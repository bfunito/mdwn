#ifndef MDWN_WATCH_H
#define MDWN_WATCH_H

#include <stdbool.h>
#include <stddef.h>

struct mdwn_watcher {
    int fd;
    char *name;
};

int mdwn_watcher_init(struct mdwn_watcher *watcher, const char *path,
                      char *err, size_t err_size);
void mdwn_watcher_destroy(struct mdwn_watcher *watcher);
int mdwn_watcher_poll(struct mdwn_watcher *watcher, bool *changed,
                      char *err, size_t err_size);

#endif
