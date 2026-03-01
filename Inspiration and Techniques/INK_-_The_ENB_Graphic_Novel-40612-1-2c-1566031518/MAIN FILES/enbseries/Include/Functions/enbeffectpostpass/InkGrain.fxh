//-----------------------GRAIN-------------------------//
// martinsh:          Author of Film Grain             //
// Angelo Gonzalez:   port to ReShade                  //
// roxahris:          port to ENB                      //
//+++++++++++++++++++++++++++++++++++++++++++++++++++++//

// a random texture generator, but you can also use a pre-computed perturbation texture
float4 rnm(in float2 tc)
{
    float noise  = sin(dot(float3(tc.x, tc.y, Timer.x * 16777216), float3(12.9898, 78.233, 0.0025216))) * 43758.5453;

    float noiseR = frac(noise) * 2.0 - 1.0;
    float noiseG = frac(noise * 1.2154) * 2.0 - 1.0;
    float noiseB = frac(noise * 1.3453) * 2.0 - 1.0;
    float noiseA = frac(noise * 1.3647) * 2.0 - 1.0;

    return float4(noiseR, noiseG, noiseB, noiseA);
}

float fade(in float t)
{
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

float pnoise3D(in float3 p)
{
    static const float permTexUnit = 1.0 / 256.0;        // Perm texture texel-size
    static const float permTexUnitHalf = 0.5 / 256.0;    // Half perm texture texel-size
    float3 pi      = permTexUnit * floor(p) + permTexUnitHalf; // Integer part, scaled so +1 moves permTexUnit texel
    // and offset 1/2 texel to sample texel centers
    float3 pf      = frac(p);     // Fractional part for interpolation

    // Noise contributions from (x=0, y=0), z=0 and z=1
    float  perm00  = rnm(pi.xy).a ;
    float3 grad000 = rnm(float2(perm00, pi.z)).rgb * 4.0 - 1.0;
    float  n000    = dot(grad000, pf);
    float3 grad001 = rnm(float2(perm00, pi.z + permTexUnit)).rgb * 4.0 - 1.0;
    float  n001    = dot(grad001, pf - float3(0.0, 0.0, 1.0));

    // Noise contributions from (x=0, y=1), z=0 and z=1
    float  perm01  = rnm(pi.xy + float2(0.0, permTexUnit)).a ;
    float3 grad010 = rnm(float2(perm01, pi.z)).rgb * 4.0 - 1.0;
    float  n010    = dot(grad010, pf - float3(0.0, 1.0, 0.0));
    float3 grad011 = rnm(float2(perm01, pi.z + permTexUnit)).rgb * 4.0 - 1.0;
    float  n011    = dot(grad011, pf - float3(0.0, 1.0, 1.0));

    // Noise contributions from (x=1, y=0), z=0 and z=1
    float  perm10  = rnm(pi.xy + float2(permTexUnit, 0.0)).a ;
    float3 grad100 = rnm(float2(perm10, pi.z)).rgb * 4.0 - 1.0;
    float  n100    = dot(grad100, pf - float3(1.0, 0.0, 0.0));
    float3 grad101 = rnm(float2(perm10, pi.z + permTexUnit)).rgb * 4.0 - 1.0;
    float  n101    = dot(grad101, pf - float3(1.0, 0.0, 1.0));

    // Noise contributions from (x=1, y=1), z=0 and z=1
    float  perm11  = rnm(pi.xy + float2(permTexUnit, permTexUnit)).a ;
    float3 grad110 = rnm(float2(perm11, pi.z)).rgb * 4.0 - 1.0;
    float  n110    = dot(grad110, pf - float3(1.0, 1.0, 0.0));
    float3 grad111 = rnm(float2(perm11, pi.z + permTexUnit)).rgb * 4.0 - 1.0;
    float  n111    = dot(grad111, pf - float3(1.0, 1.0, 1.0));

    // Blend contributions along x
    float4 n_x     = lerp(float4(n000, n001, n010, n011), float4(n100, n101, n110, n111), fade(pf.x));

    // Blend contributions along y
    float2 n_xy    = lerp(n_x.xy, n_x.zw, fade(pf.y));

    // Blend contributions along z
    float  n_xyz   = lerp(n_xy.x, n_xy.y, fade(pf.z));

    // We're done, return the final noise value.
    return n_xyz;
}

//2d coordinate orientation thing
float2 coordRot(in float2 tc, in float angle)
{
    float rotX, rotY;

    rotX = ((tc.x * 2.0 - 1.0) * ScreenSize.z * cos(angle)) - ((tc.y * 2.0 - 1.0) * sin(angle));
    rotY = ((tc.y*2.0-1.0) * cos(angle)) + ((tc.x * 2.0 - 1.0) * ScreenSize.z * sin(angle));
    rotX = ((rotX / ScreenSize.z) * 0.5 + 0.5);
    rotY = rotY * 0.5 + 0.5;

    return float2(rotX,rotY);
}

float3 InkGrainPass(float3 color, float2 coord, int mode)
{
    float2 rotOffset, rotCoordsR, rot;
    float3 noise, skymask;
    float  pNoise, depth;

    rotOffset  = float3(1.425, 3.892, 5.835); //rotation offset values
    rotCoordsR = coordRot(coord, Timer.x * 16777216 + rotOffset.x);
    rot        = rotCoordsR * float2(ScreenSize.x / 2.0, (ScreenSize.x / ScreenSize.z) / 2.0);
    pNoise     = pnoise3D(float3(rot.x, rot.y, 0.0));
    noise      = float3(pNoise, pNoise, pNoise);

    // Posterize the noise
    noise      = noise * lerp(0.44, 0.66, (GRAIN_AMOUNT * 0.01));
    noise      = floor(noise);
    noise      = noise / 1.0;
    noise      = saturate(noise);

    // Mask out the sky
    depth    = 1-GetLinearizedDepth(coord);
    skymask  = !all(depth);
    skymask  = lerp(0.0, 1-GetLuma(color, Rec709_5), skymask);

    // Only blend noise into dark areas
    switch(mode)
    {
        case 1:
            color = lerp(color, BlendScreenf(color, noise), 1-GetLuma(color, Rec709_5));
        break;

        case 2:
            color = lerp(color, BlendScreenf(color, noise), skymask);
        break;
    }

    return saturate(color);
}
