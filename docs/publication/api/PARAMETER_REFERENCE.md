# Parameter Reference

Complete reference for all SkyrimBridge float4 parameters. Each parameter is pushed to all 9 target ENB shaders via `ENBSetParameter` every frame (dirty-tracked).

All parameters use the `SB_` prefix and are declared as `extern float4` in HLSL with matching `UIName` annotations.

---

## Domain 1: Celestial (10 float4s)

| Parameter | .x | .y | .z | .w |
|---|---|---|---|---|
| `SB_Sun_NDC` | NDC X | NDC Y | onScreen (0/1) | elevation angle (rad) |
| `SB_Sun_Direction` | dir X | dir Y | dir Z | angular radius (rad) |
| `SB_Sun_Color` | R | G | B | sun glare factor |
| `SB_Masser_NDC` | NDC X | NDC Y | onScreen (0/1) | phase brightness [0,1] |
| `SB_Masser_Direction` | dir X | dir Y | dir Z | elevation angle (rad) |
| `SB_Secunda_NDC` | NDC X | NDC Y | onScreen (0/1) | phase brightness [0,1] |
| `SB_Secunda_Direction` | dir X | dir Y | dir Z | elevation angle (rad) |
| `SB_Time_Data` | gameHour [0,24) | sunriseHour | sunsetHour | dayProgress [0,1] |
| `SB_Time_Segments1` | dawn [0,1] | sunrise [0,1] | day [0,1] | sunset [0,1] |
| `SB_Time_Segments2` | dusk [0,1] | night [0,1] | goldenHour [0,1] | blueHour [0,1] |

## Domain 2: Atmosphere (8 float4s)

| Parameter | .x | .y | .z | .w |
|---|---|---|---|---|
| `SB_Atmos_SkyUpper` | R | G | B | 0 |
| `SB_Atmos_SkyLower` | R | G | B | 0 |
| `SB_Atmos_Horizon` | R | G | B | 0 |
| `SB_Atmos_Ambient` | R | G | B | intensity |
| `SB_Atmos_Sunlight` | R | G | B | sunlight scale |
| `SB_Atmos_CloudDiffuse` | R | G | B | 0 |
| `SB_Atmos_CloudAmbient` | R | G | B | 0 |
| `SB_Atmos_EffectLighting` | R | G | B | 0 |

## Domain 3: Fog (4 float4s)

| Parameter | .x | .y | .z | .w |
|---|---|---|---|---|
| `SB_Fog_Near` | R | G | B | near distance |
| `SB_Fog_Far` | R | G | B | far distance |
| `SB_Fog_Density` | power curve | maxOpacity [0,1] | isInteriorFog (0/1) | 0 |
| `SB_Fog_Height` | waterSurfaceZ | playerAltitude | seaLevelDensity | falloffRate |

## Domain 4: Weather (10 float4s)

| Parameter | .x | .y | .z | .w |
|---|---|---|---|---|
| `SB_Weather_Wind` | speed [0,1] | direction (rad) | 0 | 0 |
| `SB_Weather_Precip` | type (0/1/2) | intensity [0,1] | 0 | 0 |
| `SB_Weather_Lightning` | frequency | isFlashing (0/1) | flashIntensity | timeSinceFlash (s) |
| `SB_Weather_Flags` | isPleasant | isCloudy | isRainy | isSnowy |
| `SB_Weather_Transition` | transition% [0,1] | outgoingWeatherID | currentWeatherID | 0 |
| `SB_Weather_PrecipSurface` | wetness [0,1] | puddleDepth | snowAccum | 0 |
| `SB_Weather_WindLive` | Sky::windSpeed | Sky::windAngle (rad) | cos(angle) | sin(angle) |
| `SB_Weather_PrecipLive` | particleDensity | lastDensity | Sky::flash | currentGameHour |
| `SB_Weather_CloudCover` | avgCloudAlpha | numActiveLayers | maxLayerAlpha | weatherPct |
| `SB_Weather_AuroraFade` | auroraIn | auroraOut | auroraInStart | auroraOutStart |

## Domain 5: Player (5 float4s)

| Parameter | .x | .y | .z | .w |
|---|---|---|---|---|
| `SB_Player_Position` | worldX | worldY | worldZ | altitude |
| `SB_Player_Vitals` | health% | stamina% | magicka% | level |
| `SB_Player_Movement` | speed (u/s) | isSprinting | isSwimming | isRiding |
| `SB_Player_Combat` | packed bitfield | beastForm (0/1/2) | timeScale | combatTargets |
| `SB_Player_Water` | isUnderwater (0/1) | waterSurfaceZ | submersionDepth | isWading (0/1) |

