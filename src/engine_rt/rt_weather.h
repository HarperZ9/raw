#pragma once
// Weather state for the ray-tracing worker's dynamic weather (RAW engine runtime). Original work, MIT.
//
// Each present reads Sky (Sky::GetSingleton, 0x01C2640): currentWeather +0x48, lastWeather +0x50, the transition
// currentWeatherPct +0x1B8 (0 to 1) and Precipitation* +0xA0 (currentParticleDensity +0x84). Each TESWeather is
// classified by its DATA flags (+0x66F: pleasant 1, cloudy 2, rainy 4, snow 8). Precipitation follows the
// weathers' own fade points (DATA +0x06 begin fade-in of the incoming weather, +0x07 end fade-out of the outgoing
// one, both in 1/255 of the transition). Fog comes from the weathers' day fog far distance (FNAM +0x678 +4): 1 at
// 5,000 units or nearer, 0 at 150,000 or farther. The two weathers blend by the transition, and the result goes to
// the proxy's PG_SetWeatherState(rain, snow, cloud, fog), each 0 to 1, when the proxy exports it.
#include <string>

#include "rt_image.h"

namespace raw::rt {

void WeatherInit(const Image& img);
void WeatherOnPresent();
// weather: one result line with the blended values, both weathers' form IDs and flags, the transition and the
// precipitation density.
std::string WeatherCommand();

}  // namespace raw::rt
