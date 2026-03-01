// COMIC TREATMENT FUNCTION //////////////////////
//////////////////////////////////////////////////
// Outline code originally written by:          //
//     Marty McFly                              //
// All other code by:                           //
//     TreyM                                    //
//////////////////////////////////////////////////

TEXTURE(Panels1,   "Include/Textures/Masks/Panels/panels1.jpg")
TEXTURE(Panels2,   "Include/Textures/Masks/Panels/panels2.jpg")
TEXTURE(Page,      "Include/Textures/Masks/Panels/page.jpg")
TEXTURE(Page2,     "Include/Textures/Masks/Panels/page2.jpg")
TEXTURE(Watermark, "Include/Textures/Masks/Panels/watermark.jpg")

TEXTURE(Bubble1,   "Include/Textures/Masks/Overlays/bubble1.jpg")
TEXTURE(Bubble2,   "Include/Textures/Masks/Overlays/bubble2.jpg")
TEXTURE(Bubble3,   "Include/Textures/Masks/Overlays/bubble3.jpg")

TEXTURE(Box1,      "Include/Textures/Masks/Overlays/box1.jpg")
TEXTURE(Box2,      "Include/Textures/Masks/Overlays/box2.jpg")
TEXTURE(Box3,      "Include/Textures/Masks/Overlays/box3.jpg")

TEXTURE(Paper1,    "Include/Textures/Paper/paper1.jpg")
TEXTURE(Paper2,    "Include/Textures/Paper/paper2.jpg")
TEXTURE(Paper3,    "Include/Textures/Paper/paper3.jpg")

TEXTURE(Dirt1,     "Include/Textures/Paper/dirt1.jpg")
TEXTURE(Dirt2,     "Include/Textures/Paper/dirt2.jpg")

TEXTURE(Grid123,   "Include/Textures/Grids/g123.png")
TEXTURE(Grid45,    "Include/Textures/Grids/g45.png")

