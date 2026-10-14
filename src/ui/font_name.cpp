// SPDX-License-Identifier: MIT

#include <ui/font_name.hpp>

namespace consola {

std::string fltk_raw_font_name(std::string_view family, bool bold, bool italic)
{
    char modifier = ' ';
    if (bold && italic) {
        modifier = 'P';
    } else if (bold) {
        modifier = 'B';
    } else if (italic) {
        modifier = 'I';
    }
    std::string out;
    out.reserve(family.size() + 1);
    out.push_back(modifier);
    out.append(family);
    return out;
}

} // namespace consola
