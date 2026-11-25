// RawOverrideRegistry.cpp: Non-NIF / Non-DDS Custom Handcrafted Asset Override & Hot-Reload Registry.
// Original work, MIT.
#include "RawOverrideRegistry.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace raw::assets {
namespace {

constexpr uint64_t kFnvOffset = 14695981039346656037ull;
constexpr uint64_t kFnvPrime  = 1099511628211ull;

uint64_t Fnv1aMix(uint64_t h, const void* data, size_t len) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; ++i) {
        h ^= static_cast<uint64_t>(bytes[i]);
        h *= kFnvPrime;
    }
    return h;
}

std::string ToLowerAscii(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

std::string ExtensionLower(std::string_view path) {
    const size_t slash = path.find_last_of("/\\");
    const size_t dot = path.find_last_of('.');
    if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash)) {
        return {};
    }
    return ToLowerAscii(path.substr(dot));
}

std::string EscapeJson(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(c);
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else if (c == '\t') {
            out += "\\t";
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string FmtFloat(float v) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.6g", static_cast<double>(v));
    return std::string(buf);
}

std::string_view KindToString(OverrideAssetKind k) {
    switch (k) {
        case OverrideAssetKind::Mesh:               return "mesh";
        case OverrideAssetKind::MaterialTextureSet: return "textures";
        case OverrideAssetKind::Combined:           return "combined";
        default:                                    return "combined";
    }
}

bool ParseKindString(std::string_view s, OverrideAssetKind* outKind) {
    const std::string low = ToLowerAscii(s);
    if (low == "mesh") { *outKind = OverrideAssetKind::Mesh; return true; }
    if (low == "textures" || low == "material" || low == "materialtextureset") {
        *outKind = OverrideAssetKind::MaterialTextureSet;
        return true;
    }
    if (low == "combined" || low.empty()) { *outKind = OverrideAssetKind::Combined; return true; }
    return false;
}

bool HasAnyTexturePath(const PbrTextureSetPaths& t) {
    return !t.albedoPath.empty() || !t.normalPath.empty() || !t.rmaohPath.empty() ||
           !t.emissivePath.empty() || !t.heightPath.empty();
}

// ============================================================================
// Minimal Strict Recursive-Descent JSON Parser (Zero External Dependencies)
// ============================================================================
struct JsonValue {
    enum class Type : uint8_t { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::vector<JsonValue> arr;
    std::vector<std::pair<std::string, JsonValue>> obj;

    const JsonValue* Find(std::string_view key) const {
        if (type != Type::Object) return nullptr;
        for (const auto& kv : obj) {
            if (kv.first == key) return &kv.second;
        }
        return nullptr;
    }
};

class JsonParser {
public:
    explicit JsonParser(std::string_view src) : src_(src) {}

    bool ParseRoot(JsonValue* outVal, std::string* outErr) {
        SkipWs();
        if (!ParseValue(outVal, outErr)) return false;
        SkipWs();
        if (pos_ != src_.size()) {
            if (outErr) *outErr = "unexpected trailing characters after JSON root";
            return false;
        }
        return true;
    }

private:
    void SkipWs() {
        while (pos_ < src_.size() && std::isspace(static_cast<unsigned char>(src_[pos_]))) {
            ++pos_;
        }
    }

    bool ParseValue(JsonValue* out, std::string* err) {
        SkipWs();
        if (pos_ >= src_.size()) {
            if (err) *err = "unexpected end of JSON";
            return false;
        }
        const char c = src_[pos_];
        if (c == '{') return ParseObject(out, err);
        if (c == '[') return ParseArray(out, err);
        if (c == '"') {
            out->type = JsonValue::Type::String;
            return ParseString(&out->str, err);
        }
        if (c == 't' && src_.substr(pos_, 4) == "true") {
            pos_ += 4;
            out->type = JsonValue::Type::Bool;
            out->b = true;
            return true;
        }
        if (c == 'f' && src_.substr(pos_, 5) == "false") {
            pos_ += 5;
            out->type = JsonValue::Type::Bool;
            out->b = false;
            return true;
        }
        if (c == 'n' && src_.substr(pos_, 4) == "null") {
            pos_ += 4;
            out->type = JsonValue::Type::Null;
            return true;
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
            return ParseNumber(out, err);
        }
        if (err) *err = std::string("unexpected token in JSON: ") + c;
        return false;
    }

    bool ParseString(std::string* out, std::string* err) {
        if (pos_ >= src_.size() || src_[pos_] != '"') {
            if (err) *err = "expected '\"' in JSON string";
            return false;
        }
        ++pos_;
        out->clear();
        while (pos_ < src_.size()) {
            const char c = src_[pos_++];
            if (c == '"') return true;
            if (c == '\\') {
                if (pos_ >= src_.size()) {
                    if (err) *err = "unterminated escape in JSON string";
                    return false;
                }
                const char esc = src_[pos_++];
                switch (esc) {
                    case '"':  out->push_back('"'); break;
                    case '\\': out->push_back('\\'); break;
                    case '/':  out->push_back('/'); break;
                    case 'n':  out->push_back('\n'); break;
                    case 'r':  out->push_back('\r'); break;
                    case 't':  out->push_back('\t'); break;
                    default:   out->push_back(esc); break;
                }
            } else {
                out->push_back(c);
            }
        }
        if (err) *err = "unterminated JSON string";
        return false;
    }

    bool ParseNumber(JsonValue* out, std::string* err) {
        const size_t start = pos_;
        if (src_[pos_] == '-') ++pos_;
        bool anyDigit = false;
        while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_]))) {
            ++pos_;
            anyDigit = true;
        }
        if (pos_ < src_.size() && src_[pos_] == '.') {
            ++pos_;
            while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_]))) {
                ++pos_;
                anyDigit = true;
            }
        }
        if (pos_ < src_.size() && (src_[pos_] == 'e' || src_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < src_.size() && (src_[pos_] == '+' || src_[pos_] == '-')) ++pos_;
            while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_]))) {
                ++pos_;
            }
        }
        if (!anyDigit) {
            if (err) *err = "invalid JSON number";
            return false;
        }
        const std::string token(src_.substr(start, pos_ - start));
        char* endPtr = nullptr;
        const double val = std::strtod(token.c_str(), &endPtr);
        if (!endPtr || *endPtr != '\0' || !std::isfinite(val)) {
            if (err) *err = "failed to parse JSON number: " + token;
            return false;
        }
        out->type = JsonValue::Type::Number;
        out->num = val;
        return true;
    }

    bool ParseArray(JsonValue* out, std::string* err) {
        ++pos_;  // '['
        out->type = JsonValue::Type::Array;
        out->arr.clear();
        SkipWs();
        if (pos_ < src_.size() && src_[pos_] == ']') {
            ++pos_;
            return true;
        }
        while (pos_ < src_.size()) {
            JsonValue elem;
            if (!ParseValue(&elem, err)) return false;
            out->arr.push_back(std::move(elem));
            SkipWs();
            if (pos_ < src_.size() && src_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (pos_ < src_.size() && src_[pos_] == ']') {
                ++pos_;
                return true;
            }
            if (err) *err = "expected ',' or ']' in JSON array";
            return false;
        }
        if (err) *err = "unterminated JSON array";
        return false;
    }

    bool ParseObject(JsonValue* out, std::string* err) {
        ++pos_;  // '{'
        out->type = JsonValue::Type::Object;
        out->obj.clear();
        SkipWs();
        if (pos_ < src_.size() && src_[pos_] == '}') {
            ++pos_;
            return true;
        }
        while (pos_ < src_.size()) {
            SkipWs();
            std::string key;
            if (!ParseString(&key, err)) return false;
            SkipWs();
            if (pos_ >= src_.size() || src_[pos_] != ':') {
                if (err) *err = "expected ':' after JSON object key";
                return false;
            }
            ++pos_;
            JsonValue val;
            if (!ParseValue(&val, err)) return false;
            out->obj.emplace_back(std::move(key), std::move(val));
            SkipWs();
            if (pos_ < src_.size() && src_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (pos_ < src_.size() && src_[pos_] == '}') {
                ++pos_;
                return true;
            }
            if (err) *err = "expected ',' or '}' in JSON object";
            return false;
        }
        if (err) *err = "unterminated JSON object";
        return false;
    }

    std::string_view src_;
    size_t pos_ = 0;
};

uint64_t StampOneFile(const std::string& relOrAbs, std::string_view baseDir) {
    if (relOrAbs.empty()) return 0ull;
    std::filesystem::path p(relOrAbs);
    if (p.is_relative() && !baseDir.empty()) {
        p = std::filesystem::path(baseDir) / p;
    }
    std::error_code ec;
    if (!std::filesystem::exists(p, ec) || ec) return 0ull;
    const auto sz = std::filesystem::file_size(p, ec);
    if (ec) return 0ull;
    const auto wt = std::filesystem::last_write_time(p, ec);
    if (ec) return 0ull;
    const auto ticks = static_cast<int64_t>(wt.time_since_epoch().count());
    uint64_t h = kFnvOffset;
    h = Fnv1aMix(h, &sz, sizeof sz);
    h = Fnv1aMix(h, &ticks, sizeof ticks);
    return h;
}

bool TryParseFormId(std::string_view s, uint32_t* outId) {
    if (s.empty()) return false;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        uint32_t val = 0;
        for (size_t i = 2; i < s.size(); ++i) {
            const char c = s[i];
            uint32_t d = 0;
            if (c >= '0' && c <= '9') d = static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') d = static_cast<uint32_t>(10 + c - 'a');
            else if (c >= 'A' && c <= 'F') d = static_cast<uint32_t>(10 + c - 'A');
            else return false;
            val = (val << 4u) | d;
        }
        *outId = val;
        return val != 0u;
    }
    return false;
}

}  // namespace

