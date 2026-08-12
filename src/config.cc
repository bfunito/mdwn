#include "config.h"

#include "flavor.h"

#include <toml++/toml.hpp>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <vector>

namespace fs = std::filesystem;

static int
set_error(char *err, size_t err_size, const char *format, ...)
{
    va_list args;

    if (err && err_size) {
        va_start(args, format);
        std::vsnprintf(err, err_size, format, args);
        va_end(args);
    }
    return -1;
}

static int
node_error(const fs::path &path, const toml::node &node,
           char *err, size_t err_size, const char *format, ...)
{
    char message[256];
    va_list args;

    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    return set_error(err, err_size, "%s:%zu: %s", path.c_str(),
                     static_cast<size_t>(node.source().begin.line), message);
}

static int
read_number(const fs::path &path, const toml::node &node,
            std::string_view name, float minimum, float maximum, float *value,
            char *err, size_t err_size)
{
    auto number = node.value<double>();

    if (!number || !std::isfinite(*number) || *number < minimum ||
        *number > maximum)
        return node_error(path, node, err, err_size,
                          "%.*s must be between %g and %g",
                          static_cast<int>(name.size()), name.data(),
                          minimum, maximum);
    *value = static_cast<float>(*number);
    return 0;
}

static int
apply_viewer(const fs::path &path, const toml::table &viewer,
             struct mdwn_config *config, char *err, size_t err_size)
{
    for (const auto &[key, node] : viewer) {
        std::string_view name = key.str();
        float *value;
        float minimum;
        float maximum;

        if (name == "initial_zoom") {
            value = &config->viewer.initial_zoom;
            minimum = 0.1f;
            maximum = 10.0f;
        } else if (name == "min_zoom") {
            value = &config->viewer.min_zoom;
            minimum = 0.1f;
            maximum = 10.0f;
        } else if (name == "max_zoom") {
            value = &config->viewer.max_zoom;
            minimum = 0.1f;
            maximum = 10.0f;
        } else if (name == "wheel_zoom_speed") {
            value = &config->viewer.wheel_zoom_speed;
            minimum = 0.01f;
            maximum = 100.0f;
        } else if (name == "scroll_step") {
            value = &config->viewer.scroll_step;
            minimum = 1.0f;
            maximum = 10000.0f;
        } else {
            return node_error(path, node, err, err_size,
                              "unknown viewer setting '%.*s'",
                              static_cast<int>(name.size()), name.data());
        }

        if (read_number(path, node, name, minimum, maximum, value,
                        err, err_size) < 0)
            return -1;
    }
    return 0;
}

static int
apply_config(const fs::path &path, const toml::table &table,
             struct mdwn_config *config, char *err, size_t err_size)
{
    for (const auto &[key, node] : table) {
        std::string_view name = key.str();

        if (name == "flavor") {
            auto value = node.value<std::string>();

            if (!value)
                return node_error(path, node, err, err_size,
                                  "flavor must be a string");
            config->flavor = mdwn_flavor_find(value->c_str());
            if (!config->flavor)
                return node_error(path, node, err, err_size,
                                  "unsupported flavor '%s'", value->c_str());
        } else if (name == "theme") {
            auto value = node.value<std::string>();

            if (!value)
                return node_error(path, node, err, err_size,
                                  "theme must be a string");
            if (*value == "light")
                config->dark_theme = false;
            else if (*value == "dark")
                config->dark_theme = true;
            else
                return node_error(path, node, err, err_size,
                                  "unsupported theme '%s'", value->c_str());
        } else if (name == "viewer") {
            const toml::table *viewer = node.as_table();

            if (!viewer)
                return node_error(path, node, err, err_size,
                                  "viewer must be a table");
            if (apply_viewer(path, *viewer, config, err, err_size) < 0)
                return -1;
        } else {
            return node_error(path, node, err, err_size,
                              "unknown configuration key '%.*s'",
                              static_cast<int>(name.size()), name.data());
        }
    }
    return 0;
}

