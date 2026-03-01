
//LUT Texture size (width, height)

//#ifndef LUT_size
#define LUT_size float2(256, 16)
//#endif



texture BackBufferTex : COLOR;
sampler BackBuffer { Texture = BackBufferTex; };

uniform bool Use_Global_LUT < ui_label = "Use Global LUT"; ui_category = "LUT"; > = false;
uniform float LUT_GLOBAL_Night < ui_category = "LUT"; ui_label = "GLOBAL Night"; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; ui_tooltip = ""; > = 1.0;
uniform float LUT_GLOBAL_Sunset < ui_category = "LUT"; ui_label = "GLOBAL Sunset"; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; ui_tooltip = ""; > = 1.0;
uniform float LUT_GLOBAL_Day < ui_category = "LUT"; ui_label = "GLOBAL Day"; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; ui_tooltip = ""; > = 1.0;
uniform float LUT_Extrasunny_Night < ui_category = "LUT"; ui_label = "Extrasunny Night"; ui_spacing = 1; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Extrasunny_Sunset < ui_category = "LUT"; ui_label = "Extrasunny Sunset"; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Extrasunny_Day < ui_category = "LUT"; ui_label = "Extrasunny Day"; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Clear_Night < ui_category = "LUT"; ui_label = "Clear Night"; ui_spacing = 1; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Clear_Sunset < ui_category = "LUT"; ui_label = "Clear Sunset"; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Clear_Day < ui_category = "LUT"; ui_label = "Clear Day"; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Clearing_Night < ui_category = "LUT"; ui_label = "Clearing Night"; ui_spacing = 1; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Clearing_Sunset < ui_category = "LUT"; ui_label = "Clearing Sunset"; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Clearing_Day < ui_category = "LUT"; ui_label = "Clearing Day"; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Clouds_Night < ui_category = "LUT"; ui_label = "Clouds Night"; ui_spacing = 1; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Clouds_Sunset < ui_category = "LUT"; ui_label = "Clouds Sunset"; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Clouds_Day < ui_category = "LUT"; ui_label = "Clouds Day"; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Overcast_Night < ui_category = "LUT"; ui_label = "Overcast Night"; ui_spacing = 1; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Overcast_Sunset < ui_category = "LUT"; ui_label = "Overcast Sunset"; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Overcast_Day < ui_category = "LUT"; ui_label = "Overcast Day"; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Smog_Night < ui_category = "LUT"; ui_label = "Smog Night"; ui_spacing = 1; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Smog_Sunset < ui_category = "LUT"; ui_label = "Smog Sunset"; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Smog_Day < ui_category = "LUT"; ui_label = "Smog Day"; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Foggy_Night < ui_category = "LUT"; ui_label = "Foggy Night"; ui_spacing = 1; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Foggy_Sunset < ui_category = "LUT"; ui_label = "Foggy Sunset"; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Foggy_Day < ui_category = "LUT"; ui_label = "Foggy Day"; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Rain_Night < ui_category = "LUT"; ui_label = "Rain Night"; ui_spacing = 1; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Rain_Sunset < ui_category = "LUT"; ui_label = "Rain Sunset"; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Rain_Day < ui_category = "LUT"; ui_label = "Rain Day"; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Thunder_Night < ui_category = "LUT"; ui_label = "Thunder Night"; ui_spacing = 1; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Thunder_Sunset < ui_category = "LUT"; ui_label = "Thunder Sunset"; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Thunder_Day < ui_category = "LUT"; ui_label = "Thunder Day"; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Blizzard_Night < ui_category = "LUT"; ui_label = "Blizzard Night"; ui_spacing = 1; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Blizzard_Sunset < ui_category = "LUT"; ui_label = "Blizzard Sunset"; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Blizzard_Day < ui_category = "LUT"; ui_label = "Blizzard Day"; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Neutral_Night < ui_category = "LUT"; ui_label = "Neutral Night"; ui_spacing = 1; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Neutral_Sunset < ui_category = "LUT"; ui_label = "Neutral Sunset"; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Neutral_Day < ui_category = "LUT"; ui_label = "Neutral Day"; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Snow_Night < ui_category = "LUT"; ui_label = "Snow Night"; ui_spacing = 1; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Snow_Sunset < ui_category = "LUT"; ui_label = "Snow Sunset"; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Snow_Day < ui_category = "LUT"; ui_label = "Snow Day"; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Snowlight_Night < ui_category = "LUT"; ui_label = "Snowlight Night"; ui_spacing = 1; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Snowlight_Sunset < ui_category = "LUT"; ui_label = "Snowlight Sunset"; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Snowlight_Day < ui_category = "LUT"; ui_label = "Snowlight Day"; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Xmas_Night < ui_category = "LUT"; ui_label = "Xmas Night"; ui_spacing = 1; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Xmas_Sunset < ui_category = "LUT"; ui_label = "Xmas Sunset"; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Xmas_Day < ui_category = "LUT"; ui_label = "Xmas Day"; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Halloween_Night < ui_category = "LUT"; ui_label = "Halloween Night"; ui_spacing = 1; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Halloween_Sunset < ui_category = "LUT"; ui_label = "Halloween Sunset"; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform float LUT_Halloween_Day < ui_category = "LUT"; ui_label = "Halloween Day"; ui_type = "slider"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.005; > = 1.0;
uniform bool sep < ui_label = " "; ui_spacing = 6; noedit = true; >;
texture lut_global_night < source = "QuantV/LUT/global_night.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_global_sunset < source = "QuantV/LUT/global_sunset.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_global_day < source = "QuantV/LUT/global_day.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_extrasunny_night < source = "QuantV/LUT/w_extrasunny_night.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_extrasunny_sunset < source = "QuantV/LUT/w_extrasunny_sunset.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_extrasunny_day < source = "QuantV/LUT/w_extrasunny_day.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_clear_night < source = "QuantV/LUT/w_clear_night.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_clear_sunset < source = "QuantV/LUT/w_clear_sunset.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_clear_day < source = "QuantV/LUT/w_clear_day.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_clearing_night < source = "QuantV/LUT/w_clearing_night.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_clearing_sunset < source = "QuantV/LUT/w_clearing_sunset.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_clearing_day < source = "QuantV/LUT/w_clearing_day.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_clouds_night < source = "QuantV/LUT/w_clouds_night.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_clouds_sunset < source = "QuantV/LUT/w_clouds_sunset.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_clouds_day < source = "QuantV/LUT/w_clouds_day.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_overcast_night < source = "QuantV/LUT/w_overcast_night.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_overcast_sunset < source = "QuantV/LUT/w_overcast_sunset.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_overcast_day < source = "QuantV/LUT/w_overcast_day.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_smog_night < source = "QuantV/LUT/w_smog_night.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_smog_sunset < source = "QuantV/LUT/w_smog_sunset.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_smog_day < source = "QuantV/LUT/w_smog_day.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_foggy_night < source = "QuantV/LUT/w_foggy_night.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_foggy_sunset < source = "QuantV/LUT/w_foggy_sunset.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_foggy_day < source = "QuantV/LUT/w_foggy_day.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_rain_night < source = "QuantV/LUT/w_rain_night.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_rain_sunset < source = "QuantV/LUT/w_rain_sunset.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_rain_day < source = "QuantV/LUT/w_rain_day.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_thunder_night < source = "QuantV/LUT/w_thunder_night.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_thunder_sunset < source = "QuantV/LUT/w_thunder_sunset.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_thunder_day < source = "QuantV/LUT/w_thunder_day.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_blizzard_night < source = "QuantV/LUT/w_blizzard_night.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_blizzard_sunset < source = "QuantV/LUT/w_blizzard_sunset.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_blizzard_day < source = "QuantV/LUT/w_blizzard_day.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_neutral_night < source = "QuantV/LUT/w_neutral_night.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_neutral_sunset < source = "QuantV/LUT/w_neutral_sunset.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_neutral_day < source = "QuantV/LUT/w_neutral_day.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_snow_night < source = "QuantV/LUT/w_snow_night.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_snow_sunset < source = "QuantV/LUT/w_snow_sunset.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_snow_day < source = "QuantV/LUT/w_snow_day.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_snowl_night < source = "QuantV/LUT/w_snowlight_night.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_snowl_sunset < source = "QuantV/LUT/w_snowlight_sunset.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_snowl_day < source = "QuantV/LUT/w_snowlight_day.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_xmas_night < source = "QuantV/LUT/w_xmas_night.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_xmas_sunset < source = "QuantV/LUT/w_xmas_sunset.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_xmas_day < source = "QuantV/LUT/w_xmas_day.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_hallo_night < source = "QuantV/LUT/w_hallo_night.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_hallo_sunset < source = "QuantV/LUT/w_hallo_sunset.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_hallo_day < source = "QuantV/LUT/w_hallo_day.png"; > { Width = LUT_size.x; Height = LUT_size.y;};
texture lut_final { Width = LUT_size.x; Height = LUT_size.y;};
sampler lut_global_night_s{ Texture = lut_global_night; };
sampler lut_global_sunset_s{ Texture = lut_global_sunset; };
sampler lut_global_day_s{ Texture = lut_global_day; };
sampler lut_extrasunny_night_s{ Texture = lut_extrasunny_night; };
sampler lut_extrasunny_sunset_s{ Texture = lut_extrasunny_sunset; };
sampler lut_extrasunny_day_s{ Texture = lut_extrasunny_day; };
sampler lut_clear_night_s{ Texture = lut_clear_night; };
sampler lut_clear_sunset_s{ Texture = lut_clear_sunset; };
sampler lut_clear_day_s{ Texture = lut_clear_day; };
sampler lut_clearing_night_s{ Texture = lut_clearing_night; };
sampler lut_clearing_sunset_s{ Texture = lut_clearing_sunset; };
sampler lut_clearing_day_s{ Texture = lut_clearing_day; };
sampler lut_clouds_night_s{ Texture = lut_clouds_night; };
sampler lut_clouds_sunset_s{ Texture = lut_clouds_sunset; };
sampler lut_clouds_day_s{ Texture = lut_clouds_day; };
sampler lut_overcast_night_s{ Texture = lut_overcast_night; };
sampler lut_overcast_sunset_s{ Texture = lut_overcast_sunset; };
sampler lut_overcast_day_s{ Texture = lut_overcast_day; };
sampler lut_smog_night_s{ Texture = lut_smog_night; };
sampler lut_smog_sunset_s{ Texture = lut_smog_sunset; };
sampler lut_smog_day_s{ Texture = lut_smog_day; };
sampler lut_foggy_night_s{ Texture = lut_foggy_night; };
sampler lut_foggy_sunset_s{ Texture = lut_foggy_sunset; };
sampler lut_foggy_day_s{ Texture = lut_foggy_day; };
sampler lut_rain_night_s{ Texture = lut_rain_night; };
sampler lut_rain_sunset_s{ Texture = lut_rain_sunset; };
sampler lut_rain_day_s{ Texture = lut_rain_day; };
sampler lut_thunder_night_s{ Texture = lut_thunder_night; };
sampler lut_thunder_sunset_s{ Texture = lut_thunder_sunset; };
sampler lut_thunder_day_s{ Texture = lut_thunder_day; };
sampler lut_blizzard_night_s{ Texture = lut_blizzard_night; };
sampler lut_blizzard_sunset_s{ Texture = lut_blizzard_sunset; };
sampler lut_blizzard_day_s{ Texture = lut_blizzard_day; };
sampler lut_neutral_night_s{ Texture = lut_neutral_night; };
sampler lut_neutral_sunset_s{ Texture = lut_neutral_sunset; };
sampler lut_neutral_day_s{ Texture = lut_neutral_day; };
sampler lut_snow_night_s{ Texture = lut_snow_night; };
sampler lut_snow_sunset_s{ Texture = lut_snow_sunset; };
sampler lut_snow_day_s{ Texture = lut_snow_day; };
sampler lut_snowl_night_s{ Texture = lut_snowl_night; };
sampler lut_snowl_sunset_s{ Texture = lut_snowl_sunset; };
sampler lut_snowl_day_s{ Texture = lut_snowl_day; };
sampler lut_xmas_night_s{ Texture = lut_xmas_night; };
sampler lut_xmas_sunset_s{ Texture = lut_xmas_sunset; };
sampler lut_xmas_day_s{ Texture = lut_xmas_day; };
sampler lut_hallo_night_s{ Texture = lut_hallo_night; };
sampler lut_hallo_sunset_s{ Texture = lut_hallo_sunset; };
sampler lut_hallo_day_s{ Texture = lut_hallo_day; };
sampler lut_final_s{ Texture = lut_final; };