**Combat.x bitfield:** b0=inCombat, b1=bleedout, b2=killcam, b3=weaponDrawn

## Domain 6: Camera (23 float4s)

| Parameter | .x | .y | .z | .w |
|---|---|---|---|---|
| `SB_Camera_Info` | FOV (degrees) | nearClip | farClip | aspectRatio |
| `SB_Camera_Angles` | pitch (rad) | yaw (rad) | cameraStateEnum | 0 |
| `SB_Camera_WorldPos` | X | Y | Z | 0 |
| `SB_Camera_View0..3` | View matrix row 0-3 ||||
| `SB_Camera_Proj0..3` | Projection matrix row 0-3 ||||
| `SB_Camera_VP0..3` | ViewProj matrix row 0-3 ||||
| `SB_Camera_PrevVP0..3` | Previous frame VP row 0-3 ||||
| `SB_Camera_InvVP0..3` | Inverse VP row 0-3 ||||

## Domain 7: Interior (7 float4s)

| Parameter | .x | .y | .z | .w |
|---|---|---|---|---|
| `SB_Interior_IsInterior` | isInterior (0/1) | hasLightingTemplate | 0 | 0 |
| `SB_Interior_Ambient` | R | G | B | intensity |
| `SB_Interior_Directional` | R | G | B | fade |
| `SB_Interior_DirDir` | dir X | dir Y | dir Z | 0 |
| `SB_Interior_FogColor` | R | G | B | 0 |
| `SB_Interior_FogDist` | near | far | power | clipDist |
| `SB_Interior_LightingTemplate` | templateFormID | inheritFlags | 0 | 0 |

## Domain 8: Shadow (3 float4s)

| Parameter | .x | .y | .z | .w |
|---|---|---|---|---|
| `SB_Shadow_Direction` | dir X | dir Y | dir Z | shadow intensity |
| `SB_Shadow_Diffuse` | R | G | B | 0 |
| `SB_Shadow_Ambient` | R | G | B | 0 |

## Domain 9: Effects (4 float4s)

| Parameter | .x | .y | .z | .w |
|---|---|---|---|---|
| `SB_Effects_Vision` | nightEye | detectLife | detectDead | etherealForm |
| `SB_Effects_Time` | slowTimeFactor | isTimeStopped | 0 | 0 |
| `SB_Effects_Damage` | fire | frost | shock | poison |
| `SB_Effects_Misc` | isInvisible | isParalyzed | isDrunk | 0 |

## Domain 10: Render (4 float4s)

| Parameter | .x | .y | .z | .w |
|---|---|---|---|---|
| `SB_Render_Frame` | frameCount | deltaTime (s) | screenWidth | screenHeight |
| `SB_Render_Jitter` | TAA jitter X | TAA jitter Y | frameIndex%16 | timeDilation |
| `SB_Render_Depth` | rcp(far-near) | -near/(far-near) | near*far | 1/far |
| `SB_Render_Stencil` | available (0/1) | srvSlot | stencilBits | gamePaused (0/1) |

## Domain 11: Image Space (6 float4s)

| Parameter | .x | .y | .z | .w |
|---|---|---|---|---|
| `SB_IS_HDR` | eyeAdaptSpeed | bloomScale | bloomThreshold | sunlightScale |
| `SB_IS_Cinematic` | saturation | brightness | contrast | tintAlpha |
| `SB_IS_CineTint` | R | G | B | 0 |
| `SB_IS_DOF` | strength | distance | range | vignetteRadius |
| `SB_IS_IMOD` | hasActive (0/1) | strength | fadeIn | elapsed |
| `SB_IS_IMODTint` | R | G | B | blur amount |

## Domain 12: Nearby Lights (7 float4s)

| Parameter | .x | .y | .z | .w |
|---|---|---|---|---|
| `SB_Light0_PosRad` | worldX | worldY | worldZ | radius |
| `SB_Light0_Color` | R | G | B | intensity |
| `SB_Light1_PosRad` | worldX | worldY | worldZ | radius |
| `SB_Light1_Color` | R | G | B | intensity |
| `SB_Light2_PosRad` | worldX | worldY | worldZ | radius |
| `SB_Light2_Color` | R | G | B | intensity |
| `SB_Light_Summary` | total count | nearest dist | total flux | dominant hue |

## Domain 13: Actor Values (8 float4s)

