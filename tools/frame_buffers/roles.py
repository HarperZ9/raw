"""Names the buffers of a captured frame from how the game used them.

A capture lists textures by number, and numbers change between runs. A role comes from
facts in the manifest: the format, the size against the backbuffer, which targets were
bound together, what was copied into what, and the order of the passes. Each rule states
its evidence; a texture no rule covers stays "unassigned" with its facts.

Original work, MIT.
"""
from __future__ import annotations

RGBA16F, RG16F, RGBA8, RG8, D24S8, R16T, R16F, R32F, RGB10A2, R11G11B10F = 10, 34, 28, 49, 44, 53, 54, 41, 24, 26

ROLE_TEXT = {
    "raw_albedo": ("RAW albedo", "RAW's own target: base colour before lighting, written by the patched shaders."),
    "raw_specular": ("RAW specular", "RAW's own target: specular mask, gloss, material class."),
    "raw_emissive": ("RAW emissive", "RAW's own target: emitted light."),
    "raw_layer": ("RAW layer", "RAW's own target: depth and normal (or alpha) of the nearest water or blended surface."),
    "backbuffer": ("Frame (backbuffer)", "The finished frame: the game's final blit, then the HUD on top."),
    "scene_hdr": ("Scene colour, HDR", "Lit scene in linear light. Opaque pass target 0; blended draws, the game's own AO, fog and bloom land here later."),
    "motion_vectors": ("Motion vectors", "Opaque pass target 1. Screen-space velocity: (current NDC - previous NDC) * (-0.5, 0.5), from the game's lighting shader."),
    "normals_masks": ("Normals and masks", "Opaque pass target 2. RG: view-space normal, n.xy / sqrt(8 - 8 n.z) + 0.5. A: specular mask the game's reflections read. B: a small per-material value."),
    "snow_mask": ("Two-channel opaque mask", "Opaque pass target 3. G marks snow-shaded surfaces in this frame; R stays near zero."),
    "depth_main": ("Depth and stencil", "The scene's depth buffer (24-bit, 0 near, 1 far) and 8-bit stencil."),
    "depth_copy_prepass": ("Depth after the depth prepass", "A copy of the depth buffer taken right after the prepass; later bound as depth for a few late draws."),
    "depth_copy_scene": ("Depth after the scene", "A copy of the depth buffer taken after the last scene draw, for the post chain."),
    "shadow_mask": ("Shadow mask", "One full-screen draw before the opaque pass. R: 1 lit, 0 in the sun's shadow."),
    "shadow_cascades": ("Sun shadow cascades", "16-bit depth, one array layer per cascade, drawn before the scene."),
    "shadow_maps_small": ("Second shadow map set", "16-bit depth array at low resolution, drawn before the cascades."),
    "reflection_cube": ("Reflection cube map", "Six HDR faces; the game refreshes some faces each frame."),
    "reflection_cube_depth": ("Reflection cube depth", "Depth and stencil for the cube map faces."),
    "linear_depth_pyramid": ("View depth pyramid", "View depth as float with a full mip chain, built for the game's own ambient occlusion."),
    "volumetric_volume": ("Volumetric lighting volume", "3D float texture written by compute dispatches."),
    "volumetric_resolve": ("Volumetric lighting, screen", "Full-size single-channel float: the volume resolved to the screen (draw, then compute filters)."),
    "water_target": ("Water colour target", "HDR target drawn with the scene depth; holds the visible water surface in this frame."),
    "water_normals_mask": ("Water mask target", "10-bit target written together with the scene colour by one draw; alpha marks the water area."),
    "blend_mask": ("Blended-draw mask", "Two-channel target bound with the scene colour for every blended draw."),
    "blend_colour": ("Blended-draw colour", "HDR target bound with the scene colour for every blended draw (clouds, mist, effects)."),
    "scene_hdr_copy": ("Scene colour copy", "A copy of the scene colour taken before the blended draws; the source for refraction."),
    "half_normals": ("Normals and masks, half size", "Downsample of the normal target for the game's reflections."),
    "ssr": ("Screen-space reflections", "Half-size HDR targets of the game's reflection passes (trace, then filters)."),
    "game_ao": ("The game's ambient occlusion", "Full-size 8-bit targets of the game's own AO: packed result, then two blur passes."),
    "post_hdr": ("HDR post chain", "Downsampled HDR targets: bloom and blur steps."),
    "luminance": ("Luminance chain", "Shrinking HDR targets down to a few pixels: the exposure measurement."),
    "post_ldr": ("Display-referred post chain", "8-bit targets after the tone map: tone-mapped frame, temporal anti-aliasing outputs, reduced copies."),
    "scene_r11": ("Scene colour, packed float", "One full-screen draw into an R11G11B10 target between the game's AO and its composite."),
    "unassigned": ("Unassigned", "No rule covers this target yet."),
}


RAW_SLOTS = {4: "raw_albedo", 5: "raw_specular", 6: "raw_emissive", 7: "raw_layer"}


def split_raw_targets(manifest: dict) -> dict[int, str]:
    """RAW's own targets sit in slots 4 to 7 of the spans it extends (docs/FRAME_BUFFERS.md,
    "Shader patches"). This names them and trims every span's list back to the game's own
    targets, in place, so the rules below see the frame the game drew. The first-person span
    keeps the game's normal target RAW binds in slot 2."""
    own: dict[int, str] = {}
    for iv in manifest["intervals"]:
        if iv.get("raw_split"):
            own.update(iv["raw_targets"]); continue
        extra = {r["tex"]: RAW_SLOTS[k] for k, r in enumerate(iv["rt"]) if k >= 4 and r["tex"] >= 0}
        iv["raw_targets"] = extra
        iv["raw_added_normal"] = bool(extra) and len(iv["rt"]) > 3 and iv["rt"][3]["tex"] < 0 and iv["rt"][2]["tex"] >= 0
        iv["rt"] = [r for r in iv["rt"][:4] if r["tex"] >= 0]
        iv["raw_split"] = True
        own.update(extra)
    return own


