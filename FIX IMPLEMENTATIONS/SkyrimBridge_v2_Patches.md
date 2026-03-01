# SkyrimBridge v2.0 — Shader Patches

Exact code changes for each shader file. Apply these after replacing
`Helper/SkyrimBridge.fxh` with the v2 version.

---

## 1. enbeffectprepass.fx

### Patch A: SB_Retain() in PS_EffectsComposite

**Location:** End of `PS_EffectsComposite()` function (~line 2970)

```hlsl
// BEFORE:
    color = max(color, 0.0);

    return float4(color, 1.0);
}

// AFTER:
    color = max(color, 0.0);

    // SkyrimBridge: retain all 102 params in constant buffer
    color += SB_Retain(IN.texcoord);

    return float4(color, 1.0);
}
```

### Patch B (Optional): Monitor Panel

**Location:** After the SkyrimBridge.fxh include (~line 414)

```hlsl
// BEFORE:
#include "Helper/SkyrimBridge.fxh"

// AFTER:
#define SB_ENABLE_MONITOR 1
#include "Helper/SkyrimBridge.fxh"
#include "UI/enbUI_SkyrimBridge.fxh"
```

Then in PS_EffectsComposite, after the SB_Retain call:

```hlsl
    color += SB_Retain(IN.texcoord);

    // Optional: update ENB GUI monitor display
    if (_SBMon_Enable) SB_UpdateMonitor();

    return float4(color, 1.0);
```

---

## 2. enbeffect.fx

### Patch A: SB_Retain() in PS_Draw

**Location:** End of the main composite pixel shader

```hlsl
// BEFORE:
    color = ApplyDither(color, uv);
    return float4(color, 1.0);
}

// AFTER:
    color = ApplyDither(color, uv);
    color += SB_Retain(uv);
    return float4(color, 1.0);
}
```

---

## 3. enblens.fx

### Patch A: SB_Retain() in final lens pass

**Location:** End of the last lens technique pixel shader

```hlsl
// BEFORE:
    return float4(color, 1.0);
}

// AFTER:
    color += SB_Retain(uv);
    return float4(color, 1.0);
}
```

---

## 4. enbdepthoffield.fx

### Patch A: SB_Retain() in final DOF composite

**Location:** End of PS_DOFComposite or the final pass

```hlsl
// BEFORE:
    return float4(finalColor, 1.0);
}

// AFTER:
    finalColor += SB_Retain(uv);
    return float4(finalColor, 1.0);
}
```

---

## 5. enbsunsprite.fx

### Patch A: SB_Retain() in PS_SunFlare

**Location:** End of the sun flare pixel shader

```hlsl
// BEFORE:
    return float4(flare, alpha);
}

// AFTER:
    flare += SB_Retain(uv);
    return float4(flare, alpha);
}
```

---

## 6. enbbloom.fx

### Patch A: SB_Retain() in PS_BloomComposite

```hlsl
// BEFORE:
    return float4(bloom, 1.0);
}

// AFTER:
    bloom += SB_Retain(uv);
    return float4(bloom, 1.0);
}
```

---

## 7. enbadaptation.fx

### Patch A: SB_Retain() in PS_Adaptation

```hlsl
// BEFORE:
    return float4(adapted, 1.0);
}

// AFTER:
    adapted += SB_Retain(uv);
    return float4(adapted, 1.0);
}
```

---

## Verification Checklist

After applying patches, verify data flow:

1. **Launch Skyrim with ENB + SkyrimBridge**
2. **Check SkyrimBridge.log** for the first-frame diagnostic report:
   - `102 OK, 0 FAILED` per shader = success
   - Any FAILED params = KeepAlive not working for that shader
3. **Open ENB editor (Shift+Enter)**
   - Navigate to the shader with the monitor panel
   - Enable "SkyrimBridge Monitor"
   - Values should update live (Game Hour, Health %, FOV, etc.)
   - If all values = 0.0, data isn't flowing → check log
4. **Open ImGui debug (INSERT key)**
   - Compare ImGui values with ENB monitor values
   - They should match within floating-point precision

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| All params FAIL in log | SkyrimBridge.fxh not included | Add #include |
| All params FAIL in log | Shader filename wrong | Check case: "enbeffect.fx" not "ENBEffect.fx" |
| Most params FAIL | SB_Retain() not called | Add to a pixel shader |
| Monitor shows 0 | SB_UpdateMonitor() not called | Add to pixel shader |
| Monitor shows stale values | Wrong technique | Add to a technique that runs every frame |
| Log says "ENBSetParameter null" | ENB SDK not resolved | Check Init timing (kPostLoad) |
| Data flows but values wrong | Struct layout mismatch | Check BridgeData offsets vs HLSL names |
