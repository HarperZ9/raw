#include "CameraTracker.h"
#include <RE/Skyrim.h>
#include <cmath>
#include <cstring>

namespace SB::CameraTracker
{
    // Store previous frame's ViewProj for motion vectors
    static Float4x4 s_prevViewProj{};
    static bool     s_hasPrevVP = false;

    // ── 4x4 matrix helpers ──────────────────────────────────────────────
    // BSGraphics::State stores matrices as float[4][4] row-major.
    // We copy them into our Float4x4 layout.
    static void CopyMatrix(Float4x4& a_dst, const float a_src[4][4])
    {
        for (int r = 0; r < 4; ++r) {
            a_dst.row[r].x = a_src[r][0];
            a_dst.row[r].y = a_src[r][1];
            a_dst.row[r].z = a_src[r][2];
            a_dst.row[r].w = a_src[r][3];
        }
    }

    // Multiply two 4x4 matrices (row-major)
    static void MultiplyMatrix(Float4x4& a_out, const Float4x4& a_a, const Float4x4& a_b)
    {
        for (int r = 0; r < 4; ++r) {
            const float* ar = &a_a.row[r].x;
            for (int c = 0; c < 4; ++c) {
                float sum = 0.f;
                for (int k = 0; k < 4; ++k) {
                    const float* bk = &a_b.row[k].x;
                    sum += ar[k] * bk[c];
                }
                (&a_out.row[r].x)[c] = sum;
            }
        }
    }

    // Invert a 4x4 matrix (general, not optimized — runs once per frame)
    static bool InvertMatrix(Float4x4& a_out, const Float4x4& a_in)
    {
        // Flatten to array for Gauss-Jordan
        float m[16], inv[16];
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                m[r*4+c] = (&a_in.row[r].x)[c];

        // Identity
        std::memset(inv, 0, sizeof(inv));
        for (int i = 0; i < 4; ++i) inv[i*5] = 1.f;

        for (int col = 0; col < 4; ++col) {
            // Pivot
            int best = col;
            float bestVal = std::abs(m[col*4+col]);
            for (int row = col+1; row < 4; ++row) {
                float v = std::abs(m[row*4+col]);
                if (v > bestVal) { best = row; bestVal = v; }
            }
            if (bestVal < 1e-12f) return false;

            if (best != col) {
                for (int j = 0; j < 4; ++j) {
                    std::swap(m[col*4+j], m[best*4+j]);
                    std::swap(inv[col*4+j], inv[best*4+j]);
                }
            }

            float pivot = m[col*4+col];
            for (int j = 0; j < 4; ++j) {
                m[col*4+j] /= pivot;
                inv[col*4+j] /= pivot;
            }

            for (int row = 0; row < 4; ++row) {
                if (row == col) continue;
                float factor = m[row*4+col];
                for (int j = 0; j < 4; ++j) {
                    m[row*4+j] -= factor * m[col*4+j];
                    inv[row*4+j] -= factor * inv[col*4+j];
                }
            }
        }

        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                (&a_out.row[r].x)[c] = inv[r*4+c];

        return true;
    }

    // ── Main update ─────────────────────────────────────────────────────
    CameraData Update()
    {
        CameraData data{};

        // ── Camera state from PlayerCamera ──────────────────────────────
        auto* pcam = RE::PlayerCamera::GetSingleton();
        if (pcam) {
            // Note: pitch may not be directly accessible in all versions
            // We extract angles from the camera rotation matrix instead
            data.Angles.y = pcam->yaw;

            // Camera state enum — encode as float for shader branching
            // kFirstPerson=0, kAutoVanity=1, kVATS=2, kFree=3,
            // kIronSights=4, kFurniture=5, kPlayerTween=6, kTween=7,
            // kThirdPerson=8, kMount=9, kBleedout=10, kDragon=11
            data.Angles.z = static_cast<float>(pcam->currentState ?
                pcam->currentState->id : 0);

            data.Info.x = pcam->worldFOV;
        }

        // ── Camera world position from NiCamera ─────────────────────────
        auto* niCam = RE::Main::WorldRootCamera();
        if (niCam) {
            auto& camPos = niCam->world.translate;
            data.WorldPos.x = camPos.x;
            data.WorldPos.y = camPos.y;
            data.WorldPos.z = camPos.z;

            // Extract pitch from camera's rotation matrix
            // The camera's world rotation matrix contains the orientation
            auto& rot = niCam->world.rotate;
            // Pitch is the angle looking up/down
            // In a camera rotation matrix, we can extract pitch from the forward vector
            float forwardZ = rot.entry[2][1];  // Z component of the forward direction
            data.Angles.x = std::asin(std::clamp(forwardZ, -1.0f, 1.0f));
        }

        // Get matrices from the NiCamera runtime data
        if (niCam) {
            const auto& rd = niCam->GetRuntimeData();
            const auto& rd2 = niCam->GetRuntimeData2();

            // worldToCam is a float[4][4] — this IS the view matrix
            CopyMatrix(data.ViewMatrix, rd.worldToCam);

            // Reconstruct projection matrix from NiCamera frustum
            // NiRect members are protected, use GetWidth/GetHeight
            const auto& port = rd2.port;
            float portW = port.GetWidth();
            float portH = port.GetHeight();

            // Get near/far from viewFrustum
            float n = rd2.viewFrustum.fNear;
            float f = rd2.viewFrustum.fFar;

            data.Info.y = n;
            data.Info.z = f;

            // Aspect ratio from port
            data.Info.w = (portH > 0.f) ? portW / portH : 1.f;

            // Standard perspective projection (DX convention)
            // Assuming symmetric viewport (centered at origin)
            std::memset(&data.ProjMatrix, 0, sizeof(Float4x4));
            if (portW > 1e-6f && portH > 1e-6f && std::abs(f - n) > 1e-6f) {
                data.ProjMatrix.row[0].x = 2.f * n / portW;
                data.ProjMatrix.row[1].y = 2.f * n / portH;
                // For symmetric viewport, (r+l)/(r-l) = 0 and (t+b)/(t-b) = 0
                data.ProjMatrix.row[2].x = 0.f;
                data.ProjMatrix.row[2].y = 0.f;
                data.ProjMatrix.row[2].z = f / (f - n);
                data.ProjMatrix.row[2].w = 1.f;
                data.ProjMatrix.row[3].z = -n * f / (f - n);
            }
        }

        // ── Compute ViewProj = View * Proj ──────────────────────────────
        MultiplyMatrix(data.ViewProjMatrix, data.ViewMatrix, data.ProjMatrix);

        // ── Inverse ViewProj (for world position reconstruction) ────────
        if (!InvertMatrix(data.InvViewProj, data.ViewProjMatrix)) {
            std::memset(&data.InvViewProj, 0, sizeof(Float4x4));
        }

        // ── Previous frame ViewProj (for motion vectors) ────────────────
        if (s_hasPrevVP) {
            data.PrevViewProj = s_prevViewProj;
        } else {
            data.PrevViewProj = data.ViewProjMatrix;  // first frame: no motion
        }

        // Store current VP for next frame
        s_prevViewProj = data.ViewProjMatrix;
        s_hasPrevVP = true;

        return data;
    }
}
