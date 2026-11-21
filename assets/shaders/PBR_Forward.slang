// @entry: VSMain
// @entry: PSMain

// This shader reuses the exact vertex layout, Set 0 UBO, Set 1 material textures, and push constants from pbr.slang, 
// while running direct PBR lighting + Reinhard tone mapping + gamma correction from DeferredLighting.slang into a single alpha-blended SV_Target0 output.

struct VSInput {
    [[vk::location(0)]] float3 Position : POSITION;
    [[vk::location(1)]] float3 Normal   : NORMAL;
    [[vk::location(2)]] float3 Color    : COLOR;
    [[vk::location(3)]] float2 UV       : TEXCOORD0;
};

struct VSOutput {
    float4 Position : SV_POSITION;
    float3 WorldPos : POSITION;
    float3 Normal   : NORMAL;
    float2 UV       : TEXCOORD0;
    float3 Color    : COLOR;
};

struct UniformBufferObject {
    float4x4 view;
    float4x4 proj;
    float4x4 lightSpaceMatrices[4];
    int4 lightShadowEnabled;
    float4 lightPositions[4];  // xyz = pos (or dir if w==0), w = type (0=Dir, 1=Point, 2=Spot)
    float4 lightColors[4];     // rgb = color * intensity, a = radius
    float4 lightDirections[4]; // xyz = spot forward dir, w = cos(outerCone)
    float4 lightConeParams[4]; // x = cos(innerCone), y = cos(outerCone)
    float4 camPos;
    float exposure;
    float gamma;
    float prefilteredCubeMipLevels;
    float scaleIBLAmbient; // Added to match C++ UBO size
};

struct PushConstants {
    float4x4 modelMatrix;
    float4 baseColorFactor;
    float4 emissiveFactor;              // xyz = color, w = strength
    float4 coatAndIridescence;          // x = clearcoat, y = clearcoatRoughness, z = iridescence, w = iridescenceIor
    float metallicFactor;
    float roughnessFactor;
    int baseColorTextureSet;
    int physicalDescriptorTextureSet;
    int normalTextureSet;
    int occlusionTextureSet;
    int emissiveTextureSet;
    float alphaMask;
    float alphaMaskCutoff;
};

static const float PI = 3.14159265359;

[[vk::binding(0, 0)]] ConstantBuffer<UniformBufferObject> ubo;

[[vk::binding(0, 1)]] Sampler2D baseColorMap;
[[vk::binding(1, 1)]] Sampler2D metallicRoughnessMap;
[[vk::binding(2, 1)]] Sampler2D normalMap;
[[vk::binding(3, 1)]] Sampler2D occlusionMap;
[[vk::binding(4, 1)]] Sampler2D emissiveMap;

[[vk::push_constant]] PushConstants material;

float DistributionGGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom);
}

float GeometrySmith(float NdotV, float NdotL, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float ggx1 = NdotV / (NdotV * (1.0 - k) + k);
    float ggx2 = NdotL / (NdotL * (1.0 - k) + k);
    return ggx1 * ggx2;
}

float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

[shader("vertex")]
VSOutput VSMain(VSInput input)
{
    VSOutput output;
    float4 worldPos = mul(material.modelMatrix, float4(input.Position, 1.0));
    output.Position = mul(ubo.proj, mul(ubo.view, worldPos));
    output.WorldPos = worldPos.xyz;
    output.Normal = normalize(mul((float3x3)material.modelMatrix, input.Normal));
    output.UV = input.UV;
    output.Color = input.Color;
    return output;
}

[shader("fragment")]
float4 PSMain(VSOutput input, bool isFrontFace : SV_IsFrontFace) : SV_TARGET
{
    float4 baseColor = baseColorMap.Sample(input.UV) * material.baseColorFactor;

    // Discard transparent cutout pixels (grilles, vents, foliage, decals)
    if (material.alphaMask > 0.5 && baseColor.a < material.alphaMaskCutoff) {
        discard;
    }

    float2 metallicRoughness = metallicRoughnessMap.Sample(input.UV).bg;
    float metallic = metallicRoughness.x * material.metallicFactor;
    float roughness = clamp(metallicRoughness.y * material.roughnessFactor, 0.04, 1.0);

    float ao = (material.occlusionTextureSet >= 0) ? occlusionMap.Sample(input.UV).r : 1.0;
    float3 emissiveTex = (material.emissiveTextureSet >= 0) ? emissiveMap.Sample(input.UV).rgb : float3(1.0);
    float3 emissive = emissiveTex * material.emissiveFactor.rgb * material.emissiveFactor.w;

    // Flip normal on back faces so double-sided glass shades properly from inside and outside
    float3 N = normalize(input.Normal) * (isFrontFace ? 1.0 : -1.0);

    float3 V = normalize(ubo.camPos.xyz - input.WorldPos);
    float3 F0 = lerp(float3(0.04), baseColor.rgb, metallic);

    float3 Lo = float3(0.0);
    for (int i = 0; i < 4; i++) {
        float3 lightColor = ubo.lightColors[i].rgb;
        if (dot(lightColor, lightColor) == 0.0) continue;

        float3 L;
        float attenuation = 1.0;

        if (ubo.lightPositions[i].w == 0.0) {
            L = normalize(-ubo.lightPositions[i].xyz);
        } else {
            float3 toLight = ubo.lightPositions[i].xyz - input.WorldPos;
            float distance = length(toLight);
            float radius = max(ubo.lightColors[i].a, 0.001);
            L = toLight / distance;
            float factor = saturate(1.0 - (distance / radius));
            attenuation = (factor * factor) / (1.0 + 0.1 * distance * distance);
        }

        float3 radiance = lightColor * attenuation;
        float3 H = normalize(V + L);
        float NdotL = max(dot(N, L), 0.0);
        float NdotV = max(dot(N, V), 0.0);
        float NdotH = max(dot(N, H), 0.0);
        float HdotV = max(dot(H, V), 0.0);

        float D = DistributionGGX(NdotH, roughness);
        float G = GeometrySmith(NdotV, NdotL, roughness);
        float3 F = FresnelSchlick(HdotV, F0);

        float3 specular = (D * G * F) / (4.0 * NdotV * NdotL + 0.0001);
        float3 kD = (float3(1.0) - F) * (1.0 - metallic);

        Lo += (kD * baseColor.rgb / PI + specular) * radiance * NdotL;
    }

    // Subtle sky-to-ground reflection tint so glass reads clearly as reflective glass
    float fresnelRim = pow(1.0 - max(dot(N, V), 0.0), 3.0);
    float3 R = reflect(-V, N);
    float3 envReflect = lerp(float3(0.05, 0.05, 0.05), float3(0.22, 0.28, 0.38), saturate(R.y * 0.5 + 0.5));
    // Scale ambient & reflection by ubo.scaleIBLAmbient so glass darkens properly when lights are off
    float3 ambient = (float3(0.04, 0.05, 0.07) * baseColor.rgb * ao + envReflect * (0.15 + 0.65 * fresnelRim)) * ubo.scaleIBLAmbient;
    float3 color = ambient + Lo * 1.5 + emissive;

    // Reinhard tone mapping (no manual gamma pow, since FinalColor is Srgb!)
    //color = color / (color + float3(1.0));

    //float finalAlpha = saturate(max(baseColor.a, 0.20 + fresnelRim * 0.50));
    float finalAlpha = saturate(max(baseColor.a * 0.5, 0.06 + fresnelRim * 0.55));
    return float4(color, finalAlpha);
}