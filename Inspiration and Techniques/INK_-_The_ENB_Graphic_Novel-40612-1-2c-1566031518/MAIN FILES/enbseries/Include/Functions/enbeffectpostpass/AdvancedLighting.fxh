// ADVANCED LIGHTING /////////////////////////////
// Original "Emphasize" by OtisInf              //
// Original 3D code by SirCobra                 //
// Modifications by TreyM                       //
//////////////////////////////////////////////////

float FocusCoC(float2 coord, int light)
{
    float depth, sat, dif;
    float focus, hor, ver, width, fov;

    switch(light)
    {
        case 1:
            focus = LIGHT_DEPTH_1; // MOUSE_CONTROL ? lerp(0.0, 0.125, 1-tempInfo2.y - 0.175) : LIGHT_DEPTH_1;
            hor   = LIGHT_X_1;     // MOUSE_CONTROL ? lerp(0.0, 1.0,     tempInfo2.x)         : LIGHT_X_1;
            ver   = LIGHT_Y_1;     // MOUSE_CONTROL ? lerp(0.0, 1.0,     tempInfo2.w)         : LIGHT_Y_1;
                                   //ver = MOUSE_CONTROL ? 1-ver : ver;
            sat   = LIGHT_SIZE_1;
        break;

        case 2:
            focus = LIGHT_DEPTH_2;
            hor   = LIGHT_X_2;
            ver   = LIGHT_Y_2;
            sat   = LIGHT_SIZE_2;
        break;

        case 3:
            focus = LIGHT_DEPTH_3;
            hor   = LIGHT_X_3;
            ver   = LIGHT_Y_3;
            sat   = LIGHT_SIZE_3;
        break;
    }

    depth = GetLinearizedDepth(coord);
	focus = focus;

	coord.x   = (coord.x - hor) * Resolution.x;
	coord.y   = (coord.y - lerp(0.5, -0.5, ver - 0.5)) * Resolution.y;

    switch(light)
    {
        case 1:
            width = lerp(180.0, 1.0, (LIGHT_WIDTH_1 * 0.01)) / ScreenSize.x;
        break;

        case 2:
            width = lerp(180.0, 1.0, (LIGHT_WIDTH_2 * 0.01)) / ScreenSize.x;
        break;

        case 3:
            width = lerp(180.0, 1.0, (LIGHT_WIDTH_3 * 0.01)) / ScreenSize.x;
        break;
    }

	fov = sqrt((coord.x * coord.x) + (coord.y * coord.y)) * width;

	dif = sqrt((depth * depth) + (focus * focus) - (2 * depth * focus * cos(fov * (2 * 3.1415927 / 360))));

	return saturate((dif > sat) ? 1.0 : smoothstep(0, sat, dif));
}

float3 Spotlight(float3 color, float2 coord)
{
    float coc1 = pow(FocusCoC(coord.xy, 1), lerp(100.0, 4.0, (BEAM_SOFTNESS_1 * 0.01)));
    float coc2 = pow(FocusCoC(coord.xy, 2), lerp(100.0, 4.0, (BEAM_SOFTNESS_2 * 0.01)));
    float coc3 = pow(FocusCoC(coord.xy, 3), lerp(100.0, 4.0, (BEAM_SOFTNESS_3 * 0.01)));

    if (HARD_LIGHT_1)
    {
        coc1 = floor(coc1);
        coc1 = coc1 / 1.0;
    }

    if (HARD_LIGHT_2)
    {
        coc2 = floor(coc2);
        coc2 = coc2 / 1.0;
    }

    if (HARD_LIGHT_3)
    {
        coc3 = floor(coc3);
        coc3 = coc3 / 1.0;
    }

    float amb  = 1.0 * lerp(1.0, coc1, ENABLE_LIGHT_1) * lerp(1.0, coc2, ENABLE_LIGHT_2) * lerp(1.0, coc3, ENABLE_LIGHT_3);

    amb    = 1-amb;

    color *= 0.5;
    color *= lerp(1.0, amb, AMBIENT_LEVEL);

    if (ENABLE_LIGHT_1) color = lerp(BlendOverlayf(color, LIGHT_COLOR_1), color, coc1);
    if (ENABLE_LIGHT_2) color = lerp(BlendOverlayf(color, LIGHT_COLOR_2), color, coc2);
    if (ENABLE_LIGHT_3) color = lerp(BlendOverlayf(color, LIGHT_COLOR_3), color, coc3);

    if (USE_AMB_COLOR) color = lerp(color, color * AMBIENT_COLOR, 1-amb);

    return saturate(color);
}
