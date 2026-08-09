#include "markdown.h"

#include "document.h"
#include "flavor.h"

#include <md4c.h>

#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

struct parser_context {
    struct mdwn_document *doc;
    struct mdwn_node *current;
    char *err;
    size_t err_size;
};

static void
parser_error(struct parser_context *ctx, const char *message)
{
    if (ctx->err && ctx->err_size)
        snprintf(ctx->err, ctx->err_size, "%s", message);
}

static enum mdwn_align
convert_align(MD_ALIGN align)
{
    switch (align) {
    case MD_ALIGN_LEFT:
        return MDWN_ALIGN_LEFT;
    case MD_ALIGN_CENTER:
        return MDWN_ALIGN_CENTER;
    case MD_ALIGN_RIGHT:
        return MDWN_ALIGN_RIGHT;
    case MD_ALIGN_DEFAULT:
    default:
        return MDWN_ALIGN_DEFAULT;
    }
}

static char *
copy_attribute(struct parser_context *ctx, const MD_ATTRIBUTE *attr)
{
    return mdwn_document_strndup(ctx->doc, attr->text, attr->size);
}

static int
append_and_enter(struct parser_context *ctx, struct mdwn_node *node)
{
    if (!node) {
        parser_error(ctx, "out of memory while parsing markdown");
        return 1;
    }

    mdwn_node_append(ctx->current, node);
    ctx->current = node;
    return 0;
}

static int
enter_block(MD_BLOCKTYPE type, void *detail, void *userdata)
{
    struct parser_context *ctx = userdata;
    struct mdwn_node *node = NULL;

    if (type == MD_BLOCK_DOC)
        return 0;

    switch (type) {
    case MD_BLOCK_QUOTE:
        node = mdwn_document_new_node(ctx->doc, MDWN_NODE_BLOCKQUOTE);
        break;
    case MD_BLOCK_UL: {
        const MD_BLOCK_UL_DETAIL *d = detail;
        node = mdwn_document_new_node(ctx->doc, MDWN_NODE_UNORDERED_LIST);
        if (node) {
            node->as.list.start = 1;
            node->as.list.tight = d && d->is_tight;
        }
        break;
    }
    case MD_BLOCK_OL: {
        const MD_BLOCK_OL_DETAIL *d = detail;
        node = mdwn_document_new_node(ctx->doc, MDWN_NODE_ORDERED_LIST);
        if (node) {
            node->as.list.start = d ? d->start : 1;
            node->as.list.tight = d && d->is_tight;
        }
        break;
    }
    case MD_BLOCK_LI: {
        const MD_BLOCK_LI_DETAIL *d = detail;
        node = mdwn_document_new_node(ctx->doc, MDWN_NODE_LIST_ITEM);
        if (node && d) {
            node->as.list_item.task = d->is_task != 0;
            node->as.list_item.checked = d->task_mark == 'x' || d->task_mark == 'X';
        }
        break;
    }
    case MD_BLOCK_HR:
        node = mdwn_document_new_node(ctx->doc, MDWN_NODE_HORIZONTAL_RULE);
        break;
    case MD_BLOCK_H: {
        const MD_BLOCK_H_DETAIL *d = detail;
        node = mdwn_document_new_node(ctx->doc, MDWN_NODE_HEADING);
        if (node)
            node->as.heading.level = d ? d->level : 1;
        break;
    }
    case MD_BLOCK_CODE: {
        const MD_BLOCK_CODE_DETAIL *d = detail;
        node = mdwn_document_new_node(ctx->doc, MDWN_NODE_CODE_BLOCK);
        if (node && d && d->lang.size) {
            node->as.code_block.language = copy_attribute(ctx, &d->lang);
            if (!node->as.code_block.language) {
                parser_error(ctx, "out of memory while parsing code block");
                return 1;
            }
        }
        break;
    }
    case MD_BLOCK_HTML:
        node = mdwn_document_new_node(ctx->doc, MDWN_NODE_RAW_HTML_BLOCK);
        break;
    case MD_BLOCK_P:
        node = mdwn_document_new_node(ctx->doc, MDWN_NODE_PARAGRAPH);
        break;
    case MD_BLOCK_TABLE: {
        const MD_BLOCK_TABLE_DETAIL *d = detail;
        node = mdwn_document_new_node(ctx->doc, MDWN_NODE_TABLE);
        if (node)
            node->as.table.columns = d ? d->col_count : 0;
        break;
    }
    case MD_BLOCK_THEAD:
        node = mdwn_document_new_node(ctx->doc, MDWN_NODE_TABLE_HEAD);
        break;
    case MD_BLOCK_TBODY:
        node = mdwn_document_new_node(ctx->doc, MDWN_NODE_TABLE_BODY);
        break;
    case MD_BLOCK_TR:
        node = mdwn_document_new_node(ctx->doc, MDWN_NODE_TABLE_ROW);
        break;
    case MD_BLOCK_TH:
    case MD_BLOCK_TD: {
        const MD_BLOCK_TD_DETAIL *d = detail;
        node = mdwn_document_new_node(ctx->doc,
                type == MD_BLOCK_TH ? MDWN_NODE_TABLE_HEADER_CELL
                                    : MDWN_NODE_TABLE_CELL);
        if (node)
            node->as.table_cell.align = d ? convert_align(d->align) : MDWN_ALIGN_DEFAULT;
        break;
    }
    case MD_BLOCK_DOC:
        return 0;
    default:
        /* Keep future MD4C block types readable even before mdwn knows
         * their specific semantics. */
        node = mdwn_document_new_node(ctx->doc, MDWN_NODE_PARAGRAPH);
        break;
    }

    return append_and_enter(ctx, node);
}