| Parameter | .x | .y | .z | .w |
|---|---|---|---|---|
| `SB_AV_Resist` | fire% | frost% | shock% | magic% |
| `SB_AV_Resist2` | poison% | disease% | armor | 0 |
| `SB_AV_Combat` | atkDmgMult | wpnSpeedMult | critChance | unarmedDmg |
| `SB_AV_Movement` | speedMult | carryWeight | invWeight | encumRatio |
| `SB_AV_SkillCombat` | oneHanded | twoHanded | archery | block |
| `SB_AV_SkillMagic` | alteration | conjuration | destruction | illusion |
| `SB_AV_SkillMagic2` | restoration | enchanting | alchemy | 0 |
| `SB_AV_SkillStealth` | lightArmor | sneak | lockpicking | pickpocket |

## Domain 14: Crosshair (3 float4s)

| Parameter | .x | .y | .z | .w |
|---|---|---|---|---|
| `SB_XHair_Info` | hasTarget (0/1) | distance | formType | isActor (0/1) |
| `SB_XHair_Pos` | worldX | worldY | worldZ | boundingRadius |
| `SB_XHair_Actor` | health% | level | isHostile (0/1) | isEssential (0/1) |

## Domain 15: Equipment (4 float4s)

| Parameter | .x | .y | .z | .w |
|---|---|---|---|---|
| `SB_Equip_Right` | weaponType | baseDamage | isEnchanted (0/1) | enchantCharge |
| `SB_Equip_Left` | itemType | damage/armor | isEnchanted (0/1) | isSpell (0/1) |
| `SB_Equip_Armor` | totalRating | isHeavy (0/1) | isLight (0/1) | isRobes (0/1) |
| `SB_Equip_Flags` | weaponDrawn (0/1) | hasBow (0/1) | hasTorch (0/1) | isTwoHanding (0/1) |

## Domain 16: Quest (2 float4s)

| Parameter | .x | .y | .z | .w |
|---|---|---|---|---|
| `SB_Quest_Progress` | mainQuestStage | totalCompleted | activeCount | objectiveCount |
| `SB_Quest_Tracked` | trackedStage | questType | formID (low16) | hasMarker (0/1) |

## Domain 17: UI State (3 float4s)

| Parameter | .x | .y | .z | .w |
|---|---|---|---|---|
| `SB_UI_Menus` | inMenu (0/1) | inDialogue (0/1) | inInventory (0/1) | inMap (0/1) |
| `SB_UI_HUD` | hudVisible (0/1) | crosshairVisible (0/1) | cinematicMode (0/1) | loading (0/1) |
| `SB_UI_Detail` | crafting (0/1) | book (0/1) | lockpick (0/1) | console (0/1) |

## Domain 18: Computed Feedback (8 float4s)

GPU readback data with 1-frame delay. Populated by FeedbackProcessor.

| Parameter | .x | .y | .z | .w |
|---|---|---|---|---|
| `SB_Computed_Luminance` | smoothed center lum | instant center lum | center R | center G |
| `SB_Computed_Scene` | center B | sceneAvgLum | lumRange | feedbackValid (0/1) |
| `SB_Computed_SceneStats` | keyValue (log-avg) | contrastRatio | peripheryAvgLum | center/periphery |
| `SB_Computed_SceneColor` | avgR | avgG | avgB | colorTemp (K) |
| `SB_Computed_Histogram` | shadows (<0.05) | darks (<0.18) | mids (<0.50) | brights (>=0.50) |
| `SB_Computed_Temporal` | sceneCut (0/1) | lumVelocity | colorShift | stabilityScore |
| `SB_Computed_ENBReadback` | slot0 | slot1 | slot2 | slot3 |
| `SB_Computed_ENBReadback4` | slot4 float4 (or slot4-7 floats) ||||

## Domain 19: Region (3 float4s)

| Parameter | .x | .y | .z | .w |
|---|---|---|---|---|
| `SB_Region_Location` | locationFormID | parentLocFormID | worldspaceFormID | cellFormID |
| `SB_Region_Region` | regionFormID | hasWeatherOverride (0/1) | landMapWeight | typeFlags |
| `SB_Region_Worldspace` | hasLODWater (0/1) | defaultWaterLevel | mapCenterX | mapCenterY |

## Domain 20: Audio (2 float4s)

| Parameter | .x | .y | .z | .w |
|---|---|---|---|---|
| `SB_Audio_Music` | musicTypeFormID | priority | isCombat (0/1) | isDungeon (0/1) |
| `SB_Audio_Ambient` | isExterior (0/1) | reverbLevel | weatherSound (0/1) | 0 |

