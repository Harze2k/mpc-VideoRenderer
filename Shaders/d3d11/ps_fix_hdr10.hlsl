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
    uint selection;
};

// Use fixed default values for enhanced parameters
static const float dynamicRangeCompression = 0.5f;
static const float shadowDetail = 1.2f;
static const float colorVolumeAdaptation = 0.8f;
static const float sceneAdaptation = 0.6f;

// Rec.2020 color primaries and white point (for Dolby Vision)
static const float3x3 REC2020_TO_XYZ = float3x3(
    0.6369580, 0.1446169, 0.1688809,
    0.2627045, 0.6780980, 0.0593017,
    0.0000000, 0.0280727, 1.0609851
);

static const float3x3 XYZ_TO_REC2020 = float3x3(
    1.7166511, -0.3556708, -0.2533663,
    -0.6666844, 1.6164812, 0.0157685,
    0.0176399, -0.0427706, 0.9421031
);

// ACES color space matrices (AP0 and AP1)
static const float3x3 REC2020_TO_ACES_AP0 = float3x3(
    0.9439, 0.0000, 0.0168,
    0.0106, 1.0016, -0.0041,
    0.0019, 0.0075, 0.9906
);

static const float3x3 ACES_AP0_TO_REC2020 = float3x3(
    1.0593, 0.0000, -0.0181,
    -0.0112, 0.9984, 0.0041,
    -0.0020, -0.0075, 1.0095
);

// Luminance weights for different color spaces
static const float3 REC2020_LUMA = float3(0.2627, 0.6780, 0.0593);
static const float3 ACES_LUMA = float3(0.2722287, 0.6740818, 0.0536895);

// ✅ Enhanced ACES for Dolby Vision with Dynamic Adaptation
float3 EnhancedACESForDolbyVision(float3 color) {
	if (dynamicRangeCompression <= 0.0 && shadowDetail <= 0.0 && 
			colorVolumeAdaptation <= 0.0 && sceneAdaptation <= 0.0) {
			// Use basic ACES formula as fallback
			return saturate((color * (2.51f * color + 0.03f)) / (color * (2.43f * color + 0.59f) + 0.14f));
		}
    // Step 1: Calculate scene-relative luminance for adaptation
    float sceneLuma = dot(color, REC2020_LUMA);
    float normalizedLuma = sceneLuma / MasteringMaxLuminanceNits;
    
    // Step 2: Dynamic range compression based on scene brightness
    float compressionFactor = lerp(1.0, 0.7, dynamicRangeCompression * normalizedLuma);
    color *= compressionFactor;
    
    // Step 3: Transform to ACES color space for processing
    float3 acesColor = mul(REC2020_TO_ACES_AP0, color);
    
    // Step 4: Enhanced ACES RRT with Dolby Vision optimizations
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    
    // Dynamic adjustment of ACES parameters based on content
    float contentAdaptation = saturate(normalizedLuma * sceneAdaptation);
    
    // Adjust 'a' parameter for better highlight handling in bright scenes
    a = lerp(2.51f, 2.20f, contentAdaptation);
    
    // Adjust 'e' parameter for better shadow detail
    e = lerp(0.14f, 0.12f, shadowDetail * 0.5);
    
    // Apply modified ACES tone curve
    float3 toneMapped = (acesColor * (a * acesColor + b)) / (acesColor * (c * acesColor + d) + e);
    
    // Step 5: Transform back to Rec.2020
    toneMapped = mul(ACES_AP0_TO_REC2020, toneMapped);
    
    // Step 6: Color volume adaptation for wide gamut displays
    if (colorVolumeAdaptation > 0.0) {
        float maxComponent = max(max(toneMapped.r, toneMapped.g), toneMapped.b);
        if (maxComponent > 1.0) {
            // Smart gamut compression that preserves hue
            float compressionRatio = 1.0 / maxComponent;
            float adaptedRatio = lerp(1.0, compressionRatio, colorVolumeAdaptation);
            toneMapped *= adaptedRatio;
        }
    }
    
    return saturate(toneMapped);
}

// ✅ Dolby Vision Specific Shadow Enhancement
float3 EnhanceShadows(float3 color, float enhancement) {
    if (enhancement <= 0.0) return color;
    
    float luma = dot(color, REC2020_LUMA);
    
    // Shadow enhancement curve (affects mainly dark areas)
    float shadowMask = 1.0 - smoothstep(0.0, 0.3, luma);
    float shadowBoost = 1.0 + (enhancement * shadowMask * 0.5);
    
    // Apply enhancement while preserving color ratios
    if (luma > 1e-6) {
        float3 chromaticity = color / luma;
        float enhancedLuma = luma * shadowBoost;
        return chromaticity * enhancedLuma;
    }
    
    return color;
}

// ✅ Dynamic Highlight Protection for Peak Brightness Scenes
float3 ProtectHighlights(float3 color, float protection) {
    if (protection <= 0.0) return color;
    
    float luma = dot(color, REC2020_LUMA);
    float maxDisplayLuma = displayMaxNits / 10000.0f;
    
    // Protect highlights when approaching display limits
    if (luma > maxDisplayLuma * 0.8) {
        float highlightRatio = (luma - maxDisplayLuma * 0.8) / (maxDisplayLuma * 0.2);
        float protectionFactor = 1.0 - (protection * highlightRatio * 0.3);
        color *= protectionFactor;
    }
    
    return color;
}

