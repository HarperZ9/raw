# RAW Mod Compatibility & Conflict Matrix (Skyrim SE `1.6.1170.0` & `1.7.104.0`)

Verified against live vanilla `1.7.104.0` and heavily modded `1.6.1170.0` (`SkyGround Chronicles`, 232 SKSE plugins).

## 1. Incompatible Mods (Must Disable)

These hook the same D3D11 device, swapchain, or ENB-only shader entry points that RAW replaces natively in DirectX 12:

| Mod / DLL | Conflict Reason | Action |
|---|---|---|
| **ENBSeries Binaries** (`d3d11.dll`, `d3dcompiler_46e.dll`) | Replaces `d3d11.dll` directly | Disable mod / remove binaries |
| **ENB Presets** (e.g. `Truth ENB`, `Cabbage`, `Rudy`, `Silent Horizons`) | Requires ENBSeries binaries | Disable preset |
| **Community Shaders** (`CommunityShaders.dll` + CS feature DLLs) | Hooks D3D11 shaders & swapchain | Disable CS + CS feature DLLs |
| **ReShade** (`dxgi.dll` / `d3d11.dll`) | Conflicts with `raw::tx` DX12 swapchain | Remove ReShade |
| **SSE Display Tweaks** (`SSEDisplayTweaks.dll`) | Hooks DXGI swapchain/flip model | Disable (RAW owns tearing, borderless & Reflex) |
| **Skyrim Upscaler / DLSS / FSR / FrameGen** | Conflicts with RAW's native Streamline SDK | Disable (use RAW `DlssMode=1..5` directly) |
| **Terrain Helper for ENB** (`TerrainHelper.dll`) | ENB-only D3D11 terrain hook | Disable DLL (keep `TerrainHelper.esp` if downstream patches master it) |

## 2. Compatible & Auto-Coexisting Major Frameworks

| Category | Verified Mods / Frameworks | Notes |
|---|---|---|
| **Core Engine Fixes** | `SSE Engine Fixes` (`EngineFixes.dll`), `Bug Fixes SSE`, `Scrambled Bugs`, `po3_Tweaks`, `Papyrus Tweaks NG` | RAW auto-detects `EngineFixes.dll` (`#309`) and defers memory-heavy LOD/tree distance inflation to prevent 32 GB RAM OOM on mega-modlists. |
| **Skeleton & Physics** | `Faster HDT-SMP` (`hdtSMP64.dll`), `CBPC`, `XP32/XPMSSE`, `CBBE 3BA`, `HIMBO` | Compatible; `raw::tx` validates IB/VB pointers and `RtSkin.hlsl` skins actors directly into the DXR 1.2 BVH. |
| **Animation & Combat** | `Open Animation Replacer (OAR)`, `BFCO`, `Animation Motion Revolution`, `Precision`, `True Directional Movement`, `SmoothCam`, `SkyParkour`, `IFrame Generator RE`, `Dismembering Framework`, `Core Impact Framework` | Compatible; RAW's native tactical combat AI & stagger GMSTs coexist cleanly. |
| **Weather, Lighting & Water Assets** | `NAT.ENB III` (ESP), `EVLaS`, `Water for ENB` (meshes/textures), `ENB Light` (meshes), `Complex Parallax` texture packs, `Cathedral 3D Grass`, `Grass Cache Helper NG` | Keep `.esp` plugins that downstream patches master; RAW shades all 4K/parallax/water/foliage assets natively in DX12. |
| **Multi-Version Note (`1.6.1170` vs `1.7.104`)** | `ContentCatalog.txt` | If Steam updated Skyrim SE to `1.7.104.0`, delete/back up `%LOCALAPPDATA%\Skyrim Special Edition\ContentCatalog.txt` before launching a `1.6.1170.0` Stock Game modlist (prevents Bethesda's `CSV2_` `invalid stoull argument` main-menu CTD). |
