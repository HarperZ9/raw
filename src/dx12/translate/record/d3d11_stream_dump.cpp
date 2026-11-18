// d3d11_stream_dump: prints a text listing of a D3D11 call-stream file.
//   d3d11_stream_dump <file>
// Exit code 0 when the file loads, 1 on a usage error, 2 when it does not load.
#include "rec_reader.h"
#include <cstdio>

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: d3d11_stream_dump <call-stream file>\n");
        return 1;
    }
    return raw::rec::DumpListing(argv[1], stdout) ? 0 : 2;
}
