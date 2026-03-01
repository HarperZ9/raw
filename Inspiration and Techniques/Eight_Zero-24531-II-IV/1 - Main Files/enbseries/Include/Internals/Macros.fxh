//////////////////////////////////////////////////
// ENB KITCHEN UI MACROS                        //
//                                              //
// INSPIRED BY THE SANDVICH MAKER'S REFORGED UI //
// AUTHOR: TREYM                                //
//////////////////////////////////////////////////

// MESSAGE MACRO /////////////////////////////////
    #define UI_MSG(x, label) \
    int msg##x < \
        string UIName   = label; \
        int UIMin       = 0; \
        int UIMax       = 0; \
    > = { 0 };

// BOOLEAN MACRO /////////////////////////////////
    #define UI_BOOL(var, label, defval) \
    bool var < \
        string UIName   = label; \
    > = {defval};

// INTEGER MACROS ////////////////////////////////
    #define UI_INT(var, label, minval, maxval, defval) \
    int	var < \
        string UIName   = label; \
        string UIWidget = "spinner"; \
        int UIMin       = minval; \
        int UIMax       = maxval; \
    > = {defval};

    #define UI_INT4(var, label, minval, maxval, val1, val2, val3, val4) \
    int4 var < \
        string UIName   = label; \
        string UIWidget = "spinner"; \
        int UIMin       = minval; \
        int UIMax       = maxval; \
    > = {val1, val2, val3, val4};

    #define UI_QUALITY(var, label, minval, maxval, defval) \
    int var < \
        string UIName   = label; \
        string UIWidget = "Quality"; \
        int UIMin       = minval; \
        int UIMax       = maxval; \
    > = {defval};

// FLOAT MACROS //////////////////////////////////
    #define UI_FLOAT(var, label, minval, maxval, defval) \
    float var < \
    	string UIName   = label; \
    	string UIWidget = "spinner"; \
    	float UIMin     = minval; \
    	float UIMax     = maxval; \
    > = {defval};

    #define UI_FLOAT_FINE(var, label, minval, maxval, precision, defval) \
    float var < \
    	string UIName   = label; \
    	string UIWidget = "spinner"; \
    	float UIMin     = minval; \
    	float UIMax     = maxval; \
        float UIStep    = precision; \
    > = {defval};

// FLOAT3 MACRO //////////////////////////////////
    #define UI_FLOAT3(var, label, val1, val2, val3) \
    float3 var < \
        string UIName   = label; \
        string UIWidget = "spinner"; \
    > = {val1, val2, val3};

// COLOR MACRO ///////////////////////////////////
    #define UI_COLOR(var, label, val1, val2, val3) \
    float3	var < \
    	string UIName   = label; \
    	string UIWidget = "color"; \
    > = {val1, val2, val3};

// FLOAT4 MACRO //////////////////////////////////
    #define UI_FLOAT4(var, label, val1, val2, val3, val4) \
    float4 var < \
    	string UIName   = label; \
    	string UIWidget = "spinner"; \
    > = {val1, val2, val3, val4};

// VECTOR MACRO //////////////////////////////////
    #define UI_VECTOR(var, label, val1, val2, val3, val4) \
    float4 var < \
    	string UIName   = label; \
    	string UIWidget = "vector"; \
    > = {val1, val2, val3, val4};

// SAMPLER MACROS ////////////////////////////////
    #define SAMPLER(name, filter, uv) \
    SamplerState name { \
    	Filter = MIN_MAG_MIP_##filter; \
        AddressU = uv; \
    	AddressV = uv; \
    };

// TEXTURE MACRO /////////////////////////////////
    #define TEXTURE(name, path) \
    Texture2D name < \
        string ResourceName = path; \
    >;

