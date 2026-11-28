// RawQuestStoryEngine.cpp: Clean-Room Procedural RPG Quest State Machine, Branching Dialogue/Persuasion
// Evaluator & Radiant Story Director. Original work, MIT. Zero Windows / D3D12 dependencies.

#include "RawQuestStoryEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <unordered_map>

namespace raw::worldgen {
namespace {

inline float ClampF(float v, float lo, float hi) noexcept {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

inline float Clamp01F(float v) noexcept {
    return ClampF(v, 0.0f, 1.0f);
}

inline float SafeF(float v, float fallback = 0.0f) noexcept {
    return std::isfinite(v) ? v : fallback;
}

std::string FmtF(float v) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(SafeF(v)));
    return std::string(buf);
}

std::vector<QuestStageNode> BuildStandardStages(QuestArchetype arch) {
    switch (arch) {
        case QuestArchetype::ClearDungeon:
            return {
                {10u, "Speak with the Jarl's steward and locate the dungeon entrance", {20u, 90u}, false, false},
                {20u, "Enter the vaulted chambers and defeat the boss guardian",       {30u, 90u}, false, false},
                {30u, "Return the recovered relic to the steward for payment",       {100u},     false, false},
                {90u, "Quest failed: retreated or relic lost",                       {},         false, true},
                {100u,"Dungeon cleared and reward claimed",                          {},         true,  false},
            };
        case QuestArchetype::EscortCaravan:
            return {
                {10u, "Meet the snowbound trade caravan along the mountain pass",    {20u, 90u}, false, false},
                {20u, "Clear bandit ambushers and guide the caravan to the city",    {30u, 90u}, false, false},
                {30u, "Collect escort commission from the caravan master",           {100u},     false, false},
                {90u, "Quest failed: caravan lost on the pass",                      {},         false, true},
                {100u,"Caravan safely escorted and bounty paid",                     {},         true,  false},
            };
        case QuestArchetype::DeliverSupply:
            return {
                {10u, "Acquire scarce supply crates from a surplus hold",            {20u, 90u}, false, false},
                {20u, "Deliver the supply shipment to the quartermaster",            {100u, 90u},false, false},
                {90u, "Quest failed: shipment spoiled or undelivered",               {},         false, true},
                {100u,"Supplies delivered and scarcity eased",                       {},         true,  false},
            };
        case QuestArchetype::BrokerTruce:
            return {
                {10u, "Receive diplomatic sealed letters from the hold court",       {20u, 30u, 90u}, false, false},
                {20u, "Negotiate ceasefire terms via Persuade, Bribe, or Intimidate",{30u, 90u},      false, false},
                {30u, "Deliver signed truce accord to both faction commanders",      {100u},          false, false},
                {90u, "Quest failed: negotiations collapsed into open hostility",    {},              false, true},
                {100u,"Regional truce brokered and peace restored",                  {},              true,  false},
            };
        case QuestArchetype::BountyHunt:
        default:
            return {
                {10u, "Inspect the Jarl's bounty warrant at the tavern",             {20u, 90u}, false, false},
                {20u, "Track and neutralize the target threat in the wilds",         {30u, 90u}, false, false},
                {30u, "Present proof of completion to the hold steward",             {100u},     false, false},
                {90u, "Quest failed: target escaped jurisdiction",                   {},         false, true},
                {100u,"Bounty completed and hold security restored",                 {},         true,  false},
            };
    }
}

}  // namespace

const char* GetQuestArchetypeName(QuestArchetype arch) {
    switch (arch) {
        case QuestArchetype::ClearDungeon:  return "ClearDungeon";
        case QuestArchetype::EscortCaravan: return "EscortCaravan";
        case QuestArchetype::DeliverSupply: return "DeliverSupply";
        case QuestArchetype::BrokerTruce:   return "BrokerTruce";
        case QuestArchetype::BountyHunt:    return "BountyHunt";
        default:                            return "UnknownQuest";
    }
}

const char* GetQuestLifecycleName(QuestLifecycle life) {
    switch (life) {
        case QuestLifecycle::Offered:   return "Offered";
        case QuestLifecycle::Active:    return "Active";
        case QuestLifecycle::Completed: return "Completed";
        case QuestLifecycle::Failed:    return "Failed";
        default:                        return "Unknown";
    }
}

