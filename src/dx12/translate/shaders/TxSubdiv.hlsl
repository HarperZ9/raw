// TxSubdiv.hlsl: Format-Preserving Live GPU Compute Vlachos Curved PN-Triangle 1->4 Subdivider (SM 5.0).
// Operates directly on arbitrary D3D11/Skyrim vertex strides via ByteAddressBuffer so Bethesda's own
// compiled vertex/pixel shaders render 4x higher-poly curved silhouettes with zero layout mismatch,
// zero CPU readback, and exact preservation of packed bitangent components (pos.w, normal.w, tangent.w),
// UVs, and vertex-color blend weights. Original work, MIT.

cbuffer SubdivCB : register(b0) {
    uint  triCount;
    uint  strideBytes;
    uint  vbByteOffset;
    uint  ibByteOffset;
    int   baseVertex;
    uint  idxIs32Bit;
    uint  posOffFmt;     // low16 = byteOffset, high16 = 1:R32G32B32F, 2:R32G32B32A32F, 3:R16G16B16A16F
    uint  normOffFmt;    // low16 = byteOffset, high16 = 0:none, 1:R32G32B32F, 2:R8G8B8A8_UNORM, 3:R8G8B8A8_SNORM, 4:R16G16B16A16F
    uint  tangOffFmt;    // same encoding as normOffFmt
    uint  uvOffFmt;      // low16 = byteOffset, high16 = 0:none, 1:R32G32F, 2:R16G16F
    uint  colOffFmt;     // low16 = byteOffset, high16 = 0:none, 1:R8G8B8A8_UNORM
    float curvatureGain; // Vlachos Bezier projection weight (default 0.62)
    uint  maxVertIdx;    // inclusive max valid vertex index in InVerts
    float maxOffsetFrac; // safety clamp on PN curvature offset as fraction of edge length (0.18)
    float snowDepthU;    // live 3D volumetric snowpack buildup thickness (0..18 world units)
    float displaceAmp;   // subtle crack-free symmetric midpoint micro-relief amplitude (0..0.35)
    // Wave 35: Live 3D physical ocean/water tessellation & gravity-coupled Airy/JONSWAP wave displacement:
    uint  waterMode;     // 0=static PN 1->4, 1=water 1->4, 2=water 1->16, (+4 = VB-only per-frame update)
    float waterTimeSec;  // live unified engine simulation clock (seconds)
    float waterGravity;  // planetary gravity g(h) in m/s^2 (default 9.80665)
    float waterTideU;    // live Nirn dual-moon tidal elevation offset (world units)
    float waterBeaufort; // live Beaufort sea state (0..12)
    float waterSwellX;   // unit primary swell direction X
    float waterSwellY;   // unit primary swell direction Y
    float waterPad;
};

ByteAddressBuffer   InVerts  : register(t0);
ByteAddressBuffer   InIdx    : register(t1);
RWByteAddressBuffer OutVerts : register(u0);
RWByteAddressBuffer OutIdx   : register(u1);

uint loadIndex(uint i) {
    if (idxIs32Bit != 0u) {
        return InIdx.Load(ibByteOffset + i * 4u);
    }
    uint b = ibByteOffset + i * 2u;
    uint w = InIdx.Load(b & ~3u);
    return ((b & 2u) != 0u) ? (w >> 16u) : (w & 0xFFFFu);
}

uint clampVert(uint rawIdx) {
    int v = int(rawIdx) + baseVertex;
    return uint(clamp(v, 0, int(maxVertIdx)));
}

float4 unpackUnorm8x4(uint w) {
    return float4(float(w & 0xFFu), float((w >> 8u) & 0xFFu), float((w >> 16u) & 0xFFu), float((w >> 24u) & 0xFFu)) * (1.0f / 255.0f);
}

uint packUnorm8x4(float4 v) {
    uint4 q = uint4(saturate(v) * 255.0f + 0.5f);
    return (q.x & 0xFFu) | ((q.y & 0xFFu) << 8u) | ((q.z & 0xFFu) << 16u) | ((q.w & 0xFFu) << 24u);
}

