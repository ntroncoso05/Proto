// @entry: VSMain
// @entry: PSMain

#include "BindlessMaterial.hlsli"

// Combined vertex and fragment shader for PBR rendering

// Input from vertex buffer - Data sent per vertex from CPU
struct VSInput {
    [[vk::location(0)]] float3 Position : POSITION;  // Location 0 (pos)
    [[vk::location(1)]] float3 Normal   : NORMAL;    // Location 1 (normal)
    [[vk::location(2)]] float3 Color    : COLOR;     // Location 2 (color)
    [[vk::location(3)]] float2 UV       : TEXCOORD0; // 🟩 FIXED: Maps perfectly to location 3 (texCoord)!
};

// Output from vertex shader / Input to fragment shader - Interpolated data
struct VSOutput {
    float4 Position : SV_POSITION; // Required clip space position for rasterization
    float3 WorldPos : POSITION;    // World space position for lighting calculations
    float3 Normal : NORMAL;        // World space normal (interpolated)
    float2 UV : TEXCOORD0;         // Texture coordinates (interpolated)
    float3 Color    : COLOR;       // 🟩 Added tracking channel
};

// Uniform buffer - Global data shared across all vertices/fragments
struct UniformBufferObject {
    float4x4 view;                      // World-to-camera transformation matrix
    float4x4 proj;                      // Camera-to-clip space projection matrix
    float4 lightPositions[4];           // Light positions in world space
    float4 lightColors[4];              // Light intensities and colors
    float4 camPos;                      // Camera position for view-dependent effects
    float exposure;                     // HDR exposure control
    float gamma;                        // Gamma correction value (typically 2.2)
    float prefilteredCubeMipLevels;     // IBL prefiltered environment map mip levels
    float scaleIBLAmbient;              // IBL ambient contribution scale
};

// Mathematical constants
static const float PI = 3.14159265359;

// Resource bindings - Connect CPU resources to GPU shader registers
[[vk::binding(0, 0)]] ConstantBuffer<UniformBufferObject> ubo;

// Normal Distribution Function (D) - GGX/Trowbridge-Reitz Distribution
// Describes the statistical distribution of microfacet orientations
float DistributionGGX(float NdotH, float roughness) {
    float a = roughness * roughness;        // Remapping for more perceptual linearity
    float a2 = a * a;
    float NdotH2 = NdotH * NdotH;

    float nom = a2;                         // Numerator: concentration factor
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;             // Normalization factor

    return nom / denom;                     // Normalized distribution
}

// Geometry Function (G) - Smith's method with Schlick-GGX approximation
// Models self-shadowing and masking between microfacets
float GeometrySmith(float NdotV, float NdotL, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;               // Direct lighting remapping

    // Geometry obstruction from view direction (masking)
    float ggx1 = NdotV / (NdotV * (1.0 - k) + k);
    // Geometry obstruction from light direction (shadowing)
    float ggx2 = NdotL / (NdotL * (1.0 - k) + k);

    return ggx1 * ggx2;                     // Combined masking-shadowing
}

// Fresnel Reflectance (F) - Schlick's approximation
// Models how reflectance changes with viewing angle
float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

// Vertex shader entry point - Executes once per vertex
[[shader("vertex")]]
VSOutput VSMain(VSInput input)
{
    VSOutput output;

    // Transform vertex position through the rendering pipeline
    // Model -> World -> Camera -> Clip space transformation chain
    float4 worldPos = mul(pushConst.modelMatrix, float4(input.Position, 1.0));
    output.Position = mul(ubo.proj, mul(ubo.view, worldPos));

    // Pass world position for fragment lighting calculations
    // Fragment shader needs world space position to calculate light vectors
    output.WorldPos = worldPos.xyz;

    // Transform normal from model space to world space
    // Use only rotation/scale part of model matrix (upper-left 3x3)
    // Normalize to ensure unit length after transformation
    output.Normal = normalize(mul((float3x3)pushConst.modelMatrix, input.Normal));

    // Pass through texture coordinates unchanged
    // UV coordinates are typically in [0,1] range and don't need transformation
    output.UV = input.UV;
    output.Color = input.Color;

    // Pass tangent vector for normal mapping
    // Will be used in fragment shader to construct tangent-space basis

    return output;
}

