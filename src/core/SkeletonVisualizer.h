#pragma once
//=============================================================================
//  SkeletonVisualizer.h — 3D debug overlay for actor bone hierarchies
//
//  Traverses NiNode trees from actor 3D roots and submits bone links
//  (parent->child lines with joint diamonds) and optional name labels
//  to the DebugRenderer for real-time visualization.
//
//  Modes:
//    Player       — player character only
//    CrosshairRef — whatever the crosshair is pointing at
//    AllNearby    — all actors within configurable range (max capped)
//
//  Usage:
//    SkeletonVisualizer::Get().SetEnabled(true);
//    SkeletonVisualizer::Get().Update();  // call once per frame
//
//  Author: Zain Dana Harper
//=============================================================================

#include <cstdint>

// Forward declarations — keep RE/SKSE out of the header
namespace RE {
    class Actor;
    class NiNode;
}

namespace SB
{

enum class SkeletonTarget : uint8_t
{
    Player,         // player character only
    CrosshairRef,   // whatever the crosshair is pointing at
    AllNearby       // all actors within range (max 10)
};

class SkeletonVisualizer
{
public:
    static SkeletonVisualizer& Get();

    void SetEnabled(bool enabled);
    bool IsEnabled() const;

    void Update();

    void SetTarget(SkeletonTarget target);
    SkeletonTarget GetTarget() const;
    void SetShowBoneNames(bool show);
    void SetMaxActors(int max);     // for AllNearby mode
    void SetRange(float range);     // for AllNearby mode

    uint32_t GetBoneCount() const;
    uint32_t GetActorCount() const;

    // Public for SEH wrapper access (must be called from a function with
    // no C++ objects on the stack so __try/__except is legal under MSVC)
    void DrawSkeleton(RE::Actor* actor);

private:
    SkeletonVisualizer() = default;

    void DrawBoneRecursive(RE::NiNode* node, RE::NiNode* parent, uint32_t depth);
    uint32_t GetBoneColor(const char* boneName, uint32_t depth) const;

    bool m_enabled = false;
    SkeletonTarget m_target = SkeletonTarget::Player;
    bool m_showBoneNames = false;
    int m_maxActors = 10;
    float m_range = 4096.0f;

    uint32_t m_boneCount = 0;
    uint32_t m_actorCount = 0;
};

} // namespace SB