float4 unpackSnorm8x4(uint w) {
    int4 s = int4(int(w << 24u) >> 24, int(w << 16u) >> 24, int(w << 8u) >> 24, int(w) >> 24);
    return max(float4(s) * (1.0f / 127.0f), -1.0f.xxxx);
}

uint packSnorm8x4(float4 v) {
    int4 q = int4(round(clamp(v, -1.0f.xxxx, 1.0f.xxxx) * 127.0f));
    return (uint(q.x) & 0xFFu) | ((uint(q.y) & 0xFFu) << 8u) | ((uint(q.z) & 0xFFu) << 16u) | ((uint(q.w) & 0xFFu) << 24u);
}

float4 loadAttr4(uint vByte, uint offFmt, bool isSignedNorm) {
    uint off = offFmt & 0xFFFFu;
    uint fmt = offFmt >> 16u;
    if (fmt == 1u) {
        return float4(asfloat(InVerts.Load3(vByte + off)), 1.0f);
    }
    if (fmt == 2u) {
        if (isSignedNorm) {
            float4 u = unpackUnorm8x4(InVerts.Load(vByte + off));
            return float4(u.xyz * 2.0f - 1.0f, u.w);
        }
        return asfloat(InVerts.Load4(vByte + off));
    }
    if (fmt == 3u) {
        if (isSignedNorm) {
            return unpackSnorm8x4(InVerts.Load(vByte + off));
        }
        uint2 h = InVerts.Load2(vByte + off);
        return float4(f16tof32(h.x & 0xFFFFu), f16tof32(h.x >> 16u), f16tof32(h.y & 0xFFFFu), f16tof32(h.y >> 16u));
    }
    if (fmt == 4u) {
        uint2 h = InVerts.Load2(vByte + off);
        return float4(f16tof32(h.x & 0xFFFFu), f16tof32(h.x >> 16u), f16tof32(h.y & 0xFFFFu), f16tof32(h.y >> 16u));
    }
    return float4(0.0f, 0.0f, 1.0f, 1.0f);
}

void storeAttr4(uint outByte, uint offFmt, float4 val, bool isSignedNorm) {
    uint off = offFmt & 0xFFFFu;
    uint fmt = offFmt >> 16u;
    if (fmt == 1u) {
        OutVerts.Store3(outByte + off, asuint(val.xyz));
    } else if (fmt == 2u) {
        if (isSignedNorm) {
            OutVerts.Store(outByte + off, packUnorm8x4(float4(val.xyz * 0.5f + 0.5f, val.w)));
        } else {
            OutVerts.Store4(outByte + off, asuint(val));
        }
    } else if (fmt == 3u) {
        if (isSignedNorm) {
            OutVerts.Store(outByte + off, packSnorm8x4(val));
        } else {
            uint wx = (f32tof16(val.x) & 0xFFFFu) | ((f32tof16(val.y) & 0xFFFFu) << 16u);
            uint wy = (f32tof16(val.z) & 0xFFFFu) | ((f32tof16(val.w) & 0xFFFFu) << 16u);
            OutVerts.Store2(outByte + off, uint2(wx, wy));
        }
    } else if (fmt == 4u) {
        uint wx = (f32tof16(val.x) & 0xFFFFu) | ((f32tof16(val.y) & 0xFFFFu) << 16u);
        uint wy = (f32tof16(val.z) & 0xFFFFu) | ((f32tof16(val.w) & 0xFFFFu) << 16u);
        OutVerts.Store2(outByte + off, uint2(wx, wy));
    }
}

float2 loadUv(uint vByte, uint offFmt) {
    uint off = offFmt & 0xFFFFu;
    uint fmt = offFmt >> 16u;
    if (fmt == 1u) return asfloat(InVerts.Load2(vByte + off));
    if (fmt == 2u) {
        uint w = InVerts.Load(vByte + off);
        return float2(f16tof32(w & 0xFFFFu), f16tof32(w >> 16u));
    }
    return float2(0.0f, 0.0f);
}

