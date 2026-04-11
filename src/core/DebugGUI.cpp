#include "DebugGUI.h"
#include "D3D11Hook.h"
#include "EditorIDCache.h"
#include "FeedbackProcessor.h"
#include "SceneObserver.h"
#include "ShaderCache.h"
#include "ShaderPreProcessor.h"
#include "ExternBindingProcessor.h"
#include "WeatherSeparationEngine.h"
#include "ParameterBindingEngine.h"
#include "SharedMemoryBridge.h"
#include "WeatherParameterComputer.h"
#include "WriteBackProcessor.h"
#include "WeatherEditor.h"
#include "SB_ShaderDebug.h"
#include "DebugRenderer.h"
#include "NavMeshVisualizer.h"
#include "SkeletonVisualizer.h"
#include "FeatureManager.h"
#include "SystemHealth.h"
#include "CompatibilityProbe.h"
#include "ProxyDiagnostics.h"
// Renderer systems for Renderers tab
#include "GTAORenderer.h"
#include "ContactShadowRenderer.h"
#include "SkylightingRenderer.h"
#include "SSRRenderer.h"
#include "SSGIRenderer.h"
#include "HiZPyramid.h"
#include "LuminanceHistogram.h"
#include "ClusteredLighting.h"
#include "SDSMCascades.h"
#include "TAAManager.h"
#include "LUTManager.h"
#include "VolumetricClouds.h"
#include "FrameGenerator.h"
#include "TemporalSuperRes.h"
#include "GrassLightingRenderer.h"
#include "TreeLODLightingRenderer.h"
#include "WaterBlendingRenderer.h"
#include "DynamicCubemapRenderer.h"
#include "SceneCompositor.h"
#include "AtmosphereRenderer.h"
#include "ToneMapManager.h"
#include "GPUProfiler.h"
#include "FrameCapture.h"
#include "PhaseDispatcher.h"
// New compute systems (Tier 5)
#include "VolumetricLightingRenderer.h"
#include "SubsurfaceScatteringRenderer.h"
#include "IndirectSpecularRenderer.h"
#include "ScreenSpaceDecalRenderer.h"
#include "ParticleLightingRenderer.h"
#include <imgui.h>
#include <cstring>
#include <cmath>
#include <cstdio>

namespace SB::DebugGUI
{
    // ── Internal state ───────────────────────────────────────────────────
    static AllData s_data{};
    static AllData s_prevData{};
    static bool s_hasPrevData = false;
    static bool s_initialized = false;
    static bool s_windowOpen = true;
    static bool s_dataPushEnabled = true;

    // Per-domain dirty flags (set each frame by comparing current vs prev)
    static bool s_domainDirty[22] = {};

    // Parameter search filter
    static char s_searchFilter[64] = "";

    // ── Sparkline history ────────────────────────────────────────────────
    static constexpr int kHistoryLen = 120;  // ~2s at 60fps, used for sparkline graphs
    struct RingBuffer {
        float data[kHistoryLen] = {};
        int   head = 0;
        float minVal = 0.0f, maxVal = 1.0f;
        int   rangeAge = 0;  // frames since last full scan

        void Push(float v) {
            data[head] = v;
            head = (head + 1) % kHistoryLen;
            // Fast incremental update: expand range, age triggers full rescan
            if (v < minVal) minVal = v;
            if (v > maxVal) maxVal = v;
            ++rangeAge;
        }
        // Getter for ImGui::PlotLines (index 0 = oldest)
        static float Getter(void* ctx, int idx) {
            auto* rb = static_cast<RingBuffer*>(ctx);
            return rb->data[(rb->head + idx) % kHistoryLen];
        }
        void UpdateRange() {
            // Full rescan every ~2 seconds (kHistoryLen frames) to shrink stale range
            if (rangeAge < kHistoryLen) return;
            rangeAge = 0;
            minVal = data[0]; maxVal = data[0];
            for (float v : data) {
                if (v < minVal) minVal = v;
                if (v > maxVal) maxVal = v;
            }
            if (maxVal - minVal < 0.001f) maxVal = minVal + 0.001f;
        }
    };
    static RingBuffer s_fpsHistory;
    static RingBuffer s_frameTimeHistory;
    static RingBuffer s_luminanceHistory;

    // ── Game State Editor state ──────────────────────────────────────────
    struct GameEditState {
        bool  fovActive = false;
        float fov = 65.0f;           // degrees
        bool  fogNearActive = false;
        float fogNearDist = 0.0f;
        bool  fogFarActive = false;
        float fogFarDist = 50000.0f;
        bool  fogDensityActive = false;
        float fogPower = 0.75f;
        float fogMaxOpacity = 1.0f;
        bool  hdrActive = false;
        float hdrAdaptSpeed = 1.0f;
        float hdrBloomScale = 1.0f;
        float hdrBloomThresh = 0.5f;
        float hdrSunScale = 1.0f;
        bool  cinematicActive = false;
        float cineSat = 1.0f;
        float cineBright = 1.0f;
        float cineContrast = 1.0f;
        float cineTintAmount = 0.0f;
        bool  tintActive = false;
        float tintColor[3] = {1.0f, 1.0f, 1.0f};
        bool  dofActive = false;
        float dofStrength = 0.0f;
        float dofDist = 1000.0f;
        float dofRange = 500.0f;
        bool  sunlightActive = false;
        float sunlightColor[3] = {1.0f, 1.0f, 1.0f};
        bool  ambientActive = false;
        float ambientColor[3] = {0.2f, 0.2f, 0.25f};
        bool  timescaleActive = false;
        float timescale = 20.0f;
    };
    static GameEditState s_gameEdit;

    // Tracker health data (fed from main.cpp)
    static constexpr int kMaxTrackers = 32;
    static TrackerHealthInfo s_trackerHealthInfo[kMaxTrackers] = {};
    static int s_trackerHealthCount = 0;

    // Shader pre-processor stats
    static PreProcessorStats s_ppStats = {};

    // ── Weather Transition Analyzer history ──────────────────────────────
    static constexpr int kWeatherHistoryLen = 240;  // ~4 seconds at 60fps
    struct WeatherRing {
        float data[kWeatherHistoryLen] = {};
        int head = 0;
        void Push(float v) { data[head] = v; head = (head + 1) % kWeatherHistoryLen; }
        static float Getter(void* ctx, int idx) {
            auto* r = static_cast<WeatherRing*>(ctx);
            return r->data[(r->head + idx) % kWeatherHistoryLen];
        }
    };
    static WeatherRing s_wxTemp;          // "temperature" proxy from sun elevation
    static WeatherRing s_wxWind;          // wind speed
    static WeatherRing s_wxPrecip;        // precipitation intensity
    static WeatherRing s_wxFogNear;       // fog near distance
    static WeatherRing s_wxFogFar;        // fog far distance
    static WeatherRing s_wxSunElev;       // sun elevation
    static WeatherRing s_wxAmbientLum;    // ambient luminance
    static WeatherRing s_wxTransition;    // weather transition %

    // ── Shader Source Viewer state ───────────────────────────────────────
    static int  s_shaderViewIdx  = -1;         // selected attempt index
    static char s_shaderFilter[64] = "";       // search filter

    // ENB .fx disk browser
    struct FxFileEntry {
        std::string name;               // e.g. "enbeffect.fx"
        std::filesystem::path path;     // full path on disk
        std::string source;             // loaded + #include resolved
        bool loaded = false;
        int  techCount = 0;
        int  lineCount = 0;
    };
    static std::vector<FxFileEntry>  s_fxFiles;
    static int                       s_fxSelIdx = -1;
    static bool                      s_fxScanned = false;
    static std::filesystem::path     s_enbDir;    // enbseries/ root

    // Resolve #include directives recursively, return expanded source
    static std::string ResolveIncludes(
        const std::filesystem::path& filePath,
        const std::filesystem::path& baseDir,
        std::unordered_set<std::string>& visited,
        int depth = 0)
    {
        if (depth > 50) return "// #include depth limit reached\n";
        auto key = filePath.string();
        if (visited.count(key)) return "// (already included: " + key + ")\n";
        visited.insert(key);

        std::ifstream ifs(filePath, std::ios::binary);
        if (!ifs.is_open())
            return "// Failed to open: " + filePath.string() + "\n";

        std::string result;
        std::string line;
        while (std::getline(ifs, line)) {
            // Trim \r
            if (!line.empty() && line.back() == '\r') line.pop_back();

            // Check for #include "..." or #include <...>
            auto pos = line.find("#include");
            if (pos != std::string::npos) {
                auto q1 = line.find('"', pos + 8);
                auto q2 = (q1 != std::string::npos) ? line.find('"', q1 + 1) : std::string::npos;
                if (q2 == std::string::npos) {
                    // Try angle brackets
                    q1 = line.find('<', pos + 8);
                    q2 = (q1 != std::string::npos) ? line.find('>', q1 + 1) : std::string::npos;
                }
                if (q1 != std::string::npos && q2 != std::string::npos) {
                    std::string incName = line.substr(q1 + 1, q2 - q1 - 1);
                    // Try relative to current file first, then baseDir
                    auto incPath = filePath.parent_path() / incName;
                    if (!std::filesystem::exists(incPath))
                        incPath = baseDir / incName;
                    // Also try Helper/ and Addons/ subdirs
                    if (!std::filesystem::exists(incPath))
                        incPath = baseDir / "Helper" / incName;
                    if (!std::filesystem::exists(incPath))
                        incPath = baseDir / "Addons" / incName;
                    if (!std::filesystem::exists(incPath))
                        incPath = baseDir / "UI" / incName;

                    result += "// >>> #include \"" + incName + "\" (" +
                              incPath.filename().string() + ")\n";
                    result += ResolveIncludes(incPath, baseDir, visited, depth + 1);
                    result += "// <<< end #include \"" + incName + "\"\n";
                    continue;
                }
            }
            result += line + '\n';
        }
        return result;
    }

    // Scan enbseries directory for .fx files
    static void ScanFxFiles()
    {
        s_fxFiles.clear();
        s_fxSelIdx = -1;
        s_fxScanned = true;

        // Determine enbseries path from exe directory
        char exePath[MAX_PATH];
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        s_enbDir = std::filesystem::path(exePath).parent_path() / "enbseries";

        if (!std::filesystem::exists(s_enbDir))
            return;

        // Collect .fx files
        for (auto& entry : std::filesystem::directory_iterator(s_enbDir)) {
            if (!entry.is_regular_file()) continue;
            auto ext = entry.path().extension().string();
            // Lowercase compare
            for (auto& c : ext) c = static_cast<char>(::tolower(c));
            if (ext == ".fx") {
                FxFileEntry fe;
                fe.name = entry.path().filename().string();
                fe.path = entry.path();
                s_fxFiles.push_back(std::move(fe));
            }
        }
        // Sort alphabetically
        std::sort(s_fxFiles.begin(), s_fxFiles.end(),
            [](const FxFileEntry& a, const FxFileEntry& b) {
                return a.name < b.name;
            });
    }

    // Load and resolve a single .fx file
    static void LoadFxFile(FxFileEntry& fe)
    {
        std::unordered_set<std::string> visited;
        fe.source = ResolveIncludes(fe.path, s_enbDir, visited);
        fe.loaded = true;

        // Count lines
        fe.lineCount = static_cast<int>(
            std::count(fe.source.begin(), fe.source.end(), '\n'));

        // Count techniques
        fe.techCount = 0;
        std::string::size_type pos = 0;
        while ((pos = fe.source.find("technique", pos)) != std::string::npos) {
            if (pos == 0 || !isalnum(static_cast<unsigned char>(fe.source[pos - 1])))
                ++fe.techCount;
            pos += 9;
        }
    }

    // ── Light Inspector state ───────────────────────────────────────────
    static bool s_lightInspectorWorldView = false;

    // ── Helpers ──────────────────────────────────────────────────────────

    static void F4(const char* label, const Float4& v)
    {
        ImGui::Text("%s: (%.3f, %.3f, %.3f, %.3f)", label, v.x, v.y, v.z, v.w);
    }

    static void F4Color(const char* label, const Float4& v)
    {
        ImVec4 color(v.x, v.y, v.z, 1.0f);
        ImGui::ColorButton(label, color, ImGuiColorEditFlags_NoTooltip, ImVec2(14, 14));
        ImGui::SameLine();
        ImGui::Text("%s: (%.2f, %.2f, %.2f)", label, v.x, v.y, v.z);
    }

    static void Matrix4(const char* label, const Float4x4& m)
    {
        if (ImGui::TreeNode(label)) {
            ImGui::Text("| %8.3f %8.3f %8.3f %8.3f |", m.row[0].x, m.row[0].y, m.row[0].z, m.row[0].w);
            ImGui::Text("| %8.3f %8.3f %8.3f %8.3f |", m.row[1].x, m.row[1].y, m.row[1].z, m.row[1].w);
            ImGui::Text("| %8.3f %8.3f %8.3f %8.3f |", m.row[2].x, m.row[2].y, m.row[2].z, m.row[2].w);
            ImGui::Text("| %8.3f %8.3f %8.3f %8.3f |", m.row[3].x, m.row[3].y, m.row[3].z, m.row[3].w);
            ImGui::TreePop();
        }
    }

    static void Bool(const char* label, float v) {
        bool on = v > 0.5f;
        ImVec4 col = on ? ImVec4(0.4f, 0.85f, 0.5f, 1.0f) : ImVec4(0.50f, 0.50f, 0.50f, 0.7f);
        ImGui::TextColored(col, "%s: %s", label, on ? "Yes" : "No");
    }

    // Inline bool badge (compact, for use with SameLine)
    static void BoolTag(const char* label, float v) {
        bool on = v > 0.5f;
        ImVec4 col = on ? ImVec4(0.4f, 0.85f, 0.5f, 1.0f) : ImVec4(0.45f, 0.45f, 0.45f, 0.6f);
        ImGui::TextColored(col, "%s", label);
        ImGui::SameLine(0, 8);
    }

