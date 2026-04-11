/*
    Curl-Flow Impressionism
    A Novel Painterly Stylization Shader

    Hybrid curl-noise + structure-tensor flow field
    with curved Line Integral Convolution.

    Near edges:  strokes follow image structure (preserves form)
    In flat areas: strokes follow procedural curl noise (organic gestures)
    Blend driven by local anisotropy of the structure tensor.

    Setup:
      1. Go to shadertoy.com, create New shader
      2. Click iChannel0 at bottom -> pick any image/video/webcam
      3. Paste this code, hit Compile (Alt+Enter)

    by Zain Dana Harper, 2026
*/

// === TWEAK THESE AND RECOMPILE ===
#define STROKE_LEN    16.0  // Brush stroke length in pixels. 8=short, 24=long flowing
#define STEP_SIZE     1.5   // Pixels per trace step. 1.0=smooth, 2.0=fast
#define CURL_MIX      0.35  // Curl noise vs edge flow. 0=edges only, 1=pure curl swirls
#define CURL_SCALE    4.0   // Curl noise frequency. Higher=smaller swirl patterns
#define CURL_ANIMATE  0.06  // Swirl animation speed. 0=frozen still painting
#define BRUSH_GRAIN   0.25  // Visible brush mark texture. 0=smooth, 0.5=chunky
#define EDGE_INK      0.3   // Dark edge outlines. 0=none, 0.5=strong ink
#define SAT_BOOST     1.12  // Color saturation. 1.0=original, 1.3=vivid
#define TENSOR_RADIUS 1     // Structure tensor blur. 1=fast(3x3), 2=quality(5x5)


// === CORE UTILITIES ===

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

vec2 hash22(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy) * 2.0 - 1.0;
}

float hash21(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}


// === CURL NOISE ===
// Divergence-free procedural vector field.
// Creates organic, swirling flow patterns for flat image regions
// where the image gradient provides no directional information.

float gNoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(dot(hash22(i),             f),
                   dot(hash22(i + vec2(1,0)), f - vec2(1,0)), u.x),
               mix(dot(hash22(i + vec2(0,1)), f - vec2(0,1)),
                   dot(hash22(i + vec2(1,1)), f - vec2(1,1)), u.x), u.y);
}

vec2 curlField(vec2 p) {
    const float e = 0.01;
    float n = gNoise(p), nx = gNoise(p + vec2(e, 0)), ny = gNoise(p + vec2(0, e));
    return vec2(ny - n, -(nx - n)) / e;
}

vec2 fbmCurl(vec2 p, float t) {
    vec2 c = vec2(0);
    float amp = 1.0, freq = 1.0;
    for (int i = 0; i < 3; i++) {
        c += curlField(p * freq + vec2(float(i) * 7.3, float(i) * 3.7) + t) * amp;
        amp *= 0.5;
        freq *= 2.0;
    }
    return c;
}


// === STRUCTURE TENSOR ===
// Analyzes local image orientation via smoothed gradient outer products.
// Returns the edge-tangent direction and anisotropy (edge strength).

vec2 imgGrad(vec2 uv, vec2 px) {
    float l = luma(textureLod(iChannel0, uv - vec2(px.x, 0), 0.0).rgb);
    float r = luma(textureLod(iChannel0, uv + vec2(px.x, 0), 0.0).rgb);
    float u = luma(textureLod(iChannel0, uv - vec2(0, px.y), 0.0).rgb);
    float d = luma(textureLod(iChannel0, uv + vec2(0, px.y), 0.0).rgb);
    return vec2(r - l, d - u);
}

void tensorAnalysis(vec2 uv, vec2 px, out vec2 tangent, out float aniso) {
    // Accumulate structure tensor over neighborhood
    vec3 st = vec3(0);
    float count = 0.0;
    for (int y = -TENSOR_RADIUS; y <= TENSOR_RADIUS; y++) {
        for (int x = -TENSOR_RADIUS; x <= TENSOR_RADIUS; x++) {
            vec2 g = imgGrad(uv + vec2(float(x), float(y)) * px * 1.5, px);
            st += vec3(g.x * g.x, g.y * g.y, g.x * g.y);
            count += 1.0;
        }
    }
    st /= count;

    // Eigenvalue decomposition -> principal direction + anisotropy
    float tr = st.x + st.y;
    float det = st.x * st.y - st.z * st.z;
    float disc = sqrt(max(tr * tr * 0.25 - det, 0.0));
    float l1 = tr * 0.5 + disc;
    float l2 = tr * 0.5 - disc;

    aniso = (l1 + l2 > 1e-5) ? (l1 - l2) / (l1 + l2) : 0.0;

    float angle = 0.5 * atan(2.0 * st.z, st.x - st.y);
    tangent = vec2(cos(angle), sin(angle));
}


