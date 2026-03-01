#define Levels_black_point GuiBlack
#define Levels_black_pointNightA GuiBlackNightA
#define Levels_black_pointInterior GuiBlackInteriorA
#define Levels_black_pointN GuiBlackNight
#define Levels_black_pointI GuiBlackInterior
#define Levels_black_pointDay GuiBlackDay
#define Levels_black_pointNight GuiBlackNight
#define Levels_white_point 255


//Lut
#define Curves_mode 0 //[0|1|2] //-Choose what to apply contrast to. 0 = Luma, 1 = Chroma, 2 = both Luma and Chroma. Default is 0 (Luma)
#define Curves_formula 3 //[1|2|3|4|5|6|7|8|9|10|11] //-The contrast s-curve you want to use. 1 = Sine, 2 = Abs split, 3 = Smoothstep, 4 = Exp formula, 5 = Simplified Catmull-Rom (0,0,1,1), 6 = Perlins Smootherstep, 7 = Abs add, 8 = Techicolor Cinestyle, 9 = Parabola, 10 = Half-circles. 11 = Polynomial split. Note that Technicolor Cinestyle is practically identical to Sine, but runs slower. In fact I think the difference might only be due to rounding errors. I prefer 2 myself, but 3 is a nice alternative with a little more effect (but harsher on the highlight and shadows) and it's the fastest formula.


#define PixelSize 		 float2(ScreenSize.y, ScreenSize.y * ScreenSize.z)

#define Defog TonemapDefog
#define DefogN TonemapDefogN
#define DefogI TonemapDefogI
#define DefogIN TonemapDefogIN
#define Bleach 0.020 
#define FogColor float3(0.50,1.00,2.55)
#define GammaD TonemapGammaD
#define GammaN TonemapGammaN
#define GammaI TonemapGammaI
#define Exposure TonemapExposure
#define ExposureDay TonemapExposureDay
#define ExposureNight TonemapExposureNight
#define ExposureDawn TonemapExposureDawn
#define ExposureSunrise TonemapExposureSunrise
#define ExposureSunset TonemapExposureSunset
#define ExposureDusk TonemapExposureDusk

//Filmic Pass:
#define Strenght 0.75 
#define BaseGamma 1.0 
#define Fade 0.0 
#define Contrast 1.0 
#define FBleach 0.00 
#define FSaturation -0.15 
#define FRedCurve 1.0 
#define FGreenCurve 1.0 
#define FBlueCurve 1.0 
#define EffectGammaR 1.0 
#define EffectGammaG 1.0 
#define EffectGammaB 1.0 
#define EffectGamma 0.85 
#define LumCoeff float3(0.212656,0.715158,0.072186) 
//VIBRANCE:
//#define Vibrance_RGB_balance float3(0.00,0.00,1.10)

//SEPIA:

//#define ColorTone ToneSepia
#define GreyPower 0.1 
#define SepiaPower 0.1

//#define fFisheyeZoom 0.51 
//#define fFisheyeDistortion 0.02 
#define fFisheyeDistortionCubic 0.0 
#define fFisheyeColorshift 0.0000 

#define ENABLE_3DLUT true



#define DefaultSHBASE DefaultSHBASEA
//Silent Horizon BaseA

#define defaultKisune defaultKisune
//Silent Horizon Kisune Cut 

#define DefaultSHsummer DefaultSHsummer
//Silent Horizon Summer

#define DefaultSpring DefaultSpring
//Silent Horizon Spring

#define DefaultAuturmn DefaultAuturmn
//Silent Horizon Auturmn

#define DefaultCalmMoor DefaultCalmMoor
//Silent Horizon CalmMoor

#define DefaultReinforced DefaultReinforced
//Silent Horizon Reinforced

#define DefaultSacrifice DefaultSacrifice
//Silent Horizon Sacrifice
//--------------------------------//

//Intensity 0.0~1.0

#define 	DefaultSHAmountDay DefaultSHAmountDay
//Silent Horizon Day Lut Intensity

#define 	DefaultSHAmountNight DefaultSHAmountNight 
//Silent Horizon Night Lut Intensity

#define 	DefaultSHAmountInterior DefaultSHAmountInterior
//Silent Horizon Interior Lut Intensity

#define 	DefaultSHAmountInteriorNight DefaultSHAmountInteriorNight
//Silent Horizon Interior Night Lut Intensity 

// ---------------  ---------------  ---------------  ---------------  ---------------  --------------- 
// -:::::::::::::-  -:::::::::::::-  -:::::::::::::-  -:::::::::::::-  -:::::::::::::-  -:::::::::::::- 
// ---------------  ---------------  ---------------  ---------------  ---------------  ---------------                          


#define DefaultSHBaseB DefaultSHBaseB
//Silent Horizon BaseB

