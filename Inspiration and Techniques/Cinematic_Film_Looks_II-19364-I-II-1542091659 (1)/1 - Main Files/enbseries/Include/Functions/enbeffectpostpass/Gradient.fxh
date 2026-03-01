/*-----------------------------------------------------------.
/                         Gradient                           /
/            Gradient overlay effect by smack0007            /
/              (ported and modified by IDDQD)\n              /
/							(modified further by roxahris)   /
'-----------------------------------------------------------*/

    float3 GradientRGBToHSL(float3 color) {
    	float3 hsl;  // init to 0 to avoid warnings ? (and reverse if + remove first part)

    	float fmin = min(min(color.r, color.g), color.b);    //Min. value of RGB
    	float fmax = max(max(color.r, color.g), color.b);    //Max. value of RGB
    	float delta = fmax - fmin;              //Delta RGB value

    	hsl.z = (fmax + fmin) / 2.0;  // Luminance

    	if (delta == 0.0) {
    		hsl.x = 0.0; 	// Hue
    		hsl.y = 0.0; 	// Saturation
    	} else {
        	if (hsl.z < 0.5) {
        		hsl.y = delta / (fmax + fmin);
            }
        	else {
        		hsl.y = delta / (2.0 - fmax - fmin);
            }

        	float deltaR = (((fmax - color.r) / 6.0) + (delta / 2.0)) / delta;
        	float deltaG = (((fmax - color.g) / 6.0) + (delta / 2.0)) / delta;
        	float deltaB = (((fmax - color.b) / 6.0) + (delta / 2.0)) / delta;

        	if (color.r == fmax )
        		hsl.x = deltaB - deltaG;  // Hue
        	else if (color.g == fmax)
        		hsl.x = (1.0 / 3.0) + deltaR - deltaB;  // Hue
        	else if (color.b == fmax)
        		hsl.x = (2.0 / 3.0) + deltaG - deltaR;  // Hue

        	if (hsl.x < 0.0) hsl.x += 1.0;  // Hue
        	if (hsl.x > 1.0) hsl.x -= 1.0;  // Hue
        }

    	return hsl;
    }

    float GradientHueToRGB(float f1, float f2, float hue) {
    	if (hue < 0.0) {
    		hue += 1.0;
        } else if (hue > 1.0) {
    		hue -= 1.0;
        }

    	float res;
    	if ((6.0 * hue) < 1.0) {
    		res = f1 + (f2 - f1) * 6.0 * hue;
        } else if ((2.0 * hue) < 1.0) {
    		res = f2;
        } else if ((3.0 * hue) < 2.0) {
    		res = f1 + (f2 - f1) * ((2.0 / 3.0) - hue) * 6.0;
        } else {
    		res = f1;
        }

    	return res;
    }

    float3 GradientHSLToRGB(float3 hsl) {
    	float3 rgb;

    	if (hsl.y == 0.0) {
    		rgb = float3(hsl.z, hsl.z, hsl.z);
        } else {
    		float f2;

    		if (hsl.z < 0.5) {
    			f2 = hsl.z * (1.0 + hsl.y);
            } else {
    			f2 = (hsl.z + hsl.y) - (hsl.y * hsl.z);
            }

    		float f1 = 2.0 * hsl.z - f2;

    		rgb.r = GradientHueToRGB(f1, f2, hsl.x + (1.0/3.0));
    		rgb.g = GradientHueToRGB(f1, f2, hsl.x);
    		rgb.b= GradientHueToRGB(f1, f2, hsl.x - (1.0/3.0));
    	}

    	return rgb;
    }

    // Luminosity Blend mode creates the result color by combining the hue and saturation of the base color with the luminance of the blend color.
    float3 GradientBlendLuminosity(float3 base, float3 blend) {
    	float3 baseHSL = GradientRGBToHSL(base);
    	return GradientHSLToRGB(float3(baseHSL.r, baseHSL.g, GradientRGBToHSL(base).b));
    }

    float3 GradientPass(float3 color, float2 texcoord : TEXCOORD) : SV_Target {
        float4 Maincolor = float4(0,0,0,0);
        if (ENABLE_GRADIENT == 0) return color;

        float2 othogonal = normalize(float2(tan((clamp(0.00001, 179.99999, (GRADIENT_ROTATION - 90))) * 0.0174533), - ScreenSize.z));
        float TS_Dist = abs(dot(texcoord.xy - 0.5 - othogonal * ((GRADIENT_SHIFT) * 0.01), othogonal));

        if(GRADIENT_COLORS == 3) {
            Maincolor.rgb = lerp( GRADIENT_TOP, GRADIENT_BOTTOM, smoothstep(0.0 + (0.5 - (GRADIENT_RANGE * 0.005)), 1.0 - (0.5 - (GRADIENT_RANGE * 0.005)), TS_Dist));
        } else if(GRADIENT_COLORS == 1) {
            Maincolor.rgb = lerp( GRADIENT_TOP, 0.5, smoothstep(0.0 + (0.5 - (GRADIENT_RANGE * 0.005)), 1.0 - (0.5 - (GRADIENT_RANGE * 0.005)), TS_Dist));
        } else if(GRADIENT_COLORS == 2) {
            Maincolor.rgb = lerp( 0.5, GRADIENT_BOTTOM, smoothstep(0.0 + (0.5 - (GRADIENT_RANGE * 0.005)), 1.0 - (0.5 - (GRADIENT_RANGE * 0.005)), TS_Dist));
        }

        if (GRADIENT_MODE == 1) {
            Maincolor.rgb = BlendSoftLightf(color.rgb,Maincolor.rgb);
        } else if (GRADIENT_MODE == 2) {
            Maincolor.rgb = BlendOverlayf(color.rgb,Maincolor.rgb);
        } else if (GRADIENT_MODE == 3) {
            Maincolor.rgb = BlendVividLightf(color.rgb,Maincolor.rgb);
        } else if (GRADIENT_MODE == 4) {
            Maincolor.rgb = BlendLinearLightf(color.rgb,Maincolor.rgb);
        } else if (GRADIENT_MODE == 5) {
            Maincolor.rgb = BlendColorDodgef(color.rgb,Maincolor.rgb);
        }

        color.rgb = lerp(color.rgb,Maincolor.rgb,(GRADIENT_OPACITY * 0.01));

        return color;
    }
