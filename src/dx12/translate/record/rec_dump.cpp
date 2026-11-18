// Text listing of a call-stream file: the header, one line per record with
// its leading fixed fields, and the summary.
#include "rec_reader.h"
#include <cstdio>

namespace raw::rec
{
namespace
{
void PrintFields(FILE* f, const Record& r)
{
    Cursor c(r);
    for (const char* s = OpSig(r.op); *s && c.ok; ++s) {
        if (*s == 'q') fprintf(f, " #%llu", static_cast<unsigned long long>(c.U64()));
        else if (*s == 'f') fprintf(f, " %g", c.F32());
        else fprintf(f, " %u", c.U32());
    }
}

// The second field of these records is an object kind; print its name too.
void PrintKind(FILE* f, const Record& r)
{
    const bool hasKind = r.op == OP_DEFINE_VIEW || r.op == OP_DEFINE_SHADER || r.op == OP_DEFINE_STATE
                      || r.op == OP_DEFINE_QUERY || r.op == OP_DEFINE_OPAQUE;
    if (!hasKind) return;
    Cursor c(r);
    c.U64();
    const u32 kind = c.U32();
    if (c.ok) fprintf(f, "  (%s)", KindName(kind));
}

void PrintSummary(FILE* f, const Summary& s)
{
    fprintf(f, "summary: %llu records, %llu content bytes, last render target #%llu\n",
            static_cast<unsigned long long>(s.recordCount), static_cast<unsigned long long>(s.contentBytes),
            static_cast<unsigned long long>(s.lastRenderTarget));
    for (const auto& oc : s.opCounts) fprintf(f, "  op   %-32s %u\n", OpName(oc.first), oc.second);
    for (const auto& kc : s.kindCounts) fprintf(f, "  kind %-32s %u\n", KindName(kc.first), kc.second);
    fprintf(f, "  undefined contents: %zu\n", s.undefined.size());
    for (const Summary::Undefined& u : s.undefined)
        fprintf(f, "    #%llu subresource %u: %s\n", static_cast<unsigned long long>(u.id), u.subresource,
                ReasonName(u.reason));
}
} // namespace

bool DumpListing(const wchar_t* path, void* out)
{
    FILE* f = static_cast<FILE*>(out);
    Reader reader;
    if (!reader.Open(path)) {
        fprintf(f, "error: %s\n", reader.Error().c_str());
        return false;
    }
    const FileHeader& h = reader.Header();
    fprintf(f, "RAWD3D11 version %u, %llu bytes, backbuffer %ux%u format %u, feature level 0x%x, adapter \"%s\"\n",
            h.version, static_cast<unsigned long long>(reader.FileBytes()), h.backbufferWidth,
            h.backbufferHeight, h.backbufferFormat, h.featureLevel, h.adapter.c_str());
    const std::vector<Record>& recs = reader.Records();
    for (size_t i = 0; i < recs.size(); ++i) {
        const Record& r = recs[i];
        fprintf(f, "%7zu  @%-10llu %-32s %8u bytes ", i, static_cast<unsigned long long>(r.offset),
                OpName(r.op), r.bytes);
        if (r.op != OP_SUMMARY) PrintFields(f, r);
        PrintKind(f, r);
        fputc('\n', f);
    }
    PrintSummary(f, reader.GetSummary());
    return true;
}
} // namespace raw::rec
