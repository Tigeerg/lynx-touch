// core/emit_moves.cpp - the words of one move and the block loop (7.1).
//
// The rule of the file: a word is written only when its value changes, so a
// strip line does not repeat the Y and the Z of the line before. A block that
// would carry no coordinates and no feed is a move to the point the tool
// already stands at: it is dropped instead of becoming a bare "N100 G0".
//
// An arc is G3 with X/Y/Z where the end point differs, I/J of the move and the
// feed where it changes. A full turn is never written without an end point: the
// viewer drops such an arc and borrows U/V from a neighbour, so the planner
// writes half arcs and the emitter has no full-circle branch.
#include "core/emit_internal.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <string>

namespace qp {
namespace {

// A coordinate that differs from the modal value by less than the print
// resolution is the same coordinate: three digits after the point.
constexpr double kEps = 1e-6;

struct Modal {
    bool hasX = false;
    bool hasY = false;
    bool hasZ = false;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    // The feed is modal across operations too: it comes from EmitState and
    // goes back there, so the next operation does not repeat an unchanged F.
    bool hasFeed = false;
    double feed = 0.0;
    bool hasMotion = false;
    // The last motion word: 0, 1, 2 or 3. An arc leaves 2/3 modal as well, so
    // a following G2/G3 of the same turn does not repeat the G word.
    int motion = -1;
};

std::string axisWord(const char letter, double value, bool& has, double& stored) {
    if (has && std::fabs(value - stored) <= kEps) { return std::string(); }
    has = true;
    stored = value;
    return std::string(1, letter) + coord(value);
}

// I and J are not modal: both words are written on every arc block, because a
// control without the words of an arc has no centre for it.
std::string offsetWord(const char letter, double value) {
    return std::string(1, letter) + coord(value);
}

void appendWord(std::string& words, const std::string& word) {
    if (word.empty()) { return; }
    words += words.empty() ? word : " " + word;
}

std::string moveWords(Modal& modal, const Move& move) {
    std::string words;
    const int g = move.hasArc ? (move.arc.cw ? 2 : 3) : (move.kind == MoveKind::Cut ? 1 : 0);
    if (!modal.hasMotion || modal.motion != g) {
        words = "G" + std::to_string(g);
        modal.hasMotion = true;
        modal.motion = g;
    }
    const std::initializer_list<std::string> axes = {
        axisWord('X', move.to.x, modal.hasX, modal.x),
        axisWord('Y', move.to.y, modal.hasY, modal.y),
        axisWord('Z', move.to.z, modal.hasZ, modal.z)};
    for (const std::string& word : axes) { appendWord(words, word); }
    if (move.hasArc) {
        appendWord(words, offsetWord('I', move.arc.i));
        appendWord(words, offsetWord('J', move.arc.j));
    }
    // A feed word travels only with a cut and only when it changes: writing F
    // on a rapid would change the modal feed by accident.
    if (move.kind == MoveKind::Cut && move.hasFeed &&
        (!modal.hasFeed || std::fabs(move.feedMmMin - modal.feed) > kEps)) {
        appendWord(words, "F" + feedWord(move.feedMmMin));
        modal.hasFeed = true;
        modal.feed = move.feedMmMin;
    }
    return words;
}

// NaN never equals anything, including itself: an unknown coordinate compares
// false, so the virtual parking point never swallows the first real value.
bool samePoint(const Point3& a, const Point3& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

} // namespace

// The tag body number: "%g" with a trailing dot ("XMIN=-20.", "D=10."), the
// spelling of the reference files. The dot is kept so that the byte-for-byte
// goldens of later slices compare against the reference format.
std::string num(double value) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%g", value);
    std::string out(buf);
    const bool hasExponent = out.find('e') != std::string::npos;
    const bool isFinite = out.find("inf") == std::string::npos && out.find("nan") == std::string::npos;
    if (out.find('.') == std::string::npos && !hasExponent && isFinite) { out += '.'; }
    return out;
}

std::string coord(double value) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.3f", value);
    return std::string(buf);
}

std::string numberedBlock(int sequence, const std::string& words) {
    return "N" + std::to_string(sequence) + " " + words;
}

std::string feedWord(double value) {
    char buf[40];
    if (std::fabs(value - std::round(value)) < kEps) {
        std::snprintf(buf, sizeof(buf), "%.0f", value);
    } else {
        std::snprintf(buf, sizeof(buf), "%.3f", value);
    }
    return std::string(buf);
}

void emitMoves(std::string& out, const Path& path, std::size_t first, std::size_t last,
               int& sequence, const Point3& park, EmitState& state) {
    // The header parks the tool at safe Z and says nothing about X and Y: only
    // Z is known to the modal state at the start of a block. The motion mode is
    // not known either: M5 ended the previous operation, so the first move of
    // this one writes its own G0/G1.
    Modal modal;
    modal.hasZ = true;
    modal.z = park.z;
    modal.hasFeed = state.hasFeed;
    modal.feed = state.feed;
    Point3 previous = park;
    for (std::size_t i = first; i < last; ++i) {
        const Move& move = path.moves[i];
        if (samePoint(previous, move.to)) { continue; }
        const std::string words = moveWords(modal, move);
        if (words.empty()) { continue; }
        out += numberedBlock(sequence, words) + "\n";
        sequence += kSequenceStep;
        previous = move.to;
    }
    state.hasFeed = modal.hasFeed;
    state.feed = modal.feed;
}

void emitCycle(std::string& out, const std::vector<Point3>& holes, const DrillCycle& cycle,
               int& sequence, EmitState& state) {
    if (holes.empty()) { return; }
    Modal modal;
    modal.hasFeed = state.hasFeed;
    modal.feed = state.feed;
    // The cycle word carries the depth and the planes on its first block; the
    // following blocks carry only X/Y, as the controls expect of a canned
    // cycle. Q belongs to the G83 block; the simple cycle has no Q at all.
    bool firstHole = true;
    for (const Point3& hole : holes) {
        std::string words;
        if (firstHole) {
            words = cycle.peck ? "G83" : "G81";
            const bool sameZ = modal.hasZ && std::fabs(hole.z - modal.z) <= kEps;
            words += " X" + coord(hole.x) + " Y" + coord(hole.y);
            if (!sameZ) { words += " Z" + coord(cycle.depthZ); }
            words += " R" + coord(cycle.rZ);
            if (cycle.peck) { words += " Q" + coord(cycle.peckMm); }
            if (!modal.hasFeed || std::fabs(cycle.feedMmMin - modal.feed) > kEps) {
                words += " F" + feedWord(cycle.feedMmMin);
            }
            modal.hasZ = true;
            modal.z = cycle.depthZ;
            modal.hasFeed = true;
            modal.feed = cycle.feedMmMin;
            firstHole = false;
        } else {
            words = "X" + coord(hole.x) + " Y" + coord(hole.y);
        }
        out += numberedBlock(sequence, words) + "\n";
        sequence += kSequenceStep;
    }
    out += numberedBlock(sequence, "G80") + "\n";
    sequence += kSequenceStep;
    state.hasFeed = modal.hasFeed;
    state.feed = modal.feed;
}
} // namespace qp
