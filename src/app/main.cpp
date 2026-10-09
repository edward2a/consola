// SPDX-License-Identifier: MIT

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <vector>

#include <app/application.hpp>
#include <app/log.hpp>

namespace {

bool is_smoke_test_flag(const char *arg)
{
    return std::strcmp(arg, "--smoke-test") == 0;
}

} // namespace

int main(int argc, char **argv)
{
    std::signal(SIGPIPE, SIG_IGN);
    try {
        bool smoke_test = false;
        std::vector<char *> fltk_args;
        fltk_args.push_back(argv[0]);
        for (int i = 1; i < argc; ++i) {
            if (is_smoke_test_flag(argv[i])) {
                smoke_test = true;
                continue;
            }
            fltk_args.push_back(argv[i]);
        }
        fltk_args.push_back(nullptr);

        return consola::run_application(static_cast<int>(fltk_args.size()) - 1, fltk_args.data(),
                                        smoke_test);
    } catch (const std::exception &error) {
        consola::log(consola::LogLevel::Error, error.what());
        return EXIT_FAILURE;
    } catch (...) {
        consola::log(consola::LogLevel::Error, "fatal: unknown error");
        return EXIT_FAILURE;
    }
}
