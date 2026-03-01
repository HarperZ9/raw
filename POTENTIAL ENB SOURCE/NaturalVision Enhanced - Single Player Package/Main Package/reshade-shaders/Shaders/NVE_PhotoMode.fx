#include "ReShade.fxh"

//#define DEBUG

#ifndef OVERLAYS_PATH
#define OVERLAYS_PATH "NVE_PhotoMode/Overlays/"
#endif
#ifndef FRAMES_PATH
#define FRAMES_PATH "NVE_PhotoMode/Frames/"
#endif
#ifndef STICKERS_PATH
#define STICKERS_PATH "NVE_PhotoMode/Stickers/"
#endif

#ifndef OVERLAY_1
#define OVERLAY_1 "PostCard01.png"
#endif
#ifndef OVERLAY_2
#define OVERLAY_2 "PostCard02.png"
#endif
#ifndef OVERLAY_3
#define OVERLAY_3 "PostCard03.png"
#endif
#ifndef OVERLAY_4
#define OVERLAY_4 "PostCard04.png"
#endif
#ifndef OVERLAY_5
#define OVERLAY_5 "PrincessRobotBubblegum.png"
#endif
#ifndef OVERLAY_6
#define OVERLAY_6 "SpaceRangers.png"
#endif
#ifndef OVERLAY_7
#define OVERLAY_7 "GroveStreet.png"
#endif
#ifndef OVERLAY_8
#define OVERLAY_8 "Chumash.png"
#endif
#ifndef OVERLAY_9
#define OVERLAY_9 "Photos.png"
#endif
#ifndef OVERLAY_10
#define OVERLAY_10 "LittleSeoul.png"
#endif
#ifndef OVERLAY_11
#define OVERLAY_11 "Paleto.png"
#endif
#ifndef OVERLAY_12
#define OVERLAY_12 "Prison.png"
#endif
#ifndef OVERLAY_13
#define OVERLAY_13 "Sandy.png"
#endif
#ifndef OVERLAY_14
#define OVERLAY_14 "Vinewood.png"
#endif

#ifndef STICKER_1
#define STICKER_1 "Enhanced.png"
#endif
#ifndef STICKER_2
#define STICKER_2 "LosSantos.png"
#endif
#ifndef STICKER_3
#define STICKER_3 "NVE.png"
#endif
#ifndef STICKER_4
#define STICKER_4 "PrincessRobotBubblegumSticker.png"
#endif
#ifndef STICKER_5
#define STICKER_5 "SpaceRangerSticker.png"
#endif


#ifndef FRAME_1
#define FRAME_1 "AmmunationTV.png"
#endif
#ifndef FRAME_2
#define FRAME_2 "Laptop.png"
#endif
#ifndef FRAME_3
#define FRAME_3 "billboard2.png"
#endif
#ifndef FRAME_4
#define FRAME_4 "billboard5.png"
#endif
#ifndef FRAME_5
#define FRAME_5 "billboard9.png"
#endif


uniform int OverlaySelect <
    ui_type = "combo";
    ui_label = "Overlay";
    ui_items = "None\0Overlay 1\0Overlay 2\0Overlay 3\0Overlay 4\0Overlay 5\0Overlay 6\0Overlay 7\0Overlay 8\0Overlay 9\0Overlay 10\0Overlay 11\0Overlay 12\0Overlay 13\0Overlay 14\0";
> = 0;

uniform int StickerSelect <
    ui_type = "combo";
    ui_label = "Sticker";
    ui_items = "None\0Sticker 1\0Sticker 2\0Sticker 3\0Sticker 4\0Sticker 5\0";
> = 0;

uniform int FrameSelect <
    ui_type = "combo";
    ui_label = "Frame";
    ui_items = "None\0Frame 1\0Frame 2\0Frame 3\0Frame 4\0Frame 5\0";
> = 0;

uniform float fOpacity <
    ui_label = "Opacity";
    ui_type = "drag";
    ui_min = 0.0;
    ui_max = 1.0;
    ui_step = 0.001;
> = 1.0;

uniform float sScale <
    ui_label = "Scale (Stickers)";
    ui_type = "drag";
    ui_min = 0.1;
    ui_max = 5.0;
    ui_step = 0.01;
> = 0.2;

uniform float2 f2Position <
    ui_label = "Position (Stickers)";
    ui_type = "drag";
    ui_min = 0.0;
    ui_max = 1.5;
    ui_step = 0.001;
