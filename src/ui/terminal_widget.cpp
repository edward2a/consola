// SPDX-License-Identifier: MIT

#include <ui/terminal_widget.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <FL/Fl.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>
#include <FL/fl_utf8.h>

#include <ui/key_translator.hpp>

#include <app/log.hpp>
#include <ui/font_name.hpp>

namespace consola {
namespace {

// Dedicated FLTK font numbers for the terminal faces. FLTK caches font
// descriptors per font number and only invalidates them when the face name
// changes, so reusing a built-in face (e.g. FL_HELVETICA) can pick up a
// descriptor created before the terminal set its family. Private numbers avoid
// that entirely (watchlist #13).
constexpr Fl_Font kTerminalNormalFace = FL_FREE_FONT;
constexpr Fl_Font kTerminalBoldFace = FL_FREE_FONT + 1;
constexpr Fl_Font kTerminalItalicFace = FL_FREE_FONT + 2;
constexpr Fl_Font kTerminalBoldItalicFace = FL_FREE_FONT + 3;

int clamp_int(int value, int low, int high)
{
    return std::max(low, std::min(value, high));
}

int face_for(const CellAttributes &attributes)
{
    if (attributes.bold && attributes.italic) {
        return kTerminalBoldItalicFace;
    }
    if (attributes.bold) {
        return kTerminalBoldFace;
    }
    if (attributes.italic) {
        return kTerminalItalicFace;
    }
    return kTerminalNormalFace;
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

MouseButton fltk_mouse_button(int button)
{
    switch (button) {
    case FL_MIDDLE_MOUSE:
        return MouseButton::middle;
    case FL_RIGHT_MOUSE:
        return MouseButton::right;
    case FL_LEFT_MOUSE:
    default:
        return MouseButton::left;
    }
}

unsigned fltk_mouse_modifiers()
{
    const int state = Fl::event_state();
    unsigned modifiers = 0;
    if ((state & FL_SHIFT) != 0) {
        modifiers |= MouseModifierShift;
    }
    if ((state & FL_CTRL) != 0) {
        modifiers |= MouseModifierCtrl;
    }
    if ((state & FL_ALT) != 0) {
        modifiers |= MouseModifierAlt;
    }
    if ((state & FL_META) != 0) {
        modifiers |= MouseModifierMeta;
    }
    return modifiers;
}

} // namespace

namespace {

// FLTK's face table is process-global and Fl::set_font() keeps the const char*
// it is handed. Names are interned in a node-based map so each unique name is
// stored once and its address stays valid for the process lifetime (see
// watchlist #13).
const std::string &intern_font_name(const std::string &name)
{
    static std::unordered_map<std::string, std::string> store;
    const auto it = store.find(name);
    if (it != store.end()) {
        return it->second;
    }
    return store.emplace(name, name).first->second;
}

// Families tried, in order, when the configured family does not resolve to a
// monospace face (a missing font would otherwise fall back to a proportional
// one and space the cell grid out).
constexpr std::array<const char *, 6> kMonospaceFallbacks = {
    "Inconsolata", "DejaVu Sans Mono", "Liberation Mono", "Noto Sans Mono", "FreeMono", "Courier"};

// Install the four FLTK faces for a family with explicit style modifiers.
void set_family_faces(const std::string &family)
{
    Fl::set_font(kTerminalNormalFace,
                 intern_font_name(fltk_raw_font_name(family, false, false)).c_str());
    Fl::set_font(kTerminalBoldFace,
                 intern_font_name(fltk_raw_font_name(family, true, false)).c_str());
    Fl::set_font(kTerminalItalicFace,
                 intern_font_name(fltk_raw_font_name(family, false, true)).c_str());
    Fl::set_font(kTerminalBoldItalicFace,
                 intern_font_name(fltk_raw_font_name(family, true, true)).c_str());
}

double face_width(int face, int size, const char *text)
{
    fl_font(face, size);
    return fl_width(text);
}

// A usable terminal face advances 'M', 'i', 'W' and ' ' equally and is not a
// full-width (CJK) fallback, whose ~1em advance would double-space Latin text.
bool suitable_metrics(int size)
{
    const double m = face_width(kTerminalNormalFace, size, "M");
    if (m <= 0.0 || m > 0.85 * static_cast<double>(size)) {
        return false;
    }
    return std::abs(m - face_width(kTerminalNormalFace, size, "i")) < 0.5 &&
           std::abs(m - face_width(kTerminalNormalFace, size, "W")) < 0.5 &&
           std::abs(m - face_width(kTerminalNormalFace, size, " ")) < 0.5;
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

TerminalWidget::~TerminalWidget()
{
    if (bell_flash_active_) {
        Fl::remove_timeout(&TerminalWidget::bell_timeout, this);
    }
}

void TerminalWidget::ring_bell()
{
    const bool audible = style_.bell == BellMode::audible || style_.bell == BellMode::both;
    const bool visual = style_.bell == BellMode::visual || style_.bell == BellMode::both;
    if (audible) {
        // Coalesce bursts so a noisy program cannot flood the audio device.
        const auto now = std::chrono::steady_clock::now();
        if (now - last_audible_bell_ >= std::chrono::milliseconds(100)) {
            last_audible_bell_ = now;
            fl_beep(FL_BEEP_DEFAULT);
        }
    }
    if (visual) {
        bell_flash_active_ = true;
        Fl::remove_timeout(&TerminalWidget::bell_timeout, this);
        Fl::add_timeout(0.12, &TerminalWidget::bell_timeout, this);
        redraw();
    }
}

void TerminalWidget::bell_timeout(void *context)
{
    auto *self = static_cast<TerminalWidget *>(context);
    self->bell_flash_active_ = false;
    self->redraw();
}

void TerminalWidget::apply_font_family() const
{
    // FLTK encodes the style in the leading character of the name, so the
    // modifier must be explicit (see watchlist #13); a bare family starting
    // with 'I' would be read as italic.
    std::string family = style_.font_family;
    set_family_faces(family);

    // A family that does not resolve to a usable monospace face (for example
    // when the bundled font is unavailable) falls back to a proportional or
    // full-width font, which spaces the fixed-width cell grid out. Substitute a
    // known Latin monospace family.
    if (!suitable_metrics(style_.font_size)) {
        for (const char *candidate : kMonospaceFallbacks) {
            set_family_faces(candidate);
            if (suitable_metrics(style_.font_size)) {
                family = candidate;
                log(LogLevel::Warning, "configured terminal font is not usable; using a fallback");
                break;
            }
        }
    }

    // A style the family does not provide (e.g. italic in a regular-only font)
    // resolves to an unrelated fallback whose metrics break the cell grid.
    // Reuse the base face for that style so styled text stays aligned.
    const double base = face_width(kTerminalNormalFace, style_.font_size, "M");
    const double bold = face_width(kTerminalBoldFace, style_.font_size, "M");
    if (std::abs(face_width(kTerminalItalicFace, style_.font_size, "M") - base) > 0.5) {
        Fl::set_font(kTerminalItalicFace,
                     intern_font_name(fltk_raw_font_name(family, false, false)).c_str());
    }
    if (std::abs(face_width(kTerminalBoldItalicFace, style_.font_size, "M") - bold) > 0.5) {
        Fl::set_font(kTerminalBoldItalicFace,
                     intern_font_name(fltk_raw_font_name(family, true, false)).c_str());
    }

    std::array<char, 192> diagnostic{};
    std::snprintf(diagnostic.data(), diagnostic.size(),
                  "terminal font: family='%s' face='%s' advance=%.1f size=%d", family.c_str(),
                  Fl::get_font(kTerminalNormalFace) != nullptr ? Fl::get_font(kTerminalNormalFace)
                                                               : "(null)",
                  face_width(kTerminalNormalFace, style_.font_size, "M"), style_.font_size);
    log(LogLevel::Info, diagnostic.data());
}

int TerminalWidget::scrollbar_width() const
{
    if (!style_.scrollbar) {
        return 0;
    }
    return std::max(4, static_cast<int>(metrics_.cell_width / 4));
}

void TerminalWidget::update_metrics()
{
    const FontMetrics previous = metrics_;
    fl_font(kTerminalNormalFace, style_.font_size);
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

void TerminalWidget::copy_selection()
{
    const std::string text = emulator_.selection_copy();
    if (text.empty()) {
        return;
    }
    Fl::copy(text.data(), static_cast<int>(text.size()), 1); // clipboard
    Fl::copy(text.data(), static_cast<int>(text.size()), 0); // primary selection
}

void TerminalWidget::paste_clipboard()
{
    // Requests the clipboard; FLTK delivers it as an FL_PASTE event.
    Fl::paste(*this, 1);
}

void TerminalWidget::set_paste_text_callback(PasteTextCallback paste_text_callback, void *context)
{
    paste_text_callback_ = paste_text_callback;
    paste_text_context_ = context;
}

void TerminalWidget::set_resize_callback(ResizeCallback resize_callback, void *context)
{
    resize_callback_ = resize_callback;
    resize_context_ = context;
}

void TerminalWidget::set_focus_callback(FocusCallback focus_callback, void *context)
{
    focus_callback_ = focus_callback;
    focus_context_ = context;
}

void TerminalWidget::set_key_callback(KeyCallback key_callback, void *context)
{
    key_callback_ = key_callback;
    key_context_ = context;
}

void TerminalWidget::set_key_input_callback(KeyInputCallback key_input_callback, void *context)
{
    key_input_callback_ = key_input_callback;
    key_input_context_ = context;
}

void TerminalWidget::resize(int x, int y, int w, int h)
{
    Fl_Widget::resize(x, y, w, h);
    update_metrics();
    full_redraw_requested_ = true;
    if (resize_callback_ != nullptr) {
        // Reserve the scrollbar strip before converting pixels to cells (DIP
        // 2.11).
        const int content_width = std::max(0, w - scrollbar_width());
        const int columns =
            std::max(1, static_cast<int>(static_cast<double>(content_width) / metrics_.cell_width));
        const int rows = std::max(1, h / metrics_.cell_height);
        resize_callback_(resize_context_, columns, rows);
    }
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
    // libtsm clamps the cursor to the last row when drawing, so clamp before
    // computing the viewport row (DIP 2.2, 2.4).
    const int cursor_y = clamp_int(cursor.y, 0, emulator_.rows() - 1);
    const int viewport_row =
        emulator_.scrollback_line_count() - emulator_.scrollback_line_pos() + cursor_y;
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
    }

    // One traversal reads every visible cell for damage and URL detection, so
    // highlighting adds no extra libtsm pass (DIP 6.3, watchlist #21).
    begin_url_scan();
    const ContentAge collected = emulator_.draw(&collect_callback, this);
    finish_url_scan();

    if (!full_) {
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
    draw_scrollbar();

    // Visual bell (DIP 6.2): a brief full-pane flash painted over the content.
    if (bell_flash_active_) {
        fl_color(FL_WHITE);
        fl_rectf(x(), y(), w(), h());
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
    scan_url_cell(column, row, cell);
    if (cell.age > last_painted_age_) {
        content_region_ = union_rect(content_region_, cell_rect(column, row, cell.width));
    }
}

void TerminalWidget::begin_url_scan()
{
    url_spans_.clear();
    url_scan_active_ = true;
    url_row_ = -1;
    url_text_.clear();
    url_columns_.clear();
    url_widths_.clear();
    // Reserve the row capacity outside the libtsm callback so the per-cell
    // pushes below normally cannot allocate (DIP 2.2 callback contract).
    const std::size_t columns = static_cast<std::size_t>(std::max(0, emulator_.columns()));
    url_text_.reserve(columns + 1);
    url_columns_.reserve(columns);
    url_widths_.reserve(columns);
}

void TerminalWidget::scan_url_cell(int column, int row, const Cell &cell)
{
    if (!url_scan_active_) {
        return;
    }
    // This runs inside libtsm's draw callback, which must not throw. Reserve
    // capacity above; if a growth still fails, disable detection for this draw
    // rather than let the exception escape through C.
    try {
        if (row != url_row_) {
            flush_url_row();
            url_row_ = row;
            url_text_.clear();
            url_columns_.clear();
            url_widths_.clear();
        }
        if (cell.width == 0) {
            return; // trailing half of a wide glyph
        }
        const bool ascii = cell.text.size() == 1 && cell.text[0] < 0x80;
        url_text_.push_back(ascii ? static_cast<char>(cell.text[0]) : ' ');
        url_columns_.push_back(column);
        url_widths_.push_back(cell.width);
    } catch (...) {
        url_scan_active_ = false;
        url_spans_.clear();
        url_text_.clear();
        url_columns_.clear();
        url_widths_.clear();
    }
}

void TerminalWidget::flush_url_row()
{
    if (url_row_ < 0 || url_text_.empty()) {
        return;
    }
    for (const UrlMatch &match : find_urls(url_text_)) {
        if (match.end == 0 || match.end > url_columns_.size()) {
            continue;
        }
        const int start = url_columns_[match.begin];
        const int last = url_columns_[match.end - 1];
        const int end = last + std::max(1, url_widths_[match.end - 1]);
        url_spans_.push_back(UrlSpan{
            .row = url_row_,
            .start_column = start,
            .end_column = end,
            .text = url_text_.substr(match.begin, match.end - match.begin),
        });
    }
}

void TerminalWidget::finish_url_scan()
{
    if (url_scan_active_) {
        try {
            flush_url_row();
        } catch (...) {
            url_spans_.clear();
        }
    }
    url_scan_active_ = false;
}

bool TerminalWidget::url_at(int column, int row) const noexcept
{
    return std::ranges::any_of(url_spans_, [column, row](const UrlSpan &span) {
        return span.row == row && column >= span.start_column && column < span.end_column;
    });
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
    // URL highlighting is presentation-only (DIP 6.3): underline and tint the
    // cell without touching the terminal model.
    if (!is_cursor && url_at(column, row)) {
        attr.underline = true;
        attr.foreground = style_.ansi[12];
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

void TerminalWidget::draw_scrollbar()
{
    const int lines = emulator_.scrollback_line_count();
    if (!style_.scrollbar || lines <= 0) {
        return;
    }
    const int bar_width = scrollbar_width();
    const int bar_x = x() + w() - bar_width;

    fl_color(static_cast<unsigned char>(style_.foreground.r / 2),
             static_cast<unsigned char>(style_.foreground.g / 2),
             static_cast<unsigned char>(style_.foreground.b / 2));
    fl_rectf(bar_x, y(), bar_width, h());
    const int position = emulator_.scrollback_line_pos();
    const int total = lines + emulator_.rows();
    const int thumb_height = std::max(20, (h() * emulator_.rows()) / std::max(1, total));
    // libtsm: pos 0 is the top, pos == lines is live/bottom.
    const double fraction = static_cast<double>(position) / static_cast<double>(lines);
    const int thumb_y = y() + static_cast<int>((h() - thumb_height) * fraction);

    fl_color(style_.foreground.r, style_.foreground.g, style_.foreground.b);
    fl_rectf(bar_x, thumb_y, bar_width, thumb_height);
}

int TerminalWidget::handle(int event)
{
    switch (event) {
    case FL_FOCUS:
        if (focus_callback_ != nullptr) {
            focus_callback_(focus_context_);
        }
        return 1;
    case FL_UNFOCUS:
        return 1;
    case FL_PUSH:
        if (focus_callback_ != nullptr) {
            focus_callback_(focus_context_);
        }
        return handle_push();
    case FL_DRAG:
        return handle_drag();
    case FL_RELEASE:
        return handle_release();
    case FL_MOUSEWHEEL:
        return handle_wheel();
    case FL_KEYDOWN: {
        // Ctrl+Shift+C/V are the default copy/paste shortcuts (Stage 5 config).
        const int state = Fl::event_state();
        const int key = Fl::event_key();
        if ((state & FL_CTRL) != 0 && (state & FL_SHIFT) != 0) {
            if (key == 'c' || key == 'C') {
                copy_selection();
                return 1;
            }
            if (key == 'v' || key == 'V') {
                paste_clipboard();
                return 1;
            }
        }
        if ((state & FL_SHIFT) != 0 && (state & FL_CTRL) == 0) {
            // Shift+PageUp/PageDown/Home/End navigate scrollback (DIP 2.9).
            bool scrolled = true;
            if (key == FL_Page_Up) {
                emulator_.scroll_page(1);
            } else if (key == FL_Page_Down) {
                emulator_.scroll_page(-1);
            } else if (key == FL_Home) {
                emulator_.scroll_to_top();
            } else if (key == FL_End) {
                emulator_.scroll_to_bottom();
            } else {
                scrolled = false;
            }
            if (scrolled) {
                full_redraw_requested_ = true;
                redraw();
                return 1;
            }
        }
        // Application commands (pane split/close/focus/resize) take precedence
        // over terminal input (DIP 3.6).
        if (key_callback_ != nullptr && key_callback_(key_context_, key, state)) {
            return 1;
        }
        // A byte-producing key returns the view to live (DIP 2.9).
        if (emulator_.scrollback_line_pos() != emulator_.scrollback_line_count()) {
            emulator_.scroll_to_bottom();
            full_redraw_requested_ = true;
        }
        // The terminal consumes every key; the view fans out translated events
        // to the broadcast destinations (each encodes with its own VTE, 6.1).
        const std::vector<KeyEvent> events = translate_fltk_key_events();
        if (key_input_callback_ != nullptr) {
            key_input_callback_(key_input_context_, events);
        } else {
            for (const KeyEvent &key_event : events) {
                emulator_.handle_keyboard(key_event);
            }
        }
        return 1;
    }
    case FL_PASTE: {
        const char *text = Fl::event_text();
        const int length = Fl::event_length();
        if (paste_text_callback_ != nullptr && text != nullptr && length > 0) {
            paste_text_callback_(paste_text_context_,
                                 std::string_view(text, static_cast<std::size_t>(length)));
        }
        return 1;
    }
    default:
        return 0;
    }
}

void TerminalWidget::event_cell(int &column, int &row) const
{
    column =
        clamp_int(static_cast<int>(static_cast<double>(Fl::event_x() - x()) / metrics_.cell_width),
                  0, emulator_.columns() - 1);
    row =
        clamp_int(static_cast<int>(static_cast<double>(Fl::event_y() - y()) / metrics_.cell_height),
                  0, emulator_.rows() - 1);
}

int TerminalWidget::handle_push()
{
    // Clicking the pane focuses it so it receives key input (watchlist #14).
    Fl::focus(this);
    int column = 0;
    int row = 0;
    event_cell(column, row);

    if (emulator_.mouse_tracking() != MouseTracking::disabled) {
        const MouseButton button = fltk_mouse_button(Fl::event_button());
        pressed_button_ = static_cast<int>(button);
        MouseEvent event;
        event.cell_x = column;
        event.cell_y = row;
        event.pixel_x = Fl::event_x() - x();
        event.pixel_y = Fl::event_y() - y();
        event.button = button;
        event.action = MouseActionPressed;
        event.modifiers = fltk_mouse_modifiers();
        emulator_.handle_mouse(event);
        return 1;
    }

    if (fltk_mouse_button(Fl::event_button()) == MouseButton::middle) {
        // Middle-click pastes the primary selection.
        Fl::paste(*this, 0);
        return 1;
    }

    selecting_ = true;
    if (Fl::event_clicks() > 0) {
        word_selection_ = true;
        emulator_.select_word(column, row, style_.word_chars);
    } else {
        word_selection_ = false;
        emulator_.selection_start(column, row);
    }
    full_redraw_requested_ = true;
    redraw();
    return 1;
}

int TerminalWidget::handle_drag()
{
    int column = 0;
    int row = 0;
    event_cell(column, row);

    const MouseTracking tracking = emulator_.mouse_tracking();
    if (tracking == MouseTracking::any ||
        (tracking == MouseTracking::click && pressed_button_ >= 0)) {
        MouseEvent event;
        event.cell_x = column;
        event.cell_y = row;
        event.pixel_x = Fl::event_x() - x();
        event.pixel_y = Fl::event_y() - y();
        event.button = static_cast<MouseButton>(pressed_button_ >= 0 ? pressed_button_ : 0);
        event.action = MouseActionMoved;
        event.dragging = pressed_button_ >= 0;
        event.modifiers = fltk_mouse_modifiers();
        emulator_.handle_mouse(event);
        return 1;
    }

    if (selecting_) {
        word_selection_ = false;
        emulator_.selection_target(column, row);
        full_redraw_requested_ = true;
        redraw();
    }
    return 1;
}

int TerminalWidget::handle_release()
{
    int column = 0;
    int row = 0;
    event_cell(column, row);

    if (emulator_.mouse_tracking() != MouseTracking::disabled) {
        MouseEvent event;
        event.cell_x = column;
        event.cell_y = row;
        event.pixel_x = Fl::event_x() - x();
        event.pixel_y = Fl::event_y() - y();
        event.button = static_cast<MouseButton>(pressed_button_ >= 0 ? pressed_button_ : 0);
        event.action = MouseActionReleased;
        event.modifiers = fltk_mouse_modifiers();
        emulator_.handle_mouse(event);
        pressed_button_ = -1;
        return 1;
    }

    if (selecting_) {
        // A double-click already selected the whole word; do not shorten it.
        if (!word_selection_) {
            emulator_.selection_target(column, row);
        }
        word_selection_ = false;
        selecting_ = false;
        full_redraw_requested_ = true;
        redraw();
    }
    pressed_button_ = -1;
    return 1;
}

int TerminalWidget::handle_wheel()
{
    const bool shift = (Fl::event_state() & FL_SHIFT) != 0;
    if (!shift && emulator_.mouse_tracking() != MouseTracking::disabled) {
        int column = 0;
        int row = 0;
        event_cell(column, row);
        MouseEvent event;
        event.cell_x = column;
        event.cell_y = row;
        event.pixel_x = Fl::event_x() - x();
        event.pixel_y = Fl::event_y() - y();
        event.button = Fl::event_dy() < 0 ? MouseButton::wheel_up : MouseButton::wheel_down;
        event.action = MouseActionPressed;
        event.modifiers = fltk_mouse_modifiers();
        emulator_.handle_mouse(event);
        return 1;
    }
    // On the alternate screen with reporting off, the wheel sends Up/Down keys
    // (DIP 2.9); otherwise it scrolls scrollback (Shift+wheel always scrolls).
    if (!shift && style_.alternate_scroll && emulator_.alternate_screen()) {
        KeyEvent key;
        key.keysym = Fl::event_dy() < 0 ? 0xff52U : 0xff54U; // XKB_KEY_Up / Down
        emulator_.handle_keyboard(key);
        return 1;
    }
    const int lines = std::max(1, style_.wheel_lines);
    emulator_.scroll_lines(Fl::event_dy() < 0 ? lines : -lines);
    full_redraw_requested_ = true;
    redraw();
    return 1;
}

} // namespace consola
