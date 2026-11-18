// gtao_precision.inc: the engine's GTAO for one choice of types. Included six times by
// gtao_precision.cpp with D, C and R defined as float or double and PREC_NAME set.
static C PREC_NAME(SampleDepth)(const raw::post::ViewGBuffer& v, C fx, C fy, int sx, int sy)
{
    const C nearest = C(v.depth[size_t(sy) * v.w + sx]);
    const int x0 = int(std::floor(fx - C(0.5))), y0 = int(std::floor(fy - C(0.5)));
    const C ax = fx - C(0.5) - C(x0), ay = fy - C(0.5) - C(y0);
    C k[4], lo = C(1e30), hi = 0;
    for (int q = 0; q < 4; ++q) {
        const int qx = x0 + (q & 1), qy = y0 + (q >> 1);
        if (!v.valid(qx, qy)) return nearest;
        const C d = C(v.depth[size_t(qy) * v.w + qx]);
        lo = std::min(lo, d); hi = std::max(hi, d);
        k[q] = C(1) / d;
    }
    if (hi > C(1.1) * lo) return nearest;
    const C inv = (k[0] * (C(1) - ax) + k[1] * ax) * (C(1) - ay) + (k[2] * (C(1) - ax) + k[3] * ax) * ay;
    return C(1) / inv;
}

static void PREC_NAME(Project)(const raw::post::ViewGBuffer& v, D px, D py, D pz, D& sx, D& sy)
{
    const D w = -pz;
    sx = (px / (w * D(v.tanHalf) * D(v.aspect)) * D(0.5) + D(0.5)) * D(v.w);
    sy = (D(1) - (py / (w * D(v.tanHalf)) * D(0.5) + D(0.5))) * D(v.h);
}

