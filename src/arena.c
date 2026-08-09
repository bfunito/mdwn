#include "arena.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct mdwn_arena_block {
    struct mdwn_arena_block *next;
    size_t used;
    size_t capacity;
    max_align_t data[];
};

static size_t
align_up(size_t n)
{
    const size_t align = _Alignof(max_align_t);
    const size_t rem = n % align;

    if (rem == 0)
        return n;
    if (n > SIZE_MAX - (align - rem))
        return 0;
    return n + (align - rem);
}

void
mdwn_arena_init(struct mdwn_arena *arena, size_t block_size)
{
    arena->head = NULL;
    arena->block_size = block_size ? block_size : 4096;
}

void
mdwn_arena_destroy(struct mdwn_arena *arena)
{
    struct mdwn_arena_block *block = arena->head;

    while (block) {
        struct mdwn_arena_block *next = block->next;
        free(block);
        block = next;
    }

    arena->head = NULL;
}

void *
mdwn_arena_alloc(struct mdwn_arena *arena, size_t size)
{
    struct mdwn_arena_block *block;
    size_t aligned;
    size_t capacity;

    if (size == 0)
        size = 1;

    aligned = align_up(size);
    if (aligned == 0)
        return NULL;

    block = arena->head;
    if (block && aligned <= block->capacity - block->used) {
        void *ptr = (unsigned char *)block->data + block->used;
        block->used += aligned;
        return ptr;
    }

    capacity = arena->block_size;
    if (capacity < aligned)
        capacity = aligned;

    if (capacity > SIZE_MAX - sizeof(*block))
        return NULL;

    block = malloc(sizeof(*block) + capacity);
    if (!block)
        return NULL;

    block->next = arena->head;
    block->used = aligned;
    block->capacity = capacity;
    arena->head = block;

    return block->data;
}

char *
mdwn_arena_strndup(struct mdwn_arena *arena, const char *s, size_t len)
{
    char *copy;

    if (len == SIZE_MAX)
        return NULL;

    copy = mdwn_arena_alloc(arena, len + 1);
    if (!copy)
        return NULL;

    memcpy(copy, s, len);
    copy[len] = '\0';
    return copy;
}
