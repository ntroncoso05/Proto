// @entry: VSMain
// @entry: PSMain

// ============================================================================
// Screen-Space Full Screen Composition PBR Shader Layout
// ============================================================================

struct VSInput {
    uint VertexID : SV_VertexID; // Generated procedurally by hardware draw(3) call
};

struct VSOutput {
    float4 Position : SV_POSITION;
    float2 UV       : TEXCOORD0;
};

struct ShadowPushConstants {
    float4x4 lightSpaceMatrix;
    int shadowEnabled;
};
[[vk::push_constant]] ShadowPushConstants shadowData;

// Procedural full-screen triangle generator. Generates coordinates covering [-1, 1] clip space without CPU allocations
[shader("vertex")]
VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.UV = float2((input.VertexID << 1) & 2, input.VertexID & 2);
    output.Position = float4(output.UV * float2(2.0, 2.0) + float2(-1.0, -1.0), 0.0, 1.0);
    return output;
}

// Global uniform variables mapping tracking positions
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
    float scaleIBLAmbient;          // Added to match C++ UBO size
};

[[vk::binding(0, 0)]] ConstantBuffer<UniformBufferObject> ubo;

// G-Buffer texture target mapping allocations (Descriptor Set 1)
[[vk::binding(0, 1)]] Sampler2D   gBufferPosition;
[[vk::binding(1, 1)]] Sampler2D   gBufferNormal;
[[vk::binding(2, 1)]] Sampler2D   gBufferAlbedo;
[[vk::binding(3, 1)]] Sampler2D   gBufferEmissive;
[[vk::binding(4, 1)]] Sampler2D   shadowMap;
[[vk::binding(5, 1)]] Sampler2D   gBufferSurfaceExt;
[[vk::binding(6, 1)]] SamplerCube irradianceMap;
[[vk::binding(7, 1)]] SamplerCube prefilteredMap;
[[vk::binding(8, 1)]] Sampler2D   brdfLUT;
[[vk::binding(9, 1)]] Sampler2D   ssaoMap;

float CalculateShadow(int lightIdx, float3 worldPos, float3 N, float3 L)
{
    if (ubo.lightShadowEnabled[lightIdx] == 0) return 0.0;

    float4 fragPosLightSpace = mul(ubo.lightSpaceMatrices[lightIdx], float4(worldPos, 1.0));
    float3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;

    float2 localUV = projCoords.xy * 0.5 + 0.5;
    float currentDepth = projCoords.z;

    if (currentDepth > 1.0 || localUV.x < 0.0 || localUV.x > 1.0 || localUV.y < 0.0 || localUV.y > 1.0)
        return 0.0;

    // Remap [0, 1] tile UV into the 2x2 atlas quadrant for lightIdx (0..3)
    float2 tileOffset = float2(float(lightIdx % 2), float(lightIdx / 2)) * 0.5;
    float2 atlasUV = tileOffset + localUV * 0.5;

    float bias = max(0.005 * (1.0 - dot(N, L)), 0.0005);

    float shadow = 0.0;
    uint w, h;
    shadowMap.GetDimensions(w, h);
    float2 texelSize = 1.0 / float2(w, h);

    // Clamp PCF taps inside the light's tile so adjacent tiles never bleed across borders
    float2 minUV = tileOffset + texelSize;
    float2 maxUV = tileOffset + float2(0.5, 0.5) - texelSize;

    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float2 sampleUV = clamp(atlasUV + float2(x, y) * texelSize, minUV, maxUV);
            float pcfDepth = shadowMap.Sample(sampleUV).r;
            shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}

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

// Thin-film iridescence phase shift (approximates spectral interference across viewing angles)
float3 EvalIridescence(float cosTheta, float ior, float3 baseF0)
{
    float sinTheta2 = (1.0 - cosTheta * cosTheta) / (ior * ior);
    float cosThetaT = sqrt(max(1.0 - sinTheta2, 0.0));
    float opd = 2.0 * ior * 400.0 * cosThetaT; // Optical path difference (nm)
    float3 phase = 2.0 * PI * opd / float3(650.0, 510.0, 475.0); // R, G, B wavelengths
    float3 interference = 0.5 + 0.5 * cos(phase);
    return lerp(baseF0, interference, 0.65);
}