static void PREC_NAME(Row)(const raw::post::ViewGBuffer& v, double radiusIn, int slices, int steps, int y, std::vector<float>& out)
{
    const R kPi = R(3.14159265358979323846), radius = R(radiusIn);
    {
        for (int x = 0; x < v.w; ++x) {
            const size_t i = size_t(y) * v.w + x;
            if (v.depth[i] < 0.0f) continue;
            const R P[3] = {R(v.pos[i * 3]), R(v.pos[i * 3 + 1]), R(v.pos[i * 3 + 2])};
            const R N[3] = {R(v.nrm[i * 3]), R(v.nrm[i * 3 + 1]), R(v.nrm[i * 3 + 2])};
            const R depth = R(v.depth[i]);
            const R pl = std::sqrt(P[0] * P[0] + P[1] * P[1] + P[2] * P[2]);
            const R V[3] = {-P[0] / pl, -P[1] / pl, -P[2] / pl};
            const R rpx = radius * R(v.h) / (R(2) * R(v.tanHalf) * depth);
            R T[3] = {R(1) - V[0] * V[0], -V[0] * V[1], -V[0] * V[2]};
            const R tl = std::sqrt(T[0] * T[0] + T[1] * T[1] + T[2] * T[2]);
            T[0] /= tl; T[1] /= tl; T[2] /= tl;
            const R B[3] = {V[1] * T[2] - V[2] * T[1], V[2] * T[0] - V[0] * T[2], V[0] * T[1] - V[1] * T[0]};
            D sx0, sy0;
            PREC_NAME(Project)(v, D(P[0]), D(P[1]), D(P[2]), sx0, sy0);
            R total = 0;
            for (int s = 0; s < slices; ++s) {
                const R phi = kPi * (R(s) + R(0.5)) / R(slices);
                const R dir[3] = {std::cos(phi) * T[0] + std::sin(phi) * B[0], std::cos(phi) * T[1] + std::sin(phi) * B[1],
                                  std::cos(phi) * T[2] + std::sin(phi) * B[2]};
#ifdef PREC_ANALYTIC
                // The slice's screen direction as the derivative of the projection along dir.
                // It has the direction of the finite step below (both screen differences carry
                // the same factor 1 / (w (w + e dw))) with no subtraction of nearly equal numbers.
                const D w = -D(P[2]), dw = -D(dir[2]);
                const D vx = (D(dir[0]) * w - D(P[0]) * dw) * D(v.w) / D(v.aspect);
                const D vy = -(D(dir[1]) * w - D(P[1]) * dw) * D(v.h);
                const D ddl = std::sqrt(vx * vx + vy * vy);
                if (ddl < D(1e-12)) continue;
                const C c = C(vx / ddl), sn = C(vy / ddl);
#else
                const D e = D(1e-3) * D(depth);
                D sx1, sy1;
                PREC_NAME(Project)(v, D(P[0]) + D(dir[0]) * e, D(P[1]) + D(dir[1]) * e, D(P[2]) + D(dir[2]) * e, sx1, sy1);
                const D ddl = std::sqrt((sx1 - sx0) * (sx1 - sx0) + (sy1 - sy0) * (sy1 - sy0));
                if (ddl < D(1e-12)) continue;
                const C c = C((sx1 - sx0) / ddl), sn = C((sy1 - sy0) / ddl);
#endif
                const R dv = dir[0] * V[0] + dir[1] * V[1] + dir[2] * V[2];
                const R od[3] = {dir[0] - dv * V[0], dir[1] - dv * V[1], dir[2] - dv * V[2]};
                R ax[3] = {od[1] * V[2] - od[2] * V[1], od[2] * V[0] - od[0] * V[2], od[0] * V[1] - od[1] * V[0]};
                const R al = std::sqrt(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]);
                ax[0] /= al; ax[1] /= al; ax[2] /= al;
                const R na = N[0] * ax[0] + N[1] * ax[1] + N[2] * ax[2];
                const R pn[3] = {N[0] - ax[0] * na, N[1] - ax[1] * na, N[2] - ax[2] * na};
                const R pnl = std::sqrt(pn[0] * pn[0] + pn[1] * pn[1] + pn[2] * pn[2]);
                if (pnl < R(1e-9)) continue;
                const R sgn = (od[0] * pn[0] + od[1] * pn[1] + od[2] * pn[2]) >= 0 ? R(1) : R(-1);
                const R cn = std::clamp((pn[0] * V[0] + pn[1] * V[1] + pn[2] * V[2]) / pnl, R(0), R(1));
                const R nang = sgn * std::acos(cn);
                R hc[2] = {std::cos(nang + kPi / 2), std::cos(nang - kPi / 2)};
                for (int side = 0; side < 2; ++side) {
                    const C sd = side == 0 ? C(1) : C(-1);
                    for (int k = 1; k <= steps; ++k) {
                        const C off = sd * C(rpx) * C(k) / C(steps);
                        const C fx = C(x) + C(0.5) + off * c, fy = C(y) + C(0.5) + off * sn;
                        const int sx = int(std::floor(fx)), sy = int(std::floor(fy));
                        if (!v.valid(sx, sy) || (sx == x && sy == y)) continue;
                        const R dd = R(PREC_NAME(SampleDepth)(v, fx, fy, sx, sy));
                        const R S[3] = {(R(fx) / R(v.w) * R(2) - R(1)) * dd * R(v.tanHalf) * R(v.aspect),
                                        (R(1) - R(fy) / R(v.h) * R(2)) * dd * R(v.tanHalf), -dd};
                        const R d[3] = {S[0] - P[0], S[1] - P[1], S[2] - P[2]};
                        const R dl = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                        if (dl < R(1e-9) || dl > radius) continue;
                        hc[side] = std::max(hc[side], (d[0] * V[0] + d[1] * V[1] + d[2] * V[2]) / dl);
                    }
                }
                const R h1 = nang + std::min(std::acos(std::clamp(hc[0], R(-1), R(1))) - nang, kPi / 2);
                const R h0 = nang + std::max(-std::acos(std::clamp(hc[1], R(-1), R(1))) - nang, -kPi / 2);
                const R sn2 = std::sin(nang);
                const R a0 = (cn + R(2) * h0 * sn2 - std::cos(R(2) * h0 - nang)) / R(4);
                const R a1 = (cn + R(2) * h1 * sn2 - std::cos(R(2) * h1 - nang)) / R(4);
                total += pnl * (a0 + a1);
            }
            out[i] = float(total / R(slices));
        }
    }
}
