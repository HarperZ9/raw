"""Assign an engine name (class, function or setting) to a subsystem.

Rules are ordered; the first match wins. They test the class part of a
name (text before the first '::' after any 'RE::' prefix) and, for
settings, the INI section and key.
"""
import re

RULES = [
    ("papyrus", r"^(BSScript|SkyrimVM|SkyrimScript|BSTEventSource<BSScript|"
                r"VirtualMachine|CodeTasklet|IFunction|NativeFunction|"
                r"IObjectHandlePolicy|IVMObjectBindInterface|IStackCallbackFunctor|"
                r"Papyrus|ScriptEventSourceHolder|SCRIPT_FUNCTION|Script\b|"
                r"ConsoleLog|BSTEventSink<BSScript|^\.\?AV\?\$\w*NativeFunction|"
                r"@BSScript@@)"),
    ("water", r"^(TESWater|BSWater|WaterShader|Water(?!Path)|WadingWater|"
              r"BGSWaterCollision|ImageSpaceEffectWater|ImageSpaceEffectRefraction|"
              r"BSImagespaceShaderWater|BSImagespaceShaderRefraction|ExtraWater|"
              r"BSWaterShader)"),
    ("sky_weather", r"^(Sky|SkyObject|Atmosphere|Clouds|Moon|Stars|Sun|"
                    r"Precipitation|TESWeather|TESClimate|BGSLensFlare|"
                    r"BGSVolumetricLighting|TESImageSpace|ImageSpaceModifier|"
                    r"TESImageSpaceModifier|Calendar|BSSkyShader|SkyShader|"
                    r"BSSky|Aurora|Weather|Climate)"),
    ("lighting_shadows", r"(Shadow|^BSLight|^NiLight|^NiPointLight|"
                         r"^NiDirectionalLight|^NiAmbientLight|^NiSpotLight|"
                         r"^BSLensFlare|LightingTemplate|^BSPortal|Occlusion|"
                         r"^BSLightingShaderProperty(?!Material))"),
    ("lod", r"(LOD|Lod|^BSTerrain|^BGSTerrain|TerrainManager|^BSDistant|"
            r"^BSTree|^BGSDistantTree|DistantTree|^BGSObjectBlock|^BSMultiBound|"
            r"^BSCombinedObjects|^BGSLargeRef|SpeedTree|^BSFadeNode|"
            r"^BSLODMultiIndex|^TerrainNode|Landscape)"),
    ("shaders", r"(^BS\w*Shader(?!Manager)|ShaderProperty|ShaderMaterial|"
                r"^BSShaderTextureSet|^BSTextureSet|^BSEffect|^BSParticleShader|"
                r"^BSGrass|^BSUtilityShader|^BSImagespaceShader|^BSComputeShader|"
                r"^BSXFlags|^BSReloadShaderI|Technique)"),
    ("renderer", r"(^BSGraphics|^BSShaderManager|^BSRenderPass|^BSBatchRenderer|"
                 r"^BSShaderAccumulator|^ImageSpace|^BSImagespace|^BSRenderManager|"
                 r"^BSShaderRenderTargets|^RenderTarget|^BSCubeMapCamera|"
                 r"^BSRenderer|^Renderer|^BSDynamicTriShape|^BSD3D|^BSTextureDB|"
                 r"^BSRenderTarget|^BSTextureManager|Display\b|^BSCullingProcess|"
                 r"^NiCullingProcess|^BSShader\b|^BSScissor|^NiCamera|^Main::Draw|"
                 r"^BSPrecomputedLight|^BSOcclusion)"),
    ("scenegraph", r"(^NiNode|^NiAVObject|^NiObject|^NiRefObject|^NiObjectNET|"
                   r"^NiGeometry|^NiTriShape|^NiTriBasedGeom|^NiTransform|"
                   r"^NiBound|^NiSkin|^NiExtraData|^NiProperty|^NiAlphaProperty|"
                   r"^NiTimeController|^NiControllerManager|^NiSwitchNode|"
                   r"^NiBillboardNode|^NiStream|^NiRTTI|^BSGeometry|^BSTriShape|"
                   r"^BSSubIndexTriShape|^BSInstanceTriShape|^BSLines|"
                   r"^BSMultiIndexTriShape|^BSLeafAnimNode|^BSOrderedNode|"
                   r"^BSValueNode|^BSNiNode|^BSMasterParticleSystem|^NiParticle|"
                   r"^BSSceneGraph|^SceneGraph|^NiUpdateData|^BSFaceGenNiNode|"
                   r"^NiCollisionObject|^bhkNiCollisionObject|^BSInvMarker|"
                   r"^BSXFlags|^BSBound|^NiIntegerExtraData|^NiStringExtraData)"),
    ("streaming", r"(^BSResource|^IOManager|^BSTaskManager|^IOTask|^QueuedFile|"
                  r"^QueuedReference|^QueuedTree|^QueuedHead|^BSArchive|^BSFile|"
                  r"^BSStream|^TES\b|^TES::|^TESObjectCELL|^GridCellArray|"
                  r"^GridArray|^BSModelDB|^BSFaceGenDB|^LoadedCellDB|^TESDataHandler|"
                  r"^BSResourceNiBinaryStream|^Loader|^BSTextureStreamer|"
                  r"^BackgroundProcess|^TESWorldSpace|^BGSLoadFormBuffer|"
                  r"^BGSSaveLoad|^ExteriorCellLoader|^CellLoad|^BSCellAttach|"
                  r"Streaming|^BSJobs|^BSSpinLock|^BSThread)"),
]
_COMPILED = [(name, re.compile(rx)) for name, rx in RULES]

SETTING_SECTIONS = {
    "lighting_shadows": {"Display:Shadow"},
    "water": {"Water"},
    "lod": {"LOD", "TerrainManager", "Terrain", "SpeedTree", "Trees", "Grass"},
    "sky_weather": {"Weather", "Sky", "Clouds"},
    "papyrus": {"Papyrus"},
    "streaming": {"BackgroundLoad", "General:Grids", "Archive", "BudgetCaps"},
    "renderer": {"Display", "ImageSpace", "Imagespace", "Decals", "BlurShader",
                 "BlurShaderHDR", "LightingShader", "MapMenu"},
}


def class_part(name):
    n = name[4:] if name.startswith("RE::") else name
    for pre in ("RTTI_", "NiRTTI_", "VTABLE_"):
        if n.startswith(pre):
            n = n[len(pre):]
            break
    return n


def subsystem(name):
    n = class_part(name)
    for sub, rx in _COMPILED:
        if rx.search(n):
            return sub
    return None


def setting_subsystem(key, section):
    k = key.lower()
    if "shadow" in k:
        return "lighting_shadows"
    if "water" in k or section == "Water":
        return "water"
    if "lod" in k or section in SETTING_SECTIONS["lod"] or "tree" in k:
        return "lod"
    if section == "Papyrus":
        return "papyrus"
    if any(w in k for w in ("weather", "sky", "cloud", "sun", "moon",
                            "masser", "secunda", "aurora", "precip")):
        return "sky_weather"
    if section in ("BackgroundLoad", "Archive", "BudgetCaps") or "grid" in k:
        return "streaming"
    if section in SETTING_SECTIONS["renderer"]:
        return "renderer"
    return None