std::string NormalizeVirtualPath(std::string_view rawPath) {
    std::string s;
    s.reserve(rawPath.size());
    bool prevSlash = false;
    for (char c : rawPath) {
        char n = (c == '\\') ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (n == '/') {
            if (prevSlash) continue;
            prevSlash = true;
        } else {
            prevSlash = false;
        }
        s.push_back(n);
    }
    while (s.size() >= 2 && s[0] == '.' && s[1] == '/') s.erase(0, 2);
    while (!s.empty() && s.front() == '/') s.erase(0, 1);
    if (s.rfind("data/", 0) == 0) s.erase(0, 5);
    return s;
}

uint64_t HashVirtualPath64(std::string_view virtualPath) {
    const std::string norm = NormalizeVirtualPath(virtualPath);
    if (norm.empty()) return 0ull;
    return Fnv1aMix(kFnvOffset, norm.data(), norm.size());
}

bool IsAllowedModernMeshExtension(std::string_view path) {
    const std::string ext = ExtensionLower(path);
    return ext == ".glb" || ext == ".gltf" || ext == ".obj";
}

bool IsAllowedModernTextureExtension(std::string_view path) {
    const std::string ext = ExtensionLower(path);
    return ext == ".png" || ext == ".tga" || ext == ".tif" || ext == ".tiff" ||
           ext == ".hdr" || ext == ".exr" || ext == ".ktx2";
}

