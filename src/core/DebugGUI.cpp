#include "DebugGUI.h"
#include "D3D11Hook.h"
#include "SceneObserver.h"
#include "ShaderCache.h"
#include "SB_ShaderDebug.h"
#include "DebugRenderer.h"
#include "ConfigManager.h"
#include "ShaderLoader.h"
#include "ShaderReload.h"
// Renderer systems for Renderers tab
#include "GTAORenderer.h"
#include "ContactShadowRenderer.h"
#include "SkylightingRenderer.h"
#include "SSRRenderer.h"
#include "SSGIRenderer.h"
#include "HiZPyramid.h"
#include "LuminanceHistogram.h"
#include "ClusteredLighting.h"
// [DISABLED] #include "SDSMCascades.h"
#include "TAAManager.h"
#include "LUTManager.h"
// [DISABLED] #include "VolumetricClouds.h"
// [DISABLED] #include "FrameGenerator.h"
// [DISABLED] #include "TemporalSuperRes.h"
// [DISABLED] #include "GrassLightingRenderer.h"
// [DISABLED] #include "TreeLODLightingRenderer.h"
// [DISABLED] #include "WaterBlendingRenderer.h"
// [DISABLED] #include "DynamicCubemapRenderer.h"
#include "SceneCompositor.h"
// [DISABLED] #include "AtmosphereRenderer.h"
#include "ToneMapManager.h"
#include "ColorPipeline.h"
#include "WeatherParameterManager.h"
#include "GPUProfiler.h"
#include "FrameCapture.h"
#include "PhaseDispatcher.h"
#include "RenderPipeline.h"
// New compute systems (Tier 5)
// [DISABLED] #include "VolumetricLightingRenderer.h"
// [DISABLED] #include "SubsurfaceScatteringRenderer.h"
// [DISABLED] #include "IndirectSpecularRenderer.h"
// [DISABLED] #include "ScreenSpaceDecalRenderer.h"
// [DISABLED] #include "ParticleLightingRenderer.h"
#include <imgui.h>
#include <imgui_stdlib.h>
#include "NavMeshVisualizer.h"
#include "SkeletonVisualizer.h"
#include <fstream>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <algorithm>
#include <vector>
#include <string>

namespace SB::DebugGUI
{
    // ── Internal state ───────────────────────────────────────────────────
    static AllData s_data{};
    static bool s_initialized = false;
    static bool s_windowOpen = true;
    static bool s_dataPushEnabled = true;

    // Tracker health data (fed from main.cpp)
    static constexpr int kMaxTrackers = 32;
    static TrackerHealthInfo s_trackerHealthInfo[kMaxTrackers] = {};
    static int s_trackerHealthCount = 0;

    // Shader pre-processor stats
    static PreProcessorStats s_ppStats = {};

    // Auto-save: debounced — saves 120 frames after last change
    static bool s_configDirty = false;
    static int  s_dirtyCooldown = 0;

    static void MarkConfigDirty()
    {
        s_configDirty = true;
        s_dirtyCooldown = 120;  // ~2 seconds at 60fps
    }

    static void TickAutoSave()
    {
        if (!s_configDirty) return;
        if (--s_dirtyCooldown <= 0) {
            SB::ConfigManager::Get().Save();
            s_configDirty = false;
        }
    }

    // ── Preset Application ──────────────────────────────────────────────
    static void ApplyPreset(int preset)
    {
        auto& gtao = SB::GTAORenderer::Get();
        auto& cs   = SB::ContactShadowRenderer::Get();
        auto& sky  = SB::SkylightingRenderer::Get();
        auto& ssr  = SB::SSRRenderer::Get();
        auto& ssgi = SB::SSGIRenderer::Get();
        auto& comp = SB::SceneCompositor::Get();

        comp.SetEnabled(true);

        if (preset == 0) {
            // Performance — minimal GPU cost
            if (gtao.IsInitialized()) { gtao.SetEnabled(true); gtao.SetDirections(2); gtao.SetSteps(4); }
            if (cs.IsInitialized())   { cs.SetEnabled(true); }
            if (sky.IsInitialized())  { sky.SetEnabled(false); }
            if (ssr.IsInitialized())  { ssr.SetEnabled(false); }
            if (ssgi.IsInitialized()) { ssgi.SetEnabled(false); }
            comp.SetAOIntensity(0.5f);
            comp.SetShadowIntensity(0.7f);
        }
        else if (preset == 1) {
            // Quality — balanced
            if (gtao.IsInitialized()) { gtao.SetEnabled(true); gtao.SetDirections(4); gtao.SetSteps(8); }
            if (cs.IsInitialized())   { cs.SetEnabled(true); }
            if (sky.IsInitialized())  { sky.SetEnabled(true); }
            if (ssr.IsInitialized())  { ssr.SetEnabled(true); }
            if (ssgi.IsInitialized()) { ssgi.SetEnabled(false); }
            comp.SetAOIntensity(0.6f);
            comp.SetShadowIntensity(0.8f);
            comp.SetSkylightIntensity(0.5f);
            comp.SetSSRIntensity(0.3f);
        }
        else if (preset == 2) {
            // Ultra — everything on
            if (gtao.IsInitialized()) { gtao.SetEnabled(true); gtao.SetDirections(6); gtao.SetSteps(12); }
            if (cs.IsInitialized())   { cs.SetEnabled(true); }
            if (sky.IsInitialized())  { sky.SetEnabled(true); }
            if (ssr.IsInitialized())  { ssr.SetEnabled(true); }
            if (ssgi.IsInitialized()) { ssgi.SetEnabled(true); ssgi.SetGIIntensity(0.25f); }
            comp.SetAOIntensity(0.7f);
            comp.SetShadowIntensity(0.9f);
            comp.SetSkylightIntensity(0.6f);
            comp.SetGIIntensity(0.2f);
            comp.SetSSRIntensity(0.4f);
        }

        MarkConfigDirty();
    }

