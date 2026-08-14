#include "highlight.h"

#include <srchilite/formatter.h>
#include <srchilite/formattermanager.h>
#include <srchilite/langdefmanager.h>
#include <srchilite/langmap.h>
#include <srchilite/regexrulefactory.h>
#include <srchilite/settings.h>
#include <srchilite/sourcehighlighter.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct mdwn_highlighter;

class token_formatter : public srchilite::Formatter {
    struct mdwn_highlighter *highlighter;
    enum mdwn_highlight_type type;

public:
    token_formatter(struct mdwn_highlighter *highlighter_,
                    enum mdwn_highlight_type type_)
        : highlighter(highlighter_), type(type_)
    {
    }

    void format(const std::string &text,
                const srchilite::FormatterParams *params);
};

struct mdwn_highlighter {
    std::unique_ptr<srchilite::FormatterManager> formatter_manager;
    std::unique_ptr<srchilite::SourceHighlighter> source_highlighter;
    int (*callback)(const char *, size_t, enum mdwn_highlight_type, void *);
    void *userdata;
    int failed;

    mdwn_highlighter(srchilite::HighlightStatePtr state);
    void add_formatter(const char *name, enum mdwn_highlight_type type);
    void reset();
    int highlight(const char *line, size_t length,
                  int (*fn)(const char *, size_t,
                            enum mdwn_highlight_type, void *), void *data);
};

struct cached_run {
    size_t line;
    size_t offset;
    size_t length;
    enum mdwn_highlight_type type;
};

struct cached_block {
    bool used = true;
    bool highlighted = false;
    std::vector<cached_run> runs;
};

struct mdwn_highlight_cache {
    std::string data_dir;
    srchilite::RegexRuleFactory rule_factory;
    srchilite::LangDefManager lang_manager;
    srchilite::LangMap lang_map;
    std::unordered_map<std::string,
                       std::unique_ptr<mdwn_highlighter>> highlighters;
    std::unordered_map<std::string, cached_block> blocks;

    mdwn_highlight_cache(const std::string &directory)
        : data_dir(directory), lang_manager(&rule_factory),
          lang_map(data_dir, "lang.map")
    {
    }

    mdwn_highlighter *get_highlighter(const std::string &language);
};

void
token_formatter::format(const std::string &text,
                        const srchilite::FormatterParams *params)
{
    (void)params;
    if (!text.empty() && !highlighter->failed &&
        highlighter->callback(text.data(), text.size(), type,
                              highlighter->userdata) < 0)
        highlighter->failed = 1;
}

mdwn_highlighter::mdwn_highlighter(srchilite::HighlightStatePtr state)
    : callback(NULL), userdata(NULL), failed(0)
{
    srchilite::FormatterPtr normal(
        new token_formatter(this, MDWN_HIGHLIGHT_NORMAL));

    formatter_manager.reset(new srchilite::FormatterManager(normal));
    add_formatter("comment", MDWN_HIGHLIGHT_COMMENT);
    add_formatter("keyword", MDWN_HIGHLIGHT_KEYWORD);
    add_formatter("type", MDWN_HIGHLIGHT_TYPE);
    add_formatter("usertype", MDWN_HIGHLIGHT_CLASS);
    add_formatter("classname", MDWN_HIGHLIGHT_CLASS);
    add_formatter("string", MDWN_HIGHLIGHT_STRING);
    add_formatter("regexp", MDWN_HIGHLIGHT_REGEXP);
    add_formatter("specialchar", MDWN_HIGHLIGHT_SPECIAL_CHAR);
    add_formatter("number", MDWN_HIGHLIGHT_NUMBER);
    add_formatter("preproc", MDWN_HIGHLIGHT_PREPROCESSOR);
    add_formatter("symbol", MDWN_HIGHLIGHT_SYMBOL);
    add_formatter("cbracket", MDWN_HIGHLIGHT_SYMBOL);
    add_formatter("function", MDWN_HIGHLIGHT_FUNCTION);
    add_formatter("variable", MDWN_HIGHLIGHT_VARIABLE);
    add_formatter("predef_var", MDWN_HIGHLIGHT_BUILTIN);
    add_formatter("predef_func", MDWN_HIGHLIGHT_BUILTIN);

    source_highlighter.reset(new srchilite::SourceHighlighter(state));
    source_highlighter->setFormatterManager(formatter_manager.get());
}

void
mdwn_highlighter::add_formatter(const char *name,
                                enum mdwn_highlight_type type)
{
    formatter_manager->addFormatter(
        name, srchilite::FormatterPtr(new token_formatter(this, type)));
}

void
mdwn_highlighter::reset()
{
    source_highlighter->clearStateStack();
    source_highlighter->setCurrentState(source_highlighter->getMainState());
}