> = float2(1.000, 1.000);

#ifdef DEBUG
uniform float2 f2FrameTopLeft <
    ui_label = "Frame Top-Left Corner";
    ui_type = "drag";
    ui_min = -2.0;
    ui_max = 2.0;
    ui_step = 0.001;
> = float2(0.2, 0.2);

uniform float2 f2FrameTopRight <
    ui_label = "Frame Top-Right Corner";
    ui_type = "drag";
    ui_min = -2.0;
    ui_max = 2.0;
    ui_step = 0.001;
> = float2(0.8, 0.2);

uniform float2 f2FrameBottomLeft <
    ui_label = "Frame Bottom-Left Corner";
    ui_type = "drag";
    ui_min = -2.0;
    ui_max = 2.0;
    ui_step = 0.001;
> = float2(0.2, 0.8);

uniform float2 f2FrameBottomRight <
    ui_label = "Frame Bottom-Right Corner";
    ui_type = "drag";
    ui_min = -2.0;
    ui_max = 2.0;
    ui_step = 0.001;
> = float2(0.8, 0.8);
#endif

uniform float   VHS_Intensity
<
    ui_label="VHS Filter : Intensity";
    ui_type		=	"slider";
    ui_min=0.0;
    ui_max=0.005;
> = 0.001;

uniform float   VHS_Speed
<
    ui_label="VHS Filter : Speed";
    ui_type		=	"slider";
    ui_min=0.0;
    ui_max=0.02;
> = 0.005;

uniform float   VHS_LinesIntensity
<
    ui_label="VHS Filter : Lines Intensity";
    ui_type		=	"slider";
    ui_min=0.0;
    ui_max=0.1;
> = 0.01;


uniform float timer < source = "timer"; >;

texture2D tOverlay1 < source = OVERLAYS_PATH OVERLAY_1; > { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; };
texture2D tOverlay2 < source = OVERLAYS_PATH OVERLAY_2; > { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; };
texture2D tOverlay3 < source = OVERLAYS_PATH OVERLAY_3; > { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; };
texture2D tOverlay4 < source = OVERLAYS_PATH OVERLAY_4; > { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; };
texture2D tOverlay5 < source = OVERLAYS_PATH OVERLAY_5; > { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; };
texture2D tOverlay6 < source = OVERLAYS_PATH OVERLAY_6; > { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; };
texture2D tOverlay7 < source = OVERLAYS_PATH OVERLAY_7; > { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; };
texture2D tOverlay8 < source = OVERLAYS_PATH OVERLAY_8; > { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; };
texture2D tOverlay9 < source = OVERLAYS_PATH OVERLAY_9; > { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; };
texture2D tOverlay10 < source = OVERLAYS_PATH OVERLAY_10; > { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; };
texture2D tOverlay11 < source = OVERLAYS_PATH OVERLAY_11; > { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; };
texture2D tOverlay12 < source = OVERLAYS_PATH OVERLAY_12; > { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; };
texture2D tOverlay13 < source = OVERLAYS_PATH OVERLAY_13; > { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; };
texture2D tOverlay14 < source = OVERLAYS_PATH OVERLAY_14; > { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; };

texture2D tSticker1 < source = STICKERS_PATH STICKER_1; > { Width = 1200; Height = 650; };
texture2D tSticker2 < source = STICKERS_PATH STICKER_2; > { Width = 1027; Height = 582; };
texture2D tSticker3 < source = STICKERS_PATH STICKER_3; > { Width = 512; Height = 512; };
texture2D tSticker4 < source = STICKERS_PATH STICKER_4; > { Width = 2064; Height = 3560; };
texture2D tSticker5 < source = STICKERS_PATH STICKER_5; > { Width = 437; Height = 447; };

texture2D tFrame1 < source = FRAMES_PATH FRAME_1; > { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; };
texture2D tFrame2 < source = FRAMES_PATH FRAME_2; > { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; };
texture2D tFrame3 < source = FRAMES_PATH FRAME_3; > { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; };
texture2D tFrame4 < source = FRAMES_PATH FRAME_4; > { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; };
texture2D tFrame5 < source = FRAMES_PATH FRAME_5; > { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; };

texture2D tFrameUV5 < source = FRAMES_PATH "AmmunationTV_UV.png"; > { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = RGBA8; };