float3 ComicTreatment(VS_OUTPUT IN, uniform Texture2D TEX_ColorEdge)
{
    float3 color, edge_color, outlines, skymask, sky;
    float  edge_depth, depth, detail, watermark;
    float2 coord;

    coord = IN.txcoord.xy;
    color = BACKBUFFER(coord);

    float3 color_array[10] =
    {
        // White
        float3(1.00, 1.00, 1.00),

        // Red
        float3(1.0, 0.1, 0.1),

        // Bright Green
        float3(0.541, 1, 0),

        // Yellow
        float3(1.00, 0.75, 0.00),

        // Sky Blue
        float3(0.00, 0.50, 1.00),

        // Pastel Lime
        float3(0.725, 1, 0.29),

        // Orange
        float3(1, 0.435, 0.161),

        // Violet
        float3(0.584, 0.278, 1),

        // Muted Teal
        float3(0.435, 0.769, 0.765),

        // Bubble Gum
        float3(1, 0.38, 0.612),
    };

    float3 sky_color_array[10] =
    {
        // White
        float3(1.00, 1.00, 1.00),

        // Red (BASE COLOR)
        float3(0, 1, 0.847),

        // Bright Green (BASE COLOR)
        float3(0.753, 0.431, 1),

        // Yellow (BASE COLOR)
        float3(0.475, 1, 0.882),

        // Sky Blue (BASE COLOR)
        float3(1, 0.969, 0.475),

        // Pastel Lime (BASE COLOR)
        float3(1, 0.373, 0.549),

        // Orange (BASE COLOR)
        float3(0.161, 0.949, 1),

        // Violet (BASE COLOR)
        float3(1, 0.855, 0.278),

        // Muted Teal (BASE COLOR)
        float3(1, 0.455, 0.259),

        // Bubble Gum (BASE COLOR)
        float3(0.38, 1, 0.906),
    };

    depth      = 1.0 - GetLinearizedDepth(coord);
    edge_color = TEX_ColorEdge.Sample(LinearSampler, coord);
    edge_color = saturate(lerp(0.0, 319 / 255.0, edge_color));

    edge_depth = 0;

    int gweights[9] =
    {
        2, 4, 2,
        4,-24,4,
        2, 4, 2
    };

    for(int x = -1; x <= 1; x++)
    for(int y = -1; y <= 1; y++)
    {
        float2 loc = float2(x,y) * (float2(ScreenSize.y, ScreenSize.y * ScreenSize.z));
        int id = (x+1) + (y+1)*3;
        //edge_color += (GetLuma(TextureColor.Sample(LinearSampler, coord + loc).rgb, Rec709_5)) * gweights[id];
        edge_depth += GetLinearizedDepth(coord + loc) * gweights[id];
    }

    // Mask out the sky
    skymask  = all(depth);

    // Prepare the outlines
    switch(LINE_MODE)
    {
        case 1:
            outlines = saturate(1.0 + edge_depth/depth * lerp(800.0, 10.0, 1.0 - pow(depth, 100.0)));
        break;

        case 2:
            outlines = saturate(1.0 - edge_depth/depth * lerp(800.0, 10.0, 1.0 - pow(depth, 100.0)));
        break;

        case 3:
            outlines = saturate(1.0 - abs(edge_depth)/depth * lerp(800.0, 10.0, 1.0 - pow(depth, 100.0)));
        break;

        case 4:
            outlines = 1.0;
        break;

        case 5:
            outlines = saturate(1.0 - edge_depth/depth * lerp(1000.0, 10.0, 1.0 - pow(depth, 100.0)));
        break;
    };

    //outlines = saturate(lerp(0.0, 1.0, outlines));

    detail   =  GetLuma(color * 7.5, Rec709_5);
    detail   =  pow(detail, 2.2);
    detail   =  floor(detail);
    detail  /= (SHADE_STEPS * 1.0);
    detail   = pow(detail, 1.0 / 2.2);
    detail   = saturate(detail);

    // Prepare the background
    color    =  GetLuma(color, Rec709_5);
    #if (DEBUG_MODE == 1)
        color    =  pow(color, (TEST_MODE ? DNI(D_GAMMA_DAY, D_GAMMA_NIGHT, D_GAMMA_INTERIOR) :
                    DNI(2.2, 2.4, 2.4)));
        color   *=  TEST_MODE ? DNI(D_BRIGHTNESS_DAY, D_BRIGHTNESS_NIGHT, D_BRIGHTNESS_INTERIOR) :
                    DNI(10.0, 10.0, 10.0);
        color    =  floor(color);
        color   /= (SHADE_STEPS * 1.0);
        color    =  pow(color, 1.0 / (TEST_MODE ? DNI(D_GAMMA_DAY, D_GAMMA_NIGHT, D_GAMMA_INTERIOR) : DNI(2.2, 2.2, 2.2)));
    #else
        color    =  pow(color, DNI(lerp(2.2, lerp(3.0, 1.4, (LIGHT_INTENSITY * 0.005)), ENABLE_ADV_LIGHT),
                                   lerp(2.2, lerp(3.0, 1.4, (LIGHT_INTENSITY * 0.005)), ENABLE_ADV_LIGHT),
                                   lerp(2.2, lerp(3.0, 1.4, (LIGHT_INTENSITY * 0.005)), ENABLE_ADV_LIGHT)));
        color   *=  DNI(10.0, 10.0, 10.0);
        color    =  floor(color);
        color   /= (SHADE_STEPS * 1.0);
        color    =  pow(color, 1.0 / DNI(2.2, 2.2, 2.2));
    #endif

    color    =  saturate(color);

    // Restore shadow detail
    if (SHADOW_DETAIL && (LINE_MODE != 5)) color = BlendLighten(color, edge_color);

    // Overlay the outlines
    if (LINE_MODE != 5)
    {
        color = lerp(1-color, color, outlines);
    }
    else
    {
        color = lerp(1-color, color, edge_color);
        color = lerp(color, 1-color, outlines);
        //color = 1-color;
    }


    // Overlay the optional watermark
    watermark = Watermark.Sample(LinearSampler, coord).r;
    if (WATERMARK && !PAGE) color = lerp(1-color, color, watermark);

    // Invert image if needed
    if (INK_INVERT) color = 1-color;

    // Distance darkening
    if (DEPTH_DARKEN) color *= lerp(1.0, lerp(1.0, 0.0, 1.0 - pow(depth + 0.005, LINE_MODE != 4 ? 8.0 : 25.0)), skymask);

    // Set sky brightness
    sky    = DNI(lerp(1.0, lerp(0.0, 1.0, IGNORE_SKY), INK_INVERT),
                 lerp(0.0, lerp(1.0, 0.0, IGNORE_SKY), INK_INVERT),
                 lerp(0.0, lerp(1.0, 0.0, IGNORE_SKY), INK_INVERT));

    color  = lerp(color, sky, 1-skymask);

    // Add Grain
    if (INK_GRAIN && (GRAIN_AMOUNT != 0)) color = InkGrainPass(color, coord, 1);

    if (!CUSTOM_COLOR_ENABLE && !ENABLE_ADV_LIGHT)
    {
        // Colorize the input with preset color values
        if (TOD_COLOR)
        {
            color *= DNI(color_array[COLOR_INDEX - 1],
                         color_array[COLOR_INDEX - 1] * 0.66,
                         color_array[COLOR_INDEX - 1]);
        }
        else
        {
            color *= color_array[COLOR_INDEX - 1];
        }

    }
    else if (CUSTOM_COLOR_ENABLE && !ENABLE_ADV_LIGHT)
    {
        // Colorize the input with custom color values
        color = DNI(lerp(CUSTOM_SHADOW_COLOR_DAY,      CUSTOM_LIGHT_COLOR_DAY,      color),
                    lerp(CUSTOM_SHADOW_COLOR_NIGHT,    CUSTOM_LIGHT_COLOR_NIGHT,    color),
                    lerp(CUSTOM_SHADOW_COLOR_INTERIOR, CUSTOM_LIGHT_COLOR_INTERIOR, color));
    }

    // Remove sky color if Colored Sky is disabled
    if (!COLOR_SKY && (INK_GRAIN && (GRAIN_AMOUNT != 0))) color  = lerp(sky, color, skymask);

    // Desaturate and darken preset colors if preset night darkening is enabled
    if (!CUSTOM_COLOR_ENABLE && TOD_COLOR) color = DNI(color, lerp(GetLuma(color, Rec709_5), color, 0.66), color);

    // Change sky color if Duo Tone is enabled
    if (DUO_TONE && COLOR_SKY && !ENABLE_ADV_LIGHT)
    {
        color  = lerp(color, 1.0, !skymask);
        if (CUSTOM_COLOR_ENABLE)
        {
            color *= lerp(1.0, DNI(lerp(CUSTOM_SKY_DAY,      lerp(0.0, CUSTOM_SKY_DAY,      IGNORE_SKY), INK_INVERT),
                                   lerp(CUSTOM_SKY_NIGHT,    lerp(0.0, CUSTOM_SKY_NIGHT,    IGNORE_SKY), INK_INVERT),
                                   lerp(CUSTOM_SKY_INTERIOR, lerp(0.0, CUSTOM_SKY_INTERIOR, IGNORE_SKY), INK_INVERT)),
                                   !skymask);
        }
        else
        {
            color *= DNI(lerp(1.0, lerp(sky_color_array[COLOR_INDEX -1], lerp(0.0, sky_color_array[COLOR_INDEX -1], IGNORE_SKY), INK_INVERT), !skymask),
                         lerp(1.0, lerp(0.0, lerp(1.0, 0.0, IGNORE_SKY), INK_INVERT), !skymask),
                         lerp(1.0, lerp(0.0, lerp(1.0, 0.0, IGNORE_SKY), INK_INVERT), !skymask));
        }
    }

    // Advanced Lights
    if (ENABLE_ADV_LIGHT && (ENABLE_LIGHT_1 || ENABLE_LIGHT_2 || ENABLE_LIGHT_3)) color = Spotlight(color, coord);

    // Gradient Overlay
    if (ENABLE_GRADIENT) color = GradientPass(color, coord);

    // Add Grain
    if (INK_GRAIN && (GRAIN_AMOUNT != 0)) color = InkGrainPass(color, coord, 1);

    // Dither to avoid any visible banding
    color += triDither(color, coord, Timer.x, 8);

    //return saturate(edge_color);
    return saturate(color);
}

