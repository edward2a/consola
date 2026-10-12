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

#include <ui/key_translator.hpp>

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
    const FontMetrics previous = metrics_;
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
    if (previous.cell_width != metrics_.cell_width ||
        previous.cell_height != metrics_.cell_height || previous.ascent != metrics_.ascent) {
        // A DPI/font change moves every cell; force a full repaint (DIP 2.5).
        full_redraw_requested_ = true;
    }
}

void TerminalWidget::set_style(TerminalStyle style)
{
    style_ = std::move(style);
    apply_font_family();
    update_metrics();
    full_redraw_requested_ = true;
    redraw();
}

void TerminalWidget::resize(int x, int y, int w, int h)
{
    Fl_Widget::resize(x, y, w, h);
    update_metrics();
    full_redraw_requested_ = true;
}

TerminalWidget::Rect TerminalWidget::cell_rect(int column, int row, int width) const
{
    const int span = std::max(1, width);
    const int x0 = x() + static_cast<int>(std::lround(column * metrics_.cell_width));
    const int x1 = x() + static_cast<int>(std::lround((column + span) * metrics_.cell_width));
    const int y0 =
        y() + static_cast<int>(std::lround(row * static_cast<double>(metrics_.cell_height)));
    const int y1 = y0 + metrics_.cell_height;
    return Rect{x0, y0, x1, y1, true};
}

TerminalWidget::Rect TerminalWidget::union_rect(Rect a, Rect b)
{
    if (!a.valid) {
        return b;
    }
    if (!b.valid) {
        return a;
    }
    return Rect{std::min(a.x0, b.x0), std::min(a.y0, b.y0), std::max(a.x1, b.x1),
                std::max(a.y1, b.y1), true};
}

bool TerminalWidget::intersects(Rect a, Rect b)
{
    return a.valid && b.valid && a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1;
}

bool TerminalWidget::inside(Rect a, Rect b)
{
    return a.valid && b.valid && a.x0 >= b.x0 && a.y0 >= b.y0 && a.x1 <= b.x1 && a.y1 <= b.y1;
}

void TerminalWidget::draw()
{
    update_metrics();

    const char damage = static_cast<char>(this->damage());
    full_ = full_redraw_requested_ || last_painted_age_ == 0 || (damage & FL_DAMAGE_ALL) != 0;
    paint_region_ = Rect{};
    content_region_ = Rect{};
    cursor_repaint_ = false;

    // FLTK clips draw() to the damage region; fl_clip_box() returns 0 when the
    // widget is not clipped, in which case the whole widget is drawable.
    Rect clip{x(), y(), x() + w(), y() + h(), true};
    int clip_x = 0;
    int clip_y = 0;
    int clip_w = 0;
    int clip_h = 0;
    if (fl_clip_box(x(), y(), w(), h(), clip_x, clip_y, clip_w, clip_h) != 0) {
        clip = Rect{clip_x, clip_y, clip_x + clip_w, clip_y + clip_h, true};
    }

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

    const int scroll_pos = emulator_.scrollback_line_pos();
    if (scroll_pos != last_scrollback_pos_) {
        full_ = true;
        last_scrollback_pos_ = scroll_pos;
    }

    if (full_) {
        paint_region_ = Rect{x(), y(), x() + w(), y() + h(), true};
    } else {
        if ((damage & FL_DAMAGE_EXPOSE) != 0) {
            paint_region_ = union_rect(paint_region_, clip);
        }
        // Moving or toggling the cursor invalidates both old and new areas.
        if (last_cursor_visible_) {
            paint_region_ =
                union_rect(paint_region_, cell_rect(last_cursor_column_, last_cursor_row_, 1));
        }
        if (cursor_visible_) {
            paint_region_ = union_rect(paint_region_, cell_rect(cursor_column_, cursor_row_, 1));
        }

        // Ages select content damage; expand the paint region to cover it.
        const ContentAge collected = emulator_.draw(&collect_callback, this);
        if (collected == 0) {
            full_ = true;
            paint_region_ = Rect{x(), y(), x() + w(), y() + h(), true};
        } else if (content_region_.valid) {
            paint_region_ = union_rect(paint_region_, content_region_);
        }
    }

    if (paint_region_.valid) {
        fl_color(style_.background.r, style_.background.g, style_.background.b);
        fl_rectf(paint_region_.x0, paint_region_.y0, paint_region_.x1 - paint_region_.x0,
                 paint_region_.y1 - paint_region_.y0);
    }

    const ContentAge painted = emulator_.draw(&paint_callback, this);

    if (full_ || cursor_repaint_) {
        draw_cursor();
    }

    last_cursor_column_ = cursor_column_;
    last_cursor_row_ = cursor_row_;
    last_cursor_visible_ = cursor_visible_;

    if (inside(paint_region_, clip)) {
        last_painted_age_ = painted;
        full_redraw_requested_ = false;
    } else {
        // Some damage lies outside FLTK's clip; keep it pending and force a
        // full repaint instead of advancing the age and losing it (DIP 2.5).
        full_redraw_requested_ = true;
        redraw();
    }
}

