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
    srchilite::RegexRuleFactory rule_factory;
    srchilite::LangDefManager lang_manager;
    std::unique_ptr<srchilite::FormatterManager> formatter_manager;
    std::unique_ptr<srchilite::SourceHighlighter> source_highlighter;
    mdwn_highlight_callback callback;
    void *userdata;
    int failed;

    mdwn_highlighter(const std::string &data_dir,
                     const std::string &language_file);
    void add_formatter(const char *name, enum mdwn_highlight_type type);
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

mdwn_highlighter::mdwn_highlighter(const std::string &data_dir,
                                   const std::string &language_file)
    : lang_manager(&rule_factory), callback(NULL), userdata(NULL), failed(0)
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

    source_highlighter.reset(new srchilite::SourceHighlighter(
        lang_manager.getHighlightState(data_dir, language_file)));
    source_highlighter->setFormatterManager(formatter_manager.get());
}

void
mdwn_highlighter::add_formatter(const char *name,
                                enum mdwn_highlight_type type)
{
    formatter_manager->addFormatter(
        name, srchilite::FormatterPtr(new token_formatter(this, type)));
}

static std::string
mapped_language(const std::string &data_dir, const std::string &language)
{
    srchilite::LangMap lang_map(data_dir, "lang.map");
    std::string mapped = lang_map.getMappedFileName(language);

    if (mapped.empty()) {
        std::string lower = language;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char c) { return (char)std::tolower(c); });
        mapped = lang_map.getFileName(lower);
    }
    return mapped;
}

extern "C" struct mdwn_highlighter *
mdwn_highlighter_create(const char *language)
{
    if (!language || !language[0])
        return NULL;

    try {
        std::string data_dir = srchilite::Settings::retrieveDataDir();
        std::string language_file = mapped_language(data_dir, language);

        if (language_file.empty())
            return NULL;
        return new mdwn_highlighter(data_dir, language_file);
    } catch (...) {
        return NULL;
    }
}

extern "C" void
mdwn_highlighter_destroy(struct mdwn_highlighter *highlighter)
{
    delete highlighter;
}

extern "C" int
mdwn_highlighter_highlight(struct mdwn_highlighter *highlighter,
                           const char *line, size_t length,
                           mdwn_highlight_callback callback,
                           void *userdata)
{
    if (!highlighter || !callback)
        return -1;

    highlighter->callback = callback;
    highlighter->userdata = userdata;
    highlighter->failed = 0;

    try {
        highlighter->source_highlighter->highlightParagraph(
            std::string(line, length));
    } catch (...) {
        highlighter->callback = NULL;
        highlighter->userdata = NULL;
        return -1;
    }

    highlighter->callback = NULL;
    highlighter->userdata = NULL;
    return highlighter->failed ? -1 : 0;
}
