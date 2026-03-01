//----------------------------------------------------------------------------------------------//
//																								//
//						      Main ENB Depth of Field UI file									//
//						      by LonelyKitsuune aka Skratzer									//
//																								//
//----------------------------------------------------------------------------------------------//

//This file requires my UI Primer header to work!
#ifndef _UI_PRIMER_
#error UI_Primer couldnt be found!
#endif

//----------------------------------------------------------------------------------------------//
//								  User Interface Parameters										//
//																								//
//----------------------------------------------------------------------------------------------//


#if SHADERGROUP == 0

UI_FileHeaderLong(">>>          Mcfly Depth of Field & AO         <<<",
				  ">>>            Modified by Haevaksa                  <<<")
UI_WHITESPACE(1)
float	AOTECH		< string UIName="TECHNIQUE FIX (Be sure to check!!!) ";		string UIWidget="Spinner";	float UIStep=1.0;	float UIMin=0.00;	float UIMax=1.0;	> = {0.00};
float	AOTECHEX		< string UIName="TECHNIQUE : Mcfly ADOF --> Set to 0";		string UIWidget="Spinner";	float UIStep=0.0;	float UIMin=0.00;	float UIMax=0.0;	> = {0.00};
float	AOTECHEXX		< string UIName="TECHNIQUE : Mcfly ADOF + AO --> Set to 1";		string UIWidget="Spinner";	float UIStep=0.0;	float UIMin=0.00;	float UIMax=0.0;	> = {0.00};
float	AOTECHEXXX		< string UIName="TECHNIQUE : Mcfly AO --> Set to 1";		string UIWidget="Spinner";	float UIStep=0.0;	float UIMin=0.00;	float UIMax=0.0;	> = {0.00};
//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 1

#if(iADOF_FocusMode == 0)//Marty McFly (gp65cj04 modified) Focusing
UI_WHITESPACE(18)
int SPERFUCC <string UIName=">>>Focusing<<<";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};	
bool    bADOF_AutofocusEnable 		< string UIName="DOF: Enable Autofocus";> = {true};
bool ENABLE_VisualAuto <string UIName = "Check Auto Focus Location";> = {false};   

float2	fADOF_AutofocusCenter 		< string UIName="DOF: Autofocus sample center"; 	string UIWidget="Spinner"; 	float UIStep=0.01; 	float UIMin=0.00; 	float UIMax=1.00;	> = {0.5,0.5};
int	iADOF_AutofocusSamples		< string UIName="DOF: Autofocus sample count";  	string UIWidget="spinner"; 				int UIMin=0; 		int UIMax=10; 		> = {6};
float	fADOF_AutofocusRadius		< string UIName="DOF: Autofocus sample radius";		string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.00;	float UIMax=0.50;	> = {0.05};
float	fADOF_ManualfocusDepth		< string UIName="DOF: Manual focus depth";		string UIWidget="Spinner";	float UIStep=0.001;	float UIMin=0.00;	float UIMax=1.0;	> = {0.05};
float	fADOF_NearBlurCurve		< string UIName="DOF: Near blur curve";			string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.00;	float UIMax=20.0;	> = {1.0};
float	fADOF_FarBlurCurve		< string UIName="DOF: Far blur curve";			string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.00;	float UIMax=20.0;	> = {1.4};
float	fADOF_NearBlurMult		< string UIName="DOF: Near blur mult";			string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.00;	float UIMax=1.0;	> = {0.0};
float	fADOF_FarBlurMult		< string UIName="DOF: Far blur mult";			string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.00;	float UIMax=1.0;	> = {1.0};
float	fADOF_InfiniteFocus		< string UIName="DOF: Infinite depth distance";		string UIWidget="Spinner";	float UIStep=0.001;	float UIMin=0.00;	float UIMax=1.0;	> = {0.015};
UI_WHITESPACE(16)
bool ENABLE_EI <string UIName = "Use Interior DOF Focusing Parameters";> = {false}; 
bool    bADOF_AutofocusEnableI 		< string UIName="Interior DOF: Enable Autofocus";> = {true};
float2	fADOF_AutofocusCenterI 		< string UIName="Interior DOF: Autofocus sample center"; 	string UIWidget="Spinner"; 	float UIStep=0.01; 	float UIMin=0.00; 	float UIMax=1.00;	> = {0.5,0.5};
int	iADOF_AutofocusSamplesI		< string UIName="Interior DOF: Autofocus sample count";  	string UIWidget="spinner"; 				int UIMin=0; 		int UIMax=10; 		> = {6};
float	fADOF_AutofocusRadiusI		< string UIName="Interior DOF: Autofocus sample radius";		string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.00;	float UIMax=0.50;	> = {0.05};
float	fADOF_ManualfocusDepthI		< string UIName="Interior DOF: Manual focus depth";		string UIWidget="Spinner";	float UIStep=0.001;	float UIMin=0.00;	float UIMax=1.0;	> = {0.05};
float	fADOF_NearBlurCurveI		< string UIName="Interior DOF: Near blur curve";			string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.00;	float UIMax=20.0;	> = {1.0};
float	fADOF_FarBlurCurveI		< string UIName="Interior DOF: Far blur curve";			string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.00;	float UIMax=20.0;	> = {1.4};
float	fADOF_NearBlurMultI		< string UIName="Interior DOF: Near blur mult";			string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.00;	float UIMax=1.0;	> = {0.0};
float	fADOF_FarBlurMultI		< string UIName="Interior DOF: Far blur mult";			string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.00;	float UIMax=1.0;	> = {1.0};
float	fADOF_InfiniteFocusI		< string UIName="Interior DOF: Infinite depth distance";		string UIWidget="Spinner";	float UIStep=0.001;	float UIMin=0.00;	float UIMax=1.0;	> = {0.015};
UI_WHITESPACE(27)
bool ENABLE_Visual <string UIName = "Check Mousefocus Location";> = {false};   
bool ENABLE_Mouse <string UIName = "Use Mousefocus (Right Click)";> = {false}; 
float	fADOF_MousefocusRadius		< string UIName="Mousefocus sample radius";		string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.00;	float UIMax=0.50;	> = {0.05};  
UI_WHITESPACE(17)
//bool ENABLE_EI <string UIName = "Adapt Interior Only DOF";> = {false}; 

