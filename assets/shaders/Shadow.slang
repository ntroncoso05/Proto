// @entry: VSMain
struct VSInput {
    [[vk::location(0)]] float3 Position : POSITION;
};

struct ShadowPushConstants {
    float4x4 lightSpaceMatrix;
    float4x4 modelMatrix;
};

[[vk::push_constant]] ShadowPushConstants pc;

[shader("vertex")]
float4 VSMain(VSInput input) : SV_POSITION
{
    return mul(pc.lightSpaceMatrix, mul(pc.modelMatrix, float4(input.Position, 1.0)));
}

// (No fragment shader is needed—Vulkan writes hardware depth automatically to the eD32Sfloat shadow map!).
[shader("fragment")]
void PSMain()
{
    
}