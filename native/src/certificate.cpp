#include "raw/certificate.hpp"
#include <cstdio>
namespace raw {
const char* verdict_str(Verdict v){
    switch (v){
        case Verdict::Verified:     return "verified";
        case Verdict::Refuted:      return "refuted";
        case Verdict::Unverifiable: return "unverifiable";
    }
    return "unverifiable";
}
// Emit a JSON string literal (with surrounding quotes), escaping per RFC 8259.
static std::string jstr(const std::string& s){
    std::string o = "\"";
    for (unsigned char ch : s){
        switch (ch){
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default:
                if (ch < 0x20){ char b[8]; std::snprintf(b, sizeof b, "\\u%04x", ch); o += b; }
                else o += static_cast<char>(ch);
        }
    }
    o += "\"";
    return o;
}
std::string to_json(const Certificate& c){
    std::string o = "{";
    o += "\"claim\":"   + jstr(c.claim)                 + ",";
    o += "\"verdict\":" + jstr(verdict_str(c.verdict))  + ",";
    o += "\"oracle\":"  + jstr(c.oracle)                + ",";
    o += "\"evidence\":[";
    for (std::size_t i = 0; i < c.evidence.size(); ++i){
        if (i) o += ",";
        o += "[" + jstr(c.evidence[i].first) + "," + jstr(c.evidence[i].second) + "]";
    }
    o += "]}";
    return o;
}
}