def assign(manifest: dict) -> dict[int, str]:
    raw_own = split_raw_targets(manifest)
    tex = {t["id"]: t for t in manifest["textures"]}
    bw, bh = manifest["backbuffer"]
    ivs = [iv for iv in manifest["intervals"] if iv["draws"] > 0]
    role: dict[int, str] = dict(raw_own)

    def full(t): return t["dim"] == 2 and t["array"] == 1 and t["w"] == bw and t["h"] == bh
    def put(i, name):
        if i is not None and i >= 0: role.setdefault(i, name)

    # The opaque set: four full-size targets, HDR + RG16F + RGBA8 + RG8, with depth.
    opaque = next((iv for iv in ivs if len(iv["rt"]) >= 4 and
                   [tex[r["tex"]]["format"] for r in iv["rt"][:4]] == [RGBA16F, RG16F, RGBA8, RG8]), None)
    scene = depth = None
    if opaque:
        scene, depth = opaque["rt"][0]["tex"], opaque["ds"]["tex"]
        for r, name in zip(opaque["rt"], ("scene_hdr", "motion_vectors", "normals_masks", "snow_mask")):
            put(r["tex"], name)
        put(depth, "depth_main")
    if ivs:
        put(ivs[-1]["rt"][0]["tex"], "backbuffer")

    # Copies.
    seen_scene_copy = False
    for e in manifest["events"]:
        ev = e["event"]
        if "copy" not in ev: continue
        if ev["from"] == depth:
            put(ev["copy"], "depth_copy_prepass" if opaque and e["after"] < opaque["seq"] else "depth_copy_scene")
        if ev["from"] == scene and not seen_scene_copy:
            put(ev["copy"], "scene_hdr_copy"); seen_scene_copy = True

    for iv in ivs:
        rts = [r["tex"] for r in iv["rt"]]
        fmts = [tex[i]["format"] for i in rts]
        ds = iv["ds"]["tex"]
        if rts and tex[rts[0]]["misc"] & 4:
            put(rts[0], "reflection_cube"); put(ds, "reflection_cube_depth")
        if not rts and ds >= 0 and tex[ds]["format"] == R16T:
            put(ds, "shadow_cascades" if tex[ds]["w"] >= 2048 else "shadow_maps_small")
        if scene is not None and rts[:1] == [scene] and len(rts) == 3 and fmts[1:] == [RG8, RGBA16F]:
            put(rts[1], "blend_mask"); put(rts[2], "blend_colour")
        if scene is not None and rts[:1] == [scene] and len(rts) == 2 and fmts[1] == RGB10A2:
            put(rts[1], "water_normals_mask")
        if len(rts) == 1 and ds == depth and depth is not None and fmts[0] == RGBA16F and rts[0] != scene:
            put(rts[0], "water_target")

    cleared_red = {e["event"]["tex"] for e in manifest["events"]
                   if e["event"].get("clear") == "target" and e["event"]["rgba"][:3] == [1, 0, 0]}
    for i, t in tex.items():
        if i in role or not t["written"]: continue
        f = t["format"]
        if t["dim"] == 3: put(i, "volumetric_volume")
        elif f == R32F and t["mips"] > 1: put(i, "linear_depth_pyramid")
        elif full(t) and f == RGBA8 and i in cleared_red: put(i, "shadow_mask")
        elif full(t) and f == R16F: put(i, "volumetric_resolve")
        elif full(t) and f == R11G11B10F: put(i, "scene_r11")
    # What is left, by where it sits in the frame and by size.
    order = {i: _first_write(manifest, i) for i in tex}
    tonemap_at = min((order[i] for i, t in tex.items() if i not in role and full(t) and t["format"] == RGBA8 and
                      order[i] > (opaque["seq"] if opaque else 0) + 60), default=10**9)
    for i, t in sorted(tex.items(), key=lambda kv: order[kv[0]]):
        if i in role or not t["written"]: continue
        f = t["format"]
        if f == RGBA8 and order[i] >= tonemap_at: put(i, "post_ldr")
        elif f == RGBA8 and full(t): put(i, "game_ao")
        elif f == RGBA8: put(i, "half_normals" if t["w"] * 2 == bw and order[i] < tonemap_at else "post_ldr")
        elif f == RGBA16F and t["w"] * 2 == bw and order[i] < tonemap_at and _near_opaque(order[i], opaque): put(i, "ssr")
        elif f == RGBA16F and t["w"] <= 64: put(i, "luminance")
        elif f == RGBA16F: put(i, "post_hdr")
        else: put(i, "unassigned")
    return role


def _first_write(manifest: dict, i, skip: int = 0) -> int:
    """Sequence number of the first interval, copy or dispatch that writes texture i."""
    if i is None: return 10**9
    hits = [iv["seq"] for iv in manifest["intervals"] if iv["draws"] > 0 and
            (i in [r["tex"] for r in iv["rt"]] or iv["ds"]["tex"] == i)]
    for e in manifest["events"]:
        ev = e["event"]
        if ev.get("copy") == i or i in ev.get("uav", []): hits.append(e["after"])
    hits.sort()
    return hits[skip] if len(hits) > skip else 10**9


def _near_opaque(seq: int, opaque) -> bool:
    return bool(opaque) and 0 < seq - opaque["seq"] < 60
