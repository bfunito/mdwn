#ifndef MDWN_DOCUMENT_H
#define MDWN_DOCUMENT_H

#include "arena.h"

#include <stdbool.h>
#include <stddef.h>


enum mdwn_node_type {
    MDWN_NODE_DOCUMENT,
    MDWN_NODE_BLOCKQUOTE,
    MDWN_NODE_UNORDERED_LIST,
    MDWN_NODE_ORDERED_LIST,
    MDWN_NODE_LIST_ITEM,
    MDWN_NODE_HORIZONTAL_RULE,
    MDWN_NODE_HEADING,
    MDWN_NODE_CODE_BLOCK,
    MDWN_NODE_RAW_HTML_BLOCK,
    MDWN_NODE_PARAGRAPH,
    MDWN_NODE_TABLE,
    MDWN_NODE_TABLE_HEAD,
    MDWN_NODE_TABLE_BODY,
    MDWN_NODE_TABLE_ROW,
    MDWN_NODE_TABLE_HEADER_CELL,
    MDWN_NODE_TABLE_CELL,

    MDWN_NODE_EMPHASIS,
    MDWN_NODE_STRONG,
    MDWN_NODE_LINK,
    MDWN_NODE_IMAGE,
    MDWN_NODE_CODE_SPAN,
    MDWN_NODE_STRIKETHROUGH,
    MDWN_NODE_RAW_HTML_SPAN,

    MDWN_NODE_TEXT,
    MDWN_NODE_SOFT_BREAK,
    MDWN_NODE_HARD_BREAK,
};

enum mdwn_align {
    MDWN_ALIGN_DEFAULT,
    MDWN_ALIGN_LEFT,
    MDWN_ALIGN_CENTER,
    MDWN_ALIGN_RIGHT,
};

struct mdwn_node {
    enum mdwn_node_type type;

    struct mdwn_node *parent;
    struct mdwn_node *first_child;
    struct mdwn_node *last_child;
    struct mdwn_node *next;

    union {
        struct {
            unsigned level;
        } heading;

        struct {
            unsigned start;
            bool tight;
        } list;

        struct {
            bool task;
            bool checked;
        } list_item;

        struct {
            const char *language;
        } code_block;

        struct {
            const char *href;
        } link;

        struct {
            const char *src;
        } image;

        struct {
            unsigned columns;
        } table;

        struct {
            enum mdwn_align align;
        } table_cell;

        struct {
            const char *data;
            size_t length;
        } text;
    } as;
};

struct mdwn_document {
    struct mdwn_arena arena;
    struct mdwn_node *root;
};

int mdwn_document_init(struct mdwn_document *doc);
void mdwn_document_destroy(struct mdwn_document *doc);
struct mdwn_node *mdwn_document_new_node(struct mdwn_document *doc,
                                         enum mdwn_node_type type);
void mdwn_node_append(struct mdwn_node *parent, struct mdwn_node *child);
char *mdwn_document_strndup(struct mdwn_document *doc,
                            const char *s, size_t len);

#endif
