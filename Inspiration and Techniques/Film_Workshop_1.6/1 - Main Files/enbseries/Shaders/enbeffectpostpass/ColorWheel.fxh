


struct ColorWheelStruct {
    bool   State;
    float  Weight, lowrange, highrange, overlap;
    float3 shadow, midtone, highlight, lift, gamma, gain, offset;
};

/*
static ColorWheelStruct SplitToneData = {
    true, Split_Weight,
    lowrange, highrange, overlap,
    Shadow, Midtone, Highlight, Lift, Gamma, Gain
};
*/
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//  internals
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

struct ColorWheelWeightStruct {
    float highlight, midtone, shadow, gain, gamma, lift;
};

//return [0,1] if within target range, otherwise return 0
ColorWheelWeightStruct ColorWheelWeight( float lum, float low, float high, float overlap) {
    ColorWheelWeightStruct o;
    float2 t;

    t.x = 1 - lum/(low + overlap);
    o.shadow = step(lum, low + overlap) * log2(t.x * t.x + 1.0);

    t  = float2(lum - low, high - lum) + overlap;
    t  = float2(t.x * t.y, 2.0 / ((high - low) + 2.0 * overlap));
    t *= t;
    o.midtone = step(lum, high + overlap) * step(low - overlap, lum) * log2(t.x * t.y * t.y + 1.0);

    t    = float2(lum, 1.0) - high + overlap;
    t.x /= t.y;
    o.highlight = step(high - overlap, lum) * log2(t.x * t.x + 1.0);

    o.gain  = saturate(lum);
    o.lift  = saturate(1.0 - lum);
    o.gamma = o.gain * o.lift * 4.0;

    return o;
}

float3 ColorWheel(float3 color, ColorWheelStruct col) {
    ColorWheelWeightStruct w = ColorWheelWeight(saturate(dot(color, 0.3333)), col.lowrange, col.highrange, col.overlap);

    color += (col.shadow * w.shadow + col.midtone * w.midtone + col.highlight * w.highlight) * 0.25;
    color += (col.lift   * w.lift   + col.gamma   * w.gamma   + col.gain      * w.gain) * 0.25;

    return color + col.offset * 0.25;
}
