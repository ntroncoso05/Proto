// @entry: VSMain
// @entry: PSMain

// VolumetricFog.slang - Exponential Height Fog + Shadow-Sampled God-Rays / Sun Shafts
struct UniformBufferObject {
    float4x4 view;
    float4x4 proj;
    float4x4 lightSpaceMatrices[4];
    int4 lightShadowEnabled;
    float4 lightPositions[4];
    float4 lightColors[4];
    float4 lightDirections[4];
    float4 lightConeParams[4];
    float4 camPos;
    float exposure;
    float gamma;
    float prefilteredCubeMipLevels;
    float scaleIBLAmbient;
};

[[vk::binding(0, 0)]] ConstantBuffer<UniformBufferObject> ubo;

[[vk::binding(0, 1)]] Sampler2D hdrColorTex; // Lit HDR scene from LightingPass
[[vk::binding(1, 1)]] Sampler2D gBufferPosition; // World-space position (w > 0 if geometry hit)
[[vk::binding(2, 1)]] Sampler2D shadowAtlas; // 4096x4096 2x2 Shadow Atlas

struct FogPushConstants {
    float4x4 invViewProj;
    float4 fogColorAndDensity; // rgb = fog tint, a = fogDensity (0.0 = off)
    float4 scatterAndHeight;   // x = sunScatterIntensity, y = heightFalloff, z = baseHeight, w = maxFogDistance
};

[[vk::push_constant]] FogPushConstants pc;

struct VSOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VSOutput VSMain(uint vertexID: SV_VertexID) {
    VSOutput output;
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.uv = uv;
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

// Interleaved Gradient Noise to eliminate ray-march banding
float InterleavedGradientNoise(float2 pixel) {
    float3 magic = float3(0.06711056, 0.00583715, 52.9829189);
    return frac(magic.z * frac(dot(pixel, magic.xy)));
}

// Henyey-Greenstein forward scattering phase function (creates bright shafts when looking toward the sun)
float HenyeyGreenstein(float cosTheta, float g) {
    float g2 = g * g;
    float denom = max(1.0 + g2 - 2.0 * g * cosTheta, 0.001);
    return (1.0 - g2) / (4.0 * 3.14159265 * pow(denom, 1.5));
}

// Sample the 2x2 shadow atlas at any 3D ray-march point (returns 1.0 if lit, 0.0 if occluded)
float SampleRayShadow(int lightIndex, float3 worldPos) {
    if (ubo.lightShadowEnabled[lightIndex] == 0) return 1.0;

    float4 lightClip = mul(ubo.lightSpaceMatrices[lightIndex], float4(worldPos, 1.0));
    if (lightClip.w <= 0.0001) return 1.0;

    float3 ndc = lightClip.xyz / lightClip.w;
    float2 localUV = ndc.xy * 0.5 + 0.5;
    if (localUV.x < 0.0 || localUV.x > 1.0 || localUV.y < 0.0 || localUV.y > 1.0 || ndc.z < 0.0 || ndc.z > 1.0) {
        return 1.0;
    }

    float2 tileOffset = float2(float(lightIndex % 2) * 0.5, float(lightIndex / 2) * 0.5);
    float2 atlasUV = tileOffset + localUV * 0.5;
    float closestDepth = shadowAtlas.SampleLevel(atlasUV, 0).r;
    return (ndc.z - 0.002 <= closestDepth) ? 1.0 : 0.0;
}

[shader("fragment")]
float4 PSMain(VSOutput input) : SV_Target {
    float4 sceneColor = hdrColorTex.Sample(input.uv);
    float density = pc.fogColorAndDensity.a;
    if (density <= 0.0001) {
        return sceneColor;
    }

    float4 worldPosSample = gBufferPosition.Sample(input.uv);
    float3 camPos = ubo.camPos.xyz;
    float maxDist = pc.scatterAndHeight.w;

    float3 rayDir;
    float rayLength;
    bool hitGeometry = (length(worldPosSample.xyz) > 0.0001 || worldPosSample.w > 0.0);

    if (hitGeometry) {
        float3 toPixel = worldPosSample.xyz - camPos;
        rayLength = min(length(toPixel), maxDist);
        rayDir = normalize(toPixel);
    } else {
        // Reconstruct world ray for sky pixels
        float2 ndc = input.uv * 2.0 - 1.0;
        float4 farWorld = mul(pc.invViewProj, float4(ndc, 1.0, 1.0));
        farWorld /= farWorld.w;
        rayDir = normalize(farWorld.xyz - camPos);
        rayLength = maxDist;
    }

    const int STEPS = 24;
    float stepSize = rayLength / float(STEPS);
    float jitter = InterleavedGradientNoise(input.position.xy);

    float3 fogTint = pc.fogColorAndDensity.rgb;
    float scatterGain = pc.scatterAndHeight.x;
    float heightFalloff = pc.scatterAndHeight.y;
    float baseHeight = pc.scatterAndHeight.z;

    float transmittance = 1.0;
    float3 inScatteredLight = float3(0.0, 0.0, 0.0);

    // Precompute phase scattering for directional/spot lights
    float3 sunLightColor = float3(0.0, 0.0, 0.0);
    int primaryShadowLight = -1;
    for (int i = 0; i < 4; ++i) {
        float intensity = max(ubo.lightColors[i].r, max(ubo.lightColors[i].g, ubo.lightColors[i].b));
        if (intensity <= 0.001) continue;

        int lType = int(round(ubo.lightPositions[i].w));
        if (lType == 0) { // Directional
            float3 L = normalize(-ubo.lightPositions[i].xyz);
            float phase = lerp(0.25, HenyeyGreenstein(dot(rayDir, L), 0.65), 0.75);
            sunLightColor += ubo.lightColors[i].rgb * phase;
            if (primaryShadowLight < 0 && ubo.lightShadowEnabled[i] != 0) primaryShadowLight = i;
        }
    }

    for (int s = 0; s < STEPS; ++s) {
        float t = (float(s) + jitter) * stepSize;
        float3 samplePos = camPos + rayDir * t;

        // Exponential height falloff (thicker near ground/floor, thinner higher up)
        float heightFactor = exp(-heightFalloff * max(samplePos.y - baseHeight, 0.0));
        float localDensity = density * heightFactor;

        if (localDensity > 0.00001) {
            float visibility = 1.0;
            if (primaryShadowLight >= 0) {
                visibility = SampleRayShadow(primaryShadowLight, samplePos);
            }

            float3 ambientFog = fogTint * ubo.scaleIBLAmbient * 0.65;
            float3 shaftLight = sunLightColor * scatterGain * visibility;
            float3 stepLuminance = (ambientFog + shaftLight) * localDensity;

            float stepTransmittance = exp(-localDensity * stepSize);
            inScatteredLight += transmittance * stepLuminance * ((1.0 - stepTransmittance) / max(localDensity, 0.0001));
            transmittance *= stepTransmittance;
        }
    }

    float3 finalRGB = sceneColor.rgb * transmittance + inScatteredLight;
    return float4(finalRGB, sceneColor.a);
}