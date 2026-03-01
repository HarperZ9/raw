//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// 3D LUT SHADER WITH MULTI-LUT SUPPORT VIA TEXTURE ATLAS
// DNI SUPPORT LEFT IN FOR POTENTIAL FUTURE USE
// CODE BY KINGERIC1992
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// NEGATIVE STOCK LUT DEFINES
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
Texture2D LutNegDay <string UIName = "LUTNegDay";  string ResourceName = "Textures/Negative/LUTNegativeDay.png"; >;
Texture2D LutNegNight <string UIName = "LUTNegNight";  string ResourceName = "Textures/Negative/LUTNegativeDay.png"; >;
Texture2D LutNegInterior <string UIName = "LUTNegInterior";  string ResourceName = "Textures/Negative/LUTNegativeDay.png"; >;

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// PRINT STOCK LUT DEFINES
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
Texture2D LutPrintDay <string UIName = "LUTPrintDay";  string ResourceName = "Textures/Print/LUTPrintDay.png"; >;
Texture2D LutPrintNight <string UIName = "LUTPrintNight";  string ResourceName = "Textures/Print/LUTPrintDay.png"; >;
Texture2D LutPrintInterior <string UIName = "LUTPrintInterior";  string ResourceName = "Textures/Print/LUTPrintDay.png"; >;

#if(CUSTOM_MODE == 1)
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// CUSTOM LOOK PACK LUT DEFINES
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
Texture2D LookPackLut <string UIName = LOOK_PACK_NAME;  string ResourceName = LOOK_PACK_LUT; >;
#endif

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// USER LUT DEFINES
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
Texture2D Lut1kDay <string UIName = "LUT1kDay";  string ResourceName = "Custom/User/LUT.png"; >;
Texture2D Lut1kNight <string UIName = "LUT1kNight";  string ResourceName = "Custom/User/LUT.png"; >;
Texture2D Lut1kInterior <string UIName = "LUT1kInterior";  string ResourceName = "Custom/User/LUT.png"; >;

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// COLOR GRADING PREP LUT DEFINES
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
Texture2D LutPreGrade <string UIName = "LUTPreGrade";  string ResourceName = "Textures/Conversion/LUTPreGrade.png"; >;
Texture2D LutPostGrade <string UIName = "LUTPostGrade";  string ResourceName = "Textures/Conversion/LUTPostGrade.png"; >;

Texture2D LutRec <string UIName = "LutRec";  string ResourceName = "Textures/Conversion/709.png"; >;


// Define negative LUT count
#define neg_y   0.0238095238095238  // 1 dived by LUT COUNT
// Define Print LUT count
#define prnt_y  0.0666666666666667  // 1 divided by LUT COUNT
// Define Custom LUT count
#define lut1k_y 1                   // 1 divided by LUT COUNT

#if(CUSTOM_MODE == 0)
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// NEGATIVE FILM-STOCK SHADER
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
float3 LUTFuncNeg(float3 inColor)
{
    float2 CLut_pSize = {0.000244140625, 0.015625};
    inColor.rgb       = saturate(inColor.rgb) * 63;

    float3 CLut_UV;
    CLut_UV.z = floor(inColor.z);
    inColor.z  -= CLut_UV.z;
    inColor.xy  = (inColor.xy + 0.5) * CLut_pSize;
    inColor.x  += CLut_UV.z * CLut_pSize.y;
    inColor.y  *= neg_y;

    CLut_UV.x = inColor.x;
    CLut_UV.z = CLut_UV.x + CLut_pSize.y;
    CLut_UV.y = inColor.y + lut_n * neg_y;
    float3 lutcolor_D = lerp(LutNegDay.SampleLevel(Sampler1, CLut_UV.xy, 0).rgb, LutNegDay.SampleLevel(Sampler1, CLut_UV.zy, 0).rgb, inColor.z);
    CLut_UV.y = inColor.y + lut_n * neg_y;
    float3 lutcolor_N = lerp(LutNegNight.SampleLevel(Sampler1, CLut_UV.xy, 0).rgb, LutNegNight.SampleLevel(Sampler1, CLut_UV.zy, 0).rgb, inColor.z);
    CLut_UV.y = inColor.y + lut_n * neg_y;
    float3 lutcolor_I = lerp(LutNegInterior.SampleLevel(Sampler1, CLut_UV.xy, 0).rgb, LutNegInterior.SampleLevel(Sampler1, CLut_UV.zy, 0).rgb, inColor.z);

    inColor.rgb         = lerp(lerp( lutcolor_N, lutcolor_D, ENightDayFactor), lutcolor_I, EInteriorFactor);
  return saturate(inColor.rgb);
}

