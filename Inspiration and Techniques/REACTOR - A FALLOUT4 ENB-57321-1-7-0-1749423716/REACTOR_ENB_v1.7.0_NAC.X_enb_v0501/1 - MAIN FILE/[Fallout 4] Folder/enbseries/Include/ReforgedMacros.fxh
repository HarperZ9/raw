// ----------------------------------------------------------------------------------------------------------
// REFORGED INCLUDE FILE

// Permission to use, copy, modify, and/or distribute this software for any purpose with or without fee is
// hereby granted.

// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE
// INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE
// FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
// OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
// OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
// ----------------------------------------------------------------------------------------------------------



#ifndef REFORGED_MACROS_H
#define REFORGED_MACROS_H



// ----------------------------------------------------------------------------------------------------------
// GENERIC MACROS
// ----------------------------------------------------------------------------------------------------------
#define TO_STRING(x) #x
#define MERGE(a, b) a##b
#define COMBINE(a, b) a##_##b
#define UI_CATEGORY undefined



// ----------------------------------------------------------------------------------------------------------
// WHITESPACE COLLECTION (my pride and joy)
// ----------------------------------------------------------------------------------------------------------
#define WHITESPACE_1  " "
#define WHITESPACE_2  "  "
#define WHITESPACE_3  "   "
#define WHITESPACE_4  "    "
#define WHITESPACE_5  "     "
#define WHITESPACE_6  "      "
#define WHITESPACE_7  "       "
#define WHITESPACE_8  "        "
#define WHITESPACE_9  "         "
#define WHITESPACE_10 "          "
#define WHITESPACE_11 "           "
#define WHITESPACE_12 "            "
#define WHITESPACE_13 "             "
#define WHITESPACE_14 "              "
#define WHITESPACE_15 "               "
#define WHITESPACE_16 "                "
#define WHITESPACE_17 "                 "
#define WHITESPACE_18 "                  "
#define WHITESPACE_19 "                   "
#define WHITESPACE_20 "                    "
#define WHITESPACE_21 "                     "
#define WHITESPACE_22 "                      "
#define WHITESPACE_23 "                       "
#define WHITESPACE_24 "                        "
#define WHITESPACE_25 "                         "
#define WHITESPACE_26 "                          "
#define WHITESPACE_27 "                           "
#define WHITESPACE_28 "                            "
#define WHITESPACE_29 "                             "
#define WHITESPACE_30 "                              "
#define WHITESPACE_31 "                               "
#define WHITESPACE_32 "                                "



// ----------------------------------------------------------------------------------------------------------
// TEXTURES
// ----------------------------------------------------------------------------------------------------------
#if REFORGED_HLSL_3
    #define TEXTURE_PATH(name, path, filter, uv, srgb) \
        texture2D tex##name < string ResourceName = path; >; \
        sampler2D Sampler##name = sampler_state \
        { \
            Texture     = <tex##name>; \
            MinFilter   = filter; \
            MagFilter   = filter; \
            MipFilter   = NONE; \
            AddressU    = uv; \
            AddressV    = uv; \
            SRGBTexture = srgb; \
        };


    #define TEXTURE_UNIFORM(name, filter, uv, srgb) \
        texture2D tex##name; \
        sampler2D Sampler##name = sampler_state \
        { \
            Texture     = <tex##name>; \
            MinFilter   = filter; \
            MagFilter   = filter; \
            MipFilter   = NONE; \
            AddressU    = uv; \
            AddressV    = uv; \
            SRGBTexture = srgb; \
        };


    #define TEXTURE_ENBEFFECT(name, filter, uv, srgb) \
        texture2D texs##name; \
        sampler2D _s##name = sampler_state \
        { \
            Texture     = <texs##name>; \
            MinFilter   = filter; \
            MagFilter   = filter; \
            MipFilter   = NONE; \
            AddressU    = uv; \
            AddressV    = uv; \
            SRGBTexture = srgb; \
        };
#endif



