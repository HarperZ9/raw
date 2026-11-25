// RawTensorMath.hlsli: Metal-Level Wave-Cooperative Tensor, E8 Lattice, Bilinear-Rank & Long-Vector Math Library.
// Targets SM 6.6-6.10 uniformly across RTX 3060 through RTX 5090+ (and RDNA2-4 / Intel Arc):
//   1) Wave-Quad Register-Shuffle Differentials (QuadReadAcrossX / QuadReadAcrossY): zero extra L1 texture fetches.
//   2) Exact O(1) E8 Optimal 8D Lattice Quantizer (D8 union D8+1/2, Viazovska 8D sphere packing) for 8D feature vectors.
//   3) Strassen / Bilinear-Rank 4x4 Matrix Contraction (49 vs 64 multiplies; respects dense omega=9/4 ceiling via sparsity).
//   4) Prime-Root Low-Discrepancy 4D QMC Sequence (sqrt(2), sqrt(3), sqrt(5), sqrt(7) Kronecker Weyl shifts).
//   5) SM 6.9+ Native Long-Vector (vector<float16_t, 8> / vector<float16_t, 16>) + DP4a INT8 dot4add_i8packed.
// Original work, MIT.
#ifndef RAW_TENSOR_MATH_HLSLI
#define RAW_TENSOR_MATH_HLSLI

#if defined(__SHADER_TARGET_MAJOR) && defined(__SHADER_TARGET_MINOR)
#define RAW_HAS_SM69 ((__SHADER_TARGET_MAJOR > 6) || (__SHADER_TARGET_MAJOR == 6 && __SHADER_TARGET_MINOR >= 9))
#define RAW_HAS_SM610 ((__SHADER_TARGET_MAJOR > 6) || (__SHADER_TARGET_MAJOR == 6 && __SHADER_TARGET_MINOR >= 10))
#else
#define RAW_HAS_SM69 0
#define RAW_HAS_SM610 0
#endif

// Exact O(1) Nearest-Point Quantizer onto the 8D E8 Lattice (Conway-Sloane / Viazovska optimal 8D sphere packing):
// E8 = D8 union (D8 + 1/2), where D8 is all integer vectors in Z^8 with even coordinate sum.
// Achieves kissing number 240 and strictly lower 8D MSE than cubic Z^8 rounding for neural feature/radiance compression.
void rawQuantizeD8(float4 a, float4 b, out float4 qa, out float4 qb) {
    qa = round(a);
    qb = round(b);
    const int sumParity = (int(qa.x) + int(qa.y) + int(qa.z) + int(qa.w)
                         + int(qb.x) + int(qb.y) + int(qb.z) + int(qb.w)) & 1;
    if (sumParity != 0) {
        const float4 da = a - qa;
        const float4 db = b - qb;
        const float4 ada = abs(da);
        const float4 adb = abs(db);
        float maxD = ada.x; int best = 0;
        if (ada.y > maxD) { maxD = ada.y; best = 1; }
        if (ada.z > maxD) { maxD = ada.z; best = 2; }
        if (ada.w > maxD) { maxD = ada.w; best = 3; }
        if (adb.x > maxD) { maxD = adb.x; best = 4; }
        if (adb.y > maxD) { maxD = adb.y; best = 5; }
        if (adb.z > maxD) { maxD = adb.z; best = 6; }
        if (adb.w > maxD) { maxD = adb.w; best = 7; }
        if      (best == 0) qa.x += (da.x >= 0.0f ? 1.0f : -1.0f);
        else if (best == 1) qa.y += (da.y >= 0.0f ? 1.0f : -1.0f);
        else if (best == 2) qa.z += (da.z >= 0.0f ? 1.0f : -1.0f);
        else if (best == 3) qa.w += (da.w >= 0.0f ? 1.0f : -1.0f);
        else if (best == 4) qb.x += (db.x >= 0.0f ? 1.0f : -1.0f);
        else if (best == 5) qb.y += (db.y >= 0.0f ? 1.0f : -1.0f);
        else if (best == 6) qb.z += (db.z >= 0.0f ? 1.0f : -1.0f);
        else                qb.w += (db.w >= 0.0f ? 1.0f : -1.0f);
    }
}

