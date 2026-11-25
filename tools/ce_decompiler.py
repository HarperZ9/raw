#!/usr/bin/env python3
"""
ce_decompiler.py — Automated PE64 MSVC x64 RTTI, Vtable, Hierarchy, Xref & Disassembly Extractor
for Creation Engine (SkyrimSE.exe 1.7.104) clean-room engine replacement engineering.

Original work, MIT. Pure Python 3 stdlib (struct/mmap/re/bisect) with optional Capstone x86-64
disassembly & CFG analysis when `capstone` is installed.

Features:
  1. Direct PE64 parsing (.text, .rdata, .data, .pdata RUNTIME_FUNCTION table).
  2. Full MSVC x64 RTTI scanner:
       - RTTICompleteObjectLocator (signature == 1, pSelf == col_rva self-consistency check)
       - RTTITypeDescriptor (.?AV...@@ / .?AU...@@) + clean MSVC C++ demangler
       - RTTIClassHierarchyDescriptor & RTTIBaseClassDescriptor full inheritance chains
       - Vtable RVA discovery via .rdata scan for VA == ImageBase + col_rva at (vtable_rva - 8)
       - Exact virtual method slot counting & first-12 method RVAs (validated against .text)
  3. Automated Creation Engine subsystem taxonomy classification & TSV catalog generation.
  4. Targeted disassembly & CFG inspection (`--disasm <rva> --bytes N`) with RIP-relative
     annotation (strings, RTTI vtables, .pdata function bounds, basic block leaders, call/branch graph).
  5. RIP-relative x86-64 cross-reference scanner (`--xrefs <rva>`).
"""

from __future__ import annotations

import argparse
import bisect
import os
import re
import struct
import sys
import time
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Set, Tuple

try:
    import capstone  # type: ignore
    HAS_CAPSTONE = True
except ImportError:
    capstone = None
    HAS_CAPSTONE = False


# ---------------------------------------------------------------------------
# PE64 Binary Model
# ---------------------------------------------------------------------------

@dataclass
class Section:
    name: str
    virtual_size: int
    virtual_address: int
    raw_size: int
    raw_ptr: int
    characteristics: int

    @property
    def is_exec(self) -> bool:
        return bool(self.characteristics & 0x20000000)

    @property
    def is_read(self) -> bool:
        return bool(self.characteristics & 0x40000000)

    @property
    def is_write(self) -> bool:
        return bool(self.characteristics & 0x80000000)

    @property
    def va_end(self) -> int:
        return self.virtual_address + max(self.virtual_size, self.raw_size)

    def contains_rva(self, rva: int) -> bool:
        return self.virtual_address <= rva < self.virtual_address + self.raw_size


class PE64Image:
    """Zero-dependency PE64 parser with fast RVA<->file-offset translation and .pdata index."""

    def __init__(self, path: str) -> None:
        self.path = path
        with open(path, "rb") as f:
            self.data = f.read()
        self.size = len(self.data)
        if self.size < 0x100 or self.data[:2] != b"MZ":
            raise ValueError(f"Not a valid PE file (missing MZ header): {path}")

        e_lfanew = struct.unpack_from("<I", self.data, 0x3C)[0]
        if e_lfanew + 0x108 > self.size or self.data[e_lfanew : e_lfanew + 4] != b"PE\x00\x00":
            raise ValueError(f"Invalid PE signature at 0x{e_lfanew:X}")

        coff_off = e_lfanew + 4
        (
            self.machine,
            num_sections,
            self.timestamp,
            _sym_ptr,
            _num_syms,
            opt_size,
            _chars,
        ) = struct.unpack_from("<HHIIIHH", self.data, coff_off)
        if self.machine != 0x8664:
            raise ValueError(f"Expected AMD64 PE64 (0x8664), got 0x{self.machine:X}")

        opt_off = coff_off + 20
        magic = struct.unpack_from("<H", self.data, opt_off)[0]
        if magic != 0x20B:
            raise ValueError(f"Expected PE32+ optional header (0x20B), got 0x{magic:X}")

        self.image_base = struct.unpack_from("<Q", self.data, opt_off + 24)[0]
        self.size_of_image = struct.unpack_from("<I", self.data, opt_off + 56)[0]
        num_rva_sizes = struct.unpack_from("<I", self.data, opt_off + 108)[0]

        self.data_dirs: List[Tuple[int, int]] = []
        dd_off = opt_off + 112
        for i in range(min(num_rva_sizes, 16)):
            rva, sz = struct.unpack_from("<II", self.data, dd_off + i * 8)
            self.data_dirs.append((rva, sz))

        sec_off = opt_off + opt_size
        self.sections: List[Section] = []
        for i in range(num_sections):
            off = sec_off + i * 40
            raw_name = self.data[off : off + 8]
            name = raw_name.split(b"\x00", 1)[0].decode("ascii", errors="replace")
            vsize, vaddr, rsize, rptr, _rel, _ln, _nr, _nl, ch = struct.unpack_from(
                "<IIIIIIHHI", self.data, off + 8
            )
            self.sections.append(Section(name, vsize, vaddr, rsize, rptr, ch))

        self.sections.sort(key=lambda s: s.virtual_address)
        self._sec_starts = [s.virtual_address for s in self.sections]

        self.text_sections = [s for s in self.sections if s.is_exec]
        self.rdata_sections = [s for s in self.sections if s.is_read and not s.is_exec and not s.is_write]
        self.data_sections = [s for s in self.sections if s.is_read and s.is_write and not s.is_exec]

        # Parse .pdata RUNTIME_FUNCTION table (DataDirectory[3])
        self.pdata_begins: List[int] = []
        self.pdata_ends: List[int] = []
        if len(self.data_dirs) > 3:
            pd_rva, pd_size = self.data_dirs[3]
            pd_off = self.rva_to_offset(pd_rva)
            if pd_off is not None and pd_size >= 12:
                count = pd_size // 12
                funcs: List[Tuple[int, int]] = []
                for i in range(count):
                    b_rva, e_rva, _u = struct.unpack_from("<III", self.data, pd_off + i * 12)
                    if b_rva < e_rva:
                        funcs.append((b_rva, e_rva))
                funcs.sort()
                self.pdata_begins = [f[0] for f in funcs]
                self.pdata_ends = [f[1] for f in funcs]

    def find_section_for_rva(self, rva: int) -> Optional[Section]:
        idx = bisect.bisect_right(self._sec_starts, rva) - 1
        if idx < 0:
            return None
        s = self.sections[idx]
        if s.virtual_address <= rva < s.virtual_address + s.raw_size:
            return s
        return None

    def rva_to_offset(self, rva: int) -> Optional[int]:
        s = self.find_section_for_rva(rva)
        if s is None:
            return None
        off = s.raw_ptr + (rva - s.virtual_address)
        if off < 0 or off >= self.size:
            return None
        return off

    def is_exec_rva(self, rva: int) -> bool:
        for s in self.text_sections:
            if s.virtual_address <= rva < s.virtual_address + max(s.virtual_size, s.raw_size):
                return True
        return False

    def read_bytes(self, rva: int, length: int) -> Optional[bytes]:
        off = self.rva_to_offset(rva)
        if off is None or off + length > self.size:
            return None
        return self.data[off : off + length]

    def read_u32(self, rva: int) -> Optional[int]:
        off = self.rva_to_offset(rva)
        if off is None or off + 4 > self.size:
            return None
        return struct.unpack_from("<I", self.data, off)[0]

    def read_u64(self, rva: int) -> Optional[int]:
        off = self.rva_to_offset(rva)
        if off is None or off + 8 > self.size:
            return None
        return struct.unpack_from("<Q", self.data, off)[0]

    def read_cstr(self, rva: int, max_len: int = 512) -> Optional[str]:
        off = self.rva_to_offset(rva)
        if off is None:
            return None
        end = self.data.find(b"\x00", off, min(self.size, off + max_len))
        if end < 0:
            return None
        try:
            return self.data[off:end].decode("ascii")
        except UnicodeDecodeError:
            return None

    def pdata_func_bounds(self, rva: int) -> Optional[Tuple[int, int]]:
        if not self.pdata_begins:
            return None
        idx = bisect.bisect_right(self.pdata_begins, rva) - 1
        if idx >= 0 and self.pdata_begins[idx] <= rva < self.pdata_ends[idx]:
            return (self.pdata_begins[idx], self.pdata_ends[idx])
        return None


