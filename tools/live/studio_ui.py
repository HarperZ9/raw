"""RAW Live Studio: zero-overhead granular customization, weather, preset, and HLSL hot-reload UI.

Talks directly to the running RAW live-session service (tools/live/req.py) so users can:
- Switch Quality=0..4 presets and save/load modular JSON presets (global or per-weather)
- Tune every [Lighting], Tonemap, Cloud, Weather, and Debug View (0..21) parameter live (~100 ms)
- Scrub time of day, force/blend weather FormIDs, teleport across test biomes, and run raw_engine commands
- Edit any HLSL SM6.6+ shader (Lighting, Tonemap, RtShadow, RtWater, Clouds, LightFog, Sss) and hot-reload live
- Trigger live keyframe captures and 120-frame GPU/CPU profiler measurements.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any
from urllib.parse import parse_qs, urlparse

REQ_PY = pathlib.Path(r"E:\wt\raw-live\tools\live\req.py")
SHADER_DIR = pathlib.Path(r"E:\wt\raw-rt9\src\d3d12_bridge\shaders")
PRESET_DIR = pathlib.Path(r"E:\wt\raw-rt9\presets")

EDITABLE_SHADERS = [
    "Lighting.hlsl",
    "Tonemap.hlsl",
    "VolumetricPass.hlsl",
    "FroxelPass.hlsl",
    "GiAtrousPass.hlsl",
    "BloomPass.hlsl",
    "AtmosphereLut.hlsl",
    "RtReflFilter.hlsl",
    "DofPass.hlsl",
    "RtShadow.hlsl",
    "RtSkyFilter.hlsl",
    "RtWater.hlsl",
    "RtSkin.hlsl",
    "Clouds.hlsl",
    "CloudCommon.hlsli",
    "LightFog.hlsl",
    "DistantLod.hlsli",
    "Sss.hlsl",
    "LightingCommon.hlsli",
]


def run_req(args: list[str], timeout: int = 90) -> dict[str, Any]:
    cmd = [sys.executable, str(REQ_PY), "--as", "raw-studio", "--timeout", str(timeout), *args]
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout + 15)
    out = proc.stdout.strip()
    if out:
        try:
            return json.loads(out)
        except json.JSONDecodeError:
            return {"ok": proc.returncode == 0, "raw": out, "stderr": proc.stderr.strip()}
    return {"ok": proc.returncode == 0, "error": proc.stderr.strip() or "no output"}


HTML_PAGE = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>RAW DX12 Live Studio</title>
<style>
:root{--bg:#090c12;--panel:rgba(17,22,33,.92);--line:#253247;--ink:#eef1f6;--muted:#93a4bc;--acc:#44e0cf;--warm:#f4b350}
*{box-sizing:border-box}
body{margin:0;background:radial-gradient(ellipse at top,#11192a 0%,#090c12 72%);color:var(--ink);font:14px/1.45 "Segoe UI",system-ui,sans-serif;min-height:100vh}
header{display:flex;justify-content:space-between;align-items:center;padding:12px 20px;border-bottom:2px solid var(--acc);background:rgba(11,15,24,.96);backdrop-filter:blur(10px);position:sticky;top:0;z-index:10}
header h1{margin:0;font-size:16px;letter-spacing:.04em;color:var(--acc)}
.preset-bar{display:flex;gap:8px;flex-wrap:wrap;padding:10px 20px;background:rgba(13,18,28,.85);border-bottom:1px solid var(--line)}
.preset-bar button{font-size:12px;padding:5px 11px;border-radius:999px;background:#162032;border:1px solid #2b3c56}
.preset-bar button:hover{border-color:var(--warm);color:var(--warm)}
.grid{display:grid;grid-template-columns:360px 1fr;gap:14px;padding:14px;max-width:1600px;margin:0 auto}
@media(max-width:980px){.grid{grid-template-columns:1fr}}
.card{background:var(--panel);border:1px solid var(--line);border-radius:6px;padding:12px 14px;margin-bottom:14px}
.card h2{margin:0 0 10px;font-size:13px;text-transform:uppercase;letter-spacing:.06em;color:var(--acc)}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin-bottom:8px}
label{display:grid;gap:3px;font-size:12px;color:var(--muted);flex:1 1 130px}
input[type=range]{width:100%;accent-color:var(--warm)}
input[type=text],input[type=number],select{background:#0b0e14;color:var(--ink);border:1px solid var(--line);padding:5px 8px;border-radius:4px;font:inherit}
button{background:#1e2636;color:var(--ink);border:1px solid var(--line);padding:6px 10px;border-radius:4px;cursor:pointer;font-weight:600}
button:hover{border-color:var(--acc)}
button.primary{background:var(--acc);color:#091014;border-color:var(--acc)}
textarea{width:100%;height:420px;background:#0a0d12;color:#dbe4f0;border:1px solid var(--line);border-radius:4px;padding:10px;font:12.5px/1.45 Consolas,monospace;resize:vertical}
pre{margin:0;background:#0a0d12;border:1px solid var(--line);border-radius:4px;padding:8px;max-height:180px;overflow:auto;font:12px/1.35 Consolas,monospace;color:#b8c7dc}
</style>
</head>
<body>
<header>
  <h1>RAW DX12 / SM6.9 Live Remaster Studio</h1>
  <div class="row" style="margin:0">
    <button onclick="refreshState()">Refresh State</button>
    <button onclick="runMeasure()">Measure 120f GPU</button>
    <button class="primary" onclick="runCapture()">Capture Keyframe</button>
  </div>
</header>
<div class="preset-bar">
  <span style="font-size:12px;color:var(--muted);align-self:center;margin-right:4px">LOOK PRESETS:</span>
  <button onclick="iniSet('ToneCurve',4);iniSet('ToneX2',0)">Nordic Photoreal (AgX)</button>
  <button onclick="iniSet('ToneCurve',8);iniSet('ToneX2',2)">ACES 2.0 Kodak 2383</button>
  <button onclick="iniSet('ToneCurve',4);iniSet('ToneX2',4)">One Warm Light (500T)</button>
  <button onclick="iniSet('ToneCurve',4);iniSet('ToneX2',6)">Tarkovsky Storm Coast</button>
  <button onclick="iniSet('ToneCurve',4);iniSet('ToneX2',12)">Sony PVM CRT</button>
  <button onclick="iniSet('ToneCurve',4);iniSet('ToneX2',13)">1988 NTSC VHS</button>
</div>
<div class="grid">
  <div>
    <div class="card">
      <h2>Quality &amp; Modular Presets</h2>
      <div class="row">
        <label>Quality Tier
          <select id="q-tier" onchange="iniSet('Quality', this.value)">
            <option value="0">0 - Performance</option>
            <option value="1">1 - Balanced</option>
            <option value="2">2 - High</option>
            <option value="3">3 - Ultra</option>
            <option value="4" selected>4 - Overdrive (2-spp RT + 64-step Clouds)</option>
          </select>
        </label>
        <label>Debug View
          <select id="v-mode" onchange="iniSet('View', this.value)">
            <option value="0">0 - Final Lit</option>
            <option value="20">20 - 4-Quadrant Live Diagnostic</option>
            <option value="21">21 - Clipping Heatmap (Cyan/Magenta)</option>
            <option value="12">12 - Material Classes</option>
            <option value="1">1 - World Normals</option>
            <option value="2">2 - RT Sun Shadow</option>
            <option value="4">4 - Sky / Interior RTAO</option>
            <option value="5">5 - RT GI Bounce</option>
            <option value="6">6 - RT Reflections + Dynamic Cubemap</option>
            <option value="8">8 - Weather (Wet/Puddle/Snow)</option>
          </select>
        </label>
      </div>
      <div class="row">
        <label>Season (Dynamic Snow/Foliage)
          <select id="season-mode" onchange="iniSet('Season', this.value)">
            <option value="0" selected>0 - Auto (Weather Driven)</option>
            <option value="1">1 - Spring (Emerald &amp; Dew)</option>
            <option value="2">2 - Summer (Lush &amp; Dry)</option>
            <option value="3">3 - Autumn (Golden Amber &amp; Crimson)</option>
            <option value="4">4 - Winter (Deep Snowpack &amp; Footprints)</option>
          </select>
        </label>
        <label>Force Snow / Rain Accumulation
          <div class="row" style="margin:0">
            <button onclick="iniSet('WeatherSnow', 1)">Snow 100%</button>
            <button onclick="iniSet('WeatherRain', 1)">Rain 100%</button>
            <button onclick="iniSet('WeatherSnow', -1); iniSet('WeatherRain', -1)">Auto</button>
          </div>
        </label>
      </div>
      <div class="row">
        <label>NVIDIA Streamline (DlssMode)
          <select id="dlss-mode" onchange="iniSet('DlssMode', this.value)">
            <option value="0">0 - Off (FXAA+CAS)</option>
            <option value="1">1 - DLAA Transformer (Preset K)</option>
            <option value="2" selected>2 - DLSS Ray Reconstruction (Preset F)</option>
          </select>
        </label>
        <label>Color Space / Curve (ToneCurve)
          <select id="curve-mode" onchange="iniSet('ToneCurve', this.value)">
            <option value="0">0 - Game</option>
            <option value="1">1 - ACES Fitted</option>
            <option value="2">2 - ReinhardW</option>
            <option value="3">3 - RAW Hybrid Local</option>
            <option value="4" selected>4 - AgX Wide-Gamut (Oklab)</option>
            <option value="5">5 - Gran Turismo (Uchimura)</option>
            <option value="6">6 - Lottes</option>
            <option value="7">7 - Hable</option>
            <option value="8">8 - ACES 2.0 CAM (Perceptual)</option>
          </select>
        </label>
      </div>
      <div class="row">
        <label>Built-In Film Stock (ToneX2)
          <select id="film-stock" onchange="iniSet('ToneX2', this.value)">
            <option value="0">0 - Neutral</option>
            <option value="1" selected>1 - Nordic Alpine Film</option>
            <option value="2">2 - Kodak 2383 Print Film</option>
            <option value="3">3 - Cinema Contrast</option>
            <option value="4">4 - Kodak Vision3 500T (Night)</option>
            <option value="5">5 - Fuji Velvia 50 (Landscape)</option>
            <option value="6">6 - Tarkovsky Polaroid</option>
            <option value="7">7 - LomoChrome Purple</option>
            <option value="8">8 - Oil &amp; Impasto Paint (Kuwahara)</option>
            <option value="9">9 - Engraved Ink &amp; Cross-Hatch</option>
            <option value="10">10 - 16-Bit Pixel-Art + Bayer Dither</option>
            <option value="11">11 - Romantic Watercolor Wash</option>
            <option value="12">12 - Sony PVM Trinitron CRT</option>
            <option value="13">13 - 1988 NTSC VHS Tape</option>
          </select>
        </label>
        <label>Elder 64³ 3D PNG LUT (0=Off, 1..46)
          <input id="lut-idx" type="number" min="0" max="46" value="0" onchange="iniSet('LutIndex', this.value)" style="width:72px">
        </label>
        <label>Overlay Bitmask (0..4095)
          <input id="ov-mask" type="number" min="0" max="4095" value="263" onchange="iniSet('OverlayMask', this.value)" style="width:72px">
        </label>
        <label>Display Output (ToneView)
          <select id="hdr-mode" onchange="iniSet('ToneView', this.value)">
            <option value="0" selected>0 - SDR sRGB / Rec.709</option>
            <option value="3">3 - HDR10 PQ ST.2084 / Rec.2020</option>
          </select>
        </label>
      </div>
      <div class="row">
        <input id="preset-name" type="text" placeholder="preset-name (e.g. nordic-dawn)" style="flex:1">
        <button onclick="savePreset()">Save Preset</button>
        <button onclick="loadPreset()">Load Preset</button>
      </div>
      <div id="preset-lib" class="row" style="margin-top:6px"></div>
    </div>

    <div class="card">
      <h2>Tonemap, Exposure &amp; Atmosphere</h2>
      <div id="sliders"></div>
    </div>

    <div class="card">
      <h2>Weather, Time &amp; Biome Studio (KreatE-Style)</h2>
      <div class="row">
        <label>Game Hour <input id="hour" type="range" min="0" max="23.5" step="0.5" value="11" onchange="post('/api/time',{hour:this.value})"></label>
      </div>
      <div class="row">
        <button onclick="post('/api/weather',{id:'81a'})">Clear (81a)</button>
        <button onclick="post('/api/weather',{id:'10a7a7'})">Cloudy</button>
        <button onclick="post('/api/weather',{id:'c8220'})">Rain</button>
        <button onclick="post('/api/weather',{id:'10a23f'})">Snowstorm</button>
      </div>
      <div class="row">
        <button onclick="post('/api/move',{cell:'riverwood'})">Riverwood</button>
        <button onclick="post('/api/move',{cell:'whiterunbanneredmare'})">Bannered Mare</button>
        <button onclick="post('/api/move',{cell:'winterholdexterior01'})">Winterhold</button>
        <button onclick="post('/api/move',{cell:'dawnstarexterior01'})">Dawnstar</button>
        <button onclick="post('/api/move',{cell:'bleakfallsbarrow01'})">Bleak Falls</button>
      </div>
    </div>
  </div>

  <div>
    <div class="card">
      <h2>Live HLSL SM6.6–6.9 Shader Editor</h2>
      <div class="row">
        <select id="shader-sel" onchange="loadShader()"></select>
        <button class="primary" onclick="saveAndReloadShader()">Save &amp; Hot-Reload Live</button>
      </div>
      <textarea id="shader-src" spellcheck="false"></textarea>
    </div>
    <div class="card">
      <h2>Live Telemetry &amp; Console</h2>
      <pre id="out">Connecting to RAW live session...</pre>
    </div>
  </div>
</div>
<script>
const PARAMS = [
  ['Tonemap', 0, 1, 1, 1],
  ['ExposureKey', 0.15, 0.85, 0.02, 0.41],
  ['ToneX1', 1.5, 8.0, 0.1, 4.5],
  ['ToneX3', 0.8, 2.0, 0.05, 1.35],
  ['ToneX4', 0.3, 1.2, 0.05, 0.55],
  ['RtSkyStrength', 0.0, 1.5, 0.05, 1.0],
  ['RtBounce', 0.0, 2.5, 0.05, 1.2],
  ['RtBounceInterior', 0.0, 2.5, 0.05, 1.15],
  ['CloudCover', 0.0, 1.0, 0.02, 0.45],
  ['WeatherDrive', 0, 1, 1, 1],
];
const currentParams = {};
const slHost = document.getElementById('sliders');
for (const [k, mn, mx, st, def] of PARAMS) {
  currentParams[k] = def;
  const wrap = document.createElement('label');
  wrap.innerHTML = `${k} <strong id="v-${k}">${def}</strong><input id="s-${k}" type="range" min="${mn}" max="${mx}" step="${st}" value="${def}">`;
  wrap.querySelector('input').addEventListener('change', (e) => {
    currentParams[k] = Number(e.target.value);
    document.getElementById('v-' + k).textContent = e.target.value;
    iniSet(k, e.target.value);
  });
  slHost.appendChild(wrap);
}
const log = (o) => { document.getElementById('out').textContent = typeof o === 'string' ? o : JSON.stringify(o, null, 2); };
async function post(url, body) {
  const r = await fetch(url, { method: 'POST', headers: {'Content-Type':'application/json'}, body: JSON.stringify(body) });
  const j = await r.json(); log(j); return j;
}
const iniSet = (k, v) => post('/api/ini', { [k]: v });
const refreshState = async () => log(await (await fetch('/api/state')).json());
const runMeasure = () => post('/api/measure', { frames: 120 });
const runCapture = () => post('/api/capture', { name: 'studio_live' });
async function loadShadersList() {
  const list = await (await fetch('/api/shaders')).json();
  const sel = document.getElementById('shader-sel');
  sel.innerHTML = list.map(n => `<option value="${n}">${n}</option>`).join('');
  loadShader();
}
async function loadShader() {
  const name = document.getElementById('shader-sel').value;
  const j = await (await fetch('/api/shader?file=' + encodeURIComponent(name))).json();
  document.getElementById('shader-src').value = j.source || '';
}
const saveAndReloadShader = () => post('/api/shader', { file: document.getElementById('shader-sel').value, source: document.getElementById('shader-src').value });
const savePreset = () => post('/api/preset/save', { name: document.getElementById('preset-name').value || 'custom', params: currentParams });
async function loadPreset() {
  const j = await post('/api/preset/load', { name: document.getElementById('preset-name').value || 'custom' });
  if (j.params) for (const [k, v] of Object.entries(j.params)) {
    const el = document.getElementById('s-' + k); if (el) { el.value = v; document.getElementById('v-' + k).textContent = v; currentParams[k] = v; }
  }
}
async function loadPresetLibrary() {
  const list = await (await fetch('/api/presets')).json();
  const host = document.getElementById('preset-lib');
  host.innerHTML = '';
  for (const item of list) {
    const b = document.createElement('button');
    b.textContent = `${item.category}: ${item.name}`;
    b.onclick = async () => {
      await post('/api/ini', item.params);
      for (const [k, v] of Object.entries(item.params)) {
        const el = document.getElementById('s-' + k);
        if (el) { el.value = v; document.getElementById('v-' + k).textContent = v; currentParams[k] = v; }
      }
    };
    host.appendChild(b);
  }
}
loadShadersList(); loadPresetLibrary(); refreshState();
</script>
</body>
</html>"""