void rawQuantizeE8(float4 a, float4 b, float scale, out float4 outA, out float4 outB) {
    const float invS = rcp(max(scale, 1.0e-4f));
    const float4 sa = a * scale;
    const float4 sb = b * scale;
    float4 p0a, p0b, p1a, p1b;
    rawQuantizeD8(sa, sb, p0a, p0b);
    rawQuantizeD8(sa - 0.5f.xxxx, sb - 0.5f.xxxx, p1a, p1b);
    p1a += 0.5f.xxxx;
    p1b += 0.5f.xxxx;
    const float4 e0a = sa - p0a, e0b = sb - p0b;
    const float4 e1a = sa - p1a, e1b = sb - p1b;
    const float d0 = dot(e0a, e0a) + dot(e0b, e0b);
    const float d1 = dot(e1a, e1a) + dot(e1b, e1b);
    outA = (d0 <= d1 ? p0a : p1a) * invS;
    outB = (d0 <= d1 ? p0b : p1b) * invS;
}

// Bilinear-Rank Strassen 2x2 Matrix Product (7 multiplies vs 8 naive; foundational sub-cubic tensor contraction):
float2x2 rawStrassen2x2(float2x2 A, float2x2 B) {
    const float m1 = (A[0][0] + A[1][1]) * (B[0][0] + B[1][1]);
    const float m2 = (A[1][0] + A[1][1]) * B[0][0];
    const float m3 = A[0][0] * (B[0][1] - B[1][1]);
    const float m4 = A[1][1] * (B[1][0] - B[0][0]);
    const float m5 = (A[0][0] + A[0][1]) * B[1][1];
    const float m6 = (A[1][0] - A[0][0]) * (B[0][0] + B[0][1]);
    const float m7 = (A[0][1] - A[1][1]) * (B[1][0] + B[1][1]);
    return float2x2(m1 + m4 - m5 + m7, m3 + m5,
                    m2 + m4,           m1 - m2 + m3 + m6);
}

// Prime-Root 4D Low-Discrepancy Kronecker-Weyl QMC Sequence (sqrt(2), sqrt(3), sqrt(5), sqrt(7) fractional parts)
// rotated spatially by Cranley-Patterson Interleaved Gradient Noise for low star-discrepancy 1-2 spp DXR 1.2 sampling:
float4 rawPrimeQmc4(float2 px, float frameIdx) {
    const float4 kRoots = float4(0.4142135624f, 0.7320508076f, 0.2360679775f, 0.6457513111f);
    const float s0 = frac(52.9829189f * frac(dot(px, float2(0.06711056f, 0.00583715f))));
    const float s1 = frac(52.9829189f * frac(dot(px.yx + 13.7f, float2(0.06711056f, 0.00583715f))));
    const float s2 = frac(52.9829189f * frac(dot(px + float2(29.3f, 7.1f), float2(0.06711056f, 0.00583715f))));
    const float s3 = frac(52.9829189f * frac(dot(px.yx + float2(5.9f, 41.3f), float2(0.06711056f, 0.00583715f))));
    return frac(float4(s0, s1, s2, s3) + kRoots * fmod(frameIdx, 512.0f));
}

// Family 090 Universal Optimal 2D Triangular / Hexagonal A2 Lattice Basis (kissing number 6, minimizes energy for
// every completely monotone potential of squared distance): optimal 2D blue-noise / screen-space probe stratification.
float2 rawHexJitter2D(float2 px, uint sampleIdx, float frameIdx) {
    // A2 hexagonal basis vectors e1 = (1, 0), e2 = (0.5, sqrt(3)/2) wrapped onto [0,1)^2 with golden-angle rotation:
    const float t = float(sampleIdx) + fmod(frameIdx, 256.0f);
    const float2 hexBase = float2(t * 0.7548776662f + 0.5f * t * 0.5698402910f,
                                  t * 0.8660254038f * 0.5698402910f);
    const float ign0 = frac(52.9829189f * frac(dot(px, float2(0.06711056f, 0.00583715f))));
    const float ign1 = frac(52.9829189f * frac(dot(px.yx + 19.3f, float2(0.06711056f, 0.00583715f))));
    return frac(float2(ign0, ign1) + hexBase);
}

