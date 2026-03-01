//═════════════════════════════════════════════════════════════════════════════
//
//  enbUI_Primer.fxh v2.0 — Universal UI Macro System for ENB Shaders
//
//  Builds upon and improves:
//    • ReforgedUI whitespace system by TheSandvichMaker
//    • UI element patterns from kingeric1992, Adyss, TreyM, l00ping
//    • Silent Horizons UI architecture by Zain Dana Harper
//
//  Features beyond ReforgedUI:
//    • Branded file headers with version stamps
//    • Collapsible section headers with separator styles
//    • Sub-section indentation hierarchy (├── └── ├─)
//    • Read-only monitor displays for external data
//    • Status indicators (●/○/▶/■ encoded as extended ASCII)
//    • Category separators (thin ─, thick ═, dotted ·)
//    • Grid-aligned parameter names (auto-pad to column)
//    • Unique variable name generation (no conflicts across includes)
//    • SkyrimBridge monitor panel macros
//
//  Usage:
//    #include "Helper/enbUI_Primer.fxh"     // Must be first UI include
//    UI_FileHeader("My Shader Name")
//    UI_Section("Bloom Controls")
//    float myParam < ... >;
//    UI_Separator()
//    UI_Section("Color Grading")
//
//═════════════════════════════════════════════════════════════════════════════

#ifndef _UI_PRIMER_
#define _UI_PRIMER_

//─────────────────────────────────────────────────────────────────────────────
//  INTERNAL: Token pasting and stringification
//─────────────────────────────────────────────────────────────────────────────

#define _UI_PASTE2(a, b)   a##b
#define _UI_PASTE(a, b)    _UI_PASTE2(a, b)
#define _UI_STR2(x)        #x
#define _UI_STR(x)         _UI_STR2(x)

// Generate unique variable names using __LINE__ to avoid redefinition
// across multiple includes. Each macro call gets a unique identifier.
#define _UI_UID(prefix)    _UI_PASTE(prefix, __LINE__)


//─────────────────────────────────────────────────────────────────────────────
//  CORE: Dummy integer parameters for labels/separators
//
//  ENB's GUI renders any annotated global variable. We use int variables
//  with UIMin=UIMax=0 to create non-interactive label rows.
//  Each gets a unique name via _UI_UID to prevent redefinition errors
//  when multiple macros expand in the same scope.
//─────────────────────────────────────────────────────────────────────────────

// Raw label — displays text string as a non-interactive GUI row
#define UI_LABEL(text) \
    int _UI_UID(_uiL) < string UIName = text; int UIMin = 0; int UIMax = 0; > = {0};


//═════════════════════════════════════════════════════════════════════════════
//  FILE HEADERS — Branded top banners for each shader file
//═════════════════════════════════════════════════════════════════════════════

// Standard file header: thin rule → title → thin rule
#define UI_FileHeader(title) \
    int _UI_UID(_fh1) < string UIName = "\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4"; int UIMin = 0; int UIMax = 0; > = {0}; \
    int _UI_UID(_fh2) < string UIName = title; int UIMin = 0; int UIMax = 0; > = {0}; \
    int _UI_UID(_fh3) < string UIName = "\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF"; int UIMin = 0; int UIMax = 0; > = {0};

// Extended file header: title + subtitle
#define UI_FileHeaderLong(title, subtitle) \
    int _UI_UID(_fh1) < string UIName = "\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4"; int UIMin = 0; int UIMax = 0; > = {0}; \
    int _UI_UID(_fh2) < string UIName = title; int UIMin = 0; int UIMax = 0; > = {0}; \
    int _UI_UID(_fh3) < string UIName = subtitle; int UIMin = 0; int UIMax = 0; > = {0}; \
    int _UI_UID(_fh4) < string UIName = "\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF"; int UIMin = 0; int UIMax = 0; > = {0};

// Full branded header: title + subtitle + author + version
#define UI_FileHeaderBranded(title, subtitle, author, version) \
    int _UI_UID(_fh1) < string UIName = "\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4"; int UIMin = 0; int UIMax = 0; > = {0}; \
    int _UI_UID(_fh2) < string UIName = title; int UIMin = 0; int UIMax = 0; > = {0}; \
    int _UI_UID(_fh3) < string UIName = subtitle; int UIMin = 0; int UIMax = 0; > = {0}; \
    int _UI_UID(_fh4) < string UIName = author; int UIMin = 0; int UIMax = 0; > = {0}; \
    int _UI_UID(_fh5) < string UIName = version; int UIMin = 0; int UIMax = 0; > = {0}; \
    int _UI_UID(_fh6) < string UIName = "\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF\xAF"; int UIMin = 0; int UIMax = 0; > = {0};


