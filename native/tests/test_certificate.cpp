#include "raw/certificate.hpp"
#include "check.hpp"
#include <string>
using namespace raw;
int main() {
    // verdict_str maps to the exact lowercase strings the Python spine uses
    CHECK(std::string(verdict_str(Verdict::Verified)) == "verified");
    CHECK(std::string(verdict_str(Verdict::Refuted)) == "refuted");
    CHECK(std::string(verdict_str(Verdict::Unverifiable)) == "unverifiable");

    // canonical shape matches coherence-membrane Certificate.to_dict()
    Certificate c{"(A -> A)", Verdict::Verified, "raw-rt-ao-v1",
                  {{"valid", "ground truth matched"}}};
    CHECK(to_json(c) ==
        "{\"claim\":\"(A -> A)\",\"verdict\":\"verified\",\"oracle\":\"raw-rt-ao-v1\","
        "\"evidence\":[[\"valid\",\"ground truth matched\"]]}");

    // empty evidence -> []
    Certificate e{"x", Verdict::Unverifiable, "o", {}};
    CHECK(to_json(e) ==
        "{\"claim\":\"x\",\"verdict\":\"unverifiable\",\"oracle\":\"o\",\"evidence\":[]}");

    // string escaping: a quote and a backslash stay valid JSON
    Certificate q{"a\"b\\c", Verdict::Refuted, "o", {}};
    CHECK(to_json(q).find("\"claim\":\"a\\\"b\\\\c\"") != std::string::npos);

    // multiple evidence pairs preserve order
    Certificate m{"m", Verdict::Verified, "o", {{"k1","v1"},{"k2","v2"}}};
    CHECK(to_json(m).find("\"evidence\":[[\"k1\",\"v1\"],[\"k2\",\"v2\"]]") != std::string::npos);
    return raw_test_summary();
}