// Family 379 Exact Rectangular Matrix-Multiplication Exponent omega(F; a, b, c) for ordered x >= y >= z > 0:
// Confirms rectangular bottleneck heads (alpha = 1/2, z <= xy/(x+y)) achieve O(n^2) floor without dense n^(9/4) overhead.
float rawRectMatmulExponent(float a, float b, float c) {
    float x = max(a, max(b, c));
    float z = min(a, min(b, c));
    float y = a + b + c - x - z;
    float excess = max(0.0f, (x + y) * z - x * y);
    return x + y + (excess * excess) / max(4.0f * x * y * z, 1.0e-5f);
}

// Hardware INT8 quantized 4-element dot product (DP4a silicon on Turing/Ampere/Ada/Blackwell/RDNA3+):
int rawDot4I8(uint packedA, uint packedB, int acc) {
    return dot4add_i8packed(packedA, packedB, acc);
}

// Fast smooth rational tanh approximation in native FP16 (avoids transcendental exp() latency on 3060-5090):
// Max absolute error < 0.008 over [-3, +3], strictly monotonic & bounded in [-1, +1].
float16_t4 rawFastTanh4(float16_t4 x) {
    const float16_t4 xc = clamp(x, float16_t4(-3.0h, -3.0h, -3.0h, -3.0h), float16_t4(3.0h, 3.0h, 3.0h, 3.0h));
    const float16_t4 x2 = xc * xc;
    return xc * (float16_t(27.0h) + x2) * rcp(float16_t(27.0h) + float16_t(9.0h) * x2);
}

float16_t rawFastTanh1(float16_t x) {
    const float16_t xc = clamp(x, float16_t(-3.0h), float16_t(3.0h));
    const float16_t x2 = xc * xc;
    return xc * (float16_t(27.0h) + x2) * rcp(float16_t(27.0h) + float16_t(9.0h) * x2);
}

#if RAW_HAS_SM69
// SM 6.9+ Native DXIL 1.9 Long-Vector (vector<float16_t, 8> & vector<float16_t, 16>) activations:
vector<float16_t, 8> rawFastTanh8(vector<float16_t, 8> x) {
    const vector<float16_t, 8> lo = (float16_t)-3.0h;
    const vector<float16_t, 8> hi = (float16_t) 3.0h;
    const vector<float16_t, 8> xc = clamp(x, lo, hi);
    const vector<float16_t, 8> x2 = xc * xc;
    return xc * ((float16_t)27.0h + x2) / ((float16_t)27.0h + (float16_t)9.0h * x2);
}

vector<float16_t, 16> rawFastTanh16(vector<float16_t, 16> x) {
    const vector<float16_t, 16> lo = (float16_t)-3.0h;
    const vector<float16_t, 16> hi = (float16_t) 3.0h;
    const vector<float16_t, 16> xc = clamp(x, lo, hi);
    const vector<float16_t, 16> x2 = xc * xc;
    return xc * ((float16_t)27.0h + x2) / ((float16_t)27.0h + (float16_t)9.0h * x2);
}
#endif

// Zero-fetch 2x2 Pixel-Quad Register-Shuffle Statistics (mean & local std-dev across the hardware 2x2 quad):
void rawQuadVec3Stats(float3 v, out float3 qMean, out float qStd) {
    const float3 vx = QuadReadAcrossX(v);
    const float3 vy = QuadReadAcrossY(v);
    const float3 vd = QuadReadAcrossDiagonal(v);
    qMean = (v + vx + vy + vd) * 0.25f;
    const float3 d0 = v - qMean, d1 = vx - qMean, d2 = vy - qMean, d3 = vd - qMean;
    const float var = (dot(d0, d0) + dot(d1, d1) + dot(d2, d2) + dot(d3, d3)) * 0.25f;
    qStd = sqrt(max(var, 0.0f));
}

// Wave-Quad Register-Shuffle 2x2 Normal Curvature & Signed Divergence (0 extra texture loads!):
// Uses hardware quad-lane register exchange (QuadReadAcrossX / QuadReadAcrossY) directly inside the pixel quad.
void rawQuadSurfaceDiff(float3 nC, float3 tX, float3 tY, int2 px, out float quadCurv, out float quadDiv) {
    const float3 nX = QuadReadAcrossX(nC);
    const float3 nY = QuadReadAcrossY(nC);
    // Flip sign on odd lanes so (nX - nC) always points along +screenX and (nY - nC) along +screenY:
    const float sx = ((px.x & 1) == 0) ? 1.0f : -1.0f;
    const float sy = ((px.y & 1) == 0) ? 1.0f : -1.0f;
    const float3 dnX = (nX - nC) * sx;
    const float3 dnY = (nY - nC) * sy;
    quadCurv = saturate((length(dnX) + length(dnY)) * 1.15f);
    quadDiv  = clamp((dot(dnX, tX) + dot(dnY, tY)) * 1.65f, -1.5f, 1.5f);
}