float3 Panels(float2 coord)
{
    float  border_left, border_top, border_right, border_bottom, depth;
    float  h_div_top, h_div_bottom, v_div_left, v_div_right, watermark;
    float2 h_t_coord, h_b_coord, v_l_coord, v_r_coord, page, buffer_coord;
    float3 color;

    buffer_coord  = float2((coord.x - 0.5) + lerp(lerp(0.2, 0.8, BUFFER_X), lerp(0.4, 0.6, BUFFER_X), PAGE_INDEX -1), coord.y);

    h_t_coord     = float2(coord.x, (coord.y - 0.5) + HOR_T_Y);
    h_b_coord     = float2(coord.x, (coord.y - 0.5) + HOR_B_Y);
    v_l_coord     = float2((coord.x + 0.5) - VER_L_X, coord.y);
    v_r_coord     = float2((coord.x + 0.5) - VER_R_X, coord.y);

    border_left   = Panels1.Sample(LinearSampler, coord).r;
    border_top    = Panels2.Sample(LinearSampler, coord).r;
    border_right  = Panels1.Sample(LinearSampler, coord).b;
    border_bottom = Panels1.Sample(LinearSampler, coord).g;

    h_div_top     = Panels2.Sample(LinearSampler, h_t_coord).g;
    h_div_bottom  = Panels2.Sample(LinearSampler, h_b_coord).g;

    v_div_left    = Panels2.Sample(LinearSampler, v_l_coord).b;
    v_div_right   = Panels2.Sample(LinearSampler, v_r_coord).b;

    switch(PAGE_INDEX)
    {
        case 1:
            page = Page.Sample(LinearSampler, coord).rg;
        break;

        case 2:
            page = Page2.Sample(LinearSampler, coord).rg;
        break;
    }

    if (PAGE && FRAME_BORDER)
    {
        color = BACKBUFFER(buffer_coord);
        depth = 1.0 - GetLinearizedDepth(buffer_coord);
    }
    else
    {
        color = BACKBUFFER(coord);
        depth = 1.0 - GetLinearizedDepth(coord);
    }
        depth = pow(depth, 50.0);
        depth = 1.0 - depth;

    if (FRAME_BORDER)
    {
        if (V_DIV_L) color = lerp(color, lerp(lerp(0.0, 1.0, PANELS_INVERT), color, VER_L_Z > depth * 10.0 ? 1.0 : 0.0), v_div_left);
        if (V_DIV_R) color = lerp(color, lerp(lerp(0.0, 1.0, PANELS_INVERT), color, VER_R_Z > depth * 10.0 ? 1.0 : 0.0), v_div_right);
        if (H_DIV_T) color = lerp(color, lerp(lerp(0.0, 1.0, PANELS_INVERT), color, HOR_T_Z > depth * 10.0 ? 1.0 : 0.0), h_div_top);
        if (H_DIV_B) color = lerp(color, lerp(lerp(0.0, 1.0, PANELS_INVERT), color, HOR_B_Z > depth * 10.0 ? 1.0 : 0.0), h_div_bottom);
                     color = lerp(color, lerp(lerp(0.0, 1.0, PANELS_INVERT), color, BOR_L_Z > depth * 10.0 ? 1.0 : 0.0), border_left);
                     color = lerp(color, lerp(lerp(0.0, 1.0, PANELS_INVERT), color, BOR_T_Z > depth * 10.0 ? 1.0 : 0.0), border_top);
                     color = lerp(color, lerp(lerp(0.0, 1.0, PANELS_INVERT), color, BOR_R_Z > depth * 10.0 ? 1.0 : 0.0), border_right);
                     color = lerp(color, lerp(lerp(0.0, 1.0, PANELS_INVERT), color, BOR_B_Z > depth * 10.0 ? 1.0 : 0.0), border_bottom);
        if (PAGE)
        {
            color = color * page.r;
            color = lerp(color, lerp(0.0, 1.0, PANELS_INVERT), page.g);
        }
    }

    return saturate(color);
}

