// CINEMVTIC PHOTO SUITE ///////////////////////////
////////////////////////////////////////////////////
// - By TreyM & Marty McFly (Aspect Ratio Code)   //
////////////////////////////////////////////////////


// TEXTURES //////////////////////////////////////

    // Light Leaks ///////////////////////////////
    TEXTURE(Leak1, "Include/Textures/Leaks/leak1.jpg")
    TEXTURE(Leak2, "Include/Textures/Leaks/leak2.jpg")
    TEXTURE(Leak3, "Include/Textures/Leaks/leak3.jpg")
    TEXTURE(Leak4, "Include/Textures/Leaks/leak4.jpg")
    TEXTURE(Leak5, "Include/Textures/Leaks/leak5.jpg")

    // Photo Frames //////////////////////////////
    TEXTURE(FrameFilm1, "Include/Textures/Frames/film1.png")
    TEXTURE(FrameFilm2, "Include/Textures/Frames/film2.png")
    TEXTURE(FrameFilm3, "Include/Textures/Frames/film3.png")
    TEXTURE(FrameFilm4, "Include/Textures/Frames/film4.png")
    TEXTURE(FrameFilm5, "Include/Textures/Frames/film5.png")
    TEXTURE(FrameFilm6, "Include/Textures/Frames/film6.png")
    TEXTURE(FrameFilm7, "Include/Textures/Frames/film7.png")
    TEXTURE(FrameFilm8, "Include/Textures/Frames/film8.png")
    TEXTURE(FrameFilm9, "Include/Textures/Frames/film9.png")
    TEXTURE(FrameFilm10k, "Include/Textures/Frames/film10k.png")
    TEXTURE(FrameFilm11k, "Include/Textures/Frames/film11k.png")
    TEXTURE(FrameFilm12, "Include/Textures/Frames/film12.png")

    // Damage ////////////////////////////////////
    TEXTURE(Damages123, "Include/Textures/Damage/d123.jpg")
    TEXTURE(Damages456, "Include/Textures/Damage/d456.jpg")

    // Dirt //////////////////////////////////////
    TEXTURE(DamageDirty, "Include/Textures/Damage/Dirt/dirty.png")
    TEXTURE(DamageFilthy, "Include/Textures/Damage/Dirt/filthy.png")