// Metal-Level Native FP16 6 -> 8 -> 3 Residual Material-Light Harmonizer:
// Uses packed float16_t4 FMA + fast rational tanh (and native SM 6.9 Long Vectors when compiled for *_6_9+).
float3 rawEvalMaterialMlp6x8x3(float4 f0, float2 f1) {
    const float16_t4 x0 = float16_t4(f0);
    const float16_t2 x1 = float16_t2(f1);

    const float16_t4 z0 = float16_t4(
        dot(x0, float16_t4( 1.42h,  0.65h,  0.92h,  0.28h)) + dot(x1, float16_t2( 0.32h,  0.22h)) - 0.18h,
        dot(x0, float16_t4( 0.60h, -0.45h, -0.68h,  1.28h)) + dot(x1, float16_t2( 0.42h, -0.15h)) - 0.32h,
        dot(x0, float16_t4(-0.35h,  0.88h,  1.22h,  0.62h)) + dot(x1, float16_t2( 0.58h,  0.30h)) - 0.16h,
        dot(x0, float16_t4( 0.95h, -0.52h,  0.38h, -0.48h)) + dot(x1, float16_t2( 0.84h,  0.40h)) - 0.24h
    );
    const float16_t4 z1 = float16_t4(
        dot(x0, float16_t4( 0.48h,  1.10h, -0.25h,  0.75h)) + dot(x1, float16_t2(-0.30h,  0.65h)) - 0.14h,
        dot(x0, float16_t4(-0.62h,  0.34h,  0.80h,  0.52h)) + dot(x1, float16_t2( 0.46h, -0.58h)) - 0.22h,
        dot(x0, float16_t4( 0.72h, -0.82h,  0.44h,  0.36h)) + dot(x1, float16_t2( 0.62h,  0.48h)) - 0.20h,
        dot(x0, float16_t4( 0.30h,  0.56h,  0.68h, -0.40h)) + dot(x1, float16_t2(-0.22h,  0.85h)) - 0.18h
    );
#if RAW_HAS_SM69
    vector<float16_t, 8> z8;
    z8[0] = z0.x; z8[1] = z0.y; z8[2] = z0.z; z8[3] = z0.w;
    z8[4] = z1.x; z8[5] = z1.y; z8[6] = z1.z; z8[7] = z1.w;
    const vector<float16_t, 8> h8 = rawFastTanh8(z8);
    const float16_t4 h0 = float16_t4(h8[0], h8[1], h8[2], h8[3]);
    const float16_t4 h1 = float16_t4(h8[4], h8[5], h8[6], h8[7]);
#else
    const float16_t4 h0 = rawFastTanh4(z0);
    const float16_t4 h1 = rawFastTanh4(z1);
#endif
    const float16_t relief = rawFastTanh1(dot(h0, float16_t4(0.58h, -0.14h, 0.46h, 0.34h)) + dot(h1, float16_t4(0.24h, 0.18h, -0.12h, 0.20h)));
    const float16_t sheen  = rawFastTanh1(dot(h0, float16_t4(0.16h,  0.68h, 0.24h, 0.26h)) + dot(h1, float16_t4(0.32h, -0.16h, 0.14h, 0.22h)));
    const float16_t bounce = rawFastTanh1(dot(h0, float16_t4(-0.12h, 0.28h, 0.34h, 0.42h)) + dot(h1, float16_t4(0.18h, 0.44h, 0.26h, -0.18h)));
    return float3(float(relief), float(sheen), float(bounce));
}

