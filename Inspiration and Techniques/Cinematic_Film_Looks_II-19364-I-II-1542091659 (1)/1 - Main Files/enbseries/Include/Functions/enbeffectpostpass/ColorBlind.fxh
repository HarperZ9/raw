// DALTONIZATION SHADER ////////////////////////////
////////////////////////////////////////////////////
// Daltonization algorithm by daltonize.org       //
// Originally ported to ReShade by IDDQD          //
// modified for ReShade 3.0 by crosire            //
// Ported to ENB by TreyM                         //
////////////////////////////////////////////////////

// FUNCTION //////////////////////////////////////
    float3 ColorBlindPass(float3  color)
    {
    	// RGB to LMS matrix conversion
    	float OnizeL = (17.8824f *  color.r) + (43.5161f *  color.g) + (4.11935f *  color.b);
    	float OnizeM = (3.45565f *  color.r) + (27.1554f *  color.g) + (3.86714f *  color.b);
    	float OnizeS = (0.0299566f *  color.r) + (0.184309f *  color.g) + (1.46709f *  color.b);

    	// Simulate color blindness
    	float Daltl, Daltm, Dalts;

    	if (CB_MODE == 1) // Protanopia - reds are greatly reduced (1% men)
    	{
    		Daltl = 0.0f * OnizeL + 2.02344f * OnizeM + -2.52581f * OnizeS;
    		Daltm = 0.0f * OnizeL + 1.0f * OnizeM + 0.0f * OnizeS;
    		Dalts = 0.0f * OnizeL + 0.0f * OnizeM + 1.0f * OnizeS;
    	}
    	else if (CB_MODE == 2) // Deuteranopia - greens are greatly reduced (1% men)
    	{
    		Daltl = 1.0f * OnizeL + 0.0f * OnizeM + 0.0f * OnizeS;
    		Daltm = 0.494207f * OnizeL + 0.0f * OnizeM + 1.24827f * OnizeS;
    		Dalts = 0.0f * OnizeL + 0.0f * OnizeM + 1.0f * OnizeS;
    	}
    	else if (CB_MODE == 3) // Tritanopia - blues are greatly reduced (0.003% population)
    	{
    		Daltl = 1.0f * OnizeL + 0.0f * OnizeM + 0.0f * OnizeS;
    		Daltm = 0.0f * OnizeL + 1.0f * OnizeM + 0.0f * OnizeS;
    		Dalts = -0.395913f * OnizeL + 0.801109f * OnizeM + 0.0f * OnizeS;
    	}

    	// LMS to RGB matrix conversion
    	float3 error;
    	error.r = (0.0809444479f * Daltl) + (-0.130504409f * Daltm) + (0.116721066f * Dalts);
    	error.g = (-0.0102485335f * Daltl) + (0.0540193266f * Daltm) + (-0.113614708f * Dalts);
    	error.b = (-0.000365296938f * Daltl) + (-0.00412161469f * Daltm) + (0.693511405f * Dalts);

    	// Isolate invisible colors to color vision deficiency (calculate error matrix)
    	error = ( color - error);

    	// Shift colors towards visible spectrum (apply error modifications)
    	float3 correction;
    	correction.r = 0; // (error.r * 0.0) + (error.g * 0.0) + (error.b * 0.0);
    	correction.g = (error.r * 0.7) + (error.g * 1.0); // + (error.b * 0.0);
    	correction.b = (error.r * 0.7) + (error.b * 1.0); // + (error.g * 0.0);

    	// Add compensation to original values
    	correction =  color + correction;

    	return correction;
    }
