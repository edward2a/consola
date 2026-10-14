// SPDX-License-Identifier: MIT

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

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

consola::TerminalId tid(std::uint64_t value)
{
    return consola::TerminalId{value};
}

consola::NodeId node_of_checked(const consola::LayoutTree &tree, consola::TerminalId terminal,
                                int line)
{
    const auto found = tree.find_terminal(terminal);
    if (!found) {
        std::fprintf(stderr, "FAIL line %d: terminal not found\n", line);
        ++g_failures;
        return consola::NodeId{};
    }
    return *found;
}

#define NODE_OF(tree, value) node_of_checked((tree), tid(value), __LINE__)

consola::NodeId required(const std::optional<consola::NodeId> &value, int line)
{
    if (!value) {
        std::fprintf(stderr, "FAIL line %d: expected a NodeId\n", line);
        ++g_failures;
        return consola::NodeId{};
    }
    return *value;
}

#define REQUIRED(value) required((value), __LINE__)

void test_empty_tree()
{
    consola::LayoutTree tree;
    CHECK(tree.empty());
    CHECK(tree.terminal_count() == 0);
    CHECK(tree.root().value == 0);
    CHECK(tree.terminals().empty());
    CHECK(!tree.focused().has_value());
    CHECK(tree.find_terminal(tid(1)) == std::nullopt);
    CHECK(!tree.remove(tid(1)).has_value());
}

void test_single_terminal()
{
    consola::LayoutTree tree(tid(1));
    CHECK(!tree.empty());
    CHECK(tree.terminal_count() == 1);
    CHECK(tree.contains(tid(1)));
    CHECK(tree.terminals() == std::vector<consola::TerminalId>{tid(1)});
    CHECK(tree.focused() == std::optional<consola::TerminalId>(tid(1)));

    const consola::NodeId root = tree.root();
    CHECK(root.value != 0);
    CHECK(tree.terminal(root) != nullptr);
    CHECK(tree.terminal(root)->terminal == tid(1));
    CHECK(tree.split(root) == nullptr);
    CHECK(!tree.find_parent(root).has_value());
}

void test_split_root()
{
    consola::LayoutTree tree(tid(1));
    const auto added = tree.split_terminal(tid(1), consola::Orientation::horizontal, tid(2));
    CHECK(added.has_value());
    CHECK(tree.terminal_count() == 2);
    CHECK(tree.contains(tid(2)));

    const consola::NodeId root = tree.root();
    const consola::SplitNode *split = tree.split(root);
    CHECK(split != nullptr);
    CHECK(split->orientation == consola::Orientation::horizontal);
    CHECK(split->ratio == 0.5F);
    CHECK(split->first == NODE_OF(tree, 1));
    CHECK(split->second == REQUIRED(added));

    CHECK(tree.find_parent(REQUIRED(added)) == std::optional<consola::NodeId>(root));
    CHECK(tree.find_parent(NODE_OF(tree, 1)) == std::optional<consola::NodeId>(root));
    CHECK(tree.terminals() == std::vector<consola::TerminalId>{tid(1), tid(2)});
}

void test_nested_splits()
{
    consola::LayoutTree tree(tid(1));
    CHECK(tree.split_terminal(tid(1), consola::Orientation::horizontal, tid(2)).has_value());
    const auto added3 = tree.split_terminal(tid(2), consola::Orientation::vertical, tid(3));
    CHECK(added3.has_value());

    const consola::NodeId root = tree.root();
    const consola::SplitNode *root_split = tree.split(root);
    CHECK(root_split != nullptr);
    CHECK(root_split->orientation == consola::Orientation::horizontal);

    const consola::SplitNode *nested = tree.split(root_split->second);
    CHECK(nested != nullptr);
    CHECK(nested->orientation == consola::Orientation::vertical);
    CHECK(nested->first == NODE_OF(tree, 2));
    CHECK(nested->second == REQUIRED(added3));

    CHECK(tree.find_parent(root_split->second) == std::optional<consola::NodeId>(root));
    CHECK(tree.find_parent(nested->first) == std::optional<consola::NodeId>(root_split->second));
    CHECK(tree.terminals() == std::vector<consola::TerminalId>{tid(1), tid(2), tid(3)});
}

