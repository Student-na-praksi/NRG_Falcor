/***************************************************************************
 # Copyright (c) 2015-24, NVIDIA CORPORATION. All rights reserved.
 **************************************************************************/
#include "RadianceProbePass.h"
#include "RenderGraph/RenderPassHelpers.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
const char kProbeUpdateFile[] = "RenderPasses/RadianceProbePass/ProbeUpdate.cs.slang";
const char kProbeVisualizeFile[] = "RenderPasses/RadianceProbePass/ProbeVisualize.cs.slang";

const char kInput[] = "input";
const char kPosW[] = "posW";
const char kProbeRadiance[] = "probeRadiance";
const char kOutput[] = "output";

const char kGridSize[] = "gridSize";
const char kAutoFitToScene[] = "autoFitToScene";
const char kGridMin[] = "gridMin";
const char kGridSpacing[] = "gridSpacing";
const char kProbeRadius[] = "probeRadius";
const char kProbeContributionScale[] = "probeContributionScale";
const char kRaysPerProbe[] = "raysPerProbe";
const char kRayBias[] = "rayBias";
const char kMaxRayDistance[] = "maxRayDistance";
const char kVisualizeProbes[] = "visualizeProbes";
const char kUpdateEveryFrame[] = "updateEveryFrame";

template<typename T>
constexpr const T& clamp(const T& v, const T& lo, const T& hi)
{
    return std::min(std::max(v, lo), hi);
}

float3 probeHeatmap(float t)
{
    t = std::clamp(t, 0.f, 1.f);

    const float3 c0 = float3(0.06f, 0.02f, 0.18f); // deep purple
    const float3 c1 = float3(0.10f, 0.18f, 0.62f); // blue
    const float3 c2 = float3(0.00f, 0.68f, 0.82f); // cyan
    const float3 c3 = float3(0.35f, 0.88f, 0.22f); // green
    const float3 c4 = float3(1.00f, 0.92f, 0.18f); // yellow
    const float3 c5 = float3(0.98f, 0.98f, 1.00f); // near white

    if (t < 0.20f) return lerp(c0, c1, t / 0.20f);
    if (t < 0.45f) return lerp(c1, c2, (t - 0.20f) / 0.25f);
    if (t < 0.70f) return lerp(c2, c3, (t - 0.45f) / 0.25f);
    if (t < 0.90f) return lerp(c3, c4, (t - 0.70f) / 0.20f);
    return lerp(c4, c5, (t - 0.90f) / 0.10f);
}

uint3 sanitizeGridSize(uint3 v)
{
    return max(v, uint3(1, 1, 1));
}
}

extern "C" FALCOR_API_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<RenderPass, RadianceProbePass>();
}

RadianceProbePass::RadianceProbePass(ref<Device> pDevice, const Properties& props) : RenderPass(pDevice)
{
    if (!mpDevice->isShaderModelSupported(ShaderModel::SM6_5))
        FALCOR_THROW("RadianceProbePass requires Shader Model 6.5 support.");
    if (!mpDevice->isFeatureSupported(Device::SupportedFeatures::RaytracingTier1_1))
        FALCOR_THROW("RadianceProbePass requires Raytracing Tier 1.1 support.");

    parseProperties(props);
}

void RadianceProbePass::parseProperties(const Properties& props)
{
    for (const auto& [key, value] : props)
    {
        if (key == kGridSize)
            mGridSize = sanitizeGridSize(value);
        else if (key == kAutoFitToScene)
            mAutoFitToScene = value;
        else if (key == kGridMin)
            mGridMin = value;
        else if (key == kGridSpacing)
            mGridSpacing = value;
        else if (key == kProbeRadius)
            mProbeRadius = value;
        else if (key == kProbeContributionScale)
            mProbeContributionScale = value;
        else if (key == kRaysPerProbe)
            mRaysPerProbe = clamp((uint32_t)value, 1u, 12u);
        else if (key == kRayBias)
            mRayBias = value;
        else if (key == kMaxRayDistance)
            mMaxRayDistance = value;
        else if (key == kVisualizeProbes)
            mVisualizeProbes = value;
        else if (key == kUpdateEveryFrame)
            mUpdateEveryFrame = value;
        else
            logWarning("Unknown property '{}' in RadianceProbePass properties.", key);
    }
}