const char* GetDialogueApproachName(DialogueApproach approach) {
    switch (approach) {
        case DialogueApproach::Standard:   return "Standard";
        case DialogueApproach::Persuade:   return "Persuade";
        case DialogueApproach::Bribe:      return "Bribe";
        case DialogueApproach::Intimidate: return "Intimidate";
        default:                           return "Unknown";
    }
}

void RawQuestStoryEngine::Clear() {
    quests_.clear();
    nextQuestId_         = 1u;
    rejectedTransitions_ = 0u;
    totalGoldAwarded_    = 0.0;
}

// ============================================================================
// Strict DAG Validation (3-Color DFS Cycle Detection + Edge Integrity)
// ============================================================================

bool RawQuestStoryEngine::ValidateQuestStageGraph(const std::vector<QuestStageNode>& stages,
                                                  std::string* outErr) {
    if (stages.empty()) {
        if (outErr) *outErr = "Quest stage graph is empty.";
        return false;
    }

    std::unordered_map<uint32_t, size_t> idToIndex;
    idToIndex.reserve(stages.size());
    bool hasTerminal = false;

    for (size_t i = 0u; i < stages.size(); ++i) {
        if (idToIndex.find(stages[i].stageId) != idToIndex.end()) {
            if (outErr) *outErr = "Duplicate stage ID in quest graph.";
            return false;
        }
        if (stages[i].isTerminalSuccess && stages[i].isTerminalFailure) {
            if (outErr) *outErr = "Stage cannot be both terminal success and terminal failure.";
            return false;
        }
        if ((stages[i].isTerminalSuccess || stages[i].isTerminalFailure) && !stages[i].allowedNextStages.empty()) {
            if (outErr) *outErr = "Terminal stage cannot have outgoing transitions.";
            return false;
        }
        if (stages[i].isTerminalSuccess || stages[i].isTerminalFailure) {
            hasTerminal = true;
        }
        idToIndex[stages[i].stageId] = i;
    }

    if (!hasTerminal) {
        if (outErr) *outErr = "Quest graph has no terminal stage.";
        return false;
    }

    // Check all outgoing edge targets exist and reject self-loops:
    for (size_t i = 0u; i < stages.size(); ++i) {
        for (uint32_t nextId : stages[i].allowedNextStages) {
            if (nextId == stages[i].stageId) {
                if (outErr) *outErr = "Self-loop cycle detected in quest stage graph.";
                return false;
            }
            if (idToIndex.find(nextId) == idToIndex.end()) {
                if (outErr) *outErr = "Dangling transition target stage ID in quest graph.";
                return false;
            }
        }
    }

    // 3-color DFS cycle detection (0=unvisited, 1=visiting/in-stack, 2=done):
    std::vector<uint8_t> color(stages.size(), 0u);
    auto hasCycle = [&](auto& self, size_t u) -> bool {
        color[u] = 1u;
        for (uint32_t nextId : stages[u].allowedNextStages) {
            const size_t v = idToIndex[nextId];
            if (color[v] == 1u) return true;
            if (color[v] == 0u && self(self, v)) return true;
        }
        color[u] = 2u;
        return false;
    };

    for (size_t i = 0u; i < stages.size(); ++i) {
        if (color[i] == 0u && hasCycle(hasCycle, i)) {
            if (outErr) *outErr = "Cycle detected in quest stage graph.";
            return false;
        }
    }
    return true;
}

uint32_t RawQuestStoryEngine::RegisterCustomQuest(QuestInstance quest, std::string* outErr) {
    if (!ValidateQuestStageGraph(quest.stages, outErr)) {
        ++rejectedTransitions_;
        return 0u;
    }
    bool startFound = false;
    for (const QuestStageNode& node : quest.stages) {
        if (node.stageId == quest.currentStageId) {
            startFound = true;
            break;
        }
    }
    if (!startFound) {
        quest.currentStageId = quest.stages.front().stageId;
    }
    quest.id         = nextQuestId_++;
    quest.lifecycle  = QuestLifecycle::Active;
    quest.rewardGold = std::max(0.0f, SafeF(quest.rewardGold, 100.0f));
    quest.stageHistory.clear();
    quest.stageHistory.push_back(quest.currentStageId);
    quests_.push_back(std::move(quest));
    return quests_.back().id;
}

uint32_t RawQuestStoryEngine::CreateArchetypeQuest(QuestArchetype archetype, HoldId holdId,
                                                   FactionId sponsorFaction,
                                                   CommodityKind commodity,
                                                   DungeonArchetype dungeonArch,
                                                   Vec3 objectivePos,
                                                   float rewardGold) {
    QuestInstance q{};
    q.archetype         = archetype;
    q.holdId            = (static_cast<size_t>(holdId) < kHoldCount) ? holdId : HoldId::Whiterun;
    q.sponsorFaction    = (static_cast<size_t>(sponsorFaction) < kFactionCount) ? sponsorFaction : FactionId::Companions;
    q.commodity         = (static_cast<size_t>(commodity) < kCommodityCount) ? commodity : CommodityKind::Grain;
    q.dungeonArchetype  = dungeonArch;
    q.objectiveWorldPos = objectivePos;
    q.stages            = BuildStandardStages(archetype);
    q.currentStageId    = q.stages.front().stageId;
    q.rewardGold        = std::max(50.0f, SafeF(rewardGold, 250.0f));
    q.rewardHoldRep     = (archetype == QuestArchetype::BrokerTruce) ? 16.0f : 12.0f;
    q.rewardFactionRep  = (archetype == QuestArchetype::BrokerTruce) ? 12.0f : 8.0f;

    switch (archetype) {
        case QuestArchetype::ClearDungeon:
            q.title = std::string("Purge ") + GetDungeonArchetypeName(dungeonArch) + " in " + GetHoldName(q.holdId);
            break;
        case QuestArchetype::EscortCaravan:
            q.title = std::string("Escort ") + GetCommodityName(q.commodity) + " Caravan to " + GetHoldName(q.holdId);
            break;
        case QuestArchetype::DeliverSupply:
            q.title = std::string("Deliver Urgent ") + GetCommodityName(q.commodity) + " to " + GetHoldName(q.holdId);
            break;
        case QuestArchetype::BrokerTruce:
            q.title = std::string("Broker Regional Truce for ") + GetHoldName(q.holdId);
            break;
        case QuestArchetype::BountyHunt:
        default:
            q.title = std::string("Jarl's Bounty Warrant in ") + GetHoldName(q.holdId);
            break;
    }

    return RegisterCustomQuest(std::move(q));
}

bool RawQuestStoryEngine::HasActiveArchetype(QuestArchetype arch) const {
    for (const QuestInstance& q : quests_) {
        if (q.lifecycle == QuestLifecycle::Active && q.archetype == arch) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// Emergent Radiant Quest Director
// ============================================================================

uint32_t RawQuestStoryEngine::GenerateRadiantQuests(const RadiantQuestInputs& inputs, uint32_t maxNewQuests) {
    if (maxNewQuests == 0u) return 0u;
    uint32_t spawned = 0u;

    // 1. ClearDungeon from live GeneratedDungeon boss objective:
    if (spawned < maxNewQuests && inputs.dungeonOpt != nullptr && !HasActiveArchetype(QuestArchetype::ClearDungeon)) {
        const GeneratedDungeon& d = *inputs.dungeonOpt;
        const HoldId dungeonHold = (d.archetype == DungeonArchetype::GlacialIceCavern)
            ? HoldId::Winterhold
            : ((d.archetype == DungeonArchetype::BasaltDwemerRuin) ? HoldId::Markarth : HoldId::Whiterun);
        const FactionId sponsor = (d.archetype == DungeonArchetype::GlacialIceCavern)
            ? FactionId::CollegeOfWinterhold
            : FactionId::Companions;
        const Vec3 bossPos{d.bossPos[0], d.bossPos[1], d.bossPos[2]};
        const float reward = 320.0f + static_cast<float>(d.rooms.size()) * 25.0f;
        if (CreateArchetypeQuest(QuestArchetype::ClearDungeon, dungeonHold, sponsor,
                                 CommodityKind::SoulGems, d.archetype, bossPos, reward) != 0u) {
            ++spawned;
        }
    }

    // 2. EscortCaravan from snowbound/ambushed caravan or BlizzardCaravanRescue event:
    if (spawned < maxNewQuests && !HasActiveArchetype(QuestArchetype::EscortCaravan)) {
        bool triggerEscort = false;
        HoldId targetHold = HoldId::Winterhold;
        CommodityKind comm = CommodityKind::Grain;
        float reward = 280.0f;

        if (inputs.events != nullptr) {
            if (const WorldEvent* ev = inputs.events->FindActiveByKind(WorldEventKind::BlizzardCaravanRescue)) {
                triggerEscort = true;
                targetHold    = ev->holdId;
                comm          = ev->commodity;
                reward        = std::max(260.0f, ev->rewardGold);
            } else if (const WorldEvent* amb = inputs.events->FindActiveByKind(WorldEventKind::CaravanBanditAmbush)) {
                triggerEscort = true;
                targetHold    = amb->holdId;
                comm          = amb->commodity;
                reward        = std::max(240.0f, amb->rewardGold);
            }
        }
        if (!triggerEscort && inputs.settlement != nullptr) {
            for (const TradeCaravan& tc : inputs.settlement->Caravans()) {
                if (tc.active && (tc.snowbound || tc.banditDelayHours > 0.0f)) {
                    triggerEscort = true;
                    targetHold    = tc.destHold;
                    comm          = tc.commodity;
                    reward        = 260.0f + tc.cargoUnits * 4.0f;
                    break;
                }
            }
        }
        if (triggerEscort) {
            if (CreateArchetypeQuest(QuestArchetype::EscortCaravan, targetHold, FactionId::ImperialLegion,
                                     comm, DungeonArchetype::NordicBarrow, {}, reward) != 0u) {
                ++spawned;
            }
        }
    }

    // 3. DeliverSupply from regional shortage in RawSettlementSim:
    if (spawned < maxNewQuests && inputs.settlement != nullptr && !HasActiveArchetype(QuestArchetype::DeliverSupply)) {
        HoldId scarceHold = HoldId::Winterhold;
        CommodityKind scarceComm = CommodityKind::Grain;
        float worstRatio = 1.15f;
        bool foundShortage = false;

        for (size_t h = 0u; h < kHoldCount; ++h) {
            const HoldState* hs = inputs.settlement->GetHold(static_cast<HoldId>(h));
            if (!hs) continue;
            for (size_t c = 0u; c < kCommodityCount; ++c) {
                const float ratio = hs->price[c] / std::max(1.0f, GetCommodityBasePrice(static_cast<CommodityKind>(c)));
                if (ratio > worstRatio) {
                    worstRatio    = ratio;
                    scarceHold    = hs->id;
                    scarceComm    = static_cast<CommodityKind>(c);
                    foundShortage = true;
                }
            }
        }
        if (foundShortage) {
            const float reward = std::round(180.0f * worstRatio);
            if (CreateArchetypeQuest(QuestArchetype::DeliverSupply, scarceHold, FactionId::ThievesGuild,
                                     scarceComm, DungeonArchetype::NordicBarrow, {}, reward) != 0u) {
                ++spawned;
            }
        }
    }

    // 4. BrokerTruce from civil-war faction polarization in RawFactionDiplomacy:
    if (spawned < maxNewQuests && inputs.diplomacy != nullptr && !HasActiveArchetype(QuestArchetype::BrokerTruce)) {
        const FactionStanding* imp = inputs.diplomacy->GetFactionStanding(FactionId::ImperialLegion);
        const FactionStanding* stm = inputs.diplomacy->GetFactionStanding(FactionId::Stormcloaks);
        if (imp && stm && std::abs(imp->reputation - stm->reputation) >= 20.0f) {
            const HoldId pivotHold = HoldId::Whiterun;
            if (CreateArchetypeQuest(QuestArchetype::BrokerTruce, pivotHold, FactionId::ImperialLegion,
                                     CommodityKind::Mead, DungeonArchetype::NordicBarrow, {}, 400.0f) != 0u) {
                ++spawned;
            }
        }
    }

    // 5. BountyHunt from hostile hold bounty or PredatorMigrationAlert event:
    if (spawned < maxNewQuests && !HasActiveArchetype(QuestArchetype::BountyHunt)) {
        bool triggerBounty = false;
        HoldId bountyHold  = HoldId::Falkreath;
        if (inputs.events != nullptr &&
            (inputs.events->FindActiveByKind(WorldEventKind::PredatorMigrationAlert) != nullptr ||
             inputs.events->FindActiveByKind(WorldEventKind::WildfireContainment) != nullptr)) {
            triggerBounty = true;
        }
        if (inputs.diplomacy != nullptr) {
            const DiplomacyTelemetry dt = inputs.diplomacy->ComputeTelemetry();
            if (dt.totalBountyGold >= 200.0 || dt.hostileHoldCount > 0u) {
                triggerBounty = true;
                bountyHold    = dt.mostHostileHold;
            }
        }
        if (triggerBounty) {
            if (CreateArchetypeQuest(QuestArchetype::BountyHunt, bountyHold, FactionId::Companions,
                                     CommodityKind::Pelts, DungeonArchetype::NordicBarrow, {}, 300.0f) != 0u) {
                ++spawned;
            }
        }
    }

    return spawned;
}

// ============================================================================
// State Machine Transitions & Branching Dialogue Evaluator
// ============================================================================

bool RawQuestStoryEngine::AdvanceQuestStage(uint32_t questId, uint32_t nextStageId,
                                            RawFactionDiplomacy* diplomacyOpt) {
    QuestInstance* q = nullptr;
    for (QuestInstance& cand : quests_) {
        if (cand.id == questId) {
            q = &cand;
            break;
        }
    }
    if (!q || q->lifecycle != QuestLifecycle::Active) {
        ++rejectedTransitions_;
        return false;
    }

    const QuestStageNode* curNode  = nullptr;
    const QuestStageNode* nextNode = nullptr;
    for (const QuestStageNode& n : q->stages) {
        if (n.stageId == q->currentStageId) curNode  = &n;
        if (n.stageId == nextStageId)       nextNode = &n;
    }
    if (!curNode || !nextNode) {
        ++rejectedTransitions_;
        return false;
    }

    const bool allowed = std::find(curNode->allowedNextStages.begin(),
                                   curNode->allowedNextStages.end(),
                                   nextStageId) != curNode->allowedNextStages.end();
    if (!allowed) {
        ++rejectedTransitions_;
        return false;
    }

    q->currentStageId = nextStageId;
    q->stageHistory.push_back(nextStageId);

    if (nextNode->isTerminalSuccess) {
        q->lifecycle = QuestLifecycle::Completed;
        totalGoldAwarded_ += static_cast<double>(q->rewardGold);
        if (diplomacyOpt != nullptr) {
            diplomacyOpt->AdjustHoldReputation(q->holdId, q->rewardHoldRep);
            diplomacyOpt->AdjustFactionReputation(q->sponsorFaction, q->rewardFactionRep);
        }
    } else if (nextNode->isTerminalFailure) {
        q->lifecycle = QuestLifecycle::Failed;
    }
    return true;
}

DialogueCheckResult RawQuestStoryEngine::EvaluateDialogueBranch(uint32_t questId,
                                                                const DialogueCheckInput& input,
                                                                RawFactionDiplomacy* diplomacyOpt) {
    DialogueCheckResult res{};
    QuestInstance* q = nullptr;
    for (QuestInstance& cand : quests_) {
        if (cand.id == questId) {
            q = &cand;
            break;
        }
    }
    if (!q || q->lifecycle != QuestLifecycle::Active) {
        return res;
    }

    const float speech = ClampF(SafeF(input.speechSkill, 25.0f), 0.0f, 100.0f);
    const float intim  = ClampF(SafeF(input.intimidationLevel, 10.0f), 1.0f, 100.0f);
    const float gold   = std::max(0.0f, SafeF(input.availableGold, 0.0f));
    const float rep    = ClampF(SafeF(input.holdReputation, 0.0f), -100.0f, 100.0f);
    const float dc     = ClampF(SafeF(input.difficultyDC, 50.0f), 10.0f, 100.0f);

    switch (input.approach) {
        case DialogueApproach::Persuade: {
            const float effScore = speech + rep * 0.25f;
            res.passProbability  = Clamp01F(0.50f + (effScore - dc) / 60.0f);
            res.succeeded        = (res.passProbability >= 0.50f);
            if (res.succeeded) {
                res.bonusRewardGold = std::round(q->rewardGold * 0.22f);
                res.holdRepDelta    = 4.0f;
                res.factionRepDelta = 3.0f;
            }
            break;
        }
        case DialogueApproach::Bribe: {
            const float requiredGold = dc * 4.0f;
            res.passProbability      = Clamp01F(gold / std::max(1.0f, requiredGold));
            res.succeeded            = (gold >= requiredGold);
            if (res.succeeded) {
                res.goldSpent       = requiredGold;
                res.holdRepDelta    = -2.0f;
                res.factionRepDelta = 1.0f;
            }
            break;
        }
        case DialogueApproach::Intimidate: {
            const float effScore = intim * 1.15f + speech * 0.45f;
            res.passProbability  = Clamp01F(0.50f + (effScore - dc) / 55.0f);
            res.succeeded        = (res.passProbability >= 0.50f);
            if (res.succeeded) {
                res.bonusRewardGold = std::round(q->rewardGold * 0.15f);
                res.holdRepDelta    = -4.0f;
                res.factionRepDelta = 2.0f;
            } else {
                res.holdRepDelta    = -6.0f;
            }
            break;
        }
        case DialogueApproach::Standard:
        default:
            res.passProbability = 1.0f;
            res.succeeded       = true;
            break;
    }

    if (res.succeeded && res.bonusRewardGold > 0.0f) {
        q->rewardGold += res.bonusRewardGold;
    }
    if (diplomacyOpt != nullptr) {
        if (res.holdRepDelta != 0.0f) {
            diplomacyOpt->AdjustHoldReputation(q->holdId, res.holdRepDelta);
        }
        if (res.factionRepDelta != 0.0f) {
            diplomacyOpt->AdjustFactionReputation(q->sponsorFaction, res.factionRepDelta);
        }
    }
    return res;
}

const QuestInstance* RawQuestStoryEngine::FindQuest(uint32_t questId) const {
    for (const QuestInstance& q : quests_) {
        if (q.id == questId) return &q;
    }
    return nullptr;
}

QuestDirectorTelemetry RawQuestStoryEngine::ComputeTelemetry() const {
    QuestDirectorTelemetry t{};
    t.totalGenerated      = static_cast<uint32_t>(quests_.size());
    t.rejectedTransitions = rejectedTransitions_;
    t.totalGoldAwarded    = totalGoldAwarded_;
    for (const QuestInstance& q : quests_) {
        if (q.lifecycle == QuestLifecycle::Active)         ++t.activeCount;
        else if (q.lifecycle == QuestLifecycle::Completed) ++t.completedCount;
        else if (q.lifecycle == QuestLifecycle::Failed)    ++t.failedCount;
    }
    return t;
}

std::string RawQuestStoryEngine::ExportJournalJson() const {
    const QuestDirectorTelemetry tel = ComputeTelemetry();
    std::ostringstream os;
    os << "{\n";
    os << "  \"format\": \"RAW_QUEST_JOURNAL\",\n";
    os << "  \"telemetry\": {\n";
    os << "    \"activeCount\": " << tel.activeCount << ",\n";
    os << "    \"completedCount\": " << tel.completedCount << ",\n";
    os << "    \"failedCount\": " << tel.failedCount << ",\n";
    os << "    \"totalGenerated\": " << tel.totalGenerated << ",\n";
    os << "    \"rejectedTransitions\": " << tel.rejectedTransitions << ",\n";
    os << "    \"totalGoldAwarded\": " << FmtF(static_cast<float>(tel.totalGoldAwarded)) << "\n";
    os << "  },\n";
    os << "  \"quests\": [\n";
    for (size_t i = 0u; i < quests_.size(); ++i) {
        const QuestInstance& q = quests_[i];
        os << "    {\"id\": " << q.id
           << ", \"archetype\": \"" << GetQuestArchetypeName(q.archetype) << "\""
           << ", \"lifecycle\": \"" << GetQuestLifecycleName(q.lifecycle) << "\""
           << ", \"hold\": \"" << GetHoldName(q.holdId) << "\""
           << ", \"faction\": \"" << GetFactionName(q.sponsorFaction) << "\""
           << ", \"currentStage\": " << q.currentStageId
           << ", \"rewardGold\": " << FmtF(q.rewardGold)
           << ", \"title\": \"" << q.title << "\"}"
           << (i + 1u < quests_.size() ? "," : "") << "\n";
    }
    os << "  ]\n";
    os << "}\n";
    return os.str();
}

}  // namespace raw::worldgen
