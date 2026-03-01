
float MIXintensity (float A,float B,float C,float D) {
    float Rintensity;
	if   (EInteriorFactor==0){	Rintensity=TODA(A,B);	}
	else {Rintensity=TODA(C,D);}
	return Rintensity;
}

float Mixedintensity (float A,float B,float C,float D,float E,float F,float G,float H) {
    float Rintensity;
	if(!ENABLE_DELU){
	if   (EInteriorFactor==0){	Rintensity=TODA(A,B);	}
	else {Rintensity=TODA(C,D);}
	}
	else if(ENABLE_DELU)
	{
	if   (EInteriorFactor==0){	Rintensity=TODA(E,F);	}
	else {Rintensity=TODA(G,H);}
	}
	return Rintensity;
}


float MixLutEnable () {
    float RLutEnable=0;
	if(ENABLE_DELU){
	if(DefaultSHBASE==1  || defaultKisune==1 || DefaultSHsummer==1 || DefaultSpring==1 || DefaultAuturmn==1 || DefaultCalmMoor==1 || DefaultReinforced==1 || DefaultSacrifice==1)
	{RLutEnable=1;}
	}
	else if(ENABLE_SHKitsuneCut  || ENABLE_SHsummer || ENABLE_SHspring || ENABLE_SHAuturmn || ENABLE_SHCalmMoor || ENABLE_SHECReinforced || ENABLE_SHECSacrifice || ENABLE_PRC1)
	{	RLutEnable=1;	}
	return RLutEnable;
}
float MixLutEnable1 (){
	float RLutEnable=0;
	if(ENABLE_DELU){
	if(DefaultSHBaseB || DefaultWinter || DefaultEccentricEcho  || DefaultEccentricBurst|| DefaultStroll || ENABLE_SHLofiFade || DefaultLofiFade  || DefaultLofiOLD || DefaultLofiTrance)
	{
	RLutEnable=1;
	}
	}
	else if(ENABLE_SHWINTER || ENABLE_SHECEcho || ENABLE_SHECburst  || ENABLE_SHLofiTrance|| ENABLE_SHStroll || ENABLE_SHLofiFade || ENABLE_SHLofiOLD  || ENABLE_SHECDreamland || ENABLE_baseT)
	{	RLutEnable=1;	}	
	return RLutEnable;
}
float MixLutEnable2() {
	 float RLutEnable=0;
	 if(ENABLE_DELU){
	 if(DefaultAscension==1 || Defaultimpression ==1|| DefaultScorchTrial==1 || DefaultBloosom ==1|| DefaultJoker ==1||DefaultRomeCavalry ==1|| DefaultOceanandSky ==1|| DefaultPenance==1)
	 {RLutEnable=1;	 }}
	else if(ENABLE_SNAP || ENABLE_ALL || ENABLE_Creamy || ENABLE_Toon || ENABLE_Lost ||ENABLE_LOmo || ENABLE_Drama || ENABLE_Silence)
	{	RLutEnable=1;	}
	return RLutEnable;
}

float MixLutEnable2B() {
	 float RLutEnable=0;
	 if(ENABLE_DELU){
	 if( DefaultOldWorld==1||DefaultDarkAge==1||DefaultGhosttown==1||DefaultTurquoise==1||DefaultSediments==1||DefaultOldPhotoBlack==1||DefaultFluorite==1||DefaultWakemeup||DefaultVertigo	==1||DefaultSenpai==1)
	 {RLutEnable=1;	 }}
	else if(ENABLE_Sparta||ENABLE_Somber||ENABLE_OLD||ENABLE_Eccentric||ENABLE_Knox||ENABLE_Senpai||ENABLE_Overseer||ENABLE_Beach||ENABLE_Bay	||ENABLE_Labamba)
	{	RLutEnable=1;	}
	return RLutEnable;
}

float MixLutEnable3() {
	 float RLutEnable=0;
	  if(ENABLE_DELU){
	  if(DefaultSunflower||DefaultMythology||DefaultDistrust||DefaultSingmetosleep||DefaultDream||DefaultUntruthWorld||DefaultEgo||DefaultLivingNight)
		{RLutEnable=1;}
	  }
	else if(ENABLE_Golden||ENABLE_Aqua||ENABLE_UltraContrast||ENABLE_Vogue||ENABLE_Vintage||ENABLE_Simple||ENABLE_Creeper||ENABLE_Surfin)
	{RLutEnable=1;}
	return RLutEnable;
}

float MixLutEnable4() {
	 float RLutEnable=0;
	   if(ENABLE_DELU){
	    if(DefaultPolarRegions || DefaultBlizzard || DefaultDesertStrike || DefaultSaltandsun || DefaultVice || DefaultApocalypse ||DefaultOhmu)
		{RLutEnable=1;}
	   }
	else if(ENABLE_Caffeine1 || ENABLE_Caffeine2 || ENABLE_Caffeine3 || ENABLE_Caffeine4 || ENABLE_Caffeine5 || ENABLE_Caffeine6 ||ENABLE_Caffeine7)
	{RLutEnable=1;}
	return RLutEnable;
}

float MixLutEnable4B() {
	 float RLutEnable=0;
	   if(ENABLE_DELU){
	    if(DefaultAmplitude || DefaultWax || DefaultTropical || DefaultPapyrus || DefaultSahara || DefaultPhantom ||DefaultVineyard)
		{RLutEnable=1;}
	   }
	else if(ENABLE_Caffeine8 || ENABLE_Caffeine9 || ENABLE_Caffeine10 || ENABLE_Caffeine11 || ENABLE_Caffeine12 || ENABLE_Caffeine13 ||ENABLE_Caffeine14)
	{RLutEnable=1;}
	return RLutEnable;
}


float MixLutEnable5() {
	 float RLutEnable=0;
	 if(ENABLE_DELU){
	 if(DefaultDoze || DefaultWasteland || DefaultLake || DefaultAllSoft || DefaultLotusflower || DefaultDeapFog)
	{RLutEnable=1;}
	 }
	else if(ENABLE_Caffeine15 || ENABLE_Caffeine16 || ENABLE_Caffeine17 || ENABLE_Caffeine18 || ENABLE_Caffeine19 || ENABLE_Caffeine20 )
	{RLutEnable=1;}
	return RLutEnable;
}

float MixLutEnable5B() {
	 float RLutEnable=0;
	 if(ENABLE_DELU){
	 if(DefaultSunnyday || DefaultDawn || DefaultParchment || DefaultConcentration || DefaultDragonSnail || DefaultAqua)
	{RLutEnable=1;}
	 }
	else if(ENABLE_Caffeine21 || ENABLE_Caffeine22 || ENABLE_Caffeine23 || ENABLE_Caffeine24 || ENABLE_Caffeine25 || ENABLE_Caffeine26)
	{RLutEnable=1;}
	return RLutEnable;
}

float MixLutEnable6() {
	 float RLutEnable=0;
	 if(ENABLE_DELU){
	 if(DefaultMeonmeonLUT1==1 ||DefaultMeonmeonLUT2==1 ||DefaultMeonmeonLUT3 ==1|| DefaultMeonmeonLUT4==1||DefaultMeonmeonLUT5==1 )
	{RLutEnable=1;}
	 }
	else if(ENABLE_MeonmeonLUT1 ||ENABLE_MeonmeonLUT2 ||ENABLE_MeonmeonLUT3 || ENABLE_MeonmeonLUT4||ENABLE_MeonmeonLUT5 )
	{RLutEnable=1;}
	return RLutEnable;
}

