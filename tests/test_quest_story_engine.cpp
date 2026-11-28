// test_quest_story_engine.cpp: Verification Suite for `raw::worldgen::RawQuestStoryEngine`.
// Exports BOTH `int run_quest_story_tests()` and
// `int raw::worldgen::RunQuestStoryTests(void (*checkFn)(bool, const char*) = nullptr)`
// plus `#ifdef RAW_QUEST_STORY_TEST_STANDALONE` standalone `main()`.
// Original work, MIT.

#include "RawQuestStoryEngine.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace raw::worldgen {

int RunQuestStoryTests(void (*checkFn)(bool, const char*)) {
    int failures = 0;
    auto check = [&](bool cond, const char* name) {
        if (!cond) {
            ++failures;
        }
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s %s\n", cond ? "PASS" : "FAIL", name);
        }
    };

    // =========================================================================
    // Test 1: Planted-failure controls (cyclic graph, dangling edge, invalid transition)
    // =========================================================================
    {
        RawQuestStoryEngine eng;
        std::string err;

        // 1a. Cycle graph (10 -> 20 -> 10) MUST be strictly rejected:
        QuestInstance cyclic{};
        cyclic.title          = "Broken Cyclic Quest";
        cyclic.currentStageId = 10u;
        cyclic.stages = {
            {10u, "Loop A", {20u}, false, false},
            {20u, "Loop B", {10u, 100u}, false, false},
            {100u,"End",    {}, true, false},
        };
        const uint32_t cyclicId = eng.RegisterCustomQuest(cyclic, &err);

        // 1b. Dangling edge target (10 -> 777 nonexistent) MUST be rejected:
        QuestInstance dangling{};
        dangling.currentStageId = 10u;
        dangling.stages = {
            {10u, "Start", {777u}, false, false},
            {100u,"End",   {}, true, false},
        };
        const uint32_t danglingId = eng.RegisterCustomQuest(dangling, &err);

        // 1c. Invalid stage jump (10 -> 100 skipping 20 & 30) on valid quest MUST be rejected:
        const uint32_t qId = eng.CreateArchetypeQuest(QuestArchetype::ClearDungeon, HoldId::Whiterun,
                                                      FactionId::Companions);
        const bool badJumpRejected    = !eng.AdvanceQuestStage(qId, 100u);
        const bool unknownIdRejected  = !eng.AdvanceQuestStage(9999u, 20u);
        const bool stayedAtStage10    = (eng.FindQuest(qId)->currentStageId == 10u);

        check(cyclicId == 0u && danglingId == 0u && badJumpRejected && unknownIdRejected &&
                  stayedAtStage10 && eng.ComputeTelemetry().rejectedTransitions >= 3u,
              "quest_story_planted_cycle_and_invalid_transition_rejection");
    }

    // =========================================================================
    // Test 2: Branching Dialogue Checks (Persuade / Bribe / Intimidate) & Monotonicity
    // =========================================================================
    {
        RawQuestStoryEngine eng;
        RawFactionDiplomacy dip;
        const uint32_t qId = eng.CreateArchetypeQuest(QuestArchetype::BrokerTruce, HoldId::Whiterun,
                                                      FactionId::ImperialLegion, CommodityKind::Mead,
                                                      DungeonArchetype::NordicBarrow, {}, 300.0f);
        const float initialReward = eng.FindQuest(qId)->rewardGold;

        // Low-speech persuasion fails; high-speech persuasion succeeds & boosts reward + reputation!
        DialogueCheckInput lowSpeech{};
        lowSpeech.approach     = DialogueApproach::Persuade;
        lowSpeech.speechSkill  = 15.0f;
        lowSpeech.difficultyDC = 60.0f;
        const DialogueCheckResult rLow = eng.EvaluateDialogueBranch(qId, lowSpeech, &dip);

        DialogueCheckInput highSpeech = lowSpeech;
        highSpeech.speechSkill = 85.0f;
        const DialogueCheckResult rHigh = eng.EvaluateDialogueBranch(qId, highSpeech, &dip);

        // Bribe check: insufficient gold fails, sufficient gold succeeds:
        DialogueCheckInput cheapBribe{};
        cheapBribe.approach      = DialogueApproach::Bribe;
        cheapBribe.availableGold = 50.0f;
        cheapBribe.difficultyDC  = 50.0f; // Requires 200g
        const DialogueCheckResult rCheap = eng.EvaluateDialogueBranch(qId, cheapBribe);

        DialogueCheckInput richBribe = cheapBribe;
        richBribe.availableGold = 300.0f;
        const DialogueCheckResult rRich = eng.EvaluateDialogueBranch(qId, richBribe);

        // Intimidate high-level succeeds with bonus reward & rep penalty:
        DialogueCheckInput intim{};
        intim.approach          = DialogueApproach::Intimidate;
        intim.intimidationLevel = 55.0f;
        intim.speechSkill       = 40.0f;
        intim.difficultyDC      = 55.0f;
        const DialogueCheckResult rIntim = eng.EvaluateDialogueBranch(qId, intim);

        std::printf(
            "  [Branching Dialogue] persuade(low=%.2f pass=%d, high=%.2f pass=%d +%.0fg) "
            "bribe(cheap=%d rich=%d spent=%.0fg) intim(pass=%d)\n",
            rLow.passProbability, rLow.succeeded ? 1 : 0, rHigh.passProbability, rHigh.succeeded ? 1 : 0,
            rHigh.bonusRewardGold, rCheap.succeeded ? 1 : 0, rRich.succeeded ? 1 : 0, rRich.goldSpent,
            rIntim.succeeded ? 1 : 0);

        check(!rLow.succeeded && rHigh.succeeded && rHigh.passProbability > rLow.passProbability &&
                  (eng.FindQuest(qId)->rewardGold > initialReward) &&
                  !rCheap.succeeded && rRich.succeeded && rRich.goldSpent == 200.0f &&
                  rIntim.succeeded && rIntim.holdRepDelta < 0.0f,
              "quest_story_branching_dialogue_persuade_bribe_intimidate");
    }

    // =========================================================================
    // Test 3: Coupled 4-System Radiant Quest Generation, Completion Rewards & JSON Journal
    // =========================================================================
    {
        // Spin up all 4 live world systems:
        RawSettlementSim settle(5901u);
        SettlementWeatherMap blizWx{};
        blizWx.byHold[static_cast<size_t>(HoldId::Windhelm)].blizzard01   = 0.92f;
        blizWx.byHold[static_cast<size_t>(HoldId::Winterhold)].blizzard01 = 0.95f;
        settle.StepHours(24.0f, blizWx);

        RawFactionDiplomacy dip;
        dip.AdjustFactionReputation(FactionId::ImperialLegion, 45.0f);
        dip.RecordCrime(HoldId::Riften, CrimeSeverity::GrandTheft, true);

        RawWorldEventDirector events;
        WorldEventInputs evIn{};
        evIn.settlement = &settle;
        evIn.blizzard01 = 0.92f;
        events.StepHours(1.0f, evIn);

        GeneratedDungeon barrow{};
        DungeonGenConfig dcfg{};
        dcfg.archetype = DungeonArchetype::NordicBarrow;
        dcfg.seed      = 5959u;
        dcfg.roomCount = 7u;
        const bool okDungeon = RawDungeonGen::GenerateDungeon(dcfg, barrow);

        RawQuestStoryEngine story;
        RadiantQuestInputs rqIn{};
        rqIn.settlement = &settle;
        rqIn.diplomacy  = &dip;
        rqIn.events     = &events;
        rqIn.dungeonOpt = &barrow;
        const uint32_t generated = story.GenerateRadiantQuests(rqIn, 5u);

        // Complete the generated ClearDungeon quest step-by-step (10 -> 20 -> 30 -> 100) and verify diplomacy reward!
        uint32_t dungeonQuestId = 0u;
        HoldId   dungeonHold    = HoldId::Whiterun;
        for (const QuestInstance& q : story.Quests()) {
            if (q.archetype == QuestArchetype::ClearDungeon) {
                dungeonQuestId = q.id;
                dungeonHold    = q.holdId;
                break;
            }
        }

        const float repBefore = dip.GetHoldStanding(dungeonHold)->reputation;
        const bool s1 = story.AdvanceQuestStage(dungeonQuestId, 20u, &dip);
        const bool s2 = story.AdvanceQuestStage(dungeonQuestId, 30u, &dip);
        const bool s3 = story.AdvanceQuestStage(dungeonQuestId, 100u, &dip);
        const bool postTerminalRejected = !story.AdvanceQuestStage(dungeonQuestId, 90u, &dip);
        const float repAfter  = dip.GetHoldStanding(dungeonHold)->reputation;

        const QuestDirectorTelemetry tel = story.ComputeTelemetry();
        const std::string journalJson    = story.ExportJournalJson();
        const bool journalOk =
            (journalJson.find("\"format\": \"RAW_QUEST_JOURNAL\"") != std::string::npos) &&
            (journalJson.find("\"ClearDungeon\"") != std::string::npos) &&
            (journalJson.find("\"Completed\"") != std::string::npos) &&
            (journalJson.find("nan") == std::string::npos);

        std::printf(
            "  [Radiant Story Director] generated=%u completed=%u active=%u goldAwarded=%.0f holdRep %.1f->%.1f jsonBytes=%zu\n",
            generated, tel.completedCount, tel.activeCount, tel.totalGoldAwarded,
            repBefore, repAfter, journalJson.size());

        check(okDungeon && generated >= 4u && dungeonQuestId != 0u && s1 && s2 && s3 &&
                  postTerminalRejected && (repAfter > repBefore + 8.0f) &&
                  tel.completedCount == 1u && tel.totalGoldAwarded > 300.0 && journalOk,
              "quest_story_radiant_director_completion_and_journal_export");
    }

    return failures;
}

}  // namespace raw::worldgen

int run_quest_story_tests() {
    return raw::worldgen::RunQuestStoryTests(nullptr);
}

#ifdef RAW_QUEST_STORY_TEST_STANDALONE
int main() {
    const int failures = run_quest_story_tests();
    std::printf("RawQuestStoryEngine standalone test summary: %s (%d failures)\n",
                failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
#endif
