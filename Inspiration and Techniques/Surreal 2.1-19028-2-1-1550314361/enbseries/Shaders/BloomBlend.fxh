// Screen in HDR
float3 LDRToLinear(float3 incol)
{
   float3   res;
   res=1.0/(1.0-incol) - 1.0;
   return res;
}

float3 LinearToLDR(float3 incol)
{
   float3   res;
   res=1.0 - (1.0/(incol+1.0));
   return res;
}

float3 HDRScreen(float3 c, float3 b)
{
	float3   res;
    float3 cx, bx;
    cx=LinearToLDR(c);
    bx=LinearToLDR(b);
    res=1-(1-cx)*(1-bx);
    res=LDRToLinear(res);
    return res;
}