#define DefaultWinter DefaultWinter
//
#define DefaulDreamland DefaulDreamland
//
#define DefaultEccentricEcho DefaultEccentricEcho
//
#define DefaultEccentricBurst DefaultEccentricBurst
//
#define DefaultStroll DefaultStroll
//
#define DefaultLofiFade DefaultLofiFade
//
#define DefaultLofiOLD DefaultLofiOLD
//
#define DefaultLofiTrance DefaultLofiTrance
//
//Intensity 0.0~1.0

#define DefaultSHAmountDayB DefaultSHAmountDayB
#define	DefaultSHAmountNightB DefaultSHAmountNightB
#define DefaultSHAmountInteriorB DefaultSHAmountInteriorB
#define DefaultSHAmountInteriorNightB DefaultSHAmountInteriorNightB

// ---------------  ---------------  ---------------  ---------------  ---------------  --------------- 
// -:::::::::::::-  -:::::::::::::-  -:::::::::::::-  -:::::::::::::-  -:::::::::::::-  -:::::::::::::- 
// ---------------  ---------------  ---------------  ---------------  ---------------  ---------------            
#define DefaultAscension DefaultAscension
#define Defaultimpression Defaultimpression
#define DefaultScorchTrial DefaultScorchTrial
#define DefaultBloosom DefaultBloosom
#define DefaultJoker DefaultJoker
#define DefaultRomeCavalry DefaultRomeCavalry
#define DefaultOceanandSky DefaultOceanandSky
#define DefaultPenance DefaultPenance
#define DefaultOldWorld DefaultOldWorld
#define DefaultDarkAge DefaultDarkAge
#define DefaultTurquoise DefaultTurquoise
#define DefaultSediments DefaultSediments
#define DefaultOldPhotoBlack DefaultOldPhotoBlack
#define DefaultFluorite DefaultFluorite
#define DefaultWakemeup DefaultWakemeup
#define DefaultVertigo DefaultVertigo
#define DefaultSenpai DefaultSenpai
#define DefaultGhosttown DefaultGhosttown

//Intensity 0.0~1.0

#define 	DefaultMiiuADay DefaultMiiuADay
#define 	DefaultMiiuANight DefaultMiiuANight
#define 	DefaultMiiuAInteriorDay DefaultMiiuAInteriorDay
#define 	DefaultMiiuAInteriorNight DefaultMiiuAInteriorNight

// ---------------  ---------------  ---------------  ---------------  ---------------  --------------- 
// -:::::::::::::-  -:::::::::::::-  -:::::::::::::-  -:::::::::::::-  -:::::::::::::-  -:::::::::::::- 
// ---------------  ---------------  ---------------  ---------------  ---------------  ---------------    
 
#define DefaultSunflower DefaultSunflower
#define DefaultMythology DefaultMythology
#define DefaultDistrust DefaultDistrust
#define DefaultSingmetosleep DefaultSingmetosleep
#define DefaultDream DefaultDream
#define DefaultUntruthWorld DefaultUntruthWorld
#define DefaultEgo DefaultEgo
#define DefaultLivingNight DefaultLivingNight

//Intensity 0.0~1.0 

#define 	DefaultMiiuBDay DefaultMiiuBDay
#define 	DefaultMiiuBNight DefaultMiiuBNight
#define 	DefaultMiiuBinteriorDay DefaultMiiuBinteriorDay
#define 	DefaultMiiuBinteriorNight DefaultMiiuBinteriorNight

#define DefaultCRYSTALFRUIT DefaultCRYSTALFRUIT
#define DefaultDEATHBELLDREAMS DefaultDEATHBELLDREAMS
#define DefaultDIAMONDEYES DefaultDIAMONDEYES
#define DefaultROSEBLOOD DefaultROSEBLOOD
#define DefaultDDREALISM DefaultDDREALISM
#define DefaultDarkBase DefaultDarkBase
#define DefaultTerrorism DefaultTerrorism

//Intensity 0.0~1.0
#define 	DDDay DDDay
#define 	DDNight DDNight
#define 	DDInterior DDInterior
#define   DDInteriorNight DDInteriorNight
// ---------------  ---------------  ---------------  ---------------  ---------------  --------------- 
// -:::::::::::::-  -:::::::::::::-  -:::::::::::::-  -:::::::::::::-  -:::::::::::::-  -:::::::::::::- 
// ---------------  ---------------  ---------------  ---------------  ---------------  ---------------    
#define DefaultPolarRegion DefaultPolarRegions
#define DefaultBlizzard DefaultBlizzard
#define DefaultDesertStrike DefaultDesertStrike
#define DefaultSaltandsun DefaultSaltandsun
#define DefaultVice DefaultVice
#define DefaultApocalypse DefaultApocalypse
#define DefaultOhmu DefaultOhmu
#define DefaultAmplitude DefaultAmplitude
#define DefaultWax DefaultWax
#define DefaultTropical DefaultTropical
#define DefaultPapyrus DefaultPapyrus
#define DefaultSahara DefaultSahara
#define DefaultPhantom DefaultPhantom
#define DefaultVineyard DefaultVineyard

