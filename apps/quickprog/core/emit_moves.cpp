// core/emit_moves.cpp - the words of one move and the block loop (7.1).
//
// The rule of the file: a word is written only when its value changes, so a
// strip line does not repeat the Y and the Z of the line before. A block that
// would carry no coordinates and no feed is a move to the point the tool
// already stands at: it is dropped instead of becoming a bare "N100 G0".
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

// Feed: an integer when the value is whole ("F860"), else up to three digits.
std::string feedWord(double value) {
    char buf[40];
    if (std::fabs(value - std::round(value)) < kEps) {
        std::snprintf(buf, sizeof(buf), "%.0f", value);
    } else {
        std::snprintf(buf, sizeof(buf), "%.3f", value);
    }
    return std::string(buf);
}

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
    int motion = -1;
};

std::string axisWord(const char letter, double value, bool& has, double& stored) {
    if (has && std::fabs(value - stored) <= kEps) { return std::string(); }
    has = true;
    stored = value;
    return std::string(1, letter) + coord(value);
}

std::string moveWords(Modal& modal, const Move& move) {
    std::string words;
    const int g = move.kind == MoveKind::Cut ? 1 : 0;
    if (!modal.hasMotion || modal.motion != g) {
        words = "G" + std::to_string(g);
        modal.hasMotion = true;
        modal.motion = g;
    }
    const std::initializer_list<std::string> axes = {
        axisWord('X', move.to.x, modal.hasX, modal.x),
        axisWord('Y', move.to.y, modal.hasY, modal.y),
        axisWord('Z', move.to.z, modal.hasZ, modal.z)};
    for (const std::string& word : axes) {
        if (word.empty()) { continue; }
        words += words.empty() ? word : " " + word;
    }
    // A feed word travels only with a cut and only when it changes: writing F
    // on a rapid would change the modal feed by accident.
    if (move.kind == MoveKind::Cut && move.hasFeed &&
        (!modal.hasFeed || std::fabs(move.feedMmMin - modal.feed) > kEps)) {
        words += " F" + feedWord(move.feedMmMin);
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

std::string coord(double value) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.3f", value);
    return std::string(buf);
}

std::string numberedBlock(int sequence, const std::string& words) {
    return "N" + std::to_string(sequence) + " " + words;
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

} // namespace qp