void storeUv(uint outByte, uint offFmt, float2 uv) {
    uint off = offFmt & 0xFFFFu;
    uint fmt = offFmt >> 16u;
    if (fmt == 1u) {
        OutVerts.Store2(outByte + off, asuint(uv));
    } else if (fmt == 2u) {
        uint w = (f32tof16(uv.x) & 0xFFFFu) | ((f32tof16(uv.y) & 0xFFFFu) << 16u);
        OutVerts.Store(outByte + off, w);
    }
}

float3 safeNorm(float3 v) {
    float l2 = dot(v, v);
    return l2 > 1.0e-10f ? v * rsqrt(l2) : float3(0.0f, 0.0f, 1.0f);
}

// Deterministic symmetric position hash (evaluates identically on shared edges -> zero seam cracks):
float subHash31(float3 p) {
    float3 q = frac(p * 0.1031f);
    q += dot(q, q.yzx + 33.33f);
    return frac((q.x + q.y) * q.z);
}

// Wave 35: Crack-free organic micro-relief + true 3D volumetric snow-blanket pillow displacement
// (alexandre196 / karynatrychyk / GhislainGir). Evaluated strictly from quantized position & normal so shared
// triangle edges remain 100% watertight, and clamped safely (<= 4.5u) so doorways & Havok collision stay clean!
float3 applyMicroAndSnow(float3 p, float3 n, bool isMidpoint) {
    float3 outP = p;
    if (isMidpoint && displaceAmp > 0.001f) {
        float h = (subHash31(floor(p * 4.0f) * 0.25f) - 0.5f) * 2.0f;
        outP += n * (h * min(displaceAmp, 0.20f));
    }
    if (snowDepthU > 0.05f && n.z > 0.35f) {
        float upMask = smoothstep(0.35f, 0.82f, n.z);
        float drift = 0.72f + 0.42f * subHash31(floor(p * 0.5f) * 2.0f);
        float lift = min(snowDepthU, 4.5f) * upMask * drift * (isMidpoint ? 1.08f : 1.0f);
        outP += safeNorm(float3(n.xy * 0.28f, 1.0f)) * lift;
    }
    return outP;
}

// Symmetric crease-preserving Vlachos PN-Triangle cubic Bezier control point (bit-exact across shared edges):
float3 pnCtrl(float3 p, float3 n, float3 q, float creaseW) {
    float3 d = q - p;
    float edgeLen = length(d);
    float proj = dot(d, n);
    float maxProj = edgeLen * maxOffsetFrac * 3.0f;
    proj = clamp(proj, -maxProj, maxProj);
    return (2.0f * p + q - (proj * creaseW) * n) * (1.0f / 3.0f);
}

void copyRawVert(uint srcByte, uint dstByte) {
    uint words = strideBytes >> 2u;
    [loop] for (uint w = 0u; w < words; ++w) {
        OutVerts.Store(dstByte + w * 4u, InVerts.Load(srcByte + w * 4u));
    }
}