// ✅ Dolby Vision Perceptual Quantizer Optimization
float3 OptimizedPQHandling(float3 linearColor) {
    // Apply perceptual adjustments before PQ conversion
    float avgLuma = dot(linearColor, REC2020_LUMA);
    
    // Slightly boost mid-tones for better perceptual quality
    float midtoneBoost = 1.0 + (0.1 * (1.0 - abs(avgLuma - 0.18) / 0.18));
    midtoneBoost = clamp(midtoneBoost, 0.9, 1.1);
    
    return linearColor * midtoneBoost;
}

// ✅ Advanced Color Preservation for Wide Gamut Content
float3 PreserveColorVolume(float3 originalColor, float3 toneMappedColor) {
    float3 originalXYZ = mul(REC2020_TO_XYZ, originalColor);
    float3 toneMappedXYZ = mul(REC2020_TO_XYZ, toneMappedColor);
    
    // Preserve chromaticity in XYZ space for better color accuracy
    float originalY = originalXYZ.y;
    float toneMappedY = toneMappedXYZ.y;
    
    if (originalY > 1e-6 && toneMappedY > 1e-6) {
        float3 preservedXYZ = originalXYZ * (toneMappedY / originalY);
        float3 preservedRec2020 = mul(XYZ_TO_REC2020, preservedXYZ);
        
        // Blend between tone mapped and color preserved based on saturation
        float saturation = length(originalColor - dot(originalColor, REC2020_LUMA));
        float blendFactor = saturate(saturation * 2.0);
        
        return lerp(toneMappedColor, preservedRec2020, blendFactor * 0.7);
    }
    
    return toneMappedColor;
}

// ✅ Reinhard Tone Mapping (kept as fallback)
float3 ReinhardTonemap(float3 color) {
    float luma = dot(color, REC2020_LUMA);
    float toneMappedLuma = luma / (1.0 + luma);
    return color * (toneMappedLuma / max(luma, 1e-6));
}

// ✅ Habel Tone Mapping (kept as fallback)
float3 HabelTonemap(float3 color) {
    float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;
    return saturate(((color * (A * color + C * B) + D * E) / (color * (A * color + B) + D * F)) - E / F);
}

// ✅ Möbius Tone Mapping (kept as fallback)
float3 MobiusTonemap(float3 color) {
    float transition = displayMaxNits * 0.7;
    float peak = displayMaxNits;
    
    float luma = dot(color, REC2020_LUMA);
    
    if (luma > transition) {
        float compressionRatio = (peak - transition) / (peak - luma + 1e-6);
        color *= compressionRatio;
    }
    
    return color;
}

// ✅ Main Shader Entry Point - Optimized for Dolby Vision
float4 main(PS_INPUT input) : SV_Target {
    // Sample texture and convert from PQ to linear
    float4 color = tex.Sample(samp, input.Tex);
    color = ST2084ToLinear(color, 10000.0f);
    
    // Store original for color preservation
    float3 originalColor = color.rgb;
    
    // Enhanced luminance calculations for Dolby Vision dynamic metadata
    float effectiveMaxLum = MasteringMaxLuminanceNits;
    float contentLightLevel = maxCLL > 0.0 ? maxCLL : MasteringMaxLuminanceNits;
    float frameAverageLevel = maxFALL > 0.0 ? maxFALL : MasteringMaxLuminanceNits * 0.1;
    
    // Dynamic adaptation based on content light levels
    float adaptationFactor = 1.0;
    if (frameAverageLevel > 0.0 && frameAverageLevel < MasteringMaxLuminanceNits) {
        float fallRatio = frameAverageLevel / MasteringMaxLuminanceNits;
        adaptationFactor = lerp(0.7, 1.0, fallRatio);
    }
    
    // Intelligent display adaptation for different display capabilities
    float displayAdaptation = 1.0;
    if (displayMaxNits < MasteringMaxLuminanceNits) {
        displayAdaptation = displayMaxNits / MasteringMaxLuminanceNits;
        displayAdaptation = pow(displayAdaptation, 0.8); // Perceptual adaptation
    }
    
    // Pre-tone mapping normalization with dynamic adaptation
    effectiveMaxLum = min(contentLightLevel, MasteringMaxLuminanceNits);
    color.rgb *= (adaptationFactor * displayAdaptation) / effectiveMaxLum;
    color.rgb = max(color.rgb, 0.0);
    
    // Apply shadow enhancement before tone mapping
    color.rgb = EnhanceShadows(color.rgb, shadowDetail);
    
    // Apply selected tone mapping with enhanced ACES as primary
    float3 toneMappedColor;
    
    if (selection == 1) {
        toneMappedColor = EnhancedACESForDolbyVision(color.rgb);
    }
    else if (selection == 2) {
        toneMappedColor = ReinhardTonemap(color.rgb);
    }
    else if (selection == 3) {
        toneMappedColor = HabelTonemap(color.rgb);
    }
    else if (selection == 4) {
        toneMappedColor = MobiusTonemap(color.rgb);
    }
    else {
        toneMappedColor = EnhancedACESForDolbyVision(color.rgb); // Default to enhanced ACES
    }
    
    // Advanced color preservation for wide gamut content
    toneMappedColor = PreserveColorVolume(color.rgb, toneMappedColor);
    
    // Dynamic highlight protection
    toneMappedColor = ProtectHighlights(toneMappedColor, dynamicRangeCompression);
    
    // Scale to display capabilities
    toneMappedColor *= displayMaxNits / 10000.0f;
    
    // Optimize for PQ encoding
    toneMappedColor = OptimizedPQHandling(toneMappedColor);
    
    // Convert back to PQ color space
    color.rgb = toneMappedColor;
    color = LinearToST2084(color, 10000.0f);
    
    return float4(color.rgb, color.a);
}