void test_repeated_splitting()
{
    consola::LayoutTree tree(tid(1));
    CHECK(tree.split_terminal(tid(1), consola::Orientation::vertical, tid(2)).has_value());
    CHECK(tree.split_terminal(tid(1), consola::Orientation::horizontal, tid(3)).has_value());
    CHECK(tree.split_terminal(tid(3), consola::Orientation::vertical, tid(4)).has_value());
    CHECK(tree.terminal_count() == 4);

    const consola::NodeId root = tree.root();
    const consola::SplitNode *root_split = tree.split(root);
    CHECK(root_split != nullptr);
    CHECK(root_split->orientation == consola::Orientation::vertical);
    CHECK(root_split->second == NODE_OF(tree, 2));

    const consola::NodeId left_id = root_split->first;
    const consola::SplitNode *left = tree.split(left_id);
    CHECK(left != nullptr);
    CHECK(left->orientation == consola::Orientation::horizontal);
    CHECK(left->first == NODE_OF(tree, 1));

    const consola::SplitNode *deep = tree.split(left->second);
    CHECK(deep != nullptr);
    CHECK(deep->orientation == consola::Orientation::vertical);
    CHECK(deep->first == NODE_OF(tree, 3));
    CHECK(deep->second == NODE_OF(tree, 4));
    CHECK(tree.terminals() == std::vector<consola::TerminalId>{tid(1), tid(3), tid(4), tid(2)});
}

void test_removal_collapses_parent()
{
    consola::LayoutTree tree(tid(1));
    tree.split_terminal(tid(1), consola::Orientation::horizontal, tid(2));
    tree.split_terminal(tid(2), consola::Orientation::vertical, tid(3));

    const consola::NodeId root = tree.root();
    const consola::NodeId nested = tree.split(root)->second;
    const consola::NodeId leaf3 = NODE_OF(tree, 3);

    const auto removed = tree.remove(tid(3));
    CHECK(removed == std::optional<consola::NodeId>(leaf3));
    CHECK(tree.node(leaf3) == nullptr);
    CHECK(tree.node(nested) == nullptr);
    CHECK(tree.terminal_count() == 2);
    CHECK(tree.contains(tid(1)));
    CHECK(tree.contains(tid(2)));
    CHECK(!tree.contains(tid(3)));

    const consola::SplitNode *root_split = tree.split(root);
    CHECK(root_split != nullptr);
    CHECK(root_split->first == NODE_OF(tree, 1));
    CHECK(root_split->second == NODE_OF(tree, 2));
    CHECK(tree.find_parent(root_split->second) == std::optional<consola::NodeId>(root));
    CHECK(tree.terminals() == std::vector<consola::TerminalId>{tid(1), tid(2)});

    CHECK(tree.remove(tid(1)).has_value());
    CHECK(tree.terminal_count() == 1);
    const consola::NodeId new_root = tree.root();
    CHECK(tree.terminal(new_root) != nullptr);
    CHECK(tree.terminal(new_root)->terminal == tid(2));
    CHECK(!tree.find_parent(new_root).has_value());
    CHECK(tree.terminals() == std::vector<consola::TerminalId>{tid(2)});

    CHECK(tree.remove(tid(2)).has_value());
    CHECK(tree.empty());
    CHECK(tree.root().value == 0);
    CHECK(!tree.focused().has_value());
}

void test_remove_absent()
{
    consola::LayoutTree tree(tid(1));
    CHECK(!tree.remove(tid(99)).has_value());
    CHECK(tree.terminal_count() == 1);
}

