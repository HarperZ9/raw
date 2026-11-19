"""Harvest names from CommonLibSSE-NG (MIT) sources: IDs and virtual slots.

CommonLibSSE-NG writes each engine address as an Address Library ID pair,
RELOCATION_ID(se, ae) or REL::VariantID(se, ae, vr_offset). This module
reads the AE id and the name of the code that uses it. Only names and IDs
are taken; no CommonLib code is copied.
"""
import os
import re

ID_RE = re.compile(
    r"(?:RELOCATION_ID|REL::RelocationID)\(\s*(\d+)\s*,\s*(\d+)\s*\)"
    r"|REL::VariantID\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(0x[0-9a-fA-F]+|\d+)\s*\)"
    r"|REL::VariantID\s+\w+\(\s*(\d+)\s*,\s*(\d+)\s*,")
TABLE_RE = re.compile(r"\b((?:RTTI|NiRTTI|VTABLE)_\w+)")
NS_RE = re.compile(r"^(\t*)namespace\s+(\w+)\s*$")
OFFSET_NS_RE = re.compile(r"^\s*namespace\s+(\w+)")
CONST_RE = re.compile(r"constexpr\s+auto\s+(\w+)\s*=")
CLASS_RE = re.compile(r"^(\t*)(?:class|struct)\s+(?:alignas\(\w+\)\s+)?"
                      r"(?:RE_\w+\s+)?(\w+)\b(?!\s*;)")
FUNC_RE = re.compile(r"(~?[A-Za-z_]\w*(?:::~?[A-Za-z_]\w*)*)\s*\([^;]*$")
VIRT_RE = re.compile(r"^(\t*)(?:virtual\s+)?[^/(]*?(~?)\b(\w+)\s*\([^/]*;"
                     r"\s*//\s*([0-9A-Fa-f]{2,3})\b")
MACRO_MEMBER_RE = re.compile(r"^\t*([\w:<>,\s\*&]+?)\s+(\w+)(\[[^\]]*\])?\s*;"
                             r"\s*/\*\s*([0-9A-Fa-f]{2,4})\s*\*/")
RUNTIME_RE = re.compile(r"(\w*RUNTIME_DATA)_CONTENT\s*//\s*([0-9A-Fa-f]+)")
SECTION_RE = re.compile(r"//\s*(override|add)\b\s*(?:\(([^)]*)\))?")
KEYWORDS = {"if", "for", "while", "switch", "return", "sizeof", "decltype",
            "static_cast", "reinterpret_cast", "func", "singleton", "REL",
            "Relocation", "RELOCATION_ID", "VariantID", "assert", "static_assert"}


def _kind(line):
    if "func_t" in line or "Relocation<decltype" in line:
        return "function"
    if re.search(r"Relocation<[^>]*\*\s*>", line) or "singleton" in line.lower():
        return "global"
    return "id"


def _enclosing(lines, i):
    """Best-effort enclosing function and class for line i."""
    func = None
    for j in range(i, max(-1, i - 40), -1):
        t = lines[j].strip()
        if t.startswith(("//", "#")):
            continue
        m = FUNC_RE.search(t)
        if m and m.group(1).split("::")[-1] not in KEYWORDS and "=" not in t.split("(")[0]:
            func = m.group(1)
            break
    chain = []
    indent = len(lines[i]) - len(lines[i].lstrip("\t"))
    for j in range(i, -1, -1):
        m = CLASS_RE.match(lines[j]) or NS_RE.match(lines[j])
        if m and len(m.group(1)) < indent:
            if m.group(2) not in ("RE", "detail"):
                chain.insert(0, m.group(2))
            indent = len(m.group(1))
    return func, "::".join(chain) or None


def _indent(line):
    """Indent depth with a tab or four spaces as one level."""
    wide = line.expandtabs(4)
    return (len(wide) - len(wide.lstrip())) // 4


