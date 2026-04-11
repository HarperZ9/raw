//=============================================================================
//  RenderInspector.cpp — Domain R: Runtime rendering engine inspector
//
//  Single-frame capture of D3D11 pipeline state: draw calls, CBs, shaders,
//  and scene graph. Triggered by F12, writes JSON + bytecode to disk.
//=============================================================================

#include "RenderInspector.h"
#include "DXBCPatcher.h"
#include "MaterialTracker.h"
#include "D3D11Hook.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <unordered_set>
#include <filesystem>

namespace SB
{
    RenderInspector& RenderInspector::Get()
    {
        static RenderInspector instance;
        return instance;
    }

    // ══════════════════════════════════════════════════════════════════════
    //  State Machine
    // ══════════════════════════════════════════════════════════════════════

    void RenderInspector::Arm()
    {
        if (m_state == InspectorState::Idle) {
            m_state = InspectorState::Armed;
            SKSE::log::info("RenderInspector: ARMED — will capture next frame");
        }
    }

    void RenderInspector::BeginFrame()
    {
        ++m_frameNumber;

        if (m_state == InspectorState::Armed) {
            m_state = InspectorState::Capturing;
            m_drawCalls.clear();
            m_drawCalls.reserve(4096);

            // Reset CB tracking
            for (auto& cb : m_boundCBs)
                cb = nullptr;

            m_currentPS = nullptr;
            m_currentShaderType = 0;
            m_currentTechnique = 0;

            SKSE::log::info("RenderInspector: CAPTURING frame {}", m_frameNumber);
        }
    }

    void RenderInspector::EndFrame()
    {
        if (m_state == InspectorState::Capturing) {
            m_state = InspectorState::Writing;
            m_lastCaptureDrawCount = static_cast<uint32_t>(m_drawCalls.size());

            SKSE::log::info("RenderInspector: captured {} draw calls — writing to disk",
                m_drawCalls.size());

            // Capture scene graph (CommonLibSSE, safe to call here)
            CaptureSceneGraph();

            // Write everything to disk
            WriteToDisk();

            // Clear capture data
            m_drawCalls.clear();
            m_drawCalls.shrink_to_fit();
            m_sceneRoot = {};

            m_state = InspectorState::Idle;
            SKSE::log::info("RenderInspector: capture complete — returned to IDLE");
        }
    }

    // ══════════════════════════════════════════════════════════════════════
    //  Hook Callbacks (state-gated)
    // ══════════════════════════════════════════════════════════════════════

    void RenderInspector::OnPSSetShader(ID3D11PixelShader* ps)
    {
        if (m_state != InspectorState::Capturing) return;
        m_currentPS = ps;
    }

    void RenderInspector::OnBeginTechnique(RE::BSShader* shader, uint32_t technique)
    {
        if (m_state != InspectorState::Capturing) return;
        if (shader) {
            m_currentShaderType = static_cast<uint8_t>(shader->shaderType);
        }
        m_currentTechnique = technique;
    }

    void RenderInspector::OnPSSetConstantBuffers(ID3D11DeviceContext* ctx, uint32_t startSlot,
                                                  uint32_t numBuffers, ID3D11Buffer* const* buffers)
    {
        if (m_state != InspectorState::Capturing) return;

        for (uint32_t i = 0; i < numBuffers; ++i) {
            uint32_t slot = startSlot + i;
            if (slot < kMaxCBSlots) {
                m_boundCBs[slot] = buffers[i];
            }
        }
    }

    void RenderInspector::OnDrawIndexed(ID3D11DeviceContext* ctx, uint32_t indexCount,
                                         uint32_t startIndex, int32_t baseVertex)
    {
        if (m_state != InspectorState::Capturing) return;

        DrawCallRecord rec{};
        rec.drawIndex = static_cast<uint32_t>(m_drawCalls.size());
        rec.indexCount = indexCount;
        rec.shaderType = m_currentShaderType;
        rec.technique = m_currentTechnique;
        rec.materialType = static_cast<uint8_t>(detail::g_currentMaterial);
        rec.pixelShader = m_currentPS;
        rec.bytecodeHash = 0;

        // Hash the PS pointer as a simple identifier
        if (m_currentPS) {
            rec.bytecodeHash = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(m_currentPS) & 0xFFFFFFFF);
        }

        // Capture bound constant buffers (only non-null, first 8 slots)
        for (uint32_t slot = 0; slot < 8; ++slot) {
            if (m_boundCBs[slot]) {
                CBBinding cb{};
                cb.slot = slot;
                CaptureCBContents(ctx, m_boundCBs[slot], cb);
                rec.constantBuffers.push_back(std::move(cb));
            }
        }

        m_drawCalls.push_back(std::move(rec));
    }

    // ══════════════════════════════════════════════════════════════════════
    //  CB Content Capture
    // ══════════════════════════════════════════════════════════════════════

    void RenderInspector::CaptureCBContents(ID3D11DeviceContext* ctx, ID3D11Buffer* buffer,
                                             CBBinding& out)
    {
        D3D11_BUFFER_DESC desc{};
        buffer->GetDesc(&desc);
        out.byteWidth = desc.ByteWidth;

        // Create staging buffer for readback
        D3D11_BUFFER_DESC stagingDesc = desc;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;

        ID3D11Device* dev = nullptr;
        ctx->GetDevice(&dev);
        if (!dev) return;

        ID3D11Buffer* staging = nullptr;
        HRESULT hr = dev->CreateBuffer(&stagingDesc, nullptr, &staging);
        if (FAILED(hr) || !staging) {
            dev->Release();
            return;
        }

        ctx->CopyResource(staging, buffer);

        D3D11_MAPPED_SUBRESOURCE mapped{};
        hr = ctx->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
        if (SUCCEEDED(hr)) {
            // Cap at 1024 bytes to avoid huge captures
            uint32_t copySize = (std::min)(desc.ByteWidth, 1024u);
            out.contents.resize(copySize);
            std::memcpy(out.contents.data(), mapped.pData, copySize);
            ctx->Unmap(staging, 0);
        }

        staging->Release();
        dev->Release();
    }

    // ══════════════════════════════════════════════════════════════════════
    //  Scene Graph Capture
    // ══════════════════════════════════════════════════════════════════════

    void RenderInspector::CaptureSceneGraph()
    {
        // Get the player's loaded cell 3D root
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            SKSE::log::warn("RenderInspector: no player for scene graph capture");
            return;
        }

        auto* cell = player->GetParentCell();
        if (!cell) {
            SKSE::log::warn("RenderInspector: no parent cell for scene graph capture");
            return;
        }

        // Get cell 3D node from extra data
        auto* extra3D = cell->extraList.GetByType<RE::ExtraCell3D>();
        if (!extra3D || !extra3D->cellNode) {
            SKSE::log::warn("RenderInspector: no cell 3D node for scene graph capture");
            return;
        }

        std::function<SceneNodeRecord(RE::NiAVObject*, int)> walkNode =
            [&](RE::NiAVObject* obj, int depth) -> SceneNodeRecord
        {
            SceneNodeRecord rec{};
            if (!obj) return rec;

            // Limit depth to prevent stack overflow on huge scenes
            if (depth > 8) {
                rec.name = "(truncated)";
                return rec;
            }

            rec.name = obj->name.c_str() ? obj->name.c_str() : "";
            auto* rtti = obj->GetRTTI();
            rec.typeName = rtti ? rtti->name : "";

            rec.worldTranslate[0] = obj->world.translate.x;
            rec.worldTranslate[1] = obj->world.translate.y;
            rec.worldTranslate[2] = obj->world.translate.z;
            rec.worldScale = obj->world.scale;

            rec.hasMaterial = false;

            // Check for BSGeometry → BSShaderProperty → material
            auto* geom = obj->AsGeometry();
            if (geom) {
                auto& runtimeData = geom->GetGeometryRuntimeData();
                RE::NiProperty* effectProp = runtimeData.properties[RE::BSGeometry::States::kEffect].get();
                // BSShaderProperty inherits from NiShadeProperty → NiProperty
                // Safe downcast: check RTTI before using
                RE::BSShaderProperty* bsProp = nullptr;
                if (effectProp) {
                    bsProp = netimmerse_cast<RE::BSShaderProperty*>(effectProp);
                }
                if (bsProp) {
                    rec.shaderFlags = bsProp->flags.underlying();

                    RE::BSShaderMaterial* mat = bsProp->material;
                    if (mat) {
                        rec.hasMaterial = true;
                        // Check if this is BSLightingShaderMaterialBase
                        RE::BSShaderMaterial::Type matType = mat->GetType();
                        if (matType == RE::BSShaderMaterial::Type::kLighting) {
                            RE::BSLightingShaderMaterialBase* lightingMat =
                                static_cast<RE::BSLightingShaderMaterialBase*>(mat);
                            rec.specularColor[0] = lightingMat->specularColor.red;
                            rec.specularColor[1] = lightingMat->specularColor.green;
                            rec.specularColor[2] = lightingMat->specularColor.blue;
                            rec.specularPower = lightingMat->specularPower;
                            rec.materialAlpha = lightingMat->materialAlpha;

                            RE::BSTextureSet* texSet = lightingMat->textureSet.get();
                            if (texSet) {
                                for (int i = 0; i < 9; ++i) {
                                    const char* path = texSet->GetTexturePath(
                                        static_cast<RE::BSTextureSet::Texture>(i));
                                    rec.texturePaths[i] = path ? path : "";
                                }
                            }
                        }
                    }
                }
            }

            // Recurse into children (limit to 64 children per node to avoid huge captures)
            auto* node = obj->AsNode();
            if (node) {
                int childCount = 0;
                for (auto& child : node->GetChildren()) {
                    if (child.get() && childCount < 64) {
                        rec.children.push_back(walkNode(child.get(), depth + 1));
                        ++childCount;
                    }
                }
            }

            return rec;
        };

        m_sceneRoot = walkNode(extra3D->cellNode.get(), 0);
        SKSE::log::info("RenderInspector: captured scene graph from cell '{}'",
            cell->GetName() ? cell->GetName() : "(unnamed)");
    }

    // ══════════════════════════════════════════════════════════════════════
    //  Disk Output
    // ══════════════════════════════════════════════════════════════════════

    void RenderInspector::WriteToDisk()
    {
        // Create output directory
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        struct tm tm{};
        localtime_s(&tm, &time_t);

        char timestamp[32];
        std::strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &tm);

        auto baseDir = std::filesystem::path("Data/SKSE/Plugins/SkyrimBridge/Captures");
        auto captureDir = baseDir / fmt::format("Frame_{}_{}", m_frameNumber, timestamp);

        std::error_code ec;
        std::filesystem::create_directories(captureDir, ec);
        if (ec) {
            SKSE::log::error("RenderInspector: failed to create capture dir: {}", ec.message());
            return;
        }

        WriteDrawCallsJSON(captureDir);
        WriteSummaryJSON(captureDir);
        WriteShaderBytecode(captureDir);
        WriteSceneGraphJSON(captureDir);

        SKSE::log::info("RenderInspector: wrote capture to {}", captureDir.string());
    }

    // ── Hex dump helper ──────────────────────────────────────────────────

    std::string RenderInspector::CBContentsToHex(const std::vector<uint8_t>& data, uint32_t maxBytes)
    {
        std::ostringstream oss;
        uint32_t count = (std::min)(static_cast<uint32_t>(data.size()), maxBytes);
        for (uint32_t i = 0; i < count; ++i) {
            if (i > 0 && i % 16 == 0) oss << "\n";
            else if (i > 0) oss << " ";
            oss << std::hex << std::setfill('0') << std::setw(2)
                << static_cast<int>(data[i]);
        }
        return oss.str();
    }

    // ── Draw calls JSON ──────────────────────────────────────────────────

    void RenderInspector::WriteDrawCallsJSON(const std::filesystem::path& dir)
    {
        auto path = dir / "draw_calls.json";
        std::ofstream out(path);
        if (!out.is_open()) return;

        out << "[\n";
        for (size_t i = 0; i < m_drawCalls.size(); ++i) {
            auto& dc = m_drawCalls[i];
            out << "  {\n"
                << "    \"drawIndex\": " << dc.drawIndex << ",\n"
                << "    \"indexCount\": " << dc.indexCount << ",\n"
                << "    \"shaderType\": " << static_cast<int>(dc.shaderType) << ",\n"
                << "    \"technique\": " << dc.technique << ",\n"
                << "    \"materialType\": " << static_cast<int>(dc.materialType) << ",\n"
                << "    \"psHash\": \"0x" << std::hex << std::setfill('0') << std::setw(8)
                << dc.bytecodeHash << std::dec << "\",\n"
                << "    \"constantBuffers\": [\n";

            for (size_t j = 0; j < dc.constantBuffers.size(); ++j) {
                auto& cb = dc.constantBuffers[j];
                out << "      {\n"
                    << "        \"slot\": " << cb.slot << ",\n"
                    << "        \"byteWidth\": " << cb.byteWidth << ",\n"
                    << "        \"hexDump\": \"" << CBContentsToHex(cb.contents, 128) << "\"\n"
                    << "      }";
                if (j + 1 < dc.constantBuffers.size()) out << ",";
                out << "\n";
            }

            out << "    ]\n"
                << "  }";
            if (i + 1 < m_drawCalls.size()) out << ",";
            out << "\n";
        }
        out << "]\n";
    }

    // ── Summary JSON ─────────────────────────────────────────────────────

    void RenderInspector::WriteSummaryJSON(const std::filesystem::path& dir)
    {
        auto path = dir / "summary.json";
        std::ofstream out(path);
        if (!out.is_open()) return;

        // Count unique shaders and material types
        std::unordered_set<uint32_t> uniqueShaders;
        uint32_t materialCounts[9] = {};
        uint32_t shaderTypeCounts[16] = {};

        for (auto& dc : m_drawCalls) {
            uniqueShaders.insert(dc.bytecodeHash);
            if (dc.materialType < 9) materialCounts[dc.materialType]++;
            if (dc.shaderType < 16) shaderTypeCounts[dc.shaderType]++;
        }

        static const char* materialNames[] = {
            "General", "Skin", "Hair", "Eye", "MetalGlossy",
            "Terrain", "Vegetation", "Emissive", "Snow"
        };

        out << "{\n"
            << "  \"frameNumber\": " << m_frameNumber << ",\n"
            << "  \"totalDrawCalls\": " << m_drawCalls.size() << ",\n"
            << "  \"uniquePixelShaders\": " << uniqueShaders.size() << ",\n"
            << "  \"materialTypeCounts\": {\n";

        bool first = true;
        for (int i = 0; i < 9; ++i) {
            if (materialCounts[i] > 0) {
                if (!first) out << ",\n";
                out << "    \"" << materialNames[i] << "\": " << materialCounts[i];
                first = false;
            }
        }
        out << "\n  },\n"
            << "  \"shaderTypeCounts\": {\n";

        first = true;
        for (int i = 0; i < 16; ++i) {
            if (shaderTypeCounts[i] > 0) {
                if (!first) out << ",\n";
                out << "    \"type_" << i << "\": " << shaderTypeCounts[i];
                first = false;
            }
        }
        out << "\n  }\n}\n";
    }

    // ── Shader bytecode export ───────────────────────────────────────────

    void RenderInspector::WriteShaderBytecode(const std::filesystem::path& dir)
    {
        auto shadersDir = dir / "shaders";
        std::error_code ec;
        std::filesystem::create_directories(shadersDir, ec);

        auto& patcher = DXBCPatcher::Get();
        std::unordered_set<ID3D11PixelShader*> seen;

        for (auto& dc : m_drawCalls) {
            if (!dc.pixelShader || seen.count(dc.pixelShader)) continue;
            seen.insert(dc.pixelShader);

            // Access bytecode from DXBCPatcher's store via the public accessor
            auto bytecode = patcher.GetBytecode(dc.pixelShader);
            if (bytecode.empty()) continue;

            // Write raw DXBC
            auto dxbcPath = shadersDir / fmt::format("PS_{:08X}.dxbc", dc.bytecodeHash);
            {
                std::ofstream f(dxbcPath, std::ios::binary);
                f.write(reinterpret_cast<const char*>(bytecode.data()), bytecode.size());
            }

            // Disassemble to readable .asm
            ID3DBlob* disasm = nullptr;
            HRESULT hr = D3DDisassemble(bytecode.data(), bytecode.size(), 0, nullptr, &disasm);
            if (SUCCEEDED(hr) && disasm) {
                auto asmPath = shadersDir / fmt::format("PS_{:08X}.asm", dc.bytecodeHash);
                std::ofstream f(asmPath);
                f.write(static_cast<const char*>(disasm->GetBufferPointer()),
                        disasm->GetBufferSize());
                disasm->Release();
            }
        }
    }

    // ── Scene graph JSON ─────────────────────────────────────────────────

    static std::string EscapeJSON(const std::string& s)
    {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            default:   out += c; break;
            }
        }
        return out;
    }

    void RenderInspector::WriteNodeJSON(std::ofstream& out, const SceneNodeRecord& node, int indent)
    {
        std::string pad(indent * 2, ' ');

        out << pad << "{\n"
            << pad << "  \"name\": \"" << EscapeJSON(node.name) << "\",\n"
            << pad << "  \"type\": \"" << EscapeJSON(node.typeName) << "\",\n"
            << pad << "  \"translate\": [" << node.worldTranslate[0] << ", "
            << node.worldTranslate[1] << ", " << node.worldTranslate[2] << "],\n"
            << pad << "  \"scale\": " << node.worldScale;

        if (node.hasMaterial) {
            out << ",\n"
                << pad << "  \"material\": {\n"
                << pad << "    \"specularColor\": [" << node.specularColor[0] << ", "
                << node.specularColor[1] << ", " << node.specularColor[2] << "],\n"
                << pad << "    \"specularPower\": " << node.specularPower << ",\n"
                << pad << "    \"materialAlpha\": " << node.materialAlpha << ",\n"
                << pad << "    \"shaderFlags\": " << node.shaderFlags << ",\n"
                << pad << "    \"textures\": [\n";

            for (int i = 0; i < 9; ++i) {
                out << pad << "      \"" << EscapeJSON(node.texturePaths[i]) << "\"";
                if (i < 8) out << ",";
                out << "\n";
            }
            out << pad << "    ]\n"
                << pad << "  }";
        }

        if (!node.children.empty()) {
            out << ",\n" << pad << "  \"children\": [\n";
            for (size_t i = 0; i < node.children.size(); ++i) {
                WriteNodeJSON(out, node.children[i], indent + 2);
                if (i + 1 < node.children.size()) out << ",";
                out << "\n";
            }
            out << pad << "  ]";
        }

        out << "\n" << pad << "}";
    }

    void RenderInspector::WriteSceneGraphJSON(const std::filesystem::path& dir)
    {
        auto path = dir / "scene_graph.json";
        std::ofstream out(path);
        if (!out.is_open()) return;

        WriteNodeJSON(out, m_sceneRoot, 0);
        out << "\n";
    }

} // namespace SB
