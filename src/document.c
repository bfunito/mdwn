#include "document.h"

#include <string.h>

int
mdwn_document_init(struct mdwn_document *doc)
{
    mdwn_arena_init(&doc->arena, 16 * 1024);
    doc->root = mdwn_document_new_node(doc, MDWN_NODE_DOCUMENT);
    return doc->root ? 0 : -1;
}

void
mdwn_document_destroy(struct mdwn_document *doc)
{
    mdwn_arena_destroy(&doc->arena);
    doc->root = NULL;
}

struct mdwn_node *
mdwn_document_new_node(struct mdwn_document *doc, enum mdwn_node_type type)
{
    struct mdwn_node *node = mdwn_arena_alloc(&doc->arena, sizeof(*node));

    if (!node)
        return NULL;

    memset(node, 0, sizeof(*node));
    node->type = type;
    return node;
}

void
mdwn_node_append(struct mdwn_node *parent, struct mdwn_node *child)
{
    child->parent = parent;

    if (parent->last_child)
        parent->last_child->next = child;
    else
        parent->first_child = child;

    parent->last_child = child;
}

char *
mdwn_document_strndup(struct mdwn_document *doc, const char *s, size_t len)
{
    return mdwn_arena_strndup(&doc->arena, s, len);
}
