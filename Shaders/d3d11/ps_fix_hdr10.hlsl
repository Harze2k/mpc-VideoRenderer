#include "../convert/st2084.hlsl"

Texture2D tex : register(t0);
SamplerState samp : register(s0);

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD;
};

// HDR10 tone mapping params (dedicated slot to avoid clashes with other passes)
cbuffer HDR10ParamsCB : register(b0)
{
    float masteringMinLuminanceNits;
    float masteringMaxLuminanceNits;
    float maxCLL;
    float maxFALL;
    float displayMaxNits; // <- lowercase to match uses
    uint  selection;      // <- lowercase to match uses
    float reserved1;
    float reserved2;
}

// Standard ACES RRT + ODT Implementation
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
    const float maxL = max(displayMaxNits, 100.0f);
    return color / (1.0 + color / (maxL + epsilon));
}

// Enhanced ACES - Better alternative to ACEScg
// This provides better color preservation and more accurate tone mapping
float3 EnhancedACESTonemap(float3 color) {
    // Pre-exposure adjustment for better highlight rolloff
    color *= 0.6f;
    
    // Enhanced ACES curve with better shadow/highlight separation
    const float a = 2.8f;   // Higher contrast in mids
    const float b = 0.01f;  // Darker shadows 
    const float c = 2.2f;   // Earlier highlight rolloff
    const float d = 0.7f;   // Smoother highlights
    const float e = 0.08f;  // Better black point
    
    float3 result = (color * (a * color + b)) / (color * (c * color + d) + e);
    
    // Post-exposure compensation
    result *= 1.2f;
    
    return saturate(result);
}

// Fallback tone mapping for debugging
float3 SimpleTonemap(float3 color) {
    // Simple Reinhard-style mapping that should always work
    return color / (color + 1.0f);
}

float4 main(PS_INPUT input) : SV_Target {
    // Sample texture
    float4 color = tex.Sample(samp, input.Tex);
    
    // Debug: Return magenta if we get invalid input to identify shader issues
    if (any(isnan(color.rgb)) || any(isinf(color.rgb))) {
        return float4(1.0f, 0.0f, 1.0f, 1.0f); // Magenta for NaN/Inf
    }
    
    // Convert from PQ to linear with error handling
    float4 linearColor = color;
    if (any(color.rgb > 0.0f)) {
        linearColor = ST2084ToLinear(color, 10000.0f);
        
        // Check for conversion errors
        if (any(isnan(linearColor.rgb)) || any(isinf(linearColor.rgb))) {
            return float4(0.0f, 1.0f, 1.0f, 1.0f); // Cyan for conversion error
        }
    }
    
    // Ensure no negative values
    linearColor.rgb = max(linearColor.rgb, 0.0f);
    
    // Determine effective peak luminance
    float effectiveMaxLum = max(MasteringMaxLuminanceNits, 1000.0f);
    if (maxCLL > 100.0f && maxCLL <= MasteringMaxLuminanceNits) {
        effectiveMaxLum = maxCLL;
    }
    
    // Normalize to [0,1] range for tone mapping
    linearColor.rgb /= effectiveMaxLum;
    
    // Gentle highlight rolloff before tone mapping
    float maxComponent = max(max(linearColor.r, linearColor.g), linearColor.b);
    if (maxComponent > 1.0f) {
        float rolloff = 1.0f / (1.0f + (maxComponent - 1.0f) * 0.5f);
        linearColor.rgb *= rolloff;
    }
    
    // Apply tone mapping (selection: 1=ACES, 2=Reinhard, 3=Habel, 4=Mobius, 5=Enhanced ACES)
    float3 toneMapped;
    uint sel = selection;
    sel = (sel < 1u || sel > 5u) ? 1u : sel; // sanitize: default to ACES

    switch (sel) {
    case 5u: // Enhanced ACES
        toneMapped = EnhancedACESTonemap(linearColor.rgb);
        break;
    case 1u: // ACES
        toneMapped = ACESFilmTonemap(linearColor.rgb);
        break;
    case 2u: // Reinhard
        toneMapped = ReinhardTonemap(linearColor.rgb);
        break;
    case 3u: // Habel
        toneMapped = HabelTonemap(linearColor.rgb);
        break;
    case 4u: // Mobius
        toneMapped = MobiusTonemap(linearColor.rgb);
        break;
    default:
        toneMapped = linearColor.rgb; // unreachable due to sanitize
        break;
    }
    
    // Check for tone mapping errors
    if (any(isnan(toneMapped)) || any(isinf(toneMapped))) {
        return float4(1.0f, 1.0f, 0.0f, 1.0f); // Yellow for tone mapping error
    }
    
    // Scale to display brightness (skip for Möbius)
    if (selection != 4) {
        float targetBrightness = max(displayMaxNits, 100.0f);
        toneMapped *= targetBrightness;
    }
    
    // Final clamp
    toneMapped = max(toneMapped, 0.0f);
    
    // Convert back to PQ with error handling
    float4 result = float4(toneMapped, linearColor.a);
    
    if (any(toneMapped > 0.0f)) {
        result = LinearToST2084(result, 10000.0f);
        
        // Final error check
        if (any(isnan(result.rgb)) || any(isinf(result.rgb))) {
            return float4(1.0f, 0.0f, 0.0f, 1.0f); // Red for final conversion error
        }
    }
    
    return result;
}