bool ValidateOverrideEntry(const CustomAssetOverride& entry, std::string* outErr) {
    auto fail = [&](const char* msg) -> bool {
        if (outErr) *outErr = msg;
        return false;
    };
    if (entry.vanillaPath.empty() && entry.formId == 0u) {
        return fail("override must specify vanillaPath or non-zero formId");
    }
    if (!std::isfinite(entry.scale) || entry.scale <= 0.0f || entry.scale > 1000.0f) {
        return fail("override scale must be finite and in (0, 1000]");
    }
    const bool hasMesh = !entry.meshPath.empty();
    const bool hasTex = HasAnyTexturePath(entry.textures);
    if (!hasMesh && !hasTex) {
        return fail("override must provide at least one modern mesh or PBR texture path");
    }
    if (hasMesh && !IsAllowedModernMeshExtension(entry.meshPath)) {
        return fail("meshPath must use a modern non-.nif format (.glb, .gltf, .obj)");
    }
    auto checkTex = [&](const std::string& p, const char* label) -> bool {
        if (!p.empty() && !IsAllowedModernTextureExtension(p)) {
            if (outErr) *outErr = std::string(label) + " must use a modern non-.dds format (.png/.tga/.tif/.hdr/.exr/.ktx2)";
            return false;
        }
        return true;
    };
    if (!checkTex(entry.textures.albedoPath,   "albedoPath"))   return false;
    if (!checkTex(entry.textures.normalPath,   "normalPath"))   return false;
    if (!checkTex(entry.textures.rmaohPath,    "rmaohPath"))    return false;
    if (!checkTex(entry.textures.emissivePath, "emissivePath")) return false;
    if (!checkTex(entry.textures.heightPath,   "heightPath"))   return false;
    return true;
}

