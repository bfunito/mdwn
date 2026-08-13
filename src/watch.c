#include "watch.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __linux__
#include <sys/inotify.h>
#include <unistd.h>
#endif

static void
set_error(char *err, size_t err_size, const char *message)
{
    if (err && err_size)
        snprintf(err, err_size, "%s", message);
}

int
mdwn_watcher_init(struct mdwn_watcher *watcher, const char *path,
                  char *err, size_t err_size)
{
    watcher->fd = -1;
    watcher->name = NULL;

#ifdef __linux__
    const char *slash = strrchr(path, '/');
    const char *name = slash ? slash + 1 : path;
    const char *directory = ".";
    char *allocated_directory = NULL;

    if (!name[0]) {
        set_error(err, err_size, "document path has no filename");
        return -1;
    }

    if (slash) {
        size_t len = (size_t)(slash - path);

        if (len == 0)
            directory = "/";
        else {
            allocated_directory = malloc(len + 1);
            if (!allocated_directory)
                goto out_of_memory;
            memcpy(allocated_directory, path, len);
            allocated_directory[len] = '\0';
            directory = allocated_directory;
        }
    }

    watcher->name = strdup(name);
    if (!watcher->name)
        goto out_of_memory;

    watcher->fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (watcher->fd < 0)
        goto system_error;
    if (inotify_add_watch(watcher->fd, directory,
                          IN_CLOSE_WRITE | IN_CREATE | IN_MOVED_TO) < 0)
        goto system_error;

    free(allocated_directory);
    return 0;

system_error:
    if (err && err_size)
        snprintf(err, err_size, "could not watch '%s': %s",
                 directory, strerror(errno));
    free(allocated_directory);
    mdwn_watcher_destroy(watcher);
    return -1;

out_of_memory:
    free(allocated_directory);
    mdwn_watcher_destroy(watcher);
    set_error(err, err_size, "out of memory while watching document");
    return -1;
#else
    (void)path;
    set_error(err, err_size, "filesystem watching is not supported");
    return -1;
#endif
}

void
mdwn_watcher_destroy(struct mdwn_watcher *watcher)
{
#ifdef __linux__
    if (watcher->fd >= 0)
        close(watcher->fd);
#endif
    free(watcher->name);
    watcher->fd = -1;
    watcher->name = NULL;
}

int
mdwn_watcher_poll(struct mdwn_watcher *watcher, bool *changed,
                  char *err, size_t err_size)
{
    *changed = false;

#ifdef __linux__
    _Alignas(struct inotify_event) char buffer[4096];

    if (watcher->fd < 0)
        return 0;

    for (;;) {
        ssize_t size = read(watcher->fd, buffer, sizeof(buffer));
        size_t offset = 0;

        if (size < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return 0;
            if (errno == EINTR)
                continue;
            if (err && err_size)
                snprintf(err, err_size, "could not read filesystem events: %s",
                         strerror(errno));
            return -1;
        }

        while (offset < (size_t)size) {
            const struct inotify_event *event =
                (const struct inotify_event *)(buffer + offset);

            if ((event->mask & IN_Q_OVERFLOW) ||
                (event->len && strcmp(event->name, watcher->name) == 0))
                *changed = true;
            offset += sizeof(*event) + event->len;
        }
    }
#else
    (void)watcher;
    (void)err;
    (void)err_size;
    return 0;
#endif
}