# ---------------------------------------------------------------------------
# MSVC x64 RTTI Name Demangler (clean recursive-descent for namespaces & templates)
# ---------------------------------------------------------------------------

def _parse_msvc_name_seq(s: str, i: int) -> Tuple[List[str], int]:
    """Parses a sequence of MSVC scope components up to the terminating '@' (or end)."""
    parts: List[str] = []
    n = len(s)
    while i < n:
        if s[i] == "@":
            return parts, i + 1
        if s[i : i + 2] == "?$":
            tok, i = _parse_msvc_template(s, i)
            parts.append(tok)
        elif s[i : i + 3] == "?A0":
            end = s.find("@", i)
            if end < 0:
                parts.append("(anon)")
                return parts, n
            parts.append("(anon)")
            i = end + 1
        elif s[i : i + 1] == "?":
            end = s.find("@", i)
            if end < 0:
                parts.append(s[i:])
                return parts, n
            parts.append(s[i:end])
            i = end + 1
        else:
            end = s.find("@", i)
            if end < 0:
                parts.append(s[i:])
                return parts, n
            tok = s[i:end]
            if tok:
                parts.append(tok)
            i = end + 1
    return parts, i


def _parse_msvc_type_arg(s: str, i: int) -> Tuple[str, int]:
    """Parses a single MSVC mangled template argument (best-effort readable form)."""
    n = len(s)
    if i >= n:
        return "", i
    # Pointer/reference qualifiers: PEAV, PEAU, PEBV, AEAV, etc.
    if s[i : i + 2] in ("PE", "AE", "QE"):
        qual = "*" if s[i] == "P" else "&"
        i += 2
        if i < n and s[i] in "ABCD":
            i += 1
        inner, i = _parse_msvc_type_arg(s, i)
        return f"{inner}{qual}", i
    # Class / Struct / Union / Enum: V, U, T, W4
    if s[i] in ("V", "U", "T"):
        i += 1
        parts, i = _parse_msvc_name_seq(s, i)
        parts.reverse()
        return "::".join(parts), i
    if s[i : i + 2] == "W4":
        i += 2
        parts, i = _parse_msvc_name_seq(s, i)
        parts.reverse()
        return "::".join(parts), i
    # Non-type integer constant: $0...
    if s[i : i + 2] == "$0":
        j = i + 2
        if j < n and s[j].isdigit():
            return s[j], j + 1
        end = s.find("@", j)
        if end < 0:
            return s[i:], n
        return s[i:end], end + 1
    # Primitive types
    prims = {
        "X": "void", "D": "char", "E": "uchar", "F": "short", "G": "ushort",
        "H": "int", "I": "uint", "J": "long", "K": "ulong", "M": "float",
        "N": "double", "_N": "bool", "_J": "int64", "_K": "uint64", "_W": "wchar_t",
    }
    if s[i : i + 2] in prims:
        return prims[s[i : i + 2]], i + 2
    if s[i] in prims:
        return prims[s[i]], i + 1
    # Fallback: advance 1 char
    return s[i], i + 1


def _parse_msvc_template(s: str, i: int) -> Tuple[str, int]:
    """Parses '?$Name@Arg1Arg2...@' into 'Name<Arg1,Arg2,...>'."""
    n = len(s)
    i += 2  # skip '?$'
    name_end = s.find("@", i)
    if name_end < 0:
        return s[i:], n
    tname = s[i:name_end]
    i = name_end + 1
    args: List[str] = []
    guard = 0
    while i < n and s[i] != "@" and guard < 16:
        guard += 1
        arg, new_i = _parse_msvc_type_arg(s, i)
        if new_i <= i:
            break
        if arg:
            args.append(arg)
        i = new_i
    if i < n and s[i] == "@":
        i += 1
    args_str = ",".join(args)
    if len(args_str) > 64:
        args_str = args_str[:61] + "..."
    return f"{tname}<{args_str}>", i


