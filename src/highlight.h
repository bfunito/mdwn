#ifndef MDWN_HIGHLIGHT_H
#define MDWN_HIGHLIGHT_H

#include <stddef.h>

enum mdwn_highlight_type {
    MDWN_HIGHLIGHT_NORMAL,
    MDWN_HIGHLIGHT_COMMENT,
    MDWN_HIGHLIGHT_KEYWORD,
    MDWN_HIGHLIGHT_TYPE,
    MDWN_HIGHLIGHT_STRING,
    MDWN_HIGHLIGHT_REGEXP,
    MDWN_HIGHLIGHT_SPECIAL_CHAR,
    MDWN_HIGHLIGHT_NUMBER,
    MDWN_HIGHLIGHT_PREPROCESSOR,
    MDWN_HIGHLIGHT_SYMBOL,
    MDWN_HIGHLIGHT_FUNCTION,
    MDWN_HIGHLIGHT_CLASS,
    MDWN_HIGHLIGHT_VARIABLE,
    MDWN_HIGHLIGHT_BUILTIN,
};

struct mdwn_highlighter;

typedef int (*mdwn_highlight_callback)(const char *text, size_t length,
                                      enum mdwn_highlight_type type,
                                      void *userdata);

#ifdef __cplusplus
extern "C" {
#endif

struct mdwn_highlighter *mdwn_highlighter_create(const char *language);
void mdwn_highlighter_destroy(struct mdwn_highlighter *highlighter);
int mdwn_highlighter_highlight(struct mdwn_highlighter *highlighter,
                               const char *line, size_t length,
                               mdwn_highlight_callback callback,
                               void *userdata);

#ifdef __cplusplus
}
#endif

#endif
