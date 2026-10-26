// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace consola {

// A URL found in one row of visible text. Offsets are character positions in
// the supplied text (half-open). Detection is presentation-only: it reads text
// and never touches terminal state (DIP 6.3).
struct UrlMatch {
    std::size_t begin = 0;
    std::size_t end = 0;
};

// A URL mapped to visible screen cells. Columns are 0-based and half-open.
struct UrlSpan {
    int row = 0;
    int start_column = 0;
    int end_column = 0;
    std::string text;
};

// Find http://, https://, ftp://, file:// and www. URLs in `text`. A match
// stops at whitespace or an HTML/quoting delimiter and trailing punctuation is
// trimmed. Overlapping matches are not produced.
[[nodiscard]] std::vector<UrlMatch> find_urls(std::string_view text);

} // namespace consola