Properties RadianceProbePass::getProperties() const
{
    Properties props;
    props[kGridSize] = mGridSize;
    props[kAutoFitToScene] = mAutoFitToScene;
    props[kGridMin] = mGridMin;
    props[kGridSpacing] = mGridSpacing;
    props[kProbeRadius] = mProbeRadius;
    props[kProbeContributionScale] = mProbeContributionScale;
    props[kRaysPerProbe] = mRaysPerProbe;
    props[kRayBias] = mRayBias;
    props[kMaxRayDistance] = mMaxRayDistance;
    props[kVisualizeProbes] = mVisualizeProbes;
    props[kUpdateEveryFrame] = mUpdateEveryFrame;
    return props;
}

RenderPassReflection RadianceProbePass::reflect(const CompileData& compileData)
{
    RenderPassReflection reflector;
    reflector.addInput(kInput, "Optional color to show underneath the probe overlay.")
        .texture2D()
        .flags(RenderPassReflection::Field::Flags::Optional);
    reflector.addInput(kPosW, "Optional world-space position buffer used for probe lookup.")
        .texture2D()
        .format(ResourceFormat::RGBA32Float)
        .flags(RenderPassReflection::Field::Flags::Optional);
    reflector.addOutput(kProbeRadiance, "3D probe radiance volume")
        .texture3D(mGridSize.x, mGridSize.y, mGridSize.z)
        .bindFlags(ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource)
        .format(ResourceFormat::RGBA32Float);
    reflector.addOutput(kOutput, "Radiance probe visualization output")
        .texture2D()
        .flags(RenderPassReflection::Field::Flags::Optional)
        .bindFlags(ResourceBindFlags::UnorderedAccess)
        .format(ResourceFormat::RGBA32Float);

    return reflector;
}

void RadianceProbePass::compile(RenderContext* pRenderContext, const CompileData& compileData)
{
    mFrameDim = compileData.defaultTexDims;
}

void RadianceProbePass::setScene(RenderContext* pRenderContext, const ref<Scene>& pScene)
{
    mpScene = pScene;
    mpProbeUpdatePass = nullptr;
    mpProbeVisualizePass = nullptr;
    mpProbeRadiance = nullptr;
    mNeedsProbeUpdate = true;

    if (mpScene)
    {
        updateGridFromSceneBounds();
        createPrograms();
        createProbeTexture();
    }
}

void RadianceProbePass::createPrograms()
{
    FALCOR_ASSERT(mpScene);

    DefineList defines = mpScene->getSceneDefines();
    defines.add("USE_ENV_LIGHT", mpScene->useEnvLight() ? "1" : "0");

    ProgramDesc updateDesc;
    updateDesc.addShaderModules(mpScene->getShaderModules());
    updateDesc.addShaderLibrary(kProbeUpdateFile).csEntry("main");
    updateDesc.addTypeConformances(mpScene->getTypeConformances());
    mpProbeUpdatePass = ComputePass::create(mpDevice, updateDesc, defines);

    ProgramDesc visualizeDesc;
    visualizeDesc.addShaderModules(mpScene->getShaderModules());
    visualizeDesc.addShaderLibrary(kProbeVisualizeFile).csEntry("main");
    visualizeDesc.addTypeConformances(mpScene->getTypeConformances());
    mpProbeVisualizePass = ComputePass::create(mpDevice, visualizeDesc, mpScene->getSceneDefines());
}

void RadianceProbePass::createProbeTexture()
{
    mGridSize = sanitizeGridSize(mGridSize);
    mpProbeRadiance = mpDevice->createTexture3D(
        mGridSize.x,
        mGridSize.y,
        mGridSize.z,
        ResourceFormat::RGBA32Float,
        1,
        nullptr,
        ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess
    );
    mNeedsProbeUpdate = true;
}