sampler sOverlay1 { Texture = tOverlay1; MinFilter = LINEAR; MagFilter = LINEAR; AddressU = BORDER; AddressV = BORDER; };
sampler sOverlay2 { Texture = tOverlay2; MinFilter = LINEAR; MagFilter = LINEAR; AddressU = BORDER; AddressV = BORDER; };
sampler sOverlay3 { Texture = tOverlay3; MinFilter = LINEAR; MagFilter = LINEAR; AddressU = BORDER; AddressV = BORDER; };
sampler sOverlay4 { Texture = tOverlay4; MinFilter = LINEAR; MagFilter = LINEAR; AddressU = BORDER; AddressV = BORDER; };
sampler sOverlay5 { Texture = tOverlay5; MinFilter = LINEAR; MagFilter = LINEAR; AddressU = BORDER; AddressV = BORDER; };
sampler sOverlay6 { Texture = tOverlay6; MinFilter = LINEAR; MagFilter = LINEAR; AddressU = BORDER; AddressV = BORDER; };
sampler sOverlay7 { Texture = tOverlay7; MinFilter = LINEAR; MagFilter = LINEAR; AddressU = BORDER; AddressV = BORDER; };
sampler sOverlay8 { Texture = tOverlay8; MinFilter = LINEAR; MagFilter = LINEAR; AddressU = BORDER; AddressV = BORDER; };
sampler sOverlay9 { Texture = tOverlay9; MinFilter = LINEAR; MagFilter = LINEAR; AddressU = BORDER; AddressV = BORDER; };
sampler sOverlay10 { Texture = tOverlay10; MinFilter = LINEAR; MagFilter = LINEAR; AddressU = BORDER; AddressV = BORDER; };
sampler sOverlay11 { Texture = tOverlay11; MinFilter = LINEAR; MagFilter = LINEAR; AddressU = BORDER; AddressV = BORDER; };
sampler sOverlay12 { Texture = tOverlay12; MinFilter = LINEAR; MagFilter = LINEAR; AddressU = BORDER; AddressV = BORDER; };
sampler sOverlay13 { Texture = tOverlay13; MinFilter = LINEAR; MagFilter = LINEAR; AddressU = BORDER; AddressV = BORDER; };
sampler sOverlay14 { Texture = tOverlay14; MinFilter = LINEAR; MagFilter = LINEAR; AddressU = BORDER; AddressV = BORDER; };

sampler sSticker1 { Texture = tSticker1; MinFilter = LINEAR; MagFilter = LINEAR; AddressU = BORDER; AddressV = BORDER; };
sampler sSticker2 { Texture = tSticker2; MinFilter = LINEAR; MagFilter = LINEAR; AddressU = BORDER; AddressV = BORDER; };
sampler sSticker3 { Texture = tSticker3; MinFilter = LINEAR; MagFilter = LINEAR; AddressU = BORDER; AddressV = BORDER; };
sampler sSticker4 { Texture = tSticker4; MinFilter = LINEAR; MagFilter = LINEAR; AddressU = BORDER; AddressV = BORDER; };
sampler sSticker5 { Texture = tSticker5; MinFilter = LINEAR; MagFilter = LINEAR; AddressU = BORDER; AddressV = BORDER; };

sampler sFrame1 { Texture = tFrame1; MinFilter = LINEAR; MagFilter = LINEAR; AddressU = BORDER; AddressV = BORDER; };
sampler sFrame2 { Texture = tFrame2; MinFilter = LINEAR; MagFilter = LINEAR; AddressU = BORDER; AddressV = BORDER; };
sampler sFrame3 { Texture = tFrame3; MinFilter = LINEAR; MagFilter = LINEAR; AddressU = BORDER; AddressV = BORDER; };
sampler sFrame4 { Texture = tFrame4; MinFilter = LINEAR; MagFilter = LINEAR; AddressU = BORDER; AddressV = BORDER; };
sampler sFrame5 { Texture = tFrame5; MinFilter = LINEAR; MagFilter = LINEAR; AddressU = BORDER; AddressV = BORDER; };

sampler sFrameUV5 { Texture = tFrameUV5; MinFilter = LINEAR; MagFilter = LINEAR; AddressU = BORDER; AddressV = BORDER; };


