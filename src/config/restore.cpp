// SPDX-License-Identifier: MIT

#include <config/restore.hpp>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

#include <unistd.h>

#include <config/apply.hpp>

namespace consola {
namespace {

bool usable_file(const std::string &path)
{
    std::error_code error;
    if (!std::filesystem::exists(path, error) || std::filesystem::is_directory(path, error)) {
        return false;
    }
    return ::access(path.c_str(), X_OK) == 0;
}

} // namespace

bool executable_available(std::string_view executable)
{
    if (executable.empty()) {
        return false;
    }
    const std::string path(executable);
    if (path.find('/') != std::string::npos) {
        return usable_file(path);
    }
    const char *environment = std::getenv("PATH");
    if (environment == nullptr) {
        return false;
    }
    std::string_view paths(environment);
    while (!paths.empty()) {
        const std::size_t separator = paths.find(':');
        const std::string_view entry = paths.substr(0, separator);
        if (!entry.empty() && usable_file(std::string(entry) + "/" + path)) {
            return true;
        }
        if (separator == std::string_view::npos) {
            break;
        }
        paths.remove_prefix(separator + 1);
    }
    return false;
}

RestorePaneOptions resolve_restore_pane(const ApplicationConfig &config,
                                        const LayoutPaneConfig &pane)
{
    RestorePaneOptions result;

    std::string profile = pane.profile;
    if (profile.empty()) {
        profile = config.default_profile;
    }
    if (!config.profiles.contains(profile)) {
        result.warnings.push_back("layout pane " + pane.id + ": unknown profile '" + profile +
                                  "'; using the default profile");
        profile = config.default_profile;
    }
    result.profile = profile;
    result.session = resolve_session_options(config, profile);
    result.style = resolve_terminal_style(config, profile);

    if (!pane.working_directory.empty()) {
        std::error_code error;
        if (std::filesystem::is_directory(pane.working_directory, error)) {
            result.session.working_directory = pane.working_directory;
        } else {
            result.warnings.push_back("layout pane " + pane.id + ": directory '" +
                                      pane.working_directory +
                                      "' is unavailable; using the default directory");
        }
    }

    if (!executable_available(result.session.executable)) {
        const SessionOptions fallback = resolve_session_options(config, config.default_profile);
        if (fallback.executable != result.session.executable) {
            result.warnings.push_back("layout pane " + pane.id + ": shell '" +
                                      result.session.executable + "' cannot launch; using '" +
                                      fallback.executable + "'");
            result.session.executable = fallback.executable;
        }
        if (!executable_available(result.session.executable)) {
            result.warnings.push_back("layout pane " + pane.id +
                                      ": the default shell is unavailable; the pane will be "
                                      "retained after the failed launch");
            result.retain_on_exit = true;
        }
    }
    return result;
}

} // namespace consola
