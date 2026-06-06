#!/usr/bin/env python3
"""build_stamp.py - build-freshness oracle (coherence membrane Tier 3.5).

Answers "does the built DLL match the current source?" - the build-state dead zone
the gifts call the worst (mtime lies; git checkout / NTFS make it false). Primary
signal is a SOURCE CONTENT hash, not a timestamp. A CMake POST_BUILD step writes the
manifest at build time; the host re-hashes the tree and compares.

  build_stamp.py write <manifest> <root...>   # hash sources -> manifest (post-build)
  build_stamp.py check <manifest> <root...>    # re-hash, compare -> fresh / STALE
"""
import sys, os, json, hashlib, time

EXTS = (".cpp", ".h", ".hpp", ".hxx", ".inl", ".hlsl", ".hlsli", ".asm", ".def")

def _iter_sources(roots):
    for root in roots:
        if os.path.isfile(root):
            yield root; continue
        for dirpath, _dirs, files in os.walk(root):
            # skip build / cache / vendor noise
            low = dirpath.replace("\\", "/").lower()
            if any(seg in low for seg in ("/build", "/.git", "/vcpkg", "/.warden", "/_selftest", "/live")):
                continue
            for fn in files:
                if fn.lower().endswith(EXTS):
                    yield os.path.join(dirpath, fn)

def hash_sources(roots):
    files = sorted(set(os.path.abspath(p) for p in _iter_sources(roots)))
    h = hashlib.sha256()
    common = os.path.commonpath(files) if files else ""
    for p in files:
        rel = os.path.relpath(p, common).replace("\\", "/")
        h.update(rel.encode("utf-8")); h.update(b"\0")
        with open(p, "rb") as f:
            h.update(f.read()); h.update(b"\0")
    return h.hexdigest(), len(files)

def write(manifest, roots):
    sha, n = hash_sources(roots)
    data = {"source_sha": sha, "files": n, "ts": int(time.time()),
            "roots": [os.path.abspath(r) for r in roots]}
    os.makedirs(os.path.dirname(os.path.abspath(manifest)) or ".", exist_ok=True)
    with open(manifest, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)
    print("build_stamp: wrote %s (%d files, sha %s)" % (manifest, n, sha[:12]))
    return 0

def check(manifest, roots):
    if not os.path.exists(manifest):
        print(json.dumps({"fresh": None, "reason": "no manifest (never built?)"})); return 0
    built = json.load(open(manifest, encoding="utf-8"))
    cur_sha, n = hash_sources(roots)
    fresh = (cur_sha == built.get("source_sha"))
    out = {"fresh": fresh, "built_sha": built.get("source_sha", "")[:12],
           "current_sha": cur_sha[:12], "built_files": built.get("files"),
           "current_files": n,
           "verdict": "DLL matches current source" if fresh else
                      "STALE: source changed since last build -- rebuild before trusting in-game"}
    print(json.dumps(out, indent=2))
    return 0 if fresh else 1

def main(argv):
    if len(argv) >= 4 and argv[1] == "write":
        return write(argv[2], argv[3:])
    if len(argv) >= 4 and argv[1] == "check":
        return check(argv[2], argv[3:])
    print(__doc__); return 2

if __name__ == "__main__":
    sys.exit(main(sys.argv))
