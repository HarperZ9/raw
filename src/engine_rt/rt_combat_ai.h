#pragma once
// rt_combat_ai.h: Tactical Squad Combat AI, Modern Action Combat & Living World Ecosystem Simulation (Zero SKSE).
// Features:
//   - Squad role coordinator (Vanguard / FlankerLeft / FlankerRight / Skirmisher / Support) eliminating 2011 conga lines
//   - Precision timed parry + posture/poise stagger + stamina-gated i-frame dodge + melee impact hit-stop
//   - Weather/time-of-day living world ecosystem directives (shelter seeking, hearth warming, dusk torches)
// Original work, MIT.

#include <cstdint>
#include <string>
#include <vector>

namespace raw::rt {

enum class TacticalRole : uint8_t {
    Vanguard         = 0,
    FlankerLeft      = 1,
    FlankerRight     = 2,
    SkirmisherRanged = 3,
    SupportCaster    = 4,
};

struct CombatAgentSnapshot {
    uint32_t actorId = 0;
    float    pos[3]  = {};
    float    hpFrac  = 1.0f;
    bool     isRanged = false;
    bool     isCaster = false;
};

struct TacticalOrder {
    uint32_t     actorId = 0;
    TacticalRole role = TacticalRole::Vanguard;
    float        desiredPos[3] = {};
    float        aggression = 0.5f;   // 0..1 pacing token (prevents 5 simultaneous attacks)
    bool         attackToken = false;
};

struct CombatActorState {
    float poiseCurrent     = 100.0f;
    float poiseMax         = 100.0f;
    float poiseRegenDelay  = 0.0f;
    float stamina          = 100.0f;
    float staminaMax       = 100.0f;
    float blockElapsedSec  = 0.0f;
    float dodgeIFrameSec   = 0.0f;
    float dodgeCooldownSec = 0.0f;
    float staggerSec       = 0.0f;
    float hitStopSec       = 0.0f;
    bool  blockHeld        = false;
};

struct IncomingHit {
    float hpDamage      = 0.0f;
    float poiseDamage   = 0.0f;
    bool  isHeavy       = false;
    bool  isUnblockable = false;
};

struct HitResolution {
    bool  perfectParry    = false;
    bool  dodgedIFrame    = false;
    bool  blocked         = false;
    bool  defenderStagger = false;
    bool  attackerStagger = false;
    float appliedHpDamage = 0.0f;
    float appliedStamina  = 0.0f;
    float hitStopMs       = 0.0f;
};

enum class WorldNpcDirective : uint8_t {
    NormalRoutine = 0,
    SeekShelter   = 1,
    WarmAtBrazier = 2,
    EquipTorch    = 3,
    AlertPatrol   = 4,
};

// Assigns non-overlapping tactical squad roles and flanking anchor positions around targetPos.
std::vector<TacticalOrder> CoordinateSquadTactics(const float targetPos[3],
                                                  const float targetForward[2],
                                                  const std::vector<CombatAgentSnapshot>& agents,
                                                  uint32_t maxSimultaneousAttackers = 2);

// Advances defender combat timers (block window, dodge i-frames, poise regen, stagger, hit-stop).
void StepCombatTimers(CombatActorState& st, bool blockHeldNow, bool blockEdgeNow, bool dodgeEdgeNow, float dtSec);

// Resolves an incoming strike against defender state with precision timed parry, i-frame dodge, and poise break.
HitResolution ResolveCombatHit(CombatActorState& defender, CombatActorState& attacker,
                               const IncomingHit& hit, float parryWindowMs, float hitStopBaseMs);

// Evaluates weather, volumetric snow depth, and 24-hour clock to determine living-world NPC behavior.
WorldNpcDirective EvaluateWorldEcosystem(float gameHour, float rain01, float snow01, float snowDepthU,
                                         bool nearFire, bool inCombat);

// Mean-Payoff / Condon Simple Stochastic Game Tactical Policy Solver:
// Computes optimal action over 4 combat states via exact Bellman value iteration (converges monotonically in <= 24 steps).
enum class TacticalAction : uint8_t {
    PressAttack    = 0,
    FlankCircle    = 1,
    GuardParry     = 2,
    RetreatRecover = 3,
};

struct TacticalGameState {
    float hpFrac         = 1.0f;
    float staminaFrac    = 1.0f;
    float poiseFrac      = 1.0f;
    float distUnits      = 140.0f;
    bool  hasAttackToken = true;
    bool  enemyAttacking = false;
};

struct TacticalPolicyResult {
    TacticalAction bestAction = TacticalAction::FlankCircle;
    float          actionValues[4] = {};
    float          expectedPayoff  = 0.0f;
    uint32_t       iterations      = 0;
};

TacticalPolicyResult SolveTacticalStochasticGame(const TacticalGameState& st);

// Exact CPU reference of the O(1) 8D E8 optimal sphere-packing lattice quantizer (matches RawTensorMath.hlsli::rawQuantizeE8).
void QuantizeE8Lattice(const float in8[8], float scale, float out8[8]);

// High-FPS Physics Substep Governor (replaces Havok Fix + Display Tweaks, 30..360 Hz stable):
struct PhysicsSubstepPlan {
    float    clampedFrameDt = 1.0f / 60.0f;
    float    substepDt      = 1.0f / 120.0f;
    uint32_t substeps       = 2;
    bool     spikeClamped   = false;
};
PhysicsSubstepPlan ComputePhysicsSubstep(float rawFrameDtSec);

// Environmental Thermal Exposure & Survival Simulation (replaces Frostfall + Wet & Cold natively at 0.001 ms):
struct ThermalExposureState {
    float ambientTempC     = 12.0f;
    float coreWarmth       = 85.0f;   // 0 (hypothermia) .. 100 (warm)
    float shiverIntensity  = 0.0f;    // 0 .. 1
    float staminaRegenMult = 1.0f;    // 0.35 .. 1.15
    bool  freezingRisk     = false;
};
ThermalExposureState EvaluateThermalExposure(float altitudeZ, float windSpeed, float rain01, float snow01,
                                             bool inInterior, bool nearFire, float prevWarmth, float dtSec);

// Swept Weapon Arc-Momentum Hitbox & Sweet-Spot Evaluation (replaces Precision + Valhalla Combat):
struct WeaponArcHitResult {
    bool  hit           = false;
    float closestDist   = 1.0e9f;
    float impactParamT  = 0.0f;       // 0 = hilt, 1 = tip sweet-spot
    float momentumMult  = 1.0f;
    float poiseImpulse  = 0.0f;
};
WeaponArcHitResult EvaluateWeaponArcHit(const float swingStart[3], const float swingEnd[3],
                                        const float targetCenter[3], float targetRadius,
                                        float targetHeight, float swingSpeed);

// Physical Sabine-Eyring 3D Acoustic Room Reverb & Occlusion Estimator (replaces True 3D Sound / Reverb Overhaul):
enum class AcousticEnvClass : uint8_t {
    OpenSky       = 0,
    AlpineValley  = 1,
    StoneInterior = 2,
    CavernVault   = 3,
    Underwater    = 4,
};

struct AcousticReverbState {
    float            rt60Sec         = 0.25f;
    float            earlyReflGain   = 0.15f;
    float            lowPassHz       = 18000.0f;
    float            airAbsorbDbPerM = 0.005f;
    AcousticEnvClass envClass        = AcousticEnvClass::OpenSky;
};

AcousticReverbState ComputeAcousticReverb(bool inInterior, float enclosureRadiusU,
                                          float snowCover01, float rain01,
                                          float windSpeed, bool underwater);

// Critically-Damped 3D Camera Smooth-Follow & Target Lock-On Solver (replaces SmoothCam / True Directional Movement):
struct SmoothCameraLockResult {
    float smoothedPos[3] = {};
    float lockYawRad     = 0.0f;
    float lockPitchRad   = 0.0f;
    float dynamicFovDeg  = 80.0f;
    bool  lockedOn       = false;
};
SmoothCameraLockResult SolveSmoothCameraLockOn(const float prevCamPos[3], const float desiredCamPos[3],
                                               const float playerPos[3], const float* lockTargetPos,
                                               float baseFovDeg, float springOmega, float dtSec);

// Dynamic Living-World Hold Economy & Supply Scarcity Model (replaces Trade Routes / Supply & Demand):
struct DynamicEconomyState {
    float buyPriceMult  = 1.0f;
    float sellPriceMult = 1.0f;
    float scarcityIndex = 0.0f;   // 0 (surplus) .. 1 (severe storm shortage)
    uint8_t marketTier  = 1;      // 0=Surplus, 1=Balanced, 2=Tight, 3=Crisis
};
DynamicEconomyState EvaluateDynamicEconomy(float storm01, float snowDepthU, float gameHour,
                                           uint32_t nearbyActors, bool inInterior);

// Save-File Bloat & String-Table Safety Inspector (replaces external save-bloat warning monitors):
struct SaveHealthReport {
    uint32_t totalSaves    = 0;
    float    largestSaveMb = 0.0f;
    bool     bloatWarning  = false;   // true when largest .ess exceeds 18 MB
};
SaveHealthReport InspectSaveHealth(const char* savesDirPath);

// Wave 23 Deterministic Boruvka-Contraction Minimum Spanning Tree (inspired by Khanna-Putterman-Song O(m) MST):
// Halves active components every round to build minimal spatial connectivity trees over NPC squads & light clusters.
struct MstEdge {
    uint32_t u = 0;
    uint32_t v = 0;
    float    weight = 0.0f;
};
struct BoruvkaMstResult {
    std::vector<MstEdge> treeEdges;
    float    totalWeight   = 0.0f;
    uint32_t components    = 0;
    uint32_t rounds        = 0;
};
BoruvkaMstResult ComputeBoruvkaMst(uint32_t nodeCount, const std::vector<MstEdge>& edges);

// Wave 23 Quadratic-Reciprocity Legendre Character (a | p) CPU reference (matches RawTensorMath.hlsli::rawLegendreChar):
int LegendreSymbol(int a, int oddPrimeP);

// Wave 23 LoopCD / Richardson Iterative Contrast CPU reference (matches RawTensorMath.hlsli::rawLoopCdContrast):
void EvaluateLoopCdContrast(const float passEarly[3], const float passRefined[3], float omega, float outRgb[3]);

// Wave 25 First-Step Trapping-Region Disocclusion Separator CPU reference (matches LightingCommon.hlsli::rawTrappingSeparator):
float EvaluateTrappingSeparator(float curVal, float prevVal, float sigma, float speedPx, float edgeGate);

// Wave 25 Mod-Manager-Aware Skyrim.ini sLocalSavePath resolver (finds __MO_Saves or custom profile save dirs):
std::string ResolveSkyrimSavesDir(const char* docsSkyrimDir);

// Wave 25 Bounded Live Dynamic Economy + Thermal Stamina GMST feedback (feeds Skyrim's live Setting singletons):
struct DynamicGameplayGmsts {
    float barterMin              = 1.35f;
    float barterMax              = 3.60f;
    float sprintStaminaDrainMult = 0.82f;
};
DynamicGameplayGmsts ComputeDynamicGameplayGmsts(float economyBuyMult, float staminaRegenMult);

// Wave 26 Oscillatory-Feedback Extremum Seeking Controller (zero-model real-time GPU budget / exposure optimizer):
// Probes an unknown live cost surface l(x) via x = xHat + amp * sin(phase), high-pass filters l(x), demodulates
// against sin(phase), and integrates xHat down the estimated gradient without any hand-tuned analytic cost model.
struct ExtremumSeekingState {
    float xHat      = 0.5f;   // estimated optimal parameter in [xMin, xMax]
    float costLp    = 0.0f;   // low-pass baseline for washout high-pass filter
    float phaseRad  = 0.0f;
    bool  init      = false;
};
float StepExtremumSeekingGovernor(ExtremumSeekingState& st, float measuredCost, float dtSec,
                                  float xMin = 0.0f, float xMax = 1.0f,
                                  float ditherAmp = 0.04f, float omegaRadPerSec = 12.0f, float gain = 0.65f);

// Wave 27 Anatomical Body-Zone Damage, Injury & Dismemberment Physics Engine:
enum class BodyZone : uint8_t {
    Head     = 0,
    Torso    = 1,
    LeftArm  = 2,
    RightArm = 3,
    LeftLeg  = 4,
    RightLeg = 5,
};
struct BodyDamageInput {
    BodyZone zone          = BodyZone::Torso;
    float    rawDamage     = 25.0f;
    float    weaponMassKg  = 2.2f;
    float    swingSpeedMs  = 9.5f;
    float    edgeSharpness = 0.80f;   // 0=blunt mace, 1=razor blade
    float    armorRating   = 0.0f;
    float    targetHpNow   = 100.0f;
    float    targetHpMax   = 100.0f;
};
struct BodyDamageResult {
    float    finalDamage     = 0.0f;
    float    bleedDps        = 0.0f;
    float    staggerSec      = 0.0f;
    float    moveSpeedMult   = 1.0f;
    float    attackSpeedMult = 1.0f;
    bool     limbCrippled    = false;
    bool     dismembered     = false;
    bool     decapitated     = false;
};
BodyDamageResult EvaluateBodyDamageAndInjury(const BodyDamageInput& in);

// Wave 27 3D Voxel-Based Structural Fracture & Destructible Prop Engine (8x8x8 = 512 voxels):
enum class VoxelMaterial : uint8_t {
    Wood    = 0,
    Pottery = 1,
    Ice     = 2,
    Stone   = 3,
};
struct VoxelFragment {
    float    centerLocal[3] = {};
    float    impulseVel[3]  = {};
    uint32_t voxelCount     = 0;
};
struct RawVoxelDestructible {
    uint8_t       cells[8 * 8 * 8] = {};   // 0=empty, 1..255=structural integrity
    VoxelMaterial material         = VoxelMaterial::Wood;
    uint32_t      solidCount       = 0;
};
void InitVoxelProp(RawVoxelDestructible& prop, VoxelMaterial mat, bool hollowBox = false);
std::vector<VoxelFragment> ApplyVoxelImpact(RawVoxelDestructible& prop, const float localHit[3],
                                            const float impactDir[3], float impactForce, float blastRadiusCells);

// Wave 27 Procedural Synoptic Barometric Storm & Weather Simulator:
struct SynopticWeatherState {
    float pressureHpa     = 1013.25f;   // 972 (cyclone) .. 1032 (clear high)
    float cloudCover01    = 0.25f;
    float rainRate01      = 0.0f;
    float snowRate01      = 0.0f;
    float windSpeedMs     = 4.0f;
    float stormLightning  = 0.0f;
    float snowLineAltU    = 2400.0f;
};
SynopticWeatherState StepSynopticWeatherSim(const float worldPos[3], float simTimeSec, float gameHour);

// Wave 28 Procedural Creature & Character Locomotion / Gait Kinematics Engine:
// Drives smooth biomechanical stride phase, turn-banking roll, slope-matched spine pitch & dragon wing-vortex timing.
enum class CreatureGaitKind : uint8_t {
    Biped     = 0,
    Quadruped = 1,
    Mammoth   = 2,
    Dragon    = 3,
};
struct CreatureGaitOutput {
    float stridePhaseRad[4] = {};   // FL, FR, RL, RR (or L/R legs + arms for Biped; wings/legs for Dragon)
    float bodyBankRollRad   = 0.0f;
    float spinePitchRad     = 0.0f;
    float verticalBobU      = 0.0f;
    float cadenceHz         = 0.0f;
};
CreatureGaitOutput EvaluateCreatureGaitKinematics(CreatureGaitKind kind, float moveSpeedU, float turnRateRadPerSec,
                                                  float terrainSlopeRad, float simTimeSec);

// Wave 34 Unified Planetary Gravity, Dual-Moon Nirn Harmonic Tides (Masser M2 + Secunda N2 + Magnus S2) & Sea State:
// Connects Calendar::gameDaysPassed, Sky::currentGameHour, altitude, wind, and synoptic barometric pressure
// into one physically coherent gravity + tidal potential oracle shared by CPU world simulation and GPU shaders!
struct PlanetaryPhysicsState {
    float gravityMps2      = 9.80665f;   // altitude-corrected planetary gravity (m/s^2)
    float gravityUnitsS2   = 686.466f;   // gravity in Skyrim units/s^2 (70 u/m)
    float tideHeightU      = 0.0f;       // harmonic lunar-solar tide + inverse-barometer surge (Skyrim units)
    float tideVelocityMs   = 0.0f;       // signed flood (+)/ebb (-) tidal current speed (m/s)
    float springTide01     = 0.5f;       // 0 = quadrature neap tide, 1 = Masser/Secunda/Sun syzygy spring tide
    float beaufortScale    = 2.0f;       // physical Beaufort sea state 0..12
    float swellDirX        = 0.866f;     // dominant ocean swell propagation direction X
    float swellDirY        = 0.500f;     // dominant ocean swell propagation direction Y
};
PlanetaryPhysicsState EvaluatePlanetaryPhysics(float daysPassed, float gameHour, const float worldPos[3],
                                               float windSpeedMs, float pressureHpa);

}  // namespace raw::rt
