float3 WeatherPatch(float3 Color)
{
    float wExposure_Day, wExposure_Night, wExposure_Interior, wGamma_Day, wGamma_Night, wGamma_Interior, wSaturation_Day, wSaturation_Night, wSaturation_Interior;

if (WeatherSet==0)   // NAT
{
    wExposure_Day        = 1;
    wExposure_Night      = 1;
    wExposure_Interior   = 1;
    wGamma_Day           = 1;
    wGamma_Night         = 1;
    wGamma_Interior      = 0.93;
    wSaturation_Day      = 0;
    wSaturation_Night    = 0;
    wSaturation_Interior = 0.2;
}

if (WeatherSet==1)  // Dolomite
{
    wExposure_Day        = 0.9;
    wExposure_Night      = 0.8;
    wExposure_Interior   = 1.2;
    wGamma_Day           = 0.85;
    wGamma_Night         = 1.1;
    wGamma_Interior      = 1.1;
    wSaturation_Day      = 0.4;
    wSaturation_Night    = 0;
    wSaturation_Interior = 0.1;
}

if (WeatherSet==2)  // CoT
{
    wExposure_Day        = 1.2;
    wExposure_Night      = 1;
    wExposure_Interior   = 1.1;
    wGamma_Day           = 1.1;
    wGamma_Night         = 1.2;
    wGamma_Interior      = 0.95;
    wSaturation_Day      = 0.1;
    wSaturation_Night    = 0.1;
    wSaturation_Interior = 0.4;
}

if (WeatherSet==3)  // Vivid
{
    wExposure_Day        = 1.2;
    wExposure_Night      = 1;
    wExposure_Interior   = 1.1;
    wGamma_Day           = 1;
    wGamma_Night         = 1;
    wGamma_Interior      = 1;
    wSaturation_Day      = 0;
    wSaturation_Night    = 0;
    wSaturation_Interior = 0.1;
}

if (WeatherSet==4)  // True Storms
{
    wExposure_Day        = 1.1;
    wExposure_Night      = 1;
    wExposure_Interior   = 1.1;
    wGamma_Day           = 1.15;
    wGamma_Night         = 1;
    wGamma_Interior      = 1.1;
    wSaturation_Day      = 0.1;
    wSaturation_Night    = 0;
    wSaturation_Interior = 0.1;
}

if (WeatherSet==5)  // Obsidian
{
    wExposure_Day        = 0.95;
    wExposure_Night      = 0.9;
    wExposure_Interior   = 1.1;
    wGamma_Day           = 0.98;
    wGamma_Night         = 1.1;
    wGamma_Interior      = 1.1;
    wSaturation_Day      = -0.1;
    wSaturation_Night    = 0;
    wSaturation_Interior = 0.1;
}

if (WeatherSet==6)  // Aequinoctium
{
    wExposure_Day        = 1.4;
    wExposure_Night      = 1;
    wExposure_Interior   = 1;
    wGamma_Day           = 1.1;
    wGamma_Night         = 0.95;
    wGamma_Interior      = 1;
    wSaturation_Day      = 0.1;
    wSaturation_Night    = 0;
    wSaturation_Interior = 0;
}

if (WeatherSet==7)   // Mythical
{
    wExposure_Day        = 1.4;
    wExposure_Night      = 0.9;
    wExposure_Interior   = 1;
    wGamma_Day           = 1;
    wGamma_Night         = 1;
    wGamma_Interior      = 1;
    wSaturation_Day      = -0.1;
    wSaturation_Night    = -0.3;
    wSaturation_Interior = 0;
}

if (WeatherSet==8)   // Rustic
{
    wExposure_Day        = 1.2;
    wExposure_Night      = 0.7;
    wExposure_Interior   = 1;
    wGamma_Day           = 1;
    wGamma_Night         = 1.1;
    wGamma_Interior      = 1;
    wSaturation_Day      = 0;
    wSaturation_Night    = 0;
    wSaturation_Interior = 0;
}

	float wExposure   =lerp( lerp(wExposure_Night,   wExposure_Day,   ENightDayFactor), wExposure_Interior,   EInteriorFactor );
	float wGamma      =lerp( lerp(wGamma_Night,      wGamma_Day,      ENightDayFactor), wGamma_Interior,      EInteriorFactor );
	float wSaturation =lerp( lerp(wSaturation_Night, wSaturation_Day, ENightDayFactor), wSaturation_Interior, EInteriorFactor );

	float3 middlegray = dot(Color, (1.0 / 3.0));
	float3 diffcolor = Color - middlegray;
    Color    *= wExposure;
    Color     = pow(Color, wGamma);
	Color     = (Color + diffcolor * wSaturation) / (1 + (diffcolor * wSaturation));
    return Color;
}