## Domain 21: NPC Detection (4 float4s)

| Parameter | .x | .y | .z | .w |
|---|---|---|---|---|
| `SB_NPC_Nearest` | distance | isHostile (0/1) | health% | level |
| `SB_NPC_NearestPos` | worldX | worldY | worldZ | isAlerted (0/1) |
| `SB_NPC_Summary` | hostileCount(30m) | friendlyCount(30m) | nearestHostileDist | nearestFriendlyDist |
| `SB_NPC_Threat` | threatRating [0,1] | stealthMeter [0,100] | highActorCount | maxDetection |

## Domain 22: Performance (2 float4s)

| Parameter | .x | .y | .z | .w |
|---|---|---|---|---|
| `SB_Perf_Timing` | gpuFrameMs | cpuFrameMs | presentLatencyMs | targetFps |
| `SB_Perf_Budget` | gpuBudget% [0,1] | qualityScale [0,1] | thermalState | frameDropCount |

## Domain 23: Scene Composition (16 float4s)

| Parameter | .x | .y | .z | .w |
|---|---|---|---|---|
| `SB_Scene_MatCount1` | general% | skin% | terrain% | vegetation% |
| `SB_Scene_MatCount2` | hair% | eye% | snow% | emissive% |
| `SB_Scene_DrawStats` | totalDraws | lightingDraws | metalGlossy% | 0 |
| `SB_Scene_CharLight` | enabled (0/1) | primary | secondary | luminance |
| `SB_Scene_AmbientSpec` | R | G | B | enabled (0/1) |
| `SB_Scene_MatProps1` | avgSpecPower | avgSpecScale | avgRoughness | avgSubSurface |
| `SB_Scene_MatProps2` | avgRimLight | avgEnvMap | avgAlpha | skinSpecPower |
| `SB_Scene_ShaderFlags` | envMapFrac | glowMapFrac | backLitFrac | softLitFrac |
| `SB_Scene_EngineState` | interior (0/1) | waterState | waterIntersect | shaderTech |
| `SB_Scene_EngineTimers` | timerDefault | timerDelta | timerSystem | timerRealDelta |
| `SB_Scene_DirAmbient1` | X+ R | X+ G | X+ B | X- luminance |
| `SB_Scene_DirAmbient2` | Y+ R | Y+ G | Y+ B | Y- luminance |
| `SB_Scene_DirAmbient3` | Z+ R | Z+ G | Z+ B | Z- luminance |
| `SB_Scene_SunGlare` | glareScale | occlusionTest | activeLights | shadowCasters |
| `SB_Scene_GeometryInfo` | avgLights/draw | maxLights/draw | avgPassEnum | LODModeAvg |
| `SB_Scene_WaterPlane` | normal X | normal Y | normal Z | plane distance |
| `SB_Scene_WaterColor` | R | G | B | alpha |
| `SB_Scene_WaterParams` | sunSpecPower | reflectionAmt | refractionMag | fresnelAmt |
| `SB_Scene_WaterWave` | dispDampener | flowmapScale | aboveWaterFogFar | underwaterFogFar |
| `SB_Scene_EffectShader` | drawCount | avgColorScale | avgSoftFalloff | avgFalloffOpacity |
| `SB_Scene_EffectColor` | R | G | B | avgAlpha |

## Domain 24: Theme (1 float4)

| Parameter | .x | .y | .z | .w |
|---|---|---|---|---|
| `SB_Theme_Config` | theme index (0-7) | reserved | reserved | reserved |

Read from enbeffect.fx and broadcast to all 9 shaders for synchronization.

---

## HLSL Declaration

All parameters must be declared with matching `UIName` annotations:

```hlsl
float4 SB_Sun_NDC < string UIName = "SB_Sun_NDC"; int UIHidden = 1; >;
```

`UIHidden = 1` keeps parameters out of ENB's editor GUI while remaining accessible to the SDK. The `UIName` must exactly match the parameter name used by SkyrimBridge's `ENBSetParameter` calls.

## Target Shaders

Parameters are pushed to these 9 shaders (uppercase, case-sensitive):

```
ENBEFFECTPREPASS.FX, ENBDEPTHOFFIELD.FX, ENBBLOOM.FX,
ENBADAPTATION.FX, ENBLENS.FX, ENBEFFECT.FX,
ENBEFFECTPOSTPASS.FX, ENBSUNSPRITE.FX, ENBUNDERWATER.FX
```