// Wave 35: Deterministic, seam-free 5-mode gravity-coupled Airy/JONSWAP trochoidal 3D ocean surface
// evaluated strictly from vertex XY so adjacent water cells stay 100% watertight while rolling in real 3D.
void evalPhysicalWaterVertex(float3 pIn, out float3 pOut, out float3 nOut) {
    float gPhys = clamp(waterGravity, 1.0f, 30.0f);
    float2 sDir = float2(waterSwellX, waterSwellY);
    float sLen = length(sDir);
    sDir = (sLen > 1.0e-4f) ? (sDir / sLen) : float2(0.866f, 0.5f);
    float2 sPerp = float2(-sDir.y, sDir.x);

    float2 xm = pIn.xy * 0.0142857f; // Skyrim units -> meters
    float ampScale = clamp(0.18f + waterBeaufort * 0.14f, 0.12f, 2.20f);
    float t = waterTimeSec;

    static const float kWaves[5] = { 0.224f, 0.370f, 0.571f, 1.083f, 1.963f };
    static const float aWaves[5] = { 0.145f, 0.092f, 0.058f, 0.032f, 0.018f };
    static const float cAng[5]   = { 1.000f, 0.899f, 0.829f, 0.530f, 0.438f };
    static const float sAng[5]   = { 0.000f, 0.438f, -0.559f, 0.848f, -0.899f };

    float hMeters = 0.0f;
    float2 xyMeters = float2(0.0f, 0.0f);
    float2 dh = float2(0.0f, 0.0f);
    [unroll] for (int i = 0; i < 5; ++i) {
        float2 d = sDir * cAng[i] + sPerp * sAng[i];
        float k = kWaves[i];
        float a = aWaves[i] * ampScale;
        float omega = sqrt(gPhys * k);
        float phase = dot(d, xm) * k - omega * t + float(i) * 1.37f;
        float sp, cp;
        sincos(phase, sp, cp);
        hMeters  += a * sp;
        xyMeters -= d * (a * 0.26f * cp);
        dh       += d * (a * k * cp);
    }

    float zLiftU = clamp(hMeters * 70.0f, -22.0f, 22.0f) + clamp(waterTideU, -24.0f, 24.0f);
    float2 xyLiftU = clamp(xyMeters * 70.0f, -12.0f.xx, 12.0f.xx);
    pOut = float3(pIn.xy + xyLiftU, pIn.z + zLiftU);
    nOut = safeNorm(float3(-dh.x, -dh.y, 1.0f));
}

void writeWaterGridVertex(uint dstByte, uint b0, float4 p0, float4 p1, float4 p2,
                          float2 uv0, float2 uv1, float2 uv2, float4 c0, float4 c1, float4 c2,
                          float w0, float w1, float w2) {
    copyRawVert(b0, dstByte);
    float3 pInterp = w0 * p0.xyz + w1 * p1.xyz + w2 * p2.xyz;
    float pW = w0 * p0.w + w1 * p1.w + w2 * p2.w;
    float3 pWave, nWave;
    evalPhysicalWaterVertex(pInterp, pWave, nWave);
    storeAttr4(dstByte, posOffFmt, float4(pWave, pW), false);
    if ((normOffFmt >> 16u) != 0u) {
        storeAttr4(dstByte, normOffFmt, float4(nWave, 1.0f), true);
    }
    if ((uvOffFmt >> 16u) != 0u) {
        storeUv(dstByte, uvOffFmt, w0 * uv0 + w1 * uv1 + w2 * uv2);
    }
    if ((colOffFmt >> 16u) == 1u) {
        uint off = colOffFmt & 0xFFFFu;
        OutVerts.Store(dstByte + off, packUnorm8x4(w0 * c0 + w1 * c1 + w2 * c2));
    }
}

uint waterRowOff4(uint r) {
    return r * 5u - ((r * (r - 1u)) >> 1u);
}

