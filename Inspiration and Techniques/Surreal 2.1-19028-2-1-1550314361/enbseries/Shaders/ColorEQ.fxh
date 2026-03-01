//====================//
// Color EQ by Prod80 //
//====================//
/*
UI_SEPARATOR_CUSTOM("\xB6 Color EQ \xB6")
UI_FLOAT_DNI(fxcolorMix,           "Equalizer Intensity",      0.0, 1.0,  0.1)
UI_FLOAT_DNI(hueMid,               "Hue",                      0.0, 1.0, 0.5)
UI_FLOAT_DNI(hueRange,             "Hue Range",                0.0, 1.0, 0.5) 

#define fxcolorMix 0.2
#define hueMid     0.4 */
#define hueRange 0.45

float smootherstep(float edge0, float edge1, float x)
{
   x = clamp((x - edge0)/(edge1 - edge0), 0.0, 1.0);
   return x*x*x*(x*(x*6 - 15) + 10);
}

float Hue(float3 color)
{
   float hue = 0.0f;
   float fmin = min(min(color.r, color.g), color.b);
   float fmax = max(max(color.r, color.g), color.b);
   float delta = fmax - fmin;
   
   if (delta == 0.0)
      hue = 0.0;
   else
   {         
      float deltaR = (((fmax - color.r) / 6.0) + (delta / 2.0)) / delta;
      float deltaG = (((fmax - color.g) / 6.0) + (delta / 2.0)) / delta;
      float deltaB = (((fmax - color.b) / 6.0) + (delta / 2.0)) / delta;

      if (color.r == fmax )
         hue = deltaB - deltaG;
      else if (color.g == fmax)
         hue = (1.0 / 3.0) + deltaR - deltaB;
      else if (color.b == fmax)
         hue = (2.0 / 3.0) + deltaG - deltaR;
   }
      
   if (hue < 0.0)
      hue += 1.0f;
   else if (hue > 1.0)
      hue -= 1.0f;
   return hue;
}

float4 ColorEQ(float4 color)
{
   float3 fxcolor = saturate( color.xyz );
   float greyVal  = grayValue( fxcolor.xyz );
   float colorHue = Hue( fxcolor.xyz );
   
   float colorSat = 0.0f;
   float minColor = min( min ( fxcolor.x, fxcolor.y ), fxcolor.z );
   float maxColor = max( max ( fxcolor.x, fxcolor.y ), fxcolor.z );
   float colorDelta = maxColor - minColor;
   float colorInt = ( maxColor + minColor ) * 0.5f;
   
   if ( colorDelta != 0.0f )
   {
      if ( colorInt < 0.5f )
         colorSat = colorDelta / ( maxColor + minColor );
      else
         colorSat = colorDelta / ( 2.0f - maxColor - minColor );
   }
   
   colorSat = 1.0f;
   
   float hueMin_1 = 0.0f;
   float hueMin_2 = 0.0f;
   float hueMax_1 = 0.0f;
   float hueMax_2 = 0.0f;
   
	   if ( hueRange > hueMid )
	   {
		  hueMin_1 = hueMid - hueRange;
		  hueMin_2 = 1.0f + hueMid - hueRange;
		  hueMax_1 = hueMid + hueRange;
		  hueMax_2 = 1.0f + hueMid;
	   
		  if ( colorHue >= hueMin_1 && colorHue <= hueMid )
			 fxcolor.xyz = lerp( greyVal.xxx, fxcolor.xyz, smootherstep( hueMin_1, hueMid, colorHue ) * ( colorSat * satLimit ));
		  else if ( colorHue > hueMid && colorHue <= hueMax_1 )
			 fxcolor.xyz = lerp( greyVal.xxx, fxcolor.xyz, ( 1.0f - smootherstep( hueMid, hueMax_1, colorHue )) * ( colorSat * satLimit ));
		  else if ( colorHue >= hueMin_2 && colorHue <= hueMax_2 )
			 fxcolor.xyz = lerp( greyVal.xxx, fxcolor.xyz, smootherstep( hueMin_2, hueMax_2, colorHue ) * ( colorSat * satLimit ));
		  else
			 fxcolor.xyz = greyVal.xxx;
	   
	   }
	   else if ( hueMid + hueRange > 1.0f )
	   {
		  hueMin_1 = hueMid - hueRange;
		  hueMin_2 = 0.0f - ( 1.0f - hueMid );
		  hueMax_1 = hueMid + hueRange;
		  hueMax_2 = hueMid + hueRange - 1.0f;
	   
		  if ( colorHue >= hueMin_1 && colorHue <= hueMid )
			 fxcolor.xyz = lerp( greyVal.xxx, fxcolor.xyz, smootherstep( hueMin_1, hueMid, colorHue ) * ( colorSat * satLimit ));
		  else if ( colorHue > hueMid && colorHue <= hueMax_1 )
			 fxcolor.xyz = lerp( greyVal.xxx, fxcolor.xyz, ( 1.0f - smootherstep( hueMid, hueMax_1, colorHue )) * ( colorSat * satLimit ));
		  else if ( colorHue >= hueMin_2 && colorHue <= hueMax_2 )
			 fxcolor.xyz = lerp( greyVal.xxx, fxcolor.xyz, smootherstep( hueMin_2, hueMax_2, colorHue) * ( colorSat * satLimit ));
		  else
			 fxcolor.xyz = greyVal.xxx;
		  
	   }
   else
   {
      hueMin_1 = hueMid - hueRange;
      hueMax_1 = hueMid + hueRange;
      
      if ( colorHue >= hueMin_1 && colorHue <= hueMid )
         fxcolor.xyz = lerp( greyVal.xxx, fxcolor.xyz, smootherstep( hueMin_1, hueMid, colorHue ) * ( colorSat * satLimit ));
      else if ( colorHue > hueMid && colorHue <= hueMax_1 )
         fxcolor.xyz = lerp( greyVal.xxx, fxcolor.xyz, ( 1.0f - smootherstep( hueMid, hueMax_1, colorHue )) * ( colorSat * satLimit ));
      else
         fxcolor.xyz = greyVal.xxx;
   
   }
  
  color.xyz = lerp( color.xyz, fxcolor.xyz, fxcolorMix );
  return color;
}
