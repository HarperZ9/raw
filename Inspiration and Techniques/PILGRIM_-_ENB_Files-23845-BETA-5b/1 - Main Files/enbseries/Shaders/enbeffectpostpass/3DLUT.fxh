/// For Day, Night and Interior time and location Seperation
Texture2D GradeLutD < string ResourceName="Textures/LUTS/PILGRIMd.png"; >;
Texture2D GradeLutN < string ResourceName="Textures/LUTS/PILGRIMn.png"; >;
Texture2D GradeLutI < string ResourceName="Textures/LUTS/PILGRIMi.png"; >;


// ------------------- //
//   HELPER FUNCTIONS  //
// ------------------- //

float3 LUTfunc(float3 color) {
    float2 CLut_pSize =  1 / float2(4096, 64);
    float4 CLut_UV    = 0.0;
    color      = saturate(color) * 63.0;
    CLut_UV.w  = floor(color.b);
    CLut_UV.xy = (color.rg + 0.5) * CLut_pSize;
    CLut_UV.x += CLut_UV.w * CLut_pSize.y;
    CLut_UV.z  = CLut_UV.x + CLut_pSize.y;

  float3 lutDay      = lerp(GradeLutD.SampleLevel(Sampler1, CLut_UV.xy, 0).rgb, GradeLutD.SampleLevel(Sampler1, CLut_UV.zy, 0).rgb, color.b - CLut_UV.w);
  float3 lutNight    = lerp(GradeLutN.SampleLevel(Sampler1, CLut_UV.xy, 0).rgb, GradeLutN.SampleLevel(Sampler1, CLut_UV.zy, 0).rgb, color.b - CLut_UV.w);
  float3 lutInterior = lerp(GradeLutI.SampleLevel(Sampler1, CLut_UV.xy, 0).rgb, GradeLutI.SampleLevel(Sampler1, CLut_UV.zy, 0).rgb, color.b - CLut_UV.w);

  return lerp(lerp(lutNight, lutDay, ENightDayFactor), lutInterior, EInteriorFactor);
}
