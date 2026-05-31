from falcor import *


USE_PROBES = True


def render_graph_ProbePathTracer():
    g = RenderGraph("ProbePathTracer")

    vbuffer = createPass("VBufferRT", {"samplePattern": "Stratified", "sampleCount": 8})
    g.addPass(vbuffer, "VBufferRT")

    direct = createPass(
        "PathTracer",
        {
            "samplesPerPixel": 1,
            "maxSurfaceBounces": 0,
            "maxDiffuseBounces": 0,
            "maxSpecularBounces": 0,
            "useNEE": True,
            "useMIS": True,
        },
    )
    g.addPass(direct, "DirectPathTracer")

    if USE_PROBES:
        probes = createPass(
            "RadianceProbePass",
            {
                "gridSize": uint3(8, 4, 8),
                "autoFitToScene": True,
                "visualizeProbes": False,
                "updateEveryFrame": False,
                "probeContributionScale": 0.65,
            },
        )
        g.addPass(probes, "RadianceProbePass")

    tone_mapper = createPass("ToneMapper", {"autoExposure": False, "exposureCompensation": 0.0})
    g.addPass(tone_mapper, "ToneMapper")

    g.addEdge("VBufferRT.vbuffer", "DirectPathTracer.vbuffer")
    g.addEdge("VBufferRT.viewW", "DirectPathTracer.viewW")
    if USE_PROBES:
        g.addEdge("DirectPathTracer.color", "RadianceProbePass.input")
        g.addEdge("DirectPathTracer.reflectionPosW", "RadianceProbePass.posW")
        g.addEdge("RadianceProbePass.output", "ToneMapper.src")
    else:
        g.addEdge("DirectPathTracer.color", "ToneMapper.src")

    g.markOutput("ToneMapper.dst")
    return g


m.loadScene("C:/Users/Uporabnik/Faks_local/NRG/Falcor/media/Bistro_v5_2/BistroExterior.pyscene")
try:
    m.scene.setIsAnimated(False)
except Exception:
    pass

ProbePathTracerGraph = render_graph_ProbePathTracer()
try:
    m.addGraph(ProbePathTracerGraph)
except NameError:
    pass

for _ in range(1):
    m.renderFrame()
