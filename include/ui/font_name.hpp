// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <string_view>

namespace consola {

// Encode a font family into FLTK's XFT "raw name" form (DIP 2.3). FLTK's Xft
// driver reads the first character of a font name as a style modifier ('I'
// italic, 'B' bold, 'P' bold-italic, ' ' regular) and uses the remainder as the
// family. Passing a bare family such as "Inconsolata Condensed" is therefore
// misread as italic; the modifier must be prepended explicitly.
[[nodiscard]] std::string fltk_raw_font_name(std::string_view family, bool bold, bool italic);

} // namespace consola
