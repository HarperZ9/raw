#pragma once
//=============================================================================
//  TextureDump — Save GPU textures to disk for shader debugging
//
//  Press F5 in-game to dump all active effect SRVs as BMP files to
//  Data/SKSE/Plugins/RAW/Captures/frameNNNN_effectName.bmp
//
//  Each dump captures one frame's worth of effect outputs at full resolution.
//  Open in any image viewer to inspect pixel values, check for artifacts,
//  verify depth conventions, etc.
//
//  Usage from C++:
//    TextureDump::SaveSRV(device, context, srv, "GTAO_output", frameIndex);
//    TextureDump::DumpAllEffects(device, context, frameIndex); // F5 shortcut
//=============================================================================

#include <d3d11.h>
#include <cstdint>
#include <string>
#include <filesystem>

namespace SB
{

class TextureDump
{
public:
    /// Save a single SRV's underlying texture to a BMP file.
    /// Returns true if the file was written successfully.
    static bool SaveSRV(ID3D11Device* dev, ID3D11DeviceContext* ctx,
                        ID3D11ShaderResourceView* srv,
                        const std::string& name, uint32_t frameIndex);

    /// Dump all active effect SRVs in one call (triggered by F5).
    static void DumpAllEffects(ID3D11Device* dev, ID3D11DeviceContext* ctx,
                               uint32_t frameIndex);

    /// Set the output directory (called once at init).
    static void SetOutputDir(const std::filesystem::path& dir);

    /// Tier 1.1: did the most recent DumpAllEffects see a float NaN/Inf?
    static bool LastDumpSawNaN();

private:
    /// Copy a Texture2D to a staging texture, map it, write as BMP.
    static bool SaveTexture2D(ID3D11Device* dev, ID3D11DeviceContext* ctx,
                              ID3D11Texture2D* tex,
                              const std::string& filepath);

    /// Write raw RGBA8 pixel data as a BMP file.
    static bool WriteBMP(const std::string& filepath,
                         const uint8_t* pixels, uint32_t width, uint32_t height,
                         uint32_t rowPitch);
};

} // namespace SB
