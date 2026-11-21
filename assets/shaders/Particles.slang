// @entry: CSMain
// @entry: VSMain
// @entry: PSMain

struct Particle {
    float4 position; // xyz = world pos, w = life remaining (seconds)
    float4 velocity; // xyz = velocity, w = maxLife
    float4 color;    // rgba
};

struct ParticleParams {
    float4 emitterPos;  // xyz = world spawn origin, w = deltaTime
    float4 emitterDir;  // xyz = world emit direction (normalized), w = spread
    float4 particleCol; // rgb = color * HDRIntensity, a = gravity
    float4 lifeAndSize; // x = minLife, y = maxLife, z = initialSpeed, w = particleSize
    float4 camRight;    // xyz = camera right, w = elapsedTime
    float4 camUp;       // xyz = camera up, w = activeParticleCount (float)
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
[[vk::binding(0, 1)]] RWStructuredBuffer<Particle> particlesOut;
[[vk::binding(1, 1)]] StructuredBuffer<Particle> particlesIn;
[[vk::push_constant]] ParticleParams params;

float Hash(uint n) {
    n = (n << 13U) ^ n;
    n = n * (n * n * 15731U + 789221U) + 1376312589U;
    return float(n & 0x7fffffffU) / float(0x7fffffff);
}

// 1. COMPUTE SHADER: Integrate velocity, gravity & respawn expired particles
[shader("compute")]
[numthreads(256, 1, 1)]
void CSMain(uint3 dispatchID : SV_DispatchThreadID)
{
    uint idx = dispatchID.x;
    Particle p = particlesOut[idx];

    // Park particles beyond ActiveParticles off-screen with zero alpha
    if (float(idx) >= params.camUp.w) {
        p.position.w = 0.0;
        p.color.a = 0.0;
        particlesOut[idx] = p;
        return;
    }

    float dt = params.emitterPos.w;
    p.position.w -= dt;

    if (p.position.w <= 0.0) {
        // Respawn at emitter with pseudo-random upward/outward burst
        float r1 = Hash(idx * 3u + uint(params.camUp.w * 1000.0));
        float r2 = Hash(idx * 7u + 19u);
        float r3 = Hash(idx * 13u + 47u);

        float angle = r1 * 6.2831853;
        float spread = params.emitterDir.w;
        float3 randJitter = float3(cos(angle), sin(angle * 1.7), sin(angle)) * spread;
        float3 launchDir = normalize(params.emitterDir.xyz + randJitter);
        float speed = params.lifeAndSize.z * (0.6 + r2 * 0.8);

        p.position.xyz = params.emitterPos.xyz + randJitter * 0.15;
        p.velocity.xyz = launchDir * speed;
        p.velocity.w = lerp(params.lifeAndSize.x, params.lifeAndSize.y, r2); // maxLife
        p.position.w = p.velocity.w;
        p.color = float4(params.particleCol.rgb, 1.0); // HDR warm spark color (>1.0 triggers Bloom!)
    } else {
        p.velocity.y += params.particleCol.a * dt; // Configurable gravity
        p.position.xyz += p.velocity.xyz * dt;
        float lifeRatio = saturate(p.position.w / max(p.velocity.w, 0.001));
        p.color.a = lifeRatio;
    }

    particlesOut[idx] = p;
}

// 2. GRAPHICS BILLBOARD VERTEX SHADER (6 vertices per particle quad via SV_VertexID)
struct VSOutput {
    float4 Position : SV_POSITION;
    float2 UV       : TEXCOORD0;
    float4 Color    : COLOR;
};

[shader("vertex")]
VSOutput VSMain(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    const float2 quadOffsets[6] = {
        float2(-0.5, -0.5), float2( 0.5, -0.5), float2( 0.5,  0.5),
        float2( 0.5,  0.5), float2(-0.5,  0.5), float2(-0.5, -0.5)
    };

    Particle p = particlesIn[instanceID];
    float2 corner = quadOffsets[vertexID];
    float size = params.lifeAndSize.w * p.color.a;

    float3 worldPos = p.position.xyz
        + params.camRight.xyz * (corner.x * size)
        + params.camUp.xyz    * (corner.y * size);

    VSOutput output;
    output.Position = mul(ubo.proj, mul(ubo.view, float4(worldPos, 1.0)));
    output.UV       = corner + 0.5;
    output.Color    = p.color;
    return output;
}

// 3. SOFT RADIAL GLOW FRAGMENT SHADER
[shader("fragment")]
float4 PSMain(VSOutput input) : SV_TARGET
{
    float dist = length(input.UV - 0.5) * 2.0;
    float radial = saturate(1.0 - dist * dist);
    return float4(input.Color.rgb * radial, input.Color.a * radial);
}