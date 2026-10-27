// tests/p3/test_p3.cpp — YOUR P3 TEST SUITE.
//
// The five tests below are worked examples. Keep them, and add your own
// until you have AT LEAST 15 (spec §6 lists what must be covered).
// Name every test <area>_<what>: reg_ for the registry, ws_ for the
// workspace, calc_ for the calculator.
//
// Build & run:   cmake --build build && ./build/test_p3
// One area:      ./build/test_p3 reg_        (names starting with "reg_")
// One test:      ./build/test_p3 =reg_add_and_find

#include "../mini_test.h"
#include "../test_helpers.h"     // TempDir, write_text, args1, ...

#include "tools/calculator.h"
#include "tools/calculator_tool.h"
#include "tools/file_tools.h"
#include "tools/tool_registry.h"
#include "tools/workspace.h"

#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// A tool whose name you choose — use it to fill the registry in tests.
class NamedTool : public Tool {
public:
    explicit NamedTool(std::string n) : n_(std::move(n)), schema_(JsonValue::object()) {}
    std::string name() const override { return n_; }
    std::string description() const override { return "test tool"; }
    const JsonValue& schema() const override { return schema_; }
    ToolResult invoke(const JsonValue&, ToolContext&) override { return ToolResult::success(n_); }
private:
    std::string n_;
    JsonValue   schema_;   // an empty JSON object: {}
};

// Tokenize with the provided tokenizer, then run YOUR calc_evaluate.
bool eval(const std::string& expr, double* value, std::string* error) {
    std::vector<CalcToken> tokens;
    if (!calc_tokenize(expr, &tokens, error)) return false;
    return calc_evaluate(tokens, value, error);
}

}  // namespace

// ---------------------------------------------------------------- examples

TEST(reg_add_and_find) {
    ToolRegistry r;
    auto t = std::make_unique<NamedTool>("alpha");
    Tool* raw = t.get();                        // remember the address before moving
    REQUIRE(r.add(std::move(t)));
    CHECK(r.size() == 1);
    CHECK(r.find("alpha") == raw);              // same object, not a copy
    CHECK(r.find("beta") == nullptr);
}

TEST(reg_duplicate_keeps_original) {
    ToolRegistry r;
    auto first = std::make_unique<NamedTool>("alpha");
    Tool* original = first.get();
    REQUIRE(r.add(std::move(first)));
    CHECK(!r.add(std::make_unique<NamedTool>("alpha")));
    CHECK(r.find("alpha") == original);
    CHECK(r.size() == 1);
}

TEST(ws_rejects_parent_escape) {
    TempDir d;
    Workspace ws(d.path());
    fs::path out;
    std::string reason;
    REQUIRE(ws.resolve("notes/todo.txt", &out, &reason));   // sanity: accepts *something*
    CHECK(out == ws.root() / "notes" / "todo.txt");
    CHECK(!ws.resolve("../etc/passwd", &out, &reason));
    CHECK(!reason.empty());
}

TEST(calc_precedence) {
    double v = 0;
    std::string err;
    REQUIRE(eval("2 + 3 * 4", &v, &err));
    CHECK(v == 14);
}

TEST(calc_division_by_zero) {
    double v = 0;
    std::string err;
    CHECK(!eval("1 / 0", &v, &err));
    CHECK(err == "division by zero");
}

// ------------------------------------------------------------- registry

namespace {

// Counts how many instances are alive, to check ownership.
class CountedTool : public NamedTool {
public:
    CountedTool(std::string n, int* alive) : NamedTool(std::move(n)), alive_(alive) { ++*alive_; }
    ~CountedTool() override { --*alive_; }
private:
    int* alive_;
};

std::string tool_name(int i) { return "tool_" + std::to_string(i); }

}  // namespace

