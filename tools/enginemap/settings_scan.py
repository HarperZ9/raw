"""INI and game settings from statically initialised Setting objects.

Each Setting in .data is {vtable*, value (8 bytes), const char* name}. The
vtable tells which collection owns it (SettingT<INISettingCollection> is
Skyrim.ini, SettingT<INIPrefSettingCollection> is SkyrimPrefs.ini,
SettingT<GameSettingCollection> is a game setting). The value read here is
the compiled default, before any INI file is applied.
"""
import struct

FILES = {
    "INISettingCollection": "Skyrim.ini",
    "INIPrefSettingCollection": "SkyrimPrefs.ini",
    "GameSettingCollection": "GMST",
}


def _decode(img, prefix, raw8):
    lo32 = struct.unpack_from("<I", raw8)[0]
    if prefix == "f":
        return f"{struct.unpack_from('<f', raw8)[0]:.6g}", "float"
    if prefix == "i":
        return str(struct.unpack_from("<i", raw8)[0]), "int"
    if prefix == "u":
        return str(lo32), "uint"
    if prefix == "b":
        return str(bool(raw8[0])).lower(), "bool"
    if prefix in "rcR":
        return "rgba(%d,%d,%d,%d)" % tuple(raw8[:4]), "color"
    if prefix in "sS":
        ptr = struct.unpack_from("<Q", raw8)[0]
        if ptr == 0:
            return '""', "string"
        txt = img.cstr(ptr - img.base, 256)
        return (repr(txt) if txt is not None else "?"), "string"
    return raw8.hex(), "unknown"


def scan(img, vtables):
    """vtables: rtti.scan() output. Returns list of setting dicts."""
    owners = {}
    for v in vtables:
        for coll in FILES:
            if v["name"] == f"SettingT<{coll}>":
                owners[img.base + v["vtable"]] = coll
    data = next(s for s in img.sections if s["name"] == ".data")
    d, base_rva = data["data"], data["rva"]
    out = []
    for off in range(0, len(d) - 24, 8):
        vt, _val = struct.unpack_from("<QQ", d, off)
        coll = owners.get(vt)
        if coll is None:
            continue
        name_ptr = struct.unpack_from("<Q", d, off + 16)[0]
        name = img.cstr(name_ptr - img.base, 128)
        if not name:
            continue
        key, _, section = name.partition(":")
        prefix = key[:1]
        value, vtype = _decode(img, prefix, d[off + 8:off + 16])
        out.append({
            "rva": base_rva + off, "name": name, "key": key,
            "section": section, "file": FILES[coll], "type": vtype,
            "default": value, "name_rva": name_ptr - img.base,
        })
    return out