// 1 thread per input triangle:
//   waterMode == 0: 1->4 Vlachos curved PN-Triangle + snowpack (6v, 12i)
//   waterMode == 1/5: live 3D water wave 1->4 (6v, 12i; mode>=4 skips OutIdx)
//   waterMode == 2/6: live 3D water wave 1->16 coarse-quad tessellation (15v, 48i; mode>=4 skips OutIdx)
[numthreads(64, 1, 1)]
void CSMain(uint3 tid3 : SV_DispatchThreadID) {
    uint tid = tid3.x;
    if (tid >= triCount) return;

    uint i0 = clampVert(loadIndex(tid * 3u + 0u));
    uint i1 = clampVert(loadIndex(tid * 3u + 1u));
    uint i2 = clampVert(loadIndex(tid * 3u + 2u));

    uint b0 = vbByteOffset + i0 * strideBytes;
    uint b1 = vbByteOffset + i1 * strideBytes;
    uint b2 = vbByteOffset + i2 * strideBytes;

    float4 p0 = loadAttr4(b0, posOffFmt, false);
    float4 p1 = loadAttr4(b1, posOffFmt, false);
    float4 p2 = loadAttr4(b2, posOffFmt, false);

    uint wKind = waterMode & 3u;
    if (wKind != 0u) {
        float2 uv0 = ((uvOffFmt >> 16u) != 0u) ? loadUv(b0, uvOffFmt) : float2(0.0f, 0.0f);
        float2 uv1 = ((uvOffFmt >> 16u) != 0u) ? loadUv(b1, uvOffFmt) : float2(0.0f, 0.0f);
        float2 uv2 = ((uvOffFmt >> 16u) != 0u) ? loadUv(b2, uvOffFmt) : float2(0.0f, 0.0f);
        float4 c0 = float4(1.0f, 1.0f, 1.0f, 1.0f);
        float4 c1 = c0, c2 = c0;
        if ((colOffFmt >> 16u) == 1u) {
            uint off = colOffFmt & 0xFFFFu;
            c0 = unpackUnorm8x4(InVerts.Load(b0 + off));
            c1 = unpackUnorm8x4(InVerts.Load(b1 + off));
            c2 = unpackUnorm8x4(InVerts.Load(b2 + off));
        }
        bool writeIdx = (waterMode < 4u);
        if (wKind == 2u) {
            // 1->16 barycentric subdivision (N=4: 15 vertices, 16 micro-triangles = 48 R32_UINT indices)
            uint vBase = tid * 15u;
            uint vCur = 0u;
            [unroll] for (uint r = 0u; r <= 4u; ++r) {
                float w2 = float(r) * 0.25f;
                for (uint c = 0u; c <= (4u - r); ++c) {
                    float w1 = float(c) * 0.25f;
                    float w0 = 1.0f - w1 - w2;
                    writeWaterGridVertex((vBase + vCur) * strideBytes, b0, p0, p1, p2, uv0, uv1, uv2, c0, c1, c2, w0, w1, w2);
                    ++vCur;
                }
            }
            if (writeIdx) {
                uint iByte = tid * 192u; // 48 * 4 bytes
                uint outK = 0u;
                [unroll] for (uint r = 0u; r < 4u; ++r) {
                    uint r0 = vBase + waterRowOff4(r);
                    uint r1 = vBase + waterRowOff4(r + 1u);
                    for (uint c = 0u; c < (4u - r); ++c) {
                        OutIdx.Store3(iByte + outK * 12u, uint3(r0 + c, r0 + c + 1u, r1 + c));
                        ++outK;
                        if (c + 1u < (4u - r)) {
                            OutIdx.Store3(iByte + outK * 12u, uint3(r0 + c + 1u, r1 + c + 1u, r1 + c));
                            ++outK;
                        }
                    }
                }
            }
        } else {
            // 1->4 barycentric water wave subdivision (6 vertices, 4 micro-triangles = 12 R32_UINT indices)
            uint vBase = tid * 6u;
            writeWaterGridVertex((vBase + 0u) * strideBytes, b0, p0, p1, p2, uv0, uv1, uv2, c0, c1, c2, 1.0f, 0.0f, 0.0f);
            writeWaterGridVertex((vBase + 1u) * strideBytes, b1, p0, p1, p2, uv0, uv1, uv2, c0, c1, c2, 0.0f, 1.0f, 0.0f);
            writeWaterGridVertex((vBase + 2u) * strideBytes, b2, p0, p1, p2, uv0, uv1, uv2, c0, c1, c2, 0.0f, 0.0f, 1.0f);
            writeWaterGridVertex((vBase + 3u) * strideBytes, b0, p0, p1, p2, uv0, uv1, uv2, c0, c1, c2, 0.5f, 0.5f, 0.0f);
            writeWaterGridVertex((vBase + 4u) * strideBytes, b1, p0, p1, p2, uv0, uv1, uv2, c0, c1, c2, 0.0f, 0.5f, 0.5f);
            writeWaterGridVertex((vBase + 5u) * strideBytes, b2, p0, p1, p2, uv0, uv1, uv2, c0, c1, c2, 0.5f, 0.0f, 0.5f);
            if (writeIdx) {
                uint iBase = tid * 48u;
                OutIdx.Store4(iBase +  0u, uint4(vBase + 0u, vBase + 3u, vBase + 5u, vBase + 3u));
                OutIdx.Store4(iBase + 16u, uint4(vBase + 1u, vBase + 4u, vBase + 5u, vBase + 4u));
                OutIdx.Store4(iBase + 32u, uint4(vBase + 2u, vBase + 3u, vBase + 4u, vBase + 5u));
            }
        }
        return;
    }

    float4 n0Raw = loadAttr4(b0, normOffFmt, true);
    float4 n1Raw = loadAttr4(b1, normOffFmt, true);
    float4 n2Raw = loadAttr4(b2, normOffFmt, true);
    float3 n0 = safeNorm(n0Raw.xyz);
    float3 n1 = safeNorm(n1Raw.xyz);
    float3 n2 = safeNorm(n2Raw.xyz);

    // Crease preservation: smooth curved silhouettes round out while sharp 90-deg walls stay crisp & crack-free.
    float c01 = smoothstep(0.28f, 0.78f, dot(n0, n1)) * curvatureGain;
    float c12 = smoothstep(0.28f, 0.78f, dot(n1, n2)) * curvatureGain;
    float c20 = smoothstep(0.28f, 0.78f, dot(n2, n0)) * curvatureGain;

    float3 b210 = pnCtrl(p0.xyz, n0, p1.xyz, c01);
    float3 b120 = pnCtrl(p1.xyz, n1, p0.xyz, c01);
    float3 b021 = pnCtrl(p1.xyz, n1, p2.xyz, c12);
    float3 b012 = pnCtrl(p2.xyz, n2, p1.xyz, c12);
    float3 b102 = pnCtrl(p2.xyz, n2, p0.xyz, c20);
    float3 b201 = pnCtrl(p0.xyz, n0, p2.xyz, c20);

    // Symmetric edge midpoints (depend ONLY on the 2 endpoints + their normals -> 100% watertight across shared edges!):
    float3 m01 = 0.125f * (p0.xyz + p1.xyz) + 0.375f * (b210 + b120);
    float3 m12 = 0.125f * (p1.xyz + p2.xyz) + 0.375f * (b021 + b012);
    float3 m20 = 0.125f * (p2.xyz + p0.xyz) + 0.375f * (b102 + b201);

    uint vBase = tid * 6u;
    uint dst0 = (vBase + 0u) * strideBytes;
    uint dst1 = (vBase + 1u) * strideBytes;
    uint dst2 = (vBase + 2u) * strideBytes;
    uint dst3 = (vBase + 3u) * strideBytes; // midpoint 0-1
    uint dst4 = (vBase + 4u) * strideBytes; // midpoint 1-2
    uint dst5 = (vBase + 5u) * strideBytes; // midpoint 2-0

    // Corners 0, 1, 2: exact byte-identical copies of original vertices (plus symmetric snowpack lift when snowing).
    copyRawVert(b0, dst0);
    copyRawVert(b1, dst1);
    copyRawVert(b2, dst2);
    if (snowDepthU > 0.05f) {
        storeAttr4(dst0, posOffFmt, float4(applyMicroAndSnow(p0.xyz, n0, false), p0.w), false);
        storeAttr4(dst1, posOffFmt, float4(applyMicroAndSnow(p1.xyz, n1, false), p1.w), false);
        storeAttr4(dst2, posOffFmt, float4(applyMicroAndSnow(p2.xyz, n2, false), p2.w), false);
    }

    // Midpoints 3 (0-1), 4 (1-2), 5 (2-0): copy base bytes from first corner then write curved/interpolated attributes.
    copyRawVert(b0, dst3);
    copyRawVert(b1, dst4);
    copyRawVert(b2, dst5);

    float3 nm01 = safeNorm(n0 + n1);
    float3 nm12 = safeNorm(n1 + n2);
    float3 nm20 = safeNorm(n2 + n0);
    storeAttr4(dst3, posOffFmt, float4(applyMicroAndSnow(m01, nm01, true), 0.5f * (p0.w + p1.w)), false);
    storeAttr4(dst4, posOffFmt, float4(applyMicroAndSnow(m12, nm12, true), 0.5f * (p1.w + p2.w)), false);
    storeAttr4(dst5, posOffFmt, float4(applyMicroAndSnow(m20, nm20, true), 0.5f * (p2.w + p0.w)), false);

    if ((normOffFmt >> 16u) != 0u) {
        storeAttr4(dst3, normOffFmt, float4(nm01, 0.5f * (n0Raw.w + n1Raw.w)), true);
        storeAttr4(dst4, normOffFmt, float4(nm12, 0.5f * (n1Raw.w + n2Raw.w)), true);
        storeAttr4(dst5, normOffFmt, float4(nm20, 0.5f * (n2Raw.w + n0Raw.w)), true);
    }
    if ((tangOffFmt >> 16u) != 0u) {
        float4 t0 = loadAttr4(b0, tangOffFmt, true);
        float4 t1 = loadAttr4(b1, tangOffFmt, true);
        float4 t2 = loadAttr4(b2, tangOffFmt, true);
        storeAttr4(dst3, tangOffFmt, float4(safeNorm(t0.xyz + t1.xyz), 0.5f * (t0.w + t1.w)), true);
        storeAttr4(dst4, tangOffFmt, float4(safeNorm(t1.xyz + t2.xyz), 0.5f * (t1.w + t2.w)), true);
        storeAttr4(dst5, tangOffFmt, float4(safeNorm(t2.xyz + t0.xyz), 0.5f * (t2.w + t0.w)), true);
    }
    if ((uvOffFmt >> 16u) != 0u) {
        float2 uv0 = loadUv(b0, uvOffFmt);
        float2 uv1 = loadUv(b1, uvOffFmt);
        float2 uv2 = loadUv(b2, uvOffFmt);
        storeUv(dst3, uvOffFmt, 0.5f * (uv0 + uv1));
        storeUv(dst4, uvOffFmt, 0.5f * (uv1 + uv2));
        storeUv(dst5, uvOffFmt, 0.5f * (uv2 + uv0));
    }
    if ((colOffFmt >> 16u) == 1u) {
        uint off = colOffFmt & 0xFFFFu;
        float4 c0 = unpackUnorm8x4(InVerts.Load(b0 + off));
        float4 c1 = unpackUnorm8x4(InVerts.Load(b1 + off));
        float4 c2 = unpackUnorm8x4(InVerts.Load(b2 + off));
        OutVerts.Store(dst3 + off, packUnorm8x4(0.5f * (c0 + c1)));
        OutVerts.Store(dst4 + off, packUnorm8x4(0.5f * (c1 + c2)));
        OutVerts.Store(dst5 + off, packUnorm8x4(0.5f * (c2 + c0)));
    }

    // Emit 4 subdivided triangles (preserving exact input winding): (0,3,5), (3,1,4), (5,4,2), (3,4,5)
    uint iBase = tid * 48u; // 12 * 4 bytes
    OutIdx.Store4(iBase +  0u, uint4(vBase + 0u, vBase + 3u, vBase + 5u, vBase + 3u));
    OutIdx.Store4(iBase + 16u, uint4(vBase + 1u, vBase + 4u, vBase + 5u, vBase + 4u));
    OutIdx.Store4(iBase + 32u, uint4(vBase + 2u, vBase + 3u, vBase + 4u, vBase + 5u));
}
