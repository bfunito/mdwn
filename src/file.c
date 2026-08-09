#include "file.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void
set_error(char *err, size_t err_size, const char *path)
{
    if (err && err_size)
        snprintf(err, err_size, "%s: %s", path, strerror(errno));
}

int
mdwn_file_load(struct mdwn_file *file, const char *path,
               char *err, size_t err_size)
{
    struct stat st;
    size_t off = 0;
    int fd;

    file->data = NULL;
    file->size = 0;

    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        set_error(err, err_size, path);
        return -1;
    }

    if (fstat(fd, &st) < 0) {
        set_error(err, err_size, path);
        close(fd);
        return -1;
    }

    if (!S_ISREG(st.st_mode)) {
        if (err && err_size)
            snprintf(err, err_size, "%s: not a regular file", path);
        close(fd);
        return -1;
    }

    if (st.st_size < 0 || (uintmax_t)st.st_size > SIZE_MAX - 1) {
        if (err && err_size)
            snprintf(err, err_size, "%s: file is too large", path);
        close(fd);
        return -1;
    }

    file->size = (size_t)st.st_size;
    file->data = malloc(file->size + 1);
    if (!file->data) {
        if (err && err_size)
            snprintf(err, err_size, "%s: out of memory", path);
        close(fd);
        return -1;
    }

    while (off < file->size) {
        ssize_t n = read(fd, file->data + off, file->size - off);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            set_error(err, err_size, path);
            free(file->data);
            file->data = NULL;
            file->size = 0;
            close(fd);
            return -1;
        }

        if (n == 0) {
            if (err && err_size)
                snprintf(err, err_size, "%s: unexpected end of file", path);
            free(file->data);
            file->data = NULL;
            file->size = 0;
            close(fd);
            return -1;
        }

        off += (size_t)n;
    }

    file->data[file->size] = '\0';
    close(fd);
    return 0;
}

void
mdwn_file_unload(struct mdwn_file *file)
{
    free(file->data);
    file->data = NULL;
    file->size = 0;
}