// ----------------------------------------------------------------------------------------------------------
// TECHNIQUES
// ----------------------------------------------------------------------------------------------------------
#if REFORGED_HLSL_5
    #define TECHNIQUE(name, vs, ps) \
        technique11 name \
        { \
            pass p0 \
            { \
                SetVertexShader(CompileShader(vs_5_0, vs)); \
                SetPixelShader(CompileShader(ps_5_0, ps)); \
            } \
        }

    #define TECHNIQUE_TARGETED(name, target, vs, ps) \
        technique11 name < string RenderTarget = TO_STRING(target); > \
        { \
            pass p0 \
            { \
                SetVertexShader(CompileShader(vs_5_0, vs)); \
                SetPixelShader(CompileShader(ps_5_0, ps)); \
            } \
        }

    #define TECHNIQUE_NAMED(name, uiName, vs, ps) \
        technique11 name < string UIName = uiName; > \
        { \
            pass p0 \
            { \
                SetVertexShader(CompileShader(vs_5_0, vs)); \
                SetPixelShader(CompileShader(ps_5_0, ps)); \
            } \
        }

    #define TECHNIQUE_NAMED_TARGETED(name, uiName, target, vs, ps) \
        technique11 name < string UIName = uiName; string RenderTarget = TO_STRING(target); > \
        { \
            pass p0 \
            { \
                SetVertexShader(CompileShader(vs_5_0, vs)); \
                SetPixelShader(CompileShader(ps_5_0, ps)); \
            } \
        }
#endif



// ----------------------------------------------------------------------------------------------------------
// EXPANDERS
// ----------------------------------------------------------------------------------------------------------
#define LERP_DN(var) var = lerp(var##Night, var##Day, ENightDayFactor);
#define LERP_DNI(var) var = (EInteriorFactor == 1.0 ? var##Interior : lerp(var##Night, var##Day, ENightDayFactor));
#define SELECT_EI(var) var = (EInteriorFactor == 1.0 ? var##Interior : var##Exterior);
#define SELECT_DNI(var) var = (EInteriorFactor == 1.0 ? var##Interior : (ENightDayFactor > 0.5 ? var##Day : var##Night));
#define LERP_TODI(var) var = \
    EInteriorFactor == 1.0 ? var##Interior : \
        var##Dawn    * TimeOfDay1.x + \
        var##Sunrise * TimeOfDay1.y + \
        var##Day     * TimeOfDay1.z + \
        var##Sunset  * TimeOfDay1.w + \
        var##Dusk    * TimeOfDay2.x + \
        var##Night   * TimeOfDay2.y;



// ----------------------------------------------------------------------------------------------------------
// PARAMETERS
// ----------------------------------------------------------------------------------------------------------
#define UI_SEPARATOR int MERGE(UI_CATEGORY, _SEPARATOR) \
< \
    string UIName = MERGE(MERGE("\xAB\xAB\xAB ", TO_STRING(UI_CATEGORY)), " \xBB\xBB\xBB"); \
    int UIMin = 0; \
    int UIMax = 0; \
> = { 0 };


#define UI_SEPARATOR_CUSTOM(msg) int MERGE(UI_CATEGORY, _SEPARATOR) \
< \
    string UIName = MERGE(MERGE("\xAB\xAB\xAB ", msg), " \xBB\xBB\xBB"); \
    int UIMin = 0; \
    int UIMax = 0; \
> = { 0 };


#define UI_MESSAGE(var, str) int var \
< \
    string UIName = str; \
    int UIMin = 0; \
    int UIMax = 0; \
> = { 0 };


#define UI_WHITESPACE(num) UI_MESSAGE(Whitespace##num, WHITESPACE_##num)



// ----------------------------------------------------------------------------------------------------------
// ARCHETYPES (makes the process easier for me)
// ----------------------------------------------------------------------------------------------------------
#define __EI_1ARG(macro, var, name, arg1) \
    UI_##macro(var##Exterior, name##" (Exterior)", arg1) \
    UI_##macro(var##Interior, name##" (Interior)", arg1)


