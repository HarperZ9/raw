#pragma once
// RawQuestStoryEngine.h: Clean-Room Procedural RPG Quest State Machine, Branching Dialogue/Persuasion
// Evaluator & Radiant Story Director (`raw::worldgen`). Pure portable C++23 with zero Windows or D3D12 deps.
// Couples:
//   - `RawSettlementSim`      (regional commodity shortages & snowbound/ambushed trade caravans)
//   - `RawFactionDiplomacy`   (hold bounties, reputation, civil-war tension & deterministic quest rewards)
//   - `RawDungeonGen`         (Nordic Barrow / Glacial Ice Cavern / Basalt Dwemer boss clear objectives)
//   - `RawWorldEventDirector` (blizzards, wildfires, caravan ambushes & aurora gatherings).
// Features multi-stage DAG quests (strict cycle & invalid-transition rejection), speech/bribe/intimidate
// branching dialogue checks, and deterministic JSON quest journal export.
// Original work, MIT.

#include "RawDungeonGen.h"
#include "RawFactionDiplomacy.h"
#include "RawSettlementSim.h"
#include "RawWorldEventDirector.h"

#include <cstdint>
#include <string>
#include <vector>

namespace raw::worldgen {

enum class QuestArchetype : uint8_t {
    ClearDungeon  = 0,
    EscortCaravan = 1,
    DeliverSupply = 2,
    BrokerTruce   = 3,
    BountyHunt    = 4,
    Count         = 5,
};

enum class QuestLifecycle : uint8_t {
    Offered   = 0,
    Active    = 1,
    Completed = 2,
    Failed    = 3,
};

enum class DialogueApproach : uint8_t {
    Standard   = 0,
    Persuade   = 1,
    Bribe      = 2,
    Intimidate = 3,
};

struct QuestStageNode {
    uint32_t              stageId           = 0u;
    std::string           objectiveText;
    std::vector<uint32_t> allowedNextStages;
    bool                  isTerminalSuccess = false;
    bool                  isTerminalFailure = false;
};

struct QuestInstance {
    uint32_t                    id                = 0u;
    QuestArchetype              archetype         = QuestArchetype::ClearDungeon;
    QuestLifecycle              lifecycle         = QuestLifecycle::Active;
    HoldId                      holdId            = HoldId::Whiterun;
    FactionId                   sponsorFaction    = FactionId::Companions;
    CommodityKind               commodity         = CommodityKind::Grain;
    DungeonArchetype            dungeonArchetype  = DungeonArchetype::NordicBarrow;
    Vec3                        objectiveWorldPos{};
    std::string                 title;
    uint32_t                    currentStageId    = 10u;
    float                       rewardGold        = 250.0f;
    float                       rewardHoldRep     = 10.0f;
    float                       rewardFactionRep  = 6.0f;
    std::vector<QuestStageNode> stages;
    std::vector<uint32_t>       stageHistory;
};

struct DialogueCheckInput {
    DialogueApproach approach          = DialogueApproach::Persuade;
    float            speechSkill       = 50.0f;  // [0..100]
    float            intimidationLevel = 20.0f;  // [1..100]
    float            availableGold     = 500.0f; // Player gold available for Bribe
    float            holdReputation    = 0.0f;   // [-100..+100]
    float            difficultyDC      = 50.0f;  // [10..100]
};

struct DialogueCheckResult {
    bool     succeeded       = false;
    float    passProbability = 0.0f;
    float    goldSpent       = 0.0f;
    float    bonusRewardGold = 0.0f;
    float    holdRepDelta    = 0.0f;
    float    factionRepDelta = 0.0f;
};

struct RadiantQuestInputs {
    const RawSettlementSim*      settlement = nullptr;
    const RawFactionDiplomacy*   diplomacy  = nullptr;
    const RawWorldEventDirector* events     = nullptr;
    const GeneratedDungeon*      dungeonOpt = nullptr;
    uint32_t                     seed       = 202659u;
};

struct QuestDirectorTelemetry {
    uint32_t activeCount         = 0u;
    uint32_t completedCount      = 0u;
    uint32_t failedCount         = 0u;
    uint32_t totalGenerated      = 0u;
    uint32_t rejectedTransitions = 0u;
    double   totalGoldAwarded    = 0.0;
};

const char* GetQuestArchetypeName(QuestArchetype arch);
const char* GetQuestLifecycleName(QuestLifecycle life);
const char* GetDialogueApproachName(DialogueApproach approach);

class RawQuestStoryEngine {
public:
    void Clear();

    // Validates that a quest stage graph is a clean DAG (non-empty, unique IDs, valid edges, zero cycles).
    static bool ValidateQuestStageGraph(const std::vector<QuestStageNode>& stages,
                                        std::string* outErr = nullptr);

    // Registers a custom quest (strictly rejects invalid or cyclic stage graphs; returns 0 on failure).
    uint32_t RegisterCustomQuest(QuestInstance quest, std::string* outErr = nullptr);

    // Creates a standard multi-stage quest of `archetype` with verified DAG stage progression.
    uint32_t CreateArchetypeQuest(QuestArchetype archetype, HoldId holdId,
                                  FactionId sponsorFaction,
                                  CommodityKind commodity = CommodityKind::Grain,
                                  DungeonArchetype dungeonArch = DungeonArchetype::NordicBarrow,
                                  Vec3 objectivePos = {},
                                  float rewardGold = 250.0f);

    // Evaluates live world systems and spawns up to `maxNewQuests` non-duplicate radiant quests.
    uint32_t GenerateRadiantQuests(const RadiantQuestInputs& inputs, uint32_t maxNewQuests = 3u);

    // Advances `questId` to `nextStageId` iff transition is explicitly allowed in current stage's DAG.
    // On terminal success, applies deterministic hold & faction reputation rewards to `diplomacyOpt`.
    bool AdvanceQuestStage(uint32_t questId, uint32_t nextStageId,
                           RawFactionDiplomacy* diplomacyOpt = nullptr);

    // Evaluates deterministic Speech / Bribe / Intimidate dialogue check for an active quest,
    // updating quest bonus gold and optional live diplomacy standings.
    DialogueCheckResult EvaluateDialogueBranch(uint32_t questId,
                                               const DialogueCheckInput& input,
                                               RawFactionDiplomacy* diplomacyOpt = nullptr);

    const QuestInstance*              FindQuest(uint32_t questId) const;
    const std::vector<QuestInstance>& Quests() const noexcept { return quests_; }
    QuestDirectorTelemetry            ComputeTelemetry() const;

    // Deterministic zero-em-dash JSON quest journal export:
    std::string ExportJournalJson() const;

private:
    bool HasActiveArchetype(QuestArchetype arch) const;

    std::vector<QuestInstance> quests_;
    uint32_t                   nextQuestId_         = 1u;
    uint32_t                   rejectedTransitions_ = 0u;
    double                     totalGoldAwarded_    = 0.0;
};

}  // namespace raw::worldgen