def demangle_msvc_rtti(mangled: str) -> str:
    """Demangles MSVC RTTI TypeDescriptor names like '.?AVArchiveStream@BSResource@@' -> 'BSResource::ArchiveStream'."""
    if not mangled:
        return ""
    s = mangled
    for prefix in (".?AV", ".?AU", ".?AT", ".?AW4"):
        if s.startswith(prefix):
            s = s[len(prefix) :]
            break
    else:
        return mangled

    parts, _ = _parse_msvc_name_seq(s, 0)
    if not parts:
        return mangled
    parts.reverse()
    return "::".join(parts)


# ---------------------------------------------------------------------------
# Subsystem Classification Taxonomy
# ---------------------------------------------------------------------------

EXACT_SUBSYSTEM_MAP: Dict[str, str] = {
    # World & Streaming
    "TES": "WorldStreaming",
    "TESObjectCELL": "WorldStreaming",
    "TESWorldSpace": "WorldStreaming",
    "GridCellArray": "WorldStreaming",
    "GridArray": "WorldStreaming",
    "BGSTerrainManager": "TerrainLOD",
    "BGSTerrainNode": "TerrainLOD",
    "BGSTerrainChunk": "TerrainLOD",
    "BGSStoryTeller": "WorldStreaming",
    "TESObjectLAND": "TerrainLOD",
    "TESLandTexture": "TerrainLOD",
    "TESRegion": "WorldStreaming",
    "TESRegionList": "WorldStreaming",
    "TESDataHandler": "WorldStreaming",
    "TESFile": "WorldStreaming",
    "BGSGrassManager": "FloraGeometry",
    "TESGrass": "FloraGeometry",
    "TESObjectTREE": "FloraGeometry",
    "TESFlora": "FloraGeometry",
    # Water & Sky
    "TESWaterSystem": "WaterSim",
    "TESWaterObject": "WaterSim",
    "TESWaterForm": "WaterSim",
    "TESWaterReflections": "WaterSim",
    "TESWaterDisplacement": "WaterSim",
    "TESWaterNormals": "WaterSim",
    "Sky": "SkyAtmosphere",
    "TESWeather": "SkyAtmosphere",
    "TESClimate": "SkyAtmosphere",
    "Clouds": "SkyAtmosphere",
    "Moon": "SkyAtmosphere",
    "Stars": "SkyAtmosphere",
    "Sun": "SkyAtmosphere",
    "Atmosphere": "SkyAtmosphere",
    "Precipitation": "SkyAtmosphere",
    "SkyObject": "SkyAtmosphere",
    "TESImageSpace": "SkyAtmosphere",
    "TESImageSpaceModifier": "SkyAtmosphere",
    "ImageSpaceManager": "SkyAtmosphere",
    # Scene Graph & Rendering
    "ShadowSceneNode": "RenderPipeline",
    "BSCullingProcess": "RenderPipeline",
    "BSGeometry": "RenderPipeline",
    "BSTriShape": "RenderPipeline",
    "BSDynamicTriShape": "RenderPipeline",
    "BSLightingShaderProperty": "RenderPipeline",
    "BSEffectShaderProperty": "RenderPipeline",
    "BSShaderProperty": "RenderPipeline",
    "BSWaterShaderProperty": "WaterSim",
    "BSFadeNode": "RenderPipeline",
    "NiCamera": "RenderPipeline",
    "NiNode": "RenderPipeline",
    "NiAVObject": "RenderPipeline",
    "NiObject": "RenderPipeline",
    "NiObjectNET": "RenderPipeline",
    "BSLight": "RenderPipeline",
    "BSShadowLight": "RenderPipeline",
    "BSShadowDirectionalLight": "RenderPipeline",
    "BSShadowParabolicLight": "RenderPipeline",
    "BSShadowFrustumLight": "RenderPipeline",
    "NiLight": "RenderPipeline",
    "NiDirectionalLight": "RenderPipeline",
    "NiPointLight": "RenderPipeline",
    "NiSkinInstance": "RenderPipeline",
    "BSDismemberSkinInstance": "RenderPipeline",
    "BSMultiBoundNode": "RenderPipeline",
    "BSOcclusionShape": "RenderPipeline",
    "BSOcclusionPlane": "RenderPipeline",
    "BSLeafAnimNode": "FloraGeometry",
    "BSTreeNode": "FloraGeometry",
    "BSMultiStreamInstanceTriShape": "RenderPipeline",
    # Camera & Actors
    "PlayerCamera": "CameraActors",
    "TESCamera": "CameraActors",
    "TESCameraState": "CameraActors",
    "FreeCameraState": "CameraActors",
    "FirstPersonState": "CameraActors",
    "ThirdPersonState": "CameraActors",
    "DragonCameraState": "CameraActors",
    "HorseCameraState": "CameraActors",
    "BleedoutCameraState": "CameraActors",
    "TweenMenuCameraState": "CameraActors",
    "Actor": "CameraActors",
    "Character": "CameraActors",
    "PlayerCharacter": "CameraActors",
    "TESObjectREFR": "CameraActors",
    "ProcessLists": "CameraActors",
    "AIProcess": "CameraActors",
    "TESNPC": "CameraActors",
    "TESRace": "CameraActors",
    "NavMesh": "CameraActors",
    "NavMeshInfoMap": "CameraActors",
    # Memory, Jobs & IO
    "MemoryManager": "MemoryAllocator",
    "ScrapHeap": "MemoryAllocator",
    "IMemoryStore": "MemoryAllocator",
    "IMemoryStoreBase": "MemoryAllocator",
    "IMemoryHeap": "MemoryAllocator",
    "AbstractHeap": "MemoryAllocator",
    "MemoryHeap": "MemoryAllocator",
    "ZeroOverheadHeap": "MemoryAllocator",
    "TaskQueueInterface": "JobScheduler",
    "BSTaskManager": "JobScheduler",
    "BSTaskManagerThread": "JobScheduler",
    "JobListManager": "JobScheduler",
    "IOManager": "AssetArchive",
    "IOTask": "AssetArchive",
    "BSResource::LooseFileLocation": "AssetArchive",
    "BSResource::ArchiveStream": "AssetArchive",
    "BSResource::LooseFileStream": "AssetArchive",
    "BSResource::Stream": "AssetArchive",
    "BSResource::StreamBase": "AssetArchive",
    "BSResource::Location": "AssetArchive",
    "BSResource::Archive": "AssetArchive",
    "BSResource::CompressedArchiveStream": "AssetArchive",
    "BSResource::GlobalLocations": "AssetArchive",
    "BSResource::GlobalPaths": "AssetArchive",
    "QueuedFile": "AssetArchive",
    "QueuedReference": "WorldStreaming",
    "QueuedTree": "FloraGeometry",
    # Physics & Havok
    "bhkWorld": "PhysicsWorld",
    "bhkWorldM": "PhysicsWorld",
    "bhkCollisionObject": "PhysicsWorld",
    "bhkRigidBody": "PhysicsWorld",
    "bhkRigidBodyT": "PhysicsWorld",
    "bhkPhantom": "PhysicsWorld",
    "bhkShape": "PhysicsWorld",
    "bhkCharacterController": "PhysicsWorld",
    "bhkCharProxyController": "PhysicsWorld",
    "bhkCharRigidBodyController": "PhysicsWorld",
    "hkpWorld": "PhysicsWorld",
    "hkpRigidBody": "PhysicsWorld",
    # Magic & FX
    "MagicSystem": "MagicFX",
    "ActiveEffect": "MagicFX",
    "ValueModifierEffect": "MagicFX",
    "ScriptEffect": "MagicFX",
    "MagicTarget": "MagicFX",
    "MagicCaster": "MagicFX",
    "ActorMagicCaster": "MagicFX",
    "SpellItem": "MagicFX",
    "EffectSetting": "MagicFX",
    "BSParticleShaderEmitter": "FluidCombustion",
    "BSParticleShaderObjectEmitter": "FluidCombustion",
    "BSParticleShaderCubeEmitter": "FluidCombustion",
    "BSParticleShaderRainEmitter": "FluidCombustion",
    "BSParticleShaderSnowEmitter": "FluidCombustion",
    "BSParticleShaderProperty": "FluidCombustion",
    "NiParticleSystem": "FluidCombustion",
    "BSStripParticleSystem": "FluidCombustion",
    "BGSDecalManager": "MagicFX",
    "BGSDecalEmitter": "MagicFX",
    "BSTempEffect": "MagicFX",
    "BSTempEffectParticle": "FluidCombustion",
    "BSTempEffectGeometryDecal": "MagicFX",
    "Hazard": "FluidCombustion",
    "FlameProjectile": "FluidCombustion",
    "ConeProjectile": "FluidCombustion",
    "BeamProjectile": "MagicFX",
    "MissileProjectile": "MagicFX",
    "Projectile": "MagicFX",
    # Papyrus VM & UI
    "BSScript::Internal::VirtualMachine": "ScriptingUI",
    "UI": "ScriptingUI",
    "IMenu": "ScriptingUI",
}