void TerminalWidget::paint_callback(void *context, int column, int row, const Cell &cell)
{
    static_cast<TerminalWidget *>(context)->paint_cell(column, row, cell);
}

void TerminalWidget::collect_callback(void *context, int column, int row, const Cell &cell)
{
    static_cast<TerminalWidget *>(context)->collect_cell(column, row, cell);
}

void TerminalWidget::collect_cell(int column, int row, const Cell &cell)
{
    if (cell.age > last_painted_age_) {
        content_region_ = union_rect(content_region_, cell_rect(column, row, cell.width));
    }
}

void TerminalWidget::paint_cell(int column, int row, const Cell &cell)
{
    if (!full_ && !intersects(cell_rect(column, row, cell.width), paint_region_)) {
        return;
    }
    const bool is_cursor = cursor_visible_ && column == cursor_column_ && row == cursor_row_;
    if (is_cursor) {
        cursor_repaint_ = true;
    }
    if (cell.width == 0) {
        return; // trailing half of a wide glyph
    }

    CellAttributes attr = cell.attributes;
    if (is_cursor) {
        // Neutralise libtsm's cursor-cell inversion; the renderer draws it.
        attr.inverse = !attr.inverse;
    }

    Rgb foreground = attr.inverse ? attr.background : attr.foreground;
    Rgb background = attr.inverse ? attr.foreground : attr.background;
    if (attr.dim) {
        foreground = dim(foreground);
    }

    const Rect rect = cell_rect(column, row, cell.width);
    const int px = rect.x0;
    const int py = rect.y0;
    const int pw = std::max(1, rect.x1 - rect.x0);
    const int ph = rect.y1 - rect.y0;

    if (!same(background, style_.background)) {
        fl_color(background.r, background.g, background.b);
        fl_rectf(px, py, pw, ph);
    }

    utf8_scratch_.assign((cell.text.size() * 4) + 1, '\0');
    char *out = utf8_scratch_.data();
    for (const std::uint32_t codepoint : cell.text) {
        out += fl_utf8encode(codepoint, out);
    }
    *out = '\0';

    if (out != utf8_scratch_.data()) {
        fl_font(face_for(attr), style_.font_size);
        fl_push_clip(px, py, pw, ph);
        fl_color(foreground.r, foreground.g, foreground.b);
        fl_draw(utf8_scratch_.data(), px, py + metrics_.ascent);
        if (attr.underline) {
            fl_line(px, py + metrics_.ascent + 1, px + pw - 1, py + metrics_.ascent + 1);
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
    const Rect rect = cell_rect(cursor_column_, cursor_row_, 1);
    const int px = rect.x0;
    const int py = rect.y0;
    const int pw = std::max(1, rect.x1 - rect.x0);
    const int ph = rect.y1 - rect.y0;

    switch (cursor_style_) {
    case CursorStyle::underline_blink:
    case CursorStyle::underline_steady:
        fl_color(cursor_foreground_.r, cursor_foreground_.g, cursor_foreground_.b);
        fl_line(px, py + ph - 1, px + pw - 1, py + ph - 1);
        break;
    case CursorStyle::vbar_blink:
    case CursorStyle::vbar_steady:
        fl_color(cursor_foreground_.r, cursor_foreground_.g, cursor_foreground_.b);
        fl_line(px, py, px, py + ph - 1);
        break;
    case CursorStyle::default_style:
    case CursorStyle::block_blink:
    case CursorStyle::block_steady:
    default:
        fl_color(cursor_foreground_.r, cursor_foreground_.g, cursor_foreground_.b);
        fl_rectf(px, py, pw, ph);
        if (!cursor_text_.empty()) {
            fl_font(face_for(cursor_attributes_), style_.font_size);
            fl_color(cursor_background_.r, cursor_background_.g, cursor_background_.b);
            fl_push_clip(px, py, pw, ph);
            fl_draw(cursor_text_.c_str(), px, py + metrics_.ascent);
            if (cursor_attributes_.underline) {
                fl_line(px, py + metrics_.ascent + 1, px + pw - 1, py + metrics_.ascent + 1);
            }
            fl_pop_clip();
        }
        break;
    }
}

int TerminalWidget::handle(int event)
{
    switch (event) {
    case FL_FOCUS:
    case FL_UNFOCUS:
        return 1;
    case FL_PUSH:
        // Clicking the pane focuses it so it receives key input (watchlist #14).
        Fl::focus(this);
        return 1;
    case FL_KEYDOWN:
        // The terminal consumes every key; libtsm encodes those it knows.
        dispatch_fltk_key(emulator_);
        return 1;
    default:
        return 0;
    }
}

} // namespace consola
