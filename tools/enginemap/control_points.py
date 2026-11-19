"""Engine control points for RAW, resolved against the built map.

Each control point names what to hook or patch, how, and the risk. Targets
are regular expressions over map names (functions, vtables, globals) and
setting names; the tool writes every match with its address, so the plan
moves with the map. Nothing here runs in the game.

Approaches:
  data patch   write a Setting value or a global after the INI load and
               before the system reads it; no code change
  vtable swap  replace one slot in a class's vtable (in .rdata) with ours,
               calling through to the original
  detour       a cooperative jump at a function entry (chained, so other
               DLL mods that hooked first keep working)
"""
import re

CONTROLS = [
    {
        "control": "shadow distance and cascades",
        "approach": "data patch",
        "risk": "low: plain floats read each frame; cascade split from "
                "fShadowDistance and iNumSplits",
        "settings": [r"^fShadowDistance:Display$", r"^fInteriorShadowDistance:",
                     r"^iNumSplits:Display$", r"^fShadowBiasScale:",
                     r"^fShadowDirectionalBiasScale:", r"^fSunShadowUpdateTime:",
                     r"^iNumFocusShadow:", r"^fMaxFocusShadowMapDistance:",
                     r"^bDrawLandShadows:", r"^bTreesReceiveShadows:",
                     r"^bShadowsOnGrass:"],
        "map": [],
    },
    {
        "control": "shadow cascade matrices (replace the split computation)",
        "approach": "vtable swap",
        "risk": "medium: slot purposes are unnamed in CommonLib (Unk_09/0A/0D); "
                "pin each slot with an in-process trace before swapping",
        "settings": [],
        "map": [r"^BSShadowDirectionalLight::", r"^BSShadowLight::",
                r"^ShadowSceneNode::OnVisible$", r"^ShadowSceneNode::vftable$",
                r"^BSShadowDirectionalLight::vftable$"],
    },
    {
        "control": "shadow map resolution",
        "approach": "data patch before renderer init, plus detour on render "
                    "target creation for a live change",
        "risk": "high: the shadow atlas is allocated once from "
                "iShadowMapResolution; a live change needs every view and "
                "the cascade viewports rebuilt",
        "settings": [r"^iShadowMapResolution:", r"^iShadowMaskQuarter:",
                     r"^bCreateShadowRenderTarget:"],
        "map": [r"^Renderer::CreateRenderTexture$", r"^Renderer::Init$",
                r"RenderTargetManager"],
    },
    {
        "control": "LOD distances past the INI limits",
        "approach": "data patch (before worldspace load), then a detour on "
                    "the terrain block selection if the engine clamps",
        "risk": "medium: larger distances raise LOD memory past the "
                "BudgetCaps values; the clamp sites are in encrypted code "
                "and still to be found in process",
        "settings": [r"^fBlockLevel[01]Distance:", r"^fBlockMaximumDistance:",
                     r"^fTreeLoadDistance:", r"^fSplitDistanceMult:",
                     r"^fLODFadeOutMult", r"^fSkyCellRefFadeDistance",
                     r"^uLargeRefLODGridSize:", r"^fMeshLODLevel[12]Fade",
                     r"^fTreesMidLODSwitchDist:", r"LODBudgetAdjustment"],
        "map": [r"^BSFadeNode::", r"^BSMultiBoundNode::vftable$",
                r"^BSLODTriShape::vftable$", r"^BSMeshLODTriShape::vftable$",
                r"^TESWorldSpace::GetSkyCell$"],
    },
    {
        "control": "draw batching and GPU-driven culling",
        "approach": "vtable swap on the accumulator and culling process; "
                    "our submission takes the collected passes",
        "risk": "high: the batch renderer owns pass ordering, alpha sorting "
                "and per-technique state; slots 01 to 03 are unnamed",
        "settings": [],
        "map": [r"^BSBatchRenderer::", r"^BSShaderAccumulator::",
                r"^BSCullingProcess::", r"^NiCullingProcess::vftable$",
                r"^Renderer::Begin$", r"^Renderer::End$",
                r"^BSLightingShaderProperty::GetRenderPasses$"],
    },
    {
        "control": "cell and asset streaming",
        "approach": "data patch for budgets and grid size; detour on IO task "
                    "scheduling for priorities",
        "risk": "medium to high: uGridsToLoad above 5 is known to destabilise "
                "saves; IO runs on worker threads, so hooks need the "
                "engine's own locks",
        "settings": [r"^uGridsToLoad:", r"^iPreloadSizeLimit:",
                     r"^bBackgroundCellLoads:", r"^iPostProcessMilliseconds",
                     r"^bUseBackgroundFileLoader:", r"^uExterior Cell Buffer",
                     r"BudgetCaps"],
        "map": [r"^IOManager::", r"^BSTaskManager::", r"^QueuedReference::",
                r"^QueuedFile::vftable$", r"^TES::", r"^GridCellArray::",
                r"^CellLoaderTask::"],
    },
    {
        "control": "Papyrus VM budget (adaptive per-frame time)",
        "approach": "data patch: rewrite fUpdateBudgetMS and "
                    "fExtraTaskletBudgetMS each frame from measured spare time",
        "risk": "low for the values; medium for behaviour: longer budgets "
                "lengthen the main-thread frame",
        "settings": [r":Papyrus$"],
        "map": [r"^SkyrimVM::Singleton$"],
    },
    {
        "control": "Papyrus VM dispatch (measure, then replace)",
        "approach": "vtable swap on VirtualMachine::Update and "
                    "UpdateTasklets for timing; detour inside CodeTasklet for "
                    "the opcode loop once it is located",
        "risk": "high: the interpreter loop itself is not named by any "
                "source; it sits behind CodeTasklet and must be found in "
                "process; save-game state depends on exact semantics",
        "settings": [],
        "map": [r"^BSScript::Internal::VirtualMachine::(Update|UpdateTasklets|"
                r"TasksToJobs|BindNativeMethod|SetOverstressed|vftable)$",
                r"^BSScript::Internal::CodeTasklet::", r"^SkyrimVM::",
                r"^BSScript::NF_util::", r"^BSScript::IFunction::vftable$"],
    },
]


def resolve(map_rows, settings):
    seen, uniq = set(), []
    for m in map_rows:
        if (m["name"], m["rva"]) not in seen:
            seen.add((m["name"], m["rva"]))
            uniq.append(m)
    map_rows = uniq
    out = []
    for c in CONTROLS:
        for rx in c["map"]:
            r = re.compile(rx)
            hits = [m for m in map_rows if r.search(m["name"])]
            for m in hits[:40]:
                out.append(_row(c, m["name"], m["rva"], m["kind"],
                                m["confidence"], m.get("check", "")))
            if not hits:
                out.append(_row(c, f"(no match: {rx})", "", "", "unknown", ""))
        for rx in c["settings"]:
            r = re.compile(rx)
            hits = [s for s in settings if r.search(s["name"])]
            for s in hits[:40]:
                out.append(_row(c, s["name"], s["rva"], f"setting ({s['file']})",
                                "named", s["type"]))
            if not hits:
                out.append(_row(c, f"(no match: {rx})", "", "", "unknown", ""))
    return out


def _row(c, target, rva, kind, conf, note):
    return {"control": c["control"], "target": target, "rva": rva,
            "kind": kind, "confidence": conf, "approach": c["approach"],
            "risk": c["risk"], "notes": note}