float MixLutEnable6B() {
	 float RLutEnable=0;
	 if(ENABLE_DELU){
	 if(DefaultMeonmeonLUT6==1||DefaultMeonmeonLUT7==1|| DefaultMeonmeonLUT8==1|| DefaultMeonmeonLUT9==1|| DefaultMeonmeonLUT10==1||DefaultMeonmeonLUT11==1 ||DefaultMeonmeonLUT12==1)
	{RLutEnable=1;}
	 }
	else if(ENABLE_MeonmeonLUT6||ENABLE_MeonmeonLUT7|| ENABLE_MeonmeonLUT8|| ENABLE_MeonmeonLUT9|| ENABLE_MeonmeonLUT10||ENABLE_MeonmeonLUT11 ||ENABLE_MeonmeonLUT12)
	{RLutEnable=1;}
	return RLutEnable;
}


float MixLutEnable7() {
	float RLutEnable=0;
	 if(ENABLE_DELU){
	 if(DefaultMeonmeonLUT13 ||DefaultMeonmeonLUT14 || DefaultMeonmeonLUT15 || DefaultMeonmeonLUT16 ||DefaultMeonmeonLUT17 ||DefaultMeonmeonLUT18)
	{RLutEnable=1;}
	 }
	else if(ENABLE_MeonmeonLUT13 ||ENABLE_MeonmeonLUT14 || ENABLE_MeonmeonLUT15 || ENABLE_MeonmeonLUT16 ||ENABLE_MeonmeonLUT17 ||ENABLE_MeonmeonLUT18)
	{RLutEnable=1;}
	return RLutEnable;
}

float MixLutEnable7B() {
	float RLutEnable=0;
	 if(ENABLE_DELU){
	 if(DefaultMeonmeonLUT19 ||DefaultMeonmeonLUT20 || DefaultMeonmeonLUT21 || DefaultMeonmeonLUT22 || DefaultMeonmeonLUT23||DefaultMeonmeonLUT24)
	{RLutEnable=1;}
	 }
	else if(ENABLE_MeonmeonLUT19 ||ENABLE_MeonmeonLUT20 || ENABLE_MeonmeonLUT21 || ENABLE_MeonmeonLUT22 || ENABLE_MeonmeonLUT23||ENABLE_MeonmeonLUT24)
	{RLutEnable=1;}
	return RLutEnable;
}

float MixLutEnable8() {
	 float RLutEnable=0;
	 if(ENABLE_DELU){
	 if(DefaultCRYSTALFRUIT ||DefaultDEATHBELLDREAMS || DefaultDIAMONDEYES || DefaultROSEBLOOD ||DefaultDDREALISM ||DefaultDarkBase || DefaultTerrorism)
	{RLutEnable=1;}
	 }
	else if(ENABLE_RUDYP1 ||ENABLE_RUDYP2 || ENABLE_Engage || ENABLE_RUDYP3 ||ENABLE_RUDYP4 ||ENABLE_RUDYP5 || ENABLE_UCON)
	{RLutEnable=1;}
	return RLutEnable;
}


#define TuningColorLUTTileAmountX 256 
#define TuningColorLUTTileAmountY 16 
#define TuningColorLUTNorm float2(1.0/float(TuningColorLUTTileAmountX),1.0/float(TuningColorLUTTileAmountY))

#define TuningColorLUTTileAmountEXTEND 4096
#define TuningColorLUTTileAmountYEXTEND 64 
#define TuningColorLUTNormE float2(1.0/float(TuningColorLUTTileAmountEXTEND),1.0/float(TuningColorLUTTileAmountYEXTEND))



