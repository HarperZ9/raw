// RawDestructionEngine.cpp: First-Principles 3D Voronoi-Plane CSG Mesh Fracture & Rigid-Body Destruction Engine.
// Original work, MIT.

#include "RawDestructionEngine.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace raw::worldgen {
namespace {

constexpr double kPiD = 3.14159265358979323846;

struct Vec3d {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

inline Vec3d ToDouble(Vec3 v) { return {double(v.x), double(v.y), double(v.z)}; }
inline Vec3  ToFloat(Vec3d v) { return {float(v.x), float(v.y), float(v.z)}; }

inline Vec3d operator+(Vec3d a, Vec3d b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3d operator-(Vec3d a, Vec3d b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3d operator*(Vec3d a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3d operator/(Vec3d a, double s) { return {a.x / s, a.y / s, a.z / s}; }

inline double DotD(Vec3d a, Vec3d b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3d  CrossD(Vec3d a, Vec3d b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double LenSqD(Vec3d a) { return DotD(a, a); }
inline double LenD(Vec3d a) { return std::sqrt(LenSqD(a)); }
inline Vec3d  NormD(Vec3d a) {
    const double l = LenD(a);
    return l > 1.0e-12 ? (a / l) : Vec3d{0.0, 0.0, 1.0};
}

// Deterministic SplitMix32 PRNG (platform-independent bit-exact sequence).
struct SplitMix32 {
    uint32_t state = 1u;
    explicit SplitMix32(uint32_t seed) : state(seed ? seed : 0x9E3779B9u) {}

    uint32_t NextU32() {
        uint32_t z = (state += 0x9E3779B9u);
        z = (z ^ (z >> 16u)) * 0x85EBCA6Bu;
        z = (z ^ (z >> 13u)) * 0xC2B2AE35u;
        return z ^ (z >> 16u);
    }

    double Next01() {
        return double(NextU32() & 0x00FFFFFFu) / double(0x01000000u);
    }

    double Range(double lo, double hi) {
        return lo + (hi - lo) * Next01();
    }
};

// Ensures an 8-channel material splat is non-negative and strictly sums to 1.0.
MaterialSplat8 MakeSplat(float rock, float snow, float ice, float basalt,
                         float magma, float moss, float sand, float crystal,
                         float roughnessHint, float emissiveMask,
                         TerrainMaterialId dominant) {
    const float raw[8] = {
        std::max(0.0f, rock),  std::max(0.0f, snow),  std::max(0.0f, ice),     std::max(0.0f, basalt),
        std::max(0.0f, magma), std::max(0.0f, moss),  std::max(0.0f, sand),    std::max(0.0f, crystal),
    };
    float sum = 0.0f;
    for (float w : raw) sum += w;
    const float inv = sum > 1.0e-6f ? (1.0f / sum) : 1.0f;

    MaterialSplat8 s{};
    s.splat0 = {raw[0] * inv, raw[1] * inv, raw[2] * inv, raw[3] * inv};
    s.splat1 = {raw[4] * inv, raw[5] * inv, raw[6] * inv, raw[7] * inv};
    if (sum <= 1.0e-6f) {
        s.splat0 = {1.0f, 0.0f, 0.0f, 0.0f};
        s.splat1 = {0.0f, 0.0f, 0.0f, 0.0f};
    }
    s.roughnessHint = Clamp01(roughnessHint);
    s.emissiveMask  = Clamp01(emissiveMask);
    s.dominant      = dominant;
    return s;
}

void BuildOrthonormalBasisD(Vec3d n, Vec3d* outU, Vec3d* outV) {
    const Vec3d ref = (std::fabs(n.z) < 0.9) ? Vec3d{0.0, 0.0, 1.0} : Vec3d{1.0, 0.0, 0.0};
    *outU = NormD(CrossD(ref, n));
    *outV = CrossD(n, *outU); // Guarantees u x v == n!
}

struct PolyFaceD {
    std::vector<Vec3d> verts;
    Vec3d              normal{0.0, 0.0, 1.0};
    bool               isInterior = false;
};

struct ConvexPolyD {
    std::vector<PolyFaceD> faces;
};

ConvexPolyD ToPolyD(const ConvexPolyhedron& poly) {
    ConvexPolyD out;
    out.faces.reserve(poly.faces.size());
    for (const auto& f : poly.faces) {
        if (f.vertices.size() < 3u) continue;
        PolyFaceD fd;
        fd.verts.reserve(f.vertices.size());
        for (const Vec3& v : f.vertices) fd.verts.push_back(ToDouble(v));
        fd.normal     = NormD(ToDouble(f.normal));
        fd.isInterior = f.isInterior;
        out.faces.push_back(std::move(fd));
    }
    return out;
}

ConvexPolyhedron FromPolyD(const ConvexPolyD& polyD, const ConvexPolyhedron& tmpl) {
    ConvexPolyhedron out;
    out.exteriorMaterial = tmpl.exteriorMaterial;
    out.interiorMaterial = tmpl.interiorMaterial;
    out.densityKgM3      = tmpl.densityKgM3;
    out.restitution      = tmpl.restitution;
    out.friction         = tmpl.friction;
    out.name             = tmpl.name;
    out.faces.reserve(polyD.faces.size());
    for (const auto& fd : polyD.faces) {
        if (fd.verts.size() < 3u) continue;
        PolyFace f;
        f.vertices.reserve(fd.verts.size());
        for (const Vec3d& vd : fd.verts) f.vertices.push_back(ToFloat(vd));
        f.normal     = ToFloat(NormD(fd.normal));
        f.isInterior = fd.isInterior;
        out.faces.push_back(std::move(f));
    }
    return out;
}

double ComputeFaceAreaD(const std::vector<Vec3d>& verts) {
    if (verts.size() < 3u) return 0.0;
    Vec3d sumCross{0.0, 0.0, 0.0};
    const Vec3d& v0 = verts[0];
    for (size_t i = 1; i + 1 < verts.size(); ++i) {
        sumCross = sumCross + CrossD(verts[i] - v0, verts[i + 1] - v0);
    }
    return 0.5 * LenD(sumCross);
}

void DeduplicateRingD(std::vector<Vec3d>& ring, double eps) {
    if (ring.size() < 2u) return;
    const double epsSq = eps * eps;
    std::vector<Vec3d> cleaned;
    cleaned.reserve(ring.size());
    for (const Vec3d& p : ring) {
        if (cleaned.empty() || LenSqD(p - cleaned.back()) > epsSq) {
            cleaned.push_back(p);
        }
    }
    while (cleaned.size() >= 2u && LenSqD(cleaned.front() - cleaned.back()) <= epsSq) {
        cleaned.pop_back();
    }
    ring = std::move(cleaned);
}

// Removes near-collinear redundant vertices along a convex planar polygon ring.
void PruneCollinearRingD(std::vector<Vec3d>& ring, Vec3d normal, double areaEps) {
    if (ring.size() <= 3u) return;
    std::vector<Vec3d> kept;
    kept.reserve(ring.size());
    const size_t n = ring.size();
    for (size_t i = 0; i < n; ++i) {
        const Vec3d& prev = ring[(i + n - 1u) % n];
        const Vec3d& curr = ring[i];
        const Vec3d& next = ring[(i + 1u) % n];
        const double signedArea2 = DotD(CrossD(curr - prev, next - curr), normal);
        if (signedArea2 > areaEps) {
            kept.push_back(curr);
        }
    }
    if (kept.size() >= 3u) {
        ring = std::move(kept);
    }
}

// Exact double-precision half-space clip: keeps Dot(x - planePt, planeNormalOut) <= 0.
bool ClipPolyDByPlane(const ConvexPolyD& inPoly, Vec3d planePt, Vec3d planeNormalOut,
                      bool markCapInterior, ConvexPolyD* outPoly) {
    if (!outPoly || inPoly.faces.empty()) return false;
    const Vec3d n = NormD(planeNormalOut);
    constexpr double kEps = 1.0e-8;

    bool anyInside  = false;
    bool anyOutside = false;
    for (const auto& f : inPoly.faces) {
        for (const Vec3d& v : f.verts) {
            const double d = DotD(v - planePt, n);
            if (d < -kEps) anyInside  = true;
            if (d >  kEps) anyOutside = true;
        }
    }

    if (!anyOutside) {
        *outPoly = inPoly;
        return true;
    }
    if (!anyInside) {
        outPoly->faces.clear();
        return false;
    }

    ConvexPolyD result;
    result.faces.reserve(inPoly.faces.size() + 1u);
    std::vector<Vec3d> capPoints;
    capPoints.reserve(24u);

    auto pushCapPoint = [&](Vec3d pt) {
        // Project strictly onto plane to avoid drift across chained clips
        const double d = DotD(pt - planePt, n);
        pt = pt - n * d;
        constexpr double kMergeSq = 1.0e-12;
        for (const Vec3d& ex : capPoints) {
            if (LenSqD(ex - pt) <= kMergeSq) return;
        }
        capPoints.push_back(pt);
    };

    for (const auto& f : inPoly.faces) {
        const size_t nv = f.verts.size();
        if (nv < 3u) continue;

        std::vector<Vec3d> clipped;
        clipped.reserve(nv + 2u);

        for (size_t i = 0; i < nv; ++i) {
            const Vec3d& a  = f.verts[i];
            const Vec3d& b  = f.verts[(i + 1u) % nv];
            const double da = DotD(a - planePt, n);
            const double db = DotD(b - planePt, n);

            const bool aIn = (da <= kEps);
            const bool bIn = (db <= kEps);

            if (std::fabs(da) <= kEps) {
                pushCapPoint(a);
            }

            if (aIn && bIn) {
                clipped.push_back(b);
            } else if (aIn && !bIn) {
                const double denom = (da - db);
                const double t     = (std::fabs(denom) > 1.0e-15) ? std::clamp(da / denom, 0.0, 1.0) : 0.0;
                const Vec3d  hit   = a + (b - a) * t;
                clipped.push_back(hit);
                pushCapPoint(hit);
            } else if (!aIn && bIn) {
                const double denom = (da - db);
                const double t     = (std::fabs(denom) > 1.0e-15) ? std::clamp(da / denom, 0.0, 1.0) : 0.0;
                const Vec3d  hit   = a + (b - a) * t;
                clipped.push_back(hit);
                clipped.push_back(b);
                pushCapPoint(hit);
            }
        }

        DeduplicateRingD(clipped, 1.0e-6);
        PruneCollinearRingD(clipped, f.normal, 1.0e-10);
        if (clipped.size() >= 3u && ComputeFaceAreaD(clipped) > 1.0e-9) {
            PolyFaceD nf;
            nf.verts      = std::move(clipped);
            nf.normal     = f.normal;
            nf.isInterior = f.isInterior;
            result.faces.push_back(std::move(nf));
        }
    }

    // Reconstruct the coplanar cap face with guaranteed outward CCW winding relative to +n.
    if (capPoints.size() >= 3u) {
        Vec3d centroid{0.0, 0.0, 0.0};
        for (const Vec3d& p : capPoints) centroid = centroid + p;
        centroid = centroid / double(capPoints.size());

        Vec3d u{}, v{};
        BuildOrthonormalBasisD(n, &u, &v);

        struct AngularPt {
            Vec3d  pt;
            double angle = 0.0;
        };
        std::vector<AngularPt> sorted;
        sorted.reserve(capPoints.size());
        for (const Vec3d& p : capPoints) {
            const Vec3d rel = p - centroid;
            const double ang = std::atan2(DotD(rel, v), DotD(rel, u));
            sorted.push_back({p, ang});
        }
        std::sort(sorted.begin(), sorted.end(), [](const AngularPt& a, const AngularPt& b) {
            return a.angle < b.angle;
        });

        std::vector<Vec3d> capRing;
        capRing.reserve(sorted.size());
        for (const auto& s : sorted) capRing.push_back(s.pt);

        DeduplicateRingD(capRing, 1.0e-6);
        PruneCollinearRingD(capRing, n, 1.0e-10);

        if (capRing.size() >= 3u && ComputeFaceAreaD(capRing) > 1.0e-9) {
            // Double-check outward winding against +n; flip if ever reversed.
            Vec3d areaVec{0.0, 0.0, 0.0};
            for (size_t i = 1; i + 1 < capRing.size(); ++i) {
                areaVec = areaVec + CrossD(capRing[i] - capRing[0], capRing[i + 1] - capRing[0]);
            }
            if (DotD(areaVec, n) < 0.0) {
                std::reverse(capRing.begin(), capRing.end());
            }

            PolyFaceD cap;
            cap.verts      = std::move(capRing);
            cap.normal     = n;
            cap.isInterior = markCapInterior;
            result.faces.push_back(std::move(cap));
        }
    }

    if (result.faces.size() < 4u) {
        outPoly->faces.clear();
        return false;
    }

    *outPoly = std::move(result);
    return true;
}

// Builds a closed convex polyhedron from a box [mn, mx] with outward CCW faces.
ConvexPolyD MakeBoxPolyD(Vec3d mn, Vec3d mx) {
    const Vec3d c000{mn.x, mn.y, mn.z};
    const Vec3d c100{mx.x, mn.y, mn.z};
    const Vec3d c110{mx.x, mx.y, mn.z};
    const Vec3d c010{mn.x, mx.y, mn.z};
    const Vec3d c001{mn.x, mn.y, mx.z};
    const Vec3d c101{mx.x, mn.y, mx.z};
    const Vec3d c111{mx.x, mx.y, mx.z};
    const Vec3d c011{mn.x, mx.y, mx.z};

    ConvexPolyD p;
    p.faces = {
        {{c000, c010, c110, c100}, { 0,  0, -1}, false}, // -Z bottom
        {{c001, c101, c111, c011}, { 0,  0,  1}, false}, // +Z top
        {{c000, c100, c101, c001}, { 0, -1,  0}, false}, // -Y front
        {{c110, c010, c011, c111}, { 0,  1,  0}, false}, // +Y back
        {{c010, c000, c001, c011}, {-1,  0,  0}, false}, // -X left
        {{c100, c110, c111, c101}, { 1,  0,  0}, false}, // +X right
    };
    return p;
}

// Builds a regular N-sided prism (with optional top taper) resting at baseZ..baseZ+height.
ConvexPolyD MakeTaperedPrismD(Vec3d baseCenter, uint32_t sides, double rBot, double rTop, double height, double angleOffsetRad) {
    ConvexPolyD p;
    std::vector<Vec3d> bot(sides), top(sides);
    for (uint32_t i = 0; i < sides; ++i) {
        const double ang = angleOffsetRad + (2.0 * kPiD * double(i)) / double(sides);
        const double ca  = std::cos(ang);
        const double sa  = std::sin(ang);
        bot[i] = {baseCenter.x + rBot * ca, baseCenter.y + rBot * sa, baseCenter.z};
        top[i] = {baseCenter.x + rTop * ca, baseCenter.y + rTop * sa, baseCenter.z + height};
    }

    // Bottom cap (-Z normal -> CW viewed from +Z == CCW viewed from -Z)
    {
        std::vector<Vec3d> bRev(bot.rbegin(), bot.rend());
        p.faces.push_back({std::move(bRev), {0.0, 0.0, -1.0}, false});
    }
    // Top cap (+Z normal -> CCW viewed from +Z)
    p.faces.push_back({top, {0.0, 0.0, 1.0}, false});

    // Side quads
    for (uint32_t i = 0; i < sides; ++i) {
        const uint32_t j = (i + 1u) % sides;
        const Vec3d e1   = bot[j] - bot[i];
        const Vec3d e2   = top[i] - bot[i];
        const Vec3d n    = NormD(CrossD(e1, e2));
        p.faces.push_back({{bot[i], bot[j], top[j], top[i]}, n, false});
    }
    return p;
}

MassProperties ComputeMassPropertiesD(const ConvexPolyD& poly, double densityKgM3) {
    MassProperties mp{};
    if (poly.faces.size() < 4u) return mp;

    // Reference point inside polyhedron (mean of face centroids) for numerical precision.
    Vec3d ref{0.0, 0.0, 0.0};
    size_t vCount = 0u;
    for (const auto& f : poly.faces) {
        for (const Vec3d& v : f.verts) {
            ref = ref + v;
            ++vCount;
        }
    }
    if (vCount == 0u) return mp;
    ref = ref / double(vCount);

    double sumDetJ = 0.0;
    Vec3d  sumComNum{0.0, 0.0, 0.0};

    for (const auto& f : poly.faces) {
        if (f.verts.size() < 3u) continue;
        const Vec3d p0 = f.verts[0] - ref;
        for (size_t i = 1; i + 1 < f.verts.size(); ++i) {
            const Vec3d p1 = f.verts[i] - ref;
            const Vec3d p2 = f.verts[i + 1u] - ref;
            const double detJ = DotD(p0, CrossD(p1, p2));
            sumDetJ += detJ;
            sumComNum = sumComNum + (p0 + p1 + p2) * detJ;
        }
    }

    const double volume = sumDetJ / 6.0;
    if (volume <= 1.0e-12 || !std::isfinite(volume)) {
        return mp;
    }

    const Vec3d com = ref + sumComNum / (4.0 * sumDetJ);

    // Exact 3x3 covariance tensor integrated over signed tetrahedra about COM:
    // C_ab = sum(detJ / 120 * (2*(q0_a*q0_b + q1_a*q1_b + q2_a*q2_b) +
    //                          q0_a*q1_b + q1_a*q0_b + q0_a*q2_b + q2_a*q0_b + q1_a*q2_b + q2_a*q1_b))
    double C[3][3] = {};
    double maxR2   = 0.0;

    for (const auto& f : poly.faces) {
        if (f.verts.size() < 3u) continue;
        for (const Vec3d& v : f.verts) {
            maxR2 = std::max(maxR2, LenSqD(v - com));
        }
        const Vec3d q0 = f.verts[0] - com;
        for (size_t i = 1; i + 1 < f.verts.size(); ++i) {
            const Vec3d q1 = f.verts[i] - com;
            const Vec3d q2 = f.verts[i + 1u] - com;
            const double detJ = DotD(q0, CrossD(q1, q2));
            const double w    = detJ / 120.0;
            const double a[3] = {q0.x, q0.y, q0.z};
            const double b[3] = {q1.x, q1.y, q1.z};
            const double c[3] = {q2.x, q2.y, q2.z};
            for (int r = 0; r < 3; ++r) {
                for (int col = 0; col < 3; ++col) {
                    const double term =
                        2.0 * (a[r] * a[col] + b[r] * b[col] + c[r] * c[col]) +
                        (a[r] * b[col] + b[r] * a[col] +
                         a[r] * c[col] + c[r] * a[col] +
                         b[r] * c[col] + c[r] * b[col]);
                    C[r][col] += w * term;
                }
            }
        }
    }

    const double rho = std::max(1.0, densityKgM3);
    mp.volumeM3       = float(volume);
    mp.massKg         = float(volume * rho);
    mp.centroid       = ToFloat(com);
    mp.boundingRadius = float(std::sqrt(std::max(0.0, maxR2)));

    // Inertia tensor I = rho * (tr(C)*Identity - C)
    const double Ixx = rho * (C[1][1] + C[2][2]);
    const double Iyy = rho * (C[0][0] + C[2][2]);
    const double Izz = rho * (C[0][0] + C[1][1]);
    const double Ixy = -rho * C[0][1];
    const double Ixz = -rho * C[0][2];
    const double Iyz = -rho * C[1][2];

    const double minDiag = std::max(1.0e-7, double(mp.massKg) * maxR2 * 1.0e-4);
    mp.inertiaTensor.m[0][0] = float(std::max(minDiag, Ixx));
    mp.inertiaTensor.m[1][1] = float(std::max(minDiag, Iyy));
    mp.inertiaTensor.m[2][2] = float(std::max(minDiag, Izz));
    mp.inertiaTensor.m[0][1] = mp.inertiaTensor.m[1][0] = float(Ixy);
    mp.inertiaTensor.m[0][2] = mp.inertiaTensor.m[2][0] = float(Ixz);
    mp.inertiaTensor.m[1][2] = mp.inertiaTensor.m[2][1] = float(Iyz);
    return mp;
}

void ComputeAabbD(const ConvexPolyD& poly, Vec3d* outMin, Vec3d* outMax) {
    Vec3d mn{std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max()};
    Vec3d mx{std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest()};
    for (const auto& f : poly.faces) {
        for (const Vec3d& v : f.verts) {
            mn.x = std::min(mn.x, v.x); mn.y = std::min(mn.y, v.y); mn.z = std::min(mn.z, v.z);
            mx.x = std::max(mx.x, v.x); mx.y = std::max(mx.y, v.y); mx.z = std::max(mx.z, v.z);
        }
    }
    *outMin = mn;
    *outMax = mx;
}

bool PointInsideConvexPolyD(const ConvexPolyD& poly, Vec3d pt, double tol = 1.0e-6) {
    for (const auto& f : poly.faces) {
        if (f.verts.empty()) continue;
        if (DotD(pt - f.verts[0], f.normal) > tol) return false;
    }
    return true;
}

// --- Rigid-Body Quaternion & 3x3 Matrix Helpers ---

Quat NormalizeQuat(Quat q) {
    const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (len < 1.0e-7f || !std::isfinite(len)) return {0.0f, 0.0f, 0.0f, 1.0f};
    const float inv = 1.0f / len;
    return {q.x * inv, q.y * inv, q.z * inv, q.w * inv};
}

Vec3 RotateVec(Quat q, Vec3 v) {
    const Vec3 qv{q.x, q.y, q.z};
    const Vec3 t = Cross(qv, v) * 2.0f;
    return v + t * q.w + Cross(qv, t);
}

Mat3 QuatToMat3(Quat q) {
    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    Mat3 R{};
    R.m[0][0] = 1.0f - 2.0f * (yy + zz);
    R.m[0][1] = 2.0f * (xy - wz);
    R.m[0][2] = 2.0f * (xz + wy);
    R.m[1][0] = 2.0f * (xy + wz);
    R.m[1][1] = 1.0f - 2.0f * (xx + zz);
    R.m[1][2] = 2.0f * (yz - wx);
    R.m[2][0] = 2.0f * (xz - wy);
    R.m[2][1] = 2.0f * (yz + wx);
    R.m[2][2] = 1.0f - 2.0f * (xx + yy);
    return R;
}

Mat3 MulMat3(const Mat3& a, const Mat3& b) {
    Mat3 r{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
        }
    }
    return r;
}

Mat3 TransposeMat3(const Mat3& a) {
    Mat3 r{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            r.m[i][j] = a.m[j][i];
    return r;
}

Vec3 MulMat3Vec(const Mat3& a, Vec3 v) {
    return {
        a.m[0][0] * v.x + a.m[0][1] * v.y + a.m[0][2] * v.z,
        a.m[1][0] * v.x + a.m[1][1] * v.y + a.m[1][2] * v.z,
        a.m[2][0] * v.x + a.m[2][1] * v.y + a.m[2][2] * v.z,
    };
}

Mat3 InvertMat3Safe(const Mat3& a) {
    const double a00 = a.m[0][0], a01 = a.m[0][1], a02 = a.m[0][2];
    const double a10 = a.m[1][0], a11 = a.m[1][1], a12 = a.m[1][2];
    const double a20 = a.m[2][0], a21 = a.m[2][1], a22 = a.m[2][2];

    const double c00 = a11 * a22 - a12 * a21;
    const double c01 = -(a10 * a22 - a12 * a20);
    const double c02 = a10 * a21 - a11 * a20;
    const double c10 = -(a01 * a22 - a02 * a21);
    const double c11 = a00 * a22 - a02 * a20;
    const double c12 = -(a00 * a21 - a01 * a20);
    const double c20 = a01 * a12 - a02 * a11;
    const double c21 = -(a00 * a12 - a02 * a10);
    const double c22 = a00 * a11 - a01 * a10;

    const double det = a00 * c00 + a01 * c01 + a02 * c02;
    Mat3 inv{};
    if (std::fabs(det) < 1.0e-12 || !std::isfinite(det)) {
        inv.m[0][0] = (a00 > 1.0e-7) ? float(1.0 / a00) : 1.0f;
        inv.m[1][1] = (a11 > 1.0e-7) ? float(1.0 / a11) : 1.0f;
        inv.m[2][2] = (a22 > 1.0e-7) ? float(1.0 / a22) : 1.0f;
        return inv;
    }

    const double id = 1.0 / det;
    inv.m[0][0] = float(c00 * id); inv.m[0][1] = float(c10 * id); inv.m[0][2] = float(c20 * id);
    inv.m[1][0] = float(c01 * id); inv.m[1][1] = float(c11 * id); inv.m[1][2] = float(c21 * id);
    inv.m[2][0] = float(c02 * id); inv.m[2][1] = float(c12 * id); inv.m[2][2] = float(c22 * id);
    return inv;
}

Vec3 ClampVecLength(Vec3 v, float maxLen) {
    const float l = Length(v);
    return (l > maxLen && l > 1.0e-6f) ? (v * (maxLen / l)) : v;
}

void AppendShardGeometry(const ConvexPolyhedron& localPoly,
                         Vec3 worldPos,
                         Quat worldOrient,
                         uint32_t shardId,
                         std::vector<TerrainVertex>* outVerts,
                         std::vector<uint32_t>* outIndices) {
    for (const PolyFace& face : localPoly.faces) {
        if (face.vertices.size() < 3u) continue;

        const Vec3 nWorld = Normalize(RotateVec(worldOrient, face.normal));
        Vec3d uD{}, vD{};
        BuildOrthonormalBasisD(ToDouble(face.normal), &uD, &vD);
        const Vec3 uLocal = ToFloat(uD);
        const Vec3 vLocal = ToFloat(vD);

        const MaterialSplat8& mat = face.isInterior ? localPoly.interiorMaterial : localPoly.exteriorMaterial;
        const float curvatureMeta = face.isInterior ? 0.92f : 0.25f;
        const float roughnessMeta = mat.roughnessHint;

        const uint32_t baseVert = static_cast<uint32_t>(outVerts->size());
        std::vector<Vec3> worldPts;
        worldPts.reserve(face.vertices.size());

        for (const Vec3& lv : face.vertices) {
            const Vec3 wp = worldPos + RotateVec(worldOrient, lv);
            worldPts.push_back(wp);

            TerrainVertex tv{};
            tv.pos[0]    = wp.x;
            tv.pos[1]    = wp.y;
            tv.pos[2]    = wp.z;
            tv.normal[0] = nWorld.x;
            tv.normal[1] = nWorld.y;
            tv.normal[2] = nWorld.z;
            tv.uv[0]     = Dot(lv, uLocal) * 0.5f + 0.5f;
            tv.uv[1]     = Dot(lv, vLocal) * 0.5f + 0.5f;
            tv.splat0[0] = mat.splat0.x;
            tv.splat0[1] = mat.splat0.y;
            tv.splat0[2] = mat.splat0.z;
            tv.splat0[3] = mat.splat0.w;
            tv.splat1[0] = mat.splat1.x;
            tv.splat1[1] = mat.splat1.y;
            tv.splat1[2] = mat.splat1.z;
            tv.splat1[3] = mat.splat1.w;
            tv.meta[0]   = curvatureMeta;
            tv.meta[1]   = roughnessMeta;
            tv.meta[2]   = mat.emissiveMask;
            tv.meta[3]   = float(shardId);
            outVerts->push_back(tv);
        }

        // Fan-triangulate convex polygon, filtering any microscopic slivers below validator area gate.
        const Vec3& p0 = worldPts[0];
        for (size_t i = 1; i + 1 < worldPts.size(); ++i) {
            const Vec3 e1 = worldPts[i] - p0;
            const Vec3 e2 = worldPts[i + 1u] - p0;
            const Vec3 cr = Cross(e1, e2);
            const float areaSq = Dot(cr, cr);
            if (!std::isfinite(areaSq) || areaSq < 1.0e-8f) {
                continue;
            }
            if (Dot(cr, nWorld) >= 0.0f) {
                outIndices->push_back(baseVert);
                outIndices->push_back(baseVert + uint32_t(i));
                outIndices->push_back(baseVert + uint32_t(i + 1u));
            } else {
                outIndices->push_back(baseVert);
                outIndices->push_back(baseVert + uint32_t(i + 1u));
                outIndices->push_back(baseVert + uint32_t(i));
            }
        }
    }
}

bool FinalizeProceduralMesh(const std::vector<TerrainVertex>& srcVerts,
                            const std::vector<uint32_t>& srcIndices,
                            TerrainMaterialId primaryMat,
                            uint32_t maxMeshletTris,
                            const std::string& meshName,
                            ProceduralMesh* outMesh) {
    if (!outMesh || srcVerts.empty() || srcIndices.size() < 3u) return false;

    const uint32_t limTris = std::clamp(maxMeshletTris, 16u, 126u);
    outMesh->name            = meshName;
    outMesh->primaryMaterial = primaryMat;
    outMesh->lodLevel        = 0u;
    outMesh->vertices.clear();
    outMesh->indices.clear();
    outMesh->meshlets.clear();

    BuildMeshlets(srcVerts, srcIndices, 64u, limTris,
                  &outMesh->vertices, &outMesh->indices, &outMesh->meshlets);

    if (outMesh->vertices.empty() || outMesh->meshlets.empty()) return false;

    Vec3 mn{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Vec3 mx{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
    for (const auto& v : outMesh->vertices) {
        mn.x = std::min(mn.x, v.pos[0]); mn.y = std::min(mn.y, v.pos[1]); mn.z = std::min(mn.z, v.pos[2]);
        mx.x = std::max(mx.x, v.pos[0]); mx.y = std::max(mx.y, v.pos[1]); mx.z = std::max(mx.z, v.pos[2]);
    }
    outMesh->aabbMin = mn;
    outMesh->aabbMax = mx;
    return true;
}

}  // namespace

const char* GetDestructibleArchetypeName(DestructibleArchetype archetype) {
    switch (archetype) {
        case DestructibleArchetype::NordicStonePillar: return "NordicStonePillar";
        case DestructibleArchetype::GlacialIceBoulder: return "GlacialIceBoulder";
        case DestructibleArchetype::TimberBarricade:   return "TimberBarricade";
        case DestructibleArchetype::BasaltColumn:      return "BasaltColumn";
        case DestructibleArchetype::CastleBattlement:  return "CastleBattlement";
        default:                                       return "UnknownDestructible";
    }
}

ConvexPolyhedron BuildDestructibleArchetype(DestructibleArchetype archetype,
                                            float scale,
                                            Vec3 baseCenter) {
    const double s  = std::max(0.1, double(scale));
    const Vec3d  bc = ToDouble(baseCenter);

    ConvexPolyhedron tmpl{};
    tmpl.name = GetDestructibleArchetypeName(archetype);
    ConvexPolyD polyD{};

    switch (archetype) {
        case DestructibleArchetype::NordicStonePillar: {
            // Tapered octagonal Nordic carved ruin stone pillar (height 2.8m, radius 0.55m -> 0.44m)
            polyD = MakeTaperedPrismD(bc, 8u, 0.55 * s, 0.44 * s, 2.8 * s, kPiD / 8.0);
            tmpl.exteriorMaterial = MakeSplat(0.55f, 0.0f, 0.0f, 0.15f, 0.0f, 0.30f, 0.0f, 0.0f,
                                              0.82f, 0.0f, TerrainMaterialId::RockGranite);
            tmpl.interiorMaterial = MakeSplat(0.86f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.14f, 0.0f,
                                              0.94f, 0.0f, TerrainMaterialId::RockGranite);
            tmpl.densityKgM3 = 2650.0f;
            tmpl.restitution = 0.22f;
            tmpl.friction    = 0.72f;
            break;
        }
        case DestructibleArchetype::GlacialIceBoulder: {
            // Multi-faceted crystalline glacial boulder carved from a bounding box via deterministic bevel planes
            const double r = 0.85 * s;
            const Vec3d center{bc.x, bc.y, bc.z + r};
            polyD = MakeBoxPolyD({center.x - r, center.y - r, center.z - r},
                                 {center.x + r, center.y + r, center.z + r});
            const Vec3d bevels[10] = {
                { 1,  1,  1}, {-1,  1,  1}, { 1, -1,  1}, {-1, -1,  1},
                { 1,  1, -1}, {-1,  1, -1}, { 1, -1, -1}, {-1, -1, -1},
                { 1,  0,  0.65}, {-0.65, 1, 0.45},
            };
            for (int i = 0; i < 10; ++i) {
                const Vec3d n = NormD(bevels[i]);
                const double dist = (i < 8 ? 0.78 : 0.84) * r;
                ConvexPolyD next{};
                if (ClipPolyDByPlane(polyD, center + n * dist, n, false, &next)) {
                    polyD = std::move(next);
                }
            }
            // Rest cleanly on bc.z
            Vec3d mn{}, mx{};
            ComputeAabbD(polyD, &mn, &mx);
            const double shiftZ = bc.z - mn.z;
            for (auto& f : polyD.faces)
                for (Vec3d& v : f.verts) v.z += shiftZ;

            tmpl.exteriorMaterial = MakeSplat(0.0f, 0.55f, 0.45f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                              0.32f, 0.0f, TerrainMaterialId::SnowPack);
            tmpl.interiorMaterial = MakeSplat(0.0f, 0.0f, 0.82f, 0.0f, 0.0f, 0.0f, 0.0f, 0.18f,
                                              0.11f, 0.0f, TerrainMaterialId::GlacialIce);
            tmpl.densityKgM3 = 917.0f;
            tmpl.restitution = 0.34f;
            tmpl.friction    = 0.25f;
            break;
        }
        case DestructibleArchetype::TimberBarricade: {
            // Heavy chamfered Nordic timber defense beam slab (width 2.2m, depth 0.42m, height 1.15m)
            const double hx = 1.10 * s;
            const double hy = 0.21 * s;
            const double hz = 1.15 * s;
            polyD = MakeBoxPolyD({bc.x - hx, bc.y - hy, bc.z},
                                 {bc.x + hx, bc.y + hy, bc.z + hz});
            // Bevel top front/back shoulders like a sharpened Nordic barricade timber
            ConvexPolyD next{};
            if (ClipPolyDByPlane(polyD, {bc.x, bc.y + hy * 0.45, bc.z + hz * 0.88}, NormD({0.0, 0.85, 0.52}), false, &next)) {
                polyD = std::move(next);
            }
            if (ClipPolyDByPlane(polyD, {bc.x, bc.y - hy * 0.45, bc.z + hz * 0.88}, NormD({0.0, -0.85, 0.52}), false, &next)) {
                polyD = std::move(next);
            }
            tmpl.exteriorMaterial = MakeSplat(0.15f, 0.0f, 0.0f, 0.50f, 0.0f, 0.35f, 0.0f, 0.0f,
                                              0.88f, 0.0f, TerrainMaterialId::BasaltColumn);
            tmpl.interiorMaterial = MakeSplat(0.18f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.82f, 0.0f,
                                              0.78f, 0.0f, TerrainMaterialId::RiverPebbleSand);
            tmpl.densityKgM3 = 620.0f;
            tmpl.restitution = 0.28f;
            tmpl.friction    = 0.68f;
            break;
        }
        case DestructibleArchetype::BasaltColumn: {
            // Giant's Causeway hexagonal columnar basalt prism (radius 0.48m, height 2.4m, slight top bevel)
            polyD = MakeTaperedPrismD(bc, 6u, 0.48 * s, 0.46 * s, 2.4 * s, 0.0);
            ConvexPolyD next{};
            if (ClipPolyDByPlane(polyD, {bc.x, bc.y, bc.z + 2.28 * s}, NormD({0.25, 0.12, 0.96}), false, &next)) {
                polyD = std::move(next);
            }
            tmpl.exteriorMaterial = MakeSplat(0.0f, 0.10f, 0.0f, 0.72f, 0.0f, 0.18f, 0.0f, 0.0f,
                                              0.62f, 0.0f, TerrainMaterialId::BasaltColumn);
            tmpl.interiorMaterial = MakeSplat(0.14f, 0.0f, 0.0f, 0.82f, 0.04f, 0.0f, 0.0f, 0.0f,
                                              0.35f, 0.05f, TerrainMaterialId::BasaltColumn);
            tmpl.densityKgM3 = 2950.0f;
            tmpl.restitution = 0.20f;
            tmpl.friction    = 0.70f;
            break;
        }
        case DestructibleArchetype::CastleBattlement:
        default: {
            // Heavy fortress merlon masonry block with beveled coping cap (1.4m x 0.75m x 1.6m)
            const double hx = 0.70 * s;
            const double hy = 0.38 * s;
            const double hz = 1.60 * s;
            polyD = MakeBoxPolyD({bc.x - hx, bc.y - hy, bc.z},
                                 {bc.x + hx, bc.y + hy, bc.z + hz});
            ConvexPolyD next{};
            if (ClipPolyDByPlane(polyD, {bc.x + hx * 0.82, bc.y, bc.z + hz * 0.92}, NormD({0.6, 0.0, 0.8}), false, &next)) {
                polyD = std::move(next);
            }
            if (ClipPolyDByPlane(polyD, {bc.x - hx * 0.82, bc.y, bc.z + hz * 0.92}, NormD({-0.6, 0.0, 0.8}), false, &next)) {
                polyD = std::move(next);
            }
            tmpl.exteriorMaterial = MakeSplat(0.58f, 0.24f, 0.0f, 0.0f, 0.0f, 0.18f, 0.0f, 0.0f,
                                              0.85f, 0.0f, TerrainMaterialId::RockGranite);
            tmpl.interiorMaterial = MakeSplat(0.65f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.35f, 0.0f,
                                              0.95f, 0.0f, TerrainMaterialId::RockGranite);
            tmpl.densityKgM3 = 2400.0f;
            tmpl.restitution = 0.18f;
            tmpl.friction    = 0.78f;
            break;
        }
    }

    return FromPolyD(polyD, tmpl);
}

MassProperties ComputeMassProperties(const ConvexPolyhedron& poly) {
    return ComputeMassPropertiesD(ToPolyD(poly), double(poly.densityKgM3));
}

bool ClipConvexPolyhedronByPlane(const ConvexPolyhedron& inPoly,
                                 Vec3 planePoint,
                                 Vec3 planeNormalOut,
                                 bool markCapInterior,
                                 ConvexPolyhedron* outPoly) {
    if (!outPoly) return false;
    ConvexPolyD clippedD{};
    if (!ClipPolyDByPlane(ToPolyD(inPoly), ToDouble(planePoint), ToDouble(planeNormalOut), markCapInterior, &clippedD)) {
        outPoly->faces.clear();
        return false;
    }
    *outPoly = FromPolyD(clippedD, inPoly);
    return true;
}

FractureResult FractureConvexBody(const ConvexPolyhedron& body, const FractureConfig& cfg) {
    FractureResult res{};
    const ConvexPolyD origD = ToPolyD(body);
    const MassProperties origProps = ComputeMassPropertiesD(origD, double(body.densityKgM3));
    if (origProps.volumeM3 <= 1.0e-9f) {
        return res;
    }
    res.originalVolume = origProps.volumeM3;

    Vec3d aabbMin{}, aabbMax{};
    ComputeAabbD(origD, &aabbMin, &aabbMax);
    const Vec3d extents = aabbMax - aabbMin;
    const Vec3d center  = (aabbMin + aabbMax) * 0.5;
    const double diag   = std::max(1.0e-3, LenD(extents));

    const uint32_t targetCount = std::clamp(cfg.shardCount, 2u, 96u);
    SplitMix32 rng(cfg.seed);

    // Determine clamped impact point inside or on the body AABB for splinter clustering.
    Vec3d impact = cfg.useImpactClustering ? ToDouble(cfg.impactPoint) : center;
    impact.x = std::clamp(impact.x, aabbMin.x, aabbMax.x);
    impact.y = std::clamp(impact.y, aabbMin.y, aabbMax.y);
    impact.z = std::clamp(impact.z, aabbMin.z, aabbMax.z);

    const double focus = cfg.useImpactClustering ? double(Clamp01(cfg.clusterFocus01)) : 0.0;
    const double minSiteSepSq = std::pow(diag * 0.025, 2.0);

    // Generate deterministic Voronoi sites: mix of impact-clustered radial sites (fine splinters
    // near impact) and volume-filling stratified sites (larger chunks further away).
    std::vector<Vec3d> sites;
    sites.reserve(targetCount);
    const uint32_t clusterCount = uint32_t(std::round(double(targetCount) * focus * 0.65));

    for (uint32_t attempt = 0; attempt < targetCount * 64u && sites.size() < targetCount; ++attempt) {
        Vec3d cand{};
        if (sites.size() < clusterCount) {
            // Biased radial distribution around impact point (r ~ u^1.6 for denser core splinters)
            const double u = rng.Next01();
            const double r = diag * (0.04 + 0.42 * std::pow(u, 1.6));
            const double z = rng.Range(-1.0, 1.0);
            const double phi = rng.Range(0.0, 2.0 * kPiD);
            const double sxy = std::sqrt(std::max(0.0, 1.0 - z * z));
            cand = impact + Vec3d{r * sxy * std::cos(phi), r * sxy * std::sin(phi), r * z};
            cand.x = std::clamp(cand.x, aabbMin.x + 1.0e-4, aabbMax.x - 1.0e-4);
            cand.y = std::clamp(cand.y, aabbMin.y + 1.0e-4, aabbMax.y - 1.0e-4);
            cand.z = std::clamp(cand.z, aabbMin.z + 1.0e-4, aabbMax.z - 1.0e-4);
        } else {
            cand = {
                rng.Range(aabbMin.x + 1.0e-4, aabbMax.x - 1.0e-4),
                rng.Range(aabbMin.y + 1.0e-4, aabbMax.y - 1.0e-4),
                rng.Range(aabbMin.z + 1.0e-4, aabbMax.z - 1.0e-4),
            };
        }

        if (!PointInsideConvexPolyD(origD, cand, 1.0e-4)) continue;

        bool tooClose = false;
        for (const Vec3d& ex : sites) {
            if (LenSqD(ex - cand) < minSiteSepSq) {
                tooClose = true;
                break;
            }
        }
        if (!tooClose) {
            sites.push_back(cand);
        }
    }

    // Fallback guarantee: always produce at least 2 distinct interior sites if rejection sampling was tight.
    if (sites.size() < 2u) {
        sites.clear();
        const Vec3d comD = ToDouble(origProps.centroid);
        sites.push_back(comD - Vec3d{extents.x * 0.15, 0.0, 0.0});
        sites.push_back(comD + Vec3d{extents.x * 0.15, 0.0, 0.0});
    }

    // Build exact 3D Voronoi cell C_i by clipping origD against bisector half-spaces of all j != i.
    std::vector<std::pair<double, size_t>> neighborOrder;
    neighborOrder.reserve(sites.size());

    double totalShardVolD = 0.0;
    uint32_t nextShardId  = 0u;

    for (size_t i = 0; i < sites.size(); ++i) {
        const Vec3d si = sites[i];
        neighborOrder.clear();
        for (size_t j = 0; j < sites.size(); ++j) {
            if (i == j) continue;
            neighborOrder.push_back({LenSqD(sites[j] - si), j});
        }
        std::sort(neighborOrder.begin(), neighborOrder.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        ConvexPolyD cell = origD;
        bool alive = true;

        for (const auto& [distSq, j] : neighborOrder) {
            const double dist = std::sqrt(distSq);
            if (dist < 1.0e-9) continue;

            // Fast exact bounding-radius early-out: if current cell's farthest vertex from si
            // is closer than half the distance to sj, neither sj nor any farther site can clip cell!
            double maxR2 = 0.0;
            for (const auto& f : cell.faces) {
                for (const Vec3d& v : f.verts) {
                    maxR2 = std::max(maxR2, LenSqD(v - si));
                }
            }
            if (0.5 * dist > std::sqrt(maxR2) + 1.0e-6) {
                break;
            }

            const Vec3d mid = (si + sites[j]) * 0.5;
            const Vec3d nOut = (sites[j] - si) / dist; // Points from site i toward site j (outside cell i)

            ConvexPolyD clipped{};
            if (!ClipPolyDByPlane(cell, mid, nOut, /*markCapInterior=*/true, &clipped)) {
                alive = false;
                break;
            }
            cell = std::move(clipped);
        }

        if (!alive || cell.faces.size() < 4u) continue;

        const MassProperties shardProps = ComputeMassPropertiesD(cell, double(body.densityKgM3));
        if (shardProps.volumeM3 < cfg.minShardVolumeM3 || !std::isfinite(shardProps.volumeM3)) {
            continue;
        }

        totalShardVolD += double(shardProps.volumeM3);

        // Center shard polyhedron at its exact center of mass (local COM space) for rigid-body dynamics.
        const Vec3d comD = ToDouble(shardProps.centroid);
        uint32_t extFaces = 0u;
        uint32_t intFaces = 0u;
        std::vector<Vec3> uniqueVerts;
        uniqueVerts.reserve(24u);

        for (auto& f : cell.faces) {
            if (f.isInterior) ++intFaces; else ++extFaces;
            for (Vec3d& v : f.verts) {
                v = v - comD;
                const Vec3 vf = ToFloat(v);
                bool dup = false;
                for (const Vec3& ex : uniqueVerts) {
                    const Vec3 d = ex - vf;
                    if (Dot(d, d) <= 1.0e-8f) { dup = true; break; }
                }
                if (!dup) uniqueVerts.push_back(vf);
            }
        }

        FracturedShard shard{};
        shard.shardId           = nextShardId++;
        shard.localPoly         = FromPolyD(cell, body);
        shard.localPoly.name    = body.name + "_shard_" + std::to_string(shard.shardId);
        shard.uniqueLocalVerts  = std::move(uniqueVerts);
        shard.massProps         = shardProps;
        shard.exteriorFaceCount = extFaces;
        shard.interiorFaceCount = intFaces;
        res.shards.push_back(std::move(shard));
    }

    res.totalShardVolume = float(totalShardVolD);
    res.volumeErrorRel   = (res.originalVolume > 1.0e-9f)
                             ? std::fabs(res.totalShardVolume - res.originalVolume) / res.originalVolume
                             : 1.0f;
    res.ok = !res.shards.empty() && std::isfinite(res.volumeErrorRel);
    return res;
}

// --- DestructionSimulation ---

DestructionSimulation::DestructionSimulation(const DestructionSimConfig& cfg) : m_cfg(cfg) {}

void DestructionSimulation::Reset() {
    m_bodies.clear();
    m_elapsedSeconds = 0.0f;
    m_stepCount      = 0u;
}

void DestructionSimulation::SetConfig(const DestructionSimConfig& cfg) {
    m_cfg = cfg;
    if (m_cfg.substeps == 0u) m_cfg.substeps = 1u;
}

bool DestructionSimulation::InitializeFromFracture(const FractureResult& fracture,
                                                   const DestructionImpact* initialImpact) {
    Reset();
    if (!fracture.ok || fracture.shards.empty()) return false;

    m_bodies.reserve(fracture.shards.size());
    for (const FracturedShard& sh : fracture.shards) {
        if (sh.massProps.massKg <= 1.0e-6f) continue;
        DebrisBody b{};
        b.shardId         = sh.shardId;
        b.shard           = sh;
        b.position        = sh.massProps.centroid;
        b.orientation     = {0.0f, 0.0f, 0.0f, 1.0f};
        b.invMass         = 1.0f / sh.massProps.massKg;
        b.invInertiaLocal = InvertMat3Safe(sh.massProps.inertiaTensor);
        b.asleep          = false;
        b.sleepTimer      = 0.0f;
        m_bodies.push_back(std::move(b));
    }

    if (initialImpact) {
        ApplyImpactImpulse(*initialImpact);
    }
    return !m_bodies.empty();
}

bool DestructionSimulation::FractureAndInitialize(DestructibleArchetype archetype,
                                                  float scale,
                                                  Vec3 baseCenter,
                                                  const FractureConfig& fractureCfg,
                                                  const DestructionImpact& impact,
                                                  FractureResult* outFracture) {
    ConvexPolyhedron poly = BuildDestructibleArchetype(archetype, scale, baseCenter);
    FractureConfig cfg = fractureCfg;
    if (cfg.useImpactClustering && Length(cfg.impactPoint) < 1.0e-6f && Length(impact.point) > 1.0e-6f) {
        cfg.impactPoint = impact.point;
    }
    FractureResult frac = FractureConvexBody(poly, cfg);
    const bool ok = InitializeFromFracture(frac, &impact);
    if (outFracture) *outFracture = std::move(frac);
    return ok;
}

void DestructionSimulation::ApplyImpactImpulse(const DestructionImpact& impact) {
    const Vec3 strikeDir = Normalize(impact.direction);
    const float falloff  = std::max(0.15f, impact.falloffRadiusM);

    for (DebrisBody& b : m_bodies) {
        const Vec3 delta = b.position - impact.point;
        const float dist = Length(delta);
        const float atten = 1.0f / (1.0f + (dist / falloff) * (dist / falloff));

        // Outward radial explosion direction with slight upward lift
        Vec3 radialDir = (dist > 1.0e-4f) ? (delta / dist) : strikeDir;
        radialDir = Normalize(radialDir + Vec3{0.0f, 0.0f, Clamp01(impact.upwardBias01)});

        // Scale impulse proportionally to body mass^(2/3) (exposed cross-section) so tiny splinters
        // fly crisply while heavy base slabs still experience realistic momentum without exploding.
        const float massScale = std::clamp(std::pow(b.shard.massProps.massKg / 40.0f, 0.45f), 0.25f, 3.0f);
        const Vec3 J = (strikeDir * impact.impulseNs + radialDir * impact.radialBurstNs) * (atten * massScale);

        // Lever arm from COM toward the side of the shard facing the impact point for realistic spin (r x J).
        const float leverLen = std::min(dist, b.shard.massProps.boundingRadius * 0.65f);
        const Vec3  leverDir = (dist > 1.0e-4f) ? ((impact.point - b.position) / dist) : Vec3{0.0f, 0.0f, -1.0f};
        const Vec3  r        = leverDir * leverLen;

        const Mat3 R = QuatToMat3(b.orientation);
        const Mat3 invIWorld = MulMat3(MulMat3(R, b.invInertiaLocal), TransposeMat3(R));

        b.linearVel  = ClampVecLength(b.linearVel + J * b.invMass, m_cfg.maxLinearSpeed);
        b.angularVel = ClampVecLength(b.angularVel + MulMat3Vec(invIWorld, Cross(r, J)), m_cfg.maxAngularSpeed);
        b.asleep     = false;
        b.sleepTimer = 0.0f;
    }
}

void DestructionSimulation::Step(float dtSeconds) {
    const float clampedDt = Clamp(dtSeconds, 0.0f, 1.0f / 20.0f);
    if (clampedDt <= 0.0f) return;

    const uint32_t steps = std::clamp(m_cfg.substeps, 1u, 8u);
    const float h = clampedDt / float(steps);
    for (uint32_t s = 0; s < steps; ++s) {
        StepSubstep(h);
    }
    m_elapsedSeconds += clampedDt;
    ++m_stepCount;
}

void DestructionSimulation::StepSubstep(float h) {
    const float linDecay = std::exp(-std::max(0.0f, m_cfg.linearDamping) * h);
    const float angDecay = std::exp(-std::max(0.0f, m_cfg.angularDamping) * h);
    const float groundAngDecay = std::exp(-std::max(0.0f, m_cfg.groundAngularDamp) * h);
    const Vec3  nGround{0.0f, 0.0f, 1.0f};

    for (DebrisBody& b : m_bodies) {
        if (b.asleep || b.invMass <= 0.0f) continue;

        // 1. Semi-implicit symplectic Euler velocity integration
        b.linearVel.z -= m_cfg.gravityZ * h;
        b.linearVel  = ClampVecLength(b.linearVel * linDecay, m_cfg.maxLinearSpeed);
        b.angularVel = ClampVecLength(b.angularVel * angDecay, m_cfg.maxAngularSpeed);

        // 2. Position & quaternion integration
        b.position = b.position + b.linearVel * h;
        const Quat dq{
            0.5f * (b.angularVel.x * b.orientation.w + b.angularVel.y * b.orientation.z - b.angularVel.z * b.orientation.y),
            0.5f * (b.angularVel.y * b.orientation.w + b.angularVel.z * b.orientation.x - b.angularVel.x * b.orientation.z),
            0.5f * (b.angularVel.z * b.orientation.w + b.angularVel.x * b.orientation.y - b.angularVel.y * b.orientation.x),
            0.5f * (-b.angularVel.x * b.orientation.x - b.angularVel.y * b.orientation.y - b.angularVel.z * b.orientation.z),
        };
        b.orientation = NormalizeQuat({
            b.orientation.x + dq.x * h,
            b.orientation.y + dq.y * h,
            b.orientation.z + dq.z * h,
            b.orientation.w + dq.w * h,
        });

        // 3. Multi-point ground-plane contact detection & sequential-impulse manifold resolution
        float minZ = std::numeric_limits<float>::max();
        for (const Vec3& lv : b.shard.uniqueLocalVerts) {
            const Vec3 wp = b.position + RotateVec(b.orientation, lv);
            if (wp.z < minZ) minZ = wp.z;
        }

        const bool touchingGround = (minZ <= m_cfg.groundZ + 0.02f);
        if (minZ < m_cfg.groundZ) {
            ++b.contactCount;
            const float penetration = m_cfg.groundZ - minZ;

            // Positional depenetration so no vertex ever sinks below groundZ
            b.position.z += penetration;

            // Collect all manifold contact vertices within a tight band of the ground plane
            std::vector<Vec3> contacts;
            contacts.reserve(8u);
            for (const Vec3& lv : b.shard.uniqueLocalVerts) {
                const Vec3 wp = b.position + RotateVec(b.orientation, lv);
                if (wp.z <= m_cfg.groundZ + 0.015f) {
                    contacts.push_back(wp);
                }
            }
            if (contacts.empty()) {
                contacts.push_back({b.position.x, b.position.y, m_cfg.groundZ});
            }

            const Mat3 R = QuatToMat3(b.orientation);
            const Mat3 invIWorld = MulMat3(MulMat3(R, b.invInertiaLocal), TransposeMat3(R));
            const float mu = std::max(0.05f, b.shard.localPoly.friction);

            // Sequential Gauss-Seidel impulse passes over the support manifold stabilize flat-face resting
            // without spurious single-point rocking torque.
            for (int iter = 0; iter < 3; ++iter) {
                for (const Vec3& cp : contacts) {
                    const Vec3 r = cp - b.position;
                    const Vec3 vContact = b.linearVel + Cross(b.angularVel, r);
                    const float vn = Dot(vContact, nGround);
                    if (vn >= 0.0f) continue;

                    const Vec3 rXn   = Cross(r, nGround);
                    const float kN   = b.invMass + Dot(nGround, Cross(MulMat3Vec(invIWorld, rXn), r));
                    const float rest = (iter == 0 && std::fabs(vn) >= 0.65f) ? Clamp01(b.shard.localPoly.restitution) : 0.0f;
                    const float jN   = (kN > 1.0e-7f) ? (-(1.0f + rest) * vn / kN) : 0.0f;

                    if (jN > 0.0f) {
                        const Vec3 impulseN = nGround * jN;
                        b.linearVel  = b.linearVel + impulseN * b.invMass;
                        b.angularVel = b.angularVel + MulMat3Vec(invIWorld, Cross(r, impulseN));

                        // Coulomb friction impulse in horizontal tangent plane
                        const Vec3 vPost  = b.linearVel + Cross(b.angularVel, r);
                        const Vec3 vt     = vPost - nGround * Dot(vPost, nGround);
                        const float vtLen = Length(vt);
                        if (vtLen > 1.0e-5f) {
                            const Vec3 tDir  = vt / vtLen;
                            const Vec3 rXt   = Cross(r, tDir);
                            const float kT   = b.invMass + Dot(tDir, Cross(MulMat3Vec(invIWorld, rXt), r));
                            const float jT   = (kT > 1.0e-7f) ? std::clamp(-vtLen / kT, -mu * jN, mu * jN) : 0.0f;
                            const Vec3 impulseT = tDir * jT;
                            b.linearVel  = b.linearVel + impulseT * b.invMass;
                            b.angularVel = b.angularVel + MulMat3Vec(invIWorld, Cross(r, impulseT));
                        }
                    }
                }
            }

            // Extra rolling & sliding damping on ground contact so shards settle crisply onto stable faces
            b.angularVel = b.angularVel * groundAngDecay;
            if (std::fabs(b.linearVel.z) < 0.15f) {
                b.linearVel.x *= groundAngDecay;
                b.linearVel.y *= groundAngDecay;
            }
            if (contacts.size() >= 3u && std::fabs(b.linearVel.z) < 0.18f) {
                b.linearVel.z = std::max(0.0f, b.linearVel.z);
                b.angularVel  = b.angularVel * 0.72f;
            }
            b.linearVel  = ClampVecLength(b.linearVel, m_cfg.maxLinearSpeed);
            b.angularVel = ClampVecLength(b.angularVel, m_cfg.maxAngularSpeed);
        }

        // 4. Automatic sleep settling
        const float linSpd = Length(b.linearVel);
        const float angSpd = Length(b.angularVel);
        if (touchingGround && linSpd <= m_cfg.sleepLinSpeed && angSpd <= m_cfg.sleepAngSpeed) {
            b.sleepTimer += h;
            if (b.sleepTimer >= m_cfg.sleepHoldSeconds) {
                b.linearVel  = {0.0f, 0.0f, 0.0f};
                b.angularVel = {0.0f, 0.0f, 0.0f};
                b.asleep     = true;
            }
        } else {
            b.sleepTimer = 0.0f;
        }
    }
}

DestructionSimStats DestructionSimulation::GetStats() const noexcept {
    DestructionSimStats st{};
    st.totalBodies    = static_cast<uint32_t>(m_bodies.size());
    st.elapsedSeconds = m_elapsedSeconds;
    st.stepCount      = m_stepCount;
    st.minVertexZ     = m_bodies.empty() ? m_cfg.groundZ : std::numeric_limits<float>::max();

    for (const DebrisBody& b : m_bodies) {
        if (b.asleep) ++st.asleepBodies; else ++st.awakeBodies;

        const bool bodyFinite =
            std::isfinite(b.position.x) && std::isfinite(b.position.y) && std::isfinite(b.position.z) &&
            std::isfinite(b.linearVel.x) && std::isfinite(b.linearVel.y) && std::isfinite(b.linearVel.z) &&
            std::isfinite(b.angularVel.x) && std::isfinite(b.angularVel.y) && std::isfinite(b.angularVel.z) &&
            std::isfinite(b.orientation.x) && std::isfinite(b.orientation.y) &&
            std::isfinite(b.orientation.z) && std::isfinite(b.orientation.w);
        if (!bodyFinite) {
            st.allFinite = false;
            continue;
        }

        const float spd = Length(b.linearVel);
        st.maxSpeed = std::max(st.maxSpeed, spd);
        st.kineticEnergyJ += 0.5f * b.shard.massProps.massKg * Dot(b.linearVel, b.linearVel);

        for (const Vec3& lv : b.shard.uniqueLocalVerts) {
            const Vec3 wp = b.position + RotateVec(b.orientation, lv);
            st.minVertexZ = std::min(st.minVertexZ, wp.z);
        }
    }
    return st;
}

bool DestructionSimulation::BuildMeshFromDestructionState(uint32_t maxMeshletTris,
                                                          ProceduralMesh* outMesh,
                                                          const std::string& meshName) const {
    if (!outMesh || m_bodies.empty()) return false;

    std::vector<TerrainVertex> srcVerts;
    std::vector<uint32_t>      srcIndices;
    srcVerts.reserve(m_bodies.size() * 48u);
    srcIndices.reserve(m_bodies.size() * 72u);

    for (const DebrisBody& b : m_bodies) {
        AppendShardGeometry(b.shard.localPoly, b.position, b.orientation, b.shardId, &srcVerts, &srcIndices);
    }

    const TerrainMaterialId primaryMat = m_bodies.front().shard.localPoly.exteriorMaterial.dominant;
    return FinalizeProceduralMesh(srcVerts, srcIndices, primaryMat, maxMeshletTris, meshName, outMesh);
}

bool DestructionSimulation::BuildMeshFromShard(size_t bodyIndex,
                                               uint32_t maxMeshletTris,
                                               ProceduralMesh* outMesh) const {
    if (!outMesh || bodyIndex >= m_bodies.size()) return false;

    const DebrisBody& b = m_bodies[bodyIndex];
    std::vector<TerrainVertex> srcVerts;
    std::vector<uint32_t>      srcIndices;
    AppendShardGeometry(b.shard.localPoly, b.position, b.orientation, b.shardId, &srcVerts, &srcIndices);

    return FinalizeProceduralMesh(srcVerts, srcIndices, b.shard.localPoly.exteriorMaterial.dominant,
                                  maxMeshletTris, b.shard.localPoly.name, outMesh);
}

bool BuildMeshFromFractureResult(const FractureResult& fracture,
                                 float explodeFactor,
                                 uint32_t maxMeshletTris,
                                 ProceduralMesh* outMesh,
                                 const std::string& meshName) {
    if (!outMesh || !fracture.ok || fracture.shards.empty()) return false;

    // Compute overall centroid for optional radial explosion visualization
    Vec3 totalCenter{0.0f, 0.0f, 0.0f};
    float totalVol = 0.0f;
    for (const FracturedShard& sh : fracture.shards) {
        totalCenter = totalCenter + sh.massProps.centroid * sh.massProps.volumeM3;
        totalVol += sh.massProps.volumeM3;
    }
    if (totalVol > 1.0e-7f) totalCenter = totalCenter / totalVol;

    std::vector<TerrainVertex> srcVerts;
    std::vector<uint32_t>      srcIndices;
    srcVerts.reserve(fracture.shards.size() * 48u);
    srcIndices.reserve(fracture.shards.size() * 72u);

    const Quat idQ{0.0f, 0.0f, 0.0f, 1.0f};
    for (const FracturedShard& sh : fracture.shards) {
        const Vec3 offset = (sh.massProps.centroid - totalCenter) * explodeFactor;
        AppendShardGeometry(sh.localPoly, sh.massProps.centroid + offset, idQ, sh.shardId, &srcVerts, &srcIndices);
    }

    const TerrainMaterialId primaryMat = fracture.shards.front().localPoly.exteriorMaterial.dominant;
    return FinalizeProceduralMesh(srcVerts, srcIndices, primaryMat, maxMeshletTris, meshName, outMesh);
}

}  // namespace raw::worldgen