TEST(reg_empty_table) {
    ToolRegistry r;
    CHECK(r.size() == 0);
    CHECK(r.bucket_count() >= 1);
    CHECK(r.bucket_count() <= 64);
    CHECK(r.load_factor() == 0.0);
    CHECK(r.max_probe_length() == 0);
    CHECK(r.find("") == nullptr);
    CHECK(r.find("calculator") == nullptr);
    int visits = 0;
    r.for_each([&](const Tool&) { ++visits; });
    CHECK(visits == 0);
}

TEST(reg_null_rejected) {
    ToolRegistry r;
    CHECK(!r.add(nullptr));
    CHECK(r.size() == 0);
    REQUIRE(r.add(std::make_unique<NamedTool>("alpha")));
    CHECK(!r.add(std::unique_ptr<Tool>()));
    CHECK(r.size() == 1);
}

TEST(reg_find_absent) {
    ToolRegistry r;
    REQUIRE(r.add(std::make_unique<NamedTool>("read_file")));
    CHECK(r.find("read_fil") == nullptr);    // prefix
    CHECK(r.find("read_file_") == nullptr);  // extension
    CHECK(r.find("READ_FILE") == nullptr);   // case-sensitive
    CHECK(r.find("") == nullptr);
    CHECK(r.size() == 1);
}

TEST(reg_size_tracks_every_operation) {
    ToolRegistry r;
    CHECK(r.size() == 0);
    REQUIRE(r.add(std::make_unique<NamedTool>("a")));
    CHECK(r.size() == 1);
    CHECK(!r.add(std::make_unique<NamedTool>("a")));
    CHECK(r.size() == 1);
    CHECK(!r.add(nullptr));
    CHECK(r.size() == 1);
    CHECK(r.find("missing") == nullptr);
    CHECK(r.size() == 1);
    REQUIRE(r.add(std::make_unique<NamedTool>("b")));
    CHECK(r.size() == 2);
}

TEST(reg_empty_name_is_a_valid_key) {
    ToolRegistry r;
    auto t = std::make_unique<NamedTool>("");
    Tool* raw = t.get();
    REQUIRE(r.add(std::move(t)));
    CHECK(r.find("") == raw);
    CHECK(!r.add(std::make_unique<NamedTool>("")));
}

TEST(reg_duplicate_destroys_new_tool) {
    int alive = 0;
    {
        ToolRegistry r;
        REQUIRE(r.add(std::make_unique<CountedTool>("alpha", &alive)));
        CHECK(alive == 1);
        CHECK(!r.add(std::make_unique<CountedTool>("alpha", &alive)));
        CHECK(alive == 1);   // the rejected duplicate was destroyed right away
    }
    CHECK(alive == 0);       // the destructor freed the original
}

TEST(reg_destructor_frees_every_tool_once) {
    int alive = 0;
    {
        ToolRegistry r;
        for (int i = 0; i < 500; ++i)   // crosses several resizes
            REQUIRE(r.add(std::make_unique<CountedTool>(tool_name(i), &alive)));
        CHECK(alive == 500);
    }
    CHECK(alive == 0);   // ASan would also flag a double delete here
}

TEST(reg_for_each_visits_each_once) {
    ToolRegistry r;
    std::vector<std::string> names = {"read_file", "write_file", "list_dir", "calculator", "echo"};
    for (const auto& n : names) REQUIRE(r.add(std::make_unique<NamedTool>(n)));

    std::map<std::string, int> seen;
    int count = 0;
    r.for_each([&](const Tool& t) {
        ++seen[t.name()];
        ++count;
    });
    CHECK(count == 5);
    CHECK(seen.size() == names.size());
    for (const auto& n : names) CHECK(seen[n] == 1);
}

TEST(reg_for_each_after_resize) {
    ToolRegistry r;
    const int n = 1000;
    for (int i = 0; i < n; ++i) REQUIRE(r.add(std::make_unique<NamedTool>(tool_name(i))));
    std::vector<int> hits(n, 0);
    r.for_each([&](const Tool& t) { ++hits[std::stoi(t.name().substr(5))]; });
    for (int i = 0; i < n; ++i) CHECK(hits[i] == 1);
}