uint64_t ComputeOverrideFilesFingerprint(const CustomAssetOverride& entry, std::string_view baseDir) {
    uint64_t h = kFnvOffset;
    const std::string* paths[6] = {
        &entry.meshPath,
        &entry.textures.albedoPath,
        &entry.textures.normalPath,
        &entry.textures.rmaohPath,
        &entry.textures.emissivePath,
        &entry.textures.heightPath
    };
    for (const std::string* p : paths) {
        if (!p->empty()) {
            const uint64_t s = StampOneFile(*p, baseDir);
            h = Fnv1aMix(h, &s, sizeof s);
            h = Fnv1aMix(h, p->data(), p->size());
        }
    }
    return h;
}

void OverrideRegistry::Clear() {
    entries_.clear();
    byPathHash_.clear();
    byFormId_.clear();
    lastMirrorRoot_.clear();
}

void OverrideRegistry::RebuildIndices() {
    byPathHash_.clear();
    byFormId_.clear();
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].pathHash != 0ull) {
            byPathHash_[entries_[i].pathHash] = i;
        }
        if (entries_[i].formId != 0u) {
            byFormId_[entries_[i].formId] = i;
        }
    }
}

bool OverrideRegistry::RegisterOverride(CustomAssetOverride entry, std::string* outErr) {
    if (!entry.vanillaPath.empty()) {
        entry.vanillaPath = NormalizeVirtualPath(entry.vanillaPath);
        entry.pathHash = HashVirtualPath64(entry.vanillaPath);
    } else {
        entry.pathHash = 0ull;
    }
    if (!ValidateOverrideEntry(entry, outErr)) {
        return false;
    }

    // Update existing entry if matching pathHash or formId
    if (entry.pathHash != 0ull) {
        auto it = byPathHash_.find(entry.pathHash);
        if (it != byPathHash_.end()) {
            entries_[it->second] = std::move(entry);
            RebuildIndices();
            return true;
        }
    }
    if (entry.formId != 0u) {
        auto it = byFormId_.find(entry.formId);
        if (it != byFormId_.end()) {
            entries_[it->second] = std::move(entry);
            RebuildIndices();
            return true;
        }
    }
    entries_.push_back(std::move(entry));
    RebuildIndices();
    return true;
}