#if(ACTIVATE_GRADING == 1)
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// PRE-GRADE LUT SHADER (CONVERSION FROM CINEON TO sRGB)
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
float3 LUTFuncPre(float3 inColor)
{
    float2 CLut_pSize = {0.000244140625, 0.015625};
    inColor.rgb       = saturate(inColor.rgb) * 63;

    float3 CLut_UV;
    CLut_UV.z = floor(inColor.z);
    inColor.z  -= CLut_UV.z;
    inColor.xy  = (inColor.xy + 0.5) * CLut_pSize;
    inColor.x  += CLut_UV.z * CLut_pSize.y;
    inColor.y  *= 1;

    CLut_UV.x = inColor.x;
    CLut_UV.z = CLut_UV.x + CLut_pSize.y;
    CLut_UV.y = inColor.y;
    float3 lutcolor_pre = lerp(LutPreGrade.SampleLevel(Sampler1, CLut_UV.xy, 0).rgb, LutPreGrade.SampleLevel(Sampler1, CLut_UV.zy, 0).rgb, inColor.z);

    inColor.rgb         = lutcolor_pre;
  return saturate(inColor.rgb);
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// POST-GRADE LUT SHADER (CONVERSION BACK TO CINEON)
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
float3 LUTFuncPost(float3 inColor)
{
    float2 CLut_pSize = {0.000244140625, 0.015625};
    inColor.rgb       = saturate(inColor.rgb) * 63;

    float3 CLut_UV;
    CLut_UV.z = floor(inColor.z);
    inColor.z  -= CLut_UV.z;
    inColor.xy  = (inColor.xy + 0.5) * CLut_pSize;
    inColor.x  += CLut_UV.z * CLut_pSize.y;
    inColor.y  *= 1;

    CLut_UV.x = inColor.x;
    CLut_UV.z = CLut_UV.x + CLut_pSize.y;
    CLut_UV.y = inColor.y;
    float3 lutcolor_post = lerp(LutPostGrade.SampleLevel(Sampler1, CLut_UV.xy, 0).rgb, LutPostGrade.SampleLevel(Sampler1, CLut_UV.zy, 0).rgb, inColor.z);

    inColor.rgb         = lutcolor_post;
  return saturate(inColor.rgb);
}
#endif

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// PRINT FILM-STOCK SHADER
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
float3 LUTFuncPrint(float3 inColor)
{
    float2 CLut_pSize = {0.000244140625, 0.015625};
    inColor.rgb       = saturate(inColor.rgb) * 63;

    float3 CLut_UV;
    CLut_UV.z = floor(inColor.z);
    inColor.z  -= CLut_UV.z;
    inColor.xy  = (inColor.xy + 0.5) * CLut_pSize;
    inColor.x  += CLut_UV.z * CLut_pSize.y;
    inColor.y  *= prnt_y;

    CLut_UV.x = inColor.x;
    CLut_UV.z = CLut_UV.x + CLut_pSize.y;
    CLut_UV.y = inColor.y + lut_p * prnt_y;
    float3 lutcolor_D = lerp(LutPrintDay.SampleLevel(Sampler1, CLut_UV.xy, 0).rgb, LutPrintDay.SampleLevel(Sampler1, CLut_UV.zy, 0).rgb, inColor.z);
    CLut_UV.y = inColor.y + lut_p * prnt_y;
    float3 lutcolor_N = lerp(LutPrintNight.SampleLevel(Sampler1, CLut_UV.xy, 0).rgb, LutPrintNight.SampleLevel(Sampler1, CLut_UV.zy, 0).rgb, inColor.z);
    CLut_UV.y = inColor.y + lut_p * prnt_y;
    float3 lutcolor_I = lerp(LutPrintInterior.SampleLevel(Sampler1, CLut_UV.xy, 0).rgb, LutPrintInterior.SampleLevel(Sampler1, CLut_UV.zy, 0).rgb, inColor.z);

    inColor.rgb         = lerp(lerp( lutcolor_N, lutcolor_D, ENightDayFactor), lutcolor_I, EInteriorFactor);
  return saturate(inColor.rgb);
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Filmic Range Conversion (CONVERSION TO Rec.709)
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
float3 LUTFuncRec(float3 inColor)
{
    float2 CLut_pSize = {0.000244140625, 0.015625};
    inColor.rgb       = saturate(inColor.rgb) * 63;

    float3 CLut_UV;
    CLut_UV.z = floor(inColor.z);
    inColor.z  -= CLut_UV.z;
    inColor.xy  = (inColor.xy + 0.5) * CLut_pSize;
    inColor.x  += CLut_UV.z * CLut_pSize.y;
    inColor.y  *= 1;

    CLut_UV.x = inColor.x;
    CLut_UV.z = CLut_UV.x + CLut_pSize.y;
    CLut_UV.y = inColor.y;
    float3 lutcolor_rec = lerp(LutRec.SampleLevel(Sampler1, CLut_UV.xy, 0).rgb, LutRec.SampleLevel(Sampler1, CLut_UV.zy, 0).rgb, inColor.z);

    inColor.rgb         = lutcolor_rec;
  return saturate(inColor.rgb);
}
#endif

#define lut_c 0
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// CUSTOM LUT SHADER
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
float3 LUTFunc1k(float3 inColor)
{
    float2 CLut_pSize = {0.0009765625, 0.03125};
    inColor.rgb       = saturate(inColor.rgb) * 31;

    float3 CLut_UV;
    CLut_UV.z = floor(inColor.z);
    inColor.z  -= CLut_UV.z;
    inColor.xy  = (inColor.xy + 0.5) * CLut_pSize;
    inColor.x  += CLut_UV.z * CLut_pSize.y;
    inColor.y  *= lut1k_y;

    CLut_UV.x = inColor.x;
    CLut_UV.z = CLut_UV.x + CLut_pSize.y;
    CLut_UV.y = inColor.y + lut_c * lut1k_y;
    float3 lutcolor_D = lerp(Lut1kDay.SampleLevel(Sampler1, CLut_UV.xy, 0).rgb, Lut1kDay.SampleLevel(Sampler1, CLut_UV.zy, 0).rgb, inColor.z);
    CLut_UV.y = inColor.y + lut_c * lut1k_y;
    float3 lutcolor_N = lerp(Lut1kNight.SampleLevel(Sampler1, CLut_UV.xy, 0).rgb, Lut1kNight.SampleLevel(Sampler1, CLut_UV.zy, 0).rgb, inColor.z);
    CLut_UV.y = inColor.y + lut_c * lut1k_y;
    float3 lutcolor_I = lerp(Lut1kInterior.SampleLevel(Sampler1, CLut_UV.xy, 0).rgb, Lut1kInterior.SampleLevel(Sampler1, CLut_UV.zy, 0).rgb, inColor.z);

    inColor.rgb         = lerp(lerp( lutcolor_N, lutcolor_D, ENightDayFactor), lutcolor_I, EInteriorFactor);
  return saturate(inColor.rgb);
}

#if(CUSTOM_MODE == 1)
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// CUSTOM LOOK PACK SHADER
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
float3 LUTFuncLookPack(float3 inColor)
{
    float2 CLut_pSize = {LP_LUTWIDTH, LP_LUTHEIGHT};
    inColor.rgb       = saturate(inColor.rgb) * (LP_LUTVSIZE - 1);

    float3 CLut_UV;
    CLut_UV.z = floor(inColor.z);
    inColor.z  -= CLut_UV.z;
    inColor.xy  = (inColor.xy + 0.5) * CLut_pSize;
    inColor.x  += CLut_UV.z * CLut_pSize.y;
    inColor.y  *= LP_VCOUNTPIXEL;

    CLut_UV.x = inColor.x;
    CLut_UV.z = CLut_UV.x + CLut_pSize.y;
    CLut_UV.y = inColor.y + LutLookPack * LP_VCOUNTPIXEL;
    float3 lutcolor_lp = lerp(LookPackLut.SampleLevel(Sampler1, CLut_UV.xy, 0).rgb, LookPackLut.SampleLevel(Sampler1, CLut_UV.zy, 0).rgb, inColor.z);

    inColor.rgb         = lutcolor_lp;
  return saturate(inColor.rgb);
}
#endif
