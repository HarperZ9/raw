//HSL with Widget by kingeric1992
//June.11.2017

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

static const float4 node[9]= {
    float4(HUEcolor0, 0.0),//red
    float4(HUEcolor1, 30.0),
    float4(HUEcolor2, 60.0),
    float4(HUEcolor3, 120),
    float4(HUEcolor4, 180),
    float4(HUEcolor5, 240),
    float4(HUEcolor6, 270),
    float4(HUEcolor7, 300),
    float4(HUEcolor0, 360)//red
   };

float3 RGB_to_HSL(float3 color) {
    float3 HSL   = 0.0f;
    float  M     = max(color.r, max(color.g, color.b));
    float  C     = M - min(color.r, min(color.g, color.b));
           HSL.z = M - 0.5 * C;
    if (C != 0.0f)
    {
        float3 Delta  = (color.brg - color.rgb) / C + float3(2.0f, 4.0f, 6.0f);
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

float LoC( float L0, float L1, float angle){
    return sqrt(L0*L0+L1*L1-2.0*L0*L1*cos(angle));
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

static const float HSL_Threshold_Base  = 0.05;
static const float HSL_Threshold_Curve = 1.0;

float3 HSLShift(float3 color)
{
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

float2 DrawLines(float3 coord, float3 Wheel_L, float3 Wheel_S, bool type)
{
    float4 r    = { Wheel_S.x, Wheel_L.x, Wheel_S.y, Wheel_L.z };
    float2 dist = step(abs(sin((coord.x-r.xy)*6.2831852)),0.01/r.zw)*step(frac(r.zw * 10.0), 0.5);


    if(abs(coord.x - 0.5) < 0.2){
        dist *= step(0.25, r.xy)*step(r.xy, 0.75);
    }
    else if(coord.x < 0.5){
        dist *= 1.0 - step(coord.x+0.25, r.xy)*step(r.xy, coord.x+0.75);
    }
    else {
        dist *= 1.0-step(coord.x-0.75, r.xy)*step(r.xy, coord.x-0.25);
    }

    dist *= type?         step(r.zw, 0.75):
           (coord.yz>0.5? step(r.zw, coord.yz)*step(0.5, r.zw):
                          step(coord.yz, r.zw)*step(r.zw, 0.5));
    return dist;
}


//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void VS_Widget(inout float4 pos     : SV_POSITION,
               inout float2 txcoord : TEXCOORD0,
               out   float4 uv      : TEXCOORD1 ) {
    pos.xy = pos.xy/3.0;
    pos.y *= ScreenSize.z*0.5;
    pos.x += 2.0/3.0;
    pos.y -= 2.0/3.0;
    pos.w  = 1.0;

    uv.xy = ( txcoord.xy - float2(0.25, 0.5)) * 4.0;
    uv.zw = ( txcoord.xy - float2(0.75, 0.5)) * 4.0;
    uv.yw*= 0.5;
}

Texture2D HSLBG <string UIName = "HSLBG";  string ResourceName = "Textures/Scopes/HSLBG.png"; >;

float4 PS_Widget(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0, float4 uv : TEXCOORD1) : SV_Target
{
    if(txcoord.x > 1.0 || txcoord.y > 1.0 || !Enable_HSLwidget) discard;

    float2 dist = 0.0;
    float4 Wheel_S = { atan2(uv.x, uv.y)/6.2831852+0.5, length(uv.xy), 0.5, 1.0 };
    float4 Wheel_L = { atan2(uv.z, uv.w)/6.2831852+0.5, 0.5, length(uv.zw), 1.0 };
    float4 Node_S  = 0.0;
    float4 Node_L  = 0.0;
    float4 Line_S  = 0.0;
    float4 Line_L  = 0.0;

    float4 background = HSLBG.Sample(Sampler1, txcoord);

    static const float3 nodecolor[8]={
        {1.0, 0.0, 0.0},
        {1.0, 0.5, 0.0},
        {1.0, 1.0, 0.0},
        {0.0, 1.0, 0.0},
        {0.0, 1.0, 1.0},
        {0.0, 0.0, 1.0},
        {0.5, 0.0, 1.0},
        {1.0, 0.0, 1.0}
    };

    for(int i=0; i<8; i++){
        float3 coord = RGB_to_HSL(node[i].rgb);
        dist = float2(LoC(Wheel_S.y, coord.y, abs(coord.x-Wheel_S.x)*6.2831852),
                      LoC(Wheel_L.z, coord.z, abs(coord.x-Wheel_L.x)*6.2831852));

        Node_S = max(Node_S, float4(nodecolor[i]*step(dist.x,0.04), 1.0)*step(dist.x,0.05));
        Node_L = max(Node_L, float4(nodecolor[i]*step(dist.y,0.04), 1.0)*step(dist.y,0.05));

        dist = DrawLines(coord, Wheel_L.rgb, Wheel_S.rgb, false);

        Line_S = max(Line_S, float4(0.0, 1.0, 0.0, 1.0)*dist.x);
        Line_L = max(Line_L, float4(0.0, 1.0, 0.0, 1.0)*dist.y);
    }

    //shows src hue
    {
        dist   = DrawLines(RGB_to_HSL(TextureColor.Sample(Sampler1, tempInfo2.zw).rgb),
                                Wheel_L.rgb, Wheel_S.rgb, true);
        Line_S = max(Line_S, float4(0.0, 1.0, 0.0, 1.0)*dist.x);
        Line_L = max(Line_L, float4(0.0, 1.0, 0.0, 1.0)*dist.y);
    }

    //show res hue
    {
        dist   = DrawLines(RGB_to_HSL(HSLShift(TextureColor.Sample(Sampler1, tempInfo2.zw).rgb)),
                            Wheel_L.rgb, Wheel_S.rgb, true);
        Line_S = max(Line_S, float4(1.0, 0.5, 0.0, 1.0)*dist.x);
        Line_L = max(Line_L, float4(1.0, 0.5, 0.0, 1.0)*dist.y);
    }

    //max length @ 0.5
    Wheel_S = float4(HSL_to_RGB(Wheel_S.rgb), 1.0) * step(Wheel_S.y, 0.5);
    Wheel_L = float4(HSL_to_RGB(Wheel_L.rgb), 1.0) * step(Wheel_L.z, 0.5);


    float4 tool = lerp(lerp(Wheel_S, Node_S, Node_S.a), Line_S, Line_S.a) +
                  lerp(lerp(Wheel_L, Node_L, Node_L.a), Line_L, Line_L.a);

    return lerp(background, tool, tool.a);
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

float4	PS_HSLShift(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    return float4(HSLShift(TextureColor.Sample(Sampler1, txcoord).rgb), 1.0);
}