void RadianceProbePass::updateGridFromSceneBounds()
{
    if (!mpScene || !mAutoFitToScene)
        return;

    const AABB& bounds = mpScene->getSceneBounds();
    if (!bounds.valid())
    {
        mGridMin = float3(-1.f);
        mGridSpacing = float3(
            mGridSize.x > 1 ? 2.f / float(mGridSize.x - 1) : 2.f,
            mGridSize.y > 1 ? 2.f / float(mGridSize.y - 1) : 2.f,
            mGridSize.z > 1 ? 2.f / float(mGridSize.z - 1) : 2.f
        );
        mProbeRadius = 0.12f * std::min(std::min(mGridSpacing.x, mGridSpacing.y), mGridSpacing.z);
        mMaxRayDistance = 4.f;
        return;
    }

    float3 minPoint = bounds.minPoint;
    float3 maxPoint = bounds.maxPoint;
    float3 extent = maxPoint - minPoint;

    const float maxExtent = std::max(std::max(extent.x, extent.y), extent.z);
    const float pad = std::max(0.01f, mScenePadding * maxExtent);
    minPoint -= float3(pad);
    maxPoint += float3(pad);
    extent = max(maxPoint - minPoint, float3(0.001f));

    mGridMin = minPoint;
    mGridSpacing = float3(
        mGridSize.x > 1 ? extent.x / float(mGridSize.x - 1) : extent.x,
        mGridSize.y > 1 ? extent.y / float(mGridSize.y - 1) : extent.y,
        mGridSize.z > 1 ? extent.z / float(mGridSize.z - 1) : extent.z
    );

    const float minSpacing = std::max(0.001f, std::min(std::min(mGridSpacing.x, mGridSpacing.y), mGridSpacing.z));
    mProbeRadius = 0.12f * minSpacing;
    mMaxRayDistance = 2.0f * length(extent);
}

void RadianceProbePass::updateProbes(RenderContext* pRenderContext)
{
    FALCOR_ASSERT(mpScene && mpProbeUpdatePass && mpProbeRadiance);

    mpScene->bindShaderDataForRaytracing(pRenderContext, mpProbeUpdatePass->getRootVar()["gScene"]);

    ShaderVar var = mpProbeUpdatePass->getRootVar()["CB"];
    var["gGridSize"] = mGridSize;
    var["gGridMin"] = mGridMin;
    var["gGridSpacing"] = mGridSpacing;
    var["gRayBias"] = mRayBias;
    var["gMaxRayDistance"] = mMaxRayDistance;
    var["gRaysPerProbe"] = mRaysPerProbe;

    mpProbeUpdatePass->getRootVar()["gProbeRadiance"] = mpProbeRadiance;
    mpProbeUpdatePass->execute(pRenderContext, mGridSize);
}

void RadianceProbePass::updateProbeDebugMaterials(const float4* pProbeData)
{
    if (!mpScene || !pProbeData)
        return;

    const uint32_t probeCount = mGridSize.x * mGridSize.y * mGridSize.z;
    if (probeCount == 0)
        return;

    float maxLuminance = 0.f;
    for (uint32_t i = 0; i < probeCount; ++i)
    {
        const float3 radiance = pProbeData[i].xyz();
        maxLuminance = std::max(maxLuminance, dot(radiance, float3(0.2126f, 0.7152f, 0.0722f)));
    }

    const float responseScale = 6.0f;
    const float responseDenom = std::log2(1.f + maxLuminance * responseScale);
    const float invResponseDenom = responseDenom > 0.f ? 1.f / responseDenom : 0.f;

    for (uint32_t i = 0; i < probeCount; ++i)
    {
        const float3 radiance = pProbeData[i].xyz();
        const float lum = dot(radiance, float3(0.2126f, 0.7152f, 0.0722f));
        const float normalized = responseDenom > 0.f ? std::log2(1.f + lum * responseScale) * invResponseDenom : 0.f;
        const float tone = std::sqrt(std::clamp(normalized, 0.f, 1.f));
        const float3 color = probeHeatmap(tone);

        const std::string materialName = "ProbeDebugMaterial_" + std::to_string(i);
        auto pMaterial = mpScene->getMaterialByName(materialName);
        if (!pMaterial)
            continue;

        if (auto pBasic = pMaterial->toBasicMaterial())
        {
            pBasic->setBaseColor(float4(color, 1.f));
        }
    }
}