bool OverrideRegistry::ParseManifestJsonString(std::string_view jsonText, std::string_view baseDir,
                                               std::string* outErr) {
    JsonParser parser(jsonText);
    JsonValue root;
    if (!parser.ParseRoot(&root, outErr)) {
        return false;
    }
    if (root.type != JsonValue::Type::Object) {
        if (outErr) *outErr = "manifest root must be a JSON object";
        return false;
    }
    const JsonValue* schema = root.Find("schema");
    if (!schema || schema->type != JsonValue::Type::String || schema->str != "raw.overrides.v1") {
        if (outErr) *outErr = "manifest schema must be \"raw.overrides.v1\"";
        return false;
    }
    const JsonValue* list = root.Find("overrides");
    if (!list || list->type != JsonValue::Type::Array) {
        if (outErr) *outErr = "manifest must contain an \"overrides\" array";
        return false;
    }

    std::vector<CustomAssetOverride> parsed;
    parsed.reserve(list->arr.size());
    std::unordered_set<uint64_t> seenHashes;

    for (const auto& item : list->arr) {
        if (item.type != JsonValue::Type::Object) {
            if (outErr) *outErr = "override element must be a JSON object";
            return false;
        }
        CustomAssetOverride ov{};
        if (const auto* vp = item.Find("vanillaPath"); vp && vp->type == JsonValue::Type::String) {
            ov.vanillaPath = NormalizeVirtualPath(vp->str);
            ov.pathHash = HashVirtualPath64(ov.vanillaPath);
        }
        if (const auto* fid = item.Find("formId")) {
            if (fid->type == JsonValue::Type::Number && fid->num >= 0.0) {
                ov.formId = static_cast<uint32_t>(fid->num);
            } else if (fid->type == JsonValue::Type::String) {
                if (!TryParseFormId(fid->str, &ov.formId)) {
                    if (outErr) *outErr = "invalid hex formId string: " + fid->str;
                    return false;
                }
            }
        }
        if (const auto* k = item.Find("kind"); k && k->type == JsonValue::Type::String) {
            if (!ParseKindString(k->str, &ov.kind)) {
                if (outErr) *outErr = "unknown override kind: " + k->str;
                return false;
            }
        }
        if (const auto* mp = item.Find("meshPath"); mp && mp->type == JsonValue::Type::String) {
            ov.meshPath = mp->str;
        }
        if (const auto* tex = item.Find("textures"); tex && tex->type == JsonValue::Type::Object) {
            if (const auto* a = tex->Find("albedo");   a && a->type == JsonValue::Type::String) ov.textures.albedoPath = a->str;
            if (const auto* n = tex->Find("normal");   n && n->type == JsonValue::Type::String) ov.textures.normalPath = n->str;
            if (const auto* r = tex->Find("rmaoh");    r && r->type == JsonValue::Type::String) ov.textures.rmaohPath = r->str;
            if (const auto* e = tex->Find("emissive"); e && e->type == JsonValue::Type::String) ov.textures.emissivePath = e->str;
            if (const auto* h = tex->Find("height");   h && h->type == JsonValue::Type::String) ov.textures.heightPath = h->str;
        }
        if (const auto* mat = item.Find("materialPreset"); mat && mat->type == JsonValue::Type::String) {
            ov.materialPreset = mat->str;
        }
        if (const auto* sc = item.Find("scale"); sc && sc->type == JsonValue::Type::Number) {
            ov.scale = static_cast<float>(sc->num);
        }
        if (const auto* yup = item.Find("yUpToZUp"); yup && yup->type == JsonValue::Type::Bool) {
            ov.yUpToZUp = yup->b;
        }
        if (const auto* au = item.Find("author"); au && au->type == JsonValue::Type::String) {
            ov.author = au->str;
        }
        if (const auto* rv = item.Find("revision"); rv && rv->type == JsonValue::Type::String) {
            ov.revision = rv->str;
        }

        if (!ValidateOverrideEntry(ov, outErr)) {
            return false;
        }
        if (ov.pathHash != 0ull && !seenHashes.insert(ov.pathHash).second) {
            if (outErr) *outErr = "duplicate vanillaPath in manifest: " + ov.vanillaPath;
            return false;
        }
        ov.fileFingerprint = ComputeOverrideFilesFingerprint(ov, baseDir);
        parsed.push_back(std::move(ov));
    }

    for (auto& ov : parsed) {
        if (!RegisterOverride(std::move(ov), outErr)) return false;
    }
    return true;
}