static int
leave_block(MD_BLOCKTYPE type, void *detail, void *userdata)
{
    struct parser_context *ctx = userdata;
    (void)detail;

    if (type != MD_BLOCK_DOC && ctx->current->parent)
        ctx->current = ctx->current->parent;

    return 0;
}

static int
enter_span(MD_SPANTYPE type, void *detail, void *userdata)
{
    struct parser_context *ctx = userdata;
    struct mdwn_node *node = NULL;

    switch (type) {
    case MD_SPAN_EM:
        node = mdwn_document_new_node(ctx->doc, MDWN_NODE_EMPHASIS);
        break;
    case MD_SPAN_STRONG:
        node = mdwn_document_new_node(ctx->doc, MDWN_NODE_STRONG);
        break;
    case MD_SPAN_A: {
        const MD_SPAN_A_DETAIL *d = detail;
        node = mdwn_document_new_node(ctx->doc, MDWN_NODE_LINK);
        if (node && d) {
            node->as.link.href = copy_attribute(ctx, &d->href);
            if (!node->as.link.href) {
                parser_error(ctx, "out of memory while parsing link");
                return 1;
            }
        }
        break;
    }
    case MD_SPAN_IMG: {
        const MD_SPAN_IMG_DETAIL *d = detail;
        node = mdwn_document_new_node(ctx->doc, MDWN_NODE_IMAGE);
        if (node && d) {
            node->as.image.src = copy_attribute(ctx, &d->src);
            if (!node->as.image.src) {
                parser_error(ctx, "out of memory while parsing image");
                return 1;
            }
        }
        break;
    }
    case MD_SPAN_CODE:
        node = mdwn_document_new_node(ctx->doc, MDWN_NODE_CODE_SPAN);
        break;
    case MD_SPAN_DEL:
        node = mdwn_document_new_node(ctx->doc, MDWN_NODE_STRIKETHROUGH);
        break;
    case MD_SPAN_LATEXMATH:
    case MD_SPAN_LATEXMATH_DISPLAY:
    case MD_SPAN_WIKILINK:
    case MD_SPAN_U:
        node = mdwn_document_new_node(ctx->doc, MDWN_NODE_RAW_HTML_SPAN);
        break;
    default:
        /* Unknown future inline extensions degrade to a transparent
         * container instead of making the whole document fail. */
        node = mdwn_document_new_node(ctx->doc, MDWN_NODE_RAW_HTML_SPAN);
        break;
    }

    return append_and_enter(ctx, node);
}

static int
leave_span(MD_SPANTYPE type, void *detail, void *userdata)
{
    struct parser_context *ctx = userdata;
    (void)type;
    (void)detail;

    if (ctx->current->parent)
        ctx->current = ctx->current->parent;

    return 0;
}

static size_t
utf8_encode(uint32_t cp, char out[4])
{
    if (cp <= 0x7f) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp <= 0x7ff) {
        out[0] = (char)(0xc0u | (cp >> 6));
        out[1] = (char)(0x80u | (cp & 0x3fu));
        return 2;
    }
    if (cp <= 0xffff) {
        out[0] = (char)(0xe0u | (cp >> 12));
        out[1] = (char)(0x80u | ((cp >> 6) & 0x3fu));
        out[2] = (char)(0x80u | (cp & 0x3fu));
        return 3;
    }
    if (cp <= 0x10ffff) {
        out[0] = (char)(0xf0u | (cp >> 18));
        out[1] = (char)(0x80u | ((cp >> 12) & 0x3fu));
        out[2] = (char)(0x80u | ((cp >> 6) & 0x3fu));
        out[3] = (char)(0x80u | (cp & 0x3fu));
        return 4;
    }
    return 0;
}