#define mx lerp(lerp(lerp(lerp(lerp(lerp(lerp(lerp(lerp(1, 2, smoothstep(0.0, 0.1, saturate(tex2Dfetch(BackBuffer, 0).w))), 3, smoothstep(0.1, 0.2, saturate(tex2Dfetch(BackBuffer, 0).w))), 11, smoothstep(0.2, 0.3, saturate(tex2Dfetch(BackBuffer, 0).w))), 13, smoothstep(0.3, 0.4, saturate(tex2Dfetch(BackBuffer, 0).w))), 4, smoothstep(0.4, 0.5, saturate(tex2Dfetch(BackBuffer, 0).w))), 8, smoothstep(0.5, 0.6, saturate(tex2Dfetch(BackBuffer, 0).w))), 9, smoothstep(0.6, 0.7, saturate(tex2Dfetch(BackBuffer, 0).w))), 15, smoothstep(0.7, 0.8, saturate(tex2Dfetch(BackBuffer, 0).w))), 5, smoothstep(0.8, 0.9, saturate(tex2Dfetch(BackBuffer, 0).w)))
#define lut_t(x,y,z) lerp(lerp(lerp(lerp(x, y, smoothstep(4.69, 5.55, saturate(tex2Dfetch(BackBuffer, int2(0, 1)).w) * 23.99999)), z, smoothstep(9.31, 11.10, saturate(tex2Dfetch(BackBuffer, int2(0, 1)).w) * 23.99999)), y, smoothstep(15.81, 17.88, saturate(tex2Dfetch(BackBuffer, int2(0, 1)).w) * 23.99999)), x, smoothstep(19.48, 20.7, saturate(tex2Dfetch(BackBuffer, int2(0, 1)).w) * 23.99999))
void PostProcessVS0(in uint id : SV_VertexID, out float4 position : SV_Position, out float2 texcoord : TEXCOORD, out nointerpolation float2 ww : TEXCOORD1)
{
    texcoord.x = (id == 2) ? 2.0 : 0.0;
    texcoord.y = (id == 1) ? 2.0 : 0.0;
    position = float4(texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    ww.x = mx;
    ww.y = Use_Global_LUT ? lut_t(LUT_GLOBAL_Night, LUT_GLOBAL_Sunset, LUT_GLOBAL_Day).x : 0.0;
}
void PostProcessVS1(in uint id : SV_VertexID, out float4 position : SV_Position, out float2 texcoord : TEXCOORD, out nointerpolation float li : TEXCOORD1)
{
    texcoord.x = (id == 2) ? 2.0 : 0.0;
    texcoord.y = (id == 1) ? 2.0 : 0.0;
    position = float4(texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    
    const float w[16] =
    {
        lut_t(LUT_Extrasunny_Night, LUT_Extrasunny_Sunset, LUT_Extrasunny_Day), //
        lut_t(LUT_Extrasunny_Night, LUT_Extrasunny_Sunset, LUT_Extrasunny_Day),
        lut_t(LUT_Clear_Night, LUT_Clear_Sunset, LUT_Clear_Day),
        lut_t(LUT_Clearing_Night, LUT_Clearing_Sunset, LUT_Clearing_Day),
        lut_t(LUT_Clouds_Night, LUT_Clouds_Sunset, LUT_Clouds_Day),
        lut_t(LUT_Overcast_Night, LUT_Overcast_Sunset, LUT_Overcast_Day),
        lut_t(LUT_Smog_Night, LUT_Smog_Sunset, LUT_Smog_Day),
        lut_t(LUT_Foggy_Night, LUT_Foggy_Sunset, LUT_Foggy_Day),
        lut_t(LUT_Rain_Night, LUT_Rain_Sunset, LUT_Rain_Day),
        lut_t(LUT_Thunder_Night, LUT_Thunder_Sunset, LUT_Thunder_Day),
        lut_t(LUT_Blizzard_Night, LUT_Blizzard_Sunset, LUT_Blizzard_Day),
        lut_t(LUT_Neutral_Night, LUT_Neutral_Sunset, LUT_Neutral_Day),
        lut_t(LUT_Snow_Night, LUT_Snow_Sunset, LUT_Snow_Day),
        lut_t(LUT_Snowlight_Night, LUT_Snowlight_Sunset, LUT_Snowlight_Day),
        lut_t(LUT_Xmas_Night, LUT_Xmas_Sunset, LUT_Xmas_Day),
        lut_t(LUT_Halloween_Night, LUT_Halloween_Sunset, LUT_Halloween_Day)
    };
    li = w[uint(clamp(round(mx), 0, 15))];
}
float4 PS_Lut0(float4 position : SV_Position, float2 texcoord : TEXCOORD, nointerpolation float2 ww : TEXCOORD1) : SV_Target
{
    float3 global_night = tex2Dlod(lut_global_night_s, float4(texcoord, 0.0, 0.0)).xyz;
    float3 global_sunset = tex2Dlod(lut_global_sunset_s, float4(texcoord, 0.0, 0.0)).xyz;
    float3 global_day = tex2Dlod(lut_global_day_s, float4(texcoord, 0.0, 0.0)).xyz;
    float4 res = float4(lut_t(global_night, global_sunset, global_day), 1.0);
    [branch] if (ww.y < 1.0)
    {
        float3 extra_night = tex2Dlod(lut_extrasunny_night_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 extra_sunset = tex2Dlod(lut_extrasunny_sunset_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 extra_day = tex2Dlod(lut_extrasunny_day_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 clear_night = tex2Dlod(lut_clear_night_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 clear_sunset = tex2Dlod(lut_clear_sunset_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 clear_day = tex2Dlod(lut_clear_day_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 clearing_night = tex2Dlod(lut_clearing_night_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 clearing_sunset = tex2Dlod(lut_clearing_sunset_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 clearing_day = tex2Dlod(lut_clearing_day_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 clouds_night = tex2Dlod(lut_clouds_night_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 clouds_sunset = tex2Dlod(lut_clouds_sunset_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 clouds_day = tex2Dlod(lut_clouds_day_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 overcast_night = tex2Dlod(lut_overcast_night_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 overcast_sunset = tex2Dlod(lut_overcast_sunset_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 overcast_day = tex2Dlod(lut_overcast_day_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 smog_night = tex2Dlod(lut_smog_night_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 smog_sunset = tex2Dlod(lut_smog_sunset_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 smog_day = tex2Dlod(lut_smog_day_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 foggy_night = tex2Dlod(lut_foggy_night_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 foggy_sunset = tex2Dlod(lut_foggy_sunset_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 foggy_day = tex2Dlod(lut_foggy_day_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 rain_night = tex2Dlod(lut_rain_night_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 rain_sunset = tex2Dlod(lut_rain_sunset_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 rain_day = tex2Dlod(lut_rain_day_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 thunder_night = tex2Dlod(lut_thunder_night_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 thunder_sunset = tex2Dlod(lut_thunder_sunset_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 thunder_day = tex2Dlod(lut_thunder_day_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 blizzard_night = tex2Dlod(lut_blizzard_night_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 blizzard_sunset = tex2Dlod(lut_blizzard_sunset_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 blizzard_day = tex2Dlod(lut_blizzard_day_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 neutral_night = tex2Dlod(lut_neutral_night_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 neutral_sunset = tex2Dlod(lut_neutral_sunset_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 neutral_day = tex2Dlod(lut_neutral_day_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 snow_night = tex2Dlod(lut_snow_night_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 snow_sunset = tex2Dlod(lut_snow_sunset_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 snow_day = tex2Dlod(lut_snow_day_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 snowl_night = tex2Dlod(lut_snowl_night_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 snowl_sunset = tex2Dlod(lut_snowl_sunset_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 snowl_day = tex2Dlod(lut_snowl_day_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 xmas_night = tex2Dlod(lut_xmas_night_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 xmas_sunset = tex2Dlod(lut_xmas_sunset_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 xmas_day = tex2Dlod(lut_xmas_day_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 hallo_night = tex2Dlod(lut_hallo_night_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 hallo_sunset = tex2Dlod(lut_hallo_sunset_s, float4(texcoord, 0.0, 0.0)).xyz;
        float3 hallo_day = tex2Dlod(lut_hallo_day_s, float4(texcoord, 0.0, 0.0)).xyz;
        const float3 w[16] =
        {
            lut_t(extra_night, extra_sunset, extra_day), //
            lut_t(extra_night, extra_sunset, extra_day),
            lut_t(clear_night, clear_sunset, clear_day),
            lut_t(clearing_night, clearing_sunset, clearing_day),
            lut_t(clouds_night, clouds_sunset, clouds_day),
            lut_t(overcast_night, overcast_sunset, overcast_day),
            lut_t(smog_night, smog_sunset, smog_day),
            lut_t(foggy_night, foggy_sunset, foggy_day),
            lut_t(rain_night, rain_sunset, rain_day),
            lut_t(thunder_night, thunder_sunset, thunder_day),
            lut_t(blizzard_night, blizzard_sunset, blizzard_day),
            lut_t(neutral_night, neutral_sunset, neutral_day),
            lut_t(snow_night, snow_sunset, snow_day),
            lut_t(snowl_night, snowl_sunset, snowl_day),
            lut_t(xmas_night, xmas_sunset, xmas_day),
            lut_t(hallo_night, hallo_sunset, hallo_day)
        };
        res.xyz = lerp(w[uint(clamp(round(ww.x), 0, 15))], res.xyz, ww.y);
    }
    return res;
}
float4 PS_Lut1(float4 position : SV_Position, float2 texcoord : TEXCOORD, nointerpolation float li : TEXCOORD1) : SV_Target
{
    float4 res = tex2Dlod(BackBuffer, float4(texcoord, 0.0, 0.0));
    float2 CLut_pSize = 1.0 / LUT_size;
    float3 color = res.xyz * (LUT_size.y - 1.0);
    float4 CLut_UV;
    CLut_UV.w = floor(color.z);
    CLut_UV.xy = (color.xy + 0.5) * CLut_pSize;
    CLut_UV.x += CLut_UV.w * CLut_pSize.y;
    CLut_UV.z = CLut_UV.x + CLut_pSize.y;
    color = lerp(tex2Dlod(lut_final_s, float4(CLut_UV.xy, 0.0, 0.0)).xyz, tex2Dlod(lut_final_s, float4(CLut_UV.zy, 0.0, 0.0)).xyz, color.z - CLut_UV.w);
    res.xyz = lerp(res.xyz, color, li);
    return res;
}

technique Per_Weather_LUT < ui_tooltip = "Per_Weather_LUT"; >
{
    pass p0
    {
        VertexShader = PostProcessVS0;
        PixelShader = PS_Lut0;
        RenderTarget = lut_final;
    }
    pass p1
    {
        VertexShader = PostProcessVS1;
        PixelShader = PS_Lut1;
    }
}