def classify_subsystem(class_name: str, inheritance: List[str]) -> str:
    """Classifies a Creation Engine C++ class into a RAW clean-room replacement subsystem."""
    base_name = class_name.split("+0x", 1)[0]
    if base_name in EXACT_SUBSYSTEM_MAP:
        return EXACT_SUBSYSTEM_MAP[base_name]

    # Check direct inheritance hints
    for parent in inheritance:
        if parent in EXACT_SUBSYSTEM_MAP and parent not in ("NiObject", "NiObjectNET", "NiAVObject", "NiNode"):
            return EXACT_SUBSYSTEM_MAP[parent]

    low = base_name.lower()
    if base_name.startswith(("bhk", "hkp", "hka", "hkb", "hk")):
        return "PhysicsWorld"
    if base_name.startswith(("CombatBehavior", "BGSProcedure", "Movement", "Pathing", "Actor", "Character")):
        return "CameraActors"
    if any(k in low for k in ("terrain", "bgsdistanttree", "lod")):
        return "TerrainLOD"
    if any(k in low for k in ("water", "wave", "ripple")):
        return "WaterSim"
    if any(k in low for k in ("grass", "flora", "leaf", "foliage", "bstree", "tesobjecttree", "queuedtree", "treelod")):
        return "FloraGeometry"
    if any(k in low for k in ("particle", "emitter", "flame", "fire", "smoke", "hazard")):
        return "FluidCombustion"
    if any(k in low for k in ("sky", "weather", "cloud", "moon", "sun", "star", "precip", "imagespace")):
        return "SkyAtmosphere"
    if base_name.startswith("BSResource::") or any(k in low for k in ("archive", "iomanager", "iotask", "stream", "bsa", "ba2")):
        return "AssetArchive"
    if any(k in low for k in ("memory", "heap", "alloc", "scrapheap", "pool")):
        return "MemoryAllocator"
    if any(k in low for k in ("task", "job", "thread", "worker", "lock")):
        return "JobScheduler"
    if any(k in low for k in ("shader", "render", "shadow", "cull", "trishape", "geometry", "light", "camera", "niav", "ninode", "bsfade", "bsgraphics")):
        return "RenderPipeline"
    if any(k in low for k in ("cell", "worldspace", "gridcell", "region", "tesdata", "storyteller")):
        return "WorldStreaming"
    if any(k in low for k in ("actor", "player", "camera", "process", "navmesh", "ai", "path", "combat", "refr")):
        return "CameraActors"
    if any(k in low for k in ("magic", "spell", "effect", "decal", "projectile", "enchant", "alchemy", "shout")):
        return "MagicFX"
    if base_name.startswith(("BSScript::", "GFx", "Scaleform::")) or any(k in low for k in ("menu", "hud", "ui", "papyrus")):
        return "ScriptingUI"
    if any(k in low for k in ("audio", "sound", "xaudio")):
        return "Audio"
    return "CoreEngine"


# ---------------------------------------------------------------------------
# RTTI & Vtable Extraction Engine
# ---------------------------------------------------------------------------

@dataclass
class RTTIClassRecord:
    class_name: str
    mangled_name: str
    vtable_rva: int
    col_rva: int
    mi_offset: int
    method_count: int
    first_methods: List[int]
    inheritance_chain: List[str]
    subsystem: str
    is_primary: bool