#define __EI_3ARG(macro, var, name, arg1, arg2, arg3) \
    UI_##macro(var##Exterior, name##" (Exterior)", arg1, arg2, arg3) \
    UI_##macro(var##Interior, name##" (Interior)", arg1, arg2, arg3)


#define __EI_4ARG(macro, var, name, arg1, arg2, arg3, arg4) \
    UI_##macro(var##Exterior, name##" (Exterior)", arg1, arg2, arg3, arg4) \
    UI_##macro(var##Interior, name##" (Interior)", arg1, arg2, arg3, arg4)


#define __DNI_1ARG(macro, var, name, arg1) \
    UI_##macro(var##Day, name##" (Day)", arg1) \
    UI_##macro(var##Night, name##" (Night)", arg1) \
    UI_##macro(var##Interior, name##" (Interior)", arg1)


#define __DNI_3ARG(macro, var, name, arg1, arg2, arg3) \
    UI_##macro(var##Day, name##" (Day)", arg1, arg2, arg3) \
    UI_##macro(var##Night, name##" (Night)", arg1, arg2, arg3) \
    UI_##macro(var##Interior, name##" (Interior)", arg1, arg2, arg3)


#define __DNI_4ARG(macro, var, name, arg1, arg2, arg3, arg4) \
    UI_##macro(var##Day, name##" (Day)", arg1, arg2, arg3, arg4) \
    UI_##macro(var##Night, name##" (Night)", arg1, arg2, arg3, arg4) \
    UI_##macro(var##Interior, name##" (Interior)", arg1, arg2, arg3, arg4)


#define __TODI_1ARG(macro, var, name, arg1) \
    UI_##macro(var##Dawn, name##" (Dawn)", arg1) \
    UI_##macro(var##Sunrise, name##" (Sunrise)", arg1) \
    UI_##macro(var##Day, name##" (Day)", arg1) \
    UI_##macro(var##Sunset, name##" (Sunset)", arg1) \
    UI_##macro(var##Dusk, name##" (Dusk)", arg1) \
    UI_##macro(var##Night, name##" (Night)", arg1) \
    UI_##macro(var##Interior, name##" (Interior)", arg1)


#define __TODI_3ARG(macro, var, name, arg1, arg2, arg3) \
    UI_##macro(var##Dawn, name##" (Dawn)", arg1, arg2, arg3) \
    UI_##macro(var##Sunrise, name##" (Sunrise)", arg1, arg2, arg3) \
    UI_##macro(var##Day, name##" (Day)", arg1, arg2, arg3) \
    UI_##macro(var##Sunset, name##" (Sunset)", arg1, arg2, arg3) \
    UI_##macro(var##Dusk, name##" (Dusk)", arg1, arg2, arg3) \
    UI_##macro(var##Night, name##" (Night)", arg1, arg2, arg3) \
    UI_##macro(var##Interior, name##" (Interior)", arg1, arg2, arg3)


#define __TODI_4ARG(macro, var, name, arg1, arg2, arg3, arg4) \
    UI_##macro(var##Dawn, name##" (Dawn)", arg1, arg2, arg3, arg4) \
    UI_##macro(var##Sunrise, name##" (Sunrise)", arg1, arg2, arg3, arg4) \
    UI_##macro(var##Day, name##" (Day)", arg1, arg2, arg3, arg4) \
    UI_##macro(var##Sunset, name##" (Sunset)", arg1, arg2, arg3, arg4) \
    UI_##macro(var##Dusk, name##" (Dusk)", arg1, arg2, arg3, arg4) \
    UI_##macro(var##Night, name##" (Night)", arg1, arg2, arg3, arg4) \
    UI_##macro(var##Interior, name##" (Interior)", arg1, arg2, arg3, arg4)



// ----------------------------------------------------------------------------------------------------------
// BOOL
// ----------------------------------------------------------------------------------------------------------
#define UI_BOOL(var, name, def) bool var < string UIName = MERGE(MERGE(TO_STRING(UI_CATEGORY), ": "), name); > = {def};
#define UI_BOOL_SINGLE UI_BOOL


