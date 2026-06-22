#pragma once
#include <string>
#include <vector>
#include <utility>
namespace raw {
// The shared witnessed form. JSON shape is byte-shape-compatible with
// coherence-membrane's Certificate.to_dict(): {claim, verdict, oracle, evidence}.
enum class Verdict { Verified, Refuted, Unverifiable };
const char* verdict_str(Verdict v);   // "verified" | "refuted" | "unverifiable"
struct Certificate {
    std::string claim;
    Verdict verdict;
    std::string oracle;                                       // e.g. "raw-rt-ao-v1"
    std::vector<std::pair<std::string, std::string>> evidence; // ordered (key,value) pairs
};
std::string to_json(const Certificate& c);
}