    // Section header with inline dirty indicator on the header line itself
    static bool SectionHeader(const char* label, int domain)
    {
        bool open = ImGui::CollapsingHeader(label);
        if (domain >= 0 && domain < 22 && s_domainDirty[domain]) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), " *");
        }
        return open;
    }

    // Update per-domain dirty flags by memcmping domain structs
    static void UpdateDirtyFlags()
    {
        if (!s_hasPrevData) {
            for (auto& d : s_domainDirty) d = true;
            return;
        }
        s_domainDirty[0]  = std::memcmp(&s_data.celestial,   &s_prevData.celestial,   sizeof(CelestialData))   != 0;
        s_domainDirty[1]  = std::memcmp(&s_data.atmosphere,  &s_prevData.atmosphere,  sizeof(AtmosphereData))  != 0;
        s_domainDirty[2]  = std::memcmp(&s_data.fog,         &s_prevData.fog,         sizeof(FogData))         != 0;
        s_domainDirty[3]  = std::memcmp(&s_data.weather,     &s_prevData.weather,     sizeof(WeatherData))     != 0;
        s_domainDirty[4]  = std::memcmp(&s_data.player,      &s_prevData.player,      sizeof(PlayerData))      != 0;
        s_domainDirty[5]  = std::memcmp(&s_data.camera,      &s_prevData.camera,      sizeof(CameraData))      != 0;
        s_domainDirty[6]  = std::memcmp(&s_data.interior,    &s_prevData.interior,    sizeof(InteriorData))    != 0;
        s_domainDirty[7]  = std::memcmp(&s_data.shadow,      &s_prevData.shadow,      sizeof(ShadowData))      != 0;
        s_domainDirty[8]  = std::memcmp(&s_data.effects,     &s_prevData.effects,     sizeof(EffectsData))     != 0;
        s_domainDirty[9]  = std::memcmp(&s_data.render,      &s_prevData.render,      sizeof(RenderData))      != 0;
        s_domainDirty[10] = std::memcmp(&s_data.imageSpace,  &s_prevData.imageSpace,  sizeof(ImageSpaceData))  != 0;
        s_domainDirty[11] = std::memcmp(&s_data.lights,      &s_prevData.lights,      sizeof(LightData))       != 0;
        s_domainDirty[12] = std::memcmp(&s_data.actorValues, &s_prevData.actorValues, sizeof(ActorValueData))  != 0;
        s_domainDirty[13] = std::memcmp(&s_data.crosshair,   &s_prevData.crosshair,   sizeof(CrosshairData))   != 0;
        s_domainDirty[14] = std::memcmp(&s_data.equipment,   &s_prevData.equipment,   sizeof(EquipmentData))   != 0;
        s_domainDirty[15] = std::memcmp(&s_data.quest,       &s_prevData.quest,       sizeof(QuestData))       != 0;
        s_domainDirty[16] = std::memcmp(&s_data.uiState,     &s_prevData.uiState,     sizeof(UIStateData))     != 0;
        s_domainDirty[17] = std::memcmp(&s_data.feedback,    &s_prevData.feedback,    sizeof(FeedbackData))    != 0;
        s_domainDirty[18] = std::memcmp(&s_data.region,      &s_prevData.region,      sizeof(RegionData))      != 0;
        s_domainDirty[19] = std::memcmp(&s_data.audio,       &s_prevData.audio,       sizeof(AudioData))       != 0;
        s_domainDirty[20] = std::memcmp(&s_data.npcDetect,   &s_prevData.npcDetect,   sizeof(NPCDetectData))   != 0;
        s_domainDirty[21] = std::memcmp(&s_data.perf,        &s_prevData.perf,        sizeof(PerfData))        != 0;
    }

    // ══════════════════════════════════════════════════════════════════════
    //  STATUS DASHBOARD
    // ══════════════════════════════════════════════════════════════════════

    static void RenderStatusDashboard()
    {
        auto& sc = SB::ShaderCache::Get();
        auto& fb = SB::FeedbackProcessor::Get();
        auto& sm = SB::SharedMemoryBridge::Get();

        // Top bar: ENB status + FPS
        float fps = s_data.render.FrameInfo.y > 0.0001f ? 1.0f / s_data.render.FrameInfo.y : 0.0f;
        ImVec4 fpsColor = fps >= 50.f ? ImVec4(0.35f, 0.85f, 0.55f, 1.0f)
                        : fps >= 30.f ? ImVec4(0.90f, 0.75f, 0.30f, 1.0f)
                                      : ImVec4(0.90f, 0.35f, 0.30f, 1.0f);

        ImGui::TextColored(fpsColor, "%.0f fps", fps);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.50f, 0.52f, 0.58f, 1.0f), "|");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.65f, 0.70f, 0.78f, 1.0f), "%.2f ms", s_data.render.FrameInfo.y * 1000.0f);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.50f, 0.52f, 0.58f, 1.0f), "|");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.35f, 0.37f, 0.42f, 1.0f), "Standalone");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.50f, 0.52f, 0.58f, 1.0f), "|");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.55f, 0.60f, 0.70f, 1.0f), "F%.0f", s_data.render.FrameInfo.x);


        // Subsystem badges (pill-style with tooltips)
        auto StatusPill = [](const char* name, bool active, const char* tooltip) {
            ImVec4 col = active
                ? ImVec4(0.25f, 0.70f, 0.45f, 1.0f)
                : ImVec4(0.35f, 0.37f, 0.42f, 1.0f);
            ImGui::TextColored(col, "%s", name);
            if (ImGui::IsItemHovered() && tooltip)
                ImGui::SetTooltip("%s", tooltip);
            ImGui::SameLine(0, 10);
        };

        StatusPill("Feedback", fb.IsInitialized(),
            fb.IsInitialized() ? "GPU readback active (5x5 grid + center pixel)" : "GPU readback not initialized");
        StatusPill("Cache", sc.IsEnabled(),
            sc.IsEnabled() ? "Shader bytecode cache (disk + memory)" : "Shader cache disabled");
        StatusPill("SharedMem", sm.IsActive(),
            sm.IsActive() ? "Shared memory bridge for external tools" : "Shared memory inactive");
        uint32_t wbCount = SB::WriteBackProcessor::Get().GetEnabledRuleCount();
        char wbTip[64];
        snprintf(wbTip, sizeof(wbTip), "%u write-back rules active", wbCount);
        StatusPill("WriteBack", wbCount > 0, wbTip);
        ImGui::NewLine();

        // ShaderCache one-liner
        if (sc.IsEnabled()) {
            uint32_t total = sc.GetHitCount() + sc.GetMissCount();
            float hitRate = total > 0 ? (100.0f * sc.GetHitCount() / total) : 0.0f;
            ImGui::TextColored(ImVec4(0.50f, 0.55f, 0.65f, 1.0f),
                "Cache: %u/%u (%.0f%%) %u stored",
                sc.GetHitCount(), total, hitRate, sc.GetStoreCount());
        }

        // ── Sparklines ──────────────────────────────────────────────────
        s_fpsHistory.Push(fps);
        s_frameTimeHistory.Push(s_data.render.FrameInfo.y * 1000.0f);
        float lum = (s_data.feedback.Scene.w > 0.5f) ? s_data.feedback.Luminance.x : 0.0f;
        s_luminanceHistory.Push(lum);

        s_fpsHistory.UpdateRange();
        s_frameTimeHistory.UpdateRange();
        s_luminanceHistory.UpdateRange();

        ImGui::Spacing();
        float sparkW = ImGui::GetContentRegionAvail().x;

        char fpsOverlay[32];
        snprintf(fpsOverlay, sizeof(fpsOverlay), "FPS: %.0f", fps);
        ImGui::PlotLines("##fps", RingBuffer::Getter, &s_fpsHistory, kHistoryLen,
            0, fpsOverlay, s_fpsHistory.minVal * 0.9f, s_fpsHistory.maxVal * 1.1f,
            ImVec2(sparkW, 32));

        char ftOverlay[32];
        snprintf(ftOverlay, sizeof(ftOverlay), "%.2f ms", s_data.render.FrameInfo.y * 1000.0f);
        ImGui::PlotLines("##ft", RingBuffer::Getter, &s_frameTimeHistory, kHistoryLen,
            0, ftOverlay, 0.0f, s_frameTimeHistory.maxVal * 1.2f,
            ImVec2(sparkW, 32));

        if (s_data.feedback.Scene.w > 0.5f) {
            char lumOverlay[32];
            snprintf(lumOverlay, sizeof(lumOverlay), "Lum: %.4f", lum);
            ImGui::PlotLines("##lum", RingBuffer::Getter, &s_luminanceHistory, kHistoryLen,
                0, lumOverlay, 0.0f, s_luminanceHistory.maxVal * 1.2f,
                ImVec2(sparkW, 32));
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }

    // ══════════════════════════════════════════════════════════════════════
    //  DOMAIN SECTIONS (1-22)
    // ══════════════════════════════════════════════════════════════════════

    static void Sec_Celestial()
    {
        if (!SectionHeader("Celestial", 0)) return;
        const auto& c = s_data.celestial;

        ImGui::Text("Sun:");
        ImGui::Indent();
        F4("Dir", c.SunDirection);
        F4Color("Color", c.SunColor);
        ImGui::Unindent();

        ImGui::Text("Masser: dir(%.2f,%.2f,%.2f) phase=%.2f",
            c.MasserDirection.x, c.MasserDirection.y, c.MasserDirection.z, c.MasserDirection.w);
        ImGui::Text("Secunda: dir(%.2f,%.2f,%.2f) phase=%.2f",
            c.SecundaDirection.x, c.SecundaDirection.y, c.SecundaDirection.z, c.SecundaDirection.w);

        ImGui::Separator();
        ImGui::Text("Time: %.2f hrs (%.0f%%)", c.TimeData.x, c.TimeData.w * 100.0f);
        ImGui::Text("Sunrise: %.2f  Sunset: %.2f", c.TimeData.y, c.TimeData.z);
        F4("Segments1", c.TimeSegments1);
        F4("Segments2", c.TimeSegments2);
    }

    static void Sec_Atmosphere()
    {
        if (!SectionHeader("Atmosphere", 1)) return;
        const auto& a = s_data.atmosphere;
        F4Color("Sky Upper", a.SkyUpper);
        F4Color("Sky Lower", a.SkyLower);
        F4Color("Horizon", a.Horizon);
        F4Color("Ambient", a.Ambient);
        F4Color("Sunlight", a.SunlightColor);
        F4Color("CloudDiffuse", a.CloudLODDiffuse);
        F4Color("CloudAmbient", a.CloudLODAmbient);
        F4Color("EffectLight", a.EffectLighting);
    }

    static void Sec_Fog()
    {
        if (!SectionHeader("Fog", 2)) return;
        const auto& f = s_data.fog;
        F4Color("Near", f.NearColor);
        ImGui::Text("  distance: %.1f", f.NearColor.w);
        F4Color("Far", f.FarColor);
        ImGui::Text("  distance: %.1f", f.FarColor.w);
        ImGui::Text("Power: %.2f  Max: %.2f  Interior: %s", f.Density.x, f.Density.y, f.Density.z > 0.5f ? "Y" : "N");
        ImGui::Text("HeightFog: waterZ=%.0f  playerZ=%.0f  density=%.4f  falloff=%.4f",
            f.HeightFog.x, f.HeightFog.y, f.HeightFog.z, f.HeightFog.w);
    }

    static void Sec_Weather()
    {
        if (!SectionHeader("Weather", 3)) return;
        const auto& w = s_data.weather;
        ImGui::Text("Wind: speed=%.2f  dir=%.2f rad", w.Wind.x, w.Wind.y);

        const char* pType = "None";
        if (w.Precipitation.x >= 2.0f) pType = "Snow";
        else if (w.Precipitation.x >= 1.0f) pType = "Rain";
        ImGui::Text("Precip: %s (%.0f%%)", pType, w.Precipitation.y * 100.0f);

        ImGui::Text("Lightning: freq=%.2f  flash=%.1f  intensity=%.2f  since=%.2fs",
            w.Lightning.x, w.Lightning.y, w.Lightning.z, w.Lightning.w);

        ImGui::Text("Flags:");
        ImGui::SameLine(100);
        BoolTag("Pleasant", w.Flags.x);
        BoolTag("Cloudy", w.Flags.y);
        BoolTag("Rainy", w.Flags.z);
        BoolTag("Snowy", w.Flags.w);
        ImGui::NewLine();

        ImGui::Text("Transition: %.0f%%  from=%d  to=%d",
            w.Transition.x * 100.0f, (int)w.Transition.y, (int)w.Transition.z);

        ImGui::Text("Surface: wet=%.2f  puddle=%.2f  snow=%.2f",
            w.PrecipSurface.x, w.PrecipSurface.y, w.PrecipSurface.z);

        // Tier A: Live Sky data
        ImGui::Separator();
        ImGui::Text("Live Wind: speed=%.3f  angle=%.2f rad  dir=(%.2f,%.2f)",
            w.WindLive.x, w.WindLive.y, w.WindLive.z, w.WindLive.w);
        ImGui::Text("Live Precip: density=%.3f  lastDensity=%.3f  flash=%.2f  hour=%.2f",
            w.PrecipLive.x, w.PrecipLive.y, w.PrecipLive.z, w.PrecipLive.w);
        ImGui::Text("Cloud Cover: avg=%.3f  layers=%.0f  max=%.3f  transition=%.0f%%",
            w.CloudCover.x, w.CloudCover.y, w.CloudCover.z, w.CloudCover.w * 100.0f);
        ImGui::Text("Aurora: in=%.3f  out=%.3f  inStart=%.3f  outStart=%.3f",
            w.AuroraFade.x, w.AuroraFade.y, w.AuroraFade.z, w.AuroraFade.w);
    }

    static void Sec_Player()
    {
        if (!SectionHeader("Player", 4)) return;
        const auto& p = s_data.player;
        ImGui::Text("Pos: (%.0f, %.0f, %.0f)  alt=%.0f", p.Position.x, p.Position.y, p.Position.z, p.Position.w);

        ImGui::ProgressBar(p.Vitals.x, ImVec2(200, 0), "HP"); ImGui::SameLine(); ImGui::Text("%.0f%%", p.Vitals.x * 100.f);
        ImGui::ProgressBar(p.Vitals.y, ImVec2(200, 0), "SP"); ImGui::SameLine(); ImGui::Text("%.0f%%", p.Vitals.y * 100.f);
        ImGui::ProgressBar(p.Vitals.z, ImVec2(200, 0), "MP"); ImGui::SameLine(); ImGui::Text("%.0f%%", p.Vitals.z * 100.f);
        ImGui::Text("Level: %.0f", p.Vitals.w);

        ImGui::Text("Speed: %.1f", p.Movement.x);
        ImGui::SameLine(100);
        BoolTag("Sprint", p.Movement.y);
        BoolTag("Swim", p.Movement.z);
        BoolTag("Mount", p.Movement.w);
        ImGui::NewLine();

        ImGui::Text("Combat:");
        ImGui::SameLine(100);
        BoolTag("Active", p.Combat.x);
        BoolTag("Bleedout", p.Combat.y);
        BoolTag("Killcam", p.Combat.z);
        BoolTag("Drawn", p.Combat.w);
        ImGui::NewLine();

        ImGui::Text("Water: surfZ=%.0f  depth=%.1f", p.Water.y, p.Water.z);
        ImGui::SameLine();
        BoolTag("Under", p.Water.x);
        BoolTag("Wading", p.Water.w);
        ImGui::NewLine();
    }

    static void Sec_Camera()
    {
        if (!SectionHeader("Camera", 5)) return;
        const auto& c = s_data.camera;
        ImGui::Text("Pos: (%.0f, %.0f, %.0f)  State: %.0f", c.WorldPos.x, c.WorldPos.y, c.WorldPos.z, c.WorldPos.w);
        ImGui::Text("FOV: %.3f rad (%.1f deg)  Near: %.2f  Far: %.0f  Aspect: %.3f",
            c.Params.x, c.Params.x * 57.2957795f, c.Params.y, c.Params.z, c.Params.w);
        ImGui::Text("View R: (%.3f, %.3f, %.3f)", c.ViewRow0.x, c.ViewRow0.y, c.ViewRow0.z);
        ImGui::Text("View U: (%.3f, %.3f, %.3f)", c.ViewRow1.x, c.ViewRow1.y, c.ViewRow1.z);
        ImGui::Text("View F: (%.3f, %.3f, %.3f)", c.ViewRow2.x, c.ViewRow2.y, c.ViewRow2.z);
        ImGui::Text("Prev Pos: (%.0f, %.0f, %.0f)", c.PrevWorldPos.x, c.PrevWorldPos.y, c.PrevWorldPos.z);
    }

    static void Sec_Interior()
    {
        if (!SectionHeader("Interior", 6)) return;
        const auto& i = s_data.interior;
        Bool("Interior", i.IsInterior.x);
        ImGui::SameLine(150);
        Bool("Has Template", i.IsInterior.y);
        bool isIn = i.IsInterior.x > 0.5f;
        if (isIn) {
            F4Color("Ambient", i.AmbientColor);
            F4Color("DirColor", i.DirectionalColor);
            F4("DirDir", i.DirectionalDir);
            F4Color("FogColor", i.InteriorFogColor);
            ImGui::Text("FogDist: near=%.0f far=%.0f power=%.2f clip=%.0f",
                i.InteriorFogDist.x, i.InteriorFogDist.y, i.InteriorFogDist.z, i.InteriorFogDist.w);
            ImGui::Text("Template: formID=%.0f  inherit=%.0f", i.LightingTemplate.x, i.LightingTemplate.y);
        }
    }

    static void Sec_Shadow()
    {
        if (!SectionHeader("Shadow", 7)) return;
        const auto& s = s_data.shadow;
        F4("LightDir", s.LightDirection);
        ImGui::Text("Intensity: %.2f", s.LightDirection.w);
        F4Color("Diffuse", s.LightDiffuse);
        F4Color("Ambient", s.LightAmbient);
    }

    static void Sec_Effects()
    {
        if (!SectionHeader("Effects", 8)) return;
        const auto& e = s_data.effects;
        ImGui::Text("Vision:");
        ImGui::SameLine(80);
        BoolTag("NightEye", e.VisionEffects.x);
        BoolTag("DetectLife", e.VisionEffects.y);
        BoolTag("DetectDead", e.VisionEffects.z);
        BoolTag("Ethereal", e.VisionEffects.w);
        ImGui::NewLine();

        ImGui::Text("Time: slow=%.2f", e.TimeEffects.x);
        ImGui::SameLine();
        Bool("Stopped", e.TimeEffects.y);

        ImGui::Text("Damage:");
        ImGui::SameLine(80);
        BoolTag("Fire", e.DamageEffects.x);
        BoolTag("Frost", e.DamageEffects.y);
        BoolTag("Shock", e.DamageEffects.z);
        BoolTag("Poison", e.DamageEffects.w);
        ImGui::NewLine();

        ImGui::Text("Misc:");
        ImGui::SameLine(80);
        BoolTag("Invisible", e.MiscEffects.x);
        BoolTag("Paralyzed", e.MiscEffects.y);
        BoolTag("Drunk", e.MiscEffects.z);
        ImGui::NewLine();
    }

    static void Sec_Render()
    {
        if (!SectionHeader("Render", 9)) return;
        const auto& r = s_data.render;
        ImGui::Text("Frame: %.0f  dt: %.4fs  Res: %.0fx%.0f", r.FrameInfo.x, r.FrameInfo.y, r.FrameInfo.z, r.FrameInfo.w);
        ImGui::Text("Jitter: (%.4f, %.4f)  idx: %.0f", r.Jitter.x, r.Jitter.y, r.Jitter.z);
        F4("StencilInfo", r.StencilInfo);
    }

    static void Sec_ImageSpace()
    {
        if (!SectionHeader("ImageSpace", 10)) return;
        const auto& is = s_data.imageSpace;
        ImGui::Text("HDR: adaptSpeed=%.3f bloom=%.3f thresh=%.3f sun=%.3f",
            is.HDR.x, is.HDR.y, is.HDR.z, is.HDR.w);
        ImGui::Text("Cinematic: sat=%.2f bright=%.2f contrast=%.2f tintA=%.2f",
            is.Cinematic.x, is.Cinematic.y, is.Cinematic.z, is.Cinematic.w);
        F4Color("CineTint", is.CineTint);
        ImGui::Text("DOF: str=%.2f dist=%.1f range=%.1f vignette=%.2f",
            is.DOF.x, is.DOF.y, is.DOF.z, is.DOF.w);
        ImGui::Text("IMOD: active=%s str=%.2f fadeIn=%.2f elapsed=%.2f",
            is.IMOD.x > 0.5f ? "Y" : "N", is.IMOD.y, is.IMOD.z, is.IMOD.w);
        F4Color("IMODTint", is.IMODTint);
    }

    static void Sec_Lights()
    {
        if (!SectionHeader("Nearby Lights", 11)) return;
        const auto& l = s_data.lights;
        for (int i = 0; i < 3; ++i) {
            const Float4& pos = (i == 0) ? l.Light0PosRad : (i == 1) ? l.Light1PosRad : l.Light2PosRad;
            const Float4& col = (i == 0) ? l.Light0Color  : (i == 1) ? l.Light1Color  : l.Light2Color;
            ImGui::Text("Light%d: pos(%.0f,%.0f,%.0f) r=%.0f", i, pos.x, pos.y, pos.z, pos.w);
            F4Color("  Color", col);
        }
        ImGui::Text("Summary: count=%.0f nearest=%.0f flux=%.1f hue=%.2f",
            l.Summary.x, l.Summary.y, l.Summary.z, l.Summary.w);
    }

    static void Sec_ActorValues()
    {
        if (!SectionHeader("Actor Values", 12)) return;
        const auto& av = s_data.actorValues;
        ImGui::Text("Resist: fire=%.0f%% frost=%.0f%% shock=%.0f%% magic=%.0f%%",
            av.Resist.x, av.Resist.y, av.Resist.z, av.Resist.w);
        ImGui::Text("Resist2: poison=%.0f%% disease=%.0f%% armor=%.0f",
            av.Resist2.x, av.Resist2.y, av.Resist2.z);
        ImGui::Text("Combat: atkMul=%.2f spdMul=%.2f crit=%.0f%% unarmed=%.0f",
            av.Combat.x, av.Combat.y, av.Combat.z, av.Combat.w);
        ImGui::Text("Move: speed=%.0f carry=%.0f weight=%.0f enc=%.0f%%",
            av.Movement.x, av.Movement.y, av.Movement.z, av.Movement.w * 100.f);
    }

    static void Sec_Crosshair()
    {
        if (!SectionHeader("Crosshair", 13)) return;
        const auto& x = s_data.crosshair;
        bool has = x.Info.x > 0.5f;
        ImGui::Text("Target: %s", has ? "Yes" : "None");
        if (has) {
            ImGui::Text("  dist=%.0f type=%.0f actor=%s", x.Info.y, x.Info.z, x.Info.w > 0.5f ? "Y" : "N");
            ImGui::Text("  pos=(%.0f,%.0f,%.0f) bound=%.0f", x.Pos.x, x.Pos.y, x.Pos.z, x.Pos.w);
            if (x.Info.w > 0.5f) {
                ImGui::Text("  hp=%.0f%% lvl=%.0f hostile=%s essential=%s",
                    x.Actor.x * 100.f, x.Actor.y, x.Actor.z > 0.5f ? "Y" : "N", x.Actor.w > 0.5f ? "Y" : "N");
            }
        }
    }

    static void Sec_Equipment()
    {
        if (!SectionHeader("Equipment", 14)) return;
        const auto& e = s_data.equipment;
        ImGui::Text("Right: type=%.0f dmg=%.0f ench=%s charge=%.0f%%",
            e.Right.x, e.Right.y, e.Right.z > 0.5f ? "Y" : "N", e.Right.w * 100.f);
        ImGui::Text("Left: type=%.0f val=%.0f ench=%s spell=%s",
            e.Left.x, e.Left.y, e.Left.z > 0.5f ? "Y" : "N", e.Left.w > 0.5f ? "Y" : "N");
        ImGui::Text("Armor: rating=%.0f heavy=%s light=%s robes=%s",
            e.Armor.x, e.Armor.y > 0.5f ? "Y" : "N", e.Armor.z > 0.5f ? "Y" : "N", e.Armor.w > 0.5f ? "Y" : "N");
        ImGui::Text("Flags:");
        ImGui::SameLine(100);
        BoolTag("Drawn", e.Flags.x);
        BoolTag("Bow", e.Flags.y);
        BoolTag("Torch", e.Flags.z);
        BoolTag("2H", e.Flags.w);
        ImGui::NewLine();
    }

    static void Sec_Quest()
    {
        if (!SectionHeader("Quest", 15)) return;
        const auto& q = s_data.quest;
        ImGui::Text("Main stage: %.0f  Completed: %.0f  Active: %.0f  Objectives: %.0f",
            q.Progress.x, q.Progress.y, q.Progress.z, q.Progress.w);
        ImGui::Text("Tracked: stage=%.0f type=%.0f formID=%.0f marker=%s",
            q.Tracked.x, q.Tracked.y, q.Tracked.z, q.Tracked.w > 0.5f ? "Y" : "N");
    }

    static void Sec_UIState()
    {
        if (!SectionHeader("UI State", 16)) return;
        const auto& u = s_data.uiState;
        ImGui::Text("Menus:");
        ImGui::SameLine(80);
        BoolTag("Menu", u.Menus.x);
        BoolTag("Dialogue", u.Menus.y);
        BoolTag("Inventory", u.Menus.z);
        BoolTag("Map", u.Menus.w);
        ImGui::NewLine();

        ImGui::Text("HUD:");
        ImGui::SameLine(80);
        BoolTag("Visible", u.HUD.x);
        BoolTag("Crosshair", u.HUD.y);
        BoolTag("Cinematic", u.HUD.z);
        BoolTag("Loading", u.HUD.w);
        ImGui::NewLine();

        ImGui::Text("Detail:");
        ImGui::SameLine(80);
        BoolTag("Craft", u.Detail.x);
        BoolTag("Book", u.Detail.y);
        BoolTag("Lockpick", u.Detail.z);
        BoolTag("Console", u.Detail.w);
        ImGui::NewLine();
    }

    static void Sec_Feedback()
    {
        if (!SectionHeader("Feedback (GPU Readback)", 17)) return;
        const auto& fb = s_data.feedback;
        bool valid = fb.Scene.w > 0.5f;
        ImGui::Text("Valid: %s", valid ? "Yes" : "No");
        if (!valid) return;

        ImGui::Text("Center Lum: %.4f (smooth)  %.4f (instant)", fb.Luminance.x, fb.Luminance.y);
        ImVec4 cc(fb.Luminance.z, fb.Luminance.w, fb.Scene.x, 1.0f);
        ImGui::ColorButton("##center", cc, 0, ImVec2(14, 14));
        ImGui::SameLine();
        ImGui::Text("Center RGB: (%.3f, %.3f, %.3f)", fb.Luminance.z, fb.Luminance.w, fb.Scene.x);

        ImGui::Text("Scene: avg=%.4f range=%.4f", fb.Scene.y, fb.Scene.z);
        ImGui::Text("Stats: key=%.4f contrast=%.2f periph=%.4f c/p=%.3f",
            fb.SceneStats.x, fb.SceneStats.y, fb.SceneStats.z, fb.SceneStats.w);

        ImVec4 sc(fb.SceneColor.x, fb.SceneColor.y, fb.SceneColor.z, 1.0f);
        ImGui::ColorButton("##sceneAvg", sc, 0, ImVec2(14, 14));
        ImGui::SameLine();
        ImGui::Text("Avg RGB: (%.3f,%.3f,%.3f) temp=%.0fK",
            fb.SceneColor.x, fb.SceneColor.y, fb.SceneColor.z, fb.SceneColor.w);

        ImGui::Text("Histogram:");
        ImGui::Indent();
        ImGui::ProgressBar(fb.Histogram.x, ImVec2(140, 0), nullptr);
        ImGui::SameLine(); ImGui::Text("Shadows: %.0f%%", fb.Histogram.x * 100.f);
        ImGui::ProgressBar(fb.Histogram.y, ImVec2(140, 0), nullptr);
        ImGui::SameLine(); ImGui::Text("Darks: %.0f%%", fb.Histogram.y * 100.f);
        ImGui::ProgressBar(fb.Histogram.z, ImVec2(140, 0), nullptr);
        ImGui::SameLine(); ImGui::Text("Mids: %.0f%%", fb.Histogram.z * 100.f);
        ImGui::ProgressBar(fb.Histogram.w, ImVec2(140, 0), nullptr);
        ImGui::SameLine(); ImGui::Text("Brights: %.0f%%", fb.Histogram.w * 100.f);
        ImGui::Unindent();

        ImGui::Text("Temporal: lumVel=%+.4f  colorShift=%.4f  stability=%.3f",
            fb.Temporal.y, fb.Temporal.z, fb.Temporal.w);
        ImGui::SameLine();
        BoolTag("SceneCut", fb.Temporal.x);
    }

    static void Sec_Region()
    {
        if (!SectionHeader("Region", 18)) return;
        const auto& r = s_data.region;
        ImGui::Text("Location: formID=%.0f parent=%.0f ws=%.0f cell=%.0f",
            r.Location.x, r.Location.y, r.Location.z, r.Location.w);
        ImGui::Text("Region: formID=%.0f weatherOvr=%s landW=%.2f typeFlags=%.0f",
            r.Region.x, r.Region.y > 0.5f ? "Y" : "N", r.Region.z, r.Region.w);
        ImGui::Text("World: LODWater=%s waterLvl=%.0f center=(%.0f,%.0f)",
            r.Worldspace.x > 0.5f ? "Y" : "N", r.Worldspace.y, r.Worldspace.z, r.Worldspace.w);
    }

    static void Sec_Audio()
    {
        if (!SectionHeader("Audio", 19)) return;
        const auto& a = s_data.audio;
        ImGui::Text("Music: formID=%.0f pri=%.0f combat=%s dungeon=%s",
            a.Music.x, a.Music.y, a.Music.z > 0.5f ? "Y" : "N", a.Music.w > 0.5f ? "Y" : "N");
        ImGui::Text("Ambient: exterior=%s reverb=%.2f weatherSnd=%s",
            a.Ambient.x > 0.5f ? "Y" : "N", a.Ambient.y, a.Ambient.z > 0.5f ? "Y" : "N");
    }

    static void Sec_NPCDetect()
    {
        if (!SectionHeader("NPC Detection", 20)) return;
        const auto& n = s_data.npcDetect;
        ImGui::Text("Nearest: dist=%.0f hostile=%s hp=%.0f%% lvl=%.0f",
            n.Nearest.x, n.Nearest.y > 0.5f ? "Y" : "N", n.Nearest.z * 100.f, n.Nearest.w);
        ImGui::Text("  pos=(%.0f,%.0f,%.0f) alerted=%s",
            n.NearestPos.x, n.NearestPos.y, n.NearestPos.z, n.NearestPos.w > 0.5f ? "Y" : "N");
        ImGui::Text("Summary: hostile=%d friendly=%d nearH=%.0f nearF=%.0f",
            (int)n.Summary.x, (int)n.Summary.y, n.Summary.z, n.Summary.w);
        ImGui::Text("Threat: rating=%.2f stealth=%.0f detection=%.0f",
            n.Threat.x, n.Threat.y, n.Threat.z);
    }

    static void Sec_Perf()
    {
        if (!SectionHeader("Performance", 21)) return;
        const auto& p = s_data.perf;
        ImGui::Text("GPU: %.2f ms  CPU: %.2f ms  Present: %.2f ms  Target: %.0f fps",
            p.Timing.x, p.Timing.y, p.Timing.z, p.Timing.w);
        ImGui::Text("Budget: %.0f%% GPU  Quality: %.2f  Thermal: %.0f  Drops: %.0f",
            p.Budget.x * 100.f, p.Budget.y, p.Budget.z, p.Budget.w);
    }

    static void Sec_Scene()
    {
        if (!ImGui::CollapsingHeader("Scene Composition")) return;
        const auto& s = s_data.scene;

        ImGui::Text("Draw Calls: %.0f total  %.0f lighting", s.DrawStats.x, s.DrawStats.y);

        // Character Light
        ImGui::Text("Char Light:");
        ImGui::SameLine();
        BoolTag("Active", s.CharLight.x);
        if (s.CharLight.x > 0.5f) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.72f, 1.0f),
                "pri=%.2f sec=%.2f lum=%.2f", s.CharLight.y, s.CharLight.z, s.CharLight.w);
        }
        ImGui::NewLine();

        F4Color("Ambient Specular", s.AmbientSpec);

        // Material Breakdown
        if (s.DrawStats.y > 0 && ImGui::TreeNode("Material Breakdown")) {
            auto Bar = [](const char* label, float frac) {
                ImGui::ProgressBar(frac, ImVec2(180, 0), nullptr);
                ImGui::SameLine();
                ImGui::Text("%s: %.1f%%", label, frac * 100.0f);
            };

            Bar("General",     s.MaterialCounts1.x);
            Bar("Skin",        s.MaterialCounts1.y);
            Bar("Terrain",     s.MaterialCounts1.z);
            Bar("Vegetation",  s.MaterialCounts1.w);
            Bar("Hair",        s.MaterialCounts2.x);
            Bar("Eye",         s.MaterialCounts2.y);
            Bar("Snow",        s.MaterialCounts2.z);
            Bar("Emissive",    s.MaterialCounts2.w);
            Bar("MetalGlossy", s.DrawStats.z);
            ImGui::TreePop();
        }

        // Material Properties (SceneObserver)
        if (SB::SceneObserver::Get().IsInstalled() && ImGui::TreeNode("Material Properties")) {
            ImGui::Text("Avg SpecPower: %.2f  SpecScale: %.2f  Roughness: %.3f",
                s.MaterialProps1.x, s.MaterialProps1.y, s.MaterialProps1.z);
            ImGui::Text("Avg SubSurface: %.3f  RimLight: %.3f",
                s.MaterialProps1.w, s.MaterialProps2.x);
            ImGui::Text("Avg EnvMapScale: %.2f  Alpha: %.2f  SkinSpec: %.2f",
                s.MaterialProps2.y, s.MaterialProps2.z, s.MaterialProps2.w);
            ImGui::Text("Flags: envMap=%.1f%%  glowMap=%.1f%%  backLit=%.1f%%  softLit=%.1f%%",
                s.ShaderFlags.x * 100.0f, s.ShaderFlags.y * 100.0f,
                s.ShaderFlags.z * 100.0f, s.ShaderFlags.w * 100.0f);
            ImGui::TreePop();
        }

        // Engine State
        if (ImGui::TreeNode("Engine State")) {
            Bool("Interior", s.EngineState.x);
            ImGui::Text("WaterState: %.0f  WaterIntersect: %.2f  Technique: %.0f",
                s.EngineState.y, s.EngineState.z, s.EngineState.w);
            ImGui::Text("Timers: default=%.3f  delta=%.5f  system=%.3f  realDelta=%.5f",
                s.EngineTimers.x, s.EngineTimers.y, s.EngineTimers.z, s.EngineTimers.w);

            if (ImGui::TreeNode("Directional Ambient")) {
                F4Color("X+", s.DirAmbient1);
                ImGui::Text("  X- lum: %.4f", s.DirAmbient1.w);
                F4Color("Y+", s.DirAmbient2);
                ImGui::Text("  Y- lum: %.4f", s.DirAmbient2.w);
                F4Color("Z+", s.DirAmbient3);
                ImGui::Text("  Z- lum: %.4f", s.DirAmbient3.w);
                ImGui::TreePop();
            }

            ImGui::Text("Sun/Shadow: glare=%.2f  occlude=%.0f  lights=%.0f  shadows=%.0f",
                s.SunGlare.x, s.SunGlare.y, s.SunGlare.z, s.SunGlare.w);
            ImGui::TreePop();
        }

        // Geometry
        ImGui::Text("Geometry: avg lights/draw=%.2f  max=%.0f  pass=%.1f  LOD=%.2f",
            s.GeometryInfo.x, s.GeometryInfo.y, s.GeometryInfo.z, s.GeometryInfo.w);

        // Water
        if (ImGui::TreeNode("Water Shader")) {
            F4Color("Shallow Color", s.WaterColor);
            ImGui::Text("Alpha: %.2f  Plane: n=(%.2f,%.2f,%.2f) d=%.1f",
                s.WaterColor.w, s.WaterPlane.x, s.WaterPlane.y, s.WaterPlane.z, s.WaterPlane.w);
            ImGui::Text("SunSpec: %.1f  Reflect: %.2f  Refract: %.2f  Fresnel: %.2f",
                s.WaterParams.x, s.WaterParams.y, s.WaterParams.z, s.WaterParams.w);
            ImGui::Text("DisplaceDamp: %.3f  FlowScale: %.2f  FogFar: %.0f/%.0f",
                s.WaterWave.x, s.WaterWave.y, s.WaterWave.z, s.WaterWave.w);
            ImGui::TreePop();
        }

        // Effect Shader
        if (s.EffectShader.x > 0.5f && ImGui::TreeNode("Effect Shader")) {
            ImGui::Text("Draws: %.0f", s.EffectShader.x);
            F4Color("Avg Color", s.EffectColor);
            ImGui::Text("ColorScale: %.2f  SoftFalloff: %.2f  FalloffOpacity: %.2f",
                s.EffectShader.y, s.EffectShader.z, s.EffectShader.w);
            ImGui::TreePop();
        }
    }

    // ══════════════════════════════════════════════════════════════════════
    //  SUBSYSTEMS TAB
    // ══════════════════════════════════════════════════════════════════════

    static void RenderSubsystems()
    {
        // ENB Readback
        {
            auto& fp = SB::FeedbackProcessor::Get();
            int count = fp.GetReadbackSlotCount();
            int validCount = 0;
            for (int i = 0; i < count; ++i)
                if (fp.GetReadbackSlot(i).valid) ++validCount;
            char rbHdr[64];
            snprintf(rbHdr, sizeof(rbHdr), "ENB Readback (%d/%d valid)##readback", validCount, count);
            if (ImGui::CollapsingHeader(rbHdr)) {
                for (int i = 0; i < count; ++i) {
                    const auto& slot = fp.GetReadbackSlot(i);
                    if (slot.valid) {
                        if (slot.dataSize == 4)
                            ImGui::Text("[%d] %s/%s = %.4f", i, slot.shader, slot.paramName, slot.data[0]);
                        else
                            ImGui::Text("[%d] %s/%s = (%.3f,%.3f,%.3f,%.3f)",
                                i, slot.shader, slot.paramName,
                                slot.data[0], slot.data[1], slot.data[2], slot.data[3]);
                    } else {
                        ImGui::TextDisabled("[%d] %s/%s — not found", i, slot.shader, slot.paramName);
                    }
                }
            }
        }

        // Weather Parameter Computer
        if (ImGui::CollapsingHeader("Weather Parameters")) {
            auto& wpc = SB::WeatherParameterComputer::Get();
            const auto& params = wpc.GetParameters();

            ImGui::Text("Category: %d  Prev: %d  Transition: %.0f%%",
                static_cast<int>(wpc.GetCurrentCategory()),
                static_cast<int>(wpc.GetPreviousCategory()),
                wpc.GetTransitionPct() * 100.f);
            ImGui::Text("Weather: current=0x%X  prev=0x%X",
                wpc.GetCurrentWeatherID(), wpc.GetPrevWeatherID());

            if (!params.empty()) {
                ImGui::Separator();
                for (const auto& p : params) {
                    float val = wpc.GetValue(p.paramName);
                    ImGui::Text("  %s/%s = %.3f", p.shaderFile.c_str(), p.paramName.c_str(), val);
                }
            }
        }

        // WriteBack Processor
        {
            auto& wb = SB::WriteBackProcessor::Get();
            int count = wb.GetRuleCount();
            int enabled = wb.GetEnabledRuleCount();
            char wbHdr[64];
            snprintf(wbHdr, sizeof(wbHdr), "WriteBack (%d/%d enabled)##writeback", enabled, count);
            if (ImGui::CollapsingHeader(wbHdr)) {
                if (count == 0) {
                    ImGui::TextDisabled("No rules (see WriteBackConfig.ini)");
                } else {
                    for (int i = 0; i < count; ++i) {
                        const auto& rule = wb.GetRule(i);
                        if (rule.enabled)
                            ImGui::Text("  [%d] %s <- %s (scale=%.2f off=%.2f val=%.3f)",
                                i, rule.name.c_str(), rule.sourceField.c_str(),
                                rule.transform.scale, rule.transform.offset, rule.currentValue);
                        else
                            ImGui::TextDisabled("  [%d] %s (disabled)", i, rule.name.c_str());
                    }
                }
            }
        }

        // Tracker Health
        if (ImGui::CollapsingHeader("Tracker Health")) {
            if (s_trackerHealthCount == 0) {
                ImGui::TextDisabled("No tracker health data");
            } else {
                int disabledCount = 0;
                int errorCount = 0;
                for (int i = 0; i < s_trackerHealthCount; ++i) {
                    if (s_trackerHealthInfo[i].disabled) disabledCount++;
                    errorCount += s_trackerHealthInfo[i].totalErrors;
                }

                if (disabledCount == 0 && errorCount == 0) {
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f),
                        "All %d trackers healthy", s_trackerHealthCount);
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f),
                        "%d/%d trackers OK  |  %d disabled  |  %d total errors",
                        s_trackerHealthCount - disabledCount, s_trackerHealthCount,
                        disabledCount, errorCount);
                }

                for (int i = 0; i < s_trackerHealthCount; ++i) {
                    const auto& t = s_trackerHealthInfo[i];
                    if (t.disabled) {
                        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                            "  [X] %s — DISABLED (%d errors)", t.name, t.totalErrors);
                    } else if (t.totalErrors > 0) {
                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
                            "  [!] %s — recovered (%d errors)", t.name, t.totalErrors);
                    }
                    // Don't show healthy trackers to reduce clutter
                }
            }
        }

        // Shared Memory Bridge
        if (ImGui::CollapsingHeader("Shared Memory")) {
            auto& sm = SB::SharedMemoryBridge::Get();
            if (sm.IsActive()) {
                ImGui::Text("Active  |  Frames: %u  |  Clients: %u",
                    sm.GetFramesWritten(), sm.GetClientsConnected());
            } else {
                ImGui::TextDisabled("Inactive");
            }
        }

        // Shader Pre-Processor (ENB Extender replacement)
        if (ImGui::CollapsingHeader("Shader Pre-Processor")) {
            if (s_ppStats.processCount == 0) {
                ImGui::TextDisabled("No shaders processed yet");
            } else {
                ImGui::Text("Processed: %d  |  Cache hits: %d",
                    s_ppStats.processCount, s_ppStats.cacheHits);
                ImGui::Text("Parameters: %d  |  Separated: %d  |  Shaders: %d",
                    s_ppStats.parameterCount, s_ppStats.separatedCount, s_ppStats.shaderCount);
                if (s_ppStats.externBindingCount > 0)
                    ImGui::Text("Extern bindings: %d (%d pushed/frame)",
                        s_ppStats.externBindingCount, s_ppStats.externPushCount);
                if (s_ppStats.weatherSepCount > 0)
                    ImGui::Text("Weather separation: %d params, %d weather INIs",
                        s_ppStats.weatherSepCount, s_ppStats.weatherINICount);
            }
        }
    }

    // ══════════════════════════════════════════════════════════════════════
    //  RAW PARAMETER TABLE
    // ══════════════════════════════════════════════════════════════════════

    static void RenderParamTable()
    {
        ImGui::TextColored(ImVec4(0.55f, 0.60f, 0.70f, 1.0f), "%zu parameters", SB::kParamCount);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##filter", "Search params...", s_searchFilter, sizeof(s_searchFilter));

        const auto* raw = reinterpret_cast<const char*>(&s_data);
        const auto* prev = reinterpret_cast<const char*>(&s_prevData);

        for (std::size_t i = 0; i < SB::kParamCount; ++i) {
            const auto& entry = SB::kParamTable[i];

            // Filter
            if (s_searchFilter[0] != '\0') {
                // Case-insensitive substring match
                bool match = false;
                const char* name = entry.name;
                const char* filter = s_searchFilter;
                for (const char* p = name; *p; ++p) {
                    const char* pp = p;
                    const char* fp = filter;
                    while (*pp && *fp && ((*pp | 32) == (*fp | 32))) { ++pp; ++fp; }
                    if (!*fp) { match = true; break; }
                }
                if (!match) continue;
            }

            const auto* val = reinterpret_cast<const float*>(raw + entry.offset);
            bool dirty = s_hasPrevData && std::memcmp(raw + entry.offset, prev + entry.offset, 16) != 0;

            if (dirty)
                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.3f, 1.0f),
                    "%s: (%.3f, %.3f, %.3f, %.3f)", entry.name, val[0], val[1], val[2], val[3]);
            else
                ImGui::Text("%s: (%.3f, %.3f, %.3f, %.3f)", entry.name, val[0], val[1], val[2], val[3]);
        }
    }

    // ══════════════════════════════════════════════════════════════════════
    //  PUBLIC INTERFACE
    // ══════════════════════════════════════════════════════════════════════
    //  ANNOTATION BROWSER
    // ══════════════════════════════════════════════════════════════════════

    static void RenderAnnotationBrowser()
    {
        auto& db = SB::AnnotationDatabase::Get();
        int paramCount = db.GetParameterCount();
        int shaderCount = db.GetShaderCount();

        if (paramCount == 0) {
            ImGui::TextDisabled("No annotated parameters discovered yet.");
            ImGui::TextDisabled("(Parameters are parsed when ENB compiles shaders)");
            return;
        }

        ImGui::Text("Annotated params: %d across %d shaders", paramCount, shaderCount);
        ImGui::Separator();

        // ── Per-shader parameter tree ────────────────────────────────
        if (ImGui::CollapsingHeader("Parameters by Shader", ImGuiTreeNodeFlags_DefaultOpen)) {
            // Get all shaders that have been processed
            auto separated = db.GetSeparatedParameters();
            auto externBound = db.GetExternBoundParameters();
            auto bound = db.GetBoundParameters();

            // Collect unique shader names
            std::vector<std::string> shaderNames;
            {
                // Use a simple approach — iterate separated + extern + bound to find shader names
                std::unordered_map<std::string, bool> seen;
                auto addShader = [&](const std::string& s) {
                    if (!s.empty() && !seen[s]) {
                        seen[s] = true;
                        shaderNames.push_back(s);
                    }
                };
                for (const auto* p : separated) addShader(p->shaderFile);
                for (const auto* p : externBound) addShader(p->shaderFile);
                for (const auto* p : bound) addShader(p->shaderFile);
            }

            for (const auto& shader : shaderNames) {
                auto shaderParams = db.GetParametersForShader(shader);
                if (shaderParams.empty()) continue;

                char label[256];
                snprintf(label, sizeof(label), "%s (%d params)##shader_%s",
                         shader.c_str(), static_cast<int>(shaderParams.size()), shader.c_str());

                if (ImGui::TreeNode(label)) {
                    for (const auto* p : shaderParams) {
                        // Color-code by type
                        ImVec4 color(0.8f, 0.8f, 0.8f, 1.0f);
                        const char* tag = "";
                        if (p->separation != ParameterMeta::Separation::None) {
                            color = ImVec4(0.4f, 0.9f, 1.0f, 1.0f);
                            tag = " [SEP]";
                        }
                        if (!p->externBinding.empty()) {
                            color = ImVec4(1.0f, 0.8f, 0.3f, 1.0f);
                            tag = " [EXT]";
                        }
                        if (!p->uiBinding.empty()) {
                            color = ImVec4(0.6f, 1.0f, 0.6f, 1.0f);
                            tag = " [BIND]";
                        }

                        const char* displayName = p->uiName.empty()
                            ? p->varName.c_str()
                            : p->uiName.c_str();

                        ImGui::TextColored(color, "  %s (%s)%s",
                            displayName, p->hlslType.c_str(), tag);

                        // Tooltip with full details
                        if (ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            ImGui::Text("Variable: %s", p->varName.c_str());
                            ImGui::Text("Type: %s", p->hlslType.c_str());
                            if (!p->uiName.empty())
                                ImGui::Text("UIName: %s", p->uiName.c_str());
                            if (!p->uiGroup.empty())
                                ImGui::Text("UIGroup: %s", p->uiGroup.c_str());
                            if (p->separation != ParameterMeta::Separation::None)
                                ImGui::Text("Separation: %s",
                                    p->separation == ParameterMeta::Separation::ExteriorWeather
                                    ? "ExteriorWeather" : "Weather");
                            if (!p->externBinding.empty())
                                ImGui::Text("ExternBinding: %s", p->externBinding.c_str());
                            if (!p->uiBinding.empty()) {
                                ImGui::Text("UIBinding: %s", p->uiBinding.c_str());
                                if (!p->uiBindingCondition.empty())
                                    ImGui::Text("Condition: %s", p->uiBindingCondition.c_str());
                                if (!p->uiBindingProperty.empty())
                                    ImGui::Text("Property: %s", p->uiBindingProperty.c_str());
                            }
                            ImGui::Text("Range: [%.1f, %.1f]", p->uiMin, p->uiMax);
                            ImGui::Text("Default: %.3f", p->defaultFloat[0]);
                            ImGui::EndTooltip();
                        }
                    }
                    ImGui::TreePop();
                }
            }
        }

        // ── Separated parameters ─────────────────────────────────────
        auto sepParams = db.GetSeparatedParameters();
        if (!sepParams.empty()) {
            char sepHeader[64];
            snprintf(sepHeader, sizeof(sepHeader), "Weather Separated (%d)",
                     static_cast<int>(sepParams.size()));
            if (ImGui::CollapsingHeader(sepHeader)) {
                auto& wse = SB::WeatherSeparationEngine::Get();
                ImGui::Text("Loaded weather INIs: %d", wse.GetLoadedWeatherCount());
                ImGui::Text("ToD mode: %s",
                    wse.GetSlotMode() == ToDSlotMode::FourSlot ? "4-slot" : "6-slot");

                for (const auto* p : sepParams) {
                    float val = wse.GetValue(p->GetUniqueKey());
                    ImGui::Text("  %s = %.3f (%s)",
                        p->varName.c_str(), val,
                        p->separation == ParameterMeta::Separation::ExteriorWeather
                        ? "Exterior" : "Always");
                }
            }
        }

        // ── Extern bindings ──────────────────────────────────────────
        auto extParams = db.GetExternBoundParameters();
        if (!extParams.empty()) {
            char extHeader[64];
            snprintf(extHeader, sizeof(extHeader), "Extern Bindings (%d)",
                     static_cast<int>(extParams.size()));
            if (ImGui::CollapsingHeader(extHeader)) {
                auto& eb = SB::ExternBindingProcessor::Get();
                ImGui::Text("Resolved: %d / %d", eb.GetPushCount(), eb.GetBindingCount());
                for (const auto* p : extParams) {
                    ImGui::Text("  %s <- %s (%s)",
                        p->varName.c_str(), p->externBinding.c_str(), p->shaderFile.c_str());
                }
            }
        }

        // ── Parameter bindings ───────────────────────────────────────
        auto& pbe = SB::ParameterBindingEngine::Get();
        if (pbe.GetRuleCount() > 0) {
            char bindHeader[64];
            snprintf(bindHeader, sizeof(bindHeader), "Parameter Bindings (%d rules, %d active)",
                     pbe.GetRuleCount(), pbe.GetActiveCount());
            if (ImGui::CollapsingHeader(bindHeader)) {
                for (const auto& rule : pbe.GetRules()) {
                    ImVec4 color = rule.conditionMet
                        ? ImVec4(0.3f, 1.0f, 0.4f, 1.0f)
                        : ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
                    ImGui::TextColored(color, "  %s -> %s [%s] %s",
                        rule.sourceKey.c_str(), rule.targetKey.c_str(),
                        rule.property.c_str(),
                        rule.conditionMet ? "ACTIVE" : "inactive");
                }
            }
        }
    }

    // ══════════════════════════════════════════════════════════════════════
    //  OBJECT WINDOW (Form Browser via EditorIDCache)
    // ══════════════════════════════════════════════════════════════════════

    static void RenderObjectWindow()
    {
        static char s_objSearch[128] = "";
        static int s_formTypeFilter = -1;  // -1 = all

        // Cached results — only rebuild when search/filter changes
        struct CachedForm { RE::FormID id; uint8_t type; std::string editorID; };
        static std::vector<CachedForm> s_cachedResults;
        static char s_lastSearch[128] = "";
        static int s_lastFilter = -2;  // force initial build
        static int s_rebuildCooldown = 0;

        auto& cache = SB::EditorIDCache::Get();

        if (!cache.IsInstalled()) {
            ImGui::TextDisabled("EditorIDCache not installed");
            return;
        }

        ImGui::Text("Cached forms: %zu  |  Provider: %s",
            cache.Size(),
            cache.IsUsingExternalProvider() ? "external" : "native");

        ImGui::InputTextWithHint("##objSearch", "Search by EditorID...",
            s_objSearch, sizeof(s_objSearch));

        ImGui::Separator();

        // Form type filter buttons
        struct TypeFilter { const char* label; RE::FormType type; };
        static const TypeFilter kFilters[] = {
            { "All",      RE::FormType::None },
            { "Weather",  RE::FormType::Weather },
            { "Location", RE::FormType::Location },
            { "WorldSp",  RE::FormType::WorldSpace },
            { "Cell",     RE::FormType::Cell },
            { "NPC",      RE::FormType::NPC },
            { "Race",     RE::FormType::Race },
            { "Spell",    RE::FormType::Spell },
            { "Perk",     RE::FormType::Perk },
            { "Quest",    RE::FormType::Quest },
            { "Image",    RE::FormType::ImageSpace },
        };

        for (int i = 0; i < static_cast<int>(std::size(kFilters)); ++i) {
            if (i > 0) ImGui::SameLine();
            bool selected = (s_formTypeFilter == i);
            if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.6f, 1.0f, 1.0f));
            if (ImGui::SmallButton(kFilters[i].label))
                s_formTypeFilter = (s_formTypeFilter == i) ? -1 : i;
            if (selected) ImGui::PopStyleColor();
        }

        ImGui::Separator();

        bool hasSearch = (s_objSearch[0] != '\0');
        RE::FormType filterType = (s_formTypeFilter > 0)
            ? kFilters[s_formTypeFilter].type : RE::FormType::None;

        if (!hasSearch && s_formTypeFilter <= 0) {
            ImGui::TextDisabled("Type a search term or select a form type filter");
            return;
        }

        // Rebuild cache only when search/filter changes (with brief cooldown for typing)
        bool searchChanged = (strcmp(s_objSearch, s_lastSearch) != 0);
        bool filterChanged = (s_formTypeFilter != s_lastFilter);
        if (searchChanged) s_rebuildCooldown = 10; // wait 10 frames after typing stops
        if (s_rebuildCooldown > 0) s_rebuildCooldown--;

        if ((searchChanged && s_rebuildCooldown == 0) || filterChanged || s_lastFilter == -2) {
            strcpy_s(s_lastSearch, s_objSearch);
            s_lastFilter = s_formTypeFilter;
            s_cachedResults.clear();
            s_rebuildCooldown = 0;

            auto* dataHandler = RE::TESDataHandler::GetSingleton();
            if (!dataHandler) {
                ImGui::TextDisabled("DataHandler not ready");
                return;
            }

            const int kMaxResults = 200;

            auto collectForm = [&](RE::TESForm* form) {
                if (!form || static_cast<int>(s_cachedResults.size()) >= kMaxResults) return;
                if (filterType != RE::FormType::None && form->GetFormType() != filterType) return;

                const auto& editorID = cache.Lookup(form->GetFormID());
                if (editorID.empty()) return;

                if (hasSearch) {
                    bool match = false;
                    for (size_t p = 0; p < editorID.size(); ++p) {
                        const char* ep = editorID.c_str() + p;
                        const char* sp = s_objSearch;
                        while (*ep && *sp && ((*ep | 32) == (*sp | 32))) { ++ep; ++sp; }
                        if (!*sp) { match = true; break; }
                    }
                    if (!match) return;
                }

                s_cachedResults.push_back({
                    form->GetFormID(),
                    static_cast<uint8_t>(form->GetFormType()),
                    editorID
                });
            };

            if (filterType == RE::FormType::Weather) {
                for (auto* f : dataHandler->GetFormArray<RE::TESWeather>()) collectForm(f);
            } else if (filterType == RE::FormType::Location) {
                for (auto* f : dataHandler->GetFormArray<RE::BGSLocation>()) collectForm(f);
            } else if (filterType == RE::FormType::WorldSpace) {
                for (auto* f : dataHandler->GetFormArray<RE::TESWorldSpace>()) collectForm(f);
            } else if (filterType == RE::FormType::NPC) {
                for (auto* f : dataHandler->GetFormArray<RE::TESNPC>()) collectForm(f);
            } else if (filterType == RE::FormType::Race) {
                for (auto* f : dataHandler->GetFormArray<RE::TESRace>()) collectForm(f);
            } else if (filterType == RE::FormType::Spell) {
                for (auto* f : dataHandler->GetFormArray<RE::SpellItem>()) collectForm(f);
            } else if (filterType == RE::FormType::Perk) {
                for (auto* f : dataHandler->GetFormArray<RE::BGSPerk>()) collectForm(f);
            } else if (filterType == RE::FormType::Quest) {
                for (auto* f : dataHandler->GetFormArray<RE::TESQuest>()) collectForm(f);
            } else if (filterType == RE::FormType::ImageSpace) {
                for (auto* f : dataHandler->GetFormArray<RE::TESImageSpace>()) collectForm(f);
            } else if (filterType == RE::FormType::Cell) {
                for (auto* f : dataHandler->GetFormArray<RE::TESObjectCELL>()) collectForm(f);
            } else if (hasSearch) {
                for (auto* f : dataHandler->GetFormArray<RE::TESWeather>()) collectForm(f);
                for (auto* f : dataHandler->GetFormArray<RE::BGSLocation>()) collectForm(f);
                for (auto* f : dataHandler->GetFormArray<RE::TESWorldSpace>()) collectForm(f);
                for (auto* f : dataHandler->GetFormArray<RE::TESNPC>()) collectForm(f);
                for (auto* f : dataHandler->GetFormArray<RE::TESRace>()) collectForm(f);
                for (auto* f : dataHandler->GetFormArray<RE::SpellItem>()) collectForm(f);
                for (auto* f : dataHandler->GetFormArray<RE::BGSPerk>()) collectForm(f);
                for (auto* f : dataHandler->GetFormArray<RE::TESQuest>()) collectForm(f);
                for (auto* f : dataHandler->GetFormArray<RE::TESImageSpace>()) collectForm(f);
            }
        }

        // Render cached results (fast — just drawing text)
        for (const auto& entry : s_cachedResults) {
            ImGui::Text("0x%08X  [%02X]  %s",
                entry.id, entry.type, entry.editorID.c_str());
        }

        if (s_cachedResults.empty()) {
            if (s_rebuildCooldown > 0)
                ImGui::TextDisabled("Searching...");
            else
                ImGui::TextDisabled("No matching forms found");
        } else if (s_cachedResults.size() >= 200) {
            ImGui::TextDisabled("... %zu results shown (limit 200)",
                s_cachedResults.size());
        }
    }

    // ══════════════════════════════════════════════════════════════════════
    //  PARAMETER EDITOR (Interactive ENB parameter control)
    // ══════════════════════════════════════════════════════════════════════

    static void RenderParameterEditor()
    {
        auto& db = SB::AnnotationDatabase::Get();
        auto& pbe = SB::ParameterBindingEngine::Get();
        int paramCount = db.GetParameterCount();

        if (paramCount == 0) {
            ImGui::TextDisabled("No annotated parameters discovered yet.");
            return;
        }

        static char s_paramSearch[64] = "";
        ImGui::InputTextWithHint("##paramEdSearch", "Filter params...",
            s_paramSearch, sizeof(s_paramSearch));

        // Get all parameters grouped by shader
        std::unordered_map<std::string, std::vector<const ParameterMeta*>> byShader;

        auto sepParams = db.GetSeparatedParameters();
        auto extParams = db.GetExternBoundParameters();
        auto bndParams = db.GetBoundParameters();

        // Collect all params into byShader map
        auto addParam = [&](const ParameterMeta* p) {
            byShader[p->shaderFile].push_back(p);
        };
        for (const auto* p : sepParams) addParam(p);
        for (const auto* p : extParams) addParam(p);
        for (const auto* p : bndParams) addParam(p);

        // Also add any remaining params that have UIName
        // (These are params with annotations but no special feature)
        // For now, just show what we have from the special queries

        for (auto& [shader, params] : byShader) {
            if (params.empty()) continue;

            if (!ImGui::CollapsingHeader(shader.c_str())) continue;

            // Group by UIGroup within shader
            std::unordered_map<std::string, std::vector<const ParameterMeta*>> byGroup;
            for (const auto* p : params) {
                std::string group = p->uiGroup.empty() ? "(ungrouped)" : p->uiGroup;
                byGroup[group].push_back(p);
            }

            for (auto& [group, groupParams] : byGroup) {
                // Apply search filter
                bool anyMatch = false;
                if (s_paramSearch[0] != '\0') {
                    for (const auto* p : groupParams) {
                        const char* name = p->uiName.empty() ? p->varName.c_str() : p->uiName.c_str();
                        for (const char* n = name; *n; ++n) {
                            const char* nn = n;
                            const char* sp = s_paramSearch;
                            while (*nn && *sp && ((*nn | 32) == (*sp | 32))) { ++nn; ++sp; }
                            if (!*sp) { anyMatch = true; break; }
                        }
                        if (anyMatch) break;
                    }
                    if (!anyMatch) continue;
                }

                bool groupOpen = (group == "(ungrouped)")
                    ? true
                    : ImGui::TreeNode(group.c_str());

                if (groupOpen) {
                    for (const auto* p : groupParams) {
                        const char* displayName = p->uiName.empty()
                            ? p->varName.c_str() : p->uiName.c_str();

                        // Check binding state
                        bool isHidden = pbe.IsHidden(p->GetUniqueKey());
                        bool isReadOnly = pbe.IsReadOnly(p->GetUniqueKey());

                        if (isHidden) {
                            ImGui::TextDisabled("  %s [hidden by binding]", displayName);
                            continue;
                        }

                        // Current value (ENB readback removed — standalone mode)
                        float currentVal = p->defaultFloat[0];

                        // Render control based on widget type
                        char label[256];
                        snprintf(label, sizeof(label), "##%s_%s", shader.c_str(), p->varName.c_str());

                        if (isReadOnly) {
                            ImGui::Text("  %s = %.3f [readonly]", displayName, currentVal);
                        } else if (p->uiWidget == "Color") {
                            float color[3] = { currentVal, p->defaultFloat[1], p->defaultFloat[2] };
                            ImGui::Text("  %s", displayName);
                            ImGui::SameLine();
                            ImGui::ColorEdit3(label, color, ImGuiColorEditFlags_NoInputs);
                        } else {
                            // Slider (default)
                            float val = currentVal;
                            ImGui::Text("  %s", displayName);
                            ImGui::SameLine();
                            ImGui::SetNextItemWidth(150);
                            if (ImGui::SliderFloat(label, &val, p->uiMin, p->uiMax)) {
                                // TODO: write-back via standalone pipeline (ENB removed)
                            }
                        }
                    }
                    if (group != "(ungrouped)") ImGui::TreePop();
                }
            }
        }

        if (byShader.empty()) {
            ImGui::TextDisabled("No editable parameters found");
        }
    }

    // ══════════════════════════════════════════════════════════════════════
    //  GAME STATE EDITOR (Direct engine write-back via ImGui)
    // ══════════════════════════════════════════════════════════════════════

    static void ApplyGameEdits()
    {
        auto& e = s_gameEdit;

        if (e.fovActive) {
            if (auto* cam = RE::PlayerCamera::GetSingleton())
                cam->worldFOV = e.fov;
        }

        auto* sky = RE::Sky::GetSingleton();
        auto* weather = sky ? sky->currentWeather : nullptr;

        if (weather && e.fogNearActive) {
            weather->fogData.dayNear = e.fogNearDist;
            weather->fogData.nightNear = e.fogNearDist;
        }
        if (weather && e.fogFarActive) {
            weather->fogData.dayFar = e.fogFarDist;
            weather->fogData.nightFar = e.fogFarDist;
        }
        if (weather && e.fogDensityActive) {
            weather->fogData.dayPower = e.fogPower;
            weather->fogData.nightPower = e.fogPower;
            weather->fogData.dayMax = e.fogMaxOpacity;
            weather->fogData.nightMax = e.fogMaxOpacity;
        }

        if (auto* ism = RE::ImageSpaceManager::GetSingleton()) {
            if (e.hdrActive) {
                ism->data.baseData.hdr.eyeAdaptSpeed  = e.hdrAdaptSpeed;
                ism->data.baseData.hdr.bloomScale     = e.hdrBloomScale;
                ism->data.baseData.hdr.bloomThreshold = e.hdrBloomThresh;
                ism->data.baseData.hdr.sunlightScale  = e.hdrSunScale;
            }
            if (e.cinematicActive) {
                ism->data.baseData.cinematic.saturation = e.cineSat;
                ism->data.baseData.cinematic.brightness = e.cineBright;
                ism->data.baseData.cinematic.contrast   = e.cineContrast;
                ism->data.baseData.tint.amount          = e.cineTintAmount;
            }
            if (e.tintActive) {
                auto to8 = [](float f) -> std::uint8_t {
                    return static_cast<std::uint8_t>(std::clamp(f * 255.0f, 0.0f, 255.0f));
                };
                ism->data.baseData.tint.color.red   = to8(e.tintColor[0]);
                ism->data.baseData.tint.color.green = to8(e.tintColor[1]);
                ism->data.baseData.tint.color.blue  = to8(e.tintColor[2]);
            }
            if (e.dofActive) {
                ism->data.baseData.depthOfField.strength = e.dofStrength;
                ism->data.baseData.depthOfField.distance = e.dofDist;
                ism->data.baseData.depthOfField.range    = e.dofRange;
            }
        }

        if (sky && sky->sun && sky->sun->light.get() && e.sunlightActive) {
            auto* niLight = reinterpret_cast<RE::NiLight*>(sky->sun->light.get());
            auto& diffuse = niLight->GetLightRuntimeData().diffuse;
            diffuse.red   = e.sunlightColor[0];
            diffuse.green = e.sunlightColor[1];
            diffuse.blue  = e.sunlightColor[2];
        }

        if (e.timescaleActive) {
            if (auto* cal = RE::Calendar::GetSingleton()) {
                if (cal->timeScale) cal->timeScale->value = e.timescale;
            }
        }
    }

    static void RenderGameEditor()
    {
        auto& e = s_gameEdit;
        bool anyActive = false;

        // Sync initial values from live game data on first frame an edit becomes active
        auto SyncFromData = [](bool& active, auto syncFn) {
            if (!active) {
                // Pre-fill from current game state when user first checks the box
            }
        };

        ImGui::TextColored(ImVec4(0.6f, 0.75f, 1.0f, 1.0f),
            "Live Game State Editor");
        ImGui::TextDisabled("Check a box to override. Uncheck to release back to engine.");
        ImGui::Spacing();

        // ── Camera ──────────────────────────────────────────────────────
        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (!e.fovActive) e.fov = s_data.camera.Params.x * 57.2957795f;
            ImGui::Checkbox("FOV##edit", &e.fovActive);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Override camera field of view.\nEngine recomputes each frame; override re-applies continuously.");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(200);
            ImGui::SliderFloat("##fov", &e.fov, 40.0f, 120.0f, "%.1f deg");
            anyActive |= e.fovActive;
        }

        // ── Fog ─────────────────────────────────────────────────────────
        if (ImGui::CollapsingHeader("Fog", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (!e.fogNearActive) e.fogNearDist = s_data.fog.NearColor.w;
            ImGui::Checkbox("Near Distance##edit", &e.fogNearActive);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Override fog near plane distance (units).\nSmaller = fog starts closer to camera.");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(200);
            ImGui::DragFloat("##fognear", &e.fogNearDist, 10.0f, 0.0f, 100000.0f, "%.0f");
            anyActive |= e.fogNearActive;

            if (!e.fogFarActive) e.fogFarDist = s_data.fog.FarColor.w;
            ImGui::Checkbox("Far Distance##edit", &e.fogFarActive);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Override fog far plane distance (units).\nLarger = fog takes longer to reach full opacity.");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(200);
            ImGui::DragFloat("##fogfar", &e.fogFarDist, 100.0f, 0.0f, 500000.0f, "%.0f");
            anyActive |= e.fogFarActive;

            if (!e.fogDensityActive) {
                e.fogPower = s_data.fog.Density.x;
                e.fogMaxOpacity = s_data.fog.Density.y;
            }
            ImGui::Checkbox("Density##edit", &e.fogDensityActive);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Override fog density curve.\nPower controls falloff shape, Max caps opacity.");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120);
            ImGui::SliderFloat("Power##fogpow", &e.fogPower, 0.0f, 5.0f);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120);
            ImGui::SliderFloat("Max##fogmax", &e.fogMaxOpacity, 0.0f, 1.0f);
            anyActive |= e.fogDensityActive;
        }

        // ── Image Space ─────────────────────────────────────────────────
        if (ImGui::CollapsingHeader("Image Space", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (!e.hdrActive) {
                e.hdrAdaptSpeed  = s_data.imageSpace.HDR.x;
                e.hdrBloomScale  = s_data.imageSpace.HDR.y;
                e.hdrBloomThresh = s_data.imageSpace.HDR.z;
                e.hdrSunScale    = s_data.imageSpace.HDR.w;
            }
            ImGui::Checkbox("HDR##edit", &e.hdrActive);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Override HDR image-space parameters.\nAffects eye adaptation, bloom, and sun glare.");
            if (e.hdrActive) {
                ImGui::Indent();
                ImGui::SetNextItemWidth(180);
                ImGui::SliderFloat("Adapt Speed", &e.hdrAdaptSpeed, 0.01f, 5.0f);
                ImGui::SetNextItemWidth(180);
                ImGui::SliderFloat("Bloom Scale", &e.hdrBloomScale, 0.0f, 5.0f);
                ImGui::SetNextItemWidth(180);
                ImGui::SliderFloat("Bloom Thresh", &e.hdrBloomThresh, 0.0f, 2.0f);
                ImGui::SetNextItemWidth(180);
                ImGui::SliderFloat("Sun Scale", &e.hdrSunScale, 0.0f, 5.0f);
                ImGui::Unindent();
            }
            anyActive |= e.hdrActive;

            if (!e.cinematicActive) {
                e.cineSat         = s_data.imageSpace.Cinematic.x;
                e.cineBright      = s_data.imageSpace.Cinematic.y;
                e.cineContrast    = s_data.imageSpace.Cinematic.z;
                e.cineTintAmount  = s_data.imageSpace.Cinematic.w;
            }
            ImGui::Checkbox("Cinematic##edit", &e.cinematicActive);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Override cinematic color grading.\nSaturation, brightness, contrast, and tint amount.");
            if (e.cinematicActive) {
                ImGui::Indent();
                ImGui::SetNextItemWidth(180);
                ImGui::SliderFloat("Saturation", &e.cineSat, 0.0f, 3.0f);
                ImGui::SetNextItemWidth(180);
                ImGui::SliderFloat("Brightness", &e.cineBright, 0.0f, 3.0f);
                ImGui::SetNextItemWidth(180);
                ImGui::SliderFloat("Contrast", &e.cineContrast, 0.0f, 3.0f);
                ImGui::SetNextItemWidth(180);
                ImGui::SliderFloat("Tint Amount", &e.cineTintAmount, 0.0f, 1.0f);
                ImGui::Unindent();
            }
            anyActive |= e.cinematicActive;

            if (!e.tintActive) {
                e.tintColor[0] = s_data.imageSpace.CineTint.x;
                e.tintColor[1] = s_data.imageSpace.CineTint.y;
                e.tintColor[2] = s_data.imageSpace.CineTint.z;
            }
            ImGui::Checkbox("Tint Color##edit", &e.tintActive);
            ImGui::SameLine();
            ImGui::ColorEdit3("##tint", e.tintColor, ImGuiColorEditFlags_NoInputs);
            anyActive |= e.tintActive;

            if (!e.dofActive) {
                e.dofStrength = s_data.imageSpace.DOF.x;
                e.dofDist     = s_data.imageSpace.DOF.y;
                e.dofRange    = s_data.imageSpace.DOF.z;
            }
            ImGui::Checkbox("Depth of Field##edit", &e.dofActive);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Override vanilla depth of field.\nStrength, focus distance, and range.");
            if (e.dofActive) {
                ImGui::Indent();
                ImGui::SetNextItemWidth(180);
                ImGui::SliderFloat("Strength##dof", &e.dofStrength, 0.0f, 3.0f);
                ImGui::SetNextItemWidth(180);
                ImGui::DragFloat("Distance##dof", &e.dofDist, 10.0f, 0.0f, 50000.0f, "%.0f");
                ImGui::SetNextItemWidth(180);
                ImGui::DragFloat("Range##dof", &e.dofRange, 10.0f, 0.0f, 50000.0f, "%.0f");
                ImGui::Unindent();
            }
            anyActive |= e.dofActive;
        }

        // ── Lighting ────────────────────────────────────────────────────
        if (ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (!e.sunlightActive) {
                e.sunlightColor[0] = s_data.atmosphere.SunlightColor.x;
                e.sunlightColor[1] = s_data.atmosphere.SunlightColor.y;
                e.sunlightColor[2] = s_data.atmosphere.SunlightColor.z;
            }
            ImGui::Checkbox("Sunlight Color##edit", &e.sunlightActive);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Override directional sunlight diffuse color.\nWritten to RE::Sky::sun->light.");
            ImGui::SameLine();
            ImGui::ColorEdit3("##sunlight", e.sunlightColor, ImGuiColorEditFlags_NoInputs);
            anyActive |= e.sunlightActive;
        }

        // ── Time ────────────────────────────────────────────────────────
        if (ImGui::CollapsingHeader("Time")) {
            if (!e.timescaleActive)
                e.timescale = 20.0f;
            ImGui::Checkbox("Timescale##edit", &e.timescaleActive);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Override game timescale (default 20x).\n0 = frozen time, 100 = max fast-forward.");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(200);
            ImGui::SliderFloat("##ts", &e.timescale, 0.0f, 100.0f, "%.1fx");
            anyActive |= e.timescaleActive;
        }

        // ── Reset all ───────────────────────────────────────────────────
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if (anyActive) {
            if (ImGui::Button("Reset All Overrides")) {
                e = GameEditState{};
            }
            ImGui::SameLine();
            int count = (int)e.fovActive + (int)e.fogNearActive + (int)e.fogFarActive +
                (int)e.fogDensityActive + (int)e.hdrActive + (int)e.cinematicActive +
                (int)e.tintActive + (int)e.dofActive + (int)e.sunlightActive +
                (int)e.timescaleActive;
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%d override(s) active", count);
        } else {
            ImGui::TextDisabled("No overrides active — engine running naturally");
        }
    }

    // ══════════════════════════════════════════════════════════════════════
    //  BINDING STATUS (Parameter bindings — ENB GUI removed)
    // ══════════════════════════════════════════════════════════════════════

    static void RenderBindingStatus()
    {
        // Annotation DB
        if (ImGui::CollapsingHeader("Annotation Database", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& db = SB::AnnotationDatabase::Get();
            ImGui::Text("Annotation DB: %d params, %d shaders, gen %d",
                db.GetParameterCount(), db.GetShaderCount(), db.GetGeneration());

            auto shaders = db.GetAllShaderNames();
            for (auto& s : shaders) {
                auto params = db.GetParametersForShader(s);
                int realCount = 0;
                for (auto* p : params)
                    if (p->hlslType != "string" && !p->isSeparator)
                        ++realCount;
                if (realCount > 0)
                    ImGui::Text("  %s: %d params", s.c_str(), realCount);
            }
        }

        // Binding Engine
        if (ImGui::CollapsingHeader("Parameter Bindings")) {
            auto& pbe = SB::ParameterBindingEngine::Get();
            ImGui::Text("Rules: %d  Active: %d", pbe.GetRuleCount(), pbe.GetActiveCount());

            auto& rules = pbe.GetRules();
            for (size_t i = 0; i < rules.size() && i < 20; ++i) {
                auto& r = rules[i];
                ImGui::Text("  %s -> %s [%s] %s",
                    r.sourceKey.c_str(),
                    r.targetKey.c_str(),
                    r.property.c_str(),
                    r.conditionMet ? "ACTIVE" : "inactive");
            }
            if (rules.size() > 20)
                ImGui::TextDisabled("  ... %zu more", rules.size() - 20);
        }
    }

    // ══════════════════════════════════════════════════════════════════════

    static void ApplyStyle()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 6.0f;
        style.FrameRounding = 4.0f;
        style.GrabRounding = 3.0f;
        style.TabRounding = 4.0f;
        style.ScrollbarRounding = 6.0f;
        style.WindowPadding = ImVec2(10, 10);
        style.FramePadding = ImVec2(6, 3);
        style.ItemSpacing = ImVec2(8, 5);
        style.IndentSpacing = 16.0f;
        style.ScrollbarSize = 12.0f;

        auto* colors = style.Colors;
        // Dark blue-gray theme
        colors[ImGuiCol_WindowBg]           = ImVec4(0.08f, 0.09f, 0.12f, 0.96f);
        colors[ImGuiCol_ChildBg]            = ImVec4(0.07f, 0.08f, 0.11f, 0.0f);
        colors[ImGuiCol_Border]             = ImVec4(0.20f, 0.22f, 0.30f, 0.60f);
        colors[ImGuiCol_FrameBg]            = ImVec4(0.12f, 0.13f, 0.18f, 1.0f);
        colors[ImGuiCol_FrameBgHovered]     = ImVec4(0.18f, 0.20f, 0.28f, 1.0f);
        colors[ImGuiCol_FrameBgActive]      = ImVec4(0.22f, 0.25f, 0.35f, 1.0f);
        colors[ImGuiCol_TitleBg]            = ImVec4(0.06f, 0.07f, 0.10f, 1.0f);
        colors[ImGuiCol_TitleBgActive]      = ImVec4(0.10f, 0.14f, 0.22f, 1.0f);
        colors[ImGuiCol_Tab]                = ImVec4(0.12f, 0.14f, 0.20f, 1.0f);
        colors[ImGuiCol_TabHovered]         = ImVec4(0.22f, 0.28f, 0.42f, 1.0f);
        colors[ImGuiCol_TabActive]          = ImVec4(0.18f, 0.24f, 0.38f, 1.0f);
        colors[ImGuiCol_Header]             = ImVec4(0.14f, 0.16f, 0.24f, 1.0f);
        colors[ImGuiCol_HeaderHovered]      = ImVec4(0.20f, 0.24f, 0.36f, 1.0f);
        colors[ImGuiCol_HeaderActive]       = ImVec4(0.24f, 0.30f, 0.44f, 1.0f);
        colors[ImGuiCol_Separator]          = ImVec4(0.20f, 0.22f, 0.30f, 0.50f);
        colors[ImGuiCol_Button]             = ImVec4(0.16f, 0.20f, 0.30f, 1.0f);
        colors[ImGuiCol_ButtonHovered]      = ImVec4(0.22f, 0.28f, 0.42f, 1.0f);
        colors[ImGuiCol_ButtonActive]       = ImVec4(0.28f, 0.36f, 0.52f, 1.0f);
        colors[ImGuiCol_SliderGrab]         = ImVec4(0.30f, 0.45f, 0.70f, 1.0f);
        colors[ImGuiCol_SliderGrabActive]   = ImVec4(0.40f, 0.55f, 0.80f, 1.0f);
        colors[ImGuiCol_CheckMark]          = ImVec4(0.35f, 0.60f, 0.95f, 1.0f);
        colors[ImGuiCol_ScrollbarBg]        = ImVec4(0.06f, 0.07f, 0.10f, 0.50f);
        colors[ImGuiCol_ScrollbarGrab]      = ImVec4(0.20f, 0.22f, 0.30f, 1.0f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.28f, 0.32f, 0.42f, 1.0f);
        colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.35f, 0.40f, 0.52f, 1.0f);
        colors[ImGuiCol_Text]              = ImVec4(0.86f, 0.88f, 0.92f, 1.0f);
        colors[ImGuiCol_TextDisabled]      = ImVec4(0.40f, 0.42f, 0.48f, 1.0f);
    }

    // ═══════════════════════════════════════════════════════════════════════
    //  Developer Tool: Weather Transition Analyzer
    // ═══════════════════════════════════════════════════════════════════════

    static void UpdateWeatherHistory()
    {
        const auto& w  = s_data.weather;
        const auto& a  = s_data.atmosphere;
        const auto& f  = s_data.fog;
        const auto& c  = s_data.celestial;

        s_wxWind.Push(w.Wind.x);
        s_wxPrecip.Push(w.Precipitation.y);
        s_wxFogNear.Push(f.NearColor.w);
        s_wxFogFar.Push(f.FarColor.w);
        s_wxSunElev.Push(c.SunDirection.w);
        s_wxAmbientLum.Push(a.Ambient.x * 0.299f + a.Ambient.y * 0.587f + a.Ambient.z * 0.114f);
        s_wxTransition.Push(w.Transition.x);
        // Temperature proxy: higher sun = warmer
        s_wxTemp.Push(c.SunDirection.w * 40.0f + 10.0f);
    }

    static void RenderWeatherAnalyzer()
    {
        const auto& w  = s_data.weather;
        const auto& a  = s_data.atmosphere;
        const auto& c  = s_data.celestial;
        const auto& f  = s_data.fog;

        // Current weather status
        ImGui::TextColored(ImVec4(0.5f, 0.85f, 1.0f, 1.0f), "Current Conditions");
        ImGui::Separator();

        const char* precipLabel = "None";
        if (w.Precipitation.x > 1.5f) precipLabel = "Snow";
        else if (w.Precipitation.x > 0.5f) precipLabel = "Rain";
        ImGui::Text("Precip: %s (%.0f%%)", precipLabel, w.Precipitation.y * 100.f);
        ImGui::Text("Wind: %.1f m/s  dir=%.0f deg", w.Wind.x, w.Wind.y * 57.2958f);
        ImGui::Text("Lightning: freq=%.2f flashing=%s",
            w.Lightning.x, w.Lightning.y > 0.5f ? "YES" : "no");

        // Flags row
        ImGui::Text("Flags:");
        ImGui::SameLine();
        if (w.Flags.x > 0.5f) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "[Pleasant]"); }
        if (w.Flags.y > 0.5f) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.8f, 1.0f), "[Cloudy]"); }
        if (w.Flags.z > 0.5f) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.3f, 0.5f, 1.0f, 1.0f), "[Rainy]"); }
        if (w.Flags.w > 0.5f) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.9f, 0.95f, 1.0f, 1.0f), "[Snowy]"); }

        // Transition status
        if (w.Transition.x > 0.01f && w.Transition.x < 0.99f) {
            ImGui::ProgressBar(w.Transition.x, ImVec2(-1, 0),
                "Weather Transition");
            ImGui::Text("From: 0x%X -> To: 0x%X",
                static_cast<uint32_t>(w.Transition.y), static_cast<uint32_t>(w.Transition.z));
        }

        // Surface conditions
        ImGui::Text("Surface: wet=%.0f%% puddles=%.0f%% snow=%.0f%%",
            w.PrecipSurface.x * 100.f, w.PrecipSurface.y * 100.f, w.PrecipSurface.z * 100.f);

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.5f, 0.85f, 1.0f, 1.0f), "Atmosphere");
        ImGui::Separator();
        F4Color("Sky Upper", a.SkyUpper);
        F4Color("Sky Lower", a.SkyLower);
        F4Color("Horizon",   a.Horizon);
        F4Color("Ambient",   a.Ambient);
        F4Color("Sunlight",  a.SunlightColor);

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.5f, 0.85f, 1.0f, 1.0f), "Timeline Graphs (4s window)");
        ImGui::Separator();

        const float graphW = ImGui::GetContentRegionAvail().x;
        const float graphH = 35.0f;

        auto PlotGraph = [&](const char* label, WeatherRing& ring, const char* overlay, float scaleMin, float scaleMax) {
            ImGui::Text("%s", label);
            ImGui::SameLine(120.0f);
            ImGui::PlotLines("", WeatherRing::Getter, &ring, kWeatherHistoryLen,
                0, overlay, scaleMin, scaleMax, ImVec2(graphW - 130.f, graphH));
        };

        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f", w.Wind.x);
        PlotGraph("Wind Speed", s_wxWind, buf, 0.0f, 30.0f);

        snprintf(buf, sizeof(buf), "%.0f%%", w.Precipitation.y * 100.f);
        PlotGraph("Precip", s_wxPrecip, buf, 0.0f, 1.0f);

        snprintf(buf, sizeof(buf), "%.0f%%", w.Transition.x * 100.f);
        PlotGraph("Transition", s_wxTransition, buf, 0.0f, 1.0f);

        snprintf(buf, sizeof(buf), "%.1f rad", c.SunDirection.w);
        PlotGraph("Sun Elev", s_wxSunElev, buf, -1.6f, 1.6f);

        snprintf(buf, sizeof(buf), "%.3f", a.Ambient.x * 0.299f + a.Ambient.y * 0.587f + a.Ambient.z * 0.114f);
        PlotGraph("Ambient Lum", s_wxAmbientLum, buf, 0.0f, 1.0f);

        snprintf(buf, sizeof(buf), "%.0f", f.NearColor.w);
        PlotGraph("Fog Near", s_wxFogNear, buf, 0.0f, 10000.f);

        snprintf(buf, sizeof(buf), "%.0f", f.FarColor.w);
        PlotGraph("Fog Far", s_wxFogFar, buf, 0.0f, 100000.f);

        // Fog color transition
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.5f, 0.85f, 1.0f, 1.0f), "Fog");
        ImGui::Separator();
        F4Color("Near Color", f.NearColor);
        F4Color("Far Color",  f.FarColor);
        ImGui::Text("Density: power=%.3f maxOpacity=%.2f", f.Density.x, f.Density.y);
    }

    // ═══════════════════════════════════════════════════════════════════════
    //  Developer Tool: Shader Source Viewer
    // ═══════════════════════════════════════════════════════════════════════

    // Shared source display helper — renders HLSL with syntax coloring
    static void RenderSourceCode(const std::string& source, const char* childId,
                                  float height = 0.f)
    {
        ImGui::BeginChild(childId, ImVec2(0, height), true,
            ImGuiWindowFlags_HorizontalScrollbar);
        int lineNo = 1;
        std::string::size_type start = 0;
        while (start < source.size()) {
            auto end = source.find('\n', start);
            if (end == std::string::npos) end = source.size();
            std::string line = source.substr(start, end - start);
            if (!line.empty() && line.back() == '\r') line.pop_back();

            // Syntax coloring
            bool isTech = (line.find("technique") != std::string::npos &&
                           (line.find("technique10") != std::string::npos ||
                            line.find("technique11") != std::string::npos ||
                            line.find("technique ") != std::string::npos));
            bool isInclude = (line.find(">>> #include") != std::string::npos ||
                              line.find("<<< end #include") != std::string::npos);
            bool isSBParam = (line.find("SB_") != std::string::npos &&
                              line.find("float4") != std::string::npos);

            if (isTech)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.6f, 1.0f));
            else if (isInclude)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.7f, 0.9f, 1.0f));
            else if (isSBParam)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.4f, 1.0f));

            ImGui::Text("%4d | %s", lineNo, line.c_str());

            if (isTech || isInclude || isSBParam)
                ImGui::PopStyleColor();

            start = end + 1;
            ++lineNo;
        }
        ImGui::EndChild();
    }

    static void RenderShaderSourceViewer()
    {
        // ── Tab bar: ENB Shaders (disk) | Compilations (captured) ────
        if (ImGui::BeginTabBar("##ShaderViewerTabs")) {

            // ══════════════════════════════════════════════════════════
            //  Tab 1: ENB Shaders — load .fx from disk, resolve #include
            // ══════════════════════════════════════════════════════════
            if (ImGui::BeginTabItem("ENB Shaders")) {
                // Scan on first open or when Rescan clicked
                if (!s_fxScanned) ScanFxFiles();

                if (ImGui::Button("Rescan")) ScanFxFiles();
                ImGui::SameLine();
                ImGui::Text("%zu .fx files in enbseries/", s_fxFiles.size());
                if (s_fxFiles.empty()) {
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f),
                        "No .fx files found at: %s", s_enbDir.string().c_str());
                }
                ImGui::Separator();

                // Left panel: file list
                ImGui::BeginChild("##FxList", ImVec2(220, 0), true);
                for (int i = 0; i < static_cast<int>(s_fxFiles.size()); ++i) {
                    auto& fe = s_fxFiles[i];
                    char label[128];
                    if (fe.loaded) {
                        snprintf(label, sizeof(label), "%s (%d tech)##fx%d",
                                 fe.name.c_str(), fe.techCount, i);
                    } else {
                        snprintf(label, sizeof(label), "%s##fx%d",
                                 fe.name.c_str(), i);
                    }

                    if (ImGui::Selectable(label, s_fxSelIdx == i)) {
                        s_fxSelIdx = i;
                        if (!fe.loaded) LoadFxFile(fe);
                    }
                }
                ImGui::EndChild();

                ImGui::SameLine();

                // Right panel: source display
                ImGui::BeginChild("##FxDetail", ImVec2(0, 0), true);
                if (s_fxSelIdx >= 0 && s_fxSelIdx < static_cast<int>(s_fxFiles.size())) {
                    auto& fe = s_fxFiles[s_fxSelIdx];
                    if (!fe.loaded) LoadFxFile(fe);

                    ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.6f, 1.0f), "%s",
                                       fe.name.c_str());
                    ImGui::Text("%d lines | %d techniques | %s",
                                fe.lineCount, fe.techCount,
                                fe.path.string().c_str());

                    if (fe.techCount >= 128) {
                        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.3f, 1.0f),
                            "AT TECHNIQUE LIMIT (128 max for prepass)");
                    }

                    // Reload button
                    if (ImGui::Button("Reload")) {
                        fe.loaded = false;
                        LoadFxFile(fe);
                    }
                    ImGui::Separator();

                    RenderSourceCode(fe.source, "##FxSource");
                } else {
                    ImGui::TextDisabled("Select a .fx file from the list.");
                }
                ImGui::EndChild();

                ImGui::EndTabItem();
            }

            // ══════════════════════════════════════════════════════════
            //  Tab 2: Compilations — captured D3DCompile calls
            // ══════════════════════════════════════════════════════════
            if (ImGui::BeginTabItem("Compilations")) {
                auto& dbg = Debug::ShaderDebug::Get();
                if (!dbg.IsInstalled()) {
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f),
                        "ShaderDebug not installed.");
                    ImGui::EndTabItem();
                    ImGui::EndTabBar();
                    return;
                }

                const auto& attempts = dbg.GetAttempts();
                size_t total  = dbg.TotalAttempts();
                size_t errors = dbg.ErrorCount();
                size_t warns  = dbg.WarningCount();

                // Summary bar
                ImGui::Text("Compilations: %zu total", total);
                ImGui::SameLine();
                if (errors > 0)
                    ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.3f, 1.0f),
                        "| %zu errors", errors);
                else
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "| 0 errors");
                ImGui::SameLine();
                if (warns > 0)
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f),
                        "| %zu warnings", warns);

                // Capture to disk toggle
                ImGui::SameLine(0, 20.f);
                bool capturing = dbg.IsCaptureEnabled();
                if (ImGui::Checkbox("Capture to disk", &capturing))
                    dbg.SetCaptureEnabled(capturing);
                if (capturing) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f),
                        "(%d saved)", dbg.CapturedCount());
                }

                ImGui::Separator();

                // Filter
                ImGui::SetNextItemWidth(200.f);
                ImGui::InputText("Filter##shader", s_shaderFilter,
                    sizeof(s_shaderFilter));
                ImGui::SameLine();
                static bool showFailedOnly = false;
                ImGui::Checkbox("Failed only", &showFailedOnly);

                // Left panel: compilation list
                ImGui::BeginChild("##ShaderList", ImVec2(240, 0), true);
                for (int i = static_cast<int>(attempts.size()) - 1; i >= 0; --i) {
                    const auto& a = attempts[i];

                    if (s_shaderFilter[0]) {
                        std::string src = a.sourceFile;
                        std::string ep  = a.entryPoint;
                        std::string flt = s_shaderFilter;
                        for (auto& ch : src) ch = static_cast<char>(::tolower(ch));
                        for (auto& ch : ep)  ch = static_cast<char>(::tolower(ch));
                        for (auto& ch : flt) ch = static_cast<char>(::tolower(ch));
                        if (src.find(flt) == std::string::npos &&
                            ep.find(flt) == std::string::npos)
                            continue;
                    }
                    if (showFailedOnly && a.succeeded) continue;

                    ImVec4 col = a.succeeded ?
                        ImVec4(0.5f, 0.8f, 0.5f, 1.0f) :
                        ImVec4(1.0f, 0.4f, 0.35f, 1.0f);

                    ImGui::PushStyleColor(ImGuiCol_Text, col);
                    char label[128];
                    snprintf(label, sizeof(label), "%s %s##%d",
                        a.succeeded ? "OK" : "FAIL",
                        a.sourceFile.empty() ? a.entryPoint.c_str()
                                             : a.sourceFile.c_str(), i);

                    if (ImGui::Selectable(label, s_shaderViewIdx == i))
                        s_shaderViewIdx = i;
                    ImGui::PopStyleColor();

                    if (ImGui::IsItemHovered()) {
                        ImGui::BeginTooltip();
                        ImGui::Text("File: %s", a.sourceFile.c_str());
                        ImGui::Text("Entry: %s", a.entryPoint.c_str());
                        ImGui::Text("Profile: %s", a.profile.c_str());
                        ImGui::Text("Time: %.1f ms", a.compileTimeMs);
                        ImGui::Text("Errors: %zu  Warnings: %zu",
                                    a.errors.size(), a.warnings.size());
                        ImGui::EndTooltip();
                    }
                }
                ImGui::EndChild();

                ImGui::SameLine();

                // Right panel: detail
                ImGui::BeginChild("##ShaderDetail", ImVec2(0, 0), true);
                if (s_shaderViewIdx >= 0 &&
                    s_shaderViewIdx < static_cast<int>(attempts.size())) {
                    const auto& a = attempts[s_shaderViewIdx];

                    ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.6f, 1.0f), "%s",
                                       a.sourceFile.c_str());
                    ImGui::Text("Entry: %s  Profile: %s  Time: %.1f ms",
                        a.entryPoint.c_str(), a.profile.c_str(),
                        a.compileTimeMs);
                    ImGui::Text("Status: %s",
                        a.succeeded ? "SUCCESS" : "FAILED");
                    ImGui::Separator();

                    // Errors
                    if (!a.errors.empty()) {
                        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.3f, 1.0f),
                            "Errors (%zu):", a.errors.size());
                        for (const auto& e : a.errors) {
                            ImGui::TextColored(ImVec4(0.9f, 0.55f, 1.0f, 1.0f),
                                "  [%s]", e.errorCode.c_str());
                            ImGui::SameLine();
                            if (e.line >= 0) {
                                ImGui::TextColored(
                                    ImVec4(0.85f, 0.85f, 0.5f, 1.0f),
                                    "line %d", e.line);
                                ImGui::SameLine();
                            }
                            ImGui::TextWrapped("%s", e.message.c_str());
                        }
                    }

                    // Warnings
                    if (!a.warnings.empty()) {
                        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f),
                            "Warnings (%zu):", a.warnings.size());
                        for (const auto& w : a.warnings) {
                            ImGui::TextColored(ImVec4(0.9f, 0.55f, 1.0f, 1.0f),
                                "  [%s]", w.errorCode.c_str());
                            ImGui::SameLine();
                            if (w.line >= 0) {
                                ImGui::TextColored(
                                    ImVec4(0.85f, 0.85f, 0.5f, 1.0f),
                                    "line %d", w.line);
                                ImGui::SameLine();
                            }
                            ImGui::TextWrapped("%s", w.message.c_str());
                        }
                    }

                    // Source snippets
                    if (!a.snippets.empty()) {
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.5f, 0.85f, 1.0f, 1.0f),
                            "Source Context:");
                        ImGui::Separator();
                        for (const auto& snip : a.snippets) {
                            for (int li = 0;
                                 li < static_cast<int>(snip.lines.size()); ++li) {
                                int lineNo = snip.startLine + li;
                                bool isErr = (lineNo == snip.errorLine);
                                if (isErr)
                                    ImGui::PushStyleColor(ImGuiCol_Text,
                                        ImVec4(1.0f, 0.35f, 0.3f, 1.0f));
                                ImGui::Text("%4d | %s", lineNo,
                                    snip.lines[li].c_str());
                                if (isErr) ImGui::PopStyleColor();
                            }
                            ImGui::Spacing();
                        }
                    }

                    // Raw error blob
                    if (!a.rawErrorBlob.empty() &&
                        ImGui::CollapsingHeader("Raw Error Blob")) {
                        ImGui::TextWrapped("%s", a.rawErrorBlob.c_str());
                    }

                    // Full source code
                    if (ImGui::CollapsingHeader("Full Source Code")) {
                        auto source = dbg.GetCachedSource(a.sourceFile);
                        if (source.empty()) {
                            ImGui::TextDisabled(
                                "Source not cached for this shader.");
                        } else {
                            // Technique count
                            int techCount = 0;
                            std::string::size_type pos = 0;
                            while ((pos = source.find("technique", pos))
                                   != std::string::npos) {
                                if (pos == 0 || !isalnum(source[pos - 1]))
                                    ++techCount;
                                pos += 9;
                            }
                            if (techCount > 0) {
                                ImGui::TextColored(
                                    ImVec4(1.0f, 0.85f, 0.3f, 1.0f),
                                    "Techniques found: %d", techCount);
                                if (techCount >= 128)
                                    ImGui::TextColored(
                                        ImVec4(1.0f, 0.35f, 0.3f, 1.0f),
                                        "AT TECHNIQUE LIMIT");
                            }
                            RenderSourceCode(source, "##SourceCode", 300);
                        }
                    }
                } else {
                    ImGui::TextDisabled("Select a compilation from the list.");
                }
                ImGui::EndChild();

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    //  Developer Tool: EditorID Cross-Reference (enhanced crosshair)
    // ═══════════════════════════════════════════════════════════════════════

    static void RenderEditorIDXRef()
    {
        const auto& xh = s_data.crosshair;
        const auto& cache = EditorIDCache::Get();

        ImGui::TextColored(ImVec4(0.5f, 0.85f, 1.0f, 1.0f), "Crosshair Target");
        ImGui::Separator();

        bool hasTarget = xh.Info.x > 0.5f;
        if (!hasTarget) {
            ImGui::TextDisabled("No crosshair target. Look at an object.");
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.5f, 0.85f, 1.0f, 1.0f), "EditorID Cache");
            ImGui::Separator();
            ImGui::Text("Cached entries: %zu", cache.Size());
            ImGui::Text("Installed: %s", cache.IsInstalled() ? "Yes" : "No");
            ImGui::Text("External provider: %s", cache.IsUsingExternalProvider() ? "Yes" : "No");
            return;
        }

        // Target info
        float dist = xh.Info.y;
        float formType = xh.Info.z;
        bool isActor = xh.Info.w > 0.5f;

        ImGui::Text("Distance: %.0f units", dist);
        ImGui::Text("Form Type: %.0f", formType);
        ImGui::Text("Position: (%.0f, %.0f, %.0f)", xh.Pos.x, xh.Pos.y, xh.Pos.z);
        ImGui::Text("Bound Radius: %.1f", xh.Pos.w);

        if (isActor) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "Actor Info");
            ImGui::Text("HP: %.0f%%", xh.Actor.x * 100.f);
            ImGui::Text("Level: %.0f", xh.Actor.y);
            ImGui::Text("Hostile: %s", xh.Actor.z > 0.5f ? "YES" : "No");
            ImGui::Text("Essential: %s", xh.Actor.w > 0.5f ? "YES" : "No");
        }

        // EditorID lookup — attempt to get FormID from crosshair
        // The crosshair system tracks FormType, so we can try EditorIDCache
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.5f, 0.85f, 1.0f, 1.0f), "EditorID Lookup");
        ImGui::Separator();

        // We use the crosshair reference if available via RE::
        auto* crosshairPick = RE::CrosshairPickData::GetSingleton();
        auto targetHandle = crosshairPick ? crosshairPick->target : RE::ObjectRefHandle{};
        auto targetPtr = targetHandle ? targetHandle.get() : RE::NiPointer<RE::TESObjectREFR>{};
        auto* ref = targetPtr.get();
        if (ref) {
            RE::FormID formID = ref->GetFormID();
            const auto& editorID = cache.Lookup(formID);

            ImGui::Text("Ref FormID: 0x%08X", formID);
            if (!editorID.empty()) {
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f), "EditorID: %s", editorID.c_str());
            }

            // Base form
            auto* baseForm = ref->GetBaseObject();
            if (baseForm) {
                RE::FormID baseID = baseForm->GetFormID();
                const auto& baseEditorID = cache.Lookup(baseID);
                ImGui::Text("Base FormID: 0x%08X", baseID);
                if (!baseEditorID.empty()) {
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f), "Base EditorID: %s", baseEditorID.c_str());
                }

                // Source mod
                auto* file = baseForm->GetFile(0);
                if (file) {
                    ImGui::TextColored(ImVec4(0.85f, 0.7f, 1.0f, 1.0f), "Source Mod: %s", file->fileName);
                }
            }

            // Cell info
            auto* cell = ref->GetParentCell();
            if (cell) {
                const auto& cellEditorID = cache.Lookup(cell->GetFormID());
                ImGui::Text("Cell: 0x%08X", cell->GetFormID());
                if (!cellEditorID.empty()) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f), "(%s)", cellEditorID.c_str());
                }
            }
        } else {
            ImGui::TextDisabled("CrosshairPickData unavailable.");
        }

        // Cache stats
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.5f, 0.85f, 1.0f, 1.0f), "EditorID Cache");
        ImGui::Separator();
        ImGui::Text("Cached entries: %zu", cache.Size());
    }

    // ═══════════════════════════════════════════════════════════════════════
    //  Developer Tool: Light Inspector
    // ═══════════════════════════════════════════════════════════════════════

    static void RenderLightInspector()
    {
        const auto& l = s_data.lights;
        int count = static_cast<int>(l.Summary.x);

        ImGui::TextColored(ImVec4(0.5f, 0.85f, 1.0f, 1.0f),
            "Nearby Lights: %d tracked", count);
        ImGui::Separator();

        // Summary
        ImGui::Text("Nearest distance: %.0f units", l.Summary.y);
        ImGui::Text("Total luminous flux: %.1f", l.Summary.z);
        ImGui::Text("Dominant hue: %.2f", l.Summary.w);

        ImGui::Spacing();

        // Detailed per-light info
        const Float4* positions[] = { &l.Light0PosRad, &l.Light1PosRad, &l.Light2PosRad };
        const Float4* colors[]    = { &l.Light0Color,  &l.Light1Color,  &l.Light2Color };

        const auto& cam = s_data.camera;

        for (int i = 0; i < 3; ++i) {
            const Float4& pos = *positions[i];
            const Float4& col = *colors[i];

            // Skip empty lights
            if (i >= count) continue;

            char header[32];
            snprintf(header, sizeof(header), "Light %d", i);

            if (ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent(8.0f);

                // Position and radius
                ImGui::Text("Position: (%.0f, %.0f, %.0f)", pos.x, pos.y, pos.z);
                ImGui::Text("Radius: %.0f units", pos.w);

                // Distance from camera
                float dx = pos.x - cam.WorldPos.x;
                float dy = pos.y - cam.WorldPos.y;
                float dz = pos.z - cam.WorldPos.z;
                float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
                ImGui::Text("Distance from camera: %.0f units", dist);

                // Color display with swatch
                ImVec4 lightCol(col.x, col.y, col.z, 1.0f);
                ImGui::ColorButton("##lightcol", lightCol, ImGuiColorEditFlags_NoTooltip, ImVec2(20, 20));
                ImGui::SameLine();
                ImGui::Text("Color: (%.2f, %.2f, %.2f) Intensity: %.2f", col.x, col.y, col.z, col.w);

                // Luminance
                float lum = col.x * 0.299f + col.y * 0.587f + col.z * 0.114f;
                ImGui::Text("Luminance: %.3f", lum * col.w);

                // Visual radius bar
                float radiusPct = (pos.w > 0.f) ? (dist / pos.w) : 0.f;
                radiusPct = (std::min)(radiusPct, 1.0f);
                ImGui::Text("In radius: ");
                ImGui::SameLine();
                ImVec4 barCol = (radiusPct < 0.5f) ?
                    ImVec4(0.3f, 1.0f, 0.3f, 1.0f) :
                    (radiusPct < 0.9f ? ImVec4(1.0f, 0.85f, 0.3f, 1.0f) : ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barCol);
                ImGui::ProgressBar(1.0f - radiusPct, ImVec2(150, 0));
                ImGui::PopStyleColor();

                ImGui::Unindent(8.0f);
            }
        }

        // Light budget analysis
        if (count > 0) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.5f, 0.85f, 1.0f, 1.0f), "Budget Analysis");
            ImGui::Separator();
            ImGui::Text("Skyrim's per-draw light limit: 4");
            ImGui::Text("Tracked nearby lights: %d", count);

            // If scene has data about average lights per draw
            const auto& scene = s_data.scene;
            if (scene.GeometryInfo.x > 0.f) {
                ImGui::Text("Avg lights/draw: %.1f", scene.GeometryInfo.x);
                ImGui::Text("Max lights seen: %.0f", scene.GeometryInfo.y);
                if (scene.GeometryInfo.y >= 4.f) {
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f),
                        "Light limit likely reached — some lights being dropped!");
                }
            }
        }

        // Directional light (sun/shadow)
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.5f, 0.85f, 1.0f, 1.0f), "Directional Light (Sun)");
        ImGui::Separator();
        const auto& sh = s_data.shadow;
        ImGui::Text("Direction: (%.2f, %.2f, %.2f)", sh.LightDirection.x, sh.LightDirection.y, sh.LightDirection.z);
        ImGui::Text("Intensity: %.2f", sh.LightDirection.w);
        F4Color("Diffuse", sh.LightDiffuse);
        F4Color("Ambient", sh.LightAmbient);
    }

    void Init()
    {
        s_initialized = true;
        s_windowOpen = true;
        s_dataPushEnabled = true;
        ApplyStyle();
    }

    // ── Profiler tab — GPU timing + frame capture diagnostics ────────────────
    static void RenderProfiler()
    {
        const ImVec4 kGreen  = ImVec4(0.3f, 0.85f, 0.5f, 1.0f);
        const ImVec4 kRed    = ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
        const ImVec4 kGray   = ImVec4(0.5f, 0.5f, 0.5f, 0.7f);
        const ImVec4 kYellow = ImVec4(0.9f, 0.85f, 0.3f, 1.0f);
        const ImVec4 kCyan   = ImVec4(0.3f, 0.85f, 0.9f, 1.0f);

        // ── GPU Profiler ──────────────────────────────────────────────────
        if (ImGui::CollapsingHeader("GPU Pass Profiler", ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto& prof = SB::GPUProfiler::Get();
            bool init = prof.IsInitialized();
            ImGui::TextColored(init ? kGreen : kRed, init ? "[OK]" : "[--]");
            ImGui::SameLine();
            bool en = prof.IsEnabled();
            if (ImGui::Checkbox("Enable GPU Profiler (F11)##prof", &en)) {
                if (init) prof.SetEnabled(en);
            }
            ImGui::SameLine();
            ImGui::TextColored(kGray, "(measures per-pass GPU time)");

            if (init && en) {
                auto& results = prof.GetResults();
                float totalMs = prof.GetTotalGpuMs();

                ImGui::Text("Total GPU: %.3f ms (%.0f FPS budget)",
                    totalMs, totalMs > 0.001f ? 1000.0f / totalMs : 9999.0f);
                ImGui::Separator();

                // Per-pass timing bars
                if (!results.empty()) {
                    float maxMs = 0.1f;
                    for (auto& r : results)
                        if (r.valid && r.gpuMs > maxMs) maxMs = r.gpuMs;

                    for (auto& r : results) {
                        if (!r.valid) {
                            ImGui::TextColored(kGray, "  %-24s  ---.--- ms", r.name.c_str());
                            continue;
                        }

                        // Color based on time: <1ms green, 1-4ms yellow, >4ms red
                        ImVec4 color = kGreen;
                        if (r.gpuMs > 4.0f) color = kRed;
                        else if (r.gpuMs > 1.0f) color = kYellow;

                        ImGui::TextColored(color, "  %-24s %7.3f ms", r.name.c_str(), r.gpuMs);
                        ImGui::SameLine();

                        // Progress bar showing relative GPU time
                        float frac = r.gpuMs / maxMs;
                        ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                            ImVec4(color.x * 0.8f, color.y * 0.8f, color.z * 0.8f, 0.8f));
                        ImGui::ProgressBar(frac, ImVec2(120, 14), "");
                        ImGui::PopStyleColor();
                    }
                } else {
                    ImGui::TextColored(kGray, "  (waiting for results...)");
                }

                // GPU timing history ring buffer for sparkline
                static float s_gpuHistory[240] = {};
                static int   s_gpuHistHead = 0;
                s_gpuHistory[s_gpuHistHead] = totalMs;
                s_gpuHistHead = (s_gpuHistHead + 1) % 240;

                ImGui::Spacing();
                ImGui::PlotLines("GPU ms##gpuhist",
                    [](void* data, int idx) -> float {
                        auto* hist = static_cast<float*>(data);
                        return hist[(s_gpuHistHead + idx) % 240];
                    },
                    s_gpuHistory, 240, 0, nullptr, 0.0f, 33.3f, ImVec2(0, 50));
            }
        }

        ImGui::Spacing();

        // ── Frame Capture ─────────────────────────────────────────────────
        if (ImGui::CollapsingHeader("Frame Capture", ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto& cap = SB::FrameCapture::Get();

            if (cap.IsCapturing()) {
                ImGui::TextColored(kYellow, "CAPTURING: %u / %u frames",
                    cap.GetCapturedCount(), cap.GetTargetCount());
                ImGui::SameLine();
                if (ImGui::Button("Stop##cap")) {
                    cap.StopCapture();
                }
                // Progress bar
                float progress = static_cast<float>(cap.GetCapturedCount()) /
                                 static_cast<float>(cap.GetTargetCount());
                ImGui::ProgressBar(progress, ImVec2(-1, 14));
            } else {
                static int s_captureFrames = 600;
                ImGui::SliderInt("Frames##cap", &s_captureFrames, 60, 3600);
                ImGui::SameLine();
                ImGui::TextColored(kGray, "(~%.0fs)", s_captureFrames / 60.0f);
                if (ImGui::Button("Start Capture (F10)##cap")) {
                    cap.StartCapture(static_cast<uint32_t>(s_captureFrames));
                }
                ImGui::SameLine();
                ImGui::TextColored(kGray, "Dumps to log + CSV on completion");
            }

            // Show last capture summary if available
            if (cap.HasData() && !cap.IsCapturing()) {
                auto& frames = cap.GetFrames();
                ImGui::Separator();
                ImGui::TextColored(kCyan, "Last Capture: %u frames", static_cast<uint32_t>(frames.size()));

                // Compute summary stats
                float minGpu = 999.0f, maxGpu = 0.0f, sumGpu = 0.0f;
                uint32_t framesWithPasses = 0;
                for (auto& f : frames) {
                    if (f.totalGpuMs < minGpu) minGpu = f.totalGpuMs;
                    if (f.totalGpuMs > maxGpu) maxGpu = f.totalGpuMs;
                    sumGpu += f.totalGpuMs;
                    if (!f.passes.empty()) framesWithPasses++;
                }
                float avgGpu = sumGpu / static_cast<float>(frames.size());

                ImGui::Text("  GPU ms: min=%.3f avg=%.3f max=%.3f", minGpu, avgGpu, maxGpu);
                ImGui::Text("  Frames with passes: %u/%u",
                    framesWithPasses, static_cast<uint32_t>(frames.size()));

                // Per-pass aggregate
                if (ImGui::TreeNode("Per-Pass Breakdown##cap")) {
                    std::unordered_map<std::string, std::pair<float, uint32_t>> passAgg;
                    for (auto& f : frames) {
                        for (auto& p : f.passes) {
                            auto& a = passAgg[p.name];
                            a.first += p.gpuMs;
                            a.second++;
                        }
                    }
                    for (auto& [name, agg] : passAgg) {
                        float avg = agg.second > 0 ? agg.first / agg.second : 0.0f;
                        ImGui::Text("    %-24s avg=%.3f ms  (%u runs)",
                            name.c_str(), avg, agg.second);
                    }
                    ImGui::TreePop();
                }

                // Phase transition summary
                if (ImGui::TreeNode("Phase Transitions##cap")) {
                    std::unordered_map<uint16_t, uint32_t> transCount;
                    for (auto& f : frames) {
                        for (auto& ph : f.phases) {
                            uint16_t key = (static_cast<uint16_t>(ph.oldPhase) << 8) | ph.newPhase;
                            transCount[key]++;
                        }
                    }
                    for (auto& [key, count] : transCount) {
                        uint8_t old_ = static_cast<uint8_t>(key >> 8);
                        uint8_t new_ = static_cast<uint8_t>(key & 0xFF);
                        ImGui::Text("    Phase %u -> %u: %u times", old_, new_, count);
                    }
                    ImGui::TreePop();
                }
            }
        }

        ImGui::Spacing();

        // ── Pipeline Status ───────────────────────────────────────────────
        if (ImGui::CollapsingHeader("Pipeline Status", ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto& pipeline = SB::RenderPipeline::Get();
            auto& pd = SB::PhaseDispatcher::Get();

            ImGui::TextColored(pipeline.IsInitialized() ? kGreen : kRed,
                "RenderPipeline: %s", pipeline.IsInitialized() ? "OK" : "NOT INIT");
            ImGui::TextColored(pd.IsInitialized() ? kGreen : kRed,
                "PhaseDispatcher: %s  dispatches=%u",
                pd.IsInitialized() ? "OK" : "NOT INIT", pd.GetDispatchCount());

            bool pdEnabled = pd.IsEnabled();
            if (ImGui::Checkbox("Mid-Frame Dispatch (F7)##pd", &pdEnabled))
                pd.SetEnabled(pdEnabled);

            ImGui::Text("Registered passes: %u", pipeline.GetPassCount());
            ImGui::Text("  PostDepthPrepass: %u", pipeline.GetPassCount(SB::PipelineStage::PostDepthPrepass));
            ImGui::Text("  PostGeometry:     %u", pipeline.GetPassCount(SB::PipelineStage::PostGeometry));
            ImGui::Text("  PostSky:          %u", pipeline.GetPassCount(SB::PipelineStage::PostSky));
            ImGui::Text("  PreUI:            %u", pipeline.GetPassCount(SB::PipelineStage::PreUI));
            ImGui::Text("  PrePresent:       %u", pipeline.GetPassCount(SB::PipelineStage::PrePresent));
            ImGui::Text("Managed RTs: %u", pipeline.GetRTCount());
            ImGui::Text("Screen: %ux%u", pipeline.GetScreenW(), pipeline.GetScreenH());

            // Kill switches status
            ImGui::Separator();
            ImGui::TextColored(kYellow, "Kill Switches:");
            ImGui::Text("  F7 = Mid-frame dispatch: %s",
                pd.IsEnabled() ? "ENABLED" : "DISABLED");
            ImGui::Text("  F10 = Frame Capture  F11 = GPU Profiler");
        }
    }

    // ── Renderers tab — enable/disable + settings for all GPU renderers ──────
    static void RenderRenderers()
    {
        const ImVec4 kGreen  = ImVec4(0.3f, 0.85f, 0.5f, 1.0f);
        const ImVec4 kRed    = ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
        const ImVec4 kGray   = ImVec4(0.5f, 0.5f, 0.5f, 0.7f);
        const ImVec4 kYellow = ImVec4(0.9f, 0.85f, 0.3f, 1.0f);

        ImGui::TextColored(ImVec4(0.7f, 0.8f, 1.0f, 1.0f), "Renderer Controls");
        ImGui::Separator();
        ImGui::Spacing();

        // ── Section 1: Screen-Space Effects (initialized, can toggle) ────────
        if (ImGui::CollapsingHeader("Screen-Space Effects", ImGuiTreeNodeFlags_DefaultOpen))
        {
            // VB-SSGI (Visibility Bitmask SSGI — upgraded from GTAO)
            {
                auto& r = SB::GTAORenderer::Get();
                bool init = r.IsInitialized();
                ImGui::TextColored(init ? kGreen : kRed, init ? "[OK]" : "[--]");
                ImGui::SameLine();
                bool en = r.IsEnabled();
                if (ImGui::Checkbox("VB-SSGI (AO + Bounce GI)##gtao", &en)) {
                    if (init) r.SetEnabled(en);
                }
                if (init && en) {
                    ImGui::Indent(24.0f);
                    float radius = r.GetRadius();
                    if (ImGui::SliderFloat("AO Radius##gtao", &radius, 0.1f, 5.0f, "%.2f"))
                        r.SetRadius(radius);
                    float intensity = r.GetIntensity();
                    if (ImGui::SliderFloat("AO Intensity##gtao", &intensity, 0.0f, 3.0f, "%.2f"))
                        r.SetIntensity(intensity);
                    int dirs = r.GetDirections();
                    if (ImGui::SliderInt("Directions##gtao", &dirs, 2, 8))
                        r.SetDirections(dirs);
                    int steps = r.GetSteps();
                    if (ImGui::SliderInt("Steps##gtao", &steps, 2, 16))
                        r.SetSteps(steps);
                    ImGui::Separator();
                    bool bounce = r.IsBounceEnabled();
                    if (ImGui::Checkbox("Bounce GI##gtao", &bounce))
                        r.SetBounceEnabled(bounce);
                    if (bounce) {
                        float bounceInt = r.GetBounceIntensity();
                        if (ImGui::SliderFloat("Bounce Intensity##gtao", &bounceInt, 0.0f, 2.0f, "%.2f"))
                            r.SetBounceIntensity(bounceInt);
                    }
                    ImGui::TextColored(kGray, "Output: t%u (full-res R16G16B16A16_FLOAT, .rgb=bounce .a=AO)", GTAORenderer::kSRVSlot);
                    ImGui::Unindent(24.0f);
                }
            }

            ImGui::Spacing();

            // Contact Shadows
            {
                auto& r = SB::ContactShadowRenderer::Get();
                bool init = r.IsInitialized();
                ImGui::TextColored(init ? kGreen : kRed, init ? "[OK]" : "[--]");
                ImGui::SameLine();
                bool en = r.IsEnabled();
                if (ImGui::Checkbox("Contact Shadows##cs", &en)) {
                    if (init) r.SetEnabled(en);
                }
                if (init && en) {
                    ImGui::Indent(24.0f);
                    float rayLen = r.GetRayLength();
                    if (ImGui::SliderFloat("Ray Length##cs", &rayLen, 0.01f, 0.5f, "%.3f"))
                        r.SetRayLength(rayLen);
                    float thick = r.GetThickness();
                    if (ImGui::SliderFloat("Thickness##cs", &thick, 0.001f, 0.1f, "%.4f"))
                        r.SetThickness(thick);
                    float intensity = r.GetIntensity();
                    if (ImGui::SliderFloat("Intensity##cs", &intensity, 0.0f, 3.0f, "%.2f"))
                        r.SetIntensity(intensity);
                    int steps = r.GetMaxSteps();
                    if (ImGui::SliderInt("Max Steps##cs", &steps, 4, 64))
                        r.SetMaxSteps(steps);
                    ImGui::TextColored(kGray, "Output: t%u (full-res R8_UNORM)", ContactShadowRenderer::kSRVSlot);
                    ImGui::Unindent(24.0f);
                }
            }

            ImGui::Spacing();

            // Skylighting
            {
                auto& r = SB::SkylightingRenderer::Get();
                bool init = r.IsInitialized();
                ImGui::TextColored(init ? kGreen : kRed, init ? "[OK]" : "[--]");
                ImGui::SameLine();
                bool en = r.IsEnabled();
                if (ImGui::Checkbox("Skylighting##sky", &en)) {
                    if (init) r.SetEnabled(en);
                }
                if (init && en) {
                    ImGui::Indent(24.0f);
                    float radius = r.GetRadius();
                    if (ImGui::SliderFloat("Sample Radius##sky", &radius, 0.5f, 10.0f, "%.2f"))
                        r.SetRadius(radius);
                    float intensity = r.GetIntensity();
                    if (ImGui::SliderFloat("Intensity##sky", &intensity, 0.0f, 3.0f, "%.2f"))
                        r.SetIntensity(intensity);
                    int dirs = r.GetDirections();
                    if (ImGui::SliderInt("Directions##sky", &dirs, 2, 12))
                        r.SetDirections(dirs);
                    int steps = r.GetSteps();
                    if (ImGui::SliderInt("Steps##sky", &steps, 2, 16))
                        r.SetSteps(steps);
                    ImGui::TextColored(kGray, "Output: t%u (full-res R16_FLOAT)", SkylightingRenderer::kSRVSlot);
                    ImGui::Unindent(24.0f);
                }
            }

            ImGui::Spacing();

            // SSR
            {
                auto& r = SB::SSRRenderer::Get();
                bool init = r.IsInitialized();
                ImGui::TextColored(init ? kGreen : kRed, init ? "[OK]" : "[--]");
                ImGui::SameLine();
                bool en = r.IsEnabled();
                if (ImGui::Checkbox("SSR (Reflections)##ssr", &en)) {
                    if (init) r.SetEnabled(en);
                }
                if (init && en) {
                    ImGui::Indent(24.0f);
                    float dist = r.GetMaxDistance();
                    if (ImGui::SliderFloat("Max Distance##ssr", &dist, 10.0f, 500.0f, "%.0f"))
                        r.SetMaxDistance(dist);
                    float thick = r.GetThickness();
                    if (ImGui::SliderFloat("Thickness##ssr", &thick, 0.05f, 2.0f, "%.3f"))
                        r.SetThickness(thick);
                    float intensity = r.GetIntensity();
                    if (ImGui::SliderFloat("Intensity##ssr", &intensity, 0.0f, 3.0f, "%.2f"))
                        r.SetIntensity(intensity);
                    int steps = r.GetMaxSteps();
                    if (ImGui::SliderInt("Max Steps##ssr", &steps, 8, 128))
                        r.SetMaxSteps(steps);
                    ImGui::TextColored(kGray, "Output: t%u (half-res denoised)", SSRRenderer::kSRVSlot);
                    ImGui::Unindent(24.0f);
                }
            }

            ImGui::Spacing();

            // SSGI
            {
                auto& r = SB::SSGIRenderer::Get();
                bool init = r.IsInitialized();
                ImGui::TextColored(init ? kGreen : kRed, init ? "[OK]" : "[--]");
                ImGui::SameLine();
                bool en = r.IsEnabled();
                if (ImGui::Checkbox("SSGI (Global Illumination)##ssgi", &en)) {
                    if (init) r.SetEnabled(en);
                }
                if (init && en) {
                    ImGui::Indent(24.0f);
                    float intensity = r.GetGIIntensity();
                    if (ImGui::SliderFloat("GI Intensity##ssgi", &intensity, 0.0f, 3.0f, "%.2f"))
                        r.SetGIIntensity(intensity);
                    int rays = r.GetRayCount();
                    if (ImGui::SliderInt("Ray Count##ssgi", &rays, 1, 8))
                        r.SetRayCount(rays);
                    int steps = r.GetMaxSteps();
                    if (ImGui::SliderInt("Max Steps##ssgi", &steps, 4, 64))
                        r.SetMaxSteps(steps);
                    float range = r.GetVoxelRange();
                    if (ImGui::SliderFloat("Voxel Range##ssgi", &range, 256.0f, 8192.0f, "%.0f"))
                        r.SetVoxelRange(range);
                    ImGui::TextColored(kGray, "Output: t%u (half-res, %u^3 voxels)",
                        SSGIRenderer::kSRVSlot, SSGIRenderer::kVoxelRes);
                    ImGui::Unindent(24.0f);
                }
            }
        }

        ImGui::Spacing();

        // ── Section 2: Infrastructure (initialized, always-on) ───────────────
        if (ImGui::CollapsingHeader("Infrastructure", ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto toggleRow = [&](const char* label, auto& sys) {
                bool init = sys.IsInitialized();
                ImGui::TextColored(init ? kGreen : kRed, init ? "[OK]" : "[--]");
                ImGui::SameLine();
                bool en = sys.IsEnabled();
                if (ImGui::Checkbox(label, &en))
                    sys.SetEnabled(en);
            };

            toggleRow("HiZ Depth Pyramid (t19)##hiz", SB::HiZPyramid::Get());
            toggleRow("Luminance Histogram (t17)##lum", SB::LuminanceHistogram::Get());
            toggleRow("Clustered Lighting##cl", SB::ClusteredLighting::Get());
            toggleRow("SDSM Cascades##sdsm", SB::SDSMCascades::Get());
            toggleRow("TAA Manager (t22)##taa", SB::TAAManager::Get());
            toggleRow("LUT Manager (t18)##lut", SB::LUTManager::Get());

            // Atmosphere — no enable/disable, always runs when initialized
            {
                auto& atmo = SB::AtmosphereRenderer::Get();
                bool init = atmo.IsInitialized();
                ImGui::TextColored(init ? kGreen : kRed, init ? "[OK]" : "[--]");
                ImGui::SameLine();
                ImGui::TextColored(kGray, "Atmosphere LUTs (t23/t24) — always active");
            }

            // ToneMap (no enable/disable — controlled by pipeline pass .enabled flag)
            {
                auto& tm = SB::ToneMapManager::Get();
                bool init = tm.IsInitialized();
                ImGui::TextColored(init ? kGreen : kRed, init ? "[OK]" : "[--]");
                ImGui::SameLine();
                ImGui::TextColored(kGray, "Tone Mapping — pipeline-controlled");
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f),
                "Scene Compositor (blends effects onto backbuffer)");

            // Scene Compositor — the CRITICAL system that makes effects visible
            {
                auto& sc = SB::SceneCompositor::Get();
                bool init = sc.IsInitialized();
                ImGui::TextColored(init ? kGreen : kRed, init ? "[OK]" : "[--]");
                ImGui::SameLine();
                bool en = sc.IsEnabled();
                if (ImGui::Checkbox("Scene Compositor##comp", &en)) {
                    if (init) sc.SetEnabled(en);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Composites AO/GI/SSR/Shadows/Skylighting onto backbuffer.\n"
                                      "Required for effects to be visible in proxy-only mode (no ENB).");
                if (!init)
                    ImGui::TextColored(kRed, "  NOT INITIALIZED — effects will not be visible");

                if (init && en) {
                    ImGui::Indent(24.0f);
                    float aoI = sc.GetAOIntensity();
                    if (ImGui::SliderFloat("AO Strength##comp", &aoI, 0.0f, 1.0f, "%.2f"))
                        sc.SetAOIntensity(aoI);
                    float shadowI = sc.GetShadowIntensity();
                    if (ImGui::SliderFloat("Contact Shadow##comp", &shadowI, 0.0f, 1.0f, "%.2f"))
                        sc.SetShadowIntensity(shadowI);
                    float skyI = sc.GetSkylightIntensity();
                    if (ImGui::SliderFloat("Skylighting##comp", &skyI, 0.0f, 1.0f, "%.2f"))
                        sc.SetSkylightIntensity(skyI);
                    float giI = sc.GetGIIntensity();
                    if (ImGui::SliderFloat("GI Bounce##comp", &giI, 0.0f, 1.0f, "%.2f"))
                        sc.SetGIIntensity(giI);
                    float giMax = sc.GetGIMaxAdd();
                    if (ImGui::SliderFloat("GI Max Add##comp", &giMax, 0.05f, 1.0f, "%.2f"))
                        sc.SetGIMaxAdd(giMax);
                    float ssrI = sc.GetSSRIntensity();
                    if (ImGui::SliderFloat("SSR Blend##comp", &ssrI, 0.0f, 1.0f, "%.2f"))
                        sc.SetSSRIntensity(ssrI);
                    float cloudI = sc.GetCloudIntensity();
                    if (ImGui::SliderFloat("Cloud Blend##comp", &cloudI, 0.0f, 1.0f, "%.2f"))
                        sc.SetCloudIntensity(cloudI);

                    ImGui::Separator();
                    static const char* kDebugModes[] = {
                        "Off (Composite)", "AO Raw", "GI Raw", "SSR Raw",
                        "Clouds Raw", "Contact Shadow Raw", "Skylighting Raw"
                    };
                    int dm = sc.GetDebugMode();
                    if (ImGui::Combo("Debug View##comp", &dm, kDebugModes, 7))
                        sc.SetDebugMode(dm);
                    ImGui::Unindent(24.0f);
                }
            }
        }

        ImGui::Spacing();

        // ── Section 3: Deferred Systems (lazy-init on enable) ────────────────
        if (ImGui::CollapsingHeader("Deferred Systems (lazy-init on enable)"))
        {
            ImGui::TextColored(kYellow,
                "Click enable to initialize + activate. First enable compiles shaders\n"
                "(D3DCompile OPTIMIZATION_LEVEL3) and may take a moment.");
            ImGui::Spacing();

            auto* dev = D3D11Hook::GetDevice();
            auto* dctx = D3D11Hook::GetContext();
            auto* dsc  = D3D11Hook::GetSwapChain();

            // ── Volumetric Clouds ────────────────────────────────────────
            {
                auto& vc = SB::VolumetricClouds::Get();
                bool init = vc.IsInitialized();
                ImGui::TextColored(init ? kGreen : kGray, init ? "[OK]" : "[--]");
                ImGui::SameLine();
                bool en = init ? vc.IsEnabled() : false;
                if (ImGui::Checkbox("Volumetric Clouds##volcloud", &en)) {
                    if (en && !init && dev && dctx && dsc) {
                        if (vc.Initialize(dev, dctx, dsc)) vc.SetEnabled(true);
                    } else if (init) {
                        vc.SetEnabled(en);
                    }
                }
                if (init && vc.IsEnabled()) {
                    ImGui::Indent(24.0f);
                    float cov = vc.GetCoverage();
                    if (ImGui::SliderFloat("Coverage##vc", &cov, 0.0f, 1.0f, "%.2f")) vc.SetCoverage(cov);
                    float den = vc.GetDensity();
                    if (ImGui::SliderFloat("Density##vc", &den, 0.001f, 0.5f, "%.3f")) vc.SetDensity(den);
                    float base = vc.GetCloudBase();
                    if (ImGui::SliderFloat("Cloud Base (m)##vc", &base, 500.0f, 5000.0f, "%.0f")) vc.SetCloudBase(base);
                    float top = vc.GetCloudTop();
                    if (ImGui::SliderFloat("Cloud Top (m)##vc", &top, 2000.0f, 10000.0f, "%.0f")) vc.SetCloudTop(top);
                    ImGui::Separator();
                    bool fogEn = vc.IsFogEnabled();
                    if (ImGui::Checkbox("Height Fog##vc", &fogEn)) vc.SetFogEnabled(fogEn);
                    if (fogEn) {
                        float fd = vc.GetFogDensity();
                        if (ImGui::SliderFloat("Fog Density##vc", &fd, 0.0001f, 0.01f, "%.4f")) vc.SetFogDensity(fd);
                        float fh = vc.GetFogHeight();
                        if (ImGui::SliderFloat("Fog Height##vc", &fh, 100.0f, 2000.0f, "%.0f")) vc.SetFogHeight(fh);
                    }
                    ImGui::TextColored(kGray, "Output: t%u (quarter-res cloud)", VolumetricClouds::kCloudSRVSlot);
                    ImGui::Unindent(24.0f);
                }
            }
            ImGui::Spacing();

            // ── Frame Generator ──────────────────────────────────────────
            {
                auto& fg = SB::FrameGenerator::Get();
                bool init = fg.IsInitialized();
                ImGui::TextColored(init ? kGreen : kGray, init ? "[OK]" : "[--]");
                ImGui::SameLine();
                bool en = init ? fg.IsEnabled() : false;
                if (ImGui::Checkbox("Frame Generator (DLSS 3-style)##fg", &en)) {
                    if (en && !init && dev && dsc) {
                        if (fg.Initialize(dev, dsc)) fg.SetEnabled(true);
                    } else if (init) {
                        fg.SetEnabled(en);
                    }
                }
                if (init && fg.IsEnabled()) {
                    ImGui::Indent(24.0f);
                    static const char* kQModes[] = { "Off", "Low", "High" };
                    int q = static_cast<int>(fg.GetQuality());
                    if (ImGui::Combo("Quality##fg", &q, kQModes, 3))
                        fg.SetQuality(static_cast<SB::FrameGenQuality>(q));
                    ImGui::TextColored(kGray, "Flow: %ux%u  Frame: %ux%u",
                        fg.GetFlowWidth(), fg.GetFlowHeight(), fg.GetWidth(), fg.GetHeight());
                    ImGui::Unindent(24.0f);
                }
            }
            ImGui::Spacing();

            // ── Temporal Super Resolution ────────────────────────────────
            {
                auto& tsr = SB::TemporalSuperRes::Get();
                bool init = tsr.IsInitialized();
                ImGui::TextColored(init ? kGreen : kGray, init ? "[OK]" : "[--]");
                ImGui::SameLine();
                bool en = init ? tsr.IsEnabled() : false;
                if (ImGui::Checkbox("Temporal Super Resolution##tsr", &en)) {
                    if (en && !init && dev && dctx && dsc) {
                        if (tsr.Initialize(dev, dctx, dsc)) tsr.SetEnabled(true);
                    } else if (init) {
                        tsr.SetEnabled(en);
                    }
                }
                if (init && tsr.IsEnabled()) {
                    ImGui::Indent(24.0f);
                    static const char* kTSRModes[] = { "Performance (50%)", "Balanced (67%)", "Quality (75%)", "Native (100%)" };
                    int q = static_cast<int>(tsr.GetQuality());
                    if (ImGui::Combo("Quality##tsr", &q, kTSRModes, 4))
                        tsr.SetQuality(static_cast<SB::TSRQuality>(q));
                    float sharp = tsr.GetSharpness();
                    if (ImGui::SliderFloat("Sharpness##tsr", &sharp, 0.0f, 1.0f, "%.2f"))
                        tsr.SetSharpness(sharp);
                    bool dynRes = tsr.IsDynamicResEnabled();
                    if (ImGui::Checkbox("Dynamic Resolution##tsr", &dynRes))
                        tsr.SetDynamicResEnabled(dynRes);
                    if (dynRes) {
                        float targetMs = tsr.GetTargetFrameTimeMs();
                        if (ImGui::SliderFloat("Target ms##tsr", &targetMs, 8.0f, 33.3f, "%.1f"))
                            tsr.SetTargetFrameTimeMs(targetMs);
                    }
                    ImGui::TextColored(kGray, "Render: %ux%u  Display: %ux%u  Scale: %.0f%%",
                        tsr.GetRenderWidth(), tsr.GetRenderHeight(),
                        tsr.GetDisplayWidth(), tsr.GetDisplayHeight(),
                        tsr.GetRenderScale() * 100.0f);
                    ImGui::Unindent(24.0f);
                }
            }
            ImGui::Spacing();

            // ── Grass Lighting ───────────────────────────────────────────
            {
                auto& gl = SB::GrassLightingRenderer::Get();
                bool init = gl.IsInitialized();
                ImGui::TextColored(init ? kGreen : kGray, init ? "[OK]" : "[--]");
                ImGui::SameLine();
                bool en = init ? gl.IsEnabled() : false;
                if (ImGui::Checkbox("Grass Lighting (CS replacement)##grass", &en)) {
                    if (en && !init && dev && dctx && dsc) {
                        if (gl.Initialize(dev, dctx, dsc)) gl.SetEnabled(true);
                    } else if (init) {
                        gl.SetEnabled(en);
                    }
                }
                if (init && gl.IsEnabled()) {
                    ImGui::Indent(24.0f);
                    float ab = gl.GetAmbientBoost();
                    if (ImGui::SliderFloat("Ambient Boost##grass", &ab, 0.0f, 1.0f, "%.2f"))
                        gl.SetAmbientBoost(ab);
                    float ss = gl.GetSubsurfaceStrength();
                    if (ImGui::SliderFloat("Subsurface##grass", &ss, 0.0f, 1.0f, "%.2f"))
                        gl.SetSubsurfaceStrength(ss);
                    float ml = gl.GetMultiLightIntensity();
                    if (ImGui::SliderFloat("Multi-Light##grass", &ml, 0.0f, 3.0f, "%.2f"))
                        gl.SetMultiLightIntensity(ml);
                    ImGui::Unindent(24.0f);
                }
            }
            ImGui::Spacing();

            // ── Tree LOD Lighting ────────────────────────────────────────
            {
                auto& tl = SB::TreeLODLightingRenderer::Get();
                bool init = tl.IsInitialized();
                ImGui::TextColored(init ? kGreen : kGray, init ? "[OK]" : "[--]");
                ImGui::SameLine();
                bool en = init ? tl.IsEnabled() : false;
                if (ImGui::Checkbox("Tree LOD Lighting (CS replacement)##treelod", &en)) {
                    if (en && !init && dev && dctx && dsc) {
                        if (tl.Initialize(dev, dctx, dsc)) tl.SetEnabled(true);
                    } else if (init) {
                        tl.SetEnabled(en);
                    }
                }
                if (init && tl.IsEnabled()) {
                    ImGui::Indent(24.0f);
                    float am = tl.GetAmbientMatchStrength();
                    if (ImGui::SliderFloat("Ambient Match##treelod", &am, 0.0f, 1.0f, "%.2f"))
                        tl.SetAmbientMatchStrength(am);
                    float ds = tl.GetDirectionalStrength();
                    if (ImGui::SliderFloat("Directional##treelod", &ds, 0.0f, 1.0f, "%.2f"))
                        tl.SetDirectionalStrength(ds);
                    float cm = tl.GetColorMatchBlend();
                    if (ImGui::SliderFloat("Color Match##treelod", &cm, 0.0f, 1.0f, "%.2f"))
                        tl.SetColorMatchBlend(cm);
                    ImGui::Unindent(24.0f);
                }
            }
            ImGui::Spacing();

            // ── Water Blending ───────────────────────────────────────────
            {
                auto& wb = SB::WaterBlendingRenderer::Get();
                bool init = wb.IsInitialized();
                ImGui::TextColored(init ? kGreen : kGray, init ? "[OK]" : "[--]");
                ImGui::SameLine();
                bool en = init ? wb.IsEnabled() : false;
                if (ImGui::Checkbox("Water Blending (CS replacement)##water", &en)) {
                    if (en && !init && dev && dctx && dsc) {
                        if (wb.Initialize(dev, dctx, dsc)) wb.SetEnabled(true);
                    } else if (init) {
                        wb.SetEnabled(en);
                    }
                }
                if (init && wb.IsEnabled()) {
                    ImGui::Indent(24.0f);
                    float ew = wb.GetEdgeBlendWidth();
                    if (ImGui::SliderFloat("Edge Width##water", &ew, 0.1f, 2.0f, "%.2f"))
                        wb.SetEdgeBlendWidth(ew);
                    float ci = wb.GetCausticIntensity();
                    if (ImGui::SliderFloat("Caustic Intensity##water", &ci, 0.0f, 1.0f, "%.2f"))
                        wb.SetCausticIntensity(ci);
                    float cs = wb.GetCausticScale();
                    if (ImGui::SliderFloat("Caustic Scale##water", &cs, 0.005f, 0.1f, "%.3f"))
                        wb.SetCausticScale(cs);
                    float df = wb.GetDepthFogStrength();
                    if (ImGui::SliderFloat("Depth Fog##water", &df, 0.0f, 1.0f, "%.2f"))
                        wb.SetDepthFogStrength(df);
                    ImGui::Unindent(24.0f);
                }
            }
            ImGui::Spacing();

            // ── Dynamic Cubemaps ─────────────────────────────────────────
            {
                auto& dc = SB::DynamicCubemapRenderer::Get();
                bool init = dc.IsInitialized();
                ImGui::TextColored(init ? kGreen : kGray, init ? "[OK]" : "[--]");
                ImGui::SameLine();
                bool en = init ? dc.IsEnabled() : false;
                if (ImGui::Checkbox("Dynamic Cubemaps (CS replacement)##dcube", &en)) {
                    if (en && !init && dev && dctx && dsc) {
                        if (dc.Initialize(dev, dctx, dsc)) dc.SetEnabled(true);
                    } else if (init) {
                        dc.SetEnabled(en);
                    }
                }
                if (init && dc.IsEnabled()) {
                    ImGui::Indent(24.0f);
                    float bs = dc.GetBlendSpeed();
                    if (ImGui::SliderFloat("Blend Speed##dcube", &bs, 0.01f, 1.0f, "%.3f"))
                        dc.SetBlendSpeed(bs);
                    float uf = dc.GetUpdateFrequency();
                    if (ImGui::SliderFloat("Update Freq##dcube", &uf, 0.1f, 6.0f, "%.1f"))
                        dc.SetUpdateFrequency(uf);
                    ImGui::TextColored(kGray, "Output: t%u (%u^2 per face)",
                        DynamicCubemapRenderer::kSRVSlot, dc.GetFaceResolution());
                    ImGui::Unindent(24.0f);
                }
            }
        }

        ImGui::Spacing();

        // ── Section 4: New Compute Systems (lazy-init) ───────────────────
        if (ImGui::CollapsingHeader("New Compute Systems (lazy-init)"))
        {
            ImGui::TextColored(kYellow,
                "New screen-space compute effects. Click enable to compile + activate.");
            ImGui::Spacing();

            auto* ndev = D3D11Hook::GetDevice();
            auto* nctx = D3D11Hook::GetContext();
            auto* nsc  = D3D11Hook::GetSwapChain();

            // ── Volumetric Lighting (God Rays) ───────────────────────────
            {
                auto& vl = SB::VolumetricLightingRenderer::Get();
                bool init = vl.IsInitialized();
                ImGui::TextColored(init ? kGreen : kGray, init ? "[OK]" : "[--]");
                ImGui::SameLine();
                bool en = init ? vl.IsEnabled() : false;
                if (ImGui::Checkbox("Volumetric Lighting (God Rays)##vollight", &en)) {
                    if (en && !init && ndev && nctx && nsc) {
                        if (vl.Initialize(ndev, nctx, nsc)) vl.SetEnabled(true);
                    } else if (init) {
                        vl.SetEnabled(en);
                    }
                }
                if (init && vl.IsEnabled()) {
                    ImGui::Indent(24.0f);
                    float intensity = vl.GetIntensity();
                    if (ImGui::SliderFloat("Intensity##vollight", &intensity, 0.0f, 5.0f, "%.2f"))
                        vl.SetIntensity(intensity);
                    float density = vl.GetScatterDensity();
                    if (ImGui::SliderFloat("Scatter Density##vollight", &density, 0.001f, 0.1f, "%.4f"))
                        vl.SetScatterDensity(density);
                    float aniso = vl.GetAnisotropy();
                    if (ImGui::SliderFloat("Anisotropy (HG)##vollight", &aniso, 0.0f, 0.99f, "%.2f"))
                        vl.SetAnisotropy(aniso);
                    int steps = vl.GetNumSteps();
                    if (ImGui::SliderInt("Ray Steps##vollight", &steps, 8, 128))
                        vl.SetNumSteps(steps);
                    float maxDist = vl.GetMaxDistance();
                    if (ImGui::SliderFloat("Max Distance##vollight", &maxDist, 100.0f, 20000.0f, "%.0f"))
                        vl.SetMaxDistance(maxDist);
                    ImGui::TextColored(kGray, "Output: t%u (full-res scatter+transmittance)",
                        VolumetricLightingRenderer::kSRVSlot);
                    ImGui::Unindent(24.0f);
                }
            }
            ImGui::Spacing();

            // ── Subsurface Scattering ────────────────────────────────────
            {
                auto& sss = SB::SubsurfaceScatteringRenderer::Get();
                bool init = sss.IsInitialized();
                ImGui::TextColored(init ? kGreen : kGray, init ? "[OK]" : "[--]");
                ImGui::SameLine();
                bool en = init ? sss.IsEnabled() : false;
                if (ImGui::Checkbox("Subsurface Scattering (Skin+Foliage)##sss", &en)) {
                    if (en && !init && ndev && nctx && nsc) {
                        if (sss.Initialize(ndev, nctx, nsc)) sss.SetEnabled(true);
                    } else if (init) {
                        sss.SetEnabled(en);
                    }
                }
                if (init && sss.IsEnabled()) {
                    ImGui::Indent(24.0f);
                    float radius = sss.GetSSSRadius();
                    if (ImGui::SliderFloat("Blur Radius##sss", &radius, 0.001f, 0.05f, "%.4f"))
                        sss.SetSSSRadius(radius);
                    float strength = sss.GetSSSStrength();
                    if (ImGui::SliderFloat("Strength##sss", &strength, 0.0f, 2.0f, "%.2f"))
                        sss.SetSSSStrength(strength);
                    float trans = sss.GetTranslucency();
                    if (ImGui::SliderFloat("Translucency##sss", &trans, 0.0f, 1.0f, "%.2f"))
                        sss.SetTranslucency(trans);
                    ImGui::TextColored(kGray, "In-place backbuffer (skin=Burley, foliage=broad)");
                    ImGui::Unindent(24.0f);
                }
            }
            ImGui::Spacing();

            // ── Indirect Specular ────────────────────────────────────────
            {
                auto& is = SB::IndirectSpecularRenderer::Get();
                bool init = is.IsInitialized();
                ImGui::TextColored(init ? kGreen : kGray, init ? "[OK]" : "[--]");
                ImGui::SameLine();
                bool en = init ? is.IsEnabled() : false;
                if (ImGui::Checkbox("Indirect Specular (SSR+Cubemap)##ispec", &en)) {
                    if (en && !init && ndev && nctx && nsc) {
                        if (is.Initialize(ndev, nctx, nsc)) is.SetEnabled(true);
                    } else if (init) {
                        is.SetEnabled(en);
                    }
                }
                if (init && is.IsEnabled()) {
                    ImGui::Indent(24.0f);
                    float intensity = is.GetIntensity();
                    if (ImGui::SliderFloat("Intensity##ispec", &intensity, 0.0f, 3.0f, "%.2f"))
                        is.SetIntensity(intensity);
                    float cubeFB = is.GetCubemapFallback();
                    if (ImGui::SliderFloat("Cubemap Fallback##ispec", &cubeFB, 0.0f, 1.0f, "%.2f"))
                        is.SetCubemapFallback(cubeFB);
                    float fBias = is.GetFresnelBias();
                    if (ImGui::SliderFloat("Fresnel Bias (F0)##ispec", &fBias, 0.0f, 0.3f, "%.3f"))
                        is.SetFresnelBias(fBias);
                    float rThresh = is.GetRoughnessThreshold();
                    if (ImGui::SliderFloat("Roughness Thresh##ispec", &rThresh, 0.0f, 1.0f, "%.2f"))
                        is.SetRoughnessThreshold(rThresh);
                    ImGui::TextColored(kGray, "Output: t%u (specular GI)",
                        IndirectSpecularRenderer::kSRVSlot);
                    ImGui::Unindent(24.0f);
                }
            }
            ImGui::Spacing();

            // ── Screen-Space Decals ──────────────────────────────────────
            {
                auto& sd = SB::ScreenSpaceDecalRenderer::Get();
                bool init = sd.IsInitialized();
                ImGui::TextColored(init ? kGreen : kGray, init ? "[OK]" : "[--]");
                ImGui::SameLine();
                bool en = init ? sd.IsEnabled() : false;
                if (ImGui::Checkbox("Screen-Space Decals##decal", &en)) {
                    if (en && !init && ndev && nctx && nsc) {
                        if (sd.Initialize(ndev, nctx, nsc)) sd.SetEnabled(true);
                    } else if (init) {
                        sd.SetEnabled(en);
                    }
                }
                if (init && sd.IsEnabled()) {
                    ImGui::Indent(24.0f);
                    float opac = sd.GetGlobalOpacity();
                    if (ImGui::SliderFloat("Global Opacity##decal", &opac, 0.0f, 1.0f, "%.2f"))
                        sd.SetGlobalOpacity(opac);
                    float nThresh = sd.GetNormalThreshold();
                    if (ImGui::SliderFloat("Normal Threshold##decal", &nThresh, 0.0f, 1.0f, "%.2f"))
                        sd.SetNormalThreshold(nThresh);
                    ImGui::TextColored(kGray, "Active decals: %u / %d  (in-place backbuffer)",
                        sd.GetActiveDecalCount(), sd.GetMaxDecals());
                    ImGui::Unindent(24.0f);
                }
            }
            ImGui::Spacing();

            // ── Particle Lighting ────────────────────────────────────────
            {
                auto& pl = SB::ParticleLightingRenderer::Get();
                bool init = pl.IsInitialized();
                ImGui::TextColored(init ? kGreen : kGray, init ? "[OK]" : "[--]");
                ImGui::SameLine();
                bool en = init ? pl.IsEnabled() : false;
                if (ImGui::Checkbox("Particle Lighting (Emissive Scatter)##plight", &en)) {
                    if (en && !init && ndev && nctx && nsc) {
                        if (pl.Initialize(ndev, nctx, nsc)) pl.SetEnabled(true);
                    } else if (init) {
                        pl.SetEnabled(en);
                    }
                }
                if (init && pl.IsEnabled()) {
                    ImGui::Indent(24.0f);
                    float intensity = pl.GetIntensity();
                    if (ImGui::SliderFloat("Intensity##plight", &intensity, 0.0f, 5.0f, "%.2f"))
                        pl.SetIntensity(intensity);
                    float thresh = pl.GetLuminanceThreshold();
                    if (ImGui::SliderFloat("Lum Threshold##plight", &thresh, 0.0f, 10.0f, "%.1f"))
                        pl.SetLuminanceThreshold(thresh);
                    float scatter = pl.GetScatterRadius();
                    if (ImGui::SliderFloat("Scatter Radius##plight", &scatter, 8.0f, 256.0f, "%.0f"))
                        pl.SetScatterRadius(scatter);
                    float falloff = pl.GetFalloffExponent();
                    if (ImGui::SliderFloat("Falloff##plight", &falloff, 1.0f, 4.0f, "%.1f"))
                        pl.SetFalloffExponent(falloff);
                    ImGui::TextColored(kGray, "In-place backbuffer (emissive detection + scatter)");
                    ImGui::Unindent(24.0f);
                }
            }
        }
    }

    void Shutdown()
    {
        s_initialized = false;
    }

    void Render()
    {
        if (!s_initialized)
            return;

        UpdateDirtyFlags();
        UpdateWeatherHistory();

        ImGui::SetNextWindowSize(ImVec2(540, 720), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Playground v3##DebugWin", &s_windowOpen, ImGuiWindowFlags_None)) {
            // Status dashboard (always visible)
            RenderStatusDashboard();

            // Global controls
            ImGui::Checkbox("Enable Data Push", &s_dataPushEnabled);

            ImGui::Separator();

            // Tabbed sections
            if (ImGui::BeginTabBar("##DebugTabs")) {
                if (ImGui::BeginTabItem("Domains")) {
                    ImGui::BeginChild("##DomainScroll", ImVec2(0, 0), false, ImGuiWindowFlags_None);

                    // Helper: category header with dirty count
                    auto CategoryHeader = [](const char* label, const int* dirtyIndices, int count) -> bool {
                        int dirtyCount = 0;
                        for (int i = 0; i < count; ++i)
                            if (s_domainDirty[dirtyIndices[i]]) ++dirtyCount;

                        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.12f, 0.18f, 0.28f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.18f, 0.26f, 0.40f, 1.0f));
                        bool open = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);
                        ImGui::PopStyleColor(2);

                        if (dirtyCount > 0) {
                            ImGui::SameLine();
                            ImGui::TextColored(ImVec4(0.4f, 0.75f, 1.0f, 0.8f), "(%d active)", dirtyCount);
                        }
                        return open;
                    };

                    // Environment
                    {
                        const int idx[] = {0, 1, 2, 3};
                        if (CategoryHeader("Environment", idx, 4)) {
                            ImGui::Indent(4.0f);
                            Sec_Celestial();
                            Sec_Atmosphere();
                            Sec_Fog();
                            Sec_Weather();
                            ImGui::Unindent(4.0f);
                        }
                    }

                    // Player & Character
                    {
                        const int idx[] = {4, 14, 12, 20};
                        if (CategoryHeader("Player & Character", idx, 4)) {
                            ImGui::Indent(4.0f);
                            Sec_Player();
                            Sec_Equipment();
                            Sec_ActorValues();
                            Sec_NPCDetect();
                            ImGui::Unindent(4.0f);
                        }
                    }

                    // Camera & Rendering
                    {
                        const int idx[] = {5, 9, 10, 7, 8};
                        if (CategoryHeader("Camera & Rendering", idx, 5)) {
                            ImGui::Indent(4.0f);
                            Sec_Camera();
                            Sec_Render();
                            Sec_ImageSpace();
                            Sec_Shadow();
                            Sec_Effects();
                            ImGui::Unindent(4.0f);
                        }
                    }

                    // World
                    {
                        const int idx[] = {6, 11, 18, 19};
                        if (CategoryHeader("World", idx, 4)) {
                            ImGui::Indent(4.0f);
                            Sec_Interior();
                            Sec_Lights();
                            Sec_Region();
                            Sec_Audio();
                            ImGui::Unindent(4.0f);
                        }
                    }

                    // Interaction & UI
                    {
                        const int idx[] = {13, 15, 16};
                        if (CategoryHeader("Interaction & UI", idx, 3)) {
                            ImGui::Indent(4.0f);
                            Sec_Crosshair();
                            Sec_Quest();
                            Sec_UIState();
                            ImGui::Unindent(4.0f);
                        }
                    }

                    // Diagnostics
                    {
                        const int idx[] = {17, 21};
                        if (CategoryHeader("Diagnostics", idx, 2)) {
                            ImGui::Indent(4.0f);
                            Sec_Feedback();
                            Sec_Perf();
                            ImGui::Unindent(4.0f);
                        }
                    }

                    // Scene Composition (standalone — always visible)
                    Sec_Scene();

                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Game Editor")) {
                    ImGui::BeginChild("##GameEdScroll", ImVec2(0, 0), false, ImGuiWindowFlags_None);
                    RenderGameEditor();
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Subsystems")) {
                    ImGui::BeginChild("##SubScroll", ImVec2(0, 0), false, ImGuiWindowFlags_None);
                    RenderSubsystems();
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Raw Params")) {
                    ImGui::BeginChild("##ParamScroll", ImVec2(0, 0), false, ImGuiWindowFlags_None);
                    RenderParamTable();
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Annotations")) {
                    ImGui::BeginChild("##AnnotScroll", ImVec2(0, 0), false, ImGuiWindowFlags_None);
                    RenderAnnotationBrowser();
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Param Editor")) {
                    ImGui::BeginChild("##ParamEdScroll", ImVec2(0, 0), false, ImGuiWindowFlags_None);
                    RenderParameterEditor();
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Object Window")) {
                    ImGui::BeginChild("##ObjWinScroll", ImVec2(0, 0), false, ImGuiWindowFlags_None);
                    RenderObjectWindow();
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Bindings")) {
                    ImGui::BeginChild("##BindingsScroll", ImVec2(0, 0), false, ImGuiWindowFlags_None);
                    RenderBindingStatus();
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Weather Editor")) {
                    ImGui::BeginChild("##WeatherEdScroll", ImVec2(0, 0), false, ImGuiWindowFlags_None);
                    SB::RenderWeatherEditorTab();
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Weather Analyzer")) {
                    ImGui::BeginChild("##WxAnalScroll", ImVec2(0, 0), false, ImGuiWindowFlags_None);
                    RenderWeatherAnalyzer();
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Shader Viewer")) {
                    RenderShaderSourceViewer();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("EditorID XRef")) {
                    ImGui::BeginChild("##XRefScroll", ImVec2(0, 0), false, ImGuiWindowFlags_None);
                    RenderEditorIDXRef();
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Light Inspector")) {
                    ImGui::BeginChild("##LightInspScroll", ImVec2(0, 0), false, ImGuiWindowFlags_None);
                    RenderLightInspector();
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Debug Viz")) {
                    ImGui::BeginChild("##DebugVizScroll", ImVec2(0, 0), false, ImGuiWindowFlags_None);

                    // ── DebugRenderer controls ──────────────────────────
                    auto& dr = SB::DebugRenderer::Get();
                    {
                        bool drEnabled = dr.IsEnabled();
                        if (ImGui::Checkbox("Debug Renderer", &drEnabled))
                            dr.SetEnabled(drEnabled);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Master toggle for all 3D debug overlays");

                        if (dr.IsInitialized()) {
                            ImGui::SameLine(200);
                            ImGui::TextColored(ImVec4(0.5f, 0.55f, 0.65f, 1.0f),
                                "Verts: %u  Lines: %u  Labels: %u  Draws: %u",
                                dr.GetVertexCount(), dr.GetLineCount(),
                                dr.GetLabelCount(), dr.GetDrawCallCount());

                            bool depthTest = true; // no getter — just show the toggle
                            if (ImGui::Checkbox("Depth Test", &depthTest))
                                dr.SetDepthTestEnabled(depthTest);
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("When enabled, debug lines are occluded by geometry");
                        } else {
                            ImGui::SameLine();
                            ImGui::TextColored(ImVec4(0.85f, 0.3f, 0.3f, 1.0f), "(not initialized)");
                        }
                    }

                    ImGui::Separator();

                    // ── NavMesh Visualizer ───────────────────────────────
                    {
                        auto& nav = SB::NavMeshVisualizer::Get();
                        bool navEnabled = nav.IsEnabled();
                        if (ImGui::Checkbox("NavMesh Overlay", &navEnabled))
                            nav.SetEnabled(navEnabled);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Renders navmesh wireframe from loaded cells");

                        if (navEnabled) {
                            ImGui::Indent();
                            ImGui::Text("Visible: %u tris / %u meshes",
                                nav.GetVisibleTriangles(), nav.GetVisibleNavMeshes());

                            static float drawDist = 3000.0f;
                            if (ImGui::SliderFloat("Draw Distance", &drawDist, 500.0f, 10000.0f, "%.0f"))
                                nav.SetDrawDistance(drawDist);

                            static bool showTriFlags = true;
                            if (ImGui::Checkbox("Color by Flag", &showTriFlags))
                                nav.SetShowTriangleFlags(showTriFlags);
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("Green=walkable, Yellow=preferred, Orange=no-large, Red=deleted");

                            static bool showCover = false;
                            if (ImGui::Checkbox("Cover Edges", &showCover))
                                nav.SetShowCover(showCover);

                            static bool showPortals = false;
                            if (ImGui::Checkbox("Door Portals", &showPortals))
                                nav.SetShowPortals(showPortals);

                            static bool showEdgeLinks = false;
                            if (ImGui::Checkbox("Edge Links", &showEdgeLinks))
                                nav.SetShowEdgeLinks(showEdgeLinks);
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("Cross-navmesh connections (cyan markers)");

                            ImGui::Unindent();
                        }
                    }

                    ImGui::Separator();

                    // ── Skeleton Visualizer ──────────────────────────────
                    {
                        auto& skel = SB::SkeletonVisualizer::Get();
                        bool skelEnabled = skel.IsEnabled();
                        if (ImGui::Checkbox("Skeleton Overlay", &skelEnabled))
                            skel.SetEnabled(skelEnabled);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Renders NiNode bone hierarchy as diamond-shaped joints");

                        if (skelEnabled) {
                            ImGui::Indent();

                            ImGui::Text("Bones: %u  Actors: %u", skel.GetBoneCount(), skel.GetActorCount());

                            int targetMode = static_cast<int>(skel.GetTarget());
                            if (ImGui::RadioButton("Player", targetMode == 0)) {
                                skel.SetTarget(SB::SkeletonTarget::Player);
                            }
                            ImGui::SameLine();
                            if (ImGui::RadioButton("Crosshair Ref", targetMode == 1)) {
                                skel.SetTarget(SB::SkeletonTarget::CrosshairRef);
                            }
                            ImGui::SameLine();
                            if (ImGui::RadioButton("All Nearby", targetMode == 2)) {
                                skel.SetTarget(SB::SkeletonTarget::AllNearby);
                            }

                            ImGui::Unindent();
                        }
                    }

                    ImGui::Separator();

                    // ── Feature Manager status ──────────────────────────
                    {
                        auto& fm = SB::FeatureManager::Get();
                        ImGui::Text("Features: %u ready / %u failed / %zu total",
                            fm.GetReadyCount(), fm.GetFailedCount(), fm.GetFeatures().size());

                        if (ImGui::TreeNode("Feature List")) {
                            for (auto& f : fm.GetFeatures()) {
                                ImVec4 col;
                                switch (f.state) {
                                case SB::FeatureState::Ready:    col = ImVec4(0.3f, 0.85f, 0.5f, 1.0f); break;
                                case SB::FeatureState::Failed:   col = ImVec4(0.9f, 0.3f, 0.3f, 1.0f); break;
                                case SB::FeatureState::Disabled: col = ImVec4(0.5f, 0.5f, 0.5f, 0.7f); break;
                                default:                         col = ImVec4(0.7f, 0.7f, 0.3f, 1.0f); break;
                                }
                                ImGui::TextColored(col, "[%s] %s (%.2f ms)",
                                    fm.GetStateName(f.state),
                                    f.name ? f.name : "?",
                                    f.lastUpdateMs);
                            }
                            ImGui::TreePop();
                        }
                    }

                    // ── Material Pipeline (3-target G-buffer) ────────────
                    if (D3D11Hook::IsProxyActive()) {
                        ImGui::Separator();
                        ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f), "Material Pipeline");
                        bool matOn = D3D11Hook::IsMaterialPipelineActive();
                        if (ImGui::Checkbox("Enable Material G-Buffer", &matOn))
                            D3D11Hook::SetMaterialPipelineEnabled(matOn);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Patches BSLightingShader pixel shaders to output\n"
                                              "3-target G-buffer: albedo (o1), normals (o2),\n"
                                              "material properties (o3).\n"
                                              "Classifies materials: Skin, Hair, Eye, EnvMap,\n"
                                              "Terrain, Parallax, TreeCanopy, Snow.");
                        uint32_t patched    = D3D11Hook::GetMaterialPatchedCount();
                        uint32_t candidates = D3D11Hook::GetMaterialCandidateCount();
                        uint32_t classified = D3D11Hook::GetMaterialClassifiedCount();
                        ImGui::Text("  Candidates: %u  Patched: %u  Classified: %u",
                                    candidates, patched, classified);
                    }

                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Diagnostics")) {
                    ImGui::BeginChild("##DiagScroll", ImVec2(0, 0), false, ImGuiWindowFlags_None);

                    // ── System Health Overview ──────────────────────────
                    {
                        auto& sh = SB::SystemHealth::Get();
                        uint32_t total = sh.GetSystemCount();
                        uint32_t green = sh.GetGreenCount();
                        uint32_t yellow = sh.GetYellowCount();
                        uint32_t red = sh.GetRedCount();

                        ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f), "System Health");
                        ImGui::SameLine(200);
                        ImGui::TextColored(ImVec4(0.3f, 0.85f, 0.4f, 1.0f), "%u OK", green);
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.2f, 1.0f), "%u Warn", yellow);
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.95f, 0.25f, 0.2f, 1.0f), "%u Fail", red);
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.5f, 0.55f, 0.65f, 1.0f), "(%u total)", total);

                        // Summary bar
                        if (total > 0) {
                            float greenPct = static_cast<float>(green) / total;
                            float yellowPct = static_cast<float>(yellow) / total;
                            float redPct = static_cast<float>(red) / total;
                            ImVec2 barStart = ImGui::GetCursorScreenPos();
                            float barW = ImGui::GetContentRegionAvail().x;
                            float barH = 6.0f;
                            auto* dl = ImGui::GetWindowDrawList();
                            dl->AddRectFilled(barStart, ImVec2(barStart.x + barW * greenPct, barStart.y + barH),
                                IM_COL32(76, 217, 100, 255));
                            dl->AddRectFilled(ImVec2(barStart.x + barW * greenPct, barStart.y),
                                ImVec2(barStart.x + barW * (greenPct + yellowPct), barStart.y + barH),
                                IM_COL32(242, 204, 51, 255));
                            dl->AddRectFilled(ImVec2(barStart.x + barW * (greenPct + yellowPct), barStart.y),
                                ImVec2(barStart.x + barW, barStart.y + barH),
                                IM_COL32(242, 64, 51, 255));
                            ImGui::Dummy(ImVec2(barW, barH + 4));
                        }

                        // Per-category collapsing sections
                        for (int cat = 0; cat < static_cast<int>(SB::SystemCategory::Count); ++cat) {
                            int catCount = 0;
                            int catRed = 0, catYellow = 0;
                            for (uint32_t i = 0; i < total; ++i) {
                                auto& sys = sh.GetSystem(i);
                                if (static_cast<int>(sys.category) == cat) {
                                    ++catCount;
                                    if (sys.status == SB::HealthStatus::Red) ++catRed;
                                    else if (sys.status == SB::HealthStatus::Yellow) ++catYellow;
                                }
                            }
                            if (catCount == 0) continue;

                            // Category header with status pill
                            ImVec4 catCol = (catRed > 0)
                                ? ImVec4(0.95f, 0.25f, 0.2f, 1.0f)
                                : (catYellow > 0)
                                    ? ImVec4(0.95f, 0.8f, 0.2f, 1.0f)
                                    : ImVec4(0.3f, 0.85f, 0.4f, 1.0f);
                            char catLabel[64];
                            snprintf(catLabel, sizeof(catLabel), "%s (%d)##shcat%d",
                                SB::GetCategoryName(static_cast<SB::SystemCategory>(cat)), catCount, cat);

                            ImGui::PushStyleColor(ImGuiCol_Text, catCol);
                            bool open = ImGui::TreeNode(catLabel);
                            ImGui::PopStyleColor();

                            if (open) {
                                for (uint32_t i = 0; i < total; ++i) {
                                    auto& sys = sh.GetSystem(i);
                                    if (static_cast<int>(sys.category) != cat) continue;

                                    float cr, cg, cb;
                                    SB::GetStatusColor(sys.status, cr, cg, cb);
                                    ImGui::TextColored(ImVec4(cr, cg, cb, 1.0f), "[%c]",
                                        sys.status == SB::HealthStatus::Green ? 'G' :
                                        sys.status == SB::HealthStatus::Yellow ? 'W' :
                                        sys.status == SB::HealthStatus::Red ? 'X' : '?');
                                    ImGui::SameLine();
                                    ImGui::Text("%-24s", sys.name ? sys.name : "?");
                                    ImGui::SameLine(280);
                                    if (sys.errorCount > 0)
                                        ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.3f, 1.0f),
                                            "err:%u", sys.errorCount);
                                    else
                                        ImGui::TextColored(ImVec4(0.4f, 0.5f, 0.4f, 0.6f), "err:0");
                                    ImGui::SameLine(350);
                                    if (sys.message[0])
                                        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 0.8f),
                                            "%s", sys.message);
                                }
                                ImGui::TreePop();
                            }
                        }
                    }

                    ImGui::Separator();

                    // ── Proxy Diagnostics ─────────────────────────────────
                    {
                        auto& pd = SB::ProxyDiagnostics::Get();
                        auto& snap = pd.GetSnapshot();

                        ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f), "Proxy Diagnostics");
                        ImGui::SameLine(200);
                        if (snap.connected)
                            ImGui::TextColored(ImVec4(0.3f, 0.85f, 0.4f, 1.0f), "Connected");
                        else
                            ImGui::TextColored(ImVec4(0.95f, 0.25f, 0.2f, 1.0f), "Not Connected");

                        if (snap.connected) {
                            // Frame stats
                            if (ImGui::TreeNodeEx("Frame Stats##proxy", ImGuiTreeNodeFlags_DefaultOpen)) {
                                ImGui::Text("Draw Calls: %u  RT Switches: %u  Shader Changes: %u",
                                    snap.drawCalls, snap.rtSwitches, snap.shaderChanges);
                                ImGui::Text("Frame: %u  Phase: %s (%u)",
                                    snap.frameCount, snap.phaseName, snap.phaseId);
                                ImGui::TreePop();
                            }

                            // CB Dirty Tracking
                            if (ImGui::TreeNode("CB Dirty Tracking##proxy")) {
                                ImGui::Text("Maps: %u  Skipped: %u  Committed: %u  Tracked: %u",
                                    snap.cbMaps, snap.cbSkipped, snap.cbCommitted, snap.cbTracked);
                                ImGui::ProgressBar(snap.cbSaveRate / 100.0f, ImVec2(-1, 14));
                                ImGui::SameLine(0, 4);
                                ImGui::Text("%.1f%% saved", snap.cbSaveRate);
                                ImGui::TreePop();
                            }

                            // State Cache
                            if (ImGui::TreeNode("State Cache Redundancy##proxy")) {
                                auto StateLine = [](const char* label, uint32_t red, uint32_t tot) {
                                    float pct = tot > 0 ? 100.0f * red / tot : 0.0f;
                                    ImGui::Text("  %-8s: %u / %u (%.1f%% redundant)", label, red, tot, pct);
                                };
                                StateLine("SRV",   snap.srvRedundant, snap.srvTotal);
                                StateLine("Blend", snap.blendRedundant, snap.blendTotal);
                                StateLine("DS",    snap.dsRedundant, snap.dsTotal);
                                StateLine("RS",    snap.rsRedundant, snap.rsTotal);
                                ImGui::Text("  Total: %.1f%% calls eliminated", snap.totalSaveRate);
                                ImGui::TreePop();
                            }

                            // Occlusion Culling
                            if (ImGui::TreeNode("Occlusion Culling##proxy")) {
                                ImGui::Text("Tested: %u  Culled: %u (%.1f%%)",
                                    snap.occTested, snap.occCulled, snap.occCullRate);
                                ImGui::TreePop();
                            }

                            // HDR
                            if (ImGui::TreeNode("HDR##proxy")) {
                                ImGui::Text("Capable: %s  Enabled: %s",
                                    snap.hdrCapable ? "Yes" : "No",
                                    snap.hdrEnabled ? "Yes" : "No");
                                ImGui::Text("Max Nits: %.0f  Paper White: %.0f  Format: %u",
                                    snap.hdrMaxNits, snap.hdrPaperWhite, snap.backbufferFmt);
                                ImGui::TreePop();
                            }

                            // Material Pipeline
                            if (ImGui::TreeNode("Material Pipeline##proxy")) {
                                ImGui::Text("Active: %s  Deferred: %s",
                                    snap.matActive ? "Yes" : "No",
                                    snap.deferredActive ? "Yes" : "No");
                                ImGui::Text("Candidates: %u  Patched: %u  Classified: %u",
                                    snap.matCandidates, snap.matPatched, snap.matClassified);
                                ImGui::TreePop();
                            }
                        }
                    }

                    ImGui::Separator();

                    // ── Compatibility Probe ───────────────────────────────
                    {
                        auto& cp = SB::CompatibilityProbe::Get();
                        auto& conflicts = cp.GetConflicts();
                        auto& modules = cp.GetModules();

                        ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f), "Compatibility Probe");
                        ImGui::SameLine(200);
                        if (cp.GetErrorCount() > 0)
                            ImGui::TextColored(ImVec4(0.95f, 0.25f, 0.2f, 1.0f),
                                "%u errors", cp.GetErrorCount());
                        else if (cp.GetWarningCount() > 0)
                            ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.2f, 1.0f),
                                "%u warnings", cp.GetWarningCount());
                        else
                            ImGui::TextColored(ImVec4(0.3f, 0.85f, 0.4f, 1.0f), "Clean");

                        static bool showReprobeBtn = true;
                        if (showReprobeBtn && ImGui::SmallButton("Re-probe")) {
                            cp.RunProbe();
                        }

                        // Conflict reports
                        if (!conflicts.empty() && ImGui::TreeNodeEx("Reports##compat", ImGuiTreeNodeFlags_DefaultOpen)) {
                            for (const auto& c : conflicts) {
                                ImVec4 col;
                                const char* prefix;
                                switch (c.severity) {
                                case SB::ConflictSeverity::Error:
                                    col = ImVec4(0.95f, 0.25f, 0.2f, 1.0f);
                                    prefix = "[ERR]";
                                    break;
                                case SB::ConflictSeverity::Warning:
                                    col = ImVec4(0.95f, 0.8f, 0.2f, 1.0f);
                                    prefix = "[WRN]";
                                    break;
                                default:
                                    col = ImVec4(0.5f, 0.7f, 0.9f, 1.0f);
                                    prefix = "[INF]";
                                    break;
                                }
                                ImGui::TextColored(col, "%s %s", prefix, c.detail);
                            }
                            ImGui::TreePop();
                        }

                        // Loaded modules
                        if (!modules.empty() && ImGui::TreeNode("Loaded Modules##compat")) {
                            ImGui::Text("%zu modules loaded", modules.size());
                            uint32_t knownCount = 0;
                            for (const auto& m : modules)
                                if (m.isKnown) ++knownCount;
                            ImGui::SameLine();
                            ImGui::TextColored(ImVec4(0.5f, 0.6f, 0.7f, 0.8f),
                                "(%u recognized)", knownCount);

                            // Show known modules first
                            for (const auto& m : modules) {
                                if (!m.isKnown) continue;
                                ImGui::TextColored(ImVec4(0.7f, 0.85f, 0.6f, 1.0f),
                                    "%-28s %5u KB  %s", m.name, m.sizeKB, m.description);
                            }
                            if (ImGui::TreeNode("All Modules##all")) {
                                for (const auto& m : modules) {
                                    if (m.isKnown) continue;
                                    ImGui::TextColored(ImVec4(0.5f, 0.55f, 0.6f, 0.7f),
                                        "%-28s %5u KB", m.name, m.sizeKB);
                                }
                                ImGui::TreePop();
                            }
                            ImGui::TreePop();
                        }
                    }

                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                // ── Renderers tab ──
                if (ImGui::BeginTabItem("Renderers")) {
                    ImGui::BeginChild("##RenderersScroll", ImVec2(0, 0), false, ImGuiWindowFlags_None);
                    RenderRenderers();
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                // ── Profiler tab ──
                if (ImGui::BeginTabItem("Profiler")) {
                    ImGui::BeginChild("##ProfilerScroll", ImVec2(0, 0), false, ImGuiWindowFlags_None);
                    RenderProfiler();
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }
        }
        ImGui::End();

        // Apply game state overrides each frame (engine recomputes, must re-apply)
        ApplyGameEdits();

        // Store for next-frame dirty comparison
        s_prevData = s_data;
        s_hasPrevData = true;
    }

    void SetData(const AllData& a_data)
    {
        s_data = a_data;
    }

    bool IsDataPushEnabled()
    {
        return s_dataPushEnabled;
    }

    bool IsTrackerEnabled(const char* /* a_name */)
    {
        return true;  // All trackers always enabled in v3
    }

    void SetDataPushEnabled(bool a_enabled)
    {
        s_dataPushEnabled = a_enabled;
    }

    void SetTrackerHealth(const TrackerHealthInfo* a_info, int a_count)
    {
        s_trackerHealthCount = (std::min)(a_count, kMaxTrackers);
        for (int i = 0; i < s_trackerHealthCount; ++i)
            s_trackerHealthInfo[i] = a_info[i];
    }

    void SetPreProcessorStats(const PreProcessorStats& a_stats)
    {
        s_ppStats = a_stats;
    }
}