//Intensity 0.0~1.0
#define 	MiiuLutCDay MiiuLutCDay
#define 	MiiuLutCNigh MiiuLutCNight
#define 	MiiuLutCInteriorDay MiiuLutCInteriorDay
#define 	MiiuLutCInteriorNight MiiuLutCInteriorNight

// ---------------  ---------------  ---------------  ---------------  ---------------  --------------- 
// -:::::::::::::-  -:::::::::::::-  -:::::::::::::-  -:::::::::::::-  -:::::::::::::-  -:::::::::::::- 
// ---------------  ---------------  ---------------  ---------------  ---------------  ---------------    

#define DefaultDoze DefaultDoze
#define DefaultWasteland DefaultWasteland
#define DefaultLake DefaultLake
#define DefaultAllSoft DefaultAllSoft
#define DefaultLotusflower DefaultLotusflower
#define DefaultDeapFog DefaultDeapFog
#define DefaultSunnyday DefaultSunnyday
#define DefaultDawn DefaultDawn
#define DefaultParchment DefaultParchment
#define DefaultConcentration DefaultConcentration
#define DefaultDragonSnail DefaultDragonSnail
#define DefaultAqua DefaultAqua
//Intensity 0.0~1.0
#define 	MiiuLutDay MiiuLutDay	
#define 	MiiuLutNight MiiuLutNight
#define 	MiiuLutInteriorDay MiiuLutInteriorDay
#define 	MiiuLutInteriorNight MiiuLutInteriorNight
// ---------------  ---------------  ---------------  ---------------  ---------------  --------------- 
// -:::::::::::::-  -:::::::::::::-  -:::::::::::::-  -:::::::::::::-  -:::::::::::::-  -:::::::::::::- 
// ---------------  ---------------  ---------------  ---------------  ---------------  ---------------  

#define DefaultMeonmeonLUT1 DefaultMeonmeonLUT1
#define DefaultMeonmeonLUT2 DefaultMeonmeonLUT2
#define DefaultMeonmeonLUT3 DefaultMeonmeonLUT3
#define DefaultMeonmeonLUT4 DefaultMeonmeonLUT4
#define DefaultMeonmeonLUT5 DefaultMeonmeonLUT5
#define DefaultMeonmeonLUT6 DefaultMeonmeonLUT6
#define DefaultMeonmeonLUT7 DefaultMeonmeonLUT7
#define DefaultMeonmeonLUT8 DefaultMeonmeonLUT8
#define DefaultMeonmeonLUT9 DefaultMeonmeonLUT9
#define DefaultMeonmeonLUT10 DefaultMeonmeonLUT10
#define DefaultMeonmeonLUT11 DefaultMeonmeonLUT11
#define DefaultMeonmeonLUT12 DefaultMeonmeonLUT12
//Intensity 0.0~1.0
#define 	DefaulMeonAAmountDay DefaulMeonAAmountDay
#define 	DefaulMeonAAmountNight DefaulMeonAAmountNight
#define 	DefaulMeonAAmountInterior DefaulMeonAAmountInterior
#define	DefaulMeonAAmountInteriorNight DefaulMeonAAmountInteriorNight
// ---------------  ---------------  ---------------  ---------------  ---------------  --------------- 
// -:::::::::::::-  -:::::::::::::-  -:::::::::::::-  -:::::::::::::-  -:::::::::::::-  -:::::::::::::- 
// ---------------  ---------------  ---------------  ---------------  ---------------  ---------------    

#define DefaultMeonmeonLUT13 DefaultMeonmeonLUT13
#define DefaultMeonmeonLUT14 DefaultMeonmeonLUT14
#define DefaultMeonmeonLUT15 DefaultMeonmeonLUT15
#define DefaultMeonmeonLUT16 DefaultMeonmeonLUT16
#define DefaultMeonmeonLUT17 DefaultMeonmeonLUT17
#define DefaultMeonmeonLUT18 DefaultMeonmeonLUT18
#define DefaultMeonmeonLUT19 DefaultMeonmeonLUT19
#define DefaultMeonmeonLUT20 DefaultMeonmeonLUT20
#define DefaultMeonmeonLUT21 DefaultMeonmeonLUT21
#define DefaultMeonmeonLUT22 DefaultMeonmeonLUT22
#define DefaultMeonmeonLUT23 DefaultMeonmeonLUT23
#define DefaultMeonmeonLUT24 DefaultMeonmeonLUT24
//Intensity 0.0~1.0 
#define 	DefaultMeonBAmountDay DefaultMeonBAmountDay
#define 	DefaultMeonBAmountNight DefaultMeonBAmountNight
#define 	DefaultMeonBAmountInterior DefaultMeonBAmountInterior
#define 	DefaultMeonBAmountInteriorNight DefaultMeonBAmountInteriorNight

// ---------------  ---------------  ---------------  ---------------  ---------------  --------------- 
// -:::::::::::::-  -:::::::::::::-  -:::::::::::::-  -:::::::::::::-  -:::::::::::::-  -:::::::::::::- 
// ---------------  ---------------  ---------------  ---------------  ---------------  ---------------    