TEST(reg_hash_properties) {
    // Order-sensitive, full width, deterministic, uses every character.
    CHECK(ToolRegistry::hash("ab") != ToolRegistry::hash("ba"));
    CHECK(ToolRegistry::hash("tool_1234") != ToolRegistry::hash("tool_1243"));
    CHECK(ToolRegistry::hash("calculator") == ToolRegistry::hash("calculator"));
    CHECK(ToolRegistry::hash("abcdefgh_x") != ToolRegistry::hash("abcdefgh_y"));   // last char matters
    CHECK(ToolRegistry::hash("xbc") != ToolRegistry::hash("ybc"));                 // first char matters
    CHECK(ToolRegistry::hash("") != 0);
    // Not reduced to the table size: some hash must exceed any small table.
    bool big = false;
    for (int i = 0; i < 100; ++i) big = big || ToolRegistry::hash(tool_name(i)) > 1000000;
    CHECK(big);
}

TEST(reg_hash_distinct_for_10000_names) {
    std::set<std::size_t> distinct;
    for (int i = 0; i < 10000; ++i) distinct.insert(ToolRegistry::hash(tool_name(i)));
    CHECK(distinct.size() >= 9990);
}

TEST(reg_10000_tools) {
    ToolRegistry r;
    auto first = std::make_unique<NamedTool>(tool_name(0));
    Tool* first_ptr = first.get();
    REQUIRE(r.add(std::move(first)));
    REQUIRE(r.find(tool_name(0)) == first_ptr);
    CHECK(r.load_factor() <= 1.0);

    std::size_t resizes = 0, last_buckets = r.bucket_count();
    for (int i = 1; i < 10000; ++i) {
        REQUIRE(r.add(std::make_unique<NamedTool>(tool_name(i))));
        CHECK(r.load_factor() <= 1.0);
        if (r.bucket_count() != last_buckets) {
            ++resizes;
            last_buckets = r.bucket_count();
        }
    }
    CHECK(r.size() == 10000);
    CHECK(resizes > 0);
    CHECK(r.find(tool_name(0)) == first_ptr);   // survived every resize

    for (int i = 0; i < 10000; ++i) {
        Tool* t = r.find(tool_name(i));
        REQUIRE(t != nullptr);
        CHECK(t->name() == tool_name(i));
    }
    CHECK(r.find("tool_10000") == nullptr);
    CHECK(r.max_probe_length() <= 64);
    CHECK(r.load_factor() == static_cast<double>(r.size()) / static_cast<double>(r.bucket_count()));

    std::printf("    [reg_10000_tools] bucket_count=%zu load_factor=%.4f max_probe_length=%zu resizes=%zu\n",
                r.bucket_count(), r.load_factor(), r.max_probe_length(), resizes);
}

TEST(reg_pointers_stable_across_resizes) {
    ToolRegistry r;
    std::vector<Tool*> ptrs;
    for (int i = 0; i < 2000; ++i) {
        auto t = std::make_unique<NamedTool>(tool_name(i));
        ptrs.push_back(t.get());
        REQUIRE(r.add(std::move(t)));
    }
    for (int i = 0; i < 2000; ++i) CHECK(r.find(tool_name(i)) == ptrs[i]);
    // The objects behind the pointers are still alive and usable.
    TempDir d;
    ToolContext ctx{Workspace(d.path())};
    ToolResult res = ptrs[1234]->invoke(JsonValue::object(), ctx);
    CHECK(res.ok);
    CHECK(res.content == "tool_1234");
}

