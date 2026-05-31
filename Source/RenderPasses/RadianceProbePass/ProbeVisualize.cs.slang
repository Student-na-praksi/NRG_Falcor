/***************************************************************************
 # Copyright (c) 2015-24, NVIDIA CORPORATION. All rights reserved.
 **************************************************************************/
#include "Scene/SceneDefines.slangh"

import Scene.Scene;
import Utils.Math.Ray;

#ifndef INPUT_VALID
#define INPUT_VALID 0
#endif

#ifndef POSW_VALID
#define POSW_VALID 0
#endif

cbuffer CB
{
    uint2 gFrameDim;
    uint3 gGridSize;
    float3 gGridMin;
    float3 gGridSpacing;
    float gProbeRadius;
    float gProbeContributionScale;
    uint gVisualizeProbes;
}

Texture2D<float4> gInput;
Texture2D<float4> gPosW;
Texture3D<float4> gProbeRadiance;
RWTexture2D<float4> gOutput;

static const uint kProbeVizScale = 16;

float3 getProbePosition(uint3 coord)
{
    return gGridMin + float3(coord) * gGridSpacing;
}

float rayPointDistance(float3 rayOrigin, float3 rayDir, float3 point, out float t)
{
    const float3 v = point - rayOrigin;
    t = dot(v, rayDir);
    const float3 closest = rayOrigin + max(t, 0.f) * rayDir;
    return length(point - closest);
}

float3 sampleProbeRadiance(float3 posW)
{
    float3 coord = (posW - gGridMin) / gGridSpacing;
    const uint3 dimsMinusOneU = max(gGridSize, uint3(1, 1, 1)) - uint3(1, 1, 1);
    coord = clamp(coord, float3(0.f), float3(dimsMinusOneU));

    const uint3 base = uint3(coord);
    const float3 frac = coord - float3(base);
    const uint3 maxCoord = gGridSize - 1;
    const uint3 c000 = min(base, maxCoord);
    const uint3 c100 = min(base + uint3(1, 0, 0), maxCoord);
    const uint3 c010 = min(base + uint3(0, 1, 0), maxCoord);
    const uint3 c110 = min(base + uint3(1, 1, 0), maxCoord);
    const uint3 c001 = min(base + uint3(0, 0, 1), maxCoord);
    const uint3 c101 = min(base + uint3(1, 0, 1), maxCoord);
    const uint3 c011 = min(base + uint3(0, 1, 1), maxCoord);
    const uint3 c111 = min(base + uint3(1, 1, 1), maxCoord);

    const float3 r00 = lerp(gProbeRadiance.Load(int4(c000, 0)).rgb, gProbeRadiance.Load(int4(c100, 0)).rgb, frac.x);
    const float3 r10 = lerp(gProbeRadiance.Load(int4(c010, 0)).rgb, gProbeRadiance.Load(int4(c110, 0)).rgb, frac.x);
    const float3 r01 = lerp(gProbeRadiance.Load(int4(c001, 0)).rgb, gProbeRadiance.Load(int4(c101, 0)).rgb, frac.x);
    const float3 r11 = lerp(gProbeRadiance.Load(int4(c011, 0)).rgb, gProbeRadiance.Load(int4(c111, 0)).rgb, frac.x);
    const float3 r0 = lerp(r00, r10, frac.y);
    const float3 r1 = lerp(r01, r11, frac.y);
    return lerp(r0, r1, frac.z);
}

float3 probeToDisplayColor(float3 probeColor)
{
    return pow(probeColor / (probeColor + 1.f), 1.f / 2.2f);
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    if (any(pixel >= gFrameDim))
        return;

#if INPUT_VALID
    float3 color = gInput[pixel].rgb;
#else
    float3 color = float3(0.f);
#endif

    if (gVisualizeProbes != 0)
    {
        const uint sliceWidth = gGridSize.x * kProbeVizScale;
        const uint sliceHeight = gGridSize.y * kProbeVizScale;
        const uint atlasHeight = sliceHeight * gGridSize.z;

        if (pixel.x >= sliceWidth || pixel.y >= atlasHeight)
        {
            gOutput[pixel] = float4(0.f, 0.f, 0.f, 1.f);
            return;
        }

        const uint3 probeCoord = uint3(
            min(pixel.x / kProbeVizScale, gGridSize.x - 1),
            min((pixel.y % sliceHeight) / kProbeVizScale, gGridSize.y - 1),
            min(pixel.y / sliceHeight, gGridSize.z - 1)
        );

        float3 probeColor = gProbeRadiance.Load(int4(probeCoord, 0)).rgb;
        color = probeToDisplayColor(probeColor);

        const bool onVerticalLine = (pixel.x % kProbeVizScale) == 0;
        const bool onHorizontalLine = (pixel.y % kProbeVizScale) == 0;
        if (onVerticalLine || onHorizontalLine)
            color *= 0.35f;
    }
    else
    {
    #if POSW_VALID
        const float3 posW = gPosW[pixel].xyz;
        if (dot(posW, posW) > 1e-8f)
            color = color + gProbeContributionScale * sampleProbeRadiance(posW);
    #else
        const float2 uv = (float2(pixel) + 0.5f) / float2(gFrameDim);
        const uint z = min(uint(uv.y * gGridSize.z), gGridSize.z - 1);
        const uint x = min(uint(uv.x * gGridSize.x), gGridSize.x - 1);
        const uint y = gGridSize.y / 2;
        color = pow(gProbeRadiance.Load(int4(x, y, z, 0)).rgb / (gProbeRadiance.Load(int4(x, y, z, 0)).rgb + 1.f), 1.f / 2.2f);
    #endif
    }

    gOutput[pixel] = float4(color, 1.f);
}
