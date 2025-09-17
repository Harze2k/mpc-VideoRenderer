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
    uint selection; // 1 = ACES, 2 = Reinhard, 3 = Habel, 4 = Möbius
};

// ✅ ACES RRT + ODT Implementation
float3 RRTAndODTFit(float3 color) {
    // Constants used in the ACES Filmic tone mapping
    float A = 2.51f;  // Constant A
    float B = 0.03f;  // Constant B
    float C = 2.43f;  // Constant C
    float D = 0.59f;  // Constant D
    float E = 0.14f;  // Constant E

    // Apply the ACES RRT + ODT
    color = (color * (A * color + B)) / (color * (C * color + D) + E);
    
    return color;
}

// ✅ ACES Tone Mapping
float3 ACESFilmTonemap(float3 color) {
    return RRTAndODTFit(color);
}

// ✅ Reinhard Tone Mapping
float3 ReinhardTonemap(float3 color) {
    return color / (1.0 + color);
}

// ✅ Habel Tone Mapping
float3 HabelTonemap(float3 color) {
    float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;
    return ((color * (A * color + C * B) + D * E) / (color * (A * color + B) + D * F)) - E / F;
}

// ✅ Möbius Tone Mapping
float3 MobiusTonemap(float3 color) {
    float epsilon = 1e-6;
    float maxL = displayMaxNits;
    return color / (1.0 + color / (maxL + epsilon));
}

// ✅ Main Shader Entry Point
float4 main(PS_INPUT input) : SV_Target {
    // Sample texture and convert from PQ to linear
    float4 color = tex.Sample(samp, input.Tex);
    color = ST2084ToLinear(color, 10000.0f); // Convert PQ to Linear space

    // Determine the effective peak luminance of the content for normalization.
    // Use maxCLL if it is valid (greater than 0) and reasonably within the mastering display's peak.
    // Otherwise, fall back to the mastering display's peak luminance. This avoids issues with missing
    // or incorrect maxCLL metadata.
    float effectiveMaxLum = MasteringMaxLuminanceNits;
    if (maxCLL > 0.0 && maxCLL <= MasteringMaxLuminanceNits) {
        effectiveMaxLum = maxCLL;
    }
    
    // Apply global normalization **before tone mapping**
    // This scales the incoming linear light values so that the peak brightness
    // of the content corresponds to a value of 1.0 before entering the tone mapper.
    color.rgb *= (1.0f / effectiveMaxLum);
    color.rgb = saturate(color.rgb);

    // Select the tone mapping function based on `selection`
    if (selection == 1) {
        color.rgb = ACESFilmTonemap(color.rgb);  // Apply ACES Tone Mapping
    }
    else if (selection == 2) {
        color.rgb = ReinhardTonemap(color.rgb);  // Apply Reinhard Tone Mapping
    }
    else if (selection == 3) {
        color.rgb = HabelTonemap(color.rgb);  // Apply Habel Tone Mapping
    }
    else if (selection == 4) {
        color.rgb = MobiusTonemap(color.rgb);  // Apply Möbius Tone Mapping
    }
    else {
        color.rgb = ACESFilmTonemap(color.rgb);  // Default fallback to ACES
    }

    // Scale to display peak brightness after tone mapping (skip for Möbius as it scales internally)
    // The tonemapper outputs a normalized value (0-1), so we scale it to the target display's peak brightness.
    if (selection != 4) {
        color.rgb *= displayMaxNits;
    }

    // Convert back from linear to PQ color space
    color = LinearToST2084(color, 10000.0f);  // Convert Linear to PQ

    return float4(color.rgb, color.a);  // Final output
}