//═════════════════════════════════════════════════════════════════════════════
//  SECTION HEADERS — Collapsible group markers
//═════════════════════════════════════════════════════════════════════════════

// Major section header: ══ TITLE ══
#define UI_Section(title) \
    int _UI_UID(_sec) < string UIName = "\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD " title " \xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD"; int UIMin = 0; int UIMax = 0; > = {0};

// Sub-section header: ── Title
#define UI_SubSection(title) \
    int _UI_UID(_sub) < string UIName = "\xC4\xC4\xC4\xC4 " title; int UIMin = 0; int UIMax = 0; > = {0};

// Category label: |---- Title
#define UI_Category(title) \
    int _UI_UID(_cat) < string UIName = "|---- " title; int UIMin = 0; int UIMax = 0; > = {0};

// Inline label (no decoration): >>>>> TITLE <<<<<
#define UI_Element(title) \
    int _UI_UID(_elm) < string UIName = "     >>>>> " title " <<<<<"; int UIMin = 0; int UIMax = 0; > = {0};


//═════════════════════════════════════════════════════════════════════════════
//  SEPARATORS — Visual spacing between parameter groups
//═════════════════════════════════════════════════════════════════════════════

// Blank line (empty label)
#define UI_Space() \
    int _UI_UID(_sp) < string UIName = " "; int UIMin = 0; int UIMax = 0; > = {0};

// Thin horizontal rule: ────────────
#define UI_Separator() \
    int _UI_UID(_sep) < string UIName = "\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4"; int UIMin = 0; int UIMax = 0; > = {0};

// Thick horizontal rule: ══════════
#define UI_SeparatorThick() \
    int _UI_UID(_sep) < string UIName = "\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD"; int UIMin = 0; int UIMax = 0; > = {0};

// Dotted separator: · · · · · · · · · ·
#define UI_SeparatorDotted() \
    int _UI_UID(_sep) < string UIName = "\xFA \xFA \xFA \xFA \xFA \xFA \xFA \xFA \xFA \xFA \xFA \xFA \xFA \xFA \xFA"; int UIMin = 0; int UIMax = 0; > = {0};


//═════════════════════════════════════════════════════════════════════════════
//  WHITESPACE SYSTEM — From ReforgedUI by TheSandvichMaker
//
//  Two styles:
//    UI_WHITESPACE(n)          — Clean blank space (n spaces)
//    UI_SPECIAL_WHITESPACE(n)  — "| " prefix for tree-style indentation
//
//  Unique names via _UI_UID prevent conflicts when used multiple times.
//═════════════════════════════════════════════════════════════════════════════

// Standard whitespace: 1-60 space characters
#define WHITESPACE_1   " "
#define WHITESPACE_2   "  "
#define WHITESPACE_3   "   "
#define WHITESPACE_4   "    "
#define WHITESPACE_5   "     "
#define WHITESPACE_6   "      "
#define WHITESPACE_7   "       "
#define WHITESPACE_8   "        "
#define WHITESPACE_9   "         "
#define WHITESPACE_10  "          "
#define WHITESPACE_11  "           "
#define WHITESPACE_12  "            "
#define WHITESPACE_13  "             "
#define WHITESPACE_14  "              "
#define WHITESPACE_15  "               "
#define WHITESPACE_16  "                "
#define WHITESPACE_17  "                 "
#define WHITESPACE_18  "                  "
#define WHITESPACE_19  "                   "
#define WHITESPACE_20  "                    "
#define WHITESPACE_21  "                     "
#define WHITESPACE_22  "                      "
#define WHITESPACE_23  "                       "
#define WHITESPACE_24  "                        "
#define WHITESPACE_25  "                         "
#define WHITESPACE_26  "                          "
#define WHITESPACE_27  "                           "
#define WHITESPACE_28  "                            "
#define WHITESPACE_29  "                             "
#define WHITESPACE_30  "                              "
#define WHITESPACE_40  "                                        "
#define WHITESPACE_50  "                                                  "
#define WHITESPACE_60  "                                                            "

#define UI_WHITESPACE(n) \
    int _UI_UID(_ws) < string UIName = WHITESPACE_##n; int UIMin = 0; int UIMax = 0; > = {0};


