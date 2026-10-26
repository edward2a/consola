// SPDX-License-Identifier: MIT

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <app/application.hpp>
#include <app/log.hpp>

namespace {

bool is_smoke_test_flag(const char *arg)
{
    return std::strcmp(arg, "--smoke-test") == 0;
}

bool is_config_flag(std::string_view arg)
{
    return arg == "--config";
}

} // namespace

int main(int argc, char **argv)
{
    std::signal(SIGPIPE, SIG_IGN);
    try {
        bool smoke_test = false;
        std::optional<std::filesystem::path> config_path;
        std::optional<std::string> layout_name;
        std::vector<char *> fltk_args;
        fltk_args.push_back(argv[0]);
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg = argv[i];
            if (is_smoke_test_flag(argv[i])) {
                smoke_test = true;
                continue;
            }
            if (is_config_flag(arg)) {
                if (i + 1 >= argc) {
                    consola::log(consola::LogLevel::Error, "--config requires a path");
                    return EXIT_FAILURE;
                }
                config_path = std::filesystem::path(argv[++i]);
                continue;
            }
            if (arg.starts_with("--config=")) {
                config_path = std::filesystem::path(std::string(arg.substr(9)));
                continue;
            }
            if (arg == "--layout") {
                if (i + 1 >= argc) {
                    consola::log(consola::LogLevel::Error, "--layout requires a name");
                    return EXIT_FAILURE;
                }
                layout_name = std::string(argv[++i]);
                continue;
            }
            if (arg.starts_with("--layout=")) {
                layout_name = std::string(arg.substr(9));
                continue;
            }
            fltk_args.push_back(argv[i]);
        }
        fltk_args.push_back(nullptr);

        return consola::run_application(static_cast<int>(fltk_args.size()) - 1, fltk_args.data(),
                                        smoke_test, config_path, layout_name);
    } catch (const std::exception &error) {
        consola::log(consola::LogLevel::Error, error.what());
        return EXIT_FAILURE;
    } catch (...) {
        consola::log(consola::LogLevel::Error, "fatal: unknown error");
        return EXIT_FAILURE;
    }
}