TEST(reg_alternative_hash) {
    ToolRegistry r;
    for (int i = 0; i < 10000; ++i) REQUIRE(r.add(std::make_unique<NamedTool>(tool_name(i))));
    const std::size_t buckets = r.bucket_count();

    // Weak hash s[0] + s.size(), bucketed the same way at the same bucket count.
    std::vector<int> weak(buckets, 0);
    // djb2 for comparison.
    std::vector<int> djb2(buckets, 0);
    for (int i = 0; i < 10000; ++i) {
        std::string s = tool_name(i);
        std::size_t w = static_cast<unsigned char>(s[0]) + s.size();
        ++weak[w % buckets];
        std::size_t h = 5381;
        for (unsigned char c : s) h = h * 33 + c;
        ++djb2[h % buckets];
    }
    int weak_max = 0, djb2_max = 0;
    for (std::size_t b = 0; b < buckets; ++b) {
        if (weak[b] > weak_max) weak_max = weak[b];
        if (djb2[b] > djb2_max) djb2_max = djb2[b];
    }
    std::printf("    [reg_alternative_hash] buckets=%zu  FNV-1a longest chain=%zu  "
                "djb2 longest chain=%d  s[0]+s.size() longest chain=%d\n",
                buckets, r.max_probe_length(), djb2_max, weak_max);
    CHECK(weak_max > 64);                                       // the weak hash fails the bound
    CHECK(static_cast<std::size_t>(weak_max) > r.max_probe_length());
}

// ------------------------------------------------------------- workspace

namespace {

bool accepts(const Workspace& ws, const std::string& in, const fs::path& expect) {
    fs::path out;
    std::string reason = "unchanged";
    if (!ws.resolve(in, &out, &reason)) return false;
    return out == expect;
}

bool rejects(const Workspace& ws, const std::string& in) {
    fs::path out = "/untouched";
    std::string reason;
    bool ok = ws.resolve(in, &out, &reason);
    return !ok && !reason.empty();
}

}  // namespace

TEST(ws_accepts_simple_relative) {
    TempDir d;
    Workspace ws(d.path());
    CHECK(accepts(ws, "notes/todo.txt", ws.root() / "notes" / "todo.txt"));
}

TEST(ws_accepts_dot_and_double_slash) {
    TempDir d;
    Workspace ws(d.path());
    CHECK(accepts(ws, "./notes//todo.txt", ws.root() / "notes" / "todo.txt"));
}

TEST(ws_dot_is_root) {
    TempDir d;
    Workspace ws(d.path());
    CHECK(accepts(ws, ".", ws.root()));
    CHECK(accepts(ws, "./", ws.root()));
    CHECK(accepts(ws, "./.", ws.root()));
}

TEST(ws_folds_inner_dotdot) {
    TempDir d;
    Workspace ws(d.path());
    CHECK(accepts(ws, "a/../b", ws.root() / "b"));
    CHECK(accepts(ws, "a/..", ws.root()));
    CHECK(accepts(ws, "a/b/../../c", ws.root() / "c"));
}

TEST(ws_accepts_nonexistent_and_does_not_create) {
    TempDir d;
    Workspace ws(d.path());
    CHECK(accepts(ws, "new/dir/file.txt", ws.root() / "new" / "dir" / "file.txt"));
    CHECK(!fs::exists(ws.root() / "new"));   // resolve is purely textual
}

TEST(ws_accepts_dots_inside_names) {
    TempDir d;
    Workspace ws(d.path());
    CHECK(accepts(ws, "my..notes.txt", ws.root() / "my..notes.txt"));
    CHECK(accepts(ws, "...", ws.root() / "..."));
    CHECK(accepts(ws, "..x/y", ws.root() / "..x" / "y"));
    CHECK(accepts(ws, ".hidden", ws.root() / ".hidden"));
}

TEST(ws_rejects_empty) {
    TempDir d;
    Workspace ws(d.path());
    fs::path out;
    std::string reason;
    CHECK(!ws.resolve("", &out, &reason));
    CHECK(reason == "empty path");
}

