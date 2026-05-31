/***************************************************************************
 # Copyright (c) 2015-24, NVIDIA CORPORATION. All rights reserved.
 **************************************************************************/
#pragma once

#include "Falcor.h"
#include "RenderGraph/RenderPass.h"

using namespace Falcor;

class RadianceProbePass : public RenderPass
{
public:
    FALCOR_PLUGIN_CLASS(RadianceProbePass, "RadianceProbePass", "GPU-updated radiance probe grid with debug visualization.");

    static ref<RadianceProbePass> create(ref<Device> pDevice, const Properties& props)
    {
        return make_ref<RadianceProbePass>(pDevice, props);
    }

    RadianceProbePass(ref<Device> pDevice, const Properties& props);

    Properties getProperties() const override;
    RenderPassReflection reflect(const CompileData& compileData) override;
    void compile(RenderContext* pRenderContext, const CompileData& compileData) override;
    void execute(RenderContext* pRenderContext, const RenderData& renderData) override;
    void renderUI(Gui::Widgets& widget) override;
    void setScene(RenderContext* pRenderContext, const ref<Scene>& pScene) override;

private:
    void parseProperties(const Properties& props);
    void createPrograms();
    void createProbeTexture();
    void updateGridFromSceneBounds();
    void updateProbes(RenderContext* pRenderContext);

    ref<Scene> mpScene;
    ref<ComputePass> mpProbeUpdatePass;
    ref<ComputePass> mpProbeVisualizePass;
    ref<Texture> mpProbeRadiance;

    uint3 mGridSize = { 8, 4, 8 };
    float3 mGridMin = { -1.f, -1.f, -1.f };
    float3 mGridSpacing = { 1.f, 1.f, 1.f };
    float mScenePadding = 0.05f;
    float mProbeRadius = 0.18f;
    float mProbeContributionScale = 0.65f;
    float mRayBias = 0.03f;
    float mMaxRayDistance = 1e6f;
    uint32_t mRaysPerProbe = 12;
    uint2 mFrameDim = { 0, 0 };

    bool mAutoFitToScene = true;
    bool mVisualizeProbes = false;
    bool mUpdateEveryFrame = false;
    bool mNeedsProbeUpdate = true;
};