bool OverrideRegistry::LoadManifestFile(const std::string& manifestPath, std::string* outErr) {
    std::ifstream f(manifestPath, std::ios::binary);
    if (!f) {
        if (outErr) *outErr = "failed to open manifest file: " + manifestPath;
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    const std::filesystem::path dir = std::filesystem::path(manifestPath).parent_path();
    return ParseManifestJsonString(ss.str(), dir.string(), outErr);
}

std::string OverrideRegistry::ExportManifestJson() const {
    std::ostringstream os;
    os << "{\"schema\":\"raw.overrides.v1\",\"count\":" << entries_.size() << ",\"overrides\":[";
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (i > 0) os << ",";
        const auto& e = entries_[i];
        os << "{"
           << "\"vanillaPath\":\"" << EscapeJson(e.vanillaPath) << "\","
           << "\"formId\":" << e.formId << ","
           << "\"kind\":\"" << KindToString(e.kind) << "\","
           << "\"meshPath\":\"" << EscapeJson(e.meshPath) << "\","
           << "\"textures\":{"
           << "\"albedo\":\"" << EscapeJson(e.textures.albedoPath) << "\","
           << "\"normal\":\"" << EscapeJson(e.textures.normalPath) << "\","
           << "\"rmaoh\":\"" << EscapeJson(e.textures.rmaohPath) << "\","
           << "\"emissive\":\"" << EscapeJson(e.textures.emissivePath) << "\","
           << "\"height\":\"" << EscapeJson(e.textures.heightPath) << "\""
           << "},"
           << "\"materialPreset\":\"" << EscapeJson(e.materialPreset) << "\","
           << "\"scale\":" << FmtFloat(e.scale) << ","
           << "\"yUpToZUp\":" << (e.yUpToZUp ? "true" : "false") << ","
           << "\"author\":\"" << EscapeJson(e.author) << "\","
           << "\"revision\":\"" << EscapeJson(e.revision) << "\""
           << "}";
    }
    os << "]}";
    return os.str();
}

bool OverrideRegistry::SaveManifestFile(const std::string& manifestPath, std::string* outErr) const {
    std::ofstream f(manifestPath, std::ios::binary | std::ios::trunc);
    if (!f) {
        if (outErr) *outErr = "failed to open manifest for writing: " + manifestPath;
        return false;
    }
    const std::string json = ExportManifestJson();
    f.write(json.data(), static_cast<std::streamsize>(json.size()));
    return f.good();
}

uint32_t OverrideRegistry::ScanMirrorDirectory(const std::string& overridesRootDir, std::string* outErr) {
    std::error_code ec;
    const std::filesystem::path root(overridesRootDir);
    if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) {
        if (outErr) *outErr = "overrides root directory does not exist: " + overridesRootDir;
        return 0u;
    }
    lastMirrorRoot_ = overridesRootDir;

    std::unordered_map<std::string, CustomAssetOverride> grouped;
    for (const auto& dirEntry : std::filesystem::recursive_directory_iterator(root, ec)) {
        if (ec) break;
        if (!dirEntry.is_regular_file(ec)) continue;
        const std::filesystem::path absPath = dirEntry.path();
        const std::filesystem::path relPath = std::filesystem::relative(absPath, root, ec);
        if (ec) continue;
        const std::string relStr = NormalizeVirtualPath(relPath.generic_string());
        const std::string ext = ExtensionLower(relStr);

        if (IsAllowedModernMeshExtension(relStr)) {
            std::string stemNoExt = relStr.substr(0, relStr.size() - ext.size());
            uint32_t fid = 0u;
            const size_t slash = stemNoExt.find_last_of('/');
            const std::string baseName = (slash == std::string::npos) ? stemNoExt : stemNoExt.substr(slash + 1);
            TryParseFormId(baseName, &fid);

            const std::string vanillaKey = (fid != 0u) ? ("formid/" + baseName + ".nif") : (stemNoExt + ".nif");
            auto& ov = grouped[vanillaKey];
            ov.vanillaPath = vanillaKey;
            ov.formId = fid;
            ov.kind = HasAnyTexturePath(ov.textures) ? OverrideAssetKind::Combined : OverrideAssetKind::Mesh;
            ov.meshPath = relPath.generic_string();
        } else if (IsAllowedModernTextureExtension(relStr)) {
            std::string stemNoExt = relStr.substr(0, relStr.size() - ext.size());
            enum class Slot { Albedo, Normal, Rmaoh, Emissive, Height };
            Slot slot = Slot::Albedo;
            auto stripSuffix = [&](std::string_view suf, Slot s) -> bool {
                if (stemNoExt.size() > suf.size() &&
                    stemNoExt.compare(stemNoExt.size() - suf.size(), suf.size(), suf) == 0) {
                    stemNoExt.erase(stemNoExt.size() - suf.size());
                    slot = s;
                    return true;
                }
                return false;
            };
            if (!stripSuffix("_normal", Slot::Normal) &&
                !stripSuffix("_n", Slot::Normal) &&
                !stripSuffix("_rmaoh", Slot::Rmaoh) &&
                !stripSuffix("_rmaos", Slot::Rmaoh) &&
                !stripSuffix("_emissive", Slot::Emissive) &&
                !stripSuffix("_e", Slot::Emissive) &&
                !stripSuffix("_height", Slot::Height) &&
                !stripSuffix("_h", Slot::Height)) {
                stripSuffix("_albedo", Slot::Albedo);
            }
            const std::string vanillaKey = stemNoExt + ".dds";
            auto& ov = grouped[vanillaKey];
            ov.vanillaPath = vanillaKey;
            ov.kind = ov.meshPath.empty() ? OverrideAssetKind::MaterialTextureSet : OverrideAssetKind::Combined;
            const std::string relGeneric = relPath.generic_string();
            switch (slot) {
                case Slot::Albedo:   ov.textures.albedoPath = relGeneric; break;
                case Slot::Normal:   ov.textures.normalPath = relGeneric; break;
                case Slot::Rmaoh:    ov.textures.rmaohPath = relGeneric; break;
                case Slot::Emissive: ov.textures.emissivePath = relGeneric; break;
                case Slot::Height:   ov.textures.heightPath = relGeneric; break;
            }
        }
    }

    uint32_t addedOrUpdated = 0u;
    for (auto& kv : grouped) {
        kv.second.fileFingerprint = ComputeOverrideFilesFingerprint(kv.second, overridesRootDir);
        if (RegisterOverride(std::move(kv.second), nullptr)) {
            ++addedOrUpdated;
        }
    }
    return addedOrUpdated;
}

