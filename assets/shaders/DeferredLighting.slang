// @entry: VSMain
// @entry: PSMain

// ============================================================================
// File: assets/shaders/DeferredLighting.slang
// Screen-Space Full Screen Composition PBR Shader Layout
// ============================================================================

struct VSInput {
    uint VertexID : SV_VertexID; // Generated procedurally by hardware draw(3) call
};

struct VSOutput {
    float4 Position : SV_POSITION;
    float2 UV       : TEXCOORD0;
};

// Procedural full-screen triangle generator
// Generates coordinates covering [-1, 1] clip space without CPU allocations
[shader("vertex")]
VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.UV = float2((input.VertexID << 1) & 2, input.VertexID & 2);
    output.Position = float4(output.UV * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

// Global uniform variables mapping tracking positions
struct UniformBufferObject {
    float4x4 view;
    float4x4 proj;
    float4 lightPositions[4];
    float4 lightColors[4];
    float4 camPos;
    float exposure;
    float gamma;
};

[[vk::binding(0, 0)]] ConstantBuffer<UniformBufferObject> ubo;

// G-Buffer texture target mapping allocations (Descriptor Set 1)
[[vk::binding(0, 1)]] Sampler2D gBufferPosition;
[[vk::binding(1, 1)]] Sampler2D gBufferNormal;
[[vk::binding(2, 1)]] Sampler2D gBufferAlbedo;

static const float PI = 3.14159265359;

// (Include your Cook-Torrance BRDF helper functions here: DistributionGGX, GeometrySmith, FresnelSchlick)

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

// Fresnel Reflectance (F) - Schlick's approximation
// Models how reflectance changes with viewing angle
float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
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

[shader("fragment")]
float4 PSMain(VSOutput input) : SV_TARGET
{
    // 1. SAMPLE RAW ATTRIBUTES STRAIGHT FROM SCREEN TEXTURE COORDINATES
    float3 worldPos = gBufferPosition.Sample(input.UV).xyz;
    float3 N        = gBufferNormal.Sample(input.UV).xyz;
    float4 albedo   = gBufferAlbedo.Sample(input.UV);

    // Early out discard handling if pixel sample lands on a background clear void point
    if (dot(N, N) == 0.0) {
        return float4(0.1, 0.1, 0.15, 1.0); // Match background pass color layout
    }

    float3 V = normalize(ubo.camPos.xyz - worldPos);
    float3 F0 = float3(0.04);
    F0 = lerp(F0, albedo.rgb, 0.0); // Fallback assumption context tracker (Metallic = 0 for default)

    float3 Lo = float3(0.0);

    // 2. RUN CONCENTRATED DIRECT LIGHTING PER PIXEL TRACT
    for (int i = 0; i < 4; i++) {
        float3 lightPos = ubo.lightPositions[i].xyz;
        float3 lightColor = ubo.lightColors[i].rgb;

        float3 L = normalize(lightPos - worldPos);
        float distance = length(lightPos - worldPos);
        float attenuation = 1.0 / (distance * distance);
        float3 radiance = lightColor * attenuation;

        float3 H = normalize(V + L);
        float NdotL = max(dot(N, L), 0.0);
        float NdotV = max(dot(N, V), 0.0);
        float NdotH = max(dot(N, H), 0.0);
        float HdotV = max(dot(H, V), 0.0);

        // BRDF Calculations (D * G * F)
        float D = DistributionGGX(NdotH, 0.5); // Fixed roughness tracking value fallback
        float G = GeometrySmith(NdotV, NdotL, 0.5);
        float3 F = FresnelSchlick(HdotV, F0);

        float3 specular = (D * G * F) / (4.0 * NdotV * NdotL + 0.0001);

        float3 kS = F;
        float3 kD = float3(1.0) - kS;

        Lo += (kD * albedo.rgb / PI + specular) * radiance * NdotL;
    }

    float3 ambient = float3(0.03) * albedo.rgb;
    float3 color = ambient + Lo;

    // Reinhard Tone Mapping and Gamma Correction 
    color = color / (color + float3(1.0));
    color = pow(color, float3(1.0 / ubo.gamma));

    return float4(color, albedo.a);
}