static int
load_file(const fs::path &path, struct mdwn_config *config,
          char *err, size_t err_size)
{
    try {
        return apply_config(path, toml::parse_file(path.string()), config,
                            err, err_size);
    } catch (const toml::parse_error &error) {
        std::string_view description = error.description();

        return set_error(err, err_size, "%s:%zu:%zu: %.*s", path.c_str(),
                         static_cast<size_t>(error.source().begin.line),
                         static_cast<size_t>(error.source().begin.column),
                         static_cast<int>(description.size()),
                         description.data());
    } catch (const std::exception &error) {
        return set_error(err, err_size, "%s: %s", path.c_str(), error.what());
    }
}

static int
load_if_present(const fs::path &path, struct mdwn_config *config,
                char *err, size_t err_size)
{
    std::error_code error;
    bool exists = fs::exists(path, error);

    if (error)
        return set_error(err, err_size, "%s: %s", path.c_str(),
                         error.message().c_str());
    return exists ? load_file(path, config, err, err_size) : 0;
}

static std::vector<fs::path>
system_config_paths(void)
{
    const char *environment = std::getenv("XDG_CONFIG_DIRS");
    std::string directories = environment && environment[0]
        ? environment : "/etc/xdg";
    std::vector<fs::path> paths;
    size_t begin = 0;

    while (begin <= directories.size()) {
        size_t end = directories.find(':', begin);
        fs::path directory = directories.substr(
            begin, end == std::string::npos ? end : end - begin);

        if (!directory.empty() && directory.is_absolute())
            paths.push_back(directory / "mdwn/config.toml");
        if (end == std::string::npos)
            break;
        begin = end + 1;
    }
    return paths;
}

static fs::path
user_config_path(void)
{
    const char *environment = std::getenv("XDG_CONFIG_HOME");

    if (environment && environment[0]) {
        fs::path path(environment);
        return path.is_absolute() ? path / "mdwn/config.toml" : fs::path();
    }

    const char *home = std::getenv("HOME");
    return home && home[0]
        ? fs::path(home) / ".config/mdwn/config.toml" : fs::path();
}

static fs::path
local_config_path(const char *document_path)
{
    fs::path document = fs::absolute(document_path).lexically_normal();
    fs::path directory = document.parent_path();
    struct stat initial;
    bool check_device;

    check_device = stat(directory.c_str(), &initial) == 0;

    for (;;) {
        fs::path candidate = directory / ".mdwn/config.toml";

        if (fs::exists(candidate))
            return candidate;

        fs::path parent = directory.parent_path();
        if (parent == directory)
            break;
        if (check_device) {
            struct stat current;

            if (stat(parent.c_str(), &current) == 0 &&
                current.st_dev != initial.st_dev)
                break;
        }
        directory = parent;
    }
    return {};
}

extern "C" void
mdwn_config_init(struct mdwn_config *config)
{
    config->flavor = mdwn_flavor_default();
    config->viewer.initial_zoom = 1.0f;
    config->viewer.min_zoom = 1.0f;
    config->viewer.max_zoom = 5.0f;
    config->viewer.wheel_zoom_speed = 1.0f;
    config->viewer.scroll_step = 48.0f;
    config->dark_theme = false;
}

extern "C" int
mdwn_config_load(struct mdwn_config *config, const char *document_path,
                 char *err, size_t err_size)
{
    try {
        std::vector<fs::path> system = system_config_paths();

        for (auto path = system.rbegin(); path != system.rend(); ++path) {
            if (load_if_present(*path, config, err, err_size) < 0)
                return -1;
        }

        fs::path user = user_config_path();
        if (!user.empty() &&
            load_if_present(user, config, err, err_size) < 0)
            return -1;

        fs::path local = local_config_path(document_path);
        if (!local.empty() && load_file(local, config, err, err_size) < 0)
            return -1;

        if (config->viewer.min_zoom > config->viewer.initial_zoom ||
            config->viewer.initial_zoom > config->viewer.max_zoom)
            return set_error(err, err_size,
                "configuration requires min_zoom <= initial_zoom <= max_zoom");
        return 0;
    } catch (const std::exception &error) {
        return set_error(err, err_size, "could not load configuration: %s",
                         error.what());
    } catch (...) {
        return set_error(err, err_size, "could not load configuration");
    }
}