HotReloadReport OverrideRegistry::PollHotReload(const std::string& baseDir, bool rescanMirrorDir) {
    HotReloadReport report;
    const std::string effBase = !baseDir.empty() ? baseDir : lastMirrorRoot_;

    if (rescanMirrorDir && !effBase.empty()) {
        std::unordered_map<std::string, uint64_t> prevStamps;
        for (const auto& e : entries_) {
            prevStamps[e.vanillaPath] = e.fileFingerprint;
        }
        OverrideRegistry fresh;
        fresh.ScanMirrorDirectory(effBase, nullptr);

        std::unordered_set<std::string> seenNow;
        for (const auto& ne : fresh.All()) {
            seenNow.insert(ne.vanillaPath);
            auto it = prevStamps.find(ne.vanillaPath);
            if (it == prevStamps.end()) {
                report.added.push_back(ne.vanillaPath);
            } else if (it->second != ne.fileFingerprint) {
                report.modified.push_back(ne.vanillaPath);
            }
        }
        for (const auto& kv : prevStamps) {
            if (seenNow.find(kv.first) == seenNow.end()) {
                report.removed.push_back(kv.first);
            }
        }
        entries_ = fresh.entries_;
        lastMirrorRoot_ = effBase;
        RebuildIndices();
        return report;
    }

    for (auto& e : entries_) {
        const uint64_t nowFp = ComputeOverrideFilesFingerprint(e, effBase);
        if (nowFp != e.fileFingerprint) {
            e.fileFingerprint = nowFp;
            report.modified.push_back(e.vanillaPath);
        }
    }
    return report;
}

const CustomAssetOverride* OverrideRegistry::FindByVanillaPath(std::string_view vanillaPath) const {
    return FindByPathHash(HashVirtualPath64(vanillaPath));
}

const CustomAssetOverride* OverrideRegistry::FindByPathHash(uint64_t pathHash) const {
    auto it = byPathHash_.find(pathHash);
    return (it != byPathHash_.end()) ? &entries_[it->second] : nullptr;
}

const CustomAssetOverride* OverrideRegistry::FindByFormId(uint32_t formId) const {
    auto it = byFormId_.find(formId);
    return (it != byFormId_.end()) ? &entries_[it->second] : nullptr;
}

}  // namespace raw::assets
