"""Technique name fragments for BSLightingShader, read from .rdata.

The engine composes technique names from short fragments stored after the
format string 'BSLighting%dSh%d'. The fragments are listed in storage
order. Mapping a fragment to a flag bit is an inference from that order;
the code that does the mapping is encrypted on disk and was not read.
"""
import re

NUL = b"\x00"
ANCHOR = b"BSLighting%dSh%d" + NUL


def lighting_fragments(img):
    rd = next(s for s in img.sections if s["name"] == ".rdata")
    i = rd["data"].find(ANCHOR)
    if i < 0:
        return []
    out = []
    start = i + len(ANCHOR)
    window = rd["data"][start:start + 0x200]
    for m in re.finditer(rb"[^\x00]+", window):
        word = m.group().decode("ascii", "replace")
        if ":" in word or not word.isalnum():
            break
        out.append((rd["rva"] + start + m.start(), word))
    return out