// Special whitespace: pipe prefix for tree-style display
#define SPECIAL_WHITESPACE_1   "| "
#define SPECIAL_WHITESPACE_2   "|  "
#define SPECIAL_WHITESPACE_3   "|   "
#define SPECIAL_WHITESPACE_4   "|    "
#define SPECIAL_WHITESPACE_5   "|     "
#define SPECIAL_WHITESPACE_6   "|      "
#define SPECIAL_WHITESPACE_7   "|       "
#define SPECIAL_WHITESPACE_8   "|        "
#define SPECIAL_WHITESPACE_9   "|         "
#define SPECIAL_WHITESPACE_10  "|          "
#define SPECIAL_WHITESPACE_15  "|               "
#define SPECIAL_WHITESPACE_20  "|                    "
#define SPECIAL_WHITESPACE_25  "|                         "
#define SPECIAL_WHITESPACE_30  "|                              "
#define SPECIAL_WHITESPACE_40  "|                                        "
#define SPECIAL_WHITESPACE_50  "|                                                  "
#define SPECIAL_WHITESPACE_60  "|                                                            "

#define UI_SPECIAL_WHITESPACE(n) \
    int _UI_UID(_sw) < string UIName = SPECIAL_WHITESPACE_##n; int UIMin = 0; int UIMax = 0; > = {0};


//═════════════════════════════════════════════════════════════════════════════
//  PARAMETER DECLARATION MACROS
//
//  Standardized macros for declaring annotated parameters with consistent
//  formatting. Builds on patterns from kingeric1992 and Adyss.
//═════════════════════════════════════════════════════════════════════════════

// Boolean toggle
#define UI_BOOL(var, name, def) \
    bool var < string UIName = name; > = {def};

// Float with spinner widget
#define UI_FLOAT(var, name, step, lo, hi, def) \
    float var < string UIName = name; string UIWidget = "Spinner"; \
    float UIStep = step; float UIMin = lo; float UIMax = hi; > = {def};

// Integer with spinner widget
#define UI_INT(var, name, lo, hi, def) \
    int var < string UIName = name; string UIWidget = "Spinner"; \
    int UIMin = lo; int UIMax = hi; > = {def};

// Float4 color picker
#define UI_COLOR(var, name, r, g, b, a) \
    float4 var < string UIName = name; string UIWidget = "Color"; > = {r, g, b, a};

// Float3 color picker (no alpha)
#define UI_COLOR3(var, name, r, g, b) \
    float3 var < string UIName = name; string UIWidget = "Color"; > = {r, g, b};

// Indented parameter (tree style): "|- Name"
#define UI_FLOAT_TREE(var, name, step, lo, hi, def) \
    float var < string UIName = "|- " name; string UIWidget = "Spinner"; \
    float UIStep = step; float UIMin = lo; float UIMax = hi; > = {def};

#define UI_BOOL_TREE(var, name, def) \
    bool var < string UIName = "|- " name; > = {def};

#define UI_INT_TREE(var, name, lo, hi, def) \
    int var < string UIName = "|- " name; string UIWidget = "Spinner"; \
    int UIMin = lo; int UIMax = hi; > = {def};


//═════════════════════════════════════════════════════════════════════════════
//  DNI (Day/Night/Interior) PARAMETER MACROS
//
//  Standardized patterns for time-of-day separated parameters.
//  Inspired by Tapioks' original DNI system, improved with tree indentation.
//═════════════════════════════════════════════════════════════════════════════

// Declare a float parameter with Day/Night/Interior variants
#define UI_DNI_FLOAT(prefix, name, step, lo, hi, dayDef, nightDef, intDef) \
    float _UI_PASTE(prefix, _Day)      < string UIName = "|- " name " (Day)";      string UIWidget = "Spinner"; float UIStep = step; float UIMin = lo; float UIMax = hi; > = {dayDef}; \
    float _UI_PASTE(prefix, _Night)    < string UIName = "|- " name " (Night)";    string UIWidget = "Spinner"; float UIStep = step; float UIMin = lo; float UIMax = hi; > = {nightDef}; \
    float _UI_PASTE(prefix, _Interior) < string UIName = "|- " name " (Interior)"; string UIWidget = "Spinner"; float UIStep = step; float UIMin = lo; float UIMax = hi; > = {intDef};

// Declare a bool parameter with Day/Night/Interior variants
#define UI_DNI_BOOL(prefix, name, dayDef, nightDef, intDef) \
    bool _UI_PASTE(prefix, _Day)      < string UIName = "|- " name " (Day)";      > = {dayDef}; \
    bool _UI_PASTE(prefix, _Night)    < string UIName = "|- " name " (Night)";    > = {nightDef}; \
    bool _UI_PASTE(prefix, _Interior) < string UIName = "|- " name " (Interior)"; > = {intDef};

// Runtime DNI interpolation: lerp between Day/Night/Interior values
// Requires ENB's EInteriorFactor, ENightDayFactor to be declared
#define DNI_LERP(prefix) \
    lerp(lerp(_UI_PASTE(prefix, _Night), _UI_PASTE(prefix, _Day), ENightDayFactor), \
         _UI_PASTE(prefix, _Interior), EInteriorFactor)