TEST(ws_rejects_nul_byte) {
    TempDir d;
    Workspace ws(d.path());
    CHECK(rejects(ws, std::string("notes") + '\0' + "/x"));
    CHECK(rejects(ws, std::string(1, '\0')));
    CHECK(accepts(ws, "notes", ws.root() / "notes"));   // what the naive literal would test
}

TEST(ws_rejects_absolute) {
    TempDir d;
    Workspace ws(d.path());
    CHECK(rejects(ws, "/etc/passwd"));
    CHECK(rejects(ws, "/"));
    CHECK(rejects(ws, "//etc/passwd"));
    // Absolute even when it points inside root.
    CHECK(rejects(ws, (ws.root() / "notes" / "todo.txt").string()));
}

TEST(ws_rejects_dotdot) {
    TempDir d;
    Workspace ws(d.path());
    CHECK(rejects(ws, ".."));
    CHECK(rejects(ws, "../"));
    CHECK(rejects(ws, "../etc/passwd"));
    CHECK(rejects(ws, "a/../../b"));
    CHECK(rejects(ws, "./../x"));
    CHECK(rejects(ws, "../../../../etc/passwd"));
}

TEST(ws_rejects_escape_that_comes_back) {
    TempDir d;
    fs::path root = d.path() / "ws";
    fs::create_directories(root / "notes");
    Workspace ws(root);
    CHECK(rejects(ws, "notes/../../ws/notes/todo.txt"));
    CHECK(rejects(ws, "../ws"));
}

TEST(ws_file_tools_use_resolve) {
    TempDir d;
    write_text(d.path() / "notes" / "todo.txt", "hello\n");
    ToolContext ctx{Workspace(d.path())};
    ReadFileTool reader;
    ToolResult ok = reader.invoke(args1("path", JsonValue::string("./notes//todo.txt")), ctx);
    CHECK(ok.ok);
    CHECK(ok.content == "hello\n");
    ToolResult bad = reader.invoke(args1("path", JsonValue::string("../etc/passwd")), ctx);
    CHECK(!bad.ok);
    CHECK(!bad.error.empty());
    ToolResult abs = reader.invoke(args1("path", JsonValue::string("/etc/passwd")), ctx);
    CHECK(!abs.ok);
}

// ------------------------------------------------------------- calculator

namespace {

bool eval_is(const std::string& expr, double expect) {
    double v = 0;
    std::string err;
    return eval(expr, &v, &err) && v == expect;
}

}  // namespace

TEST(calc_spec_table) {
    CHECK(eval_is("2 + 3 * 4", 14));
    CHECK(eval_is("10 - 4 - 3", 3));
    CHECK(eval_is("100 / 10 / 5", 2));
    CHECK(eval_is("(2 + 3) * 4", 20));
    CHECK(eval_is("2 * (3 + 4) - 5", 9));
    CHECK(eval_is("-3 * -3", 9));
    CHECK(eval_is("7 / 2", 3.5));
    double v = 0;
    std::string err;
    CHECK(!eval("1 / (2 - 2)", &v, &err));
    CHECK(err == "division by zero");
}

TEST(calc_left_to_right) {
    CHECK(eval_is("10 - 4 - 3", 3));
    CHECK(eval_is("100 / 10 / 5", 2));
    CHECK(eval_is("8 / 4 * 2", 4));        // not 8 / (4*2) = 1
    CHECK(eval_is("10 - 2 + 3", 11));      // not 10 - (2+3) = 5
    CHECK(eval_is("2 * 3 / 4 * 8", 12));
}

TEST(calc_operand_order) {
    CHECK(eval_is("10 - 4", 6));
    CHECK(eval_is("1 / 4", 0.25));
}

TEST(calc_parentheses) {
    CHECK(eval_is("(1)", 1));
    CHECK(eval_is("((((7))))", 7));
    CHECK(eval_is("10 - (4 - 3)", 9));
    CHECK(eval_is("(1 + 2) * (3 + 4)", 21));
    CHECK(eval_is("2 * ((3 + 4) * (5 - 1)) / 7", 8));
    CHECK(eval_is("(12 + 30) * 2 / 7", 12));
}