float4 gaussianBlur(sampler2D tex, float2 uv) {
    float kernel[5] = {0.0022, 0.0215, 0.1012, 0.0215, 0.0022};
    float4 color = float4(0.0, 0.0, 0.0, 0.0);

    // Loop through each pixel in a 5x5 area
    for (int i = -2; i <= 2; i++) {
        for (int j = -2; j <= 2; j++) {
            float2 offset = float2(i, j) / float2(BUFFER_WIDTH,BUFFER_HEIGHT);
            
            color += tex2D(tex, uv + offset);
        }
    }

    return color/25;
}

float2 GetScaledUv(float2 texel, float2 originalSize, float2 targetSize, float2 position, float scale)
{
    // Calculate aspect ratios
    float texAspect = originalSize.x / originalSize.y;
    float screenAspect = targetSize.x / targetSize.y;
    
    // Calculate scaling factors
    float widthScale = min(1.0, screenAspect / texAspect);
    float heightScale = min(1.0, texAspect / screenAspect);
    
    // Combined scale to maintain aspect ratio
    float2 aspectScale = float2(widthScale, heightScale) * rcp(scale);
    
    // Center the texture
    float2 uvOffset = (1.0 - aspectScale) * position;
    
    // Apply scaling
    return uvOffset + texel * aspectScale;
}
void VS_Overlay(in uint id : SV_VertexID, out float4 position : SV_Position, out float2 texcoord : TEXCOORD0) {
    float2 vertices[4] = {
        float2(-1.0,  1.0),
        float2( 1.0,  1.0),
        float2(-1.0, -1.0),
        float2( 1.0, -1.0)
    };
    float2 uvs[4] = {
        float2(0.0, 0.0),
        float2(1.0, 0.0),
        float2(0.0, 1.0),
        float2(1.0, 1.0)
    };
    position = float4(vertices[id], 0.0, 1.0);
    texcoord = uvs[id];
}

float2 bilinearUV(float2 t, float2 uv[4])
{
    float2 top = lerp(uv[0], uv[1], t.x);
    float2 bottom = lerp(uv[2], uv[3], t.x);
    return lerp(top, bottom, t.y);
}

float hash(float n) {
    return frac(sin(n/1873.1873) * 253.5453);
}

