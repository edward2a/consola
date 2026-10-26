// SPDX-License-Identifier: MIT

#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <vector>

#include <ui/url_detector.hpp>

namespace {

int g_failures = 0;

#define CHECK(...)                                                                                 \
    do {                                                                                           \
        if (!(__VA_ARGS__)) {                                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__);            \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (false)

std::string_view matched(std::string_view text, const consola::UrlMatch &match)
{
    return text.substr(match.begin, match.end - match.begin);
}

void test_schemes()
{
    const std::string_view text = "see https://example.com/path now";
    const std::vector<consola::UrlMatch> matches = consola::find_urls(text);
    CHECK(matches.size() == 1);
    if (!matches.empty()) {
        CHECK(matched(text, matches[0]) == "https://example.com/path");
    }

    const std::string_view http = "go to http://a.b.";
    const auto http_matches = consola::find_urls(http);
    CHECK(http_matches.size() == 1);
    if (!http_matches.empty()) {
        CHECK(matched(http, http_matches[0]) == "http://a.b");
    }

    const std::string_view upper = "HTTP://A.B/C";
    CHECK(consola::find_urls(upper).size() == 1);

    const std::string_view file = "open file:///tmp/x";
    CHECK(consola::find_urls(file).size() == 1);

    const std::string_view www = "www.example.com/path";
    CHECK(consola::find_urls(www).size() == 1);
}

void test_boundaries()
{
    CHECK(consola::find_urls("example.com").empty());
    CHECK(consola::find_urls("xhttps://a.b").empty());
    CHECK(consola::find_urls("").empty());

    const std::string_view parens = "(https://example.com)";
    const auto matches = consola::find_urls(parens);
    CHECK(matches.size() == 1);
    if (!matches.empty()) {
        CHECK(matched(parens, matches[0]) == "https://example.com");
    }

    const std::string_view two = "http://a.b and https://c.d";
    const auto both = consola::find_urls(two);
    CHECK(both.size() == 2);
    if (both.size() == 2) {
        CHECK(matched(two, both[0]) == "http://a.b");
        CHECK(matched(two, both[1]) == "https://c.d");
    }

    const std::string_view query = "https://x/y?z=1#frag";
    const auto query_match = consola::find_urls(query);
    CHECK(query_match.size() == 1);
    if (!query_match.empty()) {
        CHECK(matched(query, query_match[0]) == query);
    }
}

} // namespace

int main()
{
    test_schemes();
    test_boundaries();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d URL detector failure(s)\n", g_failures);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