// DIVIDERS //////////////////////////////////////
    #define UI_DIVIDER(x) \
    int divider##x < \
        string UIName   = DIVIDER_##x; \
        int UIMin       = 0; \
        int UIMax       = 0; \
    > = { 0 };

    #define DIVIDER_1   "----------------------------------------------------------"
    #define DIVIDER_2   "---------------------------------------------------------- "
    #define DIVIDER_3   "----------------------------------------------------------  "
    #define DIVIDER_4   "----------------------------------------------------------   "
    #define DIVIDER_5   "----------------------------------------------------------    "
    #define DIVIDER_6   "----------------------------------------------------------     "
    #define DIVIDER_7   "----------------------------------------------------------      "
    #define DIVIDER_8   "----------------------------------------------------------       "
    #define DIVIDER_9   "----------------------------------------------------------        "
    #define DIVIDER_10  "----------------------------------------------------------         "
    #define DIVIDER_11  "----------------------------------------------------------          "
    #define DIVIDER_12  "----------------------------------------------------------           "
    #define DIVIDER_13  "----------------------------------------------------------            "
    #define DIVIDER_14  "----------------------------------------------------------             "
    #define DIVIDER_15  "----------------------------------------------------------              "
    #define DIVIDER_16  "----------------------------------------------------------               "
    #define DIVIDER_17  "----------------------------------------------------------                "
    #define DIVIDER_18  "----------------------------------------------------------                 "
    #define DIVIDER_19  "----------------------------------------------------------                  "
    #define DIVIDER_20  "----------------------------------------------------------                   "
    #define DIVIDER_21  "----------------------------------------------------------                    "
    #define DIVIDER_22  "----------------------------------------------------------                     "
    #define DIVIDER_23  "----------------------------------------------------------                      "
    #define DIVIDER_24  "----------------------------------------------------------                       "
    #define DIVIDER_25  "----------------------------------------------------------                        "
    #define DIVIDER_26  "----------------------------------------------------------                         "
    #define DIVIDER_27  "----------------------------------------------------------                          "
    #define DIVIDER_28  "----------------------------------------------------------                           "
    #define DIVIDER_29  "----------------------------------------------------------                            "
    #define DIVIDER_30  "----------------------------------------------------------                             "
    #define DIVIDER_31  "----------------------------------------------------------                              "
    #define DIVIDER_32  "----------------------------------------------------------                               "
    #define DIVIDER_33  "----------------------------------------------------------                                "
    #define DIVIDER_34  "----------------------------------------------------------                                 "
    #define DIVIDER_35  "----------------------------------------------------------                                  "
    #define DIVIDER_36  "----------------------------------------------------------                                   "
    #define DIVIDER_37  "----------------------------------------------------------                                    "
    #define DIVIDER_38  "----------------------------------------------------------                                     "
    #define DIVIDER_39  "----------------------------------------------------------                                      "
    #define DIVIDER_40  "----------------------------------------------------------                                       "
    #define DIVIDER_41  "----------------------------------------------------------                                        "
    #define DIVIDER_42  "----------------------------------------------------------                                         "
    #define DIVIDER_43  "----------------------------------------------------------                                          "
    #define DIVIDER_44  "----------------------------------------------------------                                           "
    #define DIVIDER_45  "----------------------------------------------------------                                            "
    #define DIVIDER_46  "----------------------------------------------------------                                             "
    #define DIVIDER_47  "----------------------------------------------------------                                              "
    #define DIVIDER_48  "----------------------------------------------------------                                               "
    #define DIVIDER_49  "----------------------------------------------------------                                                "
    #define DIVIDER_50  "----------------------------------------------------------                                                 "
    #define DIVIDER_51  "----------------------------------------------------------                                                  "
    #define DIVIDER_52  "----------------------------------------------------------                                                   "
    #define DIVIDER_53  "----------------------------------------------------------                                                    "
    #define DIVIDER_54  "----------------------------------------------------------                                                     "
    #define DIVIDER_55  "----------------------------------------------------------                                                      "
    #define DIVIDER_56  "----------------------------------------------------------                                                       "
    #define DIVIDER_57  "----------------------------------------------------------                                                        "
    #define DIVIDER_58  "----------------------------------------------------------                                                         "
    #define DIVIDER_59  "----------------------------------------------------------                                                          "
    #define DIVIDER_60  "----------------------------------------------------------                                                           "
    #define DIVIDER_61  "----------------------------------------------------------                                                            "
    #define DIVIDER_62  "----------------------------------------------------------                                                             "
    #define DIVIDER_63  "----------------------------------------------------------                                                              "
    #define DIVIDER_64  "----------------------------------------------------------                                                               "
    #define DIVIDER_65  "----------------------------------------------------------                                                                "
    #define DIVIDER_66  "----------------------------------------------------------                                                                 "
    #define DIVIDER_67  "----------------------------------------------------------                                                                  "
    #define DIVIDER_68  "----------------------------------------------------------                                                                   "
    #define DIVIDER_69  "----------------------------------------------------------                                                                    "
    #define DIVIDER_70  "----------------------------------------------------------                                                                     "
    #define DIVIDER_71  "----------------------------------------------------------                                                                      "
    #define DIVIDER_72  "----------------------------------------------------------                                                                       "
    #define DIVIDER_73  "----------------------------------------------------------                                                                        "
    #define DIVIDER_74  "----------------------------------------------------------                                                                         "
    #define DIVIDER_75  "----------------------------------------------------------                                                                          "