void RadianceProbePass::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    const auto& pOutput = renderData.getTexture(kOutput);
    const auto& pInput = renderData.getTexture(kInput);
    const auto& pPosW = renderData.getTexture(kPosW);
    const auto& pProbeRadiance = renderData.getTexture(kProbeRadiance);

    if (!mpScene || !mpProbeUpdatePass || !mpProbeVisualizePass || !mpProbeRadiance)
    {
        if (pOutput)
            pRenderContext->clearUAV(pOutput->getUAV().get(), float4(0.f));
        return;
    }

    if (is_set(mpScene->getUpdates(), IScene::UpdateFlags::RecompileNeeded) ||
        is_set(mpScene->getUpdates(), IScene::UpdateFlags::GeometryChanged))
    {
        FALCOR_THROW("RadianceProbePass does not support scene changes that require shader recompilation yet.");
    }

    if (mUpdateEveryFrame || mNeedsProbeUpdate)
    {
        updateProbes(pRenderContext);
        const auto probeBlobSize = mpProbeRadiance->getSubresourceLayout(0).getTotalByteSize();
        std::vector<float4> probeData(probeBlobSize / sizeof(float4));
        mpProbeRadiance->getSubresourceBlob(0, probeData.data(), probeBlobSize);
        updateProbeDebugMaterials(probeData.data());
        mNeedsProbeUpdate = false;
    }

    if (pOutput)
    {
        mFrameDim = uint2(pOutput->getWidth(), pOutput->getHeight());
    }
    else if (pInput)
    {
        mFrameDim = uint2(pInput->getWidth(), pInput->getHeight());
    }
    else
    {
        mFrameDim = uint2(0, 0);
    }

    if (mpProbeVisualizePass->getProgram()->addDefine("INPUT_VALID", pInput ? "1" : "0"))
        mpProbeVisualizePass->setVars(nullptr);
    if (mpProbeVisualizePass->getProgram()->addDefine("POSW_VALID", pPosW ? "1" : "0"))
        mpProbeVisualizePass->setVars(nullptr);

    mpScene->bindShaderDataForRaytracing(pRenderContext, mpProbeVisualizePass->getRootVar()["gScene"]);

    ShaderVar var = mpProbeVisualizePass->getRootVar()["CB"];
    var["gFrameDim"] = mFrameDim;
    var["gGridSize"] = mGridSize;
    var["gGridMin"] = mGridMin;
    var["gGridSpacing"] = mGridSpacing;
    var["gProbeRadius"] = mProbeRadius;
    var["gVisualizeProbes"] = mVisualizeProbes ? 1u : 0u;

    mpProbeVisualizePass->getRootVar()["gInput"] = pInput;
    if (pPosW)
        mpProbeVisualizePass->getRootVar()["gPosW"] = pPosW;
    if (pOutput)
    {
        mpProbeVisualizePass->getRootVar()["gOutput"] = pOutput;
        mpProbeVisualizePass->getRootVar()["gProbeRadiance"] = mpProbeRadiance;
        mpProbeVisualizePass->execute(pRenderContext, mFrameDim.x, mFrameDim.y);
    }

    if (pProbeRadiance)
    {
        pRenderContext->copyResource(pProbeRadiance.get(), mpProbeRadiance.get());
    }
}

void RadianceProbePass::renderUI(Gui::Widgets& widget)
{
    bool recreate = false;
    bool refit = false;

    recreate |= widget.var("Grid size", mGridSize, 1u, 64u);
    refit |= widget.checkbox("Auto fit to scene", mAutoFitToScene);
    if (!mAutoFitToScene)
    {
        refit |= widget.var("Grid min", mGridMin);
        refit |= widget.var("Grid spacing", mGridSpacing, 0.001f, 1e6f);
    }
    widget.var("Probe radius", mProbeRadius, 0.001f, 1e6f);
    widget.var("Probe contribution scale", mProbeContributionScale, 0.f, 2.f);
    widget.var("Ray bias", mRayBias, 0.f, 10.f);
    widget.var("Max ray distance", mMaxRayDistance, 0.001f, 1e9f);
    widget.var("Rays per probe", mRaysPerProbe, 1u, 12u);
    widget.checkbox("Visualize probes", mVisualizeProbes);
    widget.checkbox("Update every frame", mUpdateEveryFrame);

    if (widget.button("Recompute probes"))
        mNeedsProbeUpdate = true;

    if (recreate)
    {
        mGridSize = sanitizeGridSize(mGridSize);
        if (mAutoFitToScene)
            updateGridFromSceneBounds();
        createProbeTexture();
    }
    else if (refit)
    {
        if (mAutoFitToScene)
            updateGridFromSceneBounds();
        mNeedsProbeUpdate = true;
    }
}