UI_WHITESPACE(19)
#endif
#if(iADOF_FocusMode == 1)//Marty McFly Tilt Shift
float	fADOF_TiltShiftPosition		< string UIName="DOF: Tilt Shift Axis Position";	string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=-1.0;	float UIMax=1.0;	> = {0.0};
float	fADOF_TiltShiftWidth		< string UIName="DOF: Tilt Shift Focus Width";		string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=1.0;	> = {0.0};
float	fADOF_TiltShiftRotation		< string UIName="DOF: Tilt Shift Axis Rotation (\xB0)"; string UIWidget="Spinner";	float UIStep=1.00;	float UIMin=0.0;	float UIMax=180.0;	> = {0.0};
float	fADOF_TiltShiftBlurCurve	< string UIName="DOF: Tilt Shift Blur Curve";		string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.01;	float UIMax=10.0;	> = {2.0};
#endif
int DOFBOKESEPER <string UIName=">>>DOF Bokeh<<<";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
bool    bADOF_ShapePreviewWindowEnable	< string UIName="DOF: Enable Bokeh shape preview window";													> = {false};
float	fADOF_BokehCurve		< string UIName="DOF: Bokeh Intensity";			string UIWidget="Spinner";	float UIStep=0.1;	float UIMin=1.0;	float UIMax=10.0;	> = {4.5};

float	fADOF_ShapeRadius		< string UIName="DOF: Bokeh shape max size";		string UIWidget="Spinner";	float UIStep=0.1;	float UIMin=0.0;	float UIMax=100.0;	> = {15.0};
int	iADOF_ShapeVertices		< string UIName="DOF: Bokeh shape vertices";		string UIWidget="spinner";				int UIMin=3;		int UIMax=9;		> = {6};
int	iADOF_ShapeQuality		< string UIName="DOF: Bokeh shape quality";		string UIWidget="spinner";				int UIMin=2;		int UIMax=25;		> = {5};
float	fADOF_ShapeCurvatureAmount	< string UIName="DOF: Bokeh shape curvature";		string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=-1.0;	float UIMax=1.0;	> = {1.0};
float	fADOF_ShapeRotation		< string UIName="DOF: Bokeh shape rotation (\xB0)";	string UIWidget="Spinner";	float UIStep=1;		float UIMin=0;		float UIMax=360;	> = {15};
float	fADOF_ShapeAnamorphRatio	< string UIName="DOF: Bokeh shape aspect ratio";	string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=1.0;	> = {1.0};
#if(bADOF_ShapeChromaEnable != 0)
 float	fADOF_ShapeChromaAmount		< string UIName="DOF: Shape chroma amount";		string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.00;	float UIMax=0.50;	> = {0.15};