// BLANK SPACE ///////////////////////////////////
    #define UI_BLANK(x) \
    int blank##x < \
        string UIName = BLANKSPACE_##x; \
        int UIMin = 0; \
        int UIMax = 0; \
    > = { 0 };

    #define BLANKSPACE_1   " "
    #define BLANKSPACE_2   "  "
    #define BLANKSPACE_3   "   "
    #define BLANKSPACE_4   "    "
    #define BLANKSPACE_5   "     "
    #define BLANKSPACE_6   "      "
    #define BLANKSPACE_7   "       "
    #define BLANKSPACE_8   "        "
    #define BLANKSPACE_9   "         "
    #define BLANKSPACE_10  "          "
    #define BLANKSPACE_11  "           "
    #define BLANKSPACE_12  "            "
    #define BLANKSPACE_13  "             "
    #define BLANKSPACE_14  "              "
    #define BLANKSPACE_15  "               "
    #define BLANKSPACE_16  "                "
    #define BLANKSPACE_17  "                 "
    #define BLANKSPACE_18  "                  "
    #define BLANKSPACE_19  "                   "
    #define BLANKSPACE_20  "                    "
    #define BLANKSPACE_21  "                     "
    #define BLANKSPACE_22  "                      "
    #define BLANKSPACE_23  "                       "
    #define BLANKSPACE_24  "                        "
    #define BLANKSPACE_25  "                         "
    #define BLANKSPACE_26  "                          "
    #define BLANKSPACE_27  "                           "
    #define BLANKSPACE_28  "                            "
    #define BLANKSPACE_29  "                             "
    #define BLANKSPACE_30  "                              "
    #define BLANKSPACE_31  "                               "
    #define BLANKSPACE_32  "                                "
    #define BLANKSPACE_33  "                                 "
    #define BLANKSPACE_34  "                                  "
    #define BLANKSPACE_35  "                                   "
    #define BLANKSPACE_36  "                                    "
    #define BLANKSPACE_37  "                                     "
    #define BLANKSPACE_38  "                                      "
    #define BLANKSPACE_39  "                                       "
    #define BLANKSPACE_40  "                                        "
    #define BLANKSPACE_41  "                                         "
    #define BLANKSPACE_42  "                                          "
    #define BLANKSPACE_43  "                                           "
    #define BLANKSPACE_44  "                                            "
    #define BLANKSPACE_45  "                                             "
    #define BLANKSPACE_46  "                                              "
    #define BLANKSPACE_47  "                                               "
    #define BLANKSPACE_48  "                                                "
    #define BLANKSPACE_49  "                                                 "
    #define BLANKSPACE_50  "                                                  "
    #define BLANKSPACE_51  "                                                   "
    #define BLANKSPACE_52  "                                                    "
    #define BLANKSPACE_53  "                                                     "
    #define BLANKSPACE_54  "                                                      "
    #define BLANKSPACE_55  "                                                       "
    #define BLANKSPACE_56  "                                                        "
    #define BLANKSPACE_57  "                                                         "
    #define BLANKSPACE_58  "                                                          "
    #define BLANKSPACE_59  "                                                           "
    #define BLANKSPACE_60  "                                                            "
    #define BLANKSPACE_61  "                                                             "
    #define BLANKSPACE_62  "                                                              "
    #define BLANKSPACE_63  "                                                               "
    #define BLANKSPACE_64  "                                                                "
    #define BLANKSPACE_65  "                                                                 "
    #define BLANKSPACE_66  "                                                                  "
    #define BLANKSPACE_67  "                                                                   "
    #define BLANKSPACE_68  "                                                                    "
    #define BLANKSPACE_69  "                                                                     "
    #define BLANKSPACE_70  "                                                                      "
    #define BLANKSPACE_71  "                                                                       "
    #define BLANKSPACE_72  "                                                                        "
    #define BLANKSPACE_73  "                                                                         "
    #define BLANKSPACE_74  "                                                                          "
    #define BLANKSPACE_75  "                                                                           "

    // Standard Technique with inital UI String
    #define TECHNIQUE_UI(tname, uiname, pass) \
    technique11 tname < string UIName = uiname; > { \
      pass \
    }

    // Technique with RenderTarget assignment and UI String
    #define TECHNIQUE_UI_RT(tname, uiname, rtarget, pass) \
    technique11 tname < string UIName = uiname; string RenderTarget = rtarget; > { \
      pass \
    }

    // Standard Technique
    #define TECHNIQUE(tname, pass) \
    technique11 tname { \
      pass \
    }

    // Technique with RenderTarget assignment
    #define TECHNIQUE_RT(tname, rtarget, pass) \
    technique11 tname < string RenderTarget = rtarget; > { \
      pass \
    }

    // Technique Pass to be placed within TECHNIQUE macro
    #define PASS(pname, vs, ps) \
    pass pname { \
      SetVertexShader(CompileShader(vs_5_0, vs())); \
      SetPixelShader(CompileShader(ps_5_0, ps())); \
    }

    // Technique Pass to be placed within TECHNIQUE macro
    #define PASS_ARGS_PS(pname, vs, ps, args) \
    pass pname { \
      SetVertexShader(CompileShader(vs_5_0, vs())); \
      SetPixelShader(CompileShader(ps_5_0, ps##args)); \
    }

    // Technique Pass to be placed within TECHNIQUE macro
    #define PASS_ARGS_VS(pname, vs, args, ps) \
    pass pname { \
      SetVertexShader(CompileShader(vs_5_0, vs##args)); \
      SetPixelShader(CompileShader(ps_5_0, ps())); \
    }

    // Technique Pass to be placed within TECHNIQUE macro
    #define PASS_ARGS_ALL(pname, vs, args1, ps, args2) \
    pass pname { \
      SetVertexShader(CompileShader(vs_5_0, vs##args1)); \
      SetPixelShader(CompileShader(ps_5_0, ps##args2)); \
    }