// Credit for the depth-aware code: Adyss
float3 Overlays(float2 coord)
{
    float3 color;
    float2 box_1_coord, box_2_coord, box_3_coord, bubble_1_coord, bubble_2_coord, bubble_3_coord, buffer_coord;
    float  bubble_1, bubble_2, bubble_3, box_1, box_2, box_3, grid;
    float  bubble_1_border, bubble_2_border, bubble_3_border;
    float  box_1_border, box_2_border, box_3_border, depth;

    buffer_coord   = float2((coord.x - 0.5) + lerp(lerp(0.2, 0.8, BUFFER_X), lerp(0.4, 0.6, BUFFER_X), PAGE_INDEX -1), coord.y);
    color          = BACKBUFFER(coord);

    box_1_coord    = float2((coord.x - 0.5) + BOX_1_X,
                            (coord.y - 0.5) + BOX_1_Y);

    box_2_coord    = float2((coord.x - 0.5) + BOX_2_X,
                            (coord.y - 0.5) + BOX_2_Y);

    box_3_coord    = float2((coord.x - 0.5) + BOX_3_X,
                            (coord.y - 0.5) + BOX_3_Y);

    bubble_1_coord = float2(lerp((coord.x - 0.5) + BUBBLE_1_X, (1-coord.x + 0.5) - BUBBLE_1_X, BUBBLE_1_FLIP_X),
                            lerp((coord.y - 0.5) + BUBBLE_1_Y, (1-coord.y + 0.5) - BUBBLE_1_Y, BUBBLE_1_FLIP_Y));

    bubble_2_coord = float2(lerp((coord.x - 0.5) + BUBBLE_2_X, (1-coord.x + 0.5) - BUBBLE_2_X, BUBBLE_2_FLIP_X),
                            lerp((coord.y - 0.5) + BUBBLE_2_Y, (1-coord.y + 0.5) - BUBBLE_2_Y, BUBBLE_2_FLIP_Y));

    bubble_3_coord = float2(lerp((coord.x - 0.5) + BUBBLE_3_X, (1-coord.x + 0.5) - BUBBLE_3_X, BUBBLE_3_FLIP_X),
                            lerp((coord.y - 0.5) + BUBBLE_3_Y, (1-coord.y + 0.5) - BUBBLE_2_Y, BUBBLE_2_FLIP_Y));

    switch(BUBBLE_1_STYLE)
    {
        case 1:
            bubble_1        = Bubble1.Sample(LinearSampler, bubble_1_coord).r;
            bubble_1_border = 1.0 - Bubble1.Sample(LinearSampler, bubble_1_coord).g;
        break;

        case 2:
            bubble_1        = Bubble2.Sample(LinearSampler, bubble_1_coord).r;
            bubble_1_border = 1.0 - Bubble2.Sample(LinearSampler, bubble_1_coord).g;
        break;

        case 3:
            bubble_1        = Bubble3.Sample(LinearSampler, bubble_1_coord).r;
            bubble_1_border = 1.0 - Bubble3.Sample(LinearSampler, bubble_1_coord).g;
        break;
    }
    switch(BUBBLE_2_STYLE)
    {
        case 1:
            bubble_2        = Bubble1.Sample(LinearSampler, bubble_2_coord).r;
            bubble_2_border = 1.0 - Bubble1.Sample(LinearSampler, bubble_2_coord).g;
        break;

        case 2:
            bubble_2        = Bubble2.Sample(LinearSampler, bubble_2_coord).r;
            bubble_2_border = 1.0 - Bubble2.Sample(LinearSampler, bubble_2_coord).g;
        break;

        case 3:
            bubble_2        = Bubble3.Sample(LinearSampler, bubble_2_coord).r;
            bubble_2_border = 1.0 - Bubble3.Sample(LinearSampler, bubble_2_coord).g;
        break;
    }
    switch(BUBBLE_3_STYLE)
    {
        case 1:
            bubble_3        = Bubble1.Sample(LinearSampler, bubble_3_coord).r;
            bubble_3_border = 1.0 - Bubble1.Sample(LinearSampler, bubble_3_coord).g;
        break;

        case 2:
            bubble_3        = Bubble2.Sample(LinearSampler, bubble_3_coord).r;
            bubble_3_border = 1.0 - Bubble2.Sample(LinearSampler, bubble_3_coord).g;
        break;

        case 3:
            bubble_3        = Bubble3.Sample(LinearSampler, bubble_3_coord).r;
            bubble_3_border = 1.0 - Bubble3.Sample(LinearSampler, bubble_3_coord).g;
        break;
    }


    switch(BOX_1_STYLE)
    {
        case 1:
            box_1        = Box1.Sample(LinearSampler, box_1_coord).r;
            box_1_border = 1.0 - Box1.Sample(LinearSampler, box_1_coord).g;
        break;

        case 2:
            box_1        = Box2.Sample(LinearSampler, box_1_coord).r;
            box_1_border = 1.0 - Box2.Sample(LinearSampler, box_1_coord).g;
        break;

        case 3:
            box_1        = Box3.Sample(LinearSampler, box_1_coord).r;
            box_1_border = 1.0 - Box3.Sample(LinearSampler, box_1_coord).g;
        break;
    }
    switch(BOX_2_STYLE)
    {
        case 1:
            box_2        = Box1.Sample(LinearSampler, box_2_coord).r;
            box_2_border = 1.0 - Box1.Sample(LinearSampler, box_2_coord).g;
        break;

        case 2:
            box_2        = Box2.Sample(LinearSampler, box_2_coord).r;
            box_2_border = 1.0 - Box2.Sample(LinearSampler, box_2_coord).g;
        break;

        case 3:
            box_2        = Box3.Sample(LinearSampler, box_2_coord).r;
            box_2_border = 1.0 - Box3.Sample(LinearSampler, box_2_coord).g;
        break;
    }
    switch(BOX_3_STYLE)
    {
        case 1:
            box_3        = Box1.Sample(LinearSampler, box_3_coord).r;
            box_3_border = 1.0 - Box1.Sample(LinearSampler, box_3_coord).g;
        break;

        case 2:
            box_3        = Box2.Sample(LinearSampler, box_3_coord).r;
            box_3_border = 1.0 - Box2.Sample(LinearSampler, box_3_coord).g;
        break;

        case 3:
            box_3        = Box3.Sample(LinearSampler, box_3_coord).r;
            box_3_border = 1.0 - Box3.Sample(LinearSampler, box_3_coord).g;
        break;
    }

    if (PAGE && FRAME_BORDER)
    {
        depth = 1.0 - GetLinearizedDepth(buffer_coord);
    }
    else
    {
        depth = 1.0 - GetLinearizedDepth(coord);
    }

    depth = pow(depth, 50.0);
    depth = 1.0 - depth;

    if (BUBBLE_1)
    {
        color  = lerp(color, lerp(color, 1.0, BUBBLE_1_Z > depth * 10.0 ? 0.0 : 1.0), bubble_1);
        color *= lerp(1.0, bubble_1_border, BUBBLE_1_Z > depth * 10.0 ? 0.0 : 1.0);
    }
    if (BUBBLE_2)
    {
        color  = lerp(color, lerp(color, 1.0, BUBBLE_2_Z > depth * 10.0 ? 0.0 : 1.0), bubble_2);
        color *= lerp(1.0, bubble_2_border, BUBBLE_2_Z > depth * 10.0 ? 0.0 : 1.0);
    }
    if (BUBBLE_3)
    {
        color  = lerp(color, lerp(color, 1.0, BUBBLE_3_Z > depth * 10.0 ? 0.0 : 1.0), bubble_3);
        color *= lerp(1.0, bubble_3_border, BUBBLE_3_Z > depth * 10.0 ? 0.0 : 1.0);
    }


    if (BOX_1)
    {
        color  = lerp(color, lerp(color, BOX_1_COLOR ? float3(1.0, 0.75, 0.0) : 1.0, BOX_1_Z > depth * 10.0 ? 0.0 : 1.0), box_1);
        color *= lerp(1.0, box_1_border, BOX_1_Z > depth * 10.0 ? 0.0 : 1.0);
    }
    if (BOX_2)
    {
        color  = lerp(color, lerp(color, BOX_2_COLOR ? float3(1.0, 0.75, 0.0) : 1.0, BOX_2_Z > depth * 10.0 ? 0.0 : 1.0), box_2);
        color *= lerp(1.0, box_2_border, BOX_2_Z > depth * 10.0 ? 0.0 : 1.0);
    }
    if (BOX_3)
    {
        color  = lerp(color, lerp(color, BOX_3_COLOR ? float3(1.0, 0.75, 0.0) : 1.0, BOX_3_Z > depth * 10.0 ? 0.0 : 1.0), box_3);
        color *= lerp(1.0, box_3_border, BOX_3_Z > depth * 10.0 ? 0.0 : 1.0);
    }

    return color;
}