def scan_ids(root):
    """Yield dicts {ae_id, se_id, name, kind, file, line}."""
    for dirpath, _dirs, files in os.walk(root):
        for fn in files:
            if not fn.endswith((".h", ".hpp", ".cpp", ".inl")):
                continue
            path = os.path.join(dirpath, fn)
            rel = os.path.relpath(path, root).replace("\\", "/")
            with open(path, encoding="utf-8", errors="replace") as f:
                lines = f.read().splitlines()
            ns_stack = {}
            for i, line in enumerate(lines):
                nm = OFFSET_NS_RE.match(line)
                if nm:
                    ind = _indent(line)
                    ns_stack = {k: v for k, v in ns_stack.items() if k < ind}
                    ns_stack[ind] = nm.group(1)
                ind = _indent(line)
                ns = "::".join(v for k, v in sorted(ns_stack.items())
                               if k < ind and v not in ("RE", "Offset"))
                for m in ID_RE.finditer(line):
                    se = int(m.group(1) or m.group(3) or m.group(6))
                    ae = int(m.group(2) or m.group(4) or m.group(7))
                    yield _describe(lines, i, line, ns, rel, se, ae, m)


def _describe(lines, i, line, ns, rel, se, ae, m):
    tm = TABLE_RE.search(line)
    if tm:
        name = tm.group(1)
        kind = name.split("_")[0].lower()
        if kind == "vtable":
            idx = line[:m.start()].count("REL::VariantID(")
            name = f"{name}[{idx}]"
        return {"ae_id": ae, "se_id": se, "name": name, "kind": kind,
                "file": rel, "line": i + 1}
    cm = CONST_RE.search(line)
    if cm and rel.endswith("Offsets.h"):
        kind = "global" if "Singleton" in cm.group(1) else "function"
        return {"ae_id": ae, "se_id": se, "name": f"{ns}::{cm.group(1)}",
                "kind": kind, "file": rel, "line": i + 1}
    func, cls = _enclosing(lines, i)
    name = func or "?"
    if cls and "::" not in name:
        name = f"{cls}::{name}"
    if "singleton" in line.lower() and name.endswith("GetSingleton"):
        kind = "global"
    else:
        kind = _kind(line)
    return {"ae_id": ae, "se_id": se, "name": name, "kind": kind,
            "file": rel, "line": i + 1}


def scan_virtuals(root):
    """Return {class: {slot: method}} for the primary vtable of each class."""
    out = {}
    for dirpath, _dirs, files in os.walk(os.path.join(root, "include", "RE")):
        for fn in files:
            if not fn.endswith(".h"):
                continue
            with open(os.path.join(dirpath, fn), encoding="utf-8",
                      errors="replace") as f:
                lines = f.read().splitlines()
            _virtuals_in(lines, out)
    return out


def _virtuals_in(lines, out):
    classes = {}      # indent -> (class name, first base)
    section = {}      # class -> base named by the current override block
    for line in lines:
        cm = CLASS_RE.match(line)
        if cm:
            base = re.search(r":\s*(?:public|protected|private)?\s*([\w:<>]+)", line)
            classes[len(cm.group(1))] = (cm.group(2), base.group(1) if base else None)
            section[cm.group(2)] = None
            continue
        sm = SECTION_RE.search(line)
        if sm and line.strip().startswith("//"):
            owner = classes.get(len(line) - len(line.lstrip("\t")) - 1)
            if owner:
                section[owner[0]] = sm.group(2)
            continue
        vm = VIRT_RE.match(line)
        if not vm or not ("virtual" in line or "override" in line):
            continue
        owner = classes.get(len(vm.group(1)) - 1)
        if not owner:
            continue
        cls, first = owner
        label = section.get(cls)
        if label and first and label.split("<")[0].split("::")[-1] \
                != first.split("<")[0].split("::")[-1] and not _chain_ok(label, out, first):
            continue  # a secondary base's vtable; slot index is not primary
        meth = "Dtor" if vm.group(2) == "~" else vm.group(3)
        if meth == "operator":
            meth = "operator()"
        out.setdefault(cls, {}).setdefault(int(vm.group(4), 16), meth)


