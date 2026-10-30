// tests/checks_imports.cpp - the cases over the demo import graph.
//
// The graph itself (nodes, edges, search) is built by tests/import_graph.h;
// this file holds the rules a machine enforces:
//   * the graph is not empty - the parser sees the real includes;
//   * every project quote points at an existing file;
//   * no circle (depth-first walk with three colours);
//   * core/ does not include cli/ and cli/ does not include modules/.
//
// A found circle prints the chain "a.h -> b.cpp -> a.h" and returns FAIL. The
// case reuses version_checks::findRoot, so the walk to projects.json is written
// once. Outside the repository the case prints SKIP, not a failure.
#include <cstdio>
#include <filesystem>
#include <string>

#include "tests/checks.h"
#include "tests/harness.h"
#include "tests/import_graph.h"

namespace import_checks {

namespace {

using import_graph::Graph;

// The demo has 66 nodes and 138 project edges on this slice (a duplicated
// include is one edge). The floor sits below the fact but far above zero: a
// parser that stops seeing includes fails the count case, an empty graph never
// passes it.
constexpr std::size_t kMinEdges = 100;

// Depth-first walk with three colours: a grey node met again closes a circle.
class CircleWalker {
public:
    explicit CircleWalker(const Graph& graph) : graph_(graph), colour_(graph.out.size(), 0) {}

    // The first circle as "a.h -> b.cpp -> a.h"; empty when there is none.
    std::string find() {
        for (std::size_t i = 0; i < graph_.out.size(); ++i) {
            if (colour_[i] == 0 && visit(i)) {
                return circle_;
            }
        }
        return {};
    }

private:
    const Graph& graph_;
    std::vector<int> colour_;   // 0 white, 1 grey (on the stack), 2 black
    std::vector<std::size_t> stack_;
    std::string circle_;

    bool visit(std::size_t node) {
        colour_[node] = 1;
        stack_.push_back(node);
        for (std::size_t next : graph_.out[node]) {
            if (colour_[next] == 1) {
                circle_ = describe(next);
                return true;
            }
            if (colour_[next] == 0 && visit(next)) {
                return true;
            }
        }
        stack_.pop_back();
        colour_[node] = 2;
        return false;
    }

    // The chain from the grey node on the stack to the current top, then back
    // to the grey node: "a.h -> b.cpp -> a.h".
    std::string describe(std::size_t closing) const {
        std::string out;
        std::size_t from = 0;
        while (from < stack_.size() && stack_[from] != closing) {
            ++from;
        }
        for (std::size_t i = from; i < stack_.size(); ++i) {
            out += graph_.nodes[stack_[i]];
            out += " -> ";
        }
        out += graph_.nodes[closing];
        return out;
    }
};

// A layer edge that must not exist: the source layer does not include the
// target layer. The rule names the direction, the meaning and the case id.
struct LayerRule {
    const char* from;     // "core/"
    const char* to;       // "cli/"
    const char* text;     // the reason for the report
    const char* id;       // the case id of the console output
};

const LayerRule kLayerRules[] = {
    {"core/", "cli/", "ядро не включает консоль", "imports.core_without_cli"},
    {"cli/", "modules/", "консоль не включает вьювер", "imports.cli_without_modules"},
};

// The offending pairs of one rule as "core/x.cpp -> cli/app.h". The target may
// be outside the demo, so the raw quotes are read, not only the resolved edges.
// A viewer header is quoted as "core/<name>.h" (the build puts the viewer core
// on the search path under the same prefix), so the rule over modules/ also
// names those quotes as the viewer file.
std::string layerViolations(const Graph& graph, const LayerRule& rule) {
    const bool toModules = std::string(rule.to) == "modules/";
    std::string out;
    for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
        if (graph.nodes[i].rfind(rule.from, 0) != 0) {
            continue;
        }
        for (const std::string& target : graph.raw[i]) {
            const bool viewer = import_graph::inViewer(target);
            const bool bad = target.rfind(rule.to, 0) == 0 || (toModules && viewer);
            if (!bad) {
                continue;
            }
            if (!out.empty()) {
                out += ", ";
            }
            out += graph.nodes[i] + " -> "
                + (viewer ? "modules/gcode-viewer/cpp/" + target : target);
        }
    }
    return out;
}

} // namespace

void runImportChecks() {
    const std::filesystem::path root = version_checks::findRoot();
    if (root.empty()) {
        std::printf("SKIP imports.graph: каталог apps/quickprog выше каталога проверки не найден\n");
        return;
    }
    std::size_t unresolved = 0;
    const Graph graph = import_graph::build(root, unresolved);
    const std::size_t edges = import_graph::edgeCount(graph);
    const std::string counts = "узлов " + std::to_string(graph.nodes.size()) + ", рёбер "
        + std::to_string(edges);

    // The layer rules come before the counts: a console include of the viewer
    // is not an edge of the adjacency and must be reported by the rule, not
    // pass as a smaller graph.
    for (const LayerRule& rule : kLayerRules) {
        const std::string bad = layerViolations(graph, rule);
        qpcheck::check(bad.empty(), rule.id, std::string(rule.text) + ": " + bad);
    }

    qpcheck::check(graph.nodes.size() > 20 && edges > kMinEdges,
                   "imports.graph_nonempty",
                   "граф пуст или мал: " + counts + ", ждём рёбер > "
                       + std::to_string(kMinEdges));
    qpcheck::check(unresolved == 0, "imports.targets_exist",
                   "проектных включений с отсутствующим файлом: " + std::to_string(unresolved));

    const std::string circle = CircleWalker(graph).find();
    qpcheck::check(circle.empty(), "imports.no_cycles", "круг импортов: " + circle);
}

} // namespace import_checks
