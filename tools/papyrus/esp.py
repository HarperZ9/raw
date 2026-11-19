"""A minimal Skyrim Special Edition plugin writer: a TES4 header and quests that carry scripts. Original work, MIT.

Record layout from the public format description (UESP, "Skyrim Mod:Mod File Format"): a record header is type,
data size, flags, form id, version control, form version and one more 16-bit field (24 bytes); a group header is
GRUP, its whole size, its label, its type and the same stamps (24 bytes); each field is a type, a 16-bit size and
its data. Nothing from any Bethesda file is read or copied.
"""
from __future__ import annotations

import struct

FORM_VERSION = 44          # Skyrim Special Edition records
HEDR_VERSION = 1.71
FLAG_ESL = 0x200
QUEST_START_GAME_ENABLED = 0x0001


def field(tag: str, data: bytes) -> bytes:
    if len(data) > 0xFFFF:
        raise ValueError(f"{tag}: field too large")
    return tag.encode("ascii") + struct.pack("<H", len(data)) + data


def zstr(s: str) -> bytes:
    return s.encode("cp1252") + b"\0"


def wstr(s: str) -> bytes:
    e = s.encode("cp1252")
    return struct.pack("<H", len(e)) + e


def record(tag: str, form_id: int, fields: bytes, flags: int = 0) -> bytes:
    return tag.encode("ascii") + struct.pack("<IIIHHHH", len(fields), flags, form_id, 0, 0, FORM_VERSION, 0) + fields


def group(label: str, records: bytes) -> bytes:
    return b"GRUP" + struct.pack("<I4sIHHHH", 24 + len(records), label.encode("ascii"), 0, 0, 0, 0, 0) + records


def vmad_quest(scripts: list[str], objects: dict[str, int] | None = None) -> bytes:
    """Scripts attached to a quest, an empty fragment block and no aliases. objects: property name to the form id of
    a record in this plugin, set on every script (object format 2: unused 16 bits, alias -1, form id)."""
    objects = objects or {}
    out = struct.pack("<hhH", 5, 2, len(scripts))
    for s in scripts:
        out += wstr(s) + struct.pack("<BH", 0, len(objects))   # status 0 (local)
        for name, form_id in objects.items():
            out += wstr(name) + struct.pack("<BB", 1, 1) + struct.pack("<HhI", 0, -1, form_id)   # object, edited
    # quest fragment block: version byte, fragment count, fragment file name (none), alias count
    out += struct.pack("<bH", 2, 0) + wstr("") + struct.pack("<H", 0)
    return out


def quest(form_id: int, editor_id: str, scripts: list[str], objects: dict[str, int] | None = None) -> bytes:
    dnam = struct.pack("<HBBfI", QUEST_START_GAME_ENABLED, 50, 0, 0.0, 0)   # flags, priority, unused, delay, type
    fields = field("EDID", zstr(editor_id)) + field("VMAD", vmad_quest(scripts, objects)) + field("DNAM", dnam)
    fields += field("NEXT", b"") + field("ANAM", struct.pack("<I", 0))
    return record("QUST", form_id, fields)


def global_float(form_id: int, editor_id: str, value: float = 0.0) -> bytes:
    return record("GLOB", form_id, field("EDID", zstr(editor_id)) + field("FNAM", b"f") + field("FLTV", struct.pack("<f", value)))


def plugin(author: str, quests: list[tuple[str, list[str]]], light: bool = True, first_id: int = 0x800,
           counters: bool = False) -> bytes:
    """quests: (editor id, script names) in form-id order from first_id. counters: one float global per quest,
    RAWLoadCountNN, after the quests' ids, set as the quest scripts' Counter property."""
    n = len(quests)
    globs = b"".join(global_float(first_id + n + i, f"RAWLoadCount{i:02d}") for i in range(n)) if counters else b""
    recs = b"".join(quest(first_id + i, eid, s, {"Counter": first_id + n + i} if counters else None)
                    for i, (eid, s) in enumerate(quests))
    records = n * (2 if counters else 1) + (2 if counters else 1)          # records and groups
    hedr = struct.pack("<fiI", HEDR_VERSION, records, first_id + n * (2 if counters else 1))
    header = field("HEDR", hedr) + field("CNAM", zstr(author))
    body = (group("GLOB", globs) if counters else b"") + group("QUST", recs)
    return record("TES4", 0, header, FLAG_ESL if light else 0) + body