// Modify output layout structure for deferred storage
struct GBufferOutput {
    [[vk::location(0)]] float4 Position : SV_Target0; // GBuffer_Position
    [[vk::location(1)]] float4 Normal   : SV_Target1; // GBuffer_Normal
    [[vk::location(2)]] float4 Albedo   : SV_Target2; // GBuffer_Albedo
    [[vk::location(3)]] float4 Emissive : SV_Target3; // GBuffer_Emissive
    [[vk::location(4)]] float4 SurfaceExt : SV_Target4; // GBuffer_SurfaceExt (clearcoat, coatRoughness, iridescence, encodedIor)
};

// Fragment shader entry point - Executes once per pixel
[shader("fragment")]
GBufferOutput PSMain(VSOutput input, bool isFrontFace : SV_IsFrontFace)
{
    GPUMaterialData mat = g_Materials[pushConst.materialData.x];
    GBufferOutput output;

    // 1. Sample base color and apply material tint
    float4 baseColor = SampleBindless(mat.textureIndices0.x, input.UV, float4(1.0)) * mat.baseColorFactor;

    if (mat.metallicRoughnessAlpha.z > 0.5 && baseColor.a < mat.metallicRoughnessAlpha.w) {
        discard;
    }

    // 3. Sample metallic (B) and roughness (G)
    float2 mrSample = SampleBindless(mat.textureIndices0.y, input.UV, float4(1.0)).bg;
    float metallic = mrSample.x * mat.metallicRoughnessAlpha.x;
    float roughness = mrSample.y * mat.metallicRoughnessAlpha.y;

    // 4. Flip normal on back-faces of double-sided meshes
    float3 N = normalize(input.Normal) * (isFrontFace ? 1.0 : -1.0);

    // 5. Normal mapping using screen-space derivatives
    if (mat.textureIndices0.z >= 0) {
        float3 tangentNormal = SampleBindless(mat.textureIndices0.z, input.UV, float4(0.5, 0.5, 1.0, 1.0)).xyz * 2.0 - 1.0;

        float3 dp1 = ddx(input.WorldPos);
        float3 dp2 = ddy(input.WorldPos);
        float2 duv1 = ddx(input.UV);
        float2 duv2 = ddy(input.UV);

        float3 dp2perp = cross(dp2, N);
        float3 dp1perp = cross(N, dp1);
        float3 T = dp2perp * duv1.x + dp1perp * duv2.x;
        float3 B = dp2perp * duv1.y + dp1perp * duv2.y;

        float invmax = rsqrt(max(dot(T, T), dot(B, B)) + 1e-6);
        float3x3 TBN = float3x3(T * invmax, B * invmax, N);

        N = normalize(mul(tangentNormal, TBN));
    }

    // Sample emissive texture only if assigned (prevents default_white from making everything glow!)
    float3 emissiveTex = SampleBindless(mat.textureIndices1.x, input.UV, float4(1.0)).rgb;
    float3 emissive = emissiveTex * mat.emissiveFactor.rgb * mat.emissiveFactor.w;

    // 6. Pack G-Buffer targets
    output.Position = float4(input.WorldPos, metallic);
    output.Normal   = float4(N, roughness);
    output.Albedo   = baseColor;
    output.Emissive = float4(emissive, 1.0);

    float encodedIor = saturate((mat.coatAndIridescence.w - 1.0) / 2.0); // Maps IOR [1.0, 3.0] -> [0.0, 1.0]
    output.SurfaceExt = float4(
        saturate(mat.coatAndIridescence.x),
        saturate(mat.coatAndIridescence.y),
        saturate(mat.coatAndIridescence.z),
        encodedIor
    );

    return output;
}