def _chain_ok(label, out, first):
    # Override blocks can name an ancestor further up the primary chain
    # (e.g. "override (NiObject)" in a class deriving from NiAVObject).
    return label.split("::")[-1] in {"NiRefObject", "NiObject", "NiAVObject",
                                     "NiNode", "BSShader", "TESForm",
                                     "TESObjectREFR", "Actor", "BSFadeNode",
                                     "BSGeometry", "NiObjectNET"}


MEMBER_RE = re.compile(r"^(\t+)([\w:<>,\s\*&\[\]]+?)\s+(\w+)(\[[^\]]*\])?\s*(?::\s*\d+)?\s*;"
                       r"\s*//\s*([0-9A-Fa-f]{2,4})\b\s*(?:-\s*(.*))?$")


def scan_members(root, wanted):
    """Return {label: [(offset, type, name, note)]}.

    wanted maps a label to (header file name, class key); nested classes
    are keyed 'Outer::Inner', e.g. {'ShaderState': ('BSShaderManager.h',
    'BSShaderManager::State')}.
    """
    by_file = {}
    for label, (fname, key) in wanted.items():
        by_file.setdefault(fname, {})[key] = label
    out = {}
    for dirpath, _dirs, files in os.walk(os.path.join(root, "include", "RE")):
        for fn in files:
            keys = by_file.get(fn)
            if not keys:
                continue
            with open(os.path.join(dirpath, fn), encoding="utf-8",
                      errors="replace") as f:
                lines = f.read().splitlines()
            _members_in(lines, keys, out)
    return out


def _members_in(lines, keys, out):
    stack, bases, pending = {}, {}, []
    macro_owner = None
    for line in lines:
        if line.startswith("#define") and "RUNTIME_DATA_CONTENT" in line:
            deepest = stack[max(stack)] if stack else ""
            macro_owner = deepest.rsplit("::", 1)[0] if "RUNTIME_DATA" in deepest else deepest
            continue
        if macro_owner is not None:
            mm = MACRO_MEMBER_RE.match(line)
            label = keys.get(macro_owner)
            if mm and label:
                typ = " ".join(mm.group(1).split()).replace("mutable ", "")
                out.setdefault(label, []).append(
                    (int(mm.group(4), 16), typ, mm.group(2) + (mm.group(3) or ""),
                     "runtime data macro; offset as written in CommonLib (some blocks are relative to their struct)"))
            if not line.rstrip().endswith("\\"):
                macro_owner = None
            continue
        rm = RUNTIME_RE.search(line)
        if rm:
            owner = stack.get(len(line) - len(line.lstrip("\t")) - 1)
            if owner:
                bases[f"{owner}::{rm.group(1)}"] = int(rm.group(2), 16)
        cm = CLASS_RE.match(line)
        if cm:
            ind = len(cm.group(1))
            outer = stack.get(ind - 1)
            stack = {k: v for k, v in stack.items() if k < ind}
            stack[ind] = f"{outer}::{cm.group(2)}" if outer else cm.group(2)
            continue
        mm = MEMBER_RE.match(line)
        if not mm or "virtual" in line or "static" in line:
            continue
        holder = stack.get(len(mm.group(1)) - 1) or ""
        label, nested = keys.get(holder), None
        if label is None and holder.endswith("RUNTIME_DATA"):
            label, nested = keys.get(holder.rsplit("::", 1)[0]), holder
        if label is None:
            continue
        typ = " ".join(mm.group(2).split())
        if typ in ("return", "using", "friend"):
            continue
        row = [int(mm.group(5), 16), typ, mm.group(3) + (mm.group(4) or ""),
               (mm.group(6) or "").strip()]
        if nested:
            pending.append((label, nested, row))
        else:
            out.setdefault(label, []).append(tuple(row))
    for label, nested, row in pending:
        base = bases.get(nested)
        if base is None:
            row[3] = (row[3] + f" offset within {nested}").strip()
        else:
            row[0] += base
            row[3] = (row[3] + f" via {nested.split('::')[-1]} at 0x{base:X}").strip()
        out.setdefault(label, []).append(tuple(row))