class MsvcRttiExtractor:
    """Scans PE64 .rdata/.data/.text for 100% self-consistent MSVC x64 RTTI and vtables."""

    def __init__(self, pe: PE64Image) -> None:
        self.pe = pe
        self._td_cache: Dict[int, Tuple[str, str]] = {}  # td_rva -> (mangled, demangled)
        self._col_map: Dict[int, Tuple[int, int, int, int]] = {}  # col_rva -> (offset, cd_off, td_rva, chd_rva)
        self._chd_cache: Dict[int, List[str]] = {}  # chd_rva -> [base names...]

    def _read_type_descriptor(self, td_rva: int) -> Optional[Tuple[str, str]]:
        cached = self._td_cache.get(td_rva)
        if cached is not None:
            return cached
        # TypeDescriptor: +0x00 pVFTable (8), +0x08 spare (8), +0x10 char name[]
        name_rva = td_rva + 0x10
        mangled = self.pe.read_cstr(name_rva, 512)
        if not mangled or not mangled.startswith(".?"):
            return None
        demangled = demangle_msvc_rtti(mangled)
        res = (mangled, demangled)
        self._td_cache[td_rva] = res
        return res

    def _read_inheritance_chain(self, chd_rva: int) -> List[str]:
        cached = self._chd_cache.get(chd_rva)
        if cached is not None:
            return cached
        raw = self.pe.read_bytes(chd_rva, 16)
        if raw is None:
            return []
        sig, _attrs, num_bases, bca_rva = struct.unpack("<IIII", raw)
        if sig != 0 or num_bases == 0 or num_bases > 256:
            return []
        bca_raw = self.pe.read_bytes(bca_rva, num_bases * 4)
        if bca_raw is None:
            return []
        chain: List[str] = []
        seen: Set[str] = set()
        for i in range(num_bases):
            bcd_rva = struct.unpack_from("<I", bca_raw, i * 4)[0]
            td_rva = self.pe.read_u32(bcd_rva)
            if td_rva is None:
                continue
            td = self._read_type_descriptor(td_rva)
            if td is not None:
                name = td[1]
                if name not in seen:
                    seen.add(name)
                    chain.append(name)
        self._chd_cache[chd_rva] = chain
        return chain

    def extract_all(self, include_secondary: bool = False) -> Tuple[List[RTTIClassRecord], Dict[str, int]]:
        """
        Two-pass fast scan:
          Pass 1: Scan all non-exec readable sections (.rdata) on 4-byte alignment for
                  RTTICompleteObjectLocator where signature == 1 and pSelf == col_rva,
                  verifying pTypeDescriptor points to a valid '.?' TypeDescriptor.
          Pass 2: Scan .rdata on 8-byte alignment for QWORDs matching ImageBase + col_rva.
                  Each hit at rva_meta implies a vtable at vtable_rva = rva_meta + 8!
                  Count consecutive .text pointers to determine method_count and first 12 RVAs.
        """
        t0 = time.perf_counter()
        pe = self.pe
        img_base = pe.image_base
        size_of_image = pe.size_of_image

        # Collect candidate sections for COL & vtables (.rdata and any read-only data section)
        scan_sections = pe.rdata_sections if pe.rdata_sections else [s for s in pe.sections if s.is_read and not s.is_exec]

        # Pass 1: Discover all valid RTTICompleteObjectLocator structs
        col_rva_to_va: Dict[int, int] = {}
        col_va_set: Set[int] = set()
        col_bytes_rvas: Set[int] = set()

        for sec in scan_sections:
            base_off = sec.raw_ptr
            base_rva = sec.virtual_address
            raw_len = sec.raw_size
            buf = pe.data
            # Scan 4-byte aligned: look for signature == 1 (little-endian 0x00000001) AND pSelf == col_rva
            # Fast inner loop using struct.iter_unpack or step 4
            end_off = base_off + raw_len - 24
            off = base_off
            while off <= end_off:
                sig = struct.unpack_from("<I", buf, off)[0]
                if sig == 1:
                    col_rva = base_rva + (off - base_off)
                    p_self = struct.unpack_from("<I", buf, off + 20)[0]
                    if p_self == col_rva:
                        offset, cd_off, p_td, p_chd = struct.unpack_from("<IIII", buf, off + 4)
                        if offset < 0x100000 and cd_off < 0x100000 and 0 < p_td < size_of_image and 0 < p_chd < size_of_image:
                            td = self._read_type_descriptor(p_td)
                            if td is not None:
                                self._col_map[col_rva] = (offset, cd_off, p_td, p_chd)
                                va = img_base + col_rva
                                col_rva_to_va[col_rva] = va
                                col_va_set.add(va)
                                col_bytes_rvas.add(col_rva)
                                off += 24
                                continue
                off += 4

        # Pass 2: Find all vtable meta-pointers (vtable_rva - 8 == ImageBase + col_rva)
        meta_hits: List[Tuple[int, int]] = []  # (vtable_rva, col_rva)
        meta_rva_set: Set[int] = set()

        for sec in scan_sections:
            base_off = sec.raw_ptr
            base_rva = sec.virtual_address
            raw_len = sec.raw_size
            buf = pe.data
            # Align to 8 bytes relative to section start
            n_qwords = raw_len // 8
            qwords = struct.unpack_from(f"<{n_qwords}Q", buf, base_off)
            for idx, qv in enumerate(qwords):
                if qv in col_va_set:
                    meta_rva = base_rva + idx * 8
                    vtable_rva = meta_rva + 8
                    # Confirm slot 0 points into an executable section (.text)
                    if idx + 1 < n_qwords:
                        first_fn_va = qwords[idx + 1]
                        if first_fn_va > img_base:
                            fn_rva = first_fn_va - img_base
                            if pe.is_exec_rva(fn_rva):
                                col_rva = qv - img_base
                                meta_hits.append((vtable_rva, col_rva))
                                meta_rva_set.add(meta_rva)

        # Build records by counting consecutive .text method pointers for each vtable
        records: List[RTTIClassRecord] = []
        primary_seen: Set[str] = set()
        total_primary = 0
        total_secondary = 0

        for vtable_rva, col_rva in meta_hits:
            offset, _cd_off, p_td, p_chd = self._col_map[col_rva]
            is_primary = (offset == 0)
            if is_primary:
                total_primary += 1
            else:
                total_secondary += 1
                if not include_secondary:
                    continue

            td = self._read_type_descriptor(p_td)
            if td is None:
                continue
            mangled, demangled = td
            chain = self._read_inheritance_chain(p_chd)

            # Count virtual methods in .text until non-.text entry or boundary of another meta/COL
            methods: List[int] = []
            slot = 0
            while slot < 1024:
                slot_rva = vtable_rva + slot * 8
                if slot > 0 and (slot_rva in meta_rva_set or slot_rva in col_bytes_rvas):
                    break
                va = pe.read_u64(slot_rva)
                if va is None or va <= img_base:
                    break
                fn_rva = va - img_base
                if not pe.is_exec_rva(fn_rva):
                    break
                methods.append(fn_rva)
                slot += 1

            if not methods:
                continue

            display_name = demangled if is_primary else f"{demangled}+0x{offset:X}"
            if is_primary and display_name in primary_seen:
                # Disambiguate rare duplicate anonymous/same-name primary vtables by COL RVA
                display_name = f"{demangled}@0x{vtable_rva:X}"
            if is_primary:
                primary_seen.add(display_name)

            subsystem = classify_subsystem(demangled, chain)
            records.append(
                RTTIClassRecord(
                    class_name=display_name,
                    mangled_name=mangled,
                    vtable_rva=vtable_rva,
                    col_rva=col_rva,
                    mi_offset=offset,
                    method_count=len(methods),
                    first_methods=methods[:12],
                    inheritance_chain=chain,
                    subsystem=subsystem,
                    is_primary=is_primary,
                )
            )

        records.sort(key=lambda r: (r.subsystem, r.class_name, r.vtable_rva))
        elapsed_ms = int((time.perf_counter() - t0) * 1000)
        stats = {
            "total_cols": len(self._col_map),
            "total_vtables_found": len(meta_hits),
            "primary_vtables": total_primary,
            "secondary_vtables": total_secondary,
            "emitted_records": len(records),
            "elapsed_ms": elapsed_ms,
        }
        return records, stats


