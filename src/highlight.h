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

struct mdwn_highlight_cache;

enum mdwn_highlight_result {
    MDWN_HIGHLIGHT_UNAVAILABLE,
    MDWN_HIGHLIGHT_MISS,
    MDWN_HIGHLIGHT_HIT,
};

typedef int (*mdwn_highlight_callback)(size_t line,
                                      const char *text, size_t length,
                                      enum mdwn_highlight_type type,
                                      void *userdata);

#ifdef __cplusplus
extern "C" {
#endif

struct mdwn_highlight_cache *mdwn_highlight_cache_create(void);
void mdwn_highlight_cache_destroy(struct mdwn_highlight_cache *cache);
void mdwn_highlight_cache_begin(struct mdwn_highlight_cache *cache);
void mdwn_highlight_cache_end(struct mdwn_highlight_cache *cache,
                              int success);
int mdwn_highlight_cache_highlight(struct mdwn_highlight_cache *cache,
                                   const char *language,
                                   const char *text, size_t length,
                                   mdwn_highlight_callback callback,
                                   void *userdata);

#ifdef __cplusplus
}
#endif

#endif
