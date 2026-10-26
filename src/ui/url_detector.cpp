// SPDX-License-Identifier: MIT

#include <ui/url_detector.hpp>

#include <array>
#include <cctype>

namespace consola {
namespace {

constexpr std::array<std::string_view, 5> kPrefixes = {
    "http://", "https://", "ftp://", "file://", "www.",
};

char lower(char c)
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool starts_with_ci(std::string_view text, std::string_view prefix)
{
    if (text.size() < prefix.size()) {
        return false;
    }
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (lower(text[i]) != lower(prefix[i])) {
            return false;
        }
    }
    return true;
}

std::size_t prefix_length(std::string_view text, std::size_t at)
{
    for (const std::string_view prefix : kPrefixes) {
        if (starts_with_ci(text.substr(at), prefix)) {
            return prefix.size();
        }
    }
    return 0;
}

bool is_word_char(char c)
{
    const auto value = static_cast<unsigned char>(c);
    return std::isalnum(value) != 0 || c == '_' || c == '@';
}

bool is_url_char(char c)
{
    const auto value = static_cast<unsigned char>(c);
    if (std::isspace(value) != 0 || std::iscntrl(value) != 0) {
        return false;
    }
    return c != '"' && c != '\'' && c != '<' && c != '>' && c != '`';
}

bool is_trailing_punctuation(char c)
{
    return std::string_view(".,;:!?)]}'").find(c) != std::string_view::npos;
}

} // namespace

std::vector<UrlMatch> find_urls(std::string_view text)
{
    std::vector<UrlMatch> matches;
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t length = prefix_length(text, i);
        if (length == 0 || (i > 0 && is_word_char(text[i - 1]))) {
            ++i;
            continue;
        }
        // Schemes other than file:// need a host immediately after the
        // delimiter; file:// may be followed by an empty authority and a path.
        const bool file_scheme = starts_with_ci(text.substr(i), "file://");
        if (!file_scheme &&
            (i + length >= text.size() || text[i + length] == '/' || text[i + length] == '?' ||
             text[i + length] == '#' || text[i + length] == ':')) {
            i += length;
            continue;
        }
        std::size_t end = i + length;
        while (end < text.size() && is_url_char(text[end])) {
            ++end;
        }
        while (end > i + length && is_trailing_punctuation(text[end - 1])) {
            --end;
        }
        // Require content beyond the scheme and a plausible authority/host.
        if (end > i + length) {
            matches.push_back(UrlMatch{.begin = i, .end = end});
            i = end;
        } else {
            i += length;
        }
    }
    return matches;
}

} // namespace consola
