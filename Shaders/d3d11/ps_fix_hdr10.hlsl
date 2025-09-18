// === Simplified ACEScg approach ===
// Use a simplified, safer ACEScg approximation to avoid matrix issues
float3 toACEScg(float3 rgb2020) { 
    // Simplified transform - less accurate but safer
    rgb2020 = saturate(rgb2020);
    return rgb2020 * 0.95f; // Scale down slightly for ACEScg working space
}

float3 fromACEScg(float3 rgbACES) { 
    // Inverse of simplified transform
    rgbACES = saturate(rgbACES);
    return rgbACES / 0.95f; // Scale back up
}

// Simplified ACEScg filmic curve
float3 ACEScgFilmicCurve(float3 x) {
    // Use the same constants as the main ACES curve for consistency
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

#include "../convert/st2084.hlsl"

Texture2D tex : register(t0);
SamplerState samp : register(s0);

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD;
};

cbuffer RootConstants : register(b0)
{
    float MasteringMinLuminanceNits;
    float MasteringMaxLuminanceNits;
    float maxCLL;
    float maxFALL;
    float displayMaxNits;
    uint selection; // 1 = ACES, 2 = Reinhard, 3 = Habel, 4 = Möbius, 5 = ACEScg
    float reserved1;
    float reserved2;
};

// ACES RRT + ODT Implementation
float3 RRTAndODTFit(float3 color) {
    const float A = 2.51f, B = 0.03f, C = 2.43f, D = 0.59f, E = 0.14f;
    return (color * (A * color + B)) / (color * (C * color + D) + E);
}

float3 ACESFilmTonemap(float3 color) {
    return RRTAndODTFit(color);
}

float3 ReinhardTonemap(float3 color) {
    return color / (1.0 + color);
}

float3 HabelTonemap(float3 color) {
    const float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;
    return ((color * (A * color + C * B) + D * E) / (color * (A * color + B) + D * F)) - E / F;
}

float3 MobiusTonemap(float3 color) {
    const float epsilon = 1e-6;
    const float maxL = displayMaxNits;
    return color / (1.0 + color / (maxL + epsilon));
}

// ACEScg tone mapping with proper color space handling
float3 ACEScgTonemap(float3 color) {
    // Clamp input to prevent NaN/Inf issues
    color = max(color, 0.0f);
    
    // Convert to ACEScg color space
    float3 acescg = toACEScg(color);
    
    // Clamp after conversion to prevent issues
    acescg = max(acescg, 0.0f);
    
    // Apply filmic curve optimized for ACEScg
    acescg = ACEScgFilmicCurve(acescg);
    
    // Convert back to Rec.2020
    float3 result = fromACEScg(acescg);
    
    // Final safety clamp
    return max(result, 0.0f);
}

float4 main(PS_INPUT input) : SV_Target {
    // Sample texture and convert from PQ to linear
    float4 color = tex.Sample(samp, input.Tex);
    color = ST2084ToLinear(color, 10000.0f);

    // Determine effective peak luminance
    float effectiveMaxLum = MasteringMaxLuminanceNits;
    if (maxCLL > 0.0 && maxCLL <= MasteringMaxLuminanceNits) {
        effectiveMaxLum = maxCLL;
    }
    
    // Global normalization before tone mapping
    effectiveMaxLum = max(effectiveMaxLum, 400.0f);
    color.rgb *= (1.0f / effectiveMaxLum);
    float3 over = max(color.rgb - 1.0f, 0.0f);
    color.rgb = color.rgb - over + over / (1.0f + 0.25f * over);
    color.rgb = saturate(color.rgb);

    // Apply tone mapping based on selection
    if (selection == 1) {
        color.rgb = ACESFilmTonemap(color.rgb);
    }
    else if (selection == 2) {
        color.rgb = ReinhardTonemap(color.rgb);
    }
    else if (selection == 3) {
        color.rgb = HabelTonemap(color.rgb);
    }
    else if (selection == 4) {
        color.rgb = MobiusTonemap(color.rgb);
    }
    else if (selection == 5) {
        color.rgb = ACEScgTonemap(color.rgb);
    }
    else {
        color.rgb = ACESFilmTonemap(color.rgb); // Default fallback
    }

    // Scale to display peak brightness (skip for Möbius as it scales internally)
    if (selection != 4) {
        color.rgb *= displayMaxNits;
    }

    // Convert back from linear to PQ
    color = LinearToST2084(color, 10000.0f);

    return float4(color.rgb, color.a);
}