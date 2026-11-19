"""Live state map: engine singletons, the fields worth streaming, and what
skyrimbridge already reads.

Field layouts come from CommonLibSSE-NG member offset comments. A field
counts as captured by skyrimbridge when its member name is accessed
(`->name` or `.name`) anywhere in skyrimbridge's source, or when the whole
class is read through a domain listed in DOMAIN_COVER. That is a name
match, so it is labelled inferred.
"""
import os
import re

# label: (header file, class key, why it matters, singleton name in CommonLib)
CLASSES = {
    "Main": ("Main.h", "Main", "frame loop, pause, focus", "Main::GetSingleton"),
    "Calendar": ("Calendar.h", "Calendar", "game time and date", "Calendar::GetSingleton"),
    "Sky": ("Sky.h", "Sky", "weather, sun, moons, sky colours", "Sky::GetSingleton"),
    "Sun": ("Sun.h", "Sun", "sun disc and light", ""),
    "Moon": ("Moon.h", "Moon", "Masser and Secunda", ""),
    "Clouds": ("Clouds.h", "Clouds", "cloud layers", ""),
    "Precipitation": ("Precipitation.h", "Precipitation", "rain and snow", ""),
    "TESWeather": ("TESWeather.h", "TESWeather", "weather record (current, last)", ""),
    "TESClimate": ("TESClimate.h", "TESClimate", "climate record", ""),
    "PlayerCamera": ("PlayerCamera.h", "PlayerCamera", "camera state", "PlayerCamera::GetSingleton"),
    "TESCamera": ("TESCamera.h", "TESCamera", "camera node and state", ""),
    "NiCamera": ("NiCamera.h", "NiCamera", "view frustum, world-to-camera", ""),
    "PlayerCharacter": ("PlayerCharacter.h", "PlayerCharacter", "player", "PlayerCharacter::GetSingleton"),
    "TES": ("TES.h", "TES", "loaded cell grid, LOD roots", "TES::GetSingleton"),
    "TESObjectCELL": ("TESObjectCELL.h", "TESObjectCELL", "current cell", ""),
    "ShaderManagerState": ("BSShaderManager.h", "BSShaderManager::State",
                           "renderer timers, scene roots, ambient", "BSShaderManager::State::GetSingleton"),
    "GraphicsState": ("State.h", "State", "BSGraphics frame state, camera data", "BSGraphics::State::GetSingleton"),
    "RendererData": ("Renderer.h", "RendererData", "device, swap chain, render targets", ""),
    "ShadowSceneNode": ("ShadowSceneNode.h", "ShadowSceneNode", "light lists, shadow casters", ""),
    "ImageSpaceManager": ("ImageSpaceManager.h", "ImageSpaceManager", "post effects state", "ImageSpaceManager::GetSingleton"),
    "UI": ("UI.h", "UI", "menus open, pause", "UI::GetSingleton"),
    "ProcessLists": ("ProcessLists.h", "ProcessLists", "high, middle, low actor lists", "ProcessLists::GetSingleton"),
    "VirtualMachine": ("VirtualMachine.h", "VirtualMachine",
                       "Papyrus VM state (stacks, queues); reached through SkyrimVM", "SkyrimVM::Singleton"),
}
# skyrimbridge domains (src/core/BridgeData.h) that read a class wholesale
DOMAIN_COVER = {
    "Calendar": "CelestialData.TimeData",
    "Sky": "Atmosphere, Fog, Weather, Celestial",
    "PlayerCamera": "CameraData",
    "Precipitation": "WeatherData.PrecipLive",
}
STREAM = re.compile(r"(time|hour|day|weather|sun|moon|wind|color|colour|fog|"
                    r"pos|angle|rot|fov|frustum|cell|grid|light|shadow|camera|"
                    r"state|flag|pct|percent|current|last|alpha|ambient|"
                    r"timer|interior|range|speed|world|matrix|transform|"
                    r"precip|cloud|aurora|water|stack|queue|frame|loaded)", re.I)


def read_bridge_source(root):
    chunks = []
    for dirpath, _d, files in os.walk(os.path.join(root, "src")):
        for fn in files:
            if fn.endswith((".cpp", ".h")):
                with open(os.path.join(dirpath, fn), encoding="utf-8",
                          errors="replace") as f:
                    chunks.append(f.read())
    return "\n".join(chunks)


def rows(members, bridge_src, singletons):
    out = []
    for label, (_f, key, why, single) in CLASSES.items():
        fields = members.get(label, [])
        s_rva = singletons.get(single.replace("::GetSingleton", "").replace("::Singleton", ""), "")
        if not fields:
            out.append({"class": key, "field": "(no member offsets in CommonLib)",
                        "offset": "", "type": "", "stream": "", "singleton": s_rva,
                        "skyrimbridge": "", "why": why})
            continue
        for off, typ, name, note in fields:
            if name.startswith(("pad", "unk")) and not note:
                continue
            if re.fullmatch(r"[A-Z0-9_]+", name):
                continue  # a CommonLib layout macro, not a field
            bare = name.split("[")[0]
            hit = re.search(r"(->|\.)" + re.escape(bare) + r"\b", bridge_src)
            cover = "yes (name match)" if hit else (
                f"domain: {DOMAIN_COVER[label]}" if label in DOMAIN_COVER else "GAP")
            out.append({
                "class": key, "field": name, "offset": f"0x{off:03X}",
                "type": typ, "stream": "yes" if STREAM.search(name) else "",
                "singleton": s_rva, "skyrimbridge": cover,
                "why": why if off == fields[0][0] else note,
            })
    return out