// FUNCTIONS /////////////////////////////////////

    // Light Leaks ///////////////////////////////
    float3 LeakPass(float3 color, float2 txcoord)
    {
        static const float current_aspect = ScreenSize.z;
        float2 ntex = txcoord * 2.0 - 1.0;
        float is_inside;
        float4 leak;

        // Setup texture coordinates /////////////
        [flatten]
        if(BORDER_RATIO < current_aspect) ntex.x *= current_aspect / BORDER_RATIO;
        else ntex.y /= current_aspect / BORDER_RATIO;

        ntex  = ntex * 0.5 + 0.5;
        is_inside = all(saturate(1.0 - ntex * ntex));
        ntex  = SYNC_LEAK && ENABLE_BORDER ? ntex : txcoord;
        is_inside = SYNC_LEAK && ENABLE_BORDER ? is_inside : 1;

        // Grab the texture //////////////////////
        if     (LEAK_SELECT == 1)  leak = Leak1.Sample(Sampler1, ntex);
        else if(LEAK_SELECT == 2)  leak = Leak1.Sample(Sampler1, float2(ntex.x, 1.0 - ntex.y));
        else if(LEAK_SELECT == 3)  leak = Leak1.Sample(Sampler1, float2(1.0 - ntex.x, 1.0 - ntex.y));
        else if(LEAK_SELECT == 4)  leak = Leak1.Sample(Sampler1, float2(1.0 - ntex.x, ntex.y));
        else if(LEAK_SELECT == 5)  leak = Leak2.Sample(Sampler1, ntex);
        else if(LEAK_SELECT == 6)  leak = Leak2.Sample(Sampler1, float2(1.0 - ntex.x, ntex.y));
        else if(LEAK_SELECT == 7)  leak = Leak3.Sample(Sampler1, ntex);
        else if(LEAK_SELECT == 8)  leak = Leak3.Sample(Sampler1, float2(1.0 - ntex.x, ntex.y));
        else if(LEAK_SELECT == 9)  leak = Leak4.Sample(Sampler1, ntex);
        else if(LEAK_SELECT == 10) leak = Leak4.Sample(Sampler1, float2(1.0 - ntex.x, ntex.y));
        else if(LEAK_SELECT == 11) leak = Leak5.Sample(Sampler1, ntex);
        else if(LEAK_SELECT == 12) leak = Leak5.Sample(Sampler1, float2(1.0 - ntex.x, ntex.y));

        // Gamma adjustments for width ///////////
        leak = pow(leak, lerp(10.0, 1.0, (LEAK_WIDTH * 0.01)));

        // Desaturate if uusing mono pack
        if(LUT_PACK == 5) leak = dot(leak, float3(0.2126, 0.7152, 0.0722));

        // Blend bg with leak texture ////////////
        return BlendScreenf(color, lerp(0.0, leak, is_inside * (LEAK_AMOUNT * 0.01)));
    }


    // Frames ////////////////////////////////////
    float3 FramePass(float3 color, float2 txcoord)
    {
        // Grab the texture //////////////////////
        float4 frame;
        if(FRAME_SELECT == 1) frame = FrameFilm1.Sample(Sampler1, txcoord);
        else if(FRAME_SELECT == 2)  frame = FrameFilm2.Sample(Sampler1, txcoord);
        else if(FRAME_SELECT == 3)  frame = FrameFilm3.Sample(Sampler1, txcoord);
        else if(FRAME_SELECT == 4)  frame = FrameFilm4.Sample(Sampler1, txcoord);
        else if(FRAME_SELECT == 5)  frame = FrameFilm5.Sample(Sampler1, txcoord);
        else if(FRAME_SELECT == 6)  frame = FrameFilm6.Sample(Sampler1, txcoord);
        else if(FRAME_SELECT == 7)  frame = FrameFilm7.Sample(Sampler1, txcoord);
        else if(FRAME_SELECT == 8)  frame = FrameFilm8.Sample(Sampler1, txcoord);
        else if(FRAME_SELECT == 9)  frame = FrameFilm9.Sample(Sampler1, txcoord);
        else if(FRAME_SELECT == 10) frame = FrameFilm10k.Sample(Sampler1, txcoord);
        else if(FRAME_SELECT == 11) frame = FrameFilm11k.Sample(Sampler1, txcoord);
        else if(FRAME_SELECT == 12) frame = FrameFilm12.Sample(Sampler1, txcoord);

        // Overlay onto bg with transparency /////
        return lerp(color, frame, frame.a);
    }


    // Surface Damage ////////////////////////////
    float3 DamagePass(float3 color, float2 txcoord)
    {
        // Grab the texture //////////////////////
        float4 damage;
        float4 damage123 = Damages123.Sample(Sampler1, txcoord);
        float4 damage456 = Damages456.Sample(Sampler1, txcoord);

        if     (DAMAGE_SELECT == 1) damage = damage123.r;
        else if(DAMAGE_SELECT == 2) damage = damage123.g;
        else if(DAMAGE_SELECT == 3) damage = damage123.b;
        else if(DAMAGE_SELECT == 4) damage = damage456.r;
        else if(DAMAGE_SELECT == 5) damage = damage456.g;
        else if(DAMAGE_SELECT == 6) damage = damage456.b;

        // Blend bg with damage texture //////////
        return BlendOverlay(lerp(0.5, damage, (DAMAGE_INTENSITY * 0.01)), color);
    }

    // Dirt //////////////////////////////////////
    float3 DirtPass(float3 color, float2 txcoord)
    {
        // Grab the texture //////////////////////
        float4 dirt;
        if(DIRT_SELECT == 1) dirt = DamageDirty.Sample(Sampler1, txcoord);
        else if(DIRT_SELECT == 2) dirt = DamageDirty.Sample(Sampler1, float2(txcoord.x, 1.0 - txcoord.y));
        else if(DIRT_SELECT == 3) dirt = DamageDirty.Sample(Sampler1, float2(1.0 - txcoord.x, txcoord.y));
        else if(DIRT_SELECT == 4) dirt = DamageDirty.Sample(Sampler1, float2(1.0 - txcoord.x, 1.0 - txcoord.y));
        else if(DIRT_SELECT == 5) dirt = DamageFilthy.Sample(Sampler1, txcoord);
        else if(DIRT_SELECT == 6) dirt = DamageFilthy.Sample(Sampler1, float2(txcoord.x, 1.0 - txcoord.y));
        else if(DIRT_SELECT == 7) dirt = DamageFilthy.Sample(Sampler1, float2(1.0 - txcoord.x, txcoord.y));
        else if(DIRT_SELECT == 8) dirt = DamageFilthy.Sample(Sampler1, float2(1.0 - txcoord.x, 1.0 - txcoord.y));

        // Overlay onto bg ///////////////////////
        return color * pow(dirt, lerp(0.35, 2.0, (DIRT_INTENSITY * 0.01)));
    }