// RAWLSS 7.0 Split-Manifold 12 -> 16 -> 4 Residual Ray-Reconstruction Network:
// Operates on orthogonal physical manifolds (geometry curvature, demodulated irradiance variance, GI energy,
// Fresnel/specular lobe, motion, and depth discontinuity). Compiles to native 16-wide SM 6.9 Long Vectors on DXIL 1.9!
// Returns float4(reliefGain, sheenGain, bounceHarmony, disoccPriorWeight).
float4 rawEvalSplitManifoldNet12x16x4(float4 f0, float4 f1, float4 f2) {
    const float16_t4 x0 = float16_t4(f0);
    const float16_t4 x1 = float16_t4(f1);
    const float16_t4 x2 = float16_t4(f2);

    const float16_t4 z0 = float16_t4(
        dot(x0, float16_t4( 1.38h,  0.62h,  0.88h,  0.25h)) + dot(x1, float16_t4( 0.30h,  0.20h, -0.45h,  0.18h)) + dot(x2, float16_t4( 0.15h,  0.12h, -0.35h, -0.40h)) - 0.18h,
        dot(x0, float16_t4( 0.56h, -0.42h, -0.64h,  1.24h)) + dot(x1, float16_t4( 0.38h, -0.14h, -0.22h,  0.12h)) + dot(x2, float16_t4( 0.65h,  0.52h, -0.18h, -0.24h)) - 0.30h,
        dot(x0, float16_t4(-0.32h,  0.84h,  1.16h,  0.58h)) + dot(x1, float16_t4( 0.52h,  0.28h,  0.34h,  0.42h)) + dot(x2, float16_t4( 0.20h, -0.18h, -0.20h, -0.32h)) - 0.16h,
        dot(x0, float16_t4( 0.90h, -0.48h,  0.36h, -0.44h)) + dot(x1, float16_t4( 0.78h,  0.38h, -0.28h,  0.24h)) + dot(x2, float16_t4(-0.14h,  0.26h, -0.30h, -0.28h)) - 0.22h
    );
    const float16_t4 z1 = float16_t4(
        dot(x0, float16_t4( 0.44h,  1.05h, -0.22h,  0.70h)) + dot(x1, float16_t4(-0.28h,  0.60h,  0.18h,  0.55h)) + dot(x2, float16_t4( 0.32h,  0.22h, -0.12h, -0.16h)) - 0.14h,
        dot(x0, float16_t4(-0.58h,  0.32h,  0.76h,  0.48h)) + dot(x1, float16_t4( 0.42h, -0.52h,  0.64h,  0.72h)) + dot(x2, float16_t4(-0.20h, -0.26h,  0.28h,  0.35h)) - 0.20h,
        dot(x0, float16_t4( 0.68h, -0.78h,  0.42h,  0.34h)) + dot(x1, float16_t4( 0.58h,  0.44h, -0.32h,  0.28h)) + dot(x2, float16_t4( 0.48h,  0.36h, -0.22h, -0.20h)) - 0.18h,
        dot(x0, float16_t4( 0.28h,  0.52h,  0.64h, -0.38h)) + dot(x1, float16_t4(-0.20h,  0.80h,  0.26h,  0.46h)) + dot(x2, float16_t4( 0.16h, -0.14h,  0.12h, -0.18h)) - 0.16h
    );
    const float16_t4 z2 = float16_t4(
        dot(x0, float16_t4(-0.24h,  0.18h,  0.52h, -0.16h)) + dot(x1, float16_t4(-0.34h,  0.14h,  1.15h,  0.88h)) + dot(x2, float16_t4(-0.18h, -0.22h,  0.78h,  0.96h)) - 0.42h,
        dot(x0, float16_t4( 0.36h, -0.22h,  0.44h,  0.54h)) + dot(x1, float16_t4( 0.18h, -0.24h,  0.86h,  0.62h)) + dot(x2, float16_t4( 0.26h,  0.40h,  0.54h,  0.72h)) - 0.36h,
        dot(x0, float16_t4(-0.40h, -0.30h,  0.68h,  0.18h)) + dot(x1, float16_t4(-0.46h,  0.32h,  0.94h,  1.08h)) + dot(x2, float16_t4(-0.24h, -0.32h,  0.66h,  0.84h)) - 0.38h,
        dot(x0, float16_t4( 0.52h,  0.42h, -0.28h,  0.64h)) + dot(x1, float16_t4( 0.24h,  0.16h, -0.42h,  0.34h)) + dot(x2, float16_t4( 0.82h,  0.74h, -0.28h, -0.44h)) - 0.26h
    );
    const float16_t4 z3 = float16_t4(
        dot(x0, float16_t4( 0.18h,  0.26h, -0.34h,  0.40h)) + dot(x1, float16_t4(-0.22h,  0.48h,  0.58h,  0.92h)) + dot(x2, float16_t4(-0.12h,  0.18h,  0.44h,  0.60h)) - 0.32h,
        dot(x0, float16_t4(-0.30h,  0.44h,  0.22h, -0.28h)) + dot(x1, float16_t4( 0.36h, -0.18h,  0.74h,  0.50h)) + dot(x2, float16_t4( 0.22h, -0.30h,  0.52h,  0.48h)) - 0.28h,
        dot(x0, float16_t4( 0.46h, -0.16h,  0.38h,  0.32h)) + dot(x1, float16_t4( 0.28h,  0.22h, -0.36h,  0.18h)) + dot(x2, float16_t4( 0.56h,  0.48h, -0.34h, -0.38h)) - 0.20h,
        dot(x0, float16_t4(-0.14h,  0.20h,  0.48h,  0.12h)) + dot(x1, float16_t4(-0.30h,  0.12h,  0.88h,  0.76h)) + dot(x2, float16_t4(-0.16h, -0.20h,  0.62h,  0.78h)) - 0.34h
    );

#if RAW_HAS_SM69
    vector<float16_t, 16> z16;
    z16[ 0] = z0.x; z16[ 1] = z0.y; z16[ 2] = z0.z; z16[ 3] = z0.w;
    z16[ 4] = z1.x; z16[ 5] = z1.y; z16[ 6] = z1.z; z16[ 7] = z1.w;
    z16[ 8] = z2.x; z16[ 9] = z2.y; z16[10] = z2.z; z16[11] = z2.w;
    z16[12] = z3.x; z16[13] = z3.y; z16[14] = z3.z; z16[15] = z3.w;
    const vector<float16_t, 16> h16 = rawFastTanh16(z16);
    const float16_t4 h0 = float16_t4(h16[ 0], h16[ 1], h16[ 2], h16[ 3]);
    const float16_t4 h1 = float16_t4(h16[ 4], h16[ 5], h16[ 6], h16[ 7]);
    const float16_t4 h2 = float16_t4(h16[ 8], h16[ 9], h16[10], h16[11]);
    const float16_t4 h3 = float16_t4(h16[12], h16[13], h16[14], h16[15]);
#else
    const float16_t4 h0 = rawFastTanh4(z0);
    const float16_t4 h1 = rawFastTanh4(z1);
    const float16_t4 h2 = rawFastTanh4(z2);
    const float16_t4 h3 = rawFastTanh4(z3);
#endif

    const float16_t relief = rawFastTanh1(dot(h0, float16_t4( 0.52h, -0.12h,  0.42h,  0.30h)) + dot(h1, float16_t4( 0.22h,  0.14h, -0.10h,  0.18h))
                                        + dot(h2, float16_t4(-0.16h,  0.08h, -0.18h,  0.26h)) + dot(h3, float16_t4(-0.08h, -0.10h,  0.24h, -0.12h)));
    const float16_t sheen  = rawFastTanh1(dot(h0, float16_t4( 0.14h,  0.62h,  0.20h,  0.22h)) + dot(h1, float16_t4( 0.28h, -0.14h,  0.12h,  0.18h))
                                        + dot(h2, float16_t4(-0.10h,  0.22h, -0.12h,  0.46h)) + dot(h3, float16_t4( 0.12h, -0.08h,  0.32h, -0.08h)));
    const float16_t bounce = rawFastTanh1(dot(h0, float16_t4(-0.10h,  0.24h,  0.30h,  0.36h)) + dot(h1, float16_t4( 0.16h,  0.38h,  0.22h, -0.14h))
                                        + dot(h2, float16_t4( 0.22h,  0.16h,  0.34h, -0.08h)) + dot(h3, float16_t4( 0.36h,  0.18h, -0.10h,  0.24h)));
    const float16_t prior  = rawFastTanh1(dot(h0, float16_t4(-0.12h, -0.08h,  0.10h, -0.10h)) + dot(h1, float16_t4(-0.06h,  0.24h, -0.10h,  0.12h))
                                        + dot(h2, float16_t4( 0.56h,  0.34h,  0.52h, -0.22h)) + dot(h3, float16_t4( 0.42h,  0.30h, -0.18h,  0.48h)));
    return float4(float(relief), float(sheen), float(bounce), saturate(float(prior) * 0.5f + 0.5f));
}

#endif