    // ── Minimal Status Dashboard ────────────────────────────────────────
    static void RenderStatusDashboard()
    {
        auto& sc = SB::ShaderCache::Get();

        ImGui::TextColored(ImVec4(0.65f, 0.70f, 0.78f, 1.0f), "RAW Standalone");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.50f, 0.52f, 0.58f, 1.0f), "|");
        ImGui::SameLine();

        auto StatusPill = [](const char* name, bool active, const char* tooltip) {
            ImVec4 col = active
                ? ImVec4(0.25f, 0.70f, 0.45f, 1.0f)
                : ImVec4(0.35f, 0.37f, 0.42f, 1.0f);
            ImGui::TextColored(col, "%s", name);
            if (ImGui::IsItemHovered() && tooltip)
                ImGui::SetTooltip("%s", tooltip);
            ImGui::SameLine(0, 10);
        };

        StatusPill("Cache", sc.IsEnabled(),
            sc.IsEnabled() ? "Shader bytecode cache (disk + memory)" : "Shader cache disabled");
        ImGui::NewLine();

        // ShaderCache one-liner
        if (sc.IsEnabled()) {
            uint32_t total = sc.GetHitCount() + sc.GetMissCount();
            float hitRate = total > 0 ? (100.0f * sc.GetHitCount() / total) : 0.0f;
            ImGui::TextColored(ImVec4(0.45f, 0.50f, 0.60f, 1.0f),
                "Cache: %u/%u hits (%.0f%%) | %u stored",
                sc.GetHitCount(), total, hitRate, sc.GetStoreCount());
        }
    }

    // ══════════════════════════════════════════════════════════════════════
    //  PROFILER TAB
    // ══════════════════════════════════════════════════════════════════════

    static void RenderProfiler()
    {
        const ImVec4 kGreen  = ImVec4(0.3f, 0.85f, 0.5f, 1.0f);
        const ImVec4 kRed    = ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
        const ImVec4 kGray   = ImVec4(0.5f, 0.5f, 0.5f, 0.7f);
        const ImVec4 kYellow = ImVec4(0.9f, 0.85f, 0.3f, 1.0f);
        const ImVec4 kCyan   = ImVec4(0.3f, 0.85f, 0.9f, 1.0f);

        // ── GPU Profiler ──
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

                ImGui::Text("Total GPU: %.3f ms  |  Budget: %.0f FPS",
                    totalMs, totalMs > 0.001f ? 1000.0f / totalMs : 9999.0f);

                // Sort toggle
                static bool s_sortByCost = true;
                ImGui::SameLine(350);
                ImGui::Checkbox("Sort by cost##profsort", &s_sortByCost);
                ImGui::Separator();

                if (!results.empty()) {
                    // Build sorted indices
                    std::vector<int> indices(results.size());
                    for (int i = 0; i < (int)results.size(); i++) indices[i] = i;
                    if (s_sortByCost) {
                        std::sort(indices.begin(), indices.end(), [&](int a, int b) {
                            return results[a].gpuMs > results[b].gpuMs;
                        });
                    }

                    float maxMs = 0.1f;
                    for (auto& r : results)
                        if (r.valid && r.gpuMs > maxMs) maxMs = r.gpuMs;

                    // Per-pass tracking for min/avg/max
                    struct PassStats { float sum; float minMs; float maxMs; int count; };
                    static std::unordered_map<std::string, PassStats> s_passStats;
                    static int s_statFrames = 0;
                    s_statFrames++;
                    if (s_statFrames % 300 == 0) s_passStats.clear(); // reset every 5s

                    for (int idx : indices) {
                        auto& r = results[idx];
                        if (!r.valid) continue;

                        // Track stats
                        auto& ps = s_passStats[r.name];
                        ps.sum += r.gpuMs;
                        ps.count++;
                        if (ps.count == 1) { ps.minMs = r.gpuMs; ps.maxMs = r.gpuMs; }
                        else {
                            if (r.gpuMs < ps.minMs) ps.minMs = r.gpuMs;
                            if (r.gpuMs > ps.maxMs) ps.maxMs = r.gpuMs;
                        }

                        ImVec4 color = kGreen;
                        if (r.gpuMs > 4.0f) color = kRed;
                        else if (r.gpuMs > 1.0f) color = kYellow;

                        float pct = totalMs > 0.001f ? (r.gpuMs / totalMs * 100.0f) : 0.0f;

                        ImGui::TextColored(color, "  %-22s %6.3f ms  %4.1f%%",
                            r.name.c_str(), r.gpuMs, pct);

                        // Tooltip with min/avg/max
                        if (ImGui::IsItemHovered() && ps.count > 1) {
                            float avg = ps.sum / ps.count;
                            ImGui::SetTooltip("min: %.3f ms\navg: %.3f ms\nmax: %.3f ms\nsamples: %d",
                                ps.minMs, avg, ps.maxMs, ps.count);
                        }

                        ImGui::SameLine(310);
                        float frac = r.gpuMs / maxMs;
                        ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                            ImVec4(color.x * 0.8f, color.y * 0.8f, color.z * 0.8f, 0.8f));
                        ImGui::ProgressBar(frac, ImVec2(100, 14), "");
                        ImGui::PopStyleColor();

                        // Per-pass disable button
                        ImGui::SameLine(420);
                        char disLabel[64];
                        snprintf(disLabel, sizeof(disLabel), "X##dis_%s", r.name.c_str());
                        if (ImGui::SmallButton(disLabel)) {
                            SB::RenderPipeline::Get().SetPassEnabledByName(r.name.c_str(), false);
                        }
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Disable '%s' (re-enable in Renderers tab)", r.name.c_str());
                    }
                } else {
                    ImGui::TextColored(kGray, "  (waiting for results...)");
                }

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

        // ── Frame Capture ──
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

            if (cap.HasData() && !cap.IsCapturing()) {
                auto& frames = cap.GetFrames();
                ImGui::Separator();
                ImGui::TextColored(kCyan, "Last Capture: %u frames", static_cast<uint32_t>(frames.size()));

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

        // ── Pipeline Status ──
        if (ImGui::CollapsingHeader("Pipeline Status", ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto& pipeline = SB::RenderPipeline::Get();
            auto& pd = SB::PhaseDispatcher::Get();

            // ── Critical diagnostic: is the proxy working? ──
            bool proxyOK = D3D11Hook::GetDevice() != nullptr;
            bool depthOK = D3D11Hook::GetGameDepthSRV() != nullptr;
            ImGui::TextColored(proxyOK ? kGreen : kRed,
                "Proxy: %s", proxyOK ? "CONNECTED" : "NOT CONNECTED");
            ImGui::TextColored(depthOK ? kGreen : kRed,
                "Depth: %s", depthOK ? "AVAILABLE" : "NULL (effects will skip)");

            ImGui::TextColored(pipeline.IsInitialized() ? kGreen : kRed,
                "RenderPipeline: %s", pipeline.IsInitialized() ? "OK" : "NOT INIT");

            uint32_t dispatches = pd.GetDispatchCount();
            ImGui::TextColored(pd.IsInitialized() ? (dispatches > 0 ? kGreen : kYellow) : kRed,
                "PhaseDispatcher: %s  dispatches=%u %s",
                pd.IsInitialized() ? "OK" : "NOT INIT", dispatches,
                dispatches == 0 ? "(NO PHASES DETECTED!)" : "");

            bool pdEnabled = pd.IsEnabled();
            if (ImGui::Checkbox("Mid-Frame Dispatch (F7)##pd", &pdEnabled))
                pd.SetEnabled(pdEnabled);
            if (!pdEnabled) ImGui::TextColored(kRed, "  >>> DISPATCH DISABLED! Press F7 to re-enable <<<");

            ImGui::Text("Registered passes: %u", pipeline.GetPassCount());
            ImGui::Text("  PostGeometry:     %u  %s", pipeline.GetPassCount(SB::PipelineStage::PostGeometry),
                pipeline.GetPassCount(SB::PipelineStage::PostGeometry) == 0 ? "(NO EFFECTS!)" : "");
            ImGui::Text("  PostSky:          %u", pipeline.GetPassCount(SB::PipelineStage::PostSky));
            ImGui::Text("  PrePresent:       %u", pipeline.GetPassCount(SB::PipelineStage::PrePresent));
            ImGui::Text("Managed RTs: %u", pipeline.GetRTCount());
            ImGui::Text("Screen: %ux%u", pipeline.GetScreenW(), pipeline.GetScreenH());

            ImGui::Separator();
            ImGui::TextColored(kYellow, "Kill Switches:");
            ImGui::Text("  F7 = Mid-frame dispatch: %s",
                pd.IsEnabled() ? "ENABLED" : "DISABLED");
            ImGui::Text("  F10 = Frame Capture  F11 = GPU Profiler");
        }
    }

    // ══════════════════════════════════════════════════════════════════════
    //  RENDERERS TAB
    // ══════════════════════════════════════════════════════════════════════

    static void RenderRenderers()
    {
        const ImVec4 kGreen  = ImVec4(0.3f, 0.85f, 0.5f, 1.0f);
        const ImVec4 kRed    = ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
        const ImVec4 kGray   = ImVec4(0.5f, 0.5f, 0.5f, 0.7f);
        const ImVec4 kYellow = ImVec4(0.9f, 0.85f, 0.3f, 1.0f);
        const ImVec4 kCyan   = ImVec4(0.3f, 0.85f, 0.9f, 1.0f);

        ImGui::TextColored(ImVec4(0.7f, 0.8f, 1.0f, 1.0f), "Renderer Controls");
        ImGui::Separator();

        // ── Quick Controls ──
        {
            auto& gtao = SB::GTAORenderer::Get();
            auto& cs   = SB::ContactShadowRenderer::Get();
            auto& sky  = SB::SkylightingRenderer::Get();
            auto& ssr  = SB::SSRRenderer::Get();
            auto& ssgi = SB::SSGIRenderer::Get();
            auto& comp = SB::SceneCompositor::Get();

            int activeCount = 0;
            if (gtao.IsEnabled()) activeCount++;
            if (cs.IsEnabled()) activeCount++;
            if (sky.IsEnabled()) activeCount++;
            if (ssr.IsEnabled()) activeCount++;
            if (ssgi.IsEnabled()) activeCount++;

            ImGui::Text("Active: %d/5 effects", activeCount);
            ImGui::SameLine(200);

            if (ImGui::SmallButton("Enable All")) {
                if (gtao.IsInitialized()) gtao.SetEnabled(true);
                if (cs.IsInitialized()) cs.SetEnabled(true);
                if (sky.IsInitialized()) sky.SetEnabled(true);
                if (ssr.IsInitialized()) ssr.SetEnabled(true);
                if (ssgi.IsInitialized()) ssgi.SetEnabled(true);
                comp.SetEnabled(true);
                MarkConfigDirty();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Disable All")) {
                gtao.SetEnabled(false);
                cs.SetEnabled(false);
                sky.SetEnabled(false);
                ssr.SetEnabled(false);
                ssgi.SetEnabled(false);
                SB::ColorPipeline::Get().SetEnabled(false);
                MarkConfigDirty();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Recommended")) {
                // Safe defaults: core effects + AgX color pipeline + Skyrim-matched gamma
                if (gtao.IsInitialized()) gtao.SetEnabled(true);
                if (cs.IsInitialized()) cs.SetEnabled(true);
                if (sky.IsInitialized()) sky.SetEnabled(true);
                comp.SetEnabled(true);
                auto& cp = SB::ColorPipeline::Get();
                if (cp.IsInitialized()) {
                    cp.SetEnabled(true);
                    cp.SetStageMask(SB::CPS_Exposure | SB::CPS_ToneMap | SB::CPS_Contrast
                                  | SB::CPS_Grade | SB::CPS_Dither | SB::CPS_Hunt);
                    cp.SetToneCurve(SB::ColorToneCurve::AgX);
                    cp.SetOutputMode(3);  // Skyrim gamma 1.6 (CS-matched)
                    cp.SetSCurveContrast(1.05f);
                    cp.SetSaturation(1.05f);
                }
                MarkConfigDirty();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Enable GTAO + Contact Shadows + Skylighting\n+ SceneCompositor + ColorPipeline (AgX tonemapping)");

            // Presets
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.75f, 1.0f), "Presets:");
            ImGui::SameLine();
            if (ImGui::SmallButton("Performance")) ApplyPreset(0);
            ImGui::SameLine();
            if (ImGui::SmallButton("Quality")) ApplyPreset(1);
            ImGui::SameLine();
            if (ImGui::SmallButton("Ultra")) ApplyPreset(2);

            // User preset slots
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.75f, 1.0f), "User Presets:");
            auto& cfg = SB::ConfigManager::Get();
            for (int i = 1; i <= 5; i++) {
                char label[32];
                if (cfg.PresetExists(i))
                    snprintf(label, sizeof(label), "Slot %d##load%d", i, i);
                else
                    snprintf(label, sizeof(label), "[empty %d]##load%d", i, i);
                if (ImGui::SmallButton(label)) cfg.LoadPreset(i);
                ImGui::SameLine();
            }
            ImGui::NewLine();
            ImGui::TextColored(ImVec4(0.5f, 0.52f, 0.58f, 1.0f), "Save to:");
            ImGui::SameLine();
            for (int i = 1; i <= 5; i++) {
                char slabel[32];
                snprintf(slabel, sizeof(slabel), "S%d##save%d", i, i);
                if (ImGui::SmallButton(slabel)) cfg.SavePreset(i);
                ImGui::SameLine();
            }
            ImGui::NewLine();

            bool compEn = comp.IsEnabled();
            if (ImGui::Checkbox("Scene Compositor (master blend)##comp", &compEn))
                comp.SetEnabled(compEn);
        }

        ImGui::Spacing();

        // ── Screen-Space Effects ──
        if (ImGui::CollapsingHeader("Screen-Space Effects", ImGuiTreeNodeFlags_DefaultOpen))
        {
            // GTAO
            {
                auto& r = SB::GTAORenderer::Get();
                bool init = r.IsInitialized();
                ImGui::TextColored(init ? kGreen : kRed, init ? "[OK]" : "[--]");
                ImGui::SameLine();
                bool en = r.IsEnabled();
                if (ImGui::Checkbox("GTAO (Ambient Occlusion)##gtao", &en)) {
                    if (init) { r.SetEnabled(en); MarkConfigDirty(); }
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ground Truth AO — darkens crevices and corners.\nJimenez et al. 2019 visibility bitmask.");
                if (init && en) {
                    ImGui::Indent(24.0f);
                    float radius = r.GetRadius();
                    if (ImGui::SliderFloat("AO Radius##gtao", &radius, 0.1f, 5.0f, "%.2f"))
                        { r.SetRadius(radius); MarkConfigDirty(); }
                    float intensity = r.GetIntensity();
                    if (ImGui::SliderFloat("AO Intensity##gtao", &intensity, 0.0f, 3.0f, "%.2f"))
                        { r.SetIntensity(intensity); MarkConfigDirty(); }
                    int dirs = r.GetDirections();
                    if (ImGui::SliderInt("Directions##gtao", &dirs, 2, 8))
                        { r.SetDirections(dirs); MarkConfigDirty(); }
                    int steps = r.GetSteps();
                    if (ImGui::SliderInt("Steps##gtao", &steps, 2, 16))
                        { r.SetSteps(steps); MarkConfigDirty(); }
                    ImGui::Separator();
                    bool bounce = r.IsBounceEnabled();
                    if (ImGui::Checkbox("Bounce GI##gtao", &bounce))
                        r.SetBounceEnabled(bounce);
                    if (bounce) {
                        float bounceInt = r.GetBounceIntensity();
                        if (ImGui::SliderFloat("Bounce Intensity##gtao", &bounceInt, 0.0f, 2.0f, "%.2f"))
                            { r.SetBounceIntensity(bounceInt); MarkConfigDirty(); }
                    }
                    ImGui::TextColored(kGray, "Output: t%u", GTAORenderer::kSRVSlot);
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
                    if (init) { r.SetEnabled(en); MarkConfigDirty(); }
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Screen-space sun-direction ray march.\nAdds fine shadow detail near geometry contacts.");
                if (init && en) {
                    ImGui::Indent(24.0f);
                    float rayLen = r.GetRayLength();
                    if (ImGui::SliderFloat("Ray Length##cs", &rayLen, 0.01f, 0.5f, "%.3f"))
                        { r.SetRayLength(rayLen); MarkConfigDirty(); }
                    float intensity = r.GetIntensity();
                    if (ImGui::SliderFloat("Intensity##cs", &intensity, 0.0f, 3.0f, "%.2f"))
                        { r.SetIntensity(intensity); MarkConfigDirty(); }
                    ImGui::TextColored(kGray, "Output: t%u", ContactShadowRenderer::kSRVSlot);
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
                    if (init) { r.SetEnabled(en); MarkConfigDirty(); }
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Upper-hemisphere sky visibility.\nDarkens areas under overhangs and roofs.");
                if (init && en) {
                    ImGui::Indent(24.0f);
                    float intensity = r.GetIntensity();
                    if (ImGui::SliderFloat("Intensity##sky", &intensity, 0.0f, 3.0f, "%.2f"))
                        { r.SetIntensity(intensity); MarkConfigDirty(); }
                    ImGui::TextColored(kGray, "Output: t%u", SkylightingRenderer::kSRVSlot);
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
                    if (init) { r.SetEnabled(en); MarkConfigDirty(); }
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Screen-space reflections via Hi-Z ray march.\nMcGuire & Mara 2014. Half-res with temporal denoise.");
                if (init && en) {
                    ImGui::Indent(24.0f);
                    float intensity = r.GetIntensity();
                    if (ImGui::SliderFloat("Intensity##ssr", &intensity, 0.0f, 3.0f, "%.2f"))
                        { r.SetIntensity(intensity); MarkConfigDirty(); }
                    ImGui::TextColored(kGray, "Output: t%u", SSRRenderer::kSRVSlot);
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
                    if (init) { r.SetEnabled(en); MarkConfigDirty(); }
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Voxel cone-traced indirect light.\nCrassin et al. 2011. Adds color bounce between surfaces.");
                if (init && en) {
                    ImGui::Indent(24.0f);
                    float intensity = r.GetGIIntensity();
                    if (ImGui::SliderFloat("GI Intensity##ssgi", &intensity, 0.0f, 3.0f, "%.2f"))
                        { r.SetGIIntensity(intensity); MarkConfigDirty(); }
                    ImGui::TextColored(kGray, "Output: t%u", SSGIRenderer::kSRVSlot);
                    ImGui::Unindent(24.0f);
                }
            }
        }

        ImGui::Spacing();

        // ── Infrastructure ──
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
            // [DISABLED] toggleRow("SDSM Cascades##sdsm", SB::SDSMCascades::Get());
            toggleRow("TAA Manager (t22)##taa", SB::TAAManager::Get());
            toggleRow("LUT Manager (t18)##lut", SB::LUTManager::Get());

            // [DISABLED] {
            // [DISABLED]     auto& atmo = SB::AtmosphereRenderer::Get();
            // [DISABLED]     bool init = atmo.IsInitialized();
            // [DISABLED]     ImGui::TextColored(init ? kGreen : kRed, init ? "[OK]" : "[--]");
            // [DISABLED]     ImGui::SameLine();
            // [DISABLED]     ImGui::TextColored(kGray, "Atmosphere LUTs — always active");
            // [DISABLED] }

            {
                auto& tm = SB::ToneMapManager::Get();
                bool init = tm.IsInitialized();
                ImGui::TextColored(init ? kGreen : kRed, init ? "[OK]" : "[--]");
                ImGui::SameLine();
                ImGui::TextColored(kGray, "Tone Mapping — pipeline-controlled");
            }

            ImGui::Spacing();
            ImGui::Separator();

            // Scene Compositor
            {
                auto& sc = SB::SceneCompositor::Get();
                bool init = sc.IsInitialized();
                ImGui::TextColored(init ? kGreen : kRed, init ? "[OK]" : "[--]");
                ImGui::SameLine();
                bool en = sc.IsEnabled();
                if (ImGui::Checkbox("Scene Compositor##comp", &en)) {
                    if (init) sc.SetEnabled(en);
                }
                if (init && en) {
                    ImGui::Indent(24.0f);
                    float aoI = sc.GetAOIntensity();
                    if (ImGui::SliderFloat("AO Strength##comp", &aoI, 0.0f, 1.0f, "%.2f"))
                        { sc.SetAOIntensity(aoI); MarkConfigDirty(); }
                    float shadowI = sc.GetShadowIntensity();
                    if (ImGui::SliderFloat("Contact Shadow##comp", &shadowI, 0.0f, 1.0f, "%.2f"))
                        { sc.SetShadowIntensity(shadowI); MarkConfigDirty(); }
                    float skyI = sc.GetSkylightIntensity();
                    if (ImGui::SliderFloat("Skylighting##comp", &skyI, 0.0f, 1.0f, "%.2f"))
                        { sc.SetSkylightIntensity(skyI); MarkConfigDirty(); }
                    float giI = sc.GetGIIntensity();
                    if (ImGui::SliderFloat("GI Bounce##comp", &giI, 0.0f, 1.0f, "%.2f"))
                        { sc.SetGIIntensity(giI); MarkConfigDirty(); }
                    float ssrI = sc.GetSSRIntensity();
                    if (ImGui::SliderFloat("SSR Blend##comp", &ssrI, 0.0f, 1.0f, "%.2f"))
                        { sc.SetSSRIntensity(ssrI); MarkConfigDirty(); }

                    static const char* kDebugModes[] = {
                        "Off (Composite)", "AO Raw", "GI Raw", "SSR Raw",
                        "Clouds Raw", "Contact Shadow Raw", "Skylighting Raw",
                        "Linear Depth", "Normals", "Split-Screen",
                        "RAW Depth Diagnostic"
                    };
                    int dm = sc.GetDebugMode();
                    if (ImGui::Combo("Debug View##comp", &dm, kDebugModes, 11))
                        sc.SetDebugMode(dm);
                    ImGui::Unindent(24.0f);
                }
            }
        }

        ImGui::Spacing();

        // ── Color Pipeline & Weather ──
        if (ImGui::CollapsingHeader("Color Pipeline & Weather"))
        {
            auto& cp = SB::ColorPipeline::Get();
            bool cpInit = cp.IsInitialized();
            ImGui::TextColored(cpInit ? kGreen : kRed, cpInit ? "[OK]" : "[--]");
            ImGui::SameLine();
            bool cpEn = cp.IsEnabled();
            if (ImGui::Checkbox("Color Pipeline##cp", &cpEn)) {
                if (cpInit) { cp.SetEnabled(cpEn); MarkConfigDirty(); }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Multi-stage color science:\nTonemapping, Film Emulation, 3D LUT, CDL, Weather-reactive");

            if (cpInit && cpEn) {
                ImGui::Indent(24.0f);

                // Tonemapper selection
                static const char* toneNames[] = { "AgX", "ACES", "Reinhard Ext", "Hejl", "Hable/UC2", "Lottes", "Gran Turismo", "None (Linear)" };
                int curve = static_cast<int>(cp.GetToneCurve());
                if (ImGui::Combo("Tonemapper##cp", &curve, toneNames, 8))
                    { cp.SetToneCurve(static_cast<SB::ColorToneCurve>(curve)); MarkConfigDirty(); }

                // Stage toggles
                ImGui::Text("Stages:");
                uint32_t mask = cp.GetStageMask();
                auto stageCheck = [&](const char* label, SB::ColorPipelineStage s) {
                    bool on = (mask & s) != 0;
                    if (ImGui::Checkbox(label, &on)) {
                        if (on) cp.EnableStage(s); else cp.DisableStage(s);
                        MarkConfigDirty();
                    }
                };
                stageCheck("Exposure + White Balance##cp", SB::CPS_Exposure);
                ImGui::SameLine(); stageCheck("Tonemapping##cp", SB::CPS_ToneMap);
                stageCheck("Film Emulation##cp", SB::CPS_Film);
                ImGui::SameLine(); stageCheck("Contrast##cp", SB::CPS_Contrast);
                stageCheck("Color Grading##cp", SB::CPS_Grade);
                ImGui::SameLine(); stageCheck("Extended Grade##cp", SB::CPS_ExtGrade);
                stageCheck("Vignette##cp", SB::CPS_Hunt);
                ImGui::SameLine(); stageCheck("Dither##cp", SB::CPS_Dither);
                stageCheck("Local TM (bloom)##cp", SB::CPS_LocalTM);
                ImGui::SameLine(); stageCheck("AgX Punchy##cp", SB::CPS_AgXPunchy);

                // Key parameters
                ImGui::Separator();
                float ev = cp.GetExposureCompensation();
                if (ImGui::SliderFloat("Exposure Comp (EV)##cp", &ev, -4.0f, 4.0f, "%.2f"))
                    { cp.SetExposureCompensation(ev); MarkConfigDirty(); }
                float sat = cp.GetSaturation();
                if (ImGui::SliderFloat("Saturation##cp", &sat, 0.0f, 2.0f, "%.2f"))
                    { cp.SetSaturation(sat); MarkConfigDirty(); }
                float con = cp.GetSCurveContrast();
                if (ImGui::SliderFloat("Contrast##cp", &con, 0.5f, 2.0f, "%.2f"))
                    { cp.SetSCurveContrast(con); MarkConfigDirty(); }
                float wb = cp.GetWhiteBalanceTemp();
                if (ImGui::SliderFloat("White Balance (K)##cp", &wb, 3000.0f, 12000.0f, "%.0f"))
                    { cp.SetWhiteBalanceTemp(wb); MarkConfigDirty(); }

                // Output gamma mode
                static const char* gammaNames[] = { "sRGB 2.2", "PQ HDR10", "scRGB Linear", "Skyrim 1.6 (CS-style)" };
                int gmode = cp.GetOutputMode();
                if (ImGui::Combo("Output Gamma##cp", &gmode, gammaNames, 4))
                    { cp.SetOutputMode(gmode); MarkConfigDirty(); }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Skyrim 1.6: matches how CS handles gamma.\nSkyrim textures were authored for nonlinear framebuffer.\nsRGB 2.2 may over-darken shadows.");

                // 3D LUT
                auto& lm = SB::LUTManager::Get();
                if (lm.IsInitialized() && lm.GetLUTCount() > 0) {
                    ImGui::Separator();
                    ImGui::Text("3D LUT (%d loaded):", lm.GetLUTCount());
                    int idx = lm.GetActiveIndex();
                    for (int i = 0; i < lm.GetLUTCount(); i++) {
                        if (ImGui::RadioButton(lm.GetLUTName(i).c_str(), idx == i))
                            lm.SetActiveIndex(i);
                    }
                }

                ImGui::Unindent(24.0f);
            }

            // Weather status
            ImGui::Spacing();
            auto& wpm = SB::WeatherParameterManager::Get();
            if (wpm.IsInitialized()) {
                static const char* catNames[] = { "Clear", "Cloudy", "Foggy", "Rain", "ThunderRain", "Snow", "Blizzard", "Ash", "Special" };
                auto cat = static_cast<int>(wpm.GetCurrentCategory());
                auto prev = static_cast<int>(wpm.GetPreviousCategory());
                float t = wpm.GetTransition();
                ImGui::TextColored(kCyan, "Weather: %s (%.0f%%) <- %s",
                    catNames[cat], t * 100.0f, catNames[prev]);
                const auto& wp = wpm.GetCurrent();
                ImGui::TextColored(kGray, "  AO:%.2f  SSR:%.2f  GI:%.2f  Sky:%.2f  Bloom:%.2f",
                    wp.aoIntensity, wp.ssrIntensity, wp.giIntensity, wp.skylightIntensity, wp.bloomIntensity);
                ImGui::TextColored(kGray, "  EV:%+.2f  Sat:%.2f  Con:%.2f  Temp:%+.0fK",
                    wp.exposureBias, wp.saturation, wp.contrast, wp.colorTempOffset);
            }
        }

        ImGui::Spacing();

        // ── Deferred Systems ──
        if (ImGui::CollapsingHeader("Deferred Systems (lazy-init on enable)"))
        {
            ImGui::TextColored(kYellow, "Click enable to initialize + activate.");
            ImGui::Spacing();

            auto* dev = D3D11Hook::GetDevice();
            auto* dctx = D3D11Hook::GetContext();
            auto* dsc  = D3D11Hook::GetSwapChain();

            // [DISABLED] Volumetric Clouds
            // [DISABLED] {
            // [DISABLED]     auto& vc = SB::VolumetricClouds::Get();
            // [DISABLED]     bool init = vc.IsInitialized();
            // [DISABLED]     ImGui::TextColored(init ? kGreen : kGray, init ? "[OK]" : "[--]");
            // [DISABLED]     ImGui::SameLine();
            // [DISABLED]     bool en = init ? vc.IsEnabled() : false;
            // [DISABLED]     if (ImGui::Checkbox("Volumetric Clouds##volcloud", &en)) {
            // [DISABLED]         if (en && !init && dev && dctx && dsc) {
            // [DISABLED]             if (vc.Initialize(dev, dctx, dsc)) vc.SetEnabled(true);
            // [DISABLED]         } else if (init) {
            // [DISABLED]             vc.SetEnabled(en);
            // [DISABLED]         }
            // [DISABLED]     }
            // [DISABLED] }
            // [DISABLED] ImGui::Spacing();

            // [DISABLED] Frame Generator
            // [DISABLED] {
            // [DISABLED]     auto& fg = SB::FrameGenerator::Get();
            // [DISABLED]     bool init = fg.IsInitialized();
            // [DISABLED]     ImGui::TextColored(init ? kGreen : kGray, init ? "[OK]" : "[--]");
            // [DISABLED]     ImGui::SameLine();
            // [DISABLED]     bool en = init ? fg.IsEnabled() : false;
            // [DISABLED]     if (ImGui::Checkbox("Frame Generator##fg", &en)) {
            // [DISABLED]         if (en && !init && dev && dsc) {
            // [DISABLED]             if (fg.Initialize(dev, dsc)) fg.SetEnabled(true);
            // [DISABLED]         } else if (init) {
            // [DISABLED]             fg.SetEnabled(en);
            // [DISABLED]         }
            // [DISABLED]     }
            // [DISABLED] }
            // [DISABLED] ImGui::Spacing();

            // [DISABLED] Temporal Super Resolution
            // [DISABLED] {
            // [DISABLED]     auto& tsr = SB::TemporalSuperRes::Get();
            // [DISABLED]     bool init = tsr.IsInitialized();
            // [DISABLED]     ImGui::TextColored(init ? kGreen : kGray, init ? "[OK]" : "[--]");
            // [DISABLED]     ImGui::SameLine();
            // [DISABLED]     bool en = init ? tsr.IsEnabled() : false;
            // [DISABLED]     if (ImGui::Checkbox("Temporal Super Resolution##tsr", &en)) {
            // [DISABLED]         if (en && !init && dev && dctx && dsc) {
            // [DISABLED]             if (tsr.Initialize(dev, dctx, dsc)) tsr.SetEnabled(true);
            // [DISABLED]         } else if (init) {
            // [DISABLED]             tsr.SetEnabled(en);
            // [DISABLED]         }
            // [DISABLED]     }
            // [DISABLED] }
            // [DISABLED] ImGui::Spacing();

            // Grass/Tree/Water/Cubemap renderers
            auto lazyRow = [&](const char* label, auto& sys) {
                bool init = sys.IsInitialized();
                ImGui::TextColored(init ? kGreen : kGray, init ? "[OK]" : "[--]");
                ImGui::SameLine();
                bool en = init ? sys.IsEnabled() : false;
                if (ImGui::Checkbox(label, &en)) {
                    if (en && !init && dev && dctx && dsc) {
                        if (sys.Initialize(dev, dctx, dsc)) sys.SetEnabled(true);
                    } else if (init) {
                        sys.SetEnabled(en);
                    }
                }
            };
            // [DISABLED] lazyRow("Grass Lighting##grass", SB::GrassLightingRenderer::Get());
            // [DISABLED] lazyRow("Tree LOD Lighting##treelod", SB::TreeLODLightingRenderer::Get());
            // [DISABLED] lazyRow("Water Blending##water", SB::WaterBlendingRenderer::Get());
            // [DISABLED] lazyRow("Dynamic Cubemaps##dcube", SB::DynamicCubemapRenderer::Get());
        }

        ImGui::Spacing();

        // ── New Compute Systems ──
        if (ImGui::CollapsingHeader("New Compute Systems (lazy-init)"))
        {
            auto* ndev = D3D11Hook::GetDevice();
            auto* nctx = D3D11Hook::GetContext();
            auto* nsc  = D3D11Hook::GetSwapChain();

            auto computeRow = [&](const char* label, auto& sys) {
                bool init = sys.IsInitialized();
                ImGui::TextColored(init ? kGreen : ImVec4(0.5f,0.5f,0.5f,0.7f), init ? "[OK]" : "[--]");
                ImGui::SameLine();
                bool en = init ? sys.IsEnabled() : false;
                if (ImGui::Checkbox(label, &en)) {
                    if (en && !init && ndev && nctx && nsc) {
                        if (sys.Initialize(ndev, nctx, nsc)) sys.SetEnabled(true);
                    } else if (init) {
                        sys.SetEnabled(en);
                    }
                }
            };

            // [DISABLED] computeRow("Volumetric Lighting##vollight", SB::VolumetricLightingRenderer::Get());
            // [DISABLED] computeRow("Subsurface Scattering##sss", SB::SubsurfaceScatteringRenderer::Get());
            // [DISABLED] computeRow("Indirect Specular##ispec", SB::IndirectSpecularRenderer::Get());
            // [DISABLED] computeRow("Screen-Space Decals##decal", SB::ScreenSpaceDecalRenderer::Get());
            // [DISABLED] computeRow("Particle Lighting##plight", SB::ParticleLightingRenderer::Get());
        }

        ImGui::Spacing();

        // ── Debug Visualization ──
        if (ImGui::CollapsingHeader("Debug Visualization"))
        {
            const ImVec4 kGreen = ImVec4(0.3f, 0.85f, 0.5f, 1.0f);
            const ImVec4 kGrayV = ImVec4(0.5f, 0.5f, 0.5f, 0.7f);

            auto& dr = SB::DebugRenderer::Get();
            bool drInit = dr.IsInitialized();
            ImGui::TextColored(drInit ? kGreen : kGrayV, drInit ? "[OK]" : "[--]");
            ImGui::SameLine();
            ImGui::Text("Debug Line Renderer (%u lines, %u labels)",
                dr.GetLineCount(), dr.GetLabelCount());

            if (drInit) {
                ImGui::Separator();

                // ── NavMesh Overlay ──
                auto& nav = SB::NavMeshVisualizer::Get();
                bool navEn = nav.IsEnabled();
                if (ImGui::Checkbox("NavMesh Overlay##nav", &navEn))
                    nav.SetEnabled(navEn);

                if (navEn) {
                    ImGui::Indent();
                    ImGui::Text("Visible: %u tris, %u meshes",
                        nav.GetVisibleTriangles(), nav.GetVisibleNavMeshes());

                    static float navDist = 4096.0f;
                    if (ImGui::SliderFloat("Draw Distance##navdist", &navDist, 512.0f, 16384.0f))
                        nav.SetDrawDistance(navDist);

                    static bool navCover = true, navPortals = true, navEdges = true, navFlags = true;
                    if (ImGui::Checkbox("Cover##navcov", &navCover)) nav.SetShowCover(navCover);
                    ImGui::SameLine();
                    if (ImGui::Checkbox("Portals##navport", &navPortals)) nav.SetShowPortals(navPortals);
                    ImGui::SameLine();
                    if (ImGui::Checkbox("Edge Links##navedge", &navEdges)) nav.SetShowEdgeLinks(navEdges);
                    ImGui::SameLine();
                    if (ImGui::Checkbox("Flags##navflags", &navFlags)) nav.SetShowTriangleFlags(navFlags);
                    ImGui::Unindent();
                }

                ImGui::Separator();

                // ── Skeleton Overlay ──
                auto& skel = SB::SkeletonVisualizer::Get();
                bool skelEn = skel.IsEnabled();
                if (ImGui::Checkbox("Skeleton Overlay##skel", &skelEn))
                    skel.SetEnabled(skelEn);

                if (skelEn) {
                    ImGui::Indent();
                    ImGui::Text("Bones: %u, Actors: %u",
                        skel.GetBoneCount(), skel.GetActorCount());

                    int target = static_cast<int>(skel.GetTarget());
                    static const char* kTargets[] = { "Player", "Crosshair Ref", "All Nearby" };
                    if (ImGui::Combo("Target##skeltgt", &target, kTargets, 3))
                        skel.SetTarget(static_cast<SB::SkeletonTarget>(target));

                    static bool skelNames = false;
                    if (ImGui::Checkbox("Bone Names##skelnames", &skelNames))
                        skel.SetShowBoneNames(skelNames);

                    if (target == 2) {
                        static int skelMax = 10;
                        if (ImGui::SliderInt("Max Actors##skelmax", &skelMax, 1, 20))
                            skel.SetMaxActors(skelMax);
                        static float skelRange = 4096.0f;
                        if (ImGui::SliderFloat("Range##skelrange", &skelRange, 128.0f, 16384.0f))
                            skel.SetRange(skelRange);
                    }
                    ImGui::Unindent();
                }
            }
        }
    }

    // ══════════════════════════════════════════════════════════════════════
    //  STYLE
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

        auto* colors = style.Colors;
        colors[ImGuiCol_WindowBg]       = ImVec4(0.08f, 0.09f, 0.12f, 0.96f);
        colors[ImGuiCol_ChildBg]        = ImVec4(0.07f, 0.08f, 0.11f, 0.0f);
        colors[ImGuiCol_Border]         = ImVec4(0.20f, 0.22f, 0.30f, 0.60f);
        colors[ImGuiCol_FrameBg]        = ImVec4(0.12f, 0.13f, 0.18f, 1.0f);
        colors[ImGuiCol_TitleBg]        = ImVec4(0.06f, 0.07f, 0.10f, 1.0f);
        colors[ImGuiCol_TitleBgActive]  = ImVec4(0.10f, 0.14f, 0.22f, 1.0f);
        colors[ImGuiCol_Tab]            = ImVec4(0.12f, 0.14f, 0.20f, 1.0f);
        colors[ImGuiCol_TabHovered]     = ImVec4(0.22f, 0.28f, 0.42f, 1.0f);
        colors[ImGuiCol_TabActive]      = ImVec4(0.18f, 0.24f, 0.38f, 1.0f);
        colors[ImGuiCol_Header]         = ImVec4(0.14f, 0.16f, 0.24f, 1.0f);
        colors[ImGuiCol_HeaderHovered]  = ImVec4(0.20f, 0.24f, 0.36f, 1.0f);
        colors[ImGuiCol_Button]         = ImVec4(0.16f, 0.20f, 0.30f, 1.0f);
        colors[ImGuiCol_ButtonHovered]  = ImVec4(0.22f, 0.28f, 0.42f, 1.0f);
        colors[ImGuiCol_SliderGrab]     = ImVec4(0.30f, 0.45f, 0.70f, 1.0f);
        colors[ImGuiCol_CheckMark]      = ImVec4(0.35f, 0.60f, 0.95f, 1.0f);
    }

    // ══════════════════════════════════════════════════════════════════════
    //  PUBLIC API
    // ══════════════════════════════════════════════════════════════════════

    void Init()
    {
        // Don't call ApplyStyle() here — ImGui context may not exist yet.
        // Style is applied on first Render() call instead.
        s_initialized = true;
    }

    void Shutdown()
    {
        s_initialized = false;
    }

    void Render()
    {
        if (!s_initialized)
            return;

        // Apply style once on first render (ImGui context guaranteed to exist here)
        static bool s_styleApplied = false;
        if (!s_styleApplied) {
            ApplyStyle();
            s_styleApplied = true;
        }

        TickAutoSave();

        ImGui::SetNextWindowSize(ImVec2(540, 720), ImGuiCond_FirstUseEver);

        static float s_fps = 0.0f;
        static float s_fpsTimer = 0.0f;
        static int s_fpsFrames = 0;
        s_fpsFrames++;
        s_fpsTimer += ImGui::GetIO().DeltaTime;
        if (s_fpsTimer >= 0.5f) {
            s_fps = s_fpsFrames / s_fpsTimer;
            s_fpsFrames = 0;
            s_fpsTimer = 0.0f;
        }

        char titleBuf[64];
        snprintf(titleBuf, sizeof(titleBuf), "RAW v1.0 | %.0f FPS###DebugWin", s_fps);

        if (ImGui::Begin(titleBuf, &s_windowOpen, ImGuiWindowFlags_None)) {
            RenderStatusDashboard();
            ImGui::Separator();

            if (ImGui::BeginTabBar("##DebugTabs")) {
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

                // ── Shaders tab (developer tools) ──
                if (ImGui::BeginTabItem("Shaders")) {
                    ImGui::BeginChild("##ShadersScroll", ImVec2(0, 0), false, ImGuiWindowFlags_None);

                    const ImVec4 kGreen  = ImVec4(0.3f, 0.85f, 0.5f, 1.0f);
                    const ImVec4 kRed    = ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
                    const ImVec4 kGray   = ImVec4(0.5f, 0.5f, 0.5f, 0.7f);
                    const ImVec4 kYellow = ImVec4(0.9f, 0.85f, 0.3f, 1.0f);
                    const ImVec4 kCyan   = ImVec4(0.3f, 0.85f, 0.9f, 1.0f);

                    // Stats
                    uint32_t fromDisk, fromEmbed, errCount;
                    SB::ShaderLoader::GetStats(fromDisk, fromEmbed, errCount);
                    ImGui::TextColored(kCyan, "Shader Pipeline");
                    ImGui::Text("  Loaded: %u from disk, %u embedded", fromDisk, fromEmbed);
                    ImGui::Text("  Errors: %u", errCount);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("F12 Reload")) {
                        int n = SB::ReloadAllShaders();
                        char msg[64]; snprintf(msg, sizeof(msg), "RAW: %d shaders recompiled", n);
                        RE::DebugNotification(msg);
                    }
                    ImGui::SameLine();
                    if (errCount > 0 && ImGui::SmallButton("Clear Errors"))
                        SB::ShaderLoader::ClearErrors();
                    ImGui::Separator();

                    // Error list
                    auto& errors = SB::ShaderLoader::GetErrors();
                    if (!errors.empty()) {
                        ImGui::TextColored(kRed, "Compilation Errors:");
                        for (auto& e : errors) {
                            ImGui::PushStyleColor(ImGuiCol_Text, kRed);
                            ImGui::TextWrapped("[%s] %s", e.shaderName.c_str(), e.errorMsg.c_str());
                            ImGui::PopStyleColor();
                        }
                        ImGui::Separator();
                    }

                    // Shader list with source viewer
                    static int s_selectedShader = -1;
                    auto& infos = SB::ShaderLoader::GetShaderInfos();

                    if (ImGui::CollapsingHeader("All Compiled Shaders", ImGuiTreeNodeFlags_DefaultOpen)) {
                        for (int idx = 0; idx < (int)infos.size(); idx++) {
                            auto& si = infos[idx];
                            ImVec4 statusColor = si.compiled ? kGreen : kRed;
                            ImGui::TextColored(statusColor, si.compiled ? "[OK]" : "[!!]");
                            ImGui::SameLine();
                            ImGui::TextColored(si.fromDisk ? kYellow : kGray,
                                si.fromDisk ? "DISK" : " EMB");
                            ImGui::SameLine();

                            char label[128];
                            snprintf(label, sizeof(label), "%-28s %s:%-5s %5.1fms %5uB##sh%d",
                                si.name.c_str(), si.entryPoint.c_str(), si.target.c_str(),
                                si.compileTimeMs, si.bytecodeSize, idx);

                            bool selected = (s_selectedShader == idx);
                            if (ImGui::Selectable(label, selected))
                                s_selectedShader = selected ? -1 : idx;

                            if (!si.errorMsg.empty() && ImGui::IsItemHovered())
                                ImGui::SetTooltip("%s", si.errorMsg.c_str());
                        }
                    }

                    // Source viewer for selected shader
                    if (s_selectedShader >= 0 && s_selectedShader < (int)infos.size()) {
                        ImGui::Separator();
                        auto& sel = infos[s_selectedShader];
                        ImGui::TextColored(kCyan, "Source: %s", sel.name.c_str());
                        ImGui::SameLine();

                        // Open in editor button
                        if (ImGui::SmallButton("Open File##edit")) {
                            auto path = std::filesystem::path("Data/SKSE/Plugins/RAW/Shaders")
                                / (sel.name + ".hlsl");
                            std::error_code ec;
                            if (std::filesystem::exists(path, ec)) {
                                auto absPath = std::filesystem::absolute(path);
                                ShellExecuteA(nullptr, "open", absPath.string().c_str(),
                                    nullptr, nullptr, SW_SHOW);
                            }
                        }
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Open %s.hlsl in default editor", sel.name.c_str());

                        ImGui::SameLine();
                        if (ImGui::SmallButton("Copy Name##cpname")) {
                            ImGui::SetClipboardText(sel.name.c_str());
                        }

                        // In-game shader editor
                        static int         s_editIdx = -1;
                        static std::string s_editBuf;
                        static bool        s_editDirty = false;

                        // Load source into edit buffer on selection change
                        if (s_editIdx != s_selectedShader) {
                            s_editIdx = s_selectedShader;
                            s_editBuf = SB::ShaderLoader::Load(sel.name.c_str(),
                                "// (embedded source not cached for viewing)");
                            s_editDirty = false;
                        }

                        // Save / Revert buttons
                        if (s_editDirty) {
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Save##savesrc")) {
                                auto path = std::filesystem::path("Data/SKSE/Plugins/RAW/Shaders")
                                    / (sel.name + ".hlsl");
                                std::error_code ec;
                                std::filesystem::create_directories(path.parent_path(), ec);
                                std::ofstream out(path, std::ios::out | std::ios::trunc);
                                if (out.is_open()) {
                                    out << s_editBuf;
                                    out.close();
                                    s_editDirty = false;
                                    SB::ShaderLoader::InvalidateAll();
                                    SB::ReloadAllShaders();
                                }
                            }
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Revert##revert")) {
                                s_editBuf = SB::ShaderLoader::Load(sel.name.c_str(),
                                    "// (embedded source not cached for viewing)");
                                s_editDirty = false;
                            }
                        }

                        // Editable text area
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.85f, 0.9f, 1.0f));
                        if (ImGui::InputTextMultiline("##ShaderEdit", &s_editBuf,
                            ImVec2(-1, 300), ImGuiInputTextFlags_AllowTabInput))
                        {
                            s_editDirty = true;
                        }
                        ImGui::PopStyleColor();
                    }

                    // Effect output visualizer controls
                    ImGui::Separator();
                    if (ImGui::CollapsingHeader("Debug Visualization")) {
                        auto& comp = SB::SceneCompositor::Get();
                        if (comp.IsInitialized()) {
                            static const char* kModes[] = {
                                "Normal (Composite)", "AO Only", "GI Only (5x)", "SSR Only",
                                "Clouds Only", "Shadows Only", "Skylighting Only",
                                "Linear Depth (log)", "Reconstructed Normals",
                                "Split-Screen (L=off R=on)", "RAW Depth Diagnostic"
                            };
                            int dm = comp.GetDebugMode();
                            if (ImGui::Combo("View Mode##dbgview", &dm, kModes, 11))
                                comp.SetDebugMode(dm);
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("7=depth, 8=normals, 9=split-screen, 10=RAW depth (green=near, red=far, blue=sky)");

                            // Zero-output warnings
                            auto checkZero = [&](const char* name, bool enabled, ID3D11ShaderResourceView* srv) {
                                if (enabled && !srv) {
                                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f),
                                        "  WARNING: %s enabled but output SRV is null", name);
                                }
                            };
                            auto& gtao = SB::GTAORenderer::Get();
                            auto& cs   = SB::ContactShadowRenderer::Get();
                            auto& sky  = SB::SkylightingRenderer::Get();
                            auto& ssr  = SB::SSRRenderer::Get();
                            auto& ssgi = SB::SSGIRenderer::Get();
                            checkZero("GTAO", gtao.IsEnabled(), gtao.IsInitialized() ? gtao.GetOutputSRV() : nullptr);
                            checkZero("Contact Shadows", cs.IsEnabled(), cs.IsInitialized() ? cs.GetShadowSRV() : nullptr);
                            checkZero("Skylighting", sky.IsEnabled(), sky.IsInitialized() ? sky.GetSkylightSRV() : nullptr);
                            checkZero("SSR", ssr.IsEnabled(), ssr.IsInitialized() ? ssr.GetReflectionSRV() : nullptr);
                            checkZero("SSGI", ssgi.IsEnabled(), ssgi.IsInitialized() ? ssgi.GetGISRV() : nullptr);
                        } else {
                            ImGui::TextColored(kGray, "SceneCompositor not initialized");
                        }
                    }

                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }
        }

        // ── Shader Error Overlay (always visible, even when GUI closed) ──
        if (SB::ShaderLoader::HasErrors()) {
            ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(500, 0), ImGuiCond_Always);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.15f, 0.02f, 0.02f, 0.92f));
            if (ImGui::Begin("##ShaderErrors", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "SHADER ERRORS:");
                auto& errors = SB::ShaderLoader::GetErrors();
                int shown = 0;
                for (auto& e : errors) {
                    if (shown >= 5) {
                        ImGui::Text("  ... and %d more", (int)errors.size() - 5);
                        break;
                    }
                    ImGui::TextWrapped("  [%s] %s", e.shaderName.c_str(), e.errorMsg.c_str());
                    shown++;
                }
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Press F12 to reload shaders");
            }
            ImGui::End();
            ImGui::PopStyleColor();
        }

        ImGui::End();
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
        return true;
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