# ---------------------------------------------------------------------------
# Disassembler & CFG Inspector (--disasm <rva> --bytes N)
# ---------------------------------------------------------------------------

def _try_read_printable_ascii(pe: PE64Image, rva: int, min_len: int = 4, max_len: int = 80) -> Optional[str]:
    raw = pe.read_bytes(rva, max_len)
    if not raw:
        return None
    out = bytearray()
    for b in raw:
        if b == 0:
            break
        if 0x20 <= b <= 0x7E:
            out.append(b)
        else:
            return None
    if len(out) >= min_len:
        return out.decode("ascii")
    return None


def disassemble_rva(
    pe: PE64Image,
    rva: int,
    num_bytes: int,
    vtable_names: Optional[Dict[int, str]] = None,
) -> str:
    """Disassembles `num_bytes` starting at `rva` with Capstone, annotating RIP-rel refs and CFG edges."""
    code = pe.read_bytes(rva, num_bytes)
    if code is None:
        raise ValueError(f"RVA 0x{rva:X} is not mapped in binary sections")

    lines: List[str] = []
    bounds = pe.pdata_func_bounds(rva)
    if bounds:
        lines.append(
            f"; Function .pdata bounds: [0x{bounds[0]:X} .. 0x{bounds[1]:X}) "
            f"(size={bounds[1] - bounds[0]} bytes, offset=+0x{rva - bounds[0]:X})"
        )
    else:
        lines.append(f"; Disassembly at RVA 0x{rva:X} ({num_bytes} bytes, no .pdata entry)")

    if not HAS_CAPSTONE:
        lines.append("; [capstone not installed — hex dump fallback]")
        for i in range(0, len(code), 16):
            chunk = code[i : i + 16]
            hex_s = " ".join(f"{b:02X}" for b in chunk)
            lines.append(f"  0x{rva + i:08X}: {hex_s}")
        return "\n".join(lines)

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    insns = list(md.disasm(code, pe.image_base + rva))

    # Build CFG basic block leaders and call/branch targets
    leaders: Set[int] = {rva}
    calls: List[Tuple[int, int]] = []
    branches: List[Tuple[int, int, str]] = []
    rip_refs: List[Tuple[int, int, str]] = []

    for ins in insns:
        ins_rva = ins.address - pe.image_base
        mnem = ins.mnemonic.lower()
        is_branch = mnem.startswith("j") or mnem == "call"
        if is_branch and len(ins.operands) == 1 and ins.operands[0].type == capstone.x86.X86_OP_IMM:
            tgt_rva = ins.operands[0].imm - pe.image_base
            if mnem == "call":
                calls.append((ins_rva, tgt_rva))
            else:
                branches.append((ins_rva, tgt_rva, mnem))
                if rva <= tgt_rva < rva + num_bytes:
                    leaders.add(tgt_rva)
                next_rva = ins_rva + ins.size
                if rva <= next_rva < rva + num_bytes:
                    leaders.add(next_rva)

    for ins in insns:
        ins_rva = ins.address - pe.image_base
        if ins_rva in leaders:
            lines.append(f"\nloc_{ins_rva:08X}:")
        note_parts: List[str] = []
        # Check RIP-relative memory operand
        for op in ins.operands:
            if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
                target_rva = (ins.address + ins.size + op.mem.disp) - pe.image_base
                tag = f"RVA 0x{target_rva:X}"
                if vtable_names and target_rva in vtable_names:
                    tag += f" (vtable `{vtable_names[target_rva]}`)"
                else:
                    mnem_l = ins.mnemonic.lower()
                    if mnem_l in ("movss", "comiss", "ucomiss", "addss", "subss", "mulss", "divss", "maxss", "minss"):
                        raw_f = pe.read_bytes(target_rva, 4)
                        if raw_f:
                            u32 = struct.unpack("<I", raw_f)[0]
                            f32 = struct.unpack("<f", raw_f)[0]
                            tag += f" (f32={f32:.7g}, 0x{u32:08X})"
                    elif mnem_l in ("movsd", "comisd", "addsd", "subsd", "mulsd", "divsd"):
                        raw_d = pe.read_bytes(target_rva, 8)
                        if raw_d:
                            f64 = struct.unpack("<d", raw_d)[0]
                            tag += f" (f64={f64:.10g})"
                    else:
                        s = _try_read_printable_ascii(pe, target_rva)
                        if s:
                            tag += f' "{s}"'
                note_parts.append(tag)
                rip_refs.append((ins_rva, target_rva, tag))
            elif op.type == capstone.x86.X86_OP_IMM and ins.mnemonic.lower() in ("call", "jmp"):
                target_rva = op.imm - pe.image_base
                if 0 < target_rva < pe.size_of_image:
                    note_parts.append(f"target RVA 0x{target_rva:X}")

        bytes_hex = " ".join(f"{b:02X}" for b in ins.bytes)
        note = ("  ; " + " | ".join(note_parts)) if note_parts else ""
        lines.append(f"  0x{ins_rva:08X}  {bytes_hex:<28}  {ins.mnemonic:<8} {ins.op_str}{note}")

    lines.append("\n; --- CFG Summary ---")
    lines.append(f"; Instructions: {len(insns)}, Basic Blocks: {len(leaders)}, Direct Calls: {len(calls)}, Branches: {len(branches)}, RIP-Refs: {len(rip_refs)}")
    if calls:
        uniq_calls = sorted({t for _, t in calls})
        lines.append("; Callees: " + ", ".join(f"0x{t:X}" for t in uniq_calls[:24]))
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# RIP-Relative Cross-Reference Scanner (--xrefs <rva>)
# ---------------------------------------------------------------------------