int
mdwn_highlighter::highlight(
    const char *line, size_t length,
    int (*fn)(const char *, size_t, enum mdwn_highlight_type, void *),
    void *data)
{
    callback = fn;
    userdata = data;
    failed = 0;

    try {
        source_highlighter->highlightParagraph(std::string(line, length));
    } catch (...) {
        callback = NULL;
        userdata = NULL;
        return -1;
    }

    callback = NULL;
    userdata = NULL;
    return failed ? -1 : 0;
}

static std::string
mapped_language(srchilite::LangMap &lang_map, const std::string &language)
{
    std::string mapped = lang_map.getMappedFileName(language);

    if (mapped.empty()) {
        std::string lower = language;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char c) { return (char)std::tolower(c); });
        mapped = lang_map.getFileName(lower);
    }
    return mapped;
}

mdwn_highlighter *
mdwn_highlight_cache::get_highlighter(const std::string &language)
{
    auto found = highlighters.find(language);
    std::string language_file;
    std::unique_ptr<mdwn_highlighter> highlighter;

    if (found != highlighters.end())
        return found->second.get();

    language_file = mapped_language(lang_map, language);
    if (language_file.empty())
        return NULL;

    highlighter.reset(new mdwn_highlighter(
        lang_manager.getHighlightState(data_dir, language_file)));
    found = highlighters.emplace(language, std::move(highlighter)).first;
    return found->second.get();
}

struct record_context {
    cached_block *block;
    size_t line;
    size_t offset;
};

static int
record_run(const char *text, size_t length, enum mdwn_highlight_type type,
           void *userdata)
{
    record_context *ctx = static_cast<record_context *>(userdata);
    (void)text;
    ctx->block->runs.push_back({ctx->line, ctx->offset, length, type});
    ctx->offset += length;
    return 0;
}

static int
replay_block(const cached_block &block, const char *text,
             mdwn_highlight_callback callback, void *userdata)
{
    for (const cached_run &run : block.runs) {
        if (callback(run.line, text + run.offset, run.length,
                     run.type, userdata) < 0)
            return -1;
    }
    return 0;
}

extern "C" struct mdwn_highlight_cache *
mdwn_highlight_cache_create(void)
{
    try {
        return new mdwn_highlight_cache(
            srchilite::Settings::retrieveDataDir());
    } catch (...) {
        return NULL;
    }
}

extern "C" void
mdwn_highlight_cache_destroy(struct mdwn_highlight_cache *cache)
{
    delete cache;
}

extern "C" void
mdwn_highlight_cache_begin(struct mdwn_highlight_cache *cache)
{
    if (!cache)
        return;
    for (auto &entry : cache->blocks)
        entry.second.used = false;
}

extern "C" void
mdwn_highlight_cache_end(struct mdwn_highlight_cache *cache, int success)
{
    if (!cache || !success)
        return;
    for (auto entry = cache->blocks.begin(); entry != cache->blocks.end();) {
        if (!entry->second.used)
            entry = cache->blocks.erase(entry);
        else
            ++entry;
    }
}

extern "C" int
mdwn_highlight_cache_highlight(struct mdwn_highlight_cache *cache,
                               const char *language,
                               const char *text, size_t length,
                               mdwn_highlight_callback callback,
                               void *userdata)
{
    if (!cache || !language || !language[0] || !callback)
        return MDWN_HIGHLIGHT_UNAVAILABLE;

    try {
        std::string key(language);
        key.push_back('\0');
        key.append(text, length);
        auto found = cache->blocks.find(key);
        int result = MDWN_HIGHLIGHT_HIT;

        if (found == cache->blocks.end()) {
            cached_block block;
            mdwn_highlighter *highlighter =
                cache->get_highlighter(language);

            if (highlighter) {
                size_t line = 0;
                size_t start = 0;
                record_context record = {&block, 0, 0};

                block.highlighted = true;
                highlighter->reset();
                while (start < length || (start == 0 && length == 0)) {
                    size_t end = start;

                    while (end < length && text[end] != '\n')
                        ++end;
                    record.line = line++;
                    record.offset = start;
                    if (highlighter->highlight(text + start, end - start,
                                               record_run, &record) < 0)
                        return -1;
                    if (record.offset != end)
                        return -1;
                    if (end >= length)
                        break;
                    start = end + 1;
                    if (start == length)
                        break;
                }
            }
            found = cache->blocks.emplace(std::move(key),
                                          std::move(block)).first;
            result = MDWN_HIGHLIGHT_MISS;
        }

        found->second.used = true;
        if (!found->second.highlighted)
            return MDWN_HIGHLIGHT_UNAVAILABLE;
        if (replay_block(found->second, text, callback, userdata) < 0)
            return -1;
        return result;
    } catch (...) {
        return -1;
    }
}
