// SIMPLE GRID OVERLAY SHADER //////////////////////
////////////////////////////////////////////////////
// - By TreyM & Marty McFly                       //
////////////////////////////////////////////////////

// TEXTURES //////////////////////////////////////
    TEXTURE(Grids123,  "Include/Textures/Grids/g123.png")
    TEXTURE(Grids45,   "Include/Textures/Grids/g45.png")

// FUNCTION //////////////////////////////////////
    float3 GridPass(float3 color, float2 txcoord)
    {
        // Initial setup /////////////////////////
        static const float current_aspect = ScreenSize.z;
        float2 ntex = txcoord * 2.0 - 1.0;
        float is_inside;
        float4 grid;

        // Setup texture coordinates /////////////
        [flatten]
            if(BORDER_RATIO < current_aspect) ntex.x *= current_aspect / BORDER_RATIO;
            else ntex.y /= current_aspect / BORDER_RATIO;

        // Setup UV Based on Letterbox ///////////
        ntex  = ntex * 0.5 + 0.5;
        is_inside = all(saturate(1.0 - ntex * ntex));
        ntex  = SYNC_GRID && ENABLE_BORDER ? ntex : txcoord;
        is_inside = SYNC_GRID && ENABLE_BORDER ? is_inside : 1;

        float4 grid123 = Grids123.Sample(Sampler1, ntex);
        float4 grid45 = Grids45.Sample(Sampler1, ntex);

        // Grab the texture //////////////////////
        if     (GRID_SELECT == 1) grid = grid123.r;
        else if(GRID_SELECT == 2) grid = grid123.g;
        else if(GRID_SELECT == 3) grid = grid123.b;
        else if(GRID_SELECT == 4) grid = grid45.r;
        else if(GRID_SELECT == 5) grid = grid45.g;

        // Blend the grid with the background ////
        if     (GRID_BLEND == 3) return lerp(color, frac(color + 0.5), saturate(grid * 2 - 1) * is_inside * (GRID_OPACITY * 0.01));
        else if(GRID_BLEND != 3) return lerp(color, BlendSoftLightf(color.rgb, lerp((1.0 - grid.rgb), grid.rgb, (GRID_BLEND - 1))), is_inside * (GRID_OPACITY * 0.01));

        return color;
    }
