// tests/import_graph.h - the import graph of the demo sources.
//
// The first demo slice once had a circle core -> cli -> core: the C++ linker
// stayed quiet, because the circle ran through constexpr tables and inline
// variables and only the separate parser build caught it. This engine turns
// the tree into a machine graph: every .h/.cpp of core/, cli/ and main.cpp is
// a node, every quoted include of a repository path is a raw edge, and the
// resolved "core/..."/"cli/..." quotes are the adjacency. checks_imports.cpp
// holds the cases: no circles, no layer edges.
//
// The tests/ tree is excluded on purpose: the cases include the whole demo and
// would form edges the product does not have. Includes of modules/** (the
// viewer core) have no node: they are kept raw, so a layer rule can name them.
#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <system_error>
#include <vector>

namespace import_graph {

namespace fs = std::filesystem;

// The first path segments of a quote that point into the repository tree. The
// demo core and console, the viewer module and the sources of the products.
// A quote that starts with one of them is a path of this repository, even
// when the file is outside the demo, and a layer rule can name it.
bool isProjectPath(const std::string& path) {
    for (const char* prefix : {"core/", "cli/", "modules/", "apps/"}) {
        if (path.rfind(prefix, 0) == 0) {
            return true;
        }
    }
    return false;
}

// The headers of the viewer core are quoted as "core/<name>.h": the build puts
// modules/gcode-viewer/cpp on the search path, so the same "core/" prefix means
// the viewer for those names. The list is the header set of the viewer core; a
// new viewer header that the demo quotes is added here with it.
bool inViewer(const std::string& path) {
    static const char* const kViewerHeaders[] = {
        "core/collision.h", "core/gcode_parser.h", "core/gv_types.h",
        "core/normals.h", "core/np_helpers.h", "core/outline.h",
        "core/packing.h", "core/path_prep.h", "core/quality.h",
        "core/silhouette.h", "core/stl_import.h", "core/stock_sim.h",
        "core/tangents.h", "core/text_util.h", "core/tooling.h",
        "core/ui_tags.h",
    };
    for (const char* name : kViewerHeaders) {
        if (path == name) {
            return true;
        }
    }
    return false;
}

struct Graph {
    std::vector<std::string> nodes;              // project-relative paths, "core/ops.h"
    std::map<std::string, std::size_t> index;    // path -> node id
    std::vector<std::vector<std::size_t>> out;   // adjacency by node id
    std::vector<std::vector<std::string>> raw;   // every quoted target, resolved or not
};

// True when the line is a quoted include. `path` is the target as written;
// whether it is an edge of this project is decided against the node table, so
// an include of the viewer headers is not lost.
bool quotedInclude(const std::string& line, std::string& path) {
    const std::size_t at = line.find("#include");
    if (at == std::string::npos) {
        return false;
    }
    const std::size_t q1 = line.find('"', at);
    if (q1 == std::string::npos) {
        return false;
    }
    const std::size_t q2 = line.find('"', q1 + 1);
    if (q2 == std::string::npos) {
        return false;
    }
    path = line.substr(q1 + 1, q2 - q1 - 1);
    return true;
}

// The demo sources: every .h/.cpp directly under core/ and cli/ and main.cpp.
// A nested directory would be missed; the demo keeps its files flat and this
// loop says so by not walking deeper.
std::vector<std::string> listSources(const fs::path& root) {
    std::vector<std::string> out;
    for (const char* dir : {"core", "cli"}) {
        std::error_code ec;
        for (const fs::directory_entry& entry : fs::directory_iterator(root / "apps" / "quickprog" / dir, ec)) {
            const std::string name = entry.path().filename().string();
            const std::string ext = entry.path().extension().string();
            if (entry.is_regular_file() && (ext == ".h" || ext == ".cpp")) {
                out.push_back(dir + std::string("/") + name);
            }
        }
    }
    std::error_code ec;
    if (fs::is_regular_file(root / "apps" / "quickprog" / "main.cpp", ec)) {
        out.push_back("main.cpp");
    }
    std::sort(out.begin(), out.end());
    return out;
}

// The node table and the edges of the demo tree. `externalEdges` counts the
// project quotes whose file is absent: a broken include must be visible.
Graph build(const fs::path& root, std::size_t& externalEdges) {
    Graph graph;
    graph.nodes = listSources(root);
    for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
        graph.index[graph.nodes[i]] = i;
    }
    graph.out.assign(graph.nodes.size(), {});
    graph.raw.assign(graph.nodes.size(), {});
    externalEdges = 0;
    const fs::path app = root / "apps" / "quickprog";
    for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
        std::ifstream fh(app / graph.nodes[i], std::ios::binary);
        if (!fh) {
            continue;
        }
        std::string line;
        while (std::getline(fh, line)) {
            std::string target;
            if (!quotedInclude(line, target)) {
                continue;
            }
            const auto to = graph.index.find(target);
            if (graph.nodes[i].rfind("cli/", 0) == 0 && target.rfind("modules/", 0) == 0) {
                // The console must not reach the viewer. The check target has
                // modules/ on its search path, so the quotes are kept raw and a
                // layer rule names the pair even when the product would fail to
                // compile it: the graph case fails first and says what is wrong.
                graph.raw[i].push_back(target);
                continue;
            }
            if (to != graph.index.end()) {
                graph.out[i].push_back(to->second);
            }
            if (isProjectPath(target)) {
                graph.raw[i].push_back(target);
                if (to == graph.index.end() && !inViewer(target)) {
                    // A project include whose file is absent: the compiler
                    // would fail, the graph must not lose sight of it.
                    ++externalEdges;
                }
            }
        }
        std::sort(graph.out[i].begin(), graph.out[i].end());
        graph.out[i].erase(std::unique(graph.out[i].begin(), graph.out[i].end()), graph.out[i].end());
    }
    return graph;
}

std::size_t edgeCount(const Graph& graph) {
    std::size_t total = 0;
    for (const std::vector<std::size_t>& list : graph.out) {
        total += list.size();
    }
    return total;
}

// Depth-first walk with three colours: a grey node met again closes a circle.
// The helper is the colour table shared by the walk, so the recursion stays a
// member of the unordered policy of the tests, not a global state.
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

} // namespace import_graph