static int
parse_numeric_entity(const char *text, size_t len, uint32_t *cp)
{
    size_t i;
    unsigned base = 10;
    uint32_t value = 0;

    if (len < 4 || text[0] != '&' || text[1] != '#' || text[len - 1] != ';')
        return 0;

    i = 2;
    if (i < len - 1 && (text[i] == 'x' || text[i] == 'X')) {
        base = 16;
        ++i;
    }
    if (i >= len - 1)
        return 0;

    for (; i < len - 1; ++i) {
        unsigned digit;
        unsigned char c = (unsigned char)text[i];

        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f')
            digit = 10u + c - 'a';
        else if (base == 16 && c >= 'A' && c <= 'F')
            digit = 10u + c - 'A';
        else
            return 0;

        if (value > (0x10ffffu - digit) / base)
            return 0;
        value = value * base + digit;
    }

    if (value == 0 || value > 0x10ffffu || (value >= 0xd800u && value <= 0xdfffu))
        value = 0xfffdu;

    *cp = value;
    return 1;
}

static int
append_text_node(struct parser_context *ctx, const char *text, size_t len)
{
    struct mdwn_node *node = mdwn_document_new_node(ctx->doc, MDWN_NODE_TEXT);

    if (!node) {
        parser_error(ctx, "out of memory while parsing text");
        return 1;
    }

    node->as.text.data = mdwn_document_strndup(ctx->doc, text, len);
    if (!node->as.text.data) {
        parser_error(ctx, "out of memory while parsing text");
        return 1;
    }
    node->as.text.length = len;
    mdwn_node_append(ctx->current, node);
    return 0;
}

static int
append_entity(struct parser_context *ctx, const char *text, size_t len)
{
    struct named_entity {
        const char *name;
        const char *value;
        size_t value_len;
    };
    static const struct named_entity named[] = {
        { "&amp;",  "&",       1 },
        { "&lt;",   "<",       1 },
        { "&gt;",   ">",       1 },
        { "&quot;", "\"",      1 },
        { "&apos;", "'",       1 },
        { "&nbsp;", "\xc2\xa0", 2 },
    };
    uint32_t cp;
    char encoded[4];
    size_t i;

    if (parse_numeric_entity(text, len, &cp)) {
        size_t n = utf8_encode(cp, encoded);
        if (n)
            return append_text_node(ctx, encoded, n);
    }

    for (i = 0; i < sizeof(named) / sizeof(named[0]); ++i) {
        size_t name_len = strlen(named[i].name);
        if (len == name_len && memcmp(text, named[i].name, len) == 0)
            return append_text_node(ctx, named[i].value, named[i].value_len);
    }

    return append_text_node(ctx, text, len);
}

static int
text_callback(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size, void *userdata)
{
    struct parser_context *ctx = userdata;
    struct mdwn_node *node;

    switch (type) {
    case MD_TEXT_SOFTBR:
        node = mdwn_document_new_node(ctx->doc, MDWN_NODE_SOFT_BREAK);
        break;
    case MD_TEXT_BR:
        node = mdwn_document_new_node(ctx->doc, MDWN_NODE_HARD_BREAK);
        break;
    case MD_TEXT_NULLCHAR:
        return append_text_node(ctx, "\xef\xbf\xbd", 3);
    case MD_TEXT_ENTITY:
        return append_entity(ctx, text, size);
    case MD_TEXT_NORMAL:
    case MD_TEXT_CODE:
    case MD_TEXT_HTML:
    case MD_TEXT_LATEXMATH:
        return append_text_node(ctx, text, size);
    default:
        return append_text_node(ctx, text, size);
    }

    if (!node) {
        parser_error(ctx, "out of memory while parsing markdown");
        return 1;
    }

    mdwn_node_append(ctx->current, node);
    return 0;
}

static void
debug_log(const char *message, void *userdata)
{
    struct parser_context *ctx = userdata;
    parser_error(ctx, message);
}

int
mdwn_markdown_parse(struct mdwn_document *doc,
                    const char *text, size_t length,
                    const struct mdwn_flavor *flavor,
                    char *err, size_t err_size)
{
    struct parser_context ctx;
    MD_PARSER parser;
    int rc;

    if (length > UINT_MAX) {
        if (err && err_size)
            snprintf(err, err_size, "markdown input is too large for MD4C");
        return -1;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.doc = doc;
    ctx.current = doc->root;
    ctx.err = err;
    ctx.err_size = err_size;

    memset(&parser, 0, sizeof(parser));
    parser.abi_version = 0;
    parser.flags = flavor->parser_flags;
    parser.enter_block = enter_block;
    parser.leave_block = leave_block;
    parser.enter_span = enter_span;
    parser.leave_span = leave_span;
    parser.text = text_callback;
    parser.debug_log = debug_log;

    rc = md_parse(text, (MD_SIZE)length, &parser, &ctx);
    if (rc != 0) {
        if (err && err_size && err[0] == '\0')
            snprintf(err, err_size, "MD4C failed with code %d", rc);
        return -1;
    }

    return 0;
}
