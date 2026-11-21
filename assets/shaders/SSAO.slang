// @entry: VSMain
// @entry: PSSSAO
// @entry: PSBlur

struct VSInput {
    uint VertexID : SV_VertexID;
};

struct VSOutput {
    float4 Position : SV_POSITION;
    float2 UV       : TEXCOORD0;
};

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

[[vk::binding(0, 1)]] Sampler2D gBufferPosition; // World position
[[vk::binding(1, 1)]] Sampler2D gBufferNormal;   // World normal (or SSAO_Raw in PSBlur)

[shader("vertex")]
VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.UV = float2((input.VertexID << 1) & 2, input.VertexID & 2);
    output.Position = float4(output.UV * float2(2.0, 2.0) + float2(-1.0, -1.0), 0.0, 1.0);
    return output;
}

float Hash12(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float3 Hash32(float2 p)
{
    return float3(Hash12(p), Hash12(p + 17.3), Hash12(p + 41.9));
}

[shader("fragment")]
float4 PSSSAO(VSOutput input) : SV_TARGET
{
    float4 normalSample = gBufferNormal.Sample(input.UV);
    if (dot(normalSample.xyz, normalSample.xyz) == 0.0) {
        return float4(1.0, 1.0, 1.0, 1.0); // Skybox has no occlusion
    }

    float3 worldPos = gBufferPosition.Sample(input.UV).xyz;
    float3 fragPos  = mul(ubo.view, float4(worldPos, 1.0)).xyz;
    float3 normal   = normalize(mul((float3x3)ubo.view, normalize(normalSample.xyz)));

    uint w, h;
    gBufferPosition.GetDimensions(w, h);
    float2 pixelCoord = input.UV * float2(w, h);

    // Build random tangent-space TBN rotated per pixel
    float3 randVec = normalize(float3(Hash12(pixelCoord) * 2.0 - 1.0, Hash12(pixelCoord + 5.1) * 2.0 - 1.0, 0.0));
    float3 tangent = normalize(randVec - normal * dot(randVec, normal));
    float3 bitangent = cross(normal, tangent);
    float3x3 TBN = float3x3(tangent, bitangent, normal);

    const int sampleCount = 16;
    const float radius = 0.45;
    const float bias = 0.025;
    float occlusion = 0.0;

    for (int i = 0; i < sampleCount; ++i) {
        float fi = float(i + 1) / float(sampleCount);
        float3 rnd = Hash32(float2(float(i) * 13.7, float(i) * 29.3));
        float3 hemisphereDir = normalize(float3(rnd.x * 2.0 - 1.0, rnd.y * 2.0 - 1.0, max(rnd.z, 0.15)));
        float scale = lerp(0.1, 1.0, fi * fi);
        float3 sampleView = fragPos + mul(hemisphereDir * scale * radius, TBN);

        // Project sample to screen UV
        float4 offset = mul(ubo.proj, float4(sampleView, 1.0));
        offset.xyz /= offset.w;
        float2 sampleUV = offset.xy * 0.5 + 0.5;

        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0) continue;

        float4 sampleWorld = gBufferPosition.Sample(sampleUV);
        float4 sampleNorm  = gBufferNormal.Sample(sampleUV);
        if (dot(sampleNorm.xyz, sampleNorm.xyz) == 0.0) continue;

        float sampleDepth = mul(ubo.view, float4(sampleWorld.xyz, 1.0)).z;
        float rangeCheck = smoothstep(0.0, 1.0, radius / max(abs(fragPos.z - sampleDepth), 0.0001));
        occlusion += (sampleDepth >= sampleView.z + bias ? 1.0 : 0.0) * rangeCheck;
    }

    float ao = 1.0 - (occlusion / float(sampleCount));
    return float4(ao, ao, ao, 1.0);
}

[shader("fragment")]
float4 PSBlur(VSOutput input) : SV_TARGET
{
    uint w, h;
    gBufferPosition.GetDimensions(w, h);
    float2 texelSize = 1.0 / float2(w, h);

    float result = 0.0;
    for (int x = -2; x < 2; ++x) {
        for (int y = -2; y < 2; ++y) {
            float2 offset = float2(float(x), float(y)) * texelSize;
            result += gBufferPosition.Sample(input.UV + offset).r;
        }
    }
    float blurred = result / 16.0;
    return float4(blurred, blurred, blurred, 1.0);
}