float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness) {
    return F0 + (max(float3(1.0 - roughness), F0) - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

[shader("fragment")]
float4 PSMain(VSOutput input) : SV_TARGET
{
    // 1. SAMPLE RAW ATTRIBUTES + PACKED PBR PROPERTIES FROM G-BUFFER
    float4 posSample    = gBufferPosition.Sample(input.UV);
    float4 normalSample = gBufferNormal.Sample(input.UV);
    float4 albedo       = gBufferAlbedo.Sample(input.UV);

    float3 worldPos  = posSample.xyz;
    float  metallic  = saturate(posSample.a);
    float3 N         = normalSample.xyz;
    float  roughness = clamp(normalSample.a, 0.04, 1.0);

    // Early out if pixel lands on background void -> sample 3D environment cubemap skybox
    if (dot(N, N) == 0.0) {
        float2 ndc = input.UV * 2.0 - 1.0;
        float3 forward = normalize(float3(-ubo.view[0][2], -ubo.view[1][2], -ubo.view[2][2]));
        float3 right = normalize(float3(ubo.view[0][0], ubo.view[1][0], ubo.view[2][0]));
        float3 up = normalize(float3(ubo.view[0][1], ubo.view[1][1], ubo.view[2][1]));
        float tanHalfFov = 1.0 / abs(ubo.proj[1][1]);
        float aspect = abs(ubo.proj[1][1] / ubo.proj[0][0]);
        float3 rayDir = normalize(forward + right * (ndc.x * aspect * tanHalfFov) - up * (ndc.y * tanHalfFov));
        float3 skyColor = prefilteredMap.SampleLevel(rayDir, 0.0).rgb * ubo.scaleIBLAmbient;
        return float4(skyColor, 1.0);
    }
    N = normalize(N);

    float3 V = normalize(ubo.camPos.xyz - worldPos);
    float3 F0 = lerp(float3(0.04), albedo.rgb, metallic);

    float3 Lo = float3(0.0);

    float4 surfaceExt        = gBufferSurfaceExt.Sample(input.UV);
    float  clearcoatFactor   = surfaceExt.r;
    float  clearcoatRoughness = clamp(surfaceExt.g, 0.04, 1.0);
    float  iridescenceFactor = surfaceExt.b;
    float  iridIor           = 1.0 + surfaceExt.a * 2.0;
    float3 emissive          = gBufferEmissive.Sample(input.UV).rgb;

    float NdotV = max(dot(N, V), 0.0);
    F0 = lerp(F0, EvalIridescence(NdotV, iridIor, F0), iridescenceFactor);

    // 2. DIRECT LIGHTING LOOP
    for (int i = 0; i < 4; i++) {
        float3 lightColor = ubo.lightColors[i].rgb;
        if (dot(lightColor, lightColor) == 0.0) continue;

        float3 L;
        float attenuation = 1.0;

        if (ubo.lightPositions[i].w == 0.0) {
            L = normalize(-ubo.lightPositions[i].xyz);
        } else {
            float3 toLight = ubo.lightPositions[i].xyz - worldPos;
            float distance = length(toLight);
            float radius = max(ubo.lightColors[i].a, 0.001);
            L = toLight / max(distance, 0.0001);

            float factor = saturate(1.0 - (distance / radius));
            attenuation = (factor * factor) / (1.0 + 0.1 * distance * distance);

            // Spot Light smooth cone falloff (w == 2.0)
            if (ubo.lightPositions[i].w > 1.5) {
                float theta = dot(L, normalize(-ubo.lightDirections[i].xyz));
                float innerCos = ubo.lightConeParams[i].x;
                float outerCos = ubo.lightConeParams[i].y;
                float spotIntensity = clamp((theta - outerCos) / max(innerCos - outerCos, 0.0001), 0.0, 1.0);
                attenuation *= spotIntensity;
            }
        }

        float3 radiance = lightColor * attenuation;
        float3 H = normalize(V + L);
        float NdotL = max(dot(N, L), 0.0);
        float NdotH = max(dot(N, H), 0.0);
        float HdotV = max(dot(H, V), 0.0);

        float D = DistributionGGX(NdotH, roughness);
        float G = GeometrySmith(NdotV, NdotL, roughness);
        float3 F = FresnelSchlick(HdotV, F0);

        float3 specular = (D * G * F) / (4.0 * NdotV * NdotL + 0.0001);
        float3 kD = (float3(1.0) - F) * (1.0 - metallic);

        float shadow = CalculateShadow(i, worldPos, N, L);
        Lo += (1.0 - shadow) * (kD * albedo.rgb / PI + specular) * radiance * NdotL;
    }

    // HEMISPHERE IBL & AUTOMOTIVE CLEARCOAT REFLECTION
    float3 R = reflect(-V, N);

    float3 F_env = FresnelSchlickRoughness(NdotV, F0, roughness);
    float3 kD_env = (1.0 - F_env) * (1.0 - metallic);

    //float3 irradiance = irradianceMap.Sample(N).rgb;
    float3 irradiance = irradianceMap.SampleLevel(N, 4.0).rgb;
    float3 diffuseAmbient = kD_env * irradiance * albedo.rgb;

    float mip = roughness * max(ubo.prefilteredCubeMipLevels - 1.0, 0.0);
    float3 prefilteredColor = prefilteredMap.SampleLevel(R, mip).rgb;
    float2 brdf = brdfLUT.Sample(float2(NdotV, roughness)).rg;
    float3 specularAmbient = prefilteredColor * (F_env * brdf.x + brdf.y);

    // Clearcoat IBL specular uses mip 0 (or clearcoatRoughness * maxMip) with F0 = 0.04
    float3 coatPrefiltered = prefilteredMap.SampleLevel(R, clearcoatRoughness * max(ubo.prefilteredCubeMipLevels - 1.0, 0.0)).rgb;
    float2 coatBrdf = brdfLUT.Sample(float2(NdotV, clearcoatRoughness)).rg;
    float3 clearcoatReflection = coatPrefiltered * (FresnelSchlick(NdotV, float3(0.04)) * coatBrdf.x + coatBrdf.y) * clearcoatFactor;

    float ssao = ssaoMap.Sample(input.UV).r;
    float3 color = (diffuseAmbient * ssao + specularAmbient * lerp(0.35, 1.0, ssao) + clearcoatReflection) * ubo.scaleIBLAmbient + Lo + emissive;

    return float4(color, albedo.a);
}