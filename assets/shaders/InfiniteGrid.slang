// @entry: VSMain
// @entry: PSMain

struct GridPushConstants
{
    float4x4 invViewProj;
    float4x4 viewProj;
    float4   camPosAndPlaneY;   // xyz = cameraPos, w = gridPlaneY (default 0.0)
    float4   unitParams;        // x = unitScaleMeters (e.g. 1.0 = m, 0.0254 = in, 0.3048 = ft, 0.001 = mm)
                                // y = subdivisions (10 for metric, 12 for feet/inches)
                                // z = fadeDistance, w = gridOpacity
    float4   minorColor;
    float4   majorColor;
};

[[vk::push_constant]]
ConstantBuffer<GridPushConstants> pc;

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

[shader("vertex")]
VSOut VSMain(uint vertexID : SV_VertexID)
{
    VSOut o;
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    o.uv  = uv;
    o.pos = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return o;
}

float ComputeGridLine(float2 worldXZ, float spacing, float lineWidth)
{
    float2 coord = worldXZ / spacing;
    float2 derivative = fwidth(coord);
    float2 grid = abs(frac(coord - 0.5) - 0.5) / max(derivative, float2(1e-5, 1e-5));
    float line = min(grid.x, grid.y);
    return 1.0 - min(line / lineWidth, 1.0);
}

struct PSOut
{
    float4 color : SV_Target0;
    float  depth : SV_Depth;
};

[shader("fragment")]
PSOut PSMain(VSOut input)
{
    PSOut o;

    float2 ndc = input.uv * 2.0 - 1.0;
    float4 nearH = mul(pc.invViewProj, float4(ndc, 0.0, 1.0));
    float4 farH  = mul(pc.invViewProj, float4(ndc, 1.0, 1.0));
    float3 nearP = nearH.xyz / nearH.w;
    float3 farP  = farH.xyz  / farH.w;

    float3 rayDir = farP - nearP;
    float planeY = pc.camPosAndPlaneY.w;

    // Intersect camera ray with horizontal plane Y = planeY
    if (abs(rayDir.y) < 1e-6)
        discard;

    float t = (planeY - nearP.y) / rayDir.y;
    if (t <= 0.0 || t > 1.0)
        discard;

    float3 worldPos = nearP + t * rayDir;

    // Compute exact Vulkan NDC depth [0, 1] so 3D geometry depth buffer occludes the grid cleanly
    float4 clipPos = mul(pc.viewProj, float4(worldPos, 1.0));
    float ndcDepth = clipPos.z / clipPos.w;
    if (ndcDepth < 0.0 || ndcDepth > 1.0)
        discard;
    o.depth = ndcDepth;

    // Unit-aware spacing: minor = 1 unit (e.g., 1 m or 1 in), major = subdivisions * unit (e.g., 10 m or 12 in = 1 ft)
    float minorSpacing = max(pc.unitParams.x, 1e-4);
    float subdiv       = max(pc.unitParams.y, 1.0);
    float majorSpacing = minorSpacing * subdiv;

    float minorAlpha = ComputeGridLine(worldPos.xz, minorSpacing, 1.0) * pc.minorColor.a;
    float majorAlpha = ComputeGridLine(worldPos.xz, majorSpacing, 1.35) * pc.majorColor.a;

    float3 col = lerp(pc.minorColor.rgb, pc.majorColor.rgb, step(minorAlpha, majorAlpha));
    float alpha = max(minorAlpha, majorAlpha);

    // Highlight World X axis (Z == 0 -> Red) and World Z axis (X == 0 -> Blue)
    float2 derivXZ = max(fwidth(worldPos.xz), float2(1e-5, 1e-5));
    float xAxis = 1.0 - min(abs(worldPos.z) / (derivXZ.y * 1.8), 1.0);
    float zAxis = 1.0 - min(abs(worldPos.x) / (derivXZ.x * 1.8), 1.0);

    if (xAxis > 0.01) {
        col = lerp(col, float3(0.92, 0.25, 0.25), xAxis);
        alpha = max(alpha, xAxis * 0.95);
    }
    if (zAxis > 0.01) {
        col = lerp(col, float3(0.25, 0.52, 0.96), zAxis);
        alpha = max(alpha, zAxis * 0.95);
    }

    // Distance fade from camera
    float dist = length(worldPos.xz - pc.camPosAndPlaneY.xz);
    float fade = 1.0 - smoothstep(pc.unitParams.z * 0.35, pc.unitParams.z, dist);
    alpha *= fade * pc.unitParams.w;

    if (alpha < 0.005)
        discard;

    o.color = float4(col, alpha);
    return o;
}