void test_split_rejections()
{
    consola::LayoutTree tree(tid(1));
    CHECK(!tree.split_terminal(tid(99), consola::Orientation::horizontal, tid(2)).has_value());
    CHECK(!tree.split_terminal(tid(1), consola::Orientation::horizontal, tid(1)).has_value());
    CHECK(!tree.split_terminal(tid(1), consola::Orientation::horizontal, tid(2), 0.0F).has_value());
    CHECK(!tree.split_terminal(tid(1), consola::Orientation::horizontal, tid(2), 1.0F).has_value());
    CHECK(
        !tree.split_terminal(tid(1), consola::Orientation::horizontal, tid(2), -0.1F).has_value());
    CHECK(!tree.split_terminal(tid(1), consola::Orientation::horizontal, tid(2), 1.1F).has_value());
    CHECK(!tree.split_terminal(tid(1), consola::Orientation::horizontal, tid(2),
                               std::numeric_limits<float>::quiet_NaN())
               .has_value());
    CHECK(tree.terminal_count() == 1);

    CHECK(tree.split_terminal(tid(1), consola::Orientation::vertical, tid(2), 0.25F).has_value());
    CHECK(tree.split(tree.root())->ratio == 0.25F);
}

void test_focus()
{
    consola::LayoutTree tree(tid(1));
    CHECK(tree.focused() == std::optional<consola::TerminalId>(tid(1)));

    tree.focus(tid(99));
    CHECK(tree.focused() == std::optional<consola::TerminalId>(tid(1)));

    tree.split_terminal(tid(1), consola::Orientation::horizontal, tid(2));
    tree.split_terminal(tid(2), consola::Orientation::vertical, tid(3));
    tree.focus(tid(2));
    CHECK(tree.focused() == std::optional<consola::TerminalId>(tid(2)));

    tree.remove(tid(2));
    CHECK(tree.focused() == std::optional<consola::TerminalId>(tid(3)));
}

void test_focus_moves_into_sibling_subtree()
{
    consola::LayoutTree tree(tid(1));
    tree.split_terminal(tid(1), consola::Orientation::horizontal, tid(2));
    tree.split_terminal(tid(2), consola::Orientation::vertical, tid(3));
    tree.focus(tid(1));

    tree.remove(tid(1));
    CHECK(tree.terminal_count() == 2);
    CHECK(tree.focused() == std::optional<consola::TerminalId>(tid(2)));
}

void test_focus_on_last_removal()
{
    consola::LayoutTree tree(tid(1));
    tree.focus(tid(1));
    tree.remove(tid(1));
    CHECK(!tree.focused().has_value());
}

void test_equalize()
{
    consola::LayoutTree tree(tid(1));
    tree.split_terminal(tid(1), consola::Orientation::horizontal, tid(2), 0.3F);
    tree.split_terminal(tid(2), consola::Orientation::vertical, tid(3), 0.7F);

    tree.equalize();
    const consola::SplitNode *root = tree.split(tree.root());
    CHECK(root != nullptr);
    CHECK(root->ratio == 0.5F);
    const consola::SplitNode *nested = tree.split(root->second);
    CHECK(nested != nullptr);
    CHECK(nested->ratio == 0.5F);
}

void test_set_split_ratio()
{
    consola::LayoutTree tree(tid(1));
    tree.split_terminal(tid(1), consola::Orientation::vertical, tid(2));
    const consola::NodeId root = tree.root();

    CHECK(tree.set_split_ratio(root, 0.25F));
    CHECK(tree.split(root)->ratio == 0.25F);
    CHECK(!tree.set_split_ratio(root, 0.0F));
    CHECK(!tree.set_split_ratio(root, 1.0F));
    CHECK(!tree.set_split_ratio(root, -0.1F));
    CHECK(!tree.set_split_ratio(root, std::numeric_limits<float>::quiet_NaN()));
    CHECK(!tree.set_split_ratio(NODE_OF(tree, 1), 0.5F));
    CHECK(!tree.set_split_ratio(consola::NodeId{999}, 0.5F));
    CHECK(tree.split(root)->ratio == 0.25F);
}

} // namespace

int main()
{
    test_empty_tree();
    test_single_terminal();
    test_split_root();
    test_nested_splits();
    test_repeated_splitting();
    test_removal_collapses_parent();
    test_remove_absent();
    test_split_rejections();
    test_focus();
    test_focus_moves_into_sibling_subtree();
    test_focus_on_last_removal();
    test_equalize();
    test_set_split_ratio();

    if (g_failures != 0) {
        std::fprintf(stderr, "%d layout check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