#define UI_BOOL_EI(var, name, def) \
    __EI_1ARG(BOOL, var, name, def) \
    static const bool SELECT_EI(var)


#define UI_BOOL_DNI(var, name, def) \
    __DNI_1ARG(BOOL, var, name, def) \
    static const bool SELECT_DNI(var)


#define UI_BOOL_TODI(var, name, def) \
    __TODI_1ARG(BOOL, var, name, def) \
    static const bool LERP_TODI(var)


#define UI_BOOL_MULTIVAR(paramtype, var, name, def) \
    UI_BOOL_##paramtype(var, name, def)



// ----------------------------------------------------------------------------------------------------------
// QUALITY
// ----------------------------------------------------------------------------------------------------------
#define UI_QUALITY(var, name, minval, maxval, defval) \
    int var \
    < \
        string UIName = name; \
        string UIWidget = "quality"; \
        int UIMin = minval; \
        int UIMax = maxval; \
    > = {defval};

#define UI_QUALITY_SINGLE UI_QUALITY


#define UI_QUALITY_EI(var, name, minval, maxval, defval) \
    __EI_3ARG(QUALITY, var, name, minval, maxval, defval) \
    static const int SELECT_EI(var)


#define UI_QUALITY_DNI(var, name, minval, maxval, defval) \
    __DNI_3ARG(QUALITY, var, name, minval, maxval, defval) \
    static const int SELECT_DNI(var)


#define UI_QUALITY_TODI(var, name, minval, maxval, defval) \
    __TODI_3ARG(QUALITY, var, name, minval, maxval, defval) \
    static const int LERP_TODI(var)



// ----------------------------------------------------------------------------------------------------------
// INT
// ----------------------------------------------------------------------------------------------------------
#define UI_INT(var, name, minval, maxval, defval) \
    int var \
    < \
        string UIName = MERGE(MERGE(TO_STRING(UI_CATEGORY), ": "), name); \
        string UIWidget = "Spinner"; \
        int UIMin = minval; \
        int UIMax = maxval; \
    > = {defval};

#define UI_INT_SINGLE UI_INT


#define UI_INT_EI(var, name, minval, maxval, defval) \
    __EI_3ARG(INT, var, name, minval, maxval, defval) \
    static const int SELECT_EI(var)


#define UI_INT_DNI(var, name, minval, maxval, defval) \
    __DNI_3ARG(INT, var, name, minval, maxval, defval) \
    static const int SELECT_DNI(var)


#define UI_INT_TODI(var, name, minval, maxval, defval) \
    __TODI_3ARG(INT, var, name, minval, maxval, defval) \
    static const int LERP_TODI(var)


#define UI_INT_MULTIVAR(paramtype, var, name, minval, maxval, defval) \
    UI_INT_##paramtype(var, name, minval, maxval, defval)



// ----------------------------------------------------------------------------------------------------------
// FLOAT
// ----------------------------------------------------------------------------------------------------------
#define UI_FLOAT(var, name, minval, maxval, defval) \
    float var \
    < \
        string UIName = MERGE(MERGE(TO_STRING(UI_CATEGORY), ": "), name); \
        string UIWidget = "Spinner"; \
        float UIMin = minval; \
        float UIMax = maxval; \
    > = {defval};

#define UI_FLOAT_SINGLE UI_FLOAT


#define UI_FLOAT_FINE(var, name, minval, maxval, defval, step) \
    float var \
    < \
        string UIName = MERGE(MERGE(TO_STRING(UI_CATEGORY), ": "), name); \
        string UIWidget = "Spinner"; \
        float UIMin = minval; \
        float UIMax = maxval; \
        float UIStep = step; \
    > = {defval};

#define UI_FLOAT_FINE_SINGLE UI_FLOAT_FINE


