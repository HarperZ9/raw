# RAW v1.0.0-r304 Release Notes (Skyrim SE Native DX12 + SM 6.9 + DXR 1.2 + DLSS-RR)

## Release Package Contents (`RAW-SkyrimSE-DX12-v1.0.0-r304.zip`)

- `d3d11.dll` — Native D3D11 -> DX12 translation layer (`raw::tx`) + SM 6.9 / DXR 1.2 render graph + in-game studio overlay (`End` key)
- `raw_engine.dll` — Compiled Creation Engine runtime (`1.6.1170.0` & `1.7.104.0` support, synoptic weather, Havok 120 Hz, tactical combat AI, `_setmaxstdio(8192)`, live heuristic auditor)
- `d3d11_proxy.ini` — Portable public-clean default configuration (`AgilityPath=D3D12`, `DlssMode=2`, `TxDepth32=0`)
- `raw_versions/` — Verified address tables (`skyrimse-1.6.1170.tsv`, `skyrimse-1.7.104.tsv`, `entries.tsv`)
- `D3D12/D3D12Core.dll` — DirectX 12 Agility SDK 1.620.1
- `sl.*.dll` + `nvngx_dlss*.dll` — NVIDIA Streamline 2.14.16 + DLSS Ray Reconstruction (Preset F) + DLAA (Preset K) + Reflex
- `README.txt`, `LICENSE`, `SHA256SUMS.txt`

## Highlights in v1.0.0-r304 (260 PRs Merged `#45` to `#304`)

- **Pipeline & Performance:** `8.31 ms` to `12.78 ms` (`78` to `120 FPS` at `1920x1080` with full DXR 1.2 + DLSS-RR Preset F), `2.47–3.68 ms` total D3D12 pass, Native DX12 Compute Hi-Z (`240x135`) saving `44k–145k` tris/frame, zero PC/NPC RTAO shadow trails, zero screen-edge mesh/decal/reflection pop-in.
- **Sky, Clouds & Weather:** Hillaire 2020 physical atmosphere decoupled from `ShadowSceneNode`'s Moon flip (`isRealSun()`), 3-layer 4D Ekman-sheared clouds (path-traced 8-octave cumulus + 96k altocumulus + 130k cirrus), ground-up synoptic weather simulator, 42°/51° rainbows, 22° lunar halo, Eddington solar limb darkening, multi-band galactic nebula, and volcanic ashstorms with glowing embers.
- **Water & Materials:** DLSS-RR-before-RtWater flowing fluid waves with Nyquist band-limiting, Snell's Window/TIR underwater optics, engine-`waterHeight` reflective water-bounce caustics on bridges and piers, EON rough diffuse, MAT-01 dual-lobe clearcoat, Belcour-Barla Airy thin-film iridescence, 3D sastrugi snowdrifts with glacial SSS, steep-cliff tri-planar strata, and dual-lobe character skin/eye shading.
- **Tonemaps, Camera Optics & Artistic Modes:** True Sobotka AgX, exact Stephen Hill ACES RRT+ODT, Purkinje scotopic night shift, full-range cinema DoF, 2-stage Kodak 5219 halation + 6-tap Panavision anamorphic flare, 6-blade diffraction starburst, 8 photochemical film stocks, and 4 real-time artistic shaders (Oil Impasto, Engraved Ink Hatch, 16-Bit Pixel Bayer, Watercolor).
- **Engine Ownership:** `_setmaxstdio(8192)` false-save-corruption fix, 120 Hz sub-stepped Havok physics, 16 tactical combat AI & ARPG stagger GMSTs, expanded decal/grass/tree budgets, and live `engine commands=audit` heuristic verifier.