//═════════════════════════════════════════════════════════════════════════════
//  7-TOD (Time of Day) PARAMETER MACROS
//
//  For shaders using ENB's full 7-phase interpolation:
//  Dawn, Sunrise, Day, Sunset, Dusk, Night, Interior
//═════════════════════════════════════════════════════════════════════════════

#define UI_7TOD_FLOAT(prefix, name, step, lo, hi, def) \
    float _UI_PASTE(prefix, _Dawn)     < string UIName = "|- " name " (Dawn)";     string UIWidget = "Spinner"; float UIStep = step; float UIMin = lo; float UIMax = hi; > = {def}; \
    float _UI_PASTE(prefix, _Sunrise)  < string UIName = "|- " name " (Sunrise)";  string UIWidget = "Spinner"; float UIStep = step; float UIMin = lo; float UIMax = hi; > = {def}; \
    float _UI_PASTE(prefix, _Day)      < string UIName = "|- " name " (Day)";      string UIWidget = "Spinner"; float UIStep = step; float UIMin = lo; float UIMax = hi; > = {def}; \
    float _UI_PASTE(prefix, _Sunset)   < string UIName = "|- " name " (Sunset)";   string UIWidget = "Spinner"; float UIStep = step; float UIMin = lo; float UIMax = hi; > = {def}; \
    float _UI_PASTE(prefix, _Dusk)     < string UIName = "|- " name " (Dusk)";     string UIWidget = "Spinner"; float UIStep = step; float UIMin = lo; float UIMax = hi; > = {def}; \
    float _UI_PASTE(prefix, _Night)    < string UIName = "|- " name " (Night)";    string UIWidget = "Spinner"; float UIStep = step; float UIMin = lo; float UIMax = hi; > = {def}; \
    float _UI_PASTE(prefix, _Interior) < string UIName = "|- " name " (Interior)"; string UIWidget = "Spinner"; float UIStep = step; float UIMin = lo; float UIMax = hi; > = {def};

// Runtime 7-TOD interpolation (requires TimeOfDay1/2 + EInteriorFactor)
#define TOD7_LERP(prefix) \
    lerp( \
        _UI_PASTE(prefix, _Dawn)    * TimeOfDay1.x + \
        _UI_PASTE(prefix, _Sunrise) * TimeOfDay1.y + \
        _UI_PASTE(prefix, _Day)     * TimeOfDay1.z + \
        _UI_PASTE(prefix, _Sunset)  * TimeOfDay1.w + \
        _UI_PASTE(prefix, _Dusk)    * TimeOfDay2.x + \
        _UI_PASTE(prefix, _Night)   * TimeOfDay2.y, \
        _UI_PASTE(prefix, _Interior), EInteriorFactor)


//═════════════════════════════════════════════════════════════════════════════
//  MONITOR DISPLAY MACROS — Read-only parameters for data visualization
//
//  These create float parameters visible in the ENB GUI but intended for
//  display only. The shader writes values to them each frame via an
//  output technique. Useful for debugging and monitoring SkyrimBridge data.
//
//  Note: ENB parameters are read/write — users CAN modify these values,
//  but they'll be overwritten next frame. The "(read)" suffix signals intent.
//═════════════════════════════════════════════════════════════════════════════

// Read-only float display (value overwritten each frame)
#define UI_MONITOR_FLOAT(var, name, lo, hi) \
    float var < string UIName = "\xFE " name " (read)"; string UIWidget = "Spinner"; \
    float UIStep = 0.001; float UIMin = lo; float UIMax = hi; > = {0.0};

// Read-only bool indicator (overwritten each frame)
#define UI_MONITOR_BOOL(var, name) \
    bool var < string UIName = "\xFE " name " (read)"; > = {false};


//═════════════════════════════════════════════════════════════════════════════
//  SHADER GROUP SYSTEM — For multi-technique shaders
//
//  Allows splitting UI across techniques using #if SHADERGROUP == N
//  Each group gets its own section in the ENB GUI.
//═════════════════════════════════════════════════════════════════════════════

// Convenience: end current group and start next
#define UI_END_GROUP() #undef SHADERGROUP
#define UI_BEGIN_GROUP(n) #define SHADERGROUP n


//═════════════════════════════════════════════════════════════════════════════
//  CONDITIONAL TOOL BLOCKS — Debug parameters behind compile flag
//═════════════════════════════════════════════════════════════════════════════

// Wrap debug/visualization parameters inside ENABLE_TOOLS
// These are stripped in release builds
#define UI_TOOLS_BEGIN() UI_Space() UI_Element("TOOLS") UI_Space()
#define UI_TOOLS_END()


#endif // _UI_PRIMER_
