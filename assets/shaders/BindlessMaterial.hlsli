struct GPUMaterialData
{
    float4 baseColorFactor;
    float4 emissiveFactor;          // xyz = color, w = emissiveStrength
    float4 coatAndIridescence;      // x = clearcoat, y = clearcoatRoughness, z = iridescence, w = iridescenceIor
    float4 metallicRoughnessAlpha;  // x = metallic, y = roughness, z = alphaMask, w = alphaCutoff
    int4   textureIndices0;         // x = baseColorTex, y = metallicRoughnessTex, z = normalTex, w = occlusionTex
    int4   textureIndices1;         // x = emissiveTex, yzw = reserved
};

struct PushConstants
{
    float4x4 modelMatrix;
    uint4    materialData; // x = materialIndex, yzw = padding (strictly 80 bytes total!)
};

// Global Set 1: Bindless Texture Array (Binding 0) + Material SSBO (Binding 1)
[[vk::binding(0, 1)]] Sampler2D g_BindlessTextures[];
[[vk::binding(1, 1)]] StructuredBuffer<GPUMaterialData> g_Materials;

[[vk::push_constant]] PushConstants pushConst;

float4 SampleBindless(int texIndex, float2 uv, float4 fallback)
{
    if (texIndex < 0)
        return fallback;
    return g_BindlessTextures[NonUniformResourceIndex(texIndex)].Sample(uv);
}