// Live bit-level engine heuristic auditor & relationship mapper (RAW engine runtime). Original work, MIT.
#include "rt_audit.h"
#include "rt_version.h"
#include "rt_view.h"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

extern "C" uint64_t RAW_EngineView(raw::rt::EngineView* out);

namespace raw::rt {
namespace {

template <class T>
T At(const uint8_t* p, uint32_t off) {
    T v{};
    std::memcpy(&v, p + off, sizeof v);
    return v;
}

bool ValidPtr(const void* p) {
    const uintptr_t a = reinterpret_cast<uintptr_t>(p);
    return a >= 0x10000ull && a < 0x00007FFFFFFFFFFFull && (a & 0x7ull) == 0;
}

bool Finite3(const float v[3]) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

struct AuditData {
    bool skyOk = false, ssnOk = false, viewOk = false;
    uint64_t skyPtr = 0, curWx = 0, lastWx = 0, precipPtr = 0;
    uint64_t sunObj = 0, cloudsObj = 0, starsObj = 0, masserObj = 0, secundaObj = 0;
    float gameHour = -1.0f, weatherPct = -1.0f;
    float skyColors[17][3] = {};
    int skyColorsValid = 0, skyColorsNonZero = 0;
    float sunNodeDir[3] = {};
    bool sunNodeOk = false;
    uint64_t ssnPtr = 0, ssnSunLight = 0;
    uint32_t activeLights = 0, shadowCasters = 0;
    float ssnSunDir[3] = {}, ssnSunCol[3] = {};
    float ssnSunLen = 0.0f;
    float sunAngleDeltaDeg = -1.0f;
    float camOrthoErr = -1.0f;
    bool decalMgrOk = false, grassMgrOk = false, imgSpaceOk = false, waterSysOk = false;
    uint32_t waterObjCount = 0, decalCount = 0;
    uint32_t passCount = 0, warnCount = 0, failCount = 0;
};

void ProbeLive(const Image& img, AuditData* out) {
    __try {
        const uint32_t getSkyRva = Addr("fn:Sky::GetSingleton");
        const uint32_t shaderStateRva = Addr("BSShaderManager::State::GetSingleton");
        const uint32_t decalMgrRva = Addr("BGSDecalManager::GetSingleton");
        const uint32_t grassMgrRva = Addr("BGSGrassManager::GetSingleton");
        const uint32_t imgSpaceRva = Addr("ImageSpaceManager::GetSingleton");
        const uint32_t waterSysRva = Addr("TESWaterSystem::GetSingleton");
        if (getSkyRva) {
            using GetSkyFn = const uint8_t* (*)();
            const uint8_t* sky = reinterpret_cast<GetSkyFn>(const_cast<uint8_t*>(img.base) + getSkyRva)();
            if (ValidPtr(sky)) {
                out->skyOk = true;
                out->skyPtr = reinterpret_cast<uint64_t>(sky);
                out->curWx = At<uint64_t>(sky, 0x48);
                out->lastWx = At<uint64_t>(sky, 0x50);
                out->precipPtr = At<uint64_t>(sky, 0xA0);
                out->sunObj = At<uint64_t>(sky, 0xB0);
                out->cloudsObj = At<uint64_t>(sky, 0xB8);
                out->starsObj = At<uint64_t>(sky, 0xC0);
                out->masserObj = At<uint64_t>(sky, 0xC8);
                out->secundaObj = At<uint64_t>(sky, 0xD0);
                out->gameHour = At<float>(sky, 0x1B0);
                out->weatherPct = At<float>(sky, 0x1B8);
                // Scan Sky::skyColor[17] starting at +0xD8 (each NiColor is 12 bytes = 3 floats).
                for (int i = 0; i < 17; ++i) {
                    const uint32_t off = 0xD8u + uint32_t(i) * 12u;
                    float rgb[3] = {At<float>(sky, off), At<float>(sky, off + 4), At<float>(sky, off + 8)};
                    std::memcpy(out->skyColors[i], rgb, 12);
                    if (Finite3(rgb) && rgb[0] >= -0.01f && rgb[0] <= 10.0f &&
                        rgb[1] >= -0.01f && rgb[1] <= 10.0f && rgb[2] >= -0.01f && rgb[2] <= 10.0f) {
                        ++out->skyColorsValid;
                        if (rgb[0] + rgb[1] + rgb[2] > 0.005f) ++out->skyColorsNonZero;
                    }
                }
                // Probe Sky::sun (+0xB0) -> sunBaseNode (+0x10) world translation (+0xA0) for true Sun direction.
                const uint8_t* sunP = reinterpret_cast<const uint8_t*>(out->sunObj);
                if (ValidPtr(sunP)) {
                    const uint8_t* baseNode = At<const uint8_t*>(sunP, 0x10);
                    if (ValidPtr(baseNode)) {
                        float pos[3] = {At<float>(baseNode, 0xA0), At<float>(baseNode, 0xA4), At<float>(baseNode, 0xA8)};
                        const float l = std::sqrt(pos[0] * pos[0] + pos[1] * pos[1] + pos[2] * pos[2]);
                        if (Finite3(pos) && l > 1.0f && l < 1.0e8f) {
                            out->sunNodeDir[0] = pos[0] / l;
                            out->sunNodeDir[1] = pos[1] / l;
                            out->sunNodeDir[2] = pos[2] / l;
                            out->sunNodeOk = true;
                        }
                    }
                }
            }
        }
        if (decalMgrRva) {
            const uint8_t* dm = At<const uint8_t*>(img.base, decalMgrRva);
            if (ValidPtr(dm)) {
                out->decalMgrOk = true;
                out->decalCount = At<uint32_t>(dm, 0x08);
            }
        }
        if (grassMgrRva) {
            const uint8_t* gm = At<const uint8_t*>(img.base, grassMgrRva);
            out->grassMgrOk = ValidPtr(gm);
        }
        if (imgSpaceRva) {
            const uint8_t* im = At<const uint8_t*>(img.base, imgSpaceRva);
            out->imgSpaceOk = ValidPtr(im);
        }
        if (waterSysRva) {
            const uint8_t* ws = At<const uint8_t*>(img.base, waterSysRva);
            if (ValidPtr(ws)) {
                out->waterSysOk = true;
                out->waterObjCount = At<uint32_t>(ws + 0x58, 0x10);
            }
        }
        if (shaderStateRva) {
            const uint8_t* st = img.base + shaderStateRva;
            const uint8_t* ssn = At<const uint8_t*>(st, 0x00);
            if (ValidPtr(ssn)) {
                out->ssnOk = true;
                out->ssnPtr = reinterpret_cast<uint64_t>(ssn);
                out->activeLights = At<uint32_t>(ssn + 0x130, 0x10);
                out->shadowCasters = At<uint32_t>(ssn + 0x230, 0x10);
                const uint8_t* bsl = At<const uint8_t*>(ssn, 0x200);
                out->ssnSunLight = reinterpret_cast<uint64_t>(bsl);
                if (ValidPtr(bsl)) {
                    const uint8_t* ni = At<const uint8_t*>(bsl, 0x48);
                    if (ValidPtr(ni)) {
                        const float dx = -At<float>(ni, 0x7C + 0);
                        const float dy = -At<float>(ni, 0x7C + 12);
                        const float dz = -At<float>(ni, 0x7C + 24);
                        out->ssnSunLen = std::sqrt(dx * dx + dy * dy + dz * dz);
                        if (out->ssnSunLen > 1e-5f) {
                            out->ssnSunDir[0] = dx / out->ssnSunLen;
                            out->ssnSunDir[1] = dy / out->ssnSunLen;
                            out->ssnSunDir[2] = dz / out->ssnSunLen;
                        }
                        const float fade = At<float>(ni, 0x134);
                        for (int k = 0; k < 3; ++k) out->ssnSunCol[k] = At<float>(ni, 0x11C + k * 4) * fade;
                    }
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

}  // namespace

std::string AuditCommand(const Image& img) {
    AuditData a{};
    ProbeLive(img, &a);
    EngineView ev{};
    RAW_EngineView(&ev);
    a.viewOk = (ev.ok & 0xFu) != 0;
    if (ev.ok & 2u) {
        // Orthogonality check |R^T R - I| max norm on PlayerCamera::cameraRoot rotation matrix.
        float maxErr = 0.0f;
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                const float dot = ev.camRot[r * 3 + 0] * ev.camRot[c * 3 + 0] +
                                  ev.camRot[r * 3 + 1] * ev.camRot[c * 3 + 1] +
                                  ev.camRot[r * 3 + 2] * ev.camRot[c * 3 + 2];
                const float err = std::fabs(dot - (r == c ? 1.0f : 0.0f));
                if (err > maxErr) maxErr = err;
            }
        }
        a.camOrthoErr = maxErr;
    }
    if (a.sunNodeOk && a.ssnSunLen > 0.5f) {
        const float d = std::clamp(a.sunNodeDir[0] * a.ssnSunDir[0] +
                                   a.sunNodeDir[1] * a.ssnSunDir[1] +
                                   a.sunNodeDir[2] * a.ssnSunDir[2], -1.0f, 1.0f);
        a.sunAngleDeltaDeg = std::acos(d) * 57.2957795f;
    }
    // Heuristic invariants:
    auto check = [&](bool ok, bool warnOnly = false) {
        if (ok) ++a.passCount;
        else if (warnOnly) ++a.warnCount;
        else ++a.failCount;
    };
    check(a.skyOk && ValidPtr(reinterpret_cast<const void*>(a.curWx)));
    check(a.gameHour >= 0.0f && a.gameHour <= 24.0f);
    check(a.skyColorsValid >= 12);
    check(a.ssnOk && a.ssnSunLen > 0.99f && a.ssnSunLen < 1.01f);
    check(a.viewOk && a.camOrthoErr >= 0.0f && a.camOrthoErr < 1.0e-3f);
    check(a.decalMgrOk && a.grassMgrOk && a.imgSpaceOk && a.waterSysOk);
    check(a.sunNodeOk, true);

    char buf[2048];
    std::snprintf(buf, sizeof buf,
        "{\"cmd\":\"audit\",\"version\":\"%s\",\"invariants\":{\"pass\":%u,\"warn\":%u,\"fail\":%u},"
        "\"singletons\":{\"decal_mgr\":%s,\"decal_count\":%u,\"grass_mgr\":%s,\"img_space\":%s,\"water_sys\":%s,\"water_objs\":%u},"
        "\"sky\":{\"ok\":%s,\"game_hour\":%.3f,\"weather_pct\":%.3f,\"cur_wx_valid\":%s,\"sun_ptr_valid\":%s,"
        "\"clouds_ptr_valid\":%s,\"stars_ptr_valid\":%s,\"masser_ptr_valid\":%s,\"secunda_ptr_valid\":%s,"
        "\"colors_valid\":%d,\"colors_nonzero\":%d,"
        "\"c0\":[%.2f,%.2f,%.2f],\"c1\":[%.2f,%.2f,%.2f],\"c7\":[%.2f,%.2f,%.2f],\"c8\":[%.2f,%.2f,%.2f],"
        "\"sun_node_ok\":%s,\"sun_node_dir\":[%.3f,%.3f,%.3f]},"
        "\"ssn\":{\"ok\":%s,\"active_lights\":%u,\"shadow_casters\":%u,\"sun_len\":%.4f,"
        "\"sun_dir\":[%.3f,%.3f,%.3f],\"sun_col\":[%.2f,%.2f,%.2f],\"sun_vs_node_deg\":%.2f},"
        "\"view\":{\"ok_mask\":%u,\"cam_ortho_err\":%.6f,\"water_height\":%.1f,\"interior\":%u,\"cam_state\":%u,\"menu_paused\":%u}}",
        TableVersion().c_str(), a.passCount, a.warnCount, a.failCount,
        a.decalMgrOk ? "true" : "false", a.decalCount, a.grassMgrOk ? "true" : "false",
        a.imgSpaceOk ? "true" : "false", a.waterSysOk ? "true" : "false", a.waterObjCount,
        a.skyOk ? "true" : "false", a.gameHour, a.weatherPct,
        ValidPtr(reinterpret_cast<const void*>(a.curWx)) ? "true" : "false",
        ValidPtr(reinterpret_cast<const void*>(a.sunObj)) ? "true" : "false",
        ValidPtr(reinterpret_cast<const void*>(a.cloudsObj)) ? "true" : "false",
        ValidPtr(reinterpret_cast<const void*>(a.starsObj)) ? "true" : "false",
        ValidPtr(reinterpret_cast<const void*>(a.masserObj)) ? "true" : "false",
        ValidPtr(reinterpret_cast<const void*>(a.secundaObj)) ? "true" : "false",
        a.skyColorsValid, a.skyColorsNonZero,
        a.skyColors[0][0], a.skyColors[0][1], a.skyColors[0][2],
        a.skyColors[1][0], a.skyColors[1][1], a.skyColors[1][2],
        a.skyColors[7][0], a.skyColors[7][1], a.skyColors[7][2],
        a.skyColors[8][0], a.skyColors[8][1], a.skyColors[8][2],
        a.sunNodeOk ? "true" : "false", a.sunNodeDir[0], a.sunNodeDir[1], a.sunNodeDir[2],
        a.ssnOk ? "true" : "false", a.activeLights, a.shadowCasters, a.ssnSunLen,
        a.ssnSunDir[0], a.ssnSunDir[1], a.ssnSunDir[2], a.ssnSunCol[0], a.ssnSunCol[1], a.ssnSunCol[2],
        a.sunAngleDeltaDeg,
        ev.ok, a.camOrthoErr, ev.waterHeight, ev.interior, ev.camStateId, ev.menuPaused);
    return buf;
}

}  // namespace raw::rt