float4 PS_Overlay(float4 pos : SV_POSITION, float2 texcoord : TEXCOORD0) : SV_TARGET {
    
    float4 frame = 0;
    switch (FrameSelect) {
        case 1: frame = tex2D(sFrame1, texcoord); break;
        case 2: frame = tex2D(sFrame2, texcoord); break;
        case 3: frame = tex2D(sFrame3, texcoord); break;
        case 4: frame = tex2D(sFrame4, texcoord); break;
        case 5: frame = tex2D(sFrame5, texcoord); break;
    }

    float2 uv_game = texcoord;

    #ifdef DEBUG
        float2 texcoord_frame[4];
        texcoord_frame[0] = float2(f2FrameTopLeft.x, f2FrameTopLeft.y);
        texcoord_frame[1] = float2(f2FrameTopRight.x, f2FrameTopRight.y);
        texcoord_frame[2] = float2(f2FrameBottomLeft.x, f2FrameBottomLeft.y);
        texcoord_frame[3] = float2(f2FrameBottomRight.x, f2FrameBottomRight.y);
    #else
        float2 texcoord_frame[4];
    #endif

    switch (FrameSelect) {
        case 1: 
            uv_game = gaussianBlur(sFrameUV5,texcoord).yx;
            uv_game.x += VHS_Intensity*hash(sin(uv_game.y+timer*VHS_Speed)*500);
            break;
        case 2:
            texcoord_frame[0] = float2(-0.493, -0.381);
            texcoord_frame[1] = float2(1.433, -0.369);
            texcoord_frame[2] = float2(-0.447, 1.429);
            texcoord_frame[3] = float2(1.395, 1.425);
            uv_game = bilinearUV(texcoord,texcoord_frame);
            break;
        case 3: 
            texcoord_frame[0] = float2(-0.053, -0.296);
            texcoord_frame[1] = float2(1.231, -0.117);
            texcoord_frame[2] = float2(-0.025, 1.324);
            texcoord_frame[3] = float2(1.206, 1.179);
            uv_game = bilinearUV(texcoord,texcoord_frame);
            break;
        case 4: 
            texcoord_frame[0] = float2(-0.244, -0.260);
            texcoord_frame[1] = float2(1.026, -0.277);
            texcoord_frame[2] = float2(-0.246, 1.328);
            texcoord_frame[3] = float2(1.031, 1.343);
            uv_game = bilinearUV(texcoord,texcoord_frame);
            break;          
        case 5: 
            texcoord_frame[0] = float2(-0.405, -0.280);
            texcoord_frame[1] = float2(1.057, -0.107);
            texcoord_frame[2] = float2(-0.398, 1.920);
            texcoord_frame[3] = float2(1.049, 1.777);
            uv_game = bilinearUV(texcoord,texcoord_frame);
            break;        
    }
    float4 color = tex2D(ReShade::BackBuffer, uv_game);
    if(FrameSelect == 1){
        color.x = tex2Dlod(ReShade::BackBuffer,float4(uv_game-float2(0.001,0.001)*0.65f,0.0,0.0)).x;
	    color.y = tex2Dlod(ReShade::BackBuffer,float4(uv_game-float2(-0.001,-0.001)*0.65f,0.0,0.0)).y;
	    color.z = tex2Dlod(ReShade::BackBuffer,float4(uv_game-float2(0.0005,-0.0005)*0.65f,0.0,0.0)).z;
        color.xyz = lerp(color.xyz,uv_game.y-hash(sin(uv_game.y+timer*VHS_Speed)*200.0f),VHS_LinesIntensity);
    }
    color = lerp(color, frame, frame.a * fOpacity);

    // Overlay
    float4 overlay = 0;
    switch (OverlaySelect) {
        case 1: overlay = tex2D(sOverlay1, texcoord); break;
        case 2: overlay = tex2D(sOverlay2, texcoord); break;
        case 3: overlay = tex2D(sOverlay3, texcoord); break;
        case 4: overlay = tex2D(sOverlay4, texcoord); break;
        case 5: overlay = tex2D(sOverlay5, texcoord); break;
        case 6: overlay = tex2D(sOverlay6, texcoord); break;
        case 7: overlay = tex2D(sOverlay7, texcoord); break;
        case 8: overlay = tex2D(sOverlay8, texcoord); break;
        case 9: overlay = tex2D(sOverlay9, texcoord); break;
        case 10: overlay = tex2D(sOverlay10, texcoord); break;
        case 11: overlay = tex2D(sOverlay11, texcoord); break;
        case 12: overlay = tex2D(sOverlay12, texcoord); break;
        case 13: overlay = tex2D(sOverlay13, texcoord); break;
        case 14: overlay = tex2D(sOverlay14, texcoord); break;
    }
    color = lerp(color, overlay, overlay.a * fOpacity);

    // Sticker
    float2 screenSize = float2(BUFFER_WIDTH, BUFFER_HEIGHT);

    float2 uv_sticker;

    int2 stickerSize;
    switch (StickerSelect) {
        case 1: stickerSize = tex2Dsize(sSticker1); break;
        case 2: stickerSize = tex2Dsize(sSticker2); break;
        case 3: stickerSize = tex2Dsize(sSticker3); break;
        case 4: stickerSize = tex2Dsize(sSticker4); break;
        case 5: stickerSize = tex2Dsize(sSticker5); break;
    }
    float2 stickerSizeF = float2(stickerSize);
    uv_sticker = GetScaledUv(texcoord,stickerSize,screenSize,f2Position,sScale);

    float4 sticker = 0;
    switch (StickerSelect) {
        case 1: sticker = tex2D(sSticker1, uv_sticker); break;
        case 2: sticker = tex2D(sSticker2, uv_sticker); break;
        case 3: sticker = tex2D(sSticker3, uv_sticker); break;
        case 4: sticker = tex2D(sSticker4, uv_sticker); break;
        case 5: sticker = tex2D(sSticker5, uv_sticker); break;
    }
    color = lerp(color, sticker, sticker.a * fOpacity);

    /*
    if(uv_game.x < 0.0 || uv_game.x > 1.0 || uv_game.y < 0.0 || uv_game.y > 1.0) {
        return float4(1.0, 0.0, 0.0, 1.0);
    }
    */
    return color;
}

technique OverlayTech <
    ui_label = "NaturalVision Enhanced: Photo Mode";
> {
    pass OverlayPass {
        VertexShader = VS_Overlay;
        PrimitiveTopology = TRIANGLESTRIP;
        VertexCount = 4;
        PixelShader = PS_Overlay;
    }
}
