//=============================================================================
//  SkeletonVisualizer.cpp — 3D debug overlay for actor bone hierarchies
//
//  Traverses NiNode hierarchies from actor 3D roots and submits bone links
//  and optional name labels to DebugRenderer for real-time display.
//
//  Safety: All NiNode traversal is wrapped in SEH (__try/__except) because
//  actor 3D state can become invalid during cell transitions or unloading.
//  The SEH wrapper lives in a plain function with no C++ objects on the
//  stack (no destructors) so __try/__except is legal under MSVC.
//
//  Author: Zain Dana Harper
//=============================================================================

#include "SkeletonVisualizer.h"
#include "DebugRenderer.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <cstring>
#include <algorithm>

namespace SB
{

// ════════════════════════════════════════════════════════════════════════
//  Constants
// ════════════════════════════════════════════════════════════════════════

static constexpr uint32_t kMaxRecurseDepth = 20;

// ABGR color constants (A=0xFF, then B, G, R — matching DebugRenderer byte order)
//                                              A  B    G    R
static constexpr uint32_t kColorSpine   = 0xFF'00'C8'00;  // green  (0,200,0)
static constexpr uint32_t kColorArm     = 0xFF'C8'C8'00;  // cyan   (0,200,200)
static constexpr uint32_t kColorLeg     = 0xFF'00'C8'C8;  // yellow (200,200,0)
static constexpr uint32_t kColorHead    = 0xFF'50'50'FF;  // red    (255,80,80)
static constexpr uint32_t kColorWeapon  = 0xFF'C8'00'C8;  // magenta(200,0,200)
static constexpr uint32_t kColorDefault = 0xFF'C8'C8'C8;  // grey   (200,200,200)
static constexpr uint32_t kColorWhite   = 0xFFFFFFFF;

// ════════════════════════════════════════════════════════════════════════
//  SEH wrapper — must live in a function with no C++ objects on the
//  stack (no destructors), so __try/__except is legal under MSVC.
// ════════════════════════════════════════════════════════════════════════

static bool SEH_WrapDrawSkeleton(SkeletonVisualizer* vis, RE::Actor* actor) noexcept
{
    __try
    {
        vis->DrawSkeleton(actor);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// ════════════════════════════════════════════════════════════════════════
//  Singleton
// ════════════════════════════════════════════════════════════════════════

SkeletonVisualizer& SkeletonVisualizer::Get()
{
    static SkeletonVisualizer instance;
    return instance;
}

// ════════════════════════════════════════════════════════════════════════
//  Public API — settings
// ════════════════════════════════════════════════════════════════════════

void SkeletonVisualizer::SetEnabled(bool enabled)
{
    m_enabled = enabled;
    if (!enabled) {
        m_boneCount  = 0;
        m_actorCount = 0;
    }
}

bool SkeletonVisualizer::IsEnabled() const { return m_enabled; }

void SkeletonVisualizer::SetTarget(SkeletonTarget target) { m_target = target; }
SkeletonTarget SkeletonVisualizer::GetTarget() const      { return m_target; }

void SkeletonVisualizer::SetShowBoneNames(bool show) { m_showBoneNames = show; }

void SkeletonVisualizer::SetMaxActors(int max)
{
    m_maxActors = (std::max)(max, 1);
}

void SkeletonVisualizer::SetRange(float range)
{
    m_range = (std::max)(range, 128.0f);
}

uint32_t SkeletonVisualizer::GetBoneCount()  const { return m_boneCount; }
uint32_t SkeletonVisualizer::GetActorCount() const { return m_actorCount; }

// ════════════════════════════════════════════════════════════════════════
//  Update — called once per frame
// ════════════════════════════════════════════════════════════════════════

void SkeletonVisualizer::Update()
{
    if (!m_enabled)
        return;

    if (!DebugRenderer::Get().IsInitialized())
        return;

    m_boneCount  = 0;
    m_actorCount = 0;

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player)
        return;

    switch (m_target)
    {
    case SkeletonTarget::Player:
    {
        if (player->Get3D()) {
            SEH_WrapDrawSkeleton(this, player);
        }
        break;
    }

    case SkeletonTarget::CrosshairRef:
    {
        // Use CrosshairPickData to find the actor under the crosshair
        auto* crosshair = RE::CrosshairPickData::GetSingleton();
        if (!crosshair)
            break;

        auto targetHandle = crosshair->target;
        if (!targetHandle)
            break;

        auto targetPtr = targetHandle.get();
        if (!targetPtr)
            break;

        auto* targetRef = targetPtr.get();
        if (!targetRef)
            break;

        auto* targetActor = targetRef->As<RE::Actor>();
        if (targetActor && targetActor->Get3D()) {
            SEH_WrapDrawSkeleton(this, targetActor);
        }
        break;
    }

    case SkeletonTarget::AllNearby:
    {
        auto playerPos = player->GetPosition();
        int count = 0;

        // Always include the player first
        if (player->Get3D()) {
            SEH_WrapDrawSkeleton(this, player);
            count++;
        }

        // Scan high-process actors (nearby, loaded, active AI)
        auto* processList = RE::ProcessLists::GetSingleton();
        if (!processList)
            break;

        for (auto& handle : processList->highActorHandles) {
            if (count >= m_maxActors)
                break;

            auto actorPtr = handle.get();
            if (!actorPtr)
                continue;

            auto* actor = actorPtr.get();
            if (!actor || actor == player || actor->IsDead())
                continue;

            if (!actor->Get3D())
                continue;

            // Range check
            auto actorPos = actor->GetPosition();
            float dist = actorPos.GetDistance(playerPos);
            if (dist > m_range)
                continue;

            SEH_WrapDrawSkeleton(this, actor);
            count++;
        }
        break;
    }

    default:
        break;
    }
}

// ════════════════════════════════════════════════════════════════════════
//  DrawSkeleton — entry point for a single actor
// ════════════════════════════════════════════════════════════════════════

void SkeletonVisualizer::DrawSkeleton(RE::Actor* actor)
{
    if (!actor)
        return;

    auto* root3D = actor->Get3D();
    if (!root3D)
        return;

    auto* rootNode = root3D->AsNode();
    if (!rootNode)
        return;  // geometry leaf, not a node tree

    // Draw the root with no parent (parent == nullptr draws a marker)
    DrawBoneRecursive(rootNode, nullptr, 0);
    m_actorCount++;
}

// ════════════════════════════════════════════════════════════════════════
//  DrawBoneRecursive — traverse the NiNode bone tree
// ════════════════════════════════════════════════════════════════════════

void SkeletonVisualizer::DrawBoneRecursive(RE::NiNode* node, RE::NiNode* parent,
                                           uint32_t depth)
{
    if (!node || depth > kMaxRecurseDepth)
        return;

    RE::NiPoint3 nodePos = node->world.translate;

    if (parent) {
        // Draw a bone link from parent to this node
        RE::NiPoint3 parentPos = parent->world.translate;

        const char* name = node->name.c_str();
        uint32_t color = GetBoneColor(name, depth);

        DebugRenderer::Get().DrawBone(&parentPos.x, &nodePos.x, color);
        m_boneCount++;

        // Optional bone name label
        if (m_showBoneNames && name && name[0]) {
            DebugRenderer::Get().DrawLabel(&nodePos.x, name, kColorWhite);
        }
    } else {
        // Root node — draw a self-referencing marker and optional label
        DebugRenderer::Get().DrawBone(&nodePos.x, &nodePos.x, kColorWhite);

        if (m_showBoneNames) {
            const char* name = node->name.c_str();
            if (name && name[0])
                DebugRenderer::Get().DrawLabel(&nodePos.x, name, kColorWhite);
        }
    }

    // Recurse into children
    for (auto& child : node->GetChildren()) {
        if (!child)
            continue;

        auto* childNode = child->AsNode();
        if (childNode) {
            DrawBoneRecursive(childNode, node, depth + 1);
        }
    }
}

// ════════════════════════════════════════════════════════════════════════
//  GetBoneColor — heuristic coloring by bone name substring
// ════════════════════════════════════════════════════════════════════════

uint32_t SkeletonVisualizer::GetBoneColor(const char* boneName, uint32_t depth) const
{
    if (!boneName || !boneName[0])
        return kColorDefault;

    // Spine / torso
    if (std::strstr(boneName, "Spine") || std::strstr(boneName, "spine"))
        return kColorSpine;

    // Arms / hands
    if (std::strstr(boneName, "Arm")      || std::strstr(boneName, "Hand")     ||
        std::strstr(boneName, "Finger")   || std::strstr(boneName, "Clavicle"))
        return kColorArm;

    // Legs / feet
    if (std::strstr(boneName, "Leg")   || std::strstr(boneName, "Foot")  ||
        std::strstr(boneName, "Toe")   || std::strstr(boneName, "Thigh") ||
        std::strstr(boneName, "Calf"))
        return kColorLeg;

    // Head / neck
    if (std::strstr(boneName, "Head") || std::strstr(boneName, "Neck") ||
        std::strstr(boneName, "Jaw")  || std::strstr(boneName, "Eye"))
        return kColorHead;

    // Weapons / equipment
    if (std::strstr(boneName, "Weapon") || std::strstr(boneName, "Shield") ||
        std::strstr(boneName, "Quiver"))
        return kColorWeapon;

    return kColorDefault;
}

} // namespace SB
