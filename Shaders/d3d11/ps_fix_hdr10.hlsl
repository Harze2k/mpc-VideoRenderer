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
    return saturate((color * (A * color + B)) / (color * (C * color + D) + E));
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

// Improved ACEScg tone mapping
float3 ACEScgTonemap(float3 color) {
    // Ensure no negative values
    color = max(color, 0.0f);
    
    // Simple but effective ACEScg-inspired tone mapping
    // Based on ACES but with adjustments for the ACEScg color space
    const float a = 2.8f;   // Slightly higher contrast
    const float b = 0.02f;  // Lower toe
    const float c = 2.6f;   // Adjusted shoulder start
    const float d = 0.8f;   // Higher shoulder
    const float e = 0.1f;   // Lower black point
    
    float3 result = (color * (a * color + b)) / (color * (c * color + d) + e);
    return saturate(result);
}

float4 main(PS_INPUT input) : SV_Target {
    // Sample texture and convert from PQ to linear
    float4 color = tex.Sample(samp, input.Tex);
    
    // Safety check for invalid input
    if (any(isnan(color.rgb)) || any(isinf(color.rgb))) {
        return float4(0.0f, 0.0f, 0.0f, color.a);
    }
    
    color = ST2084ToLinear(color, 10000.0f);
    
    // Safety clamp after PQ conversion
    color.rgb = max(color.rgb, 0.0f);

    // Determine effective peak luminance with better defaults
    float effectiveMaxLum = max(MasteringMaxLuminanceNits, 1000.0f);
    if (maxCLL > 100.0f && maxCLL <= MasteringMaxLuminanceNits) {
        effectiveMaxLum = maxCLL;
    }
    
    // Normalize to [0,1] range for tone mapping
    color.rgb /= effectiveMaxLum;
    
    // Soft clamp for values above 1.0
    float3 over = max(color.rgb - 1.0f, 0.0f);
    color.rgb = color.rgb - over + over / (1.0f + over);
    
    // Apply tone mapping based on selection
    if (selection == 5) {
        // ACEScg tone mapping
        color.rgb = ACEScgTonemap(color.rgb);
    }
    else if (selection == 1) {
        // ACES tone mapping  
        color.rgb = ACESFilmTonemap(color.rgb);
    }
    else if (selection == 2) {
        // Reinhard tone mapping
        color.rgb = ReinhardTonemap(color.rgb);
    }
    else if (selection == 3) {
        // Habel tone mapping
        color.rgb = HabelTonemap(color.rgb);
    }
    else if (selection == 4) {
        // Möbius tone mapping
        color.rgb = MobiusTonemap(color.rgb);
    }
    else {
        // Default to ACES
        color.rgb = ACESFilmTonemap(color.rgb);
    }

    // Scale to display peak brightness (skip for Möbius as it handles this internally)
    if (selection != 4) {
        color.rgb *= max(displayMaxNits, 100.0f);
    }

    // Final safety clamp before PQ conversion
    color.rgb = max(color.rgb, 0.0f);

    // Convert back from linear to PQ
    color = LinearToST2084(color, 10000.0f);

    return color;
}