def scan_rip_xrefs(pe: PE64Image, target_rva: int, max_hits: int = 64) -> List[Tuple[int, str, Optional[int]]]:
    """
    Finds x86-64 RIP-relative LEA/MOV/CMP/CALL/JMP references in .text pointing to `target_rva`.
    Uses fast byte-pattern scan for disp32 matching `target_rva - next_rip` across .text.
    """
    hits: List[Tuple[int, str, Optional[int]]] = []
    # Common RIP-relative opcodes with ModRM (mod=00, rm=101 -> (modrm & 0xC7) == 0x05):
    # 48 8D xx (LEA r64, [rip+disp32]), 4C 8D xx, 48 8B xx (MOV), 4C 8B xx, 48 89 xx, 8B xx, E8 (CALL rel32), E9 (JMP rel32)
    for sec in pe.text_sections:
        buf = pe.data[sec.raw_ptr : sec.raw_ptr + sec.raw_size]
        base_rva = sec.virtual_address
        n = len(buf)
        # Fast scan for 7-byte REX + 8D/8B/89 + ModRM(rip) + disp32, plus 5-byte E8/E9 rel32
        i = 0
        while i + 5 <= n:
            b0 = buf[i]
            # Direct CALL / JMP rel32
            if b0 in (0xE8, 0xE9):
                rel = struct.unpack_from("<i", buf, i + 1)[0]
                if base_rva + i + 5 + rel == target_rva:
                    ins_rva = base_rva + i
                    fn = pe.pdata_func_bounds(ins_rva)
                    hits.append((ins_rva, "call" if b0 == 0xE8 else "jmp", fn[0] if fn else None))
                    if len(hits) >= max_hits:
                        return hits
            # REX.W (0x48..0x4F) + LEA(0x8D) / MOV(0x8B, 0x89) + ModRM(rip)
            elif 0x48 <= b0 <= 0x4F and i + 7 <= n:
                b1 = buf[i + 1]
                if b1 in (0x8D, 0x8B, 0x89) and (buf[i + 2] & 0xC7) == 0x05:
                    disp = struct.unpack_from("<i", buf, i + 3)[0]
                    if base_rva + i + 7 + disp == target_rva:
                        ins_rva = base_rva + i
                        fn = pe.pdata_func_bounds(ins_rva)
                        op = {0x8D: "lea", 0x8B: "mov_load", 0x89: "mov_store"}[b1]
                        hits.append((ins_rva, op, fn[0] if fn else None))
                        if len(hits) >= max_hits:
                            return hits
            i += 1
    return hits


# ---------------------------------------------------------------------------
# TSV Writer & CLI
# ---------------------------------------------------------------------------

CORE_TARGETS = [
    # World & Streaming
    "TES", "TESObjectCELL", "TESWorldSpace", "GridCellArray",
    "BGSTerrainManager", "BGSTerrainNode", "BGSTerrainChunk",
    "BGSStoryTeller", "TESObjectLAND", "TESLandTexture",
    # Water & Sky
    "TESWaterSystem", "TESWaterObject", "Sky", "TESWeather",
    "TESClimate", "Clouds", "Moon", "Stars", "Sun",
    # Scene Graph & Rendering
    "ShadowSceneNode", "BSCullingProcess", "BSGeometry", "BSTriShape",
    "BSDynamicTriShape", "BSLightingShaderProperty", "BSEffectShaderProperty",
    "BSFadeNode", "NiCamera", "NiNode", "NiAVObject", "BSLight", "BSShadowLight",
    # Camera & Actors
    "PlayerCamera", "FreeCameraState", "FirstPersonState", "ThirdPersonState",
    "Actor", "PlayerCharacter", "TESObjectREFR", "ProcessLists",
    # Memory, Jobs & IO
    "MemoryManager", "ScrapHeap", "TaskQueueInterface", "IOManager",
    "BSResource::LooseFileLocation", "BSResource::ArchiveStream",
    # Physics & Havok
    "bhkWorld", "bhkWorldM", "bhkCollisionObject", "bhkRigidBody",
    # Magic & FX
    "MagicSystem", "ActiveEffect", "BSParticleShaderEmitter",
    "NiParticleSystem", "BGSDecalManager",
]


def write_rtti_tsv(
    out_path: str,
    pe: PE64Image,
    records: List[RTTIClassRecord],
    stats: Dict[str, int],
) -> None:
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(
            f"# RAW Creation Engine RTTI & Vtable Catalog — generated directly from {os.path.basename(pe.path)}\n"
            f"# timestamp=0x{pe.timestamp:08X} size_of_image=0x{pe.size_of_image:X} image_base=0x{pe.image_base:X} "
            f"total_cols={stats['total_cols']} primary_vtables={stats['primary_vtables']} "
            f"secondary_vtables={stats['secondary_vtables']} emitted={stats['emitted_records']}\n"
        )
        f.write("class_name\tvtable_rva\tcol_rva\tmethod_count\tfirst_methods_hex\tinheritance_chain\tsubsystem\n")
        for r in records:
            methods_hex = ",".join(f"0x{m:X}" for m in r.first_methods)
            chain_str = " -> ".join(r.inheritance_chain)
            f.write(
                f"{r.class_name}\t0x{r.vtable_rva:X}\t0x{r.col_rva:X}\t"
                f"{r.method_count}\t{methods_hex}\t{chain_str}\t{r.subsystem}\n"
            )


