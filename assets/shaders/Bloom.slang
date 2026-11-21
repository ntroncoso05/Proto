// @entry: VSMain
// @entry: PSBrightExtract
// @entry: PSBlur
// @entry: PSComposite

struct VSInput {
    uint VertexID : SV_VertexID;
};

struct VSOutput {
    float4 Position : SV_POSITION;
    float2 UV       : TEXCOORD0;
};

struct BloomPushConstants {
    float2 direction;      // (1,0) for horizontal blur, (0,1) for vertical blur
    float  threshold;      // Brightness cutoff (default 1.0)
    float  bloomIntensity; // Final bloom blend strength
};

[[vk::push_constant]] BloomPushConstants pc;

[[vk::binding(0, 0)]] Sampler2D inputTex0; // HDRColor (or BloomPing/Pong)
[[vk::binding(1, 0)]] Sampler2D inputTex1; // BloomPing (used in PSComposite)

[shader("vertex")]
VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.UV = float2((input.VertexID << 1) & 2, input.VertexID & 2);
    output.Position = float4(output.UV * float2(2.0, 2.0) + float2(-1.0, -1.0), 0.0, 1.0);
    return output;
}

// Pass 1: Extract pixels brighter than pc.threshold with a soft knee
[shader("fragment")]
float4 PSBrightExtract(VSOutput input) : SV_TARGET
{
    float3 hdr = inputTex0.Sample(input.UV).rgb;
    float brightness = max(hdr.r, max(hdr.g, hdr.b));
    float soft = clamp(brightness - pc.threshold + 0.5, 0.0, 1.0);
    soft = (soft * soft) / 2.0;
    float contribution = max(brightness - pc.threshold, soft) / max(brightness, 0.0001);
    return float4(hdr * max(contribution, 0.0), 1.0);
}

// Pass 2 & 3: 9-tap separable Gaussian blur
[shader("fragment")]
float4 PSBlur(VSOutput input) : SV_TARGET
{
    static const float weights[5] = { 0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216 };

    uint w, h;
    inputTex0.GetDimensions(w, h);
    float2 texelSize = 1.0 / float2(w, h);

    float3 result = inputTex0.Sample(input.UV).rgb * weights[0];
    for (int i = 1; i < 5; ++i) {
        float2 offset = pc.direction * texelSize * float(i);
        result += inputTex0.Sample(input.UV + offset).rgb * weights[i];
        result += inputTex0.Sample(input.UV - offset).rgb * weights[i];
    }
    return float4(result, 1.0);
}

// ACES Filmic Tone Mapping
float3 ACESFilm(float3 x)
{
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

// Pass 4: Combine HDR scene + blurred bloom, then apply Reinhard tone mapping
[shader("fragment")]
float4 PSComposite(VSOutput input) : SV_TARGET
{
    float3 hdr   = inputTex0.Sample(input.UV).rgb;
    float3 bloom = inputTex1.Sample(input.UV).rgb;

    float3 color = hdr + bloom * pc.bloomIntensity;
    //color = color / (color + float3(1.0)); // Reinhard tone mapping (FinalColor is Srgb)
    color = ACESFilm(color);

    return float4(color, 1.0);
}