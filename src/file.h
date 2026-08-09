#ifndef MDWN_FILE_H
#define MDWN_FILE_H

#include <stddef.h>

struct mdwn_file {
    char *data;
    size_t size;
};

int mdwn_file_load(struct mdwn_file *file, const char *path,
                   char *err, size_t err_size);
void mdwn_file_unload(struct mdwn_file *file);

#endif