def parse_hex_or_int(val: str) -> int:
    val = val.strip()
    return int(val, 16) if val.lower().startswith("0x") else int(val, 10)


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description="RAW Creation Engine PE64 MSVC RTTI / Vtable / Xref / Disassembly Extractor"
    )
    parser.add_argument(
        "--binary",
        default=r"H:\SteamLibrary\steamapps\common\Skyrim Special Edition\SkyrimSE.exe",
        help="Path to SkyrimSE.exe PE64 binary",
    )
    parser.add_argument(
        "--out-tsv",
        default="",
        help="Write full verified RTTI class & vtable catalog to this TSV file",
    )
    parser.add_argument(
        "--include-secondary",
        action="store_true",
        help="Include multiple-inheritance secondary vtables (col.offset > 0)",
    )
    parser.add_argument(
        "--disasm",
        default="",
        help="Disassemble function/code at hex RVA (e.g. 0x2BEE00)",
    )
    parser.add_argument(
        "--bytes",
        type=int,
        default=128,
        help="Number of bytes to disassemble with --disasm (default: 128)",
    )
    parser.add_argument(
        "--xrefs",
        default="",
        help="Scan .text for RIP-relative xrefs to hex RVA (e.g. 0x18240C8)",
    )
    parser.add_argument(
        "--query",
        default="",
        help="Filter and print extracted RTTI classes matching regex/substring",
    )
    parser.add_argument(
        "--summary",
        action="store_true",
        help="Print subsystem counts and core Creation Engine target class table",
    )

    args = parser.parse_args(argv)
    if not os.path.isfile(args.binary):
        print(f"ERROR: Binary not found: {args.binary}", file=sys.stderr)
        return 2

    pe = PE64Image(args.binary)
    print(
        f"[ce_decompiler] Loaded {os.path.basename(pe.path)} "
        f"(size={pe.size:,} B, TS=0x{pe.timestamp:08X}, ImageBase=0x{pe.image_base:X}, "
        f"SizeOfImage=0x{pe.size_of_image:X}, .pdata={len(pe.pdata_begins):,} funcs, "
        f"capstone={'ON' if HAS_CAPSTONE else 'OFF'})"
    )

    vtable_names: Dict[int, str] = {}
    records: List[RTTIClassRecord] = []
    stats: Dict[str, int] = {}

    need_rtti = bool(args.out_tsv or args.query or args.summary or (not args.disasm and not args.xrefs))
    if need_rtti:
        extractor = MsvcRttiExtractor(pe)
        records, stats = extractor.extract_all(include_secondary=args.include_secondary)
        vtable_names = {r.vtable_rva: r.class_name for r in records}
        print(
            f"[ce_decompiler] Extracted {stats['emitted_records']:,} vtables "
            f"(primary={stats['primary_vtables']:,}, secondary={stats['secondary_vtables']:,}, "
            f"total_COLs={stats['total_cols']:,}) in {stats['elapsed_ms']} ms"
        )

    if args.out_tsv:
        write_rtti_tsv(args.out_tsv, pe, records, stats)
        print(f"[ce_decompiler] Wrote verified catalog -> {args.out_tsv}")

    if args.summary:
        by_sub: Dict[str, int] = {}
        by_name: Dict[str, RTTIClassRecord] = {}
        for r in records:
            by_sub[r.subsystem] = by_sub.get(r.subsystem, 0) + 1
            if r.is_primary and r.class_name not in by_name:
                by_name[r.class_name] = r
        print("\n=== Subsystem Class Counts ===")
        for sub, cnt in sorted(by_sub.items(), key=lambda kv: -kv[1]):
            print(f"  {sub:<20} {cnt:>6}")

        print("\n=== Core Creation Engine Target Classes ===")
        found = 0
        missing: List[str] = []
        for tgt in CORE_TARGETS:
            rec = by_name.get(tgt)
            if rec:
                found += 1
                m0 = f"0x{rec.first_methods[0]:X}" if rec.first_methods else "-"
                chain_preview = " -> ".join(rec.inheritance_chain[:4])
                print(
                    f"  {rec.class_name:<32} vtable=0x{rec.vtable_rva:07X}  col=0x{rec.col_rva:07X}  "
                    f"slots={rec.method_count:>3}  slot0={m0:<10}  [{rec.subsystem}]  {chain_preview}"
                )
            else:
                missing.append(tgt)
        print(f"\nCore target hit rate: {found}/{len(CORE_TARGETS)}")
        if missing:
            print("Not found as exact primary RTTI name (may be non-polymorphic struct/namespace): " + ", ".join(missing))

    if args.query:
        pat = re.compile(args.query, re.IGNORECASE)
        matches = [r for r in records if pat.search(r.class_name)]
        print(f"\n=== Query '{args.query}' ({len(matches)} matches) ===")
        for r in matches[:80]:
            print(
                f"  {r.class_name:<44} vtable=0x{r.vtable_rva:07X} col=0x{r.col_rva:07X} "
                f"slots={r.method_count:>3} [{r.subsystem}] chain={' -> '.join(r.inheritance_chain[:5])}"
            )

    if args.disasm:
        rva = parse_hex_or_int(args.disasm)
        print("\n" + disassemble_rva(pe, rva, args.bytes, vtable_names if vtable_names else None))

    if args.xrefs:
        rva = parse_hex_or_int(args.xrefs)
        hits = scan_rip_xrefs(pe, rva)
        print(f"\n=== RIP-Relative Xrefs to 0x{rva:X} ({len(hits)} hits) ===")
        for ins_rva, op, fn_start in hits:
            fn_s = f"in func 0x{fn_start:X}" if fn_start is not None else "no .pdata"
            print(f"  0x{ins_rva:08X}  {op:<10}  ({fn_s})")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
