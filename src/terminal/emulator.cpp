// SPDX-License-Identifier: MIT

#include <app/emulator.hpp>

#include <cstdarg>
#include <cstdio>
#include <stdexcept>

#include <libtsm.h>

#include <app/log.hpp>

namespace consola {

struct TerminalEmulatorImpl {
    tsm_screen *screen = nullptr;
    tsm_vte *vte = nullptr;
    TerminalEmulator::OutputCallback output_callback = nullptr;
    void *output_context = nullptr;
};

namespace {

void log_callback([[maybe_unused]] void *data, [[maybe_unused]] const char *file,
                  [[maybe_unused]] int line, [[maybe_unused]] const char *func,
                  [[maybe_unused]] const char *subs, unsigned int severity, const char *format,
                  va_list args)
{
    char buffer[512];
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    log(log_level_from_libtsm_severity(static_cast<int>(severity)), buffer);
}

void vte_write_callback([[maybe_unused]] tsm_vte *vte, const char *u8, std::size_t len, void *data)
{
    auto *impl = static_cast<TerminalEmulatorImpl *>(data);
    if (impl->output_callback != nullptr) {
        impl->output_callback(
            impl->output_context,
            std::span<const std::byte>(reinterpret_cast<const std::byte *>(u8), len));
    }
}

} // namespace

TerminalEmulator::TerminalEmulator() : impl_(std::make_unique<TerminalEmulatorImpl>())
{
    if (tsm_screen_new(&impl_->screen, &log_callback, nullptr) < 0) {
        throw std::runtime_error("failed to create libtsm screen");
    }
    if (tsm_vte_new(&impl_->vte, impl_->screen, &vte_write_callback, impl_.get(), &log_callback,
                    nullptr) < 0) {
        tsm_screen_unref(impl_->screen);
        impl_->screen = nullptr;
        throw std::runtime_error("failed to create libtsm vte");
    }
}

TerminalEmulator::~TerminalEmulator()
{
    if (impl_ == nullptr) {
        return;
    }
    if (impl_->vte != nullptr) {
        tsm_vte_unref(impl_->vte);
    }
    if (impl_->screen != nullptr) {
        tsm_screen_unref(impl_->screen);
    }
}

void TerminalEmulator::feed(std::span<const std::byte> data)
{
    if (data.empty()) {
        return;
    }
    tsm_vte_input(impl_->vte, reinterpret_cast<const char *>(data.data()), data.size());
}

void TerminalEmulator::resize(int cols, int rows)
{
    if (cols <= 0 || rows <= 0) {
        return;
    }
    tsm_screen_resize(impl_->screen, static_cast<unsigned int>(cols),
                      static_cast<unsigned int>(rows));
}

void TerminalEmulator::set_output_callback(OutputCallback callback, void *context)
{
    impl_->output_callback = callback;
    impl_->output_context = context;
}

int TerminalEmulator::columns() const
{
    return static_cast<int>(tsm_screen_get_width(impl_->screen));
}

int TerminalEmulator::rows() const
{
    return static_cast<int>(tsm_screen_get_height(impl_->screen));
}

int TerminalEmulator::cursor_x_raw() const
{
    return static_cast<int>(tsm_screen_get_cursor_x(impl_->screen));
}

int TerminalEmulator::cursor_y_raw() const
{
    return static_cast<int>(tsm_screen_get_cursor_y(impl_->screen));
}

} // namespace consola
