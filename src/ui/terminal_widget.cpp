// SPDX-License-Identifier: MIT

#include <ui/terminal_widget.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include <FL/Fl.H>
#include <FL/fl_draw.H>
#include <FL/fl_utf8.h>

namespace consola {
namespace {

int clamp_int(int value, int low, int high)
{
    return std::max(low, std::min(value, high));
}

int face_for(const CellAttributes &attributes)
{
    if (attributes.bold && attributes.italic) {
        return FL_HELVETICA_BOLD_ITALIC;
    }
    if (attributes.bold) {
        return FL_HELVETICA_BOLD;
    }
    if (attributes.italic) {
        return FL_HELVETICA_ITALIC;
    }
    return FL_HELVETICA;
}

Rgb dim(Rgb color)
{
    return Rgb{static_cast<std::uint8_t>(color.r * 2 / 3),
               static_cast<std::uint8_t>(color.g * 2 / 3),
               static_cast<std::uint8_t>(color.b * 2 / 3)};
}

bool same(Rgb lhs, Rgb rhs)
{
    return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b;
}

} // namespace

TerminalWidget::TerminalWidget(int x, int y, int w, int h, TerminalEmulator &emulator,
                               TerminalStyle style)
    : Fl_Widget(x, y, w, h, nullptr), emulator_(emulator), style_(std::move(style))
{
    box(FL_NO_BOX);
    apply_font_family();
    update_metrics();
}

void TerminalWidget::apply_font_family() const
{
    // Map the configured family to FLTK's base face and its styled variants so
    // bold/italic stay in the same monospace family (fontconfig resolves the
    // closest available style; see watchlist #13).
    Fl::set_font(FL_HELVETICA, style_.font_family.c_str());
    Fl::set_font(FL_HELVETICA_BOLD, (style_.font_family + " bold").c_str());
    Fl::set_font(FL_HELVETICA_ITALIC, (style_.font_family + " italic").c_str());
    Fl::set_font(FL_HELVETICA_BOLD_ITALIC, (style_.font_family + " bold italic").c_str());
}

void TerminalWidget::update_metrics()
{
    fl_font(FL_HELVETICA, style_.font_size);
    metrics_.cell_width = fl_width("M");
    metrics_.cell_height = fl_height();
    metrics_.ascent = fl_height() - fl_descent();
    if (metrics_.cell_width <= 0.0) {
        metrics_.cell_width = 1.0;
    }
    if (metrics_.cell_height <= 0) {
        metrics_.cell_height = 1;
    }
}

void TerminalWidget::draw()
{
    update_metrics();

    fl_color(style_.background.r, style_.background.g, style_.background.b);
    fl_rectf(x(), y(), w(), h());

    const CursorState cursor = emulator_.cursor();
    cursor_style_ = cursor.style;
    const int viewport_row =
        emulator_.scrollback_line_count() - emulator_.scrollback_line_pos() + cursor.y;
    cursor_visible_ = !cursor.hidden && viewport_row >= 0 && viewport_row < emulator_.rows();
    cursor_row_ = clamp_int(viewport_row, 0, emulator_.rows() - 1);
    cursor_column_ = clamp_int(cursor.x, 0, emulator_.columns() - 1);
    cursor_text_.clear();
    cursor_foreground_ = style_.foreground;
    cursor_background_ = style_.background;

    emulator_.draw(&TerminalWidget::cell_callback, this);
    draw_cursor();
}

void TerminalWidget::cell_callback(void *context, int column, int row, const Cell &cell)
{
    static_cast<TerminalWidget *>(context)->draw_cell(column, row, cell);
}

void TerminalWidget::draw_cell(int column, int row, const Cell &cell)
{
    if (cell.width == 0) {
        return; // trailing half of a wide glyph
    }

    CellAttributes attr = cell.attributes;
    const bool is_cursor = cursor_visible_ && column == cursor_column_ && row == cursor_row_;
    if (is_cursor) {
        // Neutralise libtsm's cursor-cell inversion; the renderer draws it.
        attr.inverse = !attr.inverse;
    }

    Rgb foreground = attr.inverse ? attr.background : attr.foreground;
    Rgb background = attr.inverse ? attr.foreground : attr.background;
    if (attr.dim) {
        foreground = dim(foreground);
    }

    const int x0 = x() + static_cast<int>(std::lround(column * metrics_.cell_width));
    const int x1 = x() + static_cast<int>(std::lround((column + cell.width) * metrics_.cell_width));
    const int py =
        y() + static_cast<int>(std::lround(row * static_cast<double>(metrics_.cell_height)));
    const int pw = std::max(1, x1 - x0);
    const int ph = metrics_.cell_height;

    if (!same(background, style_.background)) {
        fl_color(background.r, background.g, background.b);
        fl_rectf(x0, py, pw, ph);
    }

    utf8_scratch_.assign((cell.text.size() * 4) + 1, '\0');
    char *out = utf8_scratch_.data();
    for (const std::uint32_t codepoint : cell.text) {
        out += fl_utf8encode(codepoint, out);
    }
    *out = '\0';

    if (out != utf8_scratch_.data()) {
        fl_font(face_for(attr), style_.font_size);
        fl_push_clip(x0, py, pw, ph);
        fl_color(foreground.r, foreground.g, foreground.b);
        fl_draw(utf8_scratch_.data(), x0, py + metrics_.ascent);
        if (attr.underline) {
            fl_line(x0, py + metrics_.ascent + 1, x0 + pw - 1, py + metrics_.ascent + 1);
        }
        fl_pop_clip();
    }

    if (is_cursor) {
        cursor_text_ = utf8_scratch_.data();
        cursor_attributes_ = attr;
        cursor_foreground_ = foreground;
        cursor_background_ = background;
    }
}

void TerminalWidget::draw_cursor()
{
    if (!cursor_visible_) {
        return;
    }
    const int x0 = x() + static_cast<int>(std::lround(cursor_column_ * metrics_.cell_width));
    const int x1 = x() + static_cast<int>(std::lround((cursor_column_ + 1) * metrics_.cell_width));
    const int py = y() + static_cast<int>(
                             std::lround(cursor_row_ * static_cast<double>(metrics_.cell_height)));
    const int pw = std::max(1, x1 - x0);
    const int ph = metrics_.cell_height;

    switch (cursor_style_) {
    case CursorStyle::underline_blink:
    case CursorStyle::underline_steady:
        fl_color(cursor_foreground_.r, cursor_foreground_.g, cursor_foreground_.b);
        fl_line(x0, py + ph - 1, x0 + pw - 1, py + ph - 1);
        break;
    case CursorStyle::vbar_blink:
    case CursorStyle::vbar_steady:
        fl_color(cursor_foreground_.r, cursor_foreground_.g, cursor_foreground_.b);
        fl_line(x0, py, x0, py + ph - 1);
        break;
    case CursorStyle::default_style:
    case CursorStyle::block_blink:
    case CursorStyle::block_steady:
    default:
        fl_color(cursor_foreground_.r, cursor_foreground_.g, cursor_foreground_.b);
        fl_rectf(x0, py, pw, ph);
        if (!cursor_text_.empty()) {
            fl_font(face_for(cursor_attributes_), style_.font_size);
            fl_color(cursor_background_.r, cursor_background_.g, cursor_background_.b);
            fl_push_clip(x0, py, pw, ph);
            fl_draw(cursor_text_.c_str(), x0, py + metrics_.ascent);
            if (cursor_attributes_.underline) {
                fl_line(x0, py + metrics_.ascent + 1, x0 + pw - 1, py + metrics_.ascent + 1);
            }
            fl_pop_clip();
        }
        break;
    }
}

int TerminalWidget::handle(int event)
{
    static_cast<void>(event);
    return 0;
}

} // namespace consola