float3 Paper(float2 coord)
{
    float3 color, paper, damage, dirt;
    float2 paper_coord, dirt_coord;
    float  border, grid, watermark;

    float2 flip[4] =
    {
        float2(coord.x,       coord.y),
        float2(1.0 - coord.x, coord.y),
        float2(1.0 - coord.x, 1.0 - coord.y),
        float2(coord.x, 1.0 - coord.y)
    };

    color = BACKBUFFER(coord);

    switch(PAGE_INDEX)
    {
        case 1:
            border = Page.Sample(LinearSampler, coord).r;
        break;

        case 2:
            border = Page2.Sample(LinearSampler, coord).r;
        break;
    }

    switch(PAPER_INDEX)
    {
        case 1:
            paper = Paper1.Sample(LinearSampler, flip[PAPER_VARIATION - 1]);
        break;

        case 2:
            paper = Paper2.Sample(LinearSampler, flip[PAPER_VARIATION - 1]);
        break;

        case 3:
            paper = Paper3.Sample(LinearSampler, flip[PAPER_VARIATION - 1]);
        break;
    }

    switch(GRUNGE_INDEX)
    {
        case 1:
            dirt = 1-Dirt1.Sample(LinearSampler, flip[GRUNGE_VARIATION - 1]);
        break;

        case 2:
            dirt = 1-Dirt2.Sample(LinearSampler, flip[GRUNGE_VARIATION - 1]);
        break;
    }

    switch(GRID_INDEX)
    {
        case 1:
            grid = Grid123.Sample(LinearSampler, coord).r;
        break;

        case 2:
            grid = Grid123.Sample(LinearSampler, coord).g;
        break;

        case 3:
            grid = Grid123.Sample(LinearSampler, coord).b;
        break;

        case 4:
            grid = Grid45.Sample(LinearSampler,  coord).r;
        break;

        case 5:
            grid = Grid45.Sample(LinearSampler,  coord).g;
        break;
    }

    if (PAPER_TEXTURE)  color  = lerp(color, BlendOverlayf(paper, color), (PAPER_INTENSITY * 0.01));

    if (GRUNGE_OVERLAY) color *= lerp(1.0, 1-(saturate(dirt * 3.0)), (GRUNGE_INTENSITY * 0.01));

    if (PAPER_LEVELS)
    {
        color = pow(color, lerp(1.2, 1.0, (PAPER_AGE * 0.01)));
        color = lerp(GetLuma(color, Rec709_5), color, lerp(0.9, 0.7, (PAPER_AGE * 0.01)));
        color = lerp(color, color * float3(1.0, 0.898, 0.749), (PAPER_AGE * 0.01));
        color = lerp(lerp(20 / 255.0, 25 / 255.0, (PAPER_AGE * 0.01)), 220 / 255.0, color);
    }

    if (FRAME_BORDER && PAGE)
    {
        color = lerp(0.0, color, border.r);

        // Overlay the optional watermark
        watermark = Watermark.Sample(LinearSampler, coord).r;
        if (WATERMARK) color = lerp(1-color, color, watermark);
    }

    if (COMPOSITION_GRID) color = BlendOverlayf(grid, color * 0.75);

    return color;
}