#define UI_FLOAT_EI(var, name, minval, maxval, defval) \
    __EI_3ARG(FLOAT, var, name, minval, maxval, defval) \
    static const float SELECT_EI(var)


#define UI_FLOAT_DNI(var, name, minval, maxval, defval) \
    __DNI_3ARG(FLOAT, var, name, minval, maxval, defval) \
    static const float LERP_DNI(var)


#define UI_FLOAT_TODI(var, name, minval, maxval, defval) \
    __TODI_3ARG(FLOAT, var, name, minval, maxval, defval) \
    static const float LERP_TODI(var)


#define UI_FLOAT_FINE_EI(var, name, minval, maxval, defval, step) \
    __EI_4ARG(FLOAT_FINE, var, name, minval, maxval, defval, step) \
    static const float SELECT_EI(var)


#define UI_FLOAT_FINE_DNI(var, name, minval, maxval, defval, step) \
    __DNI_4ARG(FLOAT_FINE, var, name, minval, maxval, defval, step) \
    static const float LERP_DNI(var)


#define UI_FLOAT_FINE_TODI(var, name, minval, maxval, defval, step) \
    __TODI_4ARG(FLOAT_FINE, var, name, minval, maxval, defval, step) \
    static const float LERP_TODI(var)


#define UI_FLOAT_MULTIVAR(paramtype, var, name, minval, maxval, defval) \
    UI_FLOAT_##paramtype(var, name, minval, maxval, defval)


#define UI_FLOAT_FINE_MULTIVAR(paramtype, var, name, minval, maxval, defval) \
    UI_FLOAT_FINE_##paramtype(var, name, minval, maxval, defval)



// ----------------------------------------------------------------------------------------------------------
// FLOAT3
// ----------------------------------------------------------------------------------------------------------
#define UI_FLOAT3(var, name, defval1, defval2, defval3) \
    float3 var \
    < \
        string UIName = MERGE(MERGE(TO_STRING(UI_CATEGORY), ": "), name); \
        string UIWidget = "Color"; \
    > = {defval1, defval2, defval3};

#define UI_FLOAT3_SINGLE UI_FLOAT3


#define UI_FLOAT3_EI(var, name, minval, maxval, defval) \
    __EI_3ARG(FLOAT3, var, name, minval, maxval, defval) \
    static const float3 SELECT_EI(var)


#define UI_FLOAT3_DNI(var, name, minval, maxval, defval) \
    __DNI_3ARG(FLOAT3, var, name, minval, maxval, defval) \
    static const float3 LERP_DNI(var)


#define UI_FLOAT3_TODI(var, name, minval, maxval, defval) \
    __TODI_3ARG(FLOAT3, var, name, minval, maxval, defval) \
    static const float3 LERP_TODI(var)


#define UI_FLOAT3_MULTIVAR(paramtype, var, name, defval1, defval2, defval3) \
    UI_FLOAT3_##paramtype(var, name, defval1, defval2, defval3)


// ----------------------------------------------------------------------------------------------------------
// FLOAT4
// ----------------------------------------------------------------------------------------------------------
#define UI_FLOAT4(var, name, def1, def2, def3, def4) \
    float4 var  \
    < \
        string UIName = name; \
        string UIWidget = "vector"; \
    > = {def1, def2, def3, def4};

#define UI_FLOAT4_SINGLE UI_FLOAT4


#define UI_FLOAT4_EI(var, name, def1, def2, def3, def4) \
    __EI_4ARG(FLOAT4, var, name, def1, def2, def3, def4) \
    static const float4 SELECT_EI(var)


#define UI_FLOAT4_DNI(var, name, def1, def2, def3, def4) \
    __DNI_4ARG(FLOAT4, var, name, def1, def2, def3, def4) \
    static const float4 LERP_DNI(var)


#define UI_FLOAT4_TODI(var, name, def1, def2, def3, def4) \
    __TODI_4ARG(FLOAT4, var, name, def1, def2, def3, def4) \
    static const float4 LERP_TODI(var)



#endif // REFORGED_MACROS_H