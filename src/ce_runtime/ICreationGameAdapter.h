#pragma once
// ICreationGameAdapter.h: Wave 86 Shared Cross-Title Engine Adapter Contract.
// Unifies SkyrimSE, Fallout4, Starfield, and SuperframeNative above one shared
// clean-room raw_ce_core + superframe/native kernel layer with zero game-specific
// #ifdef leaks inside job graph, Hi-Z culling, crowd ECS, navmesh, or meshlet builders.
// Clean-room MIT, MSVC /W4 /WX clean.

#include <cstdint>
#include <memory>
#include <string_view>

namespace raw::ce
{

enum class CreationTitleId : uint32_t
{
    SkyrimSE         = 1u,
    Fallout4         = 2u,
    Starfield        = 3u,
    SuperframeNative = 4u,
};

struct TitleContractProfile
{
    CreationTitleId  titleId                 = CreationTitleId::SkyrimSE;
    std::string_view titleName;
    std::string_view executableName;
    std::string_view archiveExtension;
    uint32_t         expectedRttiClassCount  = 0u;
    uint64_t         expectedRttiContractTag = 0ull;
    float            worldUnitsPerMeter      = 70.0f;
    uint32_t         defaultTickHz           = 100u;
    bool             usesLeftHandedZUp       = true;
};

class ICreationGameAdapter
{
public:
    virtual ~ICreationGameAdapter() = default;

    [[nodiscard]] virtual TitleContractProfile GetProfile() const noexcept = 0;

    // Fail-closed RTTI/ABI contract verification: rejects cross-title hook leakage!
    [[nodiscard]] virtual bool ValidateRttiContract(uint64_t observedContractTag) const noexcept
    {
        return observedContractTag == GetProfile().expectedRttiContractTag;
    }

    [[nodiscard]] virtual float WorldUnitsToMeters(float worldUnits) const noexcept
    {
        const float u = GetProfile().worldUnitsPerMeter;
        return (u > 0.0f) ? (worldUnits / u) : worldUnits;
    }

    [[nodiscard]] virtual uint64_t ComputeDeterministicProfileHash() const noexcept
    {
        const auto p = GetProfile();
        uint64_t h = 14695981039346656037ull;
        auto mix = [&](uint64_t v) {
            h ^= v;
            h *= 1099511628211ull;
        };
        mix(static_cast<uint64_t>(p.titleId));
        mix(static_cast<uint64_t>(p.expectedRttiClassCount));
        mix(p.expectedRttiContractTag);
        mix(static_cast<uint64_t>(p.defaultTickHz));
        for (char c : p.titleName) {
            mix(static_cast<uint8_t>(c));
        }
        return h;
    }
};

class SkyrimSeAdapter final : public ICreationGameAdapter
{
public:
    [[nodiscard]] TitleContractProfile GetProfile() const noexcept override
    {
        return TitleContractProfile{
            CreationTitleId::SkyrimSE,
            "SkyrimSE",
            "SkyrimSE.exe",
            ".bsa",
            7376u,
            0x534B5952494D3137ull, // 'SKYRIM17' (1.7.104.0 contract)
            70.0f,
            100u,
            true
        };
    }
};

class Fallout4Adapter final : public ICreationGameAdapter
{
public:
    [[nodiscard]] TitleContractProfile GetProfile() const noexcept override
    {
        return TitleContractProfile{
            CreationTitleId::Fallout4,
            "Fallout4",
            "Fallout4.exe",
            ".ba2",
            9120u,
            0x464F344E47323032ull, // 'FO4NG202'
            70.0f,
            100u,
            true
        };
    }
};

class StarfieldAdapter final : public ICreationGameAdapter
{
public:
    [[nodiscard]] TitleContractProfile GetProfile() const noexcept override
    {
        return TitleContractProfile{
            CreationTitleId::Starfield,
            "Starfield",
            "Starfield.exe",
            ".ba2",
            14820u,
            0x5354415246434532ull, // 'STARFCE2'
            1.0f,
            100u,
            false
        };
    }
};

class SuperframeNativeAdapter final : public ICreationGameAdapter
{
public:
    [[nodiscard]] TitleContractProfile GetProfile() const noexcept override
    {
        return TitleContractProfile{
            CreationTitleId::SuperframeNative,
            "SuperframeNative",
            "superframe_native.exe",
            ".rmsh",
            512u,
            0x535550455246524Dull, // 'SUPERFRM'
            1.0f,
            100u,
            false
        };
    }
};

inline std::unique_ptr<ICreationGameAdapter> CreateGameAdapter(CreationTitleId titleId)
{
    switch (titleId) {
        case CreationTitleId::SkyrimSE:         return std::make_unique<SkyrimSeAdapter>();
        case CreationTitleId::Fallout4:         return std::make_unique<Fallout4Adapter>();
        case CreationTitleId::Starfield:        return std::make_unique<StarfieldAdapter>();
        case CreationTitleId::SuperframeNative: return std::make_unique<SuperframeNativeAdapter>();
    }
    return nullptr;
}

} // namespace raw::ce
