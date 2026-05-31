/***************************************************************************
 # Copyright (c) 2015-24, NVIDIA CORPORATION. All rights reserved.
 **************************************************************************/
#include "Scene/SceneDefines.slangh"

import Scene.RaytracingInline;
import Utils.Math.MathHelpers;
import Utils.Math.Ray;

cbuffer CB
{
    uint3 gGridSize;
    float3 gGridMin;
    float3 gGridSpacing;
    float gRayBias;
    float gMaxRayDistance;
    uint gRaysPerProbe;
}

RWTexture3D<float4> gProbeRadiance;

float3 getProbePosition(uint3 coord)
{
    return gGridMin + float3(coord) * gGridSpacing;
}

float3 getProbeDirection(uint index)
{
    switch (index % 12)
    {
    case 0: return float3(1, 0, 0);
    case 1: return float3(-1, 0, 0);
    case 2: return float3(0, 1, 0);
    case 3: return float3(0, -1, 0);
    case 4: return float3(0, 0, 1);
    case 5: return float3(0, 0, -1);
    case 6: return normalize(float3(1, 1, 1));
    case 7: return normalize(float3(-1, 1, 1));
    case 8: return normalize(float3(1, -1, 1));
    case 9: return normalize(float3(1, 1, -1));
    case 10: return normalize(float3(-1, -1, 1));
    default: return normalize(float3(-1, 1, -1));
    }
}

ShadingData loadShadingData(const HitInfo hit, const float3 rayOrigin, const float3 rayDir)
{
    VertexData v = {};
    uint materialID = {};

#if SCENE_HAS_GEOMETRY_TYPE(GEOMETRY_TYPE_TRIANGLE_MESH)
    if (hit.getType() == HitType::Triangle)
    {
        const TriangleHit triangleHit = hit.getTriangleHit();
        v = gScene.getVertexData(triangleHit);
        materialID = gScene.getMaterialID(triangleHit.instanceID);
    }
#endif
#if SCENE_HAS_GEOMETRY_TYPE(GEOMETRY_TYPE_DISPLACED_TRIANGLE_MESH)
    if (hit.getType() == HitType::DisplacedTriangle)
    {
        const DisplacedTriangleHit displacedTriangleHit = hit.getDisplacedTriangleHit();
        v = gScene.getVertexData(displacedTriangleHit, -rayDir);
        materialID = gScene.getMaterialID(displacedTriangleHit.instanceID);
    }
#endif
#if SCENE_HAS_GEOMETRY_TYPE(GEOMETRY_TYPE_CURVE)
    if (hit.getType() == HitType::Curve)
    {
        const CurveHit curveHit = hit.getCurveHit();
        v = gScene.getVertexDataFromCurve(curveHit);
        materialID = gScene.getMaterialID(curveHit.instanceID);
    }
#endif
#if SCENE_HAS_GEOMETRY_TYPE(GEOMETRY_TYPE_SDF_GRID)
    if (hit.getType() == HitType::SDFGrid)
    {
        const SDFGridHit sdfGridHit = hit.getSDFGridHit();
        v = gScene.getVertexDataFromSDFGrid(sdfGridHit, rayOrigin, rayDir);
        materialID = gScene.getMaterialID(sdfGridHit.instanceID);
    }
#endif

    return gScene.materials.prepareShadingData(v, materialID, -rayDir);
}

float3 evalSky(float3 dir)
{
    if (USE_ENV_LIGHT)
        return gScene.envMap.eval(dir);

    const float t = saturate(0.5f * dir.y + 0.5f);
    return lerp(float3(0.025f, 0.03f, 0.04f), float3(0.45f, 0.56f, 0.72f), t);
}

float3 evalHitRadiance(const HitInfo hit, float3 rayOrigin, float3 rayDir)
{
    const ShadingData sd = loadShadingData(hit, rayOrigin, rayDir);
    let lod = ExplicitLodTextureSampler(0.f);
    let mi = gScene.materials.getMaterialInstance(sd, lod);
    const BSDFProperties props = mi.getProperties(sd);

    const float3 lightDir = normalize(float3(-0.4f, 0.85f, 0.25f));
    const float ndotl = saturate(dot(sd.getOrientedFaceNormal(), lightDir));
    const float3 skyTint = float3(0.55f, 0.62f, 0.75f);
    const float3 albedo = props.diffuseReflectionAlbedo + 0.25f * props.specularReflectionAlbedo;

    return props.emission + albedo * (0.04f + ndotl * skyTint);
}

[numthreads(4, 4, 4)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint3 coord = dispatchThreadId;
    if (any(coord >= gGridSize))
        return;

    const float3 probePos = getProbePosition(coord);
    float3 radiance = float3(0.f);
    const uint rayCount = clamp(gRaysPerProbe, 1u, 12u);

    SceneRayQuery<1> sceneRayQuery;

    for (uint i = 0; i < rayCount; ++i)
    {
        const float3 dir = getProbeDirection(i);
        const Ray ray = Ray(probePos + dir * gRayBias, dir, 0.f, gMaxRayDistance);

        float hitT;
        const HitInfo hit = sceneRayQuery.traceRay(ray, hitT);
        radiance += hit.isValid() ? evalHitRadiance(hit, ray.origin, ray.dir) : evalSky(dir);
    }

    radiance /= float(rayCount);
    gProbeRadiance[coord] = float4(radiance, 1.f);
}
