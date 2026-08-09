#ifndef MDWN_ARENA_H
#define MDWN_ARENA_H

#include <stddef.h>

struct mdwn_arena_block;

struct mdwn_arena {
    struct mdwn_arena_block *head;
    size_t block_size;
};

void mdwn_arena_init(struct mdwn_arena *arena, size_t block_size);
void mdwn_arena_destroy(struct mdwn_arena *arena);
void *mdwn_arena_alloc(struct mdwn_arena *arena, size_t size);
char *mdwn_arena_strndup(struct mdwn_arena *arena, const char *s, size_t len);

#endif
