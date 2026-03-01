//+++++++++++++++++++++++++++++++++++++++++++++++++++++//
//            Contains Game Post-Processing            //
//           used by the Modular Shader files          //
//-----------------------CREDITS-----------------------//
// Boris: For ENBSeries and his knowledge and codes    //
// JawZ: Author and developer of this file             //
//+++++++++++++++++++++++++++++++++++++++++++++++++++++//


// This helper file is specifically only for use in the enbeffect.fx!
// The below list is only viable if the msHelpers.fxh is loaded/included into the enbeffect.fx file!


/***List of available fetches**********************************************
 * - PI // value of PI                                                    *
 * - GreyScale(_s4, IN.txcoord0.xy)                                       *
 * - AvgLuma(color.rgb).x  // or .y or .z or .w, never ever .xyzw!        *
 * - LogLuma(color.rgb)                                                   *
 * - random(uv.xy)                                                        *
 * - RGBtoXYZ(color.rgb)                                                  *
 * - XYZtoYxy(XYZ.xyz)                                                    *
 * - YxytoXYZ(XYZ.xyz, Yxy.rgb)                                           *
 * - XYZtoRGB(XYZ.xyz)                                                    *
 * - RGBToHSL(color.rgb)                                                  *
 * - HSLToRGB(hsl.rgb)                                                    *
 * - RGBtoHSV(color.rgb)                                                  *
 * - HSVtoRGB(hsv.rgb)                                                    *
 * - BlendLuma(hslbase.rgb, hslblend.rgb)                                 *
 * - SplitScreen(_s0, color, IN.txcoord0.xy, fSplitscreenPos)             *
 * - ClipMode(color.rgb)                                                  *
 **************************************************************************/


// ------------------- //
//   GUI ANNOTATIONS   //
// ------------------- //




// ------------------- //
//   HELPER CONSTANTS  //
// ------------------- //




// ------------------- //
//   HELPER FUNCTIONS  //
// ------------------- //

float4 enbAGCC(float4 inColor, float2 inCoords)
{
/// Fallout4 vanilla post process. Just an example for modders, better not enable without know how to edit and what you need

// Combines bloom, adaptation
    float2 bloom_offset = Params01[4].zw;
    float4 r0, r1, r2, r3;
    r0.xyz = inColor.xyz;
    r1.xy  = Params01[4].zw * inCoords.xy;
    r1.xyz = TextureBloom.Sample(Sampler1, bloom_offset * inCoords.xy).rgb * ENBParams01.x;
    r0.w   = TextureAdaptation.Sample(Sampler0, inCoords.xy).x;
    r1.w   = Params01[1].z / (0.001 + r0.w);
    r2.x   = r1.w < Params01[1].y;
    r1.w   = r2.x ? Params01[1].y : r1.w;
    r2.x   = Params01[1].x < r1.w;
    r1.w   = r2.x ? Params01[1].x : r1.w;
    r0.xyz = r1.xyz + r0.xyz;
    r0.xyz = r0.xyz * r1.w;
// returns color_adapt

// Filmic tonemapper
    r1.xyz = r0.xyz + r0.xyz;
    r2.xyz = r0.xyz * 0.3 + 0.05;
    r3.xy  = float2(0.2, 3.333333) * Params01[1].w;
    r2.xyz = r1.xyz * r2.xyz + r3.x;
    r0.xyz = r0.xyz * 0.3 + 0.5;
    r0.xyz = r1.xyz * r0.xyz + 0.06;
    r0.xyz = r2.xyz / r0.xyz;
    r0.xyz = -Params01[1].w * 3.333333 + r0.xyz;
    r1.x   = Params01[1].w * 0.2 + 19.376;
    r1.x   = r1.x * 0.0408564 - r3.y;
    r1.xyz = r0.xyz / r1.x;
// returns filmic result

// Post process
    r0.x    = dot(r1.xyz, float3(0.2125, 0.7154, 0.0721));
    r1.xyz  = r1.xyz - r0.x;
    r1.xyz  = Params01[2].x * r1.xyz + r0.x;
    r2.xyz  = r0.x * Params01[3].xyz - r1.xyz;
    r1.xyz  = Params01[3].w * r2.xyz + r1.xyz;
    r1.xyz  = Params01[2].w * r1.xyz - r0.w;
    r0.xyz  = Params01[2].z * r1.xyz + r0.w;
    inColor.xyz = lerp(r0.xyz, Params01[5].xyz, Params01[5].w);  /// Last color filter used only for certain conditions, like rifle night scope

    inColor.xyz=saturate(inColor.xyzw);   /// Clamps target within range of 0-1, to not cause clipping of low and high color values

  return inColor.xyzw;
}
