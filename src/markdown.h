#ifndef MDWN_MARKDOWN_H
#define MDWN_MARKDOWN_H

#include <stddef.h>

struct mdwn_document;
struct mdwn_flavor;

int mdwn_markdown_parse(struct mdwn_document *doc,
                        const char *text, size_t length,
                        const struct mdwn_flavor *flavor,
                        char *err, size_t err_size);

#endif
