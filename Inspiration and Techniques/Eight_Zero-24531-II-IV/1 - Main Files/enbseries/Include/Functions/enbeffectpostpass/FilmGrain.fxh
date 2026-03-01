//-----------------------GRAIN-------------------------//
// martinsh:          Author of Film Grain             //
// Angelo Gonzalez:   port to ReShade                  //
// roxahris:          port to ENB                      //
//+++++++++++++++++++++++++++++++++++++++++++++++++++++//

#define coloramount35mm 0.50
#define grainsize35mm 2.20
#define lumamount35mm 0.25
//#define grainamount35mm (GRAIN_AMOUNT * 0.001)
//#define grainamount35mm 0.030

#define coloramountVHS 2.00
#define grainsizeVHS 4.00
#define lumamountVHS 0.12
#define grainamountVHS 0.011

// a random texture generator, but you can also use a pre-computed perturbation texture
float4 rnm(in float2 tc)
  {
    float noise = sin(dot(float3(tc.x, tc.y, Timer.x*16777216), float3(12.9898, 78.233, 0.0025216))) * 43758.5453;

    float noiseR =  frac(noise)*2.0-1.0;
    float noiseG =  frac(noise*1.2154)*2.0-1.0;
    float noiseB =  frac(noise*1.3453)*2.0-1.0;
    float noiseA =  frac(noise*1.3647)*2.0-1.0;

    return float4(noiseR,noiseG,noiseB,noiseA);
    }

  float fade(in float t)
  {
    return t*t*t*(t*(t*6.0-15.0)+10.0);
  }

  float pnoise3D(in float3 p)
  {
    static const float permTexUnit = 1.0/256.0;        // Perm texture texel-size
    static const float permTexUnitHalf = 0.5/256.0;    // Half perm texture texel-size
    float3 pi = permTexUnit*floor(p)+permTexUnitHalf; // Integer part, scaled so +1 moves permTexUnit texel
    // and offset 1/2 texel to sample texel centers
    float3 pf = frac(p);     // Fractional part for interpolation

    // Noise contributions from (x=0, y=0), z=0 and z=1
    float perm00 = rnm(pi.xy).a ;
    float3  grad000 = rnm(float2(perm00, pi.z)).rgb * 4.0 - 1.0;
    float n000 = dot(grad000, pf);
    float3  grad001 = rnm(float2(perm00, pi.z + permTexUnit)).rgb * 4.0 - 1.0;
    float n001 = dot(grad001, pf - float3(0.0, 0.0, 1.0));

    // Noise contributions from (x=0, y=1), z=0 and z=1
    float perm01 = rnm(pi.xy + float2(0.0, permTexUnit)).a ;
    float3  grad010 = rnm(float2(perm01, pi.z)).rgb * 4.0 - 1.0;
    float n010 = dot(grad010, pf - float3(0.0, 1.0, 0.0));
    float3  grad011 = rnm(float2(perm01, pi.z + permTexUnit)).rgb * 4.0 - 1.0;
    float n011 = dot(grad011, pf - float3(0.0, 1.0, 1.0));

    // Noise contributions from (x=1, y=0), z=0 and z=1
    float perm10 = rnm(pi.xy + float2(permTexUnit, 0.0)).a ;
    float3  grad100 = rnm(float2(perm10, pi.z)).rgb * 4.0 - 1.0;
    float n100 = dot(grad100, pf - float3(1.0, 0.0, 0.0));
    float3  grad101 = rnm(float2(perm10, pi.z + permTexUnit)).rgb * 4.0 - 1.0;
    float n101 = dot(grad101, pf - float3(1.0, 0.0, 1.0));

    // Noise contributions from (x=1, y=1), z=0 and z=1
    float perm11 = rnm(pi.xy + float2(permTexUnit, permTexUnit)).a ;
    float3  grad110 = rnm(float2(perm11, pi.z)).rgb * 4.0 - 1.0;
    float n110 = dot(grad110, pf - float3(1.0, 1.0, 0.0));
    float3  grad111 = rnm(float2(perm11, pi.z + permTexUnit)).rgb * 4.0 - 1.0;
    float n111 = dot(grad111, pf - float3(1.0, 1.0, 1.0));

    // Blend contributions along x
    float4 n_x = lerp(float4(n000, n001, n010, n011), float4(n100, n101, n110, n111), fade(pf.x));

    // Blend contributions along y
    float2 n_xy = lerp(n_x.xy, n_x.zw, fade(pf.y));

    // Blend contributions along z
    float n_xyz = lerp(n_xy.x, n_xy.y, fade(pf.z));

    // We're done, return the final noise value.
    return n_xyz;
    }

    //2d coordinate orientation thing
    float2 coordRot(in float2 tc, in float angle)
    {
    #define aspectr ScreenSize.z
    float rotX = ((tc.x*2.0-1.0)*aspectr*cos(angle)) - ((tc.y*2.0-1.0)*sin(angle));
    float rotY = ((tc.y*2.0-1.0)*cos(angle)) + ((tc.x*2.0-1.0)*aspectr*sin(angle));
    rotX = ((rotX/aspectr)*0.5+0.5);
    rotY = rotY*0.5+0.5;
    return float2(rotX,rotY);
    #undef aspectr
    }

    float3 GrainPass35mm(float2 position, float3 col)
    {
    float3 grey = 0.5;
    float width  = ScreenSize.x; // Would #define be better?
    float height = ScreenSize.x/ScreenSize.z;

    float3 rotOffset = float3(1.425,3.892,5.835); //rotation offset values
    float2 rotCoordsR = coordRot(position, Timer.x*16777216 + rotOffset.x);
    float2 rot = rotCoordsR*float2(width/grainsize35mm,height/grainsize35mm);
    float pNoise = pnoise3D(float3(rot.x,rot.y,0.0));
    float3 noise = float3(pNoise, pNoise, pNoise);

        float2 rotCoordsG = coordRot(position, Timer.x*16777216 + rotOffset.y);
        float2 rotCoordsB = coordRot(position, Timer.x*16777216 + rotOffset.z);
        noise.g = lerp(noise.r,pnoise3D(float3(rotCoordsG*float2(width/grainsize35mm,height/grainsize35mm),1.0)),coloramount35mm);
        noise.b = lerp(noise.r,pnoise3D(float3(rotCoordsB*float2(width/grainsize35mm,height/grainsize35mm),2.0)),coloramount35mm);


    //noisiness response curve based on scene luminance
    float3 lumcoeff = float3(0.299,0.587,0.114);
    float luminance = lerp(0.0,dot(col, lumcoeff),lumamount35mm);
    float lum = smoothstep(0.2,0.0,luminance);
    lum += luminance;

    float2 thepow = pow(lum, 4.0);

    noise = lerp(noise,float3(0.0, 0.0, 0.0),pow(lum,4.0));
    grey += noise * (GRAIN_AMOUNT * 0.001);
    col = BlendSoftLightf(col, grey);

    return float4(col,1.0);

    //return noise;
}