//-----------------------LUT-------------------------//
float3 LUTfuncDiamond(float3 inColor)
{

	float MeonAAmount;
	MeonAAmount=Mixedintensity(MeonAAmountDay,MeonAAmountNight,MeonAAmountInterior,MeonAAmountInteriorNight,DefaulMeonAAmountDay,DefaulMeonAAmountNight,DefaulMeonAAmountInterior,DefaulMeonAAmountInteriorNight);
	
	float4 ColorLUTDst = float4((inColor.rg*float(TuningColorLUTTileAmountY-1)+0.5f)*TuningColorLUTNorm,inColor.b*float(TuningColorLUTTileAmountY-1),1);
    ColorLUTDst.x += trunc(ColorLUTDst.z)*TuningColorLUTNorm.y;
	if(ENABLE_MeonmeonLUT1 || (ENABLE_DELU && DefaultMeonmeonLUT1==1)){
		ColorLUTDst = lerp(
      MeonmeonLUT1.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      MeonmeonLUT1.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	  }
	else  if(ENABLE_MeonmeonLUT2 || (ENABLE_DELU && DefaultMeonmeonLUT2==1) ){
		ColorLUTDst = lerp(
      MeonmeonLUT2.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      MeonmeonLUT2.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	  }
	else  if(ENABLE_MeonmeonLUT3 || (ENABLE_DELU && DefaultMeonmeonLUT3==1)){
		ColorLUTDst = lerp(
      MeonmeonLUT3.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      MeonmeonLUT3.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	  }
	else  if(ENABLE_MeonmeonLUT4 || (ENABLE_DELU && DefaultMeonmeonLUT4==1)){
		ColorLUTDst = lerp(
      MeonmeonLUT4.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      MeonmeonLUT4.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	  }
	else if(ENABLE_MeonmeonLUT5 || (ENABLE_DELU && DefaultMeonmeonLUT5==1)){
		ColorLUTDst = lerp(
      MeonmeonLUT5.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      MeonmeonLUT5.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	  }

	else{	ColorLUTDst = lerp(      TextureLUT.SampleLevel(Sampler1, ColorLUTDst.xy, 0),      TextureLUT.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));}
	return lerp(inColor.xyz, ColorLUTDst.xyz, MeonAAmount);
}


float3 LUTfuncDiamondD(float3 inColor)
{

	float MeonAAmount;
	MeonAAmount=Mixedintensity(MeonAAmountDay,MeonAAmountNight,MeonAAmountInterior,MeonAAmountInteriorNight,DefaulMeonAAmountDay,DefaulMeonAAmountNight,DefaulMeonAAmountInterior,DefaulMeonAAmountInteriorNight);
	
	float4 ColorLUTDst = float4((inColor.rg*float(TuningColorLUTTileAmountY-1)+0.5f)*TuningColorLUTNorm,inColor.b*float(TuningColorLUTTileAmountY-1),1);
    ColorLUTDst.x += trunc(ColorLUTDst.z)*TuningColorLUTNorm.y;
	 if(ENABLE_MeonmeonLUT6 || (ENABLE_DELU && DefaultMeonmeonLUT6==1)){
		ColorLUTDst = lerp(
      MeonmeonLUT6.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      MeonmeonLUT6.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	  }
	else  if(ENABLE_MeonmeonLUT7 || (ENABLE_DELU && DefaultMeonmeonLUT7==1)){
		ColorLUTDst = lerp(
      MeonmeonLUT7.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      MeonmeonLUT7.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	  }
	else  if(ENABLE_MeonmeonLUT8 || (ENABLE_DELU && DefaultMeonmeonLUT8==1)){
		ColorLUTDst = lerp(
      MeonmeonLUT8.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      MeonmeonLUT8.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	  }
	else  if(ENABLE_MeonmeonLUT9 || (ENABLE_DELU && DefaultMeonmeonLUT9==1)){
		ColorLUTDst = lerp(
      MeonmeonLUT9.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      MeonmeonLUT9.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	  }
	else  if(ENABLE_MeonmeonLUT11 || (ENABLE_DELU && DefaultMeonmeonLUT10==1)){
		ColorLUTDst = lerp(
      MeonmeonLUT11.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      MeonmeonLUT11.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	  }
	 else if(ENABLE_MeonmeonLUT10 || (ENABLE_DELU && DefaultMeonmeonLUT11==1)){
		ColorLUTDst = lerp(
      MeonmeonLUT10.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      MeonmeonLUT10.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	  }
	 else if(ENABLE_MeonmeonLUT12 || (ENABLE_DELU && DefaultMeonmeonLUT12==1)){
		ColorLUTDst = lerp(
      MeonmeonLUT12.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      MeonmeonLUT12.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	  }
	else{	ColorLUTDst = lerp(      TextureLUT.SampleLevel(Sampler1, ColorLUTDst.xy, 0),      TextureLUT.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));}
	return lerp(inColor.xyz, ColorLUTDst.xyz, MeonAAmount);
}



float3 LUTfuncDiamondB(float3 inColor)
{
	float MeonBAmount=Mixedintensity(MeonBAmountDay,MeonBAmountNight,MeonBAmountInterior,MeonBAmountInteriorNight,DefaultMeonBAmountDay,DefaultMeonBAmountNight,DefaultMeonBAmountInterior,DefaultMeonBAmountInteriorNight);

	float4 ColorLUTDst = float4((inColor.rg*float(TuningColorLUTTileAmountY-1)+0.5f)*TuningColorLUTNorm,inColor.b*float(TuningColorLUTTileAmountY-1),1);
    ColorLUTDst.x += trunc(ColorLUTDst.z)*TuningColorLUTNorm.y;
	if(ENABLE_MeonmeonLUT13 || (ENABLE_DELU && DefaultMeonmeonLUT13==1)){
		ColorLUTDst = lerp(
      MeonmeonLUT13.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      MeonmeonLUT13.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	  }
	else if(ENABLE_MeonmeonLUT14|| (ENABLE_DELU && DefaultMeonmeonLUT14==1)){
		ColorLUTDst = lerp(
      MeonmeonLUT14.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      MeonmeonLUT14.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	  }
	else  if(ENABLE_MeonmeonLUT15 || (ENABLE_DELU && DefaultMeonmeonLUT15==1)){
		ColorLUTDst = lerp(
      MeonmeonLUT15.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      MeonmeonLUT15.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	  }
	else  if(ENABLE_MeonmeonLUT16 || (ENABLE_DELU && DefaultMeonmeonLUT16==1)){
		ColorLUTDst = lerp(
      MeonmeonLUT16.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      MeonmeonLUT16.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	  }
	else  if(ENABLE_MeonmeonLUT17 || (ENABLE_DELU && DefaultMeonmeonLUT17==1)){
		ColorLUTDst = lerp(
      MeonmeonLUT17.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      MeonmeonLUT17.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	  }
	else  if(ENABLE_MeonmeonLUT18 || (ENABLE_DELU && DefaultMeonmeonLUT18==1)){
		ColorLUTDst = lerp(
      MeonmeonLUT18.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      MeonmeonLUT18.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	  }
	else{
	ColorLUTDst = lerp(
      TextureLUT.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TextureLUT.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
		return lerp(inColor.xyz, ColorLUTDst.xyz, MeonBAmount);
}


float3 LUTfuncDiamondBD(float3 inColor)
{
	float MeonBAmount=Mixedintensity(MeonBAmountDay,MeonBAmountNight,MeonBAmountInterior,MeonBAmountInteriorNight,DefaultMeonBAmountDay,DefaultMeonBAmountNight,DefaultMeonBAmountInterior,DefaultMeonBAmountInteriorNight);

	float4 ColorLUTDst = float4((inColor.rg*float(TuningColorLUTTileAmountY-1)+0.5f)*TuningColorLUTNorm,inColor.b*float(TuningColorLUTTileAmountY-1),1);
    ColorLUTDst.x += trunc(ColorLUTDst.z)*TuningColorLUTNorm.y;
	if(ENABLE_MeonmeonLUT19 || (ENABLE_DELU && DefaultMeonmeonLUT19==1)){
		ColorLUTDst = lerp(
      MeonmeonLUT19.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      MeonmeonLUT19.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	  }
	else  if(ENABLE_MeonmeonLUT20 || (ENABLE_DELU && DefaultMeonmeonLUT20==1)){
		ColorLUTDst = lerp(
      MeonmeonLUT20.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      MeonmeonLUT20.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	  }
	else  if(ENABLE_MeonmeonLUT21 || (ENABLE_DELU && DefaultMeonmeonLUT21==1)){
		ColorLUTDst = lerp(
      MeonmeonLUT21.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      MeonmeonLUT21.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	  }
	else  if(ENABLE_MeonmeonLUT22 || (ENABLE_DELU && DefaultMeonmeonLUT22==1)){
		ColorLUTDst = lerp(
      MeonmeonLUT22.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      MeonmeonLUT22.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	  }
	else  if(ENABLE_MeonmeonLUT23 || (ENABLE_DELU && DefaultMeonmeonLUT23==1)){
		ColorLUTDst = lerp(
      MeonmeonLUT12.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      MeonmeonLUT12.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	  }
	else if(ENABLE_MeonmeonLUT24 || (ENABLE_DELU && DefaultMeonmeonLUT24==1)){
		ColorLUTDst = lerp(
      MeonmeonLUT24.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      MeonmeonLUT24.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	  }
	else{
	ColorLUTDst = lerp(
      TextureLUT.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TextureLUT.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
		return lerp(inColor.xyz, ColorLUTDst.xyz, MeonBAmount);
}



float3 CFLUTfunc(float3 inColor)
{
	float CaffeAmount=Mixedintensity(CaffeAmountDay,CaffeAmountNight,CaffeAmountInterior,CaffeAmountInteriorNight,MiiuLutCDay,MiiuLutCNight,MiiuLutCInteriorDay,MiiuLutCInteriorNight);
	
	float4 ColorLUTDst = float4((inColor.rg*float(TuningColorLUTTileAmountY-1)+0.5f)*TuningColorLUTNorm,inColor.b*float(TuningColorLUTTileAmountY-1),1);
    ColorLUTDst.x += trunc(ColorLUTDst.z)*TuningColorLUTNorm.y;
	
	if(ENABLE_Caffeine1 || (ENABLE_DELU && DefaultPolarRegions==1)){
		ColorLUTDst = lerp(
      TCaffeine01.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TCaffeine01.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	  }
	else if(ENABLE_Caffeine2 ||(ENABLE_DELU && DefaultBlizzard==1)){
		ColorLUTDst = lerp(
      TCaffeine02.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TCaffeine02.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	  }
	else if(ENABLE_Caffeine3 ||(ENABLE_DELU && DefaultDesertStrike==1)){
		ColorLUTDst = lerp(
      TCaffeine03.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TCaffeine03.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	  }
	else if(ENABLE_Caffeine4 ||(ENABLE_DELU && DefaultSaltandsun==1)){
		ColorLUTDst = lerp(
      TCaffeine04.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TCaffeine04.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	  }
	else  if(ENABLE_Caffeine5 ||(ENABLE_DELU && DefaultVice==1)){
		ColorLUTDst = lerp(
      TCaffeine05.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TCaffeine05.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	  }
	else if(ENABLE_Caffeine6 ||(ENABLE_DELU && DefaultApocalypse==1)){
		ColorLUTDst = lerp(
      TCaffeine06.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TCaffeine06.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	  }
	else if(ENABLE_Caffeine7 ||(ENABLE_DELU && DefaultOhmu==1)){
		ColorLUTDst = lerp(
      TCaffeine07.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TCaffeine07.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	  }
	else{
	ColorLUTDst = lerp(
      TextureLUT.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TextureLUT.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	return lerp(inColor.xyz, ColorLUTDst.xyz, CaffeAmount);
}

float3 CFLUTfuncD(float3 inColor)
{
	float CaffeAmount=Mixedintensity(CaffeAmountDay,CaffeAmountNight,CaffeAmountInterior,CaffeAmountInteriorNight,MiiuLutCDay,MiiuLutCNight,MiiuLutCInteriorDay,MiiuLutCInteriorNight);
	
	float4 ColorLUTDst = float4((inColor.rg*float(TuningColorLUTTileAmountY-1)+0.5f)*TuningColorLUTNorm,inColor.b*float(TuningColorLUTTileAmountY-1),1);
    ColorLUTDst.x += trunc(ColorLUTDst.z)*TuningColorLUTNorm.y;
	
	if(ENABLE_Caffeine8 ||(ENABLE_DELU && DefaultAmplitude==1)){
		ColorLUTDst = lerp(
      TCaffeine08.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TCaffeine08.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	  }
	else if(ENABLE_Caffeine10 ||(ENABLE_DELU && DefaultTropical==1)){
		ColorLUTDst = lerp(
      TCaffeine10.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TCaffeine10.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	  }
	else if(ENABLE_Caffeine9 ||(ENABLE_DELU && DefaultWax ==1)){
		ColorLUTDst = lerp(
      TCaffeine09.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TCaffeine09.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	  
	  }
	else if(ENABLE_Caffeine11 ||(ENABLE_DELU  && DefaultPapyrus ==1)){
		ColorLUTDst = lerp(
      TCaffeine11.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TCaffeine11.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	  }
	else if(ENABLE_Caffeine12 ||(ENABLE_DELU && DefaultSahara==1)){
		ColorLUTDst = lerp(
      TCaffeine12.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TCaffeine12.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	  }
	else if(ENABLE_Caffeine13 ||(ENABLE_DELU && DefaultPhantom==1)){
		ColorLUTDst = lerp(
      TCaffeine13.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TCaffeine13.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	  }
	else if(ENABLE_Caffeine14 ||(ENABLE_DELU && DefaultVineyard==1)){
		ColorLUTDst = lerp(
      TCaffeine14.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TCaffeine14.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	  }
	else{
	ColorLUTDst = lerp(
      TextureLUT.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TextureLUT.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	return lerp(inColor.xyz, ColorLUTDst.xyz, CaffeAmount);
}


float3 CFBLUTfunc(float3 inColor)
{
	float CaffeBAmount=Mixedintensity(CaffeBAmountDay,CaffeBAmountNight,CaffeBAmountInterior,CaffeBAmountInteriorNight,MiiuLutDay,MiiuLutNight,MiiuLutInteriorDay,MiiuLutInteriorNight);

	float4 ColorLUTDst = float4((inColor.rg*float(TuningColorLUTTileAmountY-1)+0.5f)*TuningColorLUTNorm,inColor.b*float(TuningColorLUTTileAmountY-1),1);
    ColorLUTDst.x += trunc(ColorLUTDst.z)*TuningColorLUTNorm.y;
	if(ENABLE_Caffeine15 ||(ENABLE_DELU && DefaultDoze==1) ){
		ColorLUTDst = lerp(
      TCaffeine15.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TCaffeine15.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	  }
	else if(ENABLE_Caffeine16 ||(ENABLE_DELU && DefaultWasteland==1)){
		ColorLUTDst = lerp(
      TCaffeine16.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TCaffeine16.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	 }
	 else if(ENABLE_Caffeine17 ||(ENABLE_DELU && DefaultLake==1)){
		ColorLUTDst = lerp(
      TCaffeine17.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TCaffeine17.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	 }
	else if(ENABLE_Caffeine18||(ENABLE_DELU && DefaultAllSoft==1)){
		ColorLUTDst = lerp(
      TCaffeine18.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TCaffeine18.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	  else if(ENABLE_Caffeine19||(ENABLE_DELU && DefaultLotusflower==1)){
		ColorLUTDst = lerp(
      TCaffeine19.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TCaffeine19.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	 }
	   else if(ENABLE_Caffeine20||(ENABLE_DELU && DefaultDeapFog==1)){
		ColorLUTDst = lerp(
      TCaffeine20.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TCaffeine20.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	else{
	ColorLUTDst = lerp(
      TextureLUT.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TextureLUT.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	
	}
	return lerp(inColor.xyz, ColorLUTDst.xyz, CaffeBAmount);
}


float3 CFBLUTfuncD(float3 inColor)
{
	float CaffeBAmount=Mixedintensity(CaffeBAmountDay,CaffeBAmountNight,CaffeBAmountInterior,CaffeBAmountInteriorNight,MiiuLutDay,MiiuLutNight,MiiuLutInteriorDay,MiiuLutInteriorNight);

	float4 ColorLUTDst = float4((inColor.rg*float(TuningColorLUTTileAmountY-1)+0.5f)*TuningColorLUTNorm,inColor.b*float(TuningColorLUTTileAmountY-1),1);
    ColorLUTDst.x += trunc(ColorLUTDst.z)*TuningColorLUTNorm.y;
	if(ENABLE_Caffeine21||(ENABLE_DELU && DefaultSunnyday==1)){
		ColorLUTDst = lerp(
      TCaffeine21.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TCaffeine21.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	   else if(ENABLE_Caffeine22||(ENABLE_DELU && DefaultDawn==1)){
		ColorLUTDst = lerp(
      TCaffeine22.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TCaffeine22.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	else if(ENABLE_Caffeine23||(ENABLE_DELU && DefaultParchment==1)){
		ColorLUTDst = lerp(
      TCaffeine23.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TCaffeine23.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	   else if(ENABLE_Caffeine24||(ENABLE_DELU && DefaultConcentration==1)){
		ColorLUTDst = lerp(
      TCaffeine24.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TCaffeine24.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	else if(ENABLE_Caffeine25||(ENABLE_DELU && DefaultDragonSnail==1)){
		ColorLUTDst = lerp(
      TCaffeine25.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TCaffeine25.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	else if(ENABLE_Caffeine26||(ENABLE_DELU && DefaultAqua==1)){
		ColorLUTDst = lerp(
      TCaffeine26.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TCaffeine26.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	else{
	ColorLUTDst = lerp(
      TextureLUT.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TextureLUT.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	
	}
	return lerp(inColor.xyz, ColorLUTDst.xyz, CaffeBAmount);
}



float3 SNAPLUTfunc(float3 inColor)
{
	float SNAPAAmount=Mixedintensity(SNAPAAmountDay,SNAPAAmountNight,SNAPAAmountInterior,SNAPAAmountInteriorNight,DefaultMiiuADay,DefaultMiiuANight,DefaultMiiuAInteriorDay,DefaultMiiuAInteriorNight);
	
    float4 ColorLUTDst = float4((inColor.rg*float(TuningColorLUTTileAmountY-1)+0.5f)*TuningColorLUTNorm,inColor.b*float(TuningColorLUTTileAmountY-1),1);
    ColorLUTDst.x += trunc(ColorLUTDst.z)*TuningColorLUTNorm.y;
	if(ENABLE_SNAP ||(ENABLE_DELU &&DefaultAscension ==1)){
	ColorLUTDst = lerp(
      Snapdragon.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      Snapdragon.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	
	}
	else if(ENABLE_ALL||(ENABLE_DELU &&Defaultimpression ==1)){
	ColorLUTDst = lerp(
      Allisvain.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      Allisvain.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	else if(ENABLE_Creamy||(ENABLE_DELU &&DefaultScorchTrial ==1)){
	ColorLUTDst = lerp(
      Creamy.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      Creamy.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	else if(ENABLE_Toon||(ENABLE_DELU &&DefaultBloosom ==1)){
	ColorLUTDst = lerp(
      Toon.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      Toon.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));

	}
	else if(ENABLE_Lost||(ENABLE_DELU &&DefaultJoker ==1)){
	ColorLUTDst = lerp(
      Lostintime.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      Lostintime.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	else if(ENABLE_LOmo||(ENABLE_DELU && DefaultRomeCavalry==1)){
	ColorLUTDst = lerp(
      Lomo.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      Lomo.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	else if(ENABLE_Drama||(ENABLE_DELU &&DefaultOceanandSky ==1)){
	ColorLUTDst = lerp(
      Drama.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      Drama.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	else if(ENABLE_Silence||(ENABLE_DELU && DefaultPenance==1)){
	ColorLUTDst = lerp(
      Silence.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      Silence.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	else if(ENABLE_Sparta||(ENABLE_DELU && DefaultOldWorld==1)){
	ColorLUTDst = lerp(
      Sparta.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      Sparta.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	return lerp(inColor.xyz, ColorLUTDst.xyz, SNAPAAmount);
}

float3 SNAPLUTfuncD(float3 inColor)
{
	float SNAPAAmount=Mixedintensity(SNAPAAmountDay,SNAPAAmountNight,SNAPAAmountInterior,SNAPAAmountInteriorNight,DefaultMiiuADay,DefaultMiiuANight,DefaultMiiuAInteriorDay,DefaultMiiuAInteriorNight);
	
    float4 ColorLUTDst = float4((inColor.rg*float(TuningColorLUTTileAmountY-1)+0.5f)*TuningColorLUTNorm,inColor.b*float(TuningColorLUTTileAmountY-1),1);
    ColorLUTDst.x += trunc(ColorLUTDst.z)*TuningColorLUTNorm.y;
	if(ENABLE_Sparta||(ENABLE_DELU && DefaultOldWorld==1)){
	ColorLUTDst = lerp(
      Sparta.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      Sparta.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	else if(ENABLE_Somber||(ENABLE_DELU && DefaultDarkAge==1)){
	ColorLUTDst = lerp(
      Somber.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      Somber.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	else if(ENABLE_OLD||(ENABLE_DELU &&DefaultTurquoise ==1)){
	ColorLUTDst = lerp(
      OldWorld.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      OldWorld.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	else if(ENABLE_Eccentric||(ENABLE_DELU &&DefaultSediments ==1)){
	ColorLUTDst = lerp(
      Eccentric.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      Eccentric.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	else if(ENABLE_Knox||(ENABLE_DELU && DefaultOldPhotoBlack==1)){
	ColorLUTDst = lerp(
      Knox.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      Knox.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	else if(ENABLE_Senpai||(ENABLE_DELU && DefaultFluorite==1)){
	ColorLUTDst = lerp(
      Senpai.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      Senpai.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	else if(ENABLE_Overseer||(ENABLE_DELU && DefaultWakemeup==1)){
	ColorLUTDst = lerp(
      Overseer.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      Overseer.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	else if(ENABLE_Beach||(ENABLE_DELU && DefaultVertigo==1)){
	ColorLUTDst = lerp(
      BeachBoy.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      BeachBoy.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	else if(ENABLE_Bay||(ENABLE_DELU &&DefaultSenpai ==1)){
	ColorLUTDst = lerp(
      Baywatch.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      Baywatch.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	else if(ENABLE_Labamba||(ENABLE_DELU && DefaultGhosttown==1)){
	ColorLUTDst = lerp(
      LaBamba.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      LaBamba.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	return lerp(inColor.xyz, ColorLUTDst.xyz, SNAPAAmount);
}



float3 SNAPBLUTfunc(float3 inColor)
{
	float SNAPBAmount=Mixedintensity(SNAPBAmountDay,SNAPBAmountNight,SNAPBAmountInterior,SNAPBAmountInteriorNight,DefaultMiiuBDay,DefaultMiiuBNight,DefaultMiiuBinteriorDay,DefaultMiiuBinteriorNight);


	float4 ColorLUTDst = float4((inColor.rg*float(TuningColorLUTTileAmountY-1)+0.5f)*TuningColorLUTNorm,inColor.b*float(TuningColorLUTTileAmountY-1),1);
    ColorLUTDst.x += trunc(ColorLUTDst.z)*TuningColorLUTNorm.y;
	if(ENABLE_Golden ||(ENABLE_DELU && DefaultSunflower==1)){
	ColorLUTDst = lerp(
      GoldenHour.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      GoldenHour.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	else if(ENABLE_Aqua ||(ENABLE_DELU && DefaultMythology==1)){
	ColorLUTDst = lerp(
      Aqua.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      Aqua.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	else if(ENABLE_UltraContrast ||(ENABLE_DELU &&DefaultDistrust==1)){
	ColorLUTDst = lerp(
      UltraContrast.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      UltraContrast.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	else if(ENABLE_Vogue ||(ENABLE_DELU && DefaultSingmetosleep==1)){
	ColorLUTDst = lerp(
      Vogue.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      Vogue.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	else if(ENABLE_Vintage ||(ENABLE_DELU && DefaultDream==1)){
	ColorLUTDst = lerp(
      VintageBW.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      VintageBW.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	else if(ENABLE_Simple ||(ENABLE_DELU && DefaultUntruthWorld==1)){
	ColorLUTDst = lerp(
      SimpleBW.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      SimpleBW.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	else if(ENABLE_Creeper ||(ENABLE_DELU && DefaultEgo==1)){
	ColorLUTDst = lerp(
      Creeper.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      Creeper.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	else if(ENABLE_Surfin ||(ENABLE_DELU && DefaultLivingNight==1)){
	ColorLUTDst = lerp(
      SurfinBird.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      SurfinBird.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	else{
	ColorLUTDst = lerp(
      TextureLUT.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TextureLUT.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	
	}
	return lerp(inColor.xyz, ColorLUTDst.xyz, SNAPBAmount);
}
float3 LUTfunc(float3 inColor)
{
	float RudyAmount=Mixedintensity(RudyAmountDay,RudyAmountNight,RudyAmountInterior,RudyAmountInteriorNight,DDDay,DDNight,DDInterior,DDInteriorNight);

    float4 ColorLUTDst = float4((inColor.rg*float(TuningColorLUTTileAmountY-1)+0.5f)*TuningColorLUTNorm,inColor.b*float(TuningColorLUTTileAmountY-1),1);
    ColorLUTDst.x += trunc(ColorLUTDst.z)*TuningColorLUTNorm.y;
	if(ENABLE_RUDYP1 ||(ENABLE_DELU && DefaultCRYSTALFRUIT==1) ){
	ColorLUTDst = lerp(
      TRUDYP1.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TRUDYP1.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	else if(ENABLE_RUDYP2 ||(ENABLE_DELU && DefaultDEATHBELLDREAMS==1)){
	ColorLUTDst = lerp(
      TRUDYP2.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TRUDYP2.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	else if(ENABLE_RUDYP3 ||(ENABLE_DELU && DefaultDIAMONDEYES==1)){
	ColorLUTDst = lerp(
      TRUDYP4.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TRUDYP4.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	else if(ENABLE_RUDYP4 ||(ENABLE_DELU && DefaultROSEBLOOD==1)){
	ColorLUTDst = lerp(
      TRUDYP5.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TRUDYP5.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	else if(ENABLE_RUDYP5 ||(ENABLE_DELU && DefaultDDREALISM==1)){
	ColorLUTDst = lerp(
      TRUDYP6.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TRUDYP6.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	else if(ENABLE_Engage ||(ENABLE_DELU && DefaultDarkBase==1)){
	ColorLUTDst = lerp(
      TDarkMythical.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TDarkMythical.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	else if(ENABLE_UCON || (ENABLE_DELU && DefaultTerrorism==1)) {
	ColorLUTDst = lerp(
      TextureLUT.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TextureLUT.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	else {
	ColorLUTDst = lerp(
      TextureLUT.SampleLevel(Sampler1, ColorLUTDst.xy, 0),
      TextureLUT.SampleLevel(Sampler1, float2(ColorLUTDst.x+TuningColorLUTNorm.y,ColorLUTDst.y), 0),frac(ColorLUTDst.z));
	}
	return lerp(inColor.xyz, ColorLUTDst.xyz, RudyAmount);
}
float3 LUTfuncT(float3 inColor)
{
	
	float SHAmount=Mixedintensity(SHAmountDay,SHAmountNight,SHAmountInterior,SHAmountInteriorNight,DefaultSHAmountDay,DefaultSHAmountNight,DefaultSHAmountInterior,DefaultSHAmountInteriorNight);
	
	float4 ColorLUTDstT = 
	float4((inColor.rg*float(TuningColorLUTTileAmountYEXTEND-1)+0.5f)*TuningColorLUTNormE,inColor.b*float(TuningColorLUTTileAmountYEXTEND-1),1);
	ColorLUTDstT.x += trunc(ColorLUTDstT.z)*TuningColorLUTNormE .y;
	  if(ENABLE_SHKitsuneCut==1 ||(ENABLE_DELU && defaultKisune==1)){
	ColorLUTDstT = lerp(
      KitsuneCut.SampleLevel(Sampler1, ColorLUTDstT.xy, 0),
      KitsuneCut.SampleLevel(Sampler1, float2(ColorLUTDstT.x+TuningColorLUTNormE.y,ColorLUTDstT.y), 0),frac(ColorLUTDstT.z));
	  }
	  else if(ENABLE_SHsummer ||(ENABLE_DELU && DefaultSHsummer==1)){
		ColorLUTDstT = lerp(
      SHSummer.SampleLevel(Sampler1, ColorLUTDstT.xy, 0),
      SHSummer.SampleLevel(Sampler1, float2(ColorLUTDstT.x+TuningColorLUTNormE.y,ColorLUTDstT.y), 0),frac(ColorLUTDstT.z));
	  }
	  else if(ENABLE_SHspring ||(ENABLE_DELU && DefaultSpring==1)){
		ColorLUTDstT = lerp(
      SHSpring.SampleLevel(Sampler1, ColorLUTDstT.xy, 0),
      SHSpring.SampleLevel(Sampler1, float2(ColorLUTDstT.x+TuningColorLUTNormE.y,ColorLUTDstT.y), 0),frac(ColorLUTDstT.z));
	  }
	  else if(ENABLE_SHCalmMoor ||(ENABLE_DELU && DefaultAuturmn==1)){
		ColorLUTDstT = lerp(
      SHCalmMoor.SampleLevel(Sampler1, ColorLUTDstT.xy, 0),
      SHCalmMoor.SampleLevel(Sampler1, float2(ColorLUTDstT.x+TuningColorLUTNormE.y,ColorLUTDstT.y), 0),frac(ColorLUTDstT.z));
	  }
	  else if(ENABLE_SHECReinforced ||(ENABLE_DELU && DefaultCalmMoor==1)){
		ColorLUTDstT = lerp(
      SHEccentricReinforced.SampleLevel(Sampler1, ColorLUTDstT.xy, 0),
      SHEccentricReinforced.SampleLevel(Sampler1, float2(ColorLUTDstT.x+TuningColorLUTNormE.y,ColorLUTDstT.y), 0),frac(ColorLUTDstT.z));
	  }
	  else if(ENABLE_SHECSacrifice ||(ENABLE_DELU && DefaultReinforced==1)){
		ColorLUTDstT = lerp(
      SHEccentricSacrifice.SampleLevel(Sampler1, ColorLUTDstT.xy, 0),
      SHEccentricSacrifice.SampleLevel(Sampler1, float2(ColorLUTDstT.x+TuningColorLUTNormE.y,ColorLUTDstT.y), 0),frac(ColorLUTDstT.z));
	  }
	  else if(ENABLE_SHAuturmn ||(ENABLE_DELU && DefaultSacrifice==1)){
		ColorLUTDstT = lerp(
      SHAuturmn.SampleLevel(Sampler1, ColorLUTDstT.xy, 0),
      SHAuturmn.SampleLevel(Sampler1, float2(ColorLUTDstT.x+TuningColorLUTNormE.y,ColorLUTDstT.y), 0),frac(ColorLUTDstT.z));
	  }
	  else if(ENABLE_PRC1 ||(ENABLE_DELU && DefaultSHBASEA==1)){
	ColorLUTDstT = lerp(
      PRC1.SampleLevel(Sampler1, ColorLUTDstT.xy, 0),
      PRC1.SampleLevel(Sampler1, float2(ColorLUTDstT.x+TuningColorLUTNormE.y,ColorLUTDstT.y), 0),frac(ColorLUTDstT.z));
	  
	  }
	  else
	  {
	  ColorLUTDstT = lerp(
      SHWinter.SampleLevel(Sampler1, ColorLUTDstT.xy, 0),
      SHWinter.SampleLevel(Sampler1, float2(ColorLUTDstT.x+TuningColorLUTNormE.y,ColorLUTDstT.y), 0),frac(ColorLUTDstT.z));
	  }
	  return lerp(inColor.xyz, ColorLUTDstT.xyz, SHAmount);
}	
////////////////////////
float3 LUTfuncTB(float3 inColor)
{
	
	float SHAmountB=Mixedintensity(SHAmountDayB,SHAmountNightB,SHAmountInteriorB,SHAmountInteriorNightB,DefaultSHAmountDayB,DefaultSHAmountNightB,DefaultSHAmountInteriorB,DefaultSHAmountInteriorNightB);
	//float SHAmountBB=MIXintensity(DefaultSHAmountDayB,DefaultSHAmountNightB,DefaultSHAmountInteriorB,DefaultSHAmountInteriorNightB);
	
	float4 ColorLUTDstT = 
	float4((inColor.rg*float(TuningColorLUTTileAmountYEXTEND-1)+0.5f)*TuningColorLUTNormE,inColor.b*float(TuningColorLUTTileAmountYEXTEND-1),1);
	ColorLUTDstT.x += trunc(ColorLUTDstT.z)*TuningColorLUTNormE .y;
	   if(ENABLE_SHWINTER ||(ENABLE_DELU && DefaultWinter==1)){
		ColorLUTDstT = lerp(
      SHWinter.SampleLevel(Sampler1, ColorLUTDstT.xy, 0),
      SHWinter.SampleLevel(Sampler1, float2(ColorLUTDstT.x+TuningColorLUTNormE.y,ColorLUTDstT.y), 0),frac(ColorLUTDstT.z));
	  }
	  else if(ENABLE_SHStroll||(ENABLE_DELU && DefaultStroll==1)){
		ColorLUTDstT = lerp(
      SHStroll.SampleLevel(Sampler1, ColorLUTDstT.xy, 0),
      SHStroll.SampleLevel(Sampler1, float2(ColorLUTDstT.x+TuningColorLUTNormE.y,ColorLUTDstT.y), 0),frac(ColorLUTDstT.z));
	  }
	  else if(ENABLE_SHECburst||(ENABLE_DELU && DefaultEccentricBurst==1)){
		ColorLUTDstT = lerp(
      SHEccentricBurst.SampleLevel(Sampler1, ColorLUTDstT.xy, 0),
      SHEccentricBurst.SampleLevel(Sampler1, float2(ColorLUTDstT.x+TuningColorLUTNormE.y,ColorLUTDstT.y), 0),frac(ColorLUTDstT.z));
	  }
	  else if(ENABLE_SHECDreamland||(ENABLE_DELU && DefaulDreamland==1)){
		ColorLUTDstT = lerp(
      SHEccentricDreamland.SampleLevel(Sampler1, ColorLUTDstT.xy, 0),
      SHEccentricDreamland.SampleLevel(Sampler1, float2(ColorLUTDstT.x+TuningColorLUTNormE.y,ColorLUTDstT.y), 0),frac(ColorLUTDstT.z));
	  }
	  else if(ENABLE_SHECEcho||(ENABLE_DELU && DefaultEccentricEcho==1)){
		ColorLUTDstT = lerp(
      SHEccentricEcho.SampleLevel(Sampler1, ColorLUTDstT.xy, 0),
      SHEccentricEcho.SampleLevel(Sampler1, float2(ColorLUTDstT.x+TuningColorLUTNormE.y,ColorLUTDstT.y), 0),frac(ColorLUTDstT.z));
	  }
	  else if(ENABLE_SHLofiFade||(ENABLE_DELU && DefaultLofiFade==1)){
		ColorLUTDstT = lerp(
      SHLofiFade.SampleLevel(Sampler1, ColorLUTDstT.xy, 0),
      SHLofiFade.SampleLevel(Sampler1, float2(ColorLUTDstT.x+TuningColorLUTNormE.y,ColorLUTDstT.y), 0),frac(ColorLUTDstT.z));
	  }
	  else if(ENABLE_SHLofiOLD||(ENABLE_DELU && DefaultLofiOLD==1)){
		ColorLUTDstT = lerp(
      SHLofiOld.SampleLevel(Sampler1, ColorLUTDstT.xy, 0),
      SHLofiOld.SampleLevel(Sampler1, float2(ColorLUTDstT.x+TuningColorLUTNormE.y,ColorLUTDstT.y), 0),frac(ColorLUTDstT.z));
	  }
	  else if(ENABLE_SHLofiTrance||(ENABLE_DELU && DefaultLofiTrance==1)){
		ColorLUTDstT = lerp(
      SHLofiTrance.SampleLevel(Sampler1, ColorLUTDstT.xy, 0),
      SHLofiTrance.SampleLevel(Sampler1, float2(ColorLUTDstT.x+TuningColorLUTNormE.y,ColorLUTDstT.y), 0),frac(ColorLUTDstT.z));
	  }
	  else if(ENABLE_baseT||(ENABLE_DELU && DefaultSHBaseB==1)){
		ColorLUTDstT = lerp(
      PRC1.SampleLevel(Sampler1, ColorLUTDstT.xy, 0),
      PRC1.SampleLevel(Sampler1, float2(ColorLUTDstT.x+TuningColorLUTNormE.y,ColorLUTDstT.y), 0),frac(ColorLUTDstT.z));
	  }
	  else 
	  {
	  ColorLUTDstT = lerp(
      SHWinter.SampleLevel(Sampler1, ColorLUTDstT.xy, 0),
      SHWinter.SampleLevel(Sampler1, float2(ColorLUTDstT.x+TuningColorLUTNormE.y,ColorLUTDstT.y), 0),frac(ColorLUTDstT.z));
	  }
	  return lerp(inColor.xyz, ColorLUTDstT.xyz, SHAmountB);
}	

////////////////////////
float3 LUTfuncPRC(float3 inColor)
{
	float CGAmount=MIXintensity(CGAmountDay,CGAmountNight,CGAmountInterior,CGAmountInteriorNight);
	
	
	float4 ColorLUTDstT = 
	float4((inColor.rg*float(TuningColorLUTTileAmountYEXTEND-1)+0.5f)*TuningColorLUTNormE,inColor.b*float(TuningColorLUTTileAmountYEXTEND-1),1);
	ColorLUTDstT.x += trunc(ColorLUTDstT.z)*TuningColorLUTNormE .y;
	  
	   if(iCG==2){
		ColorLUTDstT = lerp(
      PRC2.SampleLevel(Sampler1, ColorLUTDstT.xy, 0),
      PRC2.SampleLevel(Sampler1, float2(ColorLUTDstT.x+TuningColorLUTNormE.y,ColorLUTDstT.y), 0),frac(ColorLUTDstT.z));
	  }
	  else if(iCG==3){
		ColorLUTDstT = lerp(
      PRC3.SampleLevel(Sampler1, ColorLUTDstT.xy, 0),
      PRC3.SampleLevel(Sampler1, float2(ColorLUTDstT.x+TuningColorLUTNormE.y,ColorLUTDstT.y), 0),frac(ColorLUTDstT.z));
	  }
	  else if(iCG==4){
		ColorLUTDstT = lerp(
      PRC4.SampleLevel(Sampler1, ColorLUTDstT.xy, 0),
      PRC4.SampleLevel(Sampler1, float2(ColorLUTDstT.x+TuningColorLUTNormE.y,ColorLUTDstT.y), 0),frac(ColorLUTDstT.z));
	  }
	  else if(iCG==5){
		ColorLUTDstT = lerp(
      PRC5.SampleLevel(Sampler1, ColorLUTDstT.xy, 0),
      PRC5.SampleLevel(Sampler1, float2(ColorLUTDstT.x+TuningColorLUTNormE.y,ColorLUTDstT.y), 0),frac(ColorLUTDstT.z));
	  }
	  else if(iCG==6){
		ColorLUTDstT = lerp(
      PRC6.SampleLevel(Sampler1, ColorLUTDstT.xy, 0),
      PRC6.SampleLevel(Sampler1, float2(ColorLUTDstT.x+TuningColorLUTNormE.y,ColorLUTDstT.y), 0),frac(ColorLUTDstT.z));
	  }
	  else 
	  {
	  ColorLUTDstT = lerp(
      PRC10.SampleLevel(Sampler1, ColorLUTDstT.xy, 0),
      PRC10.SampleLevel(Sampler1, float2(ColorLUTDstT.x+TuningColorLUTNormE.y,ColorLUTDstT.y), 0),frac(ColorLUTDstT.z));
	  }
	  return lerp(inColor.xyz, ColorLUTDstT.xyz, CGAmount);
}	

////////////////////////
float3 LUTfuncPRCD(float3 inColor)
{
	float CGAmount=MIXintensity(CGAmountDay,CGAmountNight,CGAmountInterior,CGAmountInteriorNight);
	
	
	float4 ColorLUTDstT = 
	float4((inColor.rg*float(TuningColorLUTTileAmountYEXTEND-1)+0.5f)*TuningColorLUTNormE,inColor.b*float(TuningColorLUTTileAmountYEXTEND-1),1);
	ColorLUTDstT.x += trunc(ColorLUTDstT.z)*TuningColorLUTNormE .y;
	  
	  if(iCG==7){
		ColorLUTDstT = lerp(
      PRC7.SampleLevel(Sampler1, ColorLUTDstT.xy, 0),
      PRC7.SampleLevel(Sampler1, float2(ColorLUTDstT.x+TuningColorLUTNormE.y,ColorLUTDstT.y), 0),frac(ColorLUTDstT.z));
	  }
	  else if(iCG==8){
		ColorLUTDstT = lerp(
      PRC8.SampleLevel(Sampler1, ColorLUTDstT.xy, 0),
      PRC8.SampleLevel(Sampler1, float2(ColorLUTDstT.x+TuningColorLUTNormE.y,ColorLUTDstT.y), 0),frac(ColorLUTDstT.z));
	  }
	  else if(iCG==9){
		ColorLUTDstT = lerp(
      PRC9.SampleLevel(Sampler1, ColorLUTDstT.xy, 0),
      PRC9.SampleLevel(Sampler1, float2(ColorLUTDstT.x+TuningColorLUTNormE.y,ColorLUTDstT.y), 0),frac(ColorLUTDstT.z));
	  }
	  else if(iCG==10){
		ColorLUTDstT = lerp(
      PRC10.SampleLevel(Sampler1, ColorLUTDstT.xy, 0),
      PRC10.SampleLevel(Sampler1, float2(ColorLUTDstT.x+TuningColorLUTNormE.y,ColorLUTDstT.y), 0),frac(ColorLUTDstT.z));
	  }
	    else if(iCG==11){
		ColorLUTDstT = lerp(
      PRC16.SampleLevel(Sampler1, ColorLUTDstT.xy, 0),
      PRC16.SampleLevel(Sampler1, float2(ColorLUTDstT.x+TuningColorLUTNormE.y,ColorLUTDstT.y), 0),frac(ColorLUTDstT.z));
	  }
	  else if(iCG==12){
		ColorLUTDstT = lerp(
      PRC17.SampleLevel(Sampler1, ColorLUTDstT.xy, 0),
      PRC17.SampleLevel(Sampler1, float2(ColorLUTDstT.x+TuningColorLUTNormE.y,ColorLUTDstT.y), 0),frac(ColorLUTDstT.z));
	  }
	  else if(iCG==13){
		ColorLUTDstT = lerp(
      PRC18.SampleLevel(Sampler1, ColorLUTDstT.xy, 0),
      PRC18.SampleLevel(Sampler1, float2(ColorLUTDstT.x+TuningColorLUTNormE.y,ColorLUTDstT.y), 0),frac(ColorLUTDstT.z));
	  }
	  else 
	  {
	  ColorLUTDstT = lerp(
      PRC10.SampleLevel(Sampler1, ColorLUTDstT.xy, 0),
      PRC10.SampleLevel(Sampler1, float2(ColorLUTDstT.x+TuningColorLUTNormE.y,ColorLUTDstT.y), 0),frac(ColorLUTDstT.z));
	  }
	  return lerp(inColor.xyz, ColorLUTDstT.xyz, CGAmount);
}	



float3 LUTfuncPRCA(float3 inColor)
{
	float CAmount=MIXintensity(CAmountDay,CAmountNight,CAmountInterior,CAmountInteriorNight);
	
	float4 ColorLUTDstT = 
	float4((inColor.rg*float(TuningColorLUTTileAmountYEXTEND-1)+0.5f)*TuningColorLUTNormE,inColor.b*float(TuningColorLUTTileAmountYEXTEND-1),1);
	ColorLUTDstT.x += trunc(ColorLUTDstT.z)*TuningColorLUTNormE .y;
	  if(iDSLRType==1){
	ColorLUTDstT = lerp(
      PRC11.SampleLevel(Sampler1, ColorLUTDstT.xy, 0),
      PRC11.SampleLevel(Sampler1, float2(ColorLUTDstT.x+TuningColorLUTNormE.y,ColorLUTDstT.y), 0),frac(ColorLUTDstT.z));
	  }
	   else if(iDSLRType==2){
		ColorLUTDstT = lerp(
      PRC12.SampleLevel(Sampler1, ColorLUTDstT.xy, 0),
      PRC12.SampleLevel(Sampler1, float2(ColorLUTDstT.x+TuningColorLUTNormE.y,ColorLUTDstT.y), 0),frac(ColorLUTDstT.z));
	  }
	  else if(iDSLRType==3){
		ColorLUTDstT = lerp(
      PRC13.SampleLevel(Sampler1, ColorLUTDstT.xy, 0),
      PRC13.SampleLevel(Sampler1, float2(ColorLUTDstT.x+TuningColorLUTNormE.y,ColorLUTDstT.y), 0),frac(ColorLUTDstT.z));
	  }
	  
	  else if(iDSLRType==4){
		ColorLUTDstT = lerp(
      PRC14.SampleLevel(Sampler1, ColorLUTDstT.xy, 0),
      PRC14.SampleLevel(Sampler1, float2(ColorLUTDstT.x+TuningColorLUTNormE.y,ColorLUTDstT.y), 0),frac(ColorLUTDstT.z));
	  }
	  else if(iDSLRType==5){
		ColorLUTDstT = lerp(
      PRC15.SampleLevel(Sampler1, ColorLUTDstT.xy, 0),
      PRC15.SampleLevel(Sampler1, float2(ColorLUTDstT.x+TuningColorLUTNormE.y,ColorLUTDstT.y), 0),frac(ColorLUTDstT.z));
	  }
	  
	  else 
	  {
	  ColorLUTDstT = lerp(
      PRC10.SampleLevel(Sampler1, ColorLUTDstT.xy, 0),
      PRC10.SampleLevel(Sampler1, float2(ColorLUTDstT.x+TuningColorLUTNormE.y,ColorLUTDstT.y), 0),frac(ColorLUTDstT.z));}
	  return lerp(inColor.xyz, ColorLUTDstT.xyz, CAmount);
}	
