// @entry: vertexMain
// @entry: fragmentMain

// Structural blocks for input/output communication
struct VertexInput
{
    [[vk::location(0)]] float2 pos   : POSITION;
    [[vk::location(1)]] float2 uv    : TEXCOORD0;
    [[vk::location(2)]] float4 color : COLOR0;
};

struct VertexStageOutput
{
    float2 uv    : TEXCOORD0;
    float4 color : COLOR1;
    float4 pos   : SV_Position;
};

// Push Constants
struct PushConstants
{
    float2 scale;
    float2 translate;
};
[[vk::push_constant]] PushConstants pc;

// Fragment Texture Binding
//[[vk::binding(0, 0)]] Texture2D    sTexture;
[[vk::binding(0, 0)]] Sampler2D sTexture;
//[[vk::binding(0, 0)]] SamplerState sSampler;

// --- Vertex Shader ---
[shader("vertex")]
VertexStageOutput vertexMain(VertexInput input)
{
    VertexStageOutput output;
    output.uv    = input.uv;
    output.color = input.color;
    output.pos   = float4(input.pos * pc.scale + pc.translate, 0.0, 1.0);
    return output;
}

// --- Fragment Shader ---
[shader("fragment")]
float4 fragmentMain(VertexStageOutput input) : SV_Target
{
    //return input.color * sTexture.Sample(sSampler, input.uv);
    return input.color * sTexture.Sample(input.uv);
}