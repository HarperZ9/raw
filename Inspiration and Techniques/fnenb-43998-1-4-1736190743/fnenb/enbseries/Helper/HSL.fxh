//HSL by kingeric1992:
//TOD CONSTANT
static const float timeweight() {
    return TimeOfDay1.x +
           TimeOfDay1.y +
           TimeOfDay1.z +
           TimeOfDay1.w +
           TimeOfDay2.x +
           TimeOfDay2.y;
}
        
#define TODIE(a) lerp( TOD(a), a##_Interior, EInteriorFactor )

#define TOD(a) \
  ((TimeOfDay1.x * a##_Dawn    + \
    TimeOfDay1.y * a##_Sunrise + \
    TimeOfDay1.z * a##_Day     + \
    TimeOfDay1.w * a##_Sunset  + \
    TimeOfDay2.x * a##_Dusk    + \
    TimeOfDay2.y * a##_Night) / timeweight())
        
static const float4 node[9]= {
    float4(TODIE(HUEcolor0), 0.0),//red
    float4(TODIE(HUEcolor1), 30.0),
    float4(TODIE(HUEcolor2), 60.0),
    float4(TODIE(HUEcolor3), 120),
    float4(TODIE(HUEcolor4), 180),
    float4(TODIE(HUEcolor5), 240),
    float4(TODIE(HUEcolor6), 270),
    float4(TODIE(HUEcolor7), 300),
    float4(TODIE(HUEcolor0), 360)//red
};

//FUNCTIONS

float3 RGB_to_HSL(float3 color) {
    float3 HSL = 0.0f;
    float  M   = max(color.r, max(color.g, color.b));
    float  C   = M - min(color.r, min(color.g, color.b));
    HSL.z = M - 0.5 * C;

    if (C != 0.0f) {
        float3 Delta = (color.brg - color.rgb) / C + float3(2.0f, 4.0f, 6.0f);
        Delta *= step(M, color.gbr); //if max = rgb
        HSL.x = frac(max(Delta.r, max(Delta.g, Delta.b)) / 6.0);
        HSL.y = (HSL.z == 1)? 0.0: C/ (1 - abs( 2 * HSL.z - 1));
        }

        return HSL;
}

float3 Hue_to_RGB( float h) {
    return saturate(float3( abs(h * 6.0f - 3.0f) - 1.0f,
                            2.0f - abs(h * 6.0f - 2.0f),
                            2.0f - abs(h * 6.0f - 4.0f)));
}

float3 HSL_to_RGB( float3 HSL ) {
    return (Hue_to_RGB(HSL.x) - 0.5) * (1.0 - abs(2.0 * HSL.z - 1)) * HSL.y + HSL.z;
}

float LoC( float L0, float L1, float angle) {
    return sqrt(L0*L0+L1*L1-2.0*L0*L1*cos(angle));
}

static const float HSL_Threshold_Base  = 0.05;
static const float HSL_Threshold_Curve = 1.0;

float3 HSLShift(float3 color) {
    float3 hsl = RGB_to_HSL(color);

    float base;
    for(int i=0; i<9; i++) if(node[i].a < hsl.r*360.0 )base = i;

    float w = saturate((hsl.r*360.0-node[base].a)/(node[base+1].a-node[base].a));

    float3 H0 = RGB_to_HSL(node[base].rgb);
    float3 H1 = RGB_to_HSL(node[base+1].rgb);

    H1.x += (H1.x < H0.x)? 1.0:0.0;

    float3 shift = frac(lerp( H0, H1 , w));
            w = max( hsl.g, 0.0)*max( 1.0-hsl.b, 0.0);
            shift.b = (shift.b - 0.5)*(pow(w, HSL_Threshold_Curve)*(1.0-HSL_Threshold_Base)+HSL_Threshold_Base)*2.0; //[-1.0, 1.0]

    return HSL_to_RGB(saturate(float3(shift.r, hsl.g*(shift.g*2.0), hsl.b*(1.0+shift.b))));
}

//PIXEL SHADER: color = float4(HSLShift(color.rgb), 1.0);

float4	PS_HSLShift(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target {
    return float4(HSLShift(TextureColor.Sample(Sampler1, txcoord).rgb), 1.0);
}