// === MAIN ===

void mainImage(out vec4 O, in vec2 FC) {
    vec2 uv = FC / iResolution.xy;
    vec2 px = 1.0 / iResolution.xy;
    float asp = iResolution.x / iResolution.y;

    // ---- 1. Compute hybrid flow direction ----

    // Structure tensor: where are edges and which way do they run?
    vec2 edgeTan;
    float aniso;
    tensorAnalysis(uv, px, edgeTan, aniso);

    // Curl noise: organic swirl field for flat regions
    vec2 curl = normalize(fbmCurl(uv * vec2(asp, 1.0) * CURL_SCALE,
                                   iTime * CURL_ANIMATE) + 1e-8);
    // Orient curl consistently with edge tangent
    if (dot(curl, edgeTan) < 0.0) curl = -curl;

    // THE KEY BLEND: high anisotropy (edges) -> follow structure
    //                low anisotropy (flat)   -> follow curl noise
    float edgeness = smoothstep(0.05, 0.5, aniso);
    vec2 flow = normalize(mix(curl, edgeTan, edgeness) + 1e-8);

    // Adaptive stroke length: long flowing strokes in flat areas,
    // short precise strokes near edges
    float sLen = STROKE_LEN * mix(1.8, 0.5, edgeness);
    int halfN = clamp(int(sLen / STEP_SIZE), 3, 24);
    float sigma = sLen * 0.35;


    // ---- 2. Curved Line Integral Convolution ----
    // Trace a streamline through the hybrid flow field,
    // averaging color along the path = directional brush stroke.

    vec3 cSum = textureLod(iChannel0, uv, 0.0).rgb;
    float wSum = 1.0;

    // Forward and backward traces from center pixel
    for (int sign = 0; sign < 2; sign++)
    {
        vec2 pos = uv;
        vec2 dir = (sign == 0) ? flow : -flow;

        for (int i = 1; i <= 24; i++)
        {
            if (i > halfN) break;

            // Step along flow
            pos += dir * px * STEP_SIZE;

            // Bounds check
            if (any(lessThan(pos, vec2(0))) || any(greaterThan(pos, vec2(1)))) break;

            // Sample scene color at this point along the stroke
            vec3 c = textureLod(iChannel0, pos, 0.0).rgb;

            // Gaussian falloff + brush texture modulation
            float fi = float(i);
            float gW = exp(-fi * fi / (2.0 * sigma * sigma));
            float bW = 1.0 - BRUSH_GRAIN + BRUSH_GRAIN * hash21(pos * 743.31);

            cSum += c * gW * bW;
            wSum += gW * bW;

            // Re-query local image gradient for CURVED strokes
            // (this is what makes strokes bend around contours)
            vec2 g = imgGrad(pos, px);
            vec2 lt = vec2(-g.y, g.x); // tangent = perpendicular to gradient
            if (dot(lt, dir) < 0.0) lt = -lt; // consistent orientation

            // Smooth direction update: 70% momentum + 30% local
            // In flat areas (g~0), dir stays unchanged -> curl noise dominates
            // Near edges, local tangent takes over -> follows contours
            dir = normalize(dir * 0.7 + lt * 0.3 + 1e-8);
        }
    }

    vec3 col = cSum / wSum;


    // ---- 3. Post-processing ----

    // Saturation boost (paintings feel more vivid than photos)
    float l = luma(col);
    col = max(vec3(0), mix(vec3(l), col, SAT_BOOST));

    // Edge darkening (subtle ink outlines for structural definition)
    float gMag = length(imgGrad(uv, px));
    col *= 1.0 - smoothstep(0.03, 0.15, gMag) * EDGE_INK;

    // Subtle pigment color variation (simulates mixed paint on palette)
    float shift = (hash21(floor(FC * 0.4)) - 0.5) * 0.015;
    col.r += shift;
    col.b -= shift;

    O = vec4(col, 1.0);
}