TEST(calc_single_number_and_negatives) {
    CHECK(eval_is("42", 42));
    CHECK(eval_is("-5", -5));
    CHECK(eval_is("2 * -3", -6));
    CHECK(eval_is("(-1)", -1));
    CHECK(eval_is("-2 - -3", 1));
    CHECK(eval_is("0 - 5", -5));
}

TEST(calc_decimals) {
    CHECK(eval_is("0.5 + 0.25", 0.75));
    CHECK(eval_is("3.25 * 4", 13));
    CHECK(eval_is("007 + 1", 8));
    double v = 0;
    std::string err;
    REQUIRE(eval("0.1 + 0.2", &v, &err));
    CHECK(v > 0.2999999 && v < 0.3000001);
}

TEST(calc_division_by_zero_variants) {
    double v = 0;
    std::string err;
    CHECK(!eval("5 / 0.0", &v, &err));
    CHECK(err == "division by zero");
    err.clear();
    CHECK(!eval("1 + 2 / (3 - 3) * 4", &v, &err));
    CHECK(err == "division by zero");
    err.clear();
    CHECK(!eval("0 / 0", &v, &err));
    CHECK(err == "division by zero");
    CHECK(eval_is("0 / 5", 0));
}

TEST(calc_long_sum_20000_terms) {
    std::string expr = "1";
    for (int i = 1; i < 20000; ++i) expr += "+1";
    CHECK(eval_is(expr, 20000));
    std::string alt = "1";
    for (int i = 1; i < 20000; ++i) alt += (i % 2 ? "*1" : "-0");
    CHECK(eval_is(alt, 1));
}

TEST(calc_deep_nesting_5000) {
    std::string expr = std::string(5000, '(') + "3" + std::string(5000, ')');
    CHECK(eval_is(expr, 3));
    std::string sum;
    for (int i = 0; i < 5000; ++i) sum += "(1+";
    sum += "0";
    sum += std::string(5000, ')');
    CHECK(eval_is(sum, 5000));
}

TEST(calc_malformed_tokens_do_not_crash) {
    using K = CalcToken::Kind;
    auto num = [](double v) { CalcToken t; t.kind = K::Number; t.value = v; return t; };
    auto op = [](K k) { CalcToken t; t.kind = k; return t; };
    double v = 0;
    std::string err;

    CHECK(!calc_evaluate({}, &v, &err));
    CHECK(!err.empty());
    err.clear();
    CHECK(!calc_evaluate({op(K::Plus)}, &v, &err));
    CHECK(!err.empty());
    err.clear();
    CHECK(!calc_evaluate({num(1), op(K::Plus)}, &v, &err));
    CHECK(!err.empty());
    err.clear();
    CHECK(!calc_evaluate({num(1), num(2)}, &v, &err));
    CHECK(!err.empty());
    err.clear();
    CHECK(!calc_evaluate({op(K::RParen)}, &v, &err));
    CHECK(!err.empty());
    err.clear();
    CHECK(!calc_evaluate({op(K::LParen), num(1)}, &v, &err));
    CHECK(!err.empty());
    err.clear();
    CHECK(!calc_evaluate({op(K::LParen), op(K::RParen)}, &v, &err));
    CHECK(!err.empty());
}

TEST(calc_tool_end_to_end) {
    TempDir d;
    ToolContext ctx{Workspace(d.path())};
    CalculatorTool calc;
    ToolResult r = calc.invoke(args1("expression", JsonValue::string("(12 + 30) * 2 / 7")), ctx);
    CHECK(r.ok);
    CHECK(r.content == "12");
    ToolResult z = calc.invoke(args1("expression", JsonValue::string("1/0")), ctx);
    CHECK(!z.ok);
    CHECK(z.error == "division by zero");
}

int main(int argc, char** argv) { return mt::run_all(argc, argv); }
