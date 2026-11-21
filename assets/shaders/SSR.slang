// @entry: VSMain
// @entry: PSMain

struct VSInput {
    uint VertexID : SV_VertexID;
};

struct VSOutput {
    float4 Position : SV_POSITION;
    float2 UV       : TEXCOORD0;
};

[shader("vertex")]
VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.UV = float2((input.VertexID << 1) & 2, input.VertexID & 2);
    output.Position = float4(output.UV * float2(2.0, 2.0) + float2(-1.0, -1.0), 0.0, 1.0);
    return output;
}

struct UniformBufferObject {
    float4x4 view;
    float4x4 proj;
    float4x4 lightSpaceMatrices[4];
    int4     lightShadowEnabled;
    float4   lightPositions[4];
    float4   lightColors[4];
    float4   lightDirections[4];
    float4   lightConeParams[4];
    float4   camPos;
    float    exposure;
    float    gamma;
    float    prefilteredCubeMipLevels;
    float    scaleIBLAmbient;
};

struct SSRPushConstants {
    float maxDistance;
    float stepSize;
    float thickness;
    float ssrIntensity;
};

[[vk::binding(0, 0)]] ConstantBuffer<UniformBufferObject> ubo;
[[vk::binding(0, 1)]] Sampler2D hdrColorMap;
[[vk::binding(1, 1)]] Sampler2D gBufferPosition;
[[vk::binding(2, 1)]] Sampler2D gBufferNormal;
[[vk::binding(3, 1)]] Sampler2D gBufferAlbedo;
[[vk::push_constant]] SSRPushConstants pc;

float2 ProjectToScreenUV(float3 viewPos)
{
    float4 clip = mul(ubo.proj, float4(viewPos, 1.0));
    float2 ndc  = clip.xy / max(clip.w, 0.0001);
    return ndc * 0.5 + 0.5;
}

[shader("fragment")]
float4 PSMain(VSOutput input) : SV_TARGET
{
    float4 baseHDR      = hdrColorMap.Sample(input.UV);
    float4 posSample    = gBufferPosition.Sample(input.UV);
    float4 normalSample = gBufferNormal.Sample(input.UV);

    // Skip sky/background pixels
    if (dot(normalSample.xyz, normalSample.xyz) == 0.0 || pc.ssrIntensity <= 0.001) {
        return baseHDR;
    }

    float  metallic  = saturate(posSample.a);
    float  roughness = clamp(normalSample.a, 0.04, 1.0);

    // Skip very rough matte surfaces (> 0.65 roughness) for crisp performance & realism
    float roughnessFade = saturate(1.0 - (roughness / 0.65));
    if (roughnessFade <= 0.001) {
        return baseHDR;
    }

    float3 worldPos = posSample.xyz;
    float3 worldN   = normalize(normalSample.xyz);

    // Transform position & normal to View Space (camera looks down -Z)
    float3 viewPos = mul(ubo.view, float4(worldPos, 1.0)).xyz;
    float3 viewN   = normalize(mul((float3x3)ubo.view, worldN));
    float3 viewV   = normalize(-viewPos);
    float3 viewR   = normalize(reflect(-viewV, viewN));

    // Fade out rays pointing back toward the camera (cannot intersect anything behind the eye)
    float backFacingFade = saturate(1.0 - max(viewR.z, 0.0) * 2.0);
    if (backFacingFade <= 0.001) {
        return baseHDR;
    }

    // Ray-march in View Space
    // Interleaved gradient noise per pixel to eliminate horizontal ray-step banding
    float jitter = frac(52.9829189 * frac(dot(input.Position.xy, float2(0.06711056, 0.00583715))));

    const int MAX_STEPS = 72;
    float stepLen = 0.16;
    float bias = max(0.03, -viewPos.z * 0.004);
    float3 rayOrigin = viewPos + viewN * bias;
    float3 rayStep = viewR * stepLen;
    float3 currPos = rayOrigin + rayStep * jitter;

    float2 hitUV = float2(-1.0, -1.0);
    float hitAlpha = 0.0;

    for (int i = 0; i < MAX_STEPS; ++i) {
        currPos += rayStep;
        float traveled = length(currPos - rayOrigin);
        if (-currPos.z <= 0.1 || traveled > pc.maxDistance) break;

        float2 sampleUV = ProjectToScreenUV(currPos);
        if (sampleUV.x < 0.001 || sampleUV.x > 0.999 || sampleUV.y < 0.001 || sampleUV.y > 0.999) break;

        float4 sampleWorldPos = gBufferPosition.SampleLevel(sampleUV, 0.0);
        float4 sampleNormal = gBufferNormal.SampleLevel(sampleUV, 0.0);
        if (dot(sampleNormal.xyz, sampleNormal.xyz) == 0.0) continue; // Ignore sky void

        // Reject coplanar hits (prevents flat floor from reflecting itself into shadows)
        float3 sampleViewN = normalize(mul((float3x3)ubo.view, normalize(sampleNormal.xyz)));
        if (dot(viewN, sampleViewN) > 0.92) continue;

        float sceneViewZ = mul(ubo.view, float4(sampleWorldPos.xyz, 1.0)).z;
        float depthDiff = sceneViewZ - currPos.z;

        if (depthDiff > 0.025 && depthDiff < pc.thickness) {
            // Binary search refinement (6 steps)
            float3 lo = currPos - rayStep;
            float3 hi = currPos;
            for (int b = 0; b < 6; ++b) {
                float3 mid = 0.5 * (lo + hi);
                float2 midUV = ProjectToScreenUV(mid);
                float midSceneZ = mul(ubo.view, float4(gBufferPosition.SampleLevel(midUV, 0.0).xyz, 1.0)).z;
                if (midSceneZ - mid.z > 0.025) hi = mid;
                else lo = mid;
            }

            hitUV = ProjectToScreenUV(0.5 * (lo + hi));

            // Verify final refined hit actually landed on geometry (not sky background!)
            if (dot(gBufferNormal.SampleLevel(hitUV, 0.0).xyz, gBufferNormal.SampleLevel(hitUV, 0.0).xyz) == 0.0) {
                hitAlpha = 0.0;
                break;
            }

            float2 edgeDist = min(hitUV, 1.0 - hitUV);
            float edgeFade = smoothstep(0.0, 0.08, min(edgeDist.x, edgeDist.y));
            float distFade = saturate(1.0 - traveled / pc.maxDistance);
            hitAlpha = edgeFade * distFade;
            break;
        }
    }

    if (hitAlpha <= 0.001) {
        return baseHDR;
    }

    float3 albedo = gBufferAlbedo.Sample(input.UV).rgb;
    float3 F0 = lerp(float3(0.08), albedo, metallic);
    float NdotV = max(dot(viewN, viewV), 0.0);
    float3 fresnel = F0 + (1.0 - F0) * pow(1.0 - NdotV, 4.0);
    fresnel = max(fresnel, float3(0.18) * roughnessFade); // Keep showroom reflections clearly visible even at steeper angles

    float3 reflectedColor = hdrColorMap.SampleLevel(hitUV, 0.0).rgb;
    float3 ssrContrib = reflectedColor * fresnel * (hitAlpha * roughnessFade * backFacingFade * pc.ssrIntensity);

    return float4(baseHDR.rgb + ssrContrib, baseHDR.a);
}