#endif
float	fADOF_RenderResolutionMult	< string UIName="DOF: Blur render res mult";		string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.5;	float UIMax=1.0;	> = {0.5};
#if(iADOF_PostBlurMode == 0)
 float	fADOF_SmootheningAmount		< string UIName="DOF: Gaussian postblur width";		string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=5.0;	> = {1.0};
#endif
UI_WHITESPACE(30)
bool ENABLE_IE <string UIName = "Use Interior DOF Bokeh Parameters";> = {false}; 
float	fADOF_BokehCurveI		< string UIName="Interior DOF: Bokeh Intensity";			string UIWidget="Spinner";	float UIStep=0.1;	float UIMin=1.0;	float UIMax=10.0;	> = {4.5};
float	fADOF_ShapeRadiusI		< string UIName="Interior DOF: Bokeh shape max size";		string UIWidget="Spinner";	float UIStep=0.1;	float UIMin=0.0;	float UIMax=100.0;	> = {15.0};
int	iADOF_ShapeVerticesI		< string UIName="Interior DOF: Bokeh shape vertices";		string UIWidget="spinner";				int UIMin=3;		int UIMax=9;		> = {6};
int	iADOF_ShapeQualityI		< string UIName="Interior DOF: Bokeh shape quality";		string UIWidget="spinner";				int UIMin=2;		int UIMax=25;		> = {5};
float	fADOF_ShapeCurvatureAmountI	< string UIName="Interior DOF: Bokeh shape curvature";		string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=-1.0;	float UIMax=1.0;	> = {1.0};
float	fADOF_ShapeRotationI		< string UIName="Interior DOF: Bokeh shape rotation (\xB0)";	string UIWidget="Spinner";	float UIStep=1;		float UIMin=0;		float UIMax=360;	> = {15};
float	fADOF_ShapeAnamorphRatioI	< string UIName="Interior DOF: Bokeh shape aspect ratio";	string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=1.0;	> = {1.0};
#if(bADOF_ShapeChromaEnable != 0)
 float	fADOF_ShapeChromaAmountI		< string UIName="Interior DOF: Shape chroma amount";		string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.00;	float UIMax=0.50;	> = {0.15};
#endif
float	fADOF_RenderResolutionMultI	< string UIName="Interior DOF: Blur render res mult";		string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.5;	float UIMax=1.0;	> = {0.5};
#if(iADOF_PostBlurMode == 0)
 float	fADOF_SmootheningAmountI		< string UIName="Interior DOF: Gaussian postblur width";		string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=5.0;	> = {1.0};
#endif
//float	Zoom		< string UIName="Crop";		string UIWidget="Spinner";	float UIStep=1.0;	float UIMin=0.00;	float UIMax=10.0;	> = {1.00};
UI_WHITESPACE(20)

#if(bADOF_ShapeWeightEnable != 0)
int DOFBLURSEPER <string UIName=">>>ShapeWeight<<<";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
 float	fADOF_ShapeWeightCurve		< string UIName="DOF: Bokeh shape weight curve";	string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.50;	float UIMax=8.0;	> = {3.0};
 float	fADOF_ShapeWeightAmount		< string UIName="DOF: Bokeh shape weight amount";	string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.00;	float UIMax=1.0;	> = {0.65};
#endif
UI_WHITESPACE(31)
int BokehColorGradingLine <string UIName=">>>            Bokeh Tone Mapping          <<<";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
bool EBCG < string UIName = "Enable Bokeh Tone Mapping";> = {false};
int BokehColorGradingLineTD <string UIName="-------Day -----------------------------";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
float ExteriorBrightnessD <string UIName="Exterior Dau Bokeh Brightness"; float UIMin=0.0; float UIMax=10.0; > = {1.0};
float ExteriorSaturationD <string UIName="Exterior Day Bokeh Saturation"; float UIMin=0.0; float UIMax=10.0; > = {1.0};
float ExteriorContrastD   <string UIName="Exterior Day Bokeh Contrast";   float UIMin=0.0; float UIMax=10.0; > = {1.0};
int BokehColorGradingLineTN <string UIName="-------Night -----------------------------";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
float ExteriorBrightnessN <string UIName="Exterior Night Bokeh Brightness"; float UIMin=0.0; float UIMax=10.0; > = {1.0};
float ExteriorSaturationN <string UIName="Exterior Night Bokeh Saturation"; float UIMin=0.0; float UIMax=10.0; > = {1.0};
float ExteriorContrastN   <string UIName="Exterior Night Bokeh Contrast";   float UIMin=0.0; float UIMax=10.0; > = {1.0};
int BokehColorGradingLineTI <string UIName="-------Interior -----------------------------";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
float InteriorSaturation <string UIName="Interior Bokeh Saturation"; float UIMin=0.0; float UIMax=10.0; > = {1.0};
float InteriorBrightness <string UIName="Interior Bokeh Brightness"; float UIMin=0.0; float UIMax=10.0; > = {1.0};
float InteriorContrast   <string UIName="Interior Bokeh Contrast";   float UIMin=0.0; float UIMax=10.0; > = {1.0};
UI_WHITESPACE(33)
float MappingCurve <string UIName="ToneMapping Curve";string UIWidget="Spinner";float UIMin=0.0;float UIMax=100.0;> = {4.0};
float MappingOversaturation <string UIName="Dampening";string UIWidget="Spinner";float UIMin=0.0;float UIMax=200.0;> = {30.0};

UI_WHITESPACE(32)

int LineA1 <string UIName="___________________________________________";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
int STARTAO <string UIName=">>>                    Ambient Occlusion           <<<";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};	
 bool   bMXAOShowAON 		        < string UIName="Show raw AO (use >AO only< technique)";													> = {false};
int STARTAOa <string UIName="-Defualt or Exterior-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};	
//#if(ENABLE_AO_TECHNIQUES != 0)
 
  float	fMXAOAmbientOcclusionAmountDawnN	< string UIName="AO: Amount Dawn";		                string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=5.0;	> = {2.0};
  float	fMXAOAmbientOcclusionAmountSunriseN	< string UIName="AO: Amount Sunrise";		                string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=5.0;	> = {2.0};
   
 float	fMXAOAmbientOcclusionAmountN	< string UIName="AO: Amount Day";		                string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=5.0;	> = {2.0};
 float	fMXAOAmbientOcclusionAmountSunsetN	< string UIName="AO: Amount Sunset";		                string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=5.0;	> = {2.0};
 float	fMXAOAmbientOcclusionAmountDuskN	< string UIName="AO: Amount Dusk";		                string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=5.0;	> = {2.0};
 float	fMXAOAmbientOcclusionAmountNightN	< string UIName="AO: Amount Night";		                string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=5.0;	> = {2.0};
 
 float	fMXAOSampleRadiusN	        < string UIName="AO: Sample Radius";		        string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=10.0;	> = {2.5};
 int	iMXAOSampleCountN		< string UIName="AO: Sample Count";		        string UIWidget="spinner";				int UIMin=4;		int UIMax=128;		> = {16};
 float	fMXAONormalBiasN		        < string UIName="AO: Normal Map Bias";		        string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=1.0;	> = {0.2};
 //float	fMXAONormalBiasNN		        < string UIName="AO: Normal Map Bias Night";		        string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=1.0;	> = {0.2};
 float	fMXAOAmbientOcclusionPowerN	< string UIName="AO: Curve";		                string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.05;	float UIMax=5.0;	> = {1.0};

 float	fMXAOAmbientOcclusionLuminanceN	< string UIName="AO: Highlight Preservation";	        string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=2.0;	> = {1.0};
 int	iMXAOBlurStepsN		        < string UIName="AO: Blur Steps";		        string UIWidget="spinner";				int UIMin=0;		int UIMax=5;		> = {2};
 float	fMXAOBlurSharpnessN	        < string UIName="AO: Blur Sharpness";		        string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=5.0;	> = {1.0};
 float	fMXAOFadeStartN	                < string UIName="AO: Fade Start";	                string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=1.0;	> = {0.05};
 float	fMXAOFadeEndN	                < string UIName="AO: Fade End";	                        string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=1.0;	> = {0.25};

//#endif
float	fPrepass_SharpScale		< string UIName="PREPASS: Sharp Scale";			string UIWidget="Spinner";	float UIStep=0.001;	float UIMin=0.0;	float UIMax=1.0;	> = {0.032};
//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 2
int LineA2 <string UIName="            ";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
int STARTAO2 <string UIName="-Rain & FOG & SNOW WEATHER-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};	
bool ENABLE_RainAO <string UIName = "Use Weather Parameters";> = {false};   

float	fMXAOAmbientOcclusionAmountRainDawn	< string UIName="AO -RF: Amount Dawn";		                string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=5.0;	> = {2.0};
float	fMXAOAmbientOcclusionAmountRainSunrise	< string UIName="AO -RF: Amount Sunrise";		                string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=5.0;	> = {2.0};
float	fMXAOAmbientOcclusionAmountRain	< string UIName="AO -RF: Amount Day";		                string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=5.0;	> = {2.0};
float	fMXAOAmbientOcclusionAmountRainSunset	< string UIName="AO -RF: Amount Sunset";		                string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=5.0;	> = {2.0};
float	fMXAOAmbientOcclusionAmountRainDusk	< string UIName="AO -RF: Amount Dusk";		                string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=5.0;	> = {2.0};
float	fMXAOAmbientOcclusionAmountRainNight	< string UIName="AO -RF: Amount Night";		                string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=5.0;	> = {2.0};


float	fMXAOSampleRadiusRain	        < string UIName="AO -RF : Sample Radius";		        string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=10.0;	> = {2.5};
 int	iMXAOSampleCountRain		< string UIName="AO -RF: Sample Count";		        string UIWidget="spinner";				int UIMin=4;		int UIMax=128;		> = {16};
 float	fMXAONormalBiasRain		        < string UIName="AO -RF: Normal Map Bias";		        string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=1.0;	> = {0.2};
 float	fMXAOAmbientOcclusionPowerRain	< string UIName="AO -RF: Curve";		                string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.05;	float UIMax=5.0;	> = {1.0};
 
 float	fMXAOAmbientOcclusionLuminanceRain	< string UIName="AO -RF: Highlight Preservation";	        string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=2.0;	> = {1.0};
 int	iMXAOBlurStepsRain		        < string UIName="AO -RF: Blur Steps";		        string UIWidget="spinner";				int UIMin=0;		int UIMax=5;		> = {2};
 float	fMXAOBlurSharpnessRain	        < string UIName="AO -RF: Blur Sharpness";		        string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=5.0;	> = {1.0};
 float	fMXAOFadeStartRain	                < string UIName="AO -RF: Fade Start";	                string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=1.0;	> = {0.05};
 float	fMXAOFadeEndRain	                < string UIName="AO -RF: Fade End";	                        string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=1.0;	> = {0.25};
float	fPrepass_SharpScaleRain		< string UIName="PREPASS -RF: Sharp Scale";			string UIWidget="Spinner";	float UIStep=0.001;	float UIMin=0.0;	float UIMax=1.0;	> = {0.032};

//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//
#elif SHADERGROUP == 3
int LineA3 <string UIName="     ";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};
int STARTAO3 <string UIName="-Interior-";  string UIWidget="spinner";  int UIMin=0;  int UIMax=0;> = {0};	
bool ENABLE_InteriorAO <string UIName = "Use Interior Parameters";> = {false};   
float	fMXAOSampleRadiusInterior	        < string UIName="AO Interior : Sample Radius";		        string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=10.0;	> = {2.5};
 int	iMXAOSampleCountInterior		< string UIName="AO Interior: Sample Count";		        string UIWidget="spinner";				int UIMin=4;		int UIMax=128;		> = {16};
 float	fMXAONormalBiasInterior		        < string UIName="AO Interior: Normal Map Bias";		        string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=1.0;	> = {0.2};
 float	fMXAOAmbientOcclusionPowerInterior	< string UIName="AO Interior: Curve";		                string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.05;	float UIMax=5.0;	> = {1.0};
 float	fMXAOAmbientOcclusionAmountInterior	< string UIName="AO Interior: Amount";		                string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=5.0;	> = {2.0};
 float	fMXAOAmbientOcclusionLuminanceInterior	< string UIName="AO Interior: Highlight Preservation";	        string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=2.0;	> = {1.0};
 int	iMXAOBlurStepsInterior		        < string UIName="AO Interior: Blur Steps";		        string UIWidget="spinner";				int UIMin=0;		int UIMax=5;		> = {2};
 float	fMXAOBlurSharpnessInterior	        < string UIName="AO Interior: Blur Sharpness";		        string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=5.0;	> = {1.0};
 float	fMXAOFadeStartInterior	                < string UIName="AO Interior: Fade Start";	                string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=1.0;	> = {0.05};
 float	fMXAOFadeEndInterior	                < string UIName="AO Interior: Fade End";	                        string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=1.0;	> = {0.25};

float	fPrepass_SharpScaleInterior		< string UIName="PREPASS Interior: Sharp Scale";			string UIWidget="Spinner";	float UIStep=0.001;	float UIMin=0.0;	float UIMax=1.0;	> = {0.032};
#endif //SHADERGROUP