class StudioHandler(BaseHTTPRequestHandler):
    def _send_json(self, payload: Any, code: int = 200) -> None:
        raw = json.dumps(payload, indent=2).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def _read_json(self) -> dict[str, Any]:
        n = int(self.headers.get("Content-Length", "0"))
        return json.loads(self.rfile.read(n).decode("utf-8")) if n > 0 else {}

    def do_GET(self) -> None:
        u = urlparse(self.path)
        if u.path == "/":
            raw = HTML_PAGE.encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(raw)))
            self.end_headers()
            self.wfile.write(raw)
        elif u.path == "/api/state":
            self._send_json(run_req(["state"], timeout=15))
        elif u.path == "/api/presets":
            items = []
            if PRESET_DIR.is_dir():
                for p in sorted(PRESET_DIR.rglob("*.json")):
                    try:
                        d = json.loads(p.read_text(encoding="utf-8"))
                        items.append({"id": p.relative_to(PRESET_DIR).with_suffix("").as_posix(), "name": d.get("name", p.stem), "category": d.get("category", "custom"), "params": d.get("params", d)})
                    except Exception:
                        pass
            self._send_json(items)
        elif u.path == "/api/shaders":
            self._send_json([s for s in EDITABLE_SHADERS if (SHADER_DIR / s).is_file()])
        elif u.path == "/api/shader":
            name = parse_qs(u.query).get("file", [""])[0]
            if name not in EDITABLE_SHADERS or not (SHADER_DIR / name).is_file():
                self._send_json({"ok": False, "error": "unknown shader"}, 404)
                return
            self._send_json({"ok": True, "file": name, "source": (SHADER_DIR / name).read_text(encoding="utf-8")})
        else:
            self._send_json({"ok": False, "error": "not found"}, 404)

    def do_POST(self) -> None:
        u = urlparse(self.path)
        body = self._read_json()
        if u.path == "/api/ini":
            pairs = [f"{k}={v}" for k, v in body.items()]
            self._send_json(run_req(["reload_ini", *pairs], timeout=30))
        elif u.path == "/api/time":
            self._send_json(run_req(["set_time", f"hour={body.get('hour', 12)}"], timeout=30))
        elif u.path == "/api/weather":
            self._send_json(run_req(["set_weather", f"id={body.get('id', '81a')}", "force=true"], timeout=30))
        elif u.path == "/api/move":
            self._send_json(run_req(["move", f"cell={body.get('cell', 'riverwood')}"], timeout=60))
        elif u.path == "/api/measure":
            self._send_json(run_req(["measure", f"frames={body.get('frames', 120)}", "name=studio_perf"], timeout=60))
        elif u.path == "/api/capture":
            self._send_json(run_req(["capture", "kind=keyframe", f"name={body.get('name', 'studio')}", "steps=game,improved", "hold=20"], timeout=90))
        elif u.path == "/api/shader":
            name = body.get("file", "")
            if name not in EDITABLE_SHADERS:
                self._send_json({"ok": False, "error": "unknown shader"}, 400)
                return
            path = SHADER_DIR / name
            path.write_text(str(body.get("source", "")), encoding="utf-8", newline="\n")
            self._send_json(run_req(["reload_shader", f"file={name}", f"source={path.as_posix()}"], timeout=60))
        elif u.path == "/api/preset/save":
            PRESET_DIR.mkdir(parents=True, exist_ok=True)
            name = "".join(c for c in str(body.get("name", "custom")) if c.isalnum() or c in "-_") or "custom"
            target = PRESET_DIR / f"{name}.json"
            target.write_text(json.dumps(body.get("params", {}), indent=2) + "\n", encoding="utf-8")
            self._send_json({"ok": True, "saved": str(target)})
        elif u.path == "/api/preset/load":
            name = "".join(c for c in str(body.get("name", "custom")) if c.isalnum() or c in "-_") or "custom"
            target = PRESET_DIR / f"{name}.json"
            if not target.is_file():
                self._send_json({"ok": False, "error": f"preset {name} not found"}, 404)
                return
            params = json.loads(target.read_text(encoding="utf-8"))
            pairs = [f"{k}={v}" for k, v in params.items()]
            res = run_req(["reload_ini", *pairs], timeout=30)
            self._send_json({"ok": res.get("ok", False), "params": params, "result": res})
        else:
            self._send_json({"ok": False, "error": "not found"}, 404)

    def log_message(self, format: str, *args: Any) -> None:
        return


def main() -> None:
    p = argparse.ArgumentParser(description="RAW DX12 Live Studio UI")
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=8844)
    args = p.parse_args()
    srv = ThreadingHTTPServer((args.host, args.port), StudioHandler)
    print(f"RAW Live Studio listening on http://{args.host}:{args.port}")
    srv.serve_forever()


if __name__ == "__main__":
    main()
