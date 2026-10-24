// SPDX-License-Identifier: MIT

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <optional>
#include <string>

#include <app/session.hpp>
#include <app/tab_manager.hpp>
#include <app/terminal_instance.hpp>
#include <config/capture.hpp>
#include <config/loader.hpp>
#include <config/writer.hpp>
#include <layout/layout_tree.hpp>

namespace {

int g_failures = 0;

#define CHECK(...)                                                                                 \
    do {                                                                                           \
        if (!(__VA_ARGS__)) {                                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__);            \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (false)

using consola::LayoutConfig;
using consola::LayoutNodeConfig;
using consola::LayoutPaneConfig;
using consola::LayoutTabConfig;
using consola::Orientation;

std::unique_ptr<consola::TerminalInstance> make_instance(std::uint64_t id)
{
    consola::SessionOptions options;
    options.executable = "/bin/sh";
    return std::make_unique<consola::TerminalInstance>(consola::TerminalId{id}, options);
}

consola::ConfigLoadResult parse_layout_doc(const std::string &body)
{
    return consola::parse_config("version: 1\nlayouts:\n  l:\n" + body);
}

void test_round_trip()
{
    consola::ApplicationConfig config = consola::default_application_config();

    LayoutNodeConfig root;
    root.orientation = Orientation::vertical;
    root.ratio = 0.25F;
    root.children = {1, 2};

    LayoutNodeConfig first;
    first.pane = LayoutPaneConfig{.id = "p1", .profile = "default", .working_directory = "/tmp"};

    LayoutNodeConfig second;
    second.pane = LayoutPaneConfig{.id = "p2", .profile = "default", .working_directory = ""};

    LayoutTabConfig tab;
    tab.root = 0;
    tab.focused = "p2";
    tab.nodes = {root, first, second};

    LayoutConfig layout;
    layout.active_tab = 0;
    layout.tabs = {tab};
    config.layouts["work"] = layout;

    const std::string text = consola::serialize_config(config);
    const consola::ConfigLoadResult parsed = consola::parse_config(text);
    CHECK(parsed.ok());
    if (parsed.error.has_value()) {
        std::fprintf(stderr, "unexpected error: %s\n", parsed.error.value().c_str());
        return;
    }
    CHECK(parsed.config == config);
    CHECK(parsed.config.layouts.at("work") == layout);
}

void test_validation()
{
    const std::string valid = R"(    active_tab: 0
    tabs:
      - root: 0
        focused: p1
        nodes:
          - pane: {id: p1, profile: default}
)";
    CHECK(parse_layout_doc(valid).ok());

    // A ratio must be strictly between 0 and 1.
    CHECK(!parse_layout_doc(R"(    tabs:
      - root: 0
        focused: p1
        nodes:
          - split: {orientation: horizontal, ratio: 0, children: [1, 2]}
          - pane: {id: p1}
          - pane: {id: p2}
)")
               .ok());
    CHECK(!parse_layout_doc(R"(    tabs:
      - root: 0
        focused: p1
        nodes:
          - split: {orientation: horizontal, ratio: 1.5, children: [1, 2]}
          - pane: {id: p1}
          - pane: {id: p2}
)")
               .ok());

    // Child indices must be in range.
    CHECK(!parse_layout_doc(R"(    tabs:
      - root: 0
        focused: p1
        nodes:
          - split: {orientation: horizontal, ratio: 0.5, children: [1, 5]}
          - pane: {id: p1}
          - pane: {id: p2}
)")
               .ok());

    // A node graph cannot contain a cycle or a shared child.
    CHECK(!parse_layout_doc(R"(    tabs:
      - root: 0
        focused: p1
        nodes:
          - split: {orientation: horizontal, ratio: 0.5, children: [0, 1]}
          - pane: {id: p1}
)")
               .ok());

    // Pane ids must be unique and focus must reference a pane.
    CHECK(!parse_layout_doc(R"(    tabs:
      - root: 0
        focused: p1
        nodes:
          - split: {orientation: horizontal, ratio: 0.5, children: [1, 2]}
          - pane: {id: p1}
          - pane: {id: p1}
)")
               .ok());
    CHECK(!parse_layout_doc(R"(    tabs:
      - root: 0
        focused: nope
        nodes:
          - pane: {id: p1}
)")
               .ok());

    // Pane profiles must exist.
    CHECK(!parse_layout_doc(R"(    tabs:
      - root: 0
        focused: p1
        nodes:
          - pane: {id: p1, profile: absent}
)")
               .ok());

    // active_tab must be in range.
    CHECK(!parse_layout_doc(R"(    active_tab: 3
    tabs:
      - root: 0
        focused: p1
        nodes:
          - pane: {id: p1}
)")
               .ok());

    // Every node must be reachable from the root.
    CHECK(!parse_layout_doc(R"(    tabs:
      - root: 0
        focused: p1
        nodes:
          - pane: {id: p1}
          - pane: {id: p2}
)")
               .ok());

    // A node is exactly one of a split or a pane.
    CHECK(!parse_layout_doc(R"(    tabs:
      - root: 0
        focused: p1
        nodes:
          - split: {orientation: horizontal, ratio: 0.5, children: [1, 2]}
            pane: {id: p1}
          - pane: {id: p2}
          - pane: {id: p3}
)")
               .ok());
}

void test_capture()
{
    consola::TabManager tabs;
    consola::Tab *tab = tabs.find(tabs.active_id());
    CHECK(tab != nullptr);
    if (tab == nullptr) {
        return;
    }
    CHECK(tab->add_root(make_instance(1)) != nullptr);
    CHECK(
        tab->split_terminal(consola::TerminalId{1}, Orientation::horizontal, make_instance(2), 0.4F)
            .has_value());
    tab->layout().focus(consola::TerminalId{2});

    const LayoutConfig layout = consola::capture_layout(tabs, "default");
    CHECK(layout.active_tab == 0);
    CHECK(layout.tabs.size() == 1);
    if (layout.tabs.size() != 1) {
        return;
    }
    const LayoutTabConfig &captured = layout.tabs.front();
    CHECK(captured.root == 0);
    CHECK(captured.nodes.size() == 3);
    if (captured.nodes.size() != 3) {
        return;
    }
    CHECK(captured.nodes[0].children.size() == 2);
    CHECK(captured.nodes[0].ratio == 0.4F);
    CHECK(captured.nodes[0].orientation == Orientation::horizontal);
    CHECK(captured.focused == "p2");
    const std::optional<LayoutPaneConfig> &first_pane = captured.nodes[1].pane;
    CHECK(first_pane.has_value());
    if (first_pane.has_value()) {
        CHECK(first_pane.value().id == "p1");
    }
    const std::optional<LayoutPaneConfig> &second_pane = captured.nodes[2].pane;
    CHECK(second_pane.has_value());
    if (second_pane.has_value()) {
        CHECK(second_pane.value().id == "p2");
    }

    // Captured data round-trips through the configuration file.
    consola::ApplicationConfig config = consola::default_application_config();
    config.layouts["captured"] = layout;
    const std::string text = consola::serialize_config(config);
    const consola::ConfigLoadResult parsed = consola::parse_config(text);
    CHECK(parsed.ok());
    if (parsed.ok()) {
        CHECK(parsed.config.layouts.at("captured") == layout);
    }
}

} // namespace

int main()
{
    try {
        test_round_trip();
        test_validation();
        test_capture();
    } catch (const std::exception &exception) {
        std::fprintf(stderr, "unexpected exception: %s\n", exception.what());
        return EXIT_FAILURE;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    std::printf("config_layout_test: ok\n");
    return EXIT_SUCCESS;
}