float3 GrainPassVHS(float2 position, float3 col)
{

float width  = ScreenSize.x; // Would #define be better?
float height = ScreenSize.x/ScreenSize.z;

float3 rotOffset = float3(1.425,3.892,5.835); //rotation offset values
float2 rotCoordsR = coordRot(position, Timer.x*16777216 + rotOffset.x);
float2 rot = rotCoordsR*float2(width/grainsizeVHS,height/grainsizeVHS);
float pNoise = pnoise3D(float3(rot.x,rot.y,0.0));
float3 noise = float3(pNoise, pNoise, pNoise);

    float2 rotCoordsG = coordRot(position, Timer.x*16777216 + rotOffset.y);
    float2 rotCoordsB = coordRot(position, Timer.x*16777216 + rotOffset.z);
    noise.g = lerp(noise.r,pnoise3D(float3(rotCoordsG*float2(width/grainsizeVHS,height/grainsizeVHS),1.0)),coloramountVHS);
    noise.b = lerp(noise.r,pnoise3D(float3(rotCoordsB*float2(width/grainsizeVHS,height/grainsizeVHS),2.0)),coloramountVHS);

//noisiness response curve based on scene luminance
float3 lumcoeff = float3(0.299,0.587,0.114);
float luminance = lerp(0.0,dot(col, lumcoeff),lumamountVHS);
float lum = smoothstep(0.2,0.0,luminance);
lum += luminance;

float2 thepow = pow(lum, 4.0);

noise = lerp(noise,float3(0.0, 0.0, 0.0),pow(lum,4.0));
col += noise*grainamountVHS;

return float4(col,1.0);


//return noise;
}
