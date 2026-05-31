from falcor import *


USE_PROBES = True
VISUALISE_PROBES = False


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
                "visualizeProbes": VISUALISE_PROBES,
                "updateEveryFrame": False,
                "probeContributionScale": 0.65,
                "raysPerProbe": 8,
            },
        )
        g.addPass(probes, "RadianceProbePass")

    if not VISUALISE_PROBES:
        accumulate = createPass("AccumulatePass", {"enabled": True, "precisionMode": "Single"})
        g.addPass(accumulate, "AccumulatePass")

    tone_mapper = createPass("ToneMapper", {"autoExposure": False, "exposureCompensation": 0.0})
    g.addPass(tone_mapper, "ToneMapper")

    g.addEdge("VBufferRT.vbuffer", "DirectPathTracer.vbuffer")
    g.addEdge("VBufferRT.viewW", "DirectPathTracer.viewW")
    if USE_PROBES:
        g.addEdge("RadianceProbePass.probeRadiance", "DirectPathTracer.probeRadiance")
    if VISUALISE_PROBES:
        g.addEdge("RadianceProbePass.output", "ToneMapper.src")
    else:
        g.addEdge("DirectPathTracer.color", "AccumulatePass.input")
        g.addEdge("AccumulatePass.output", "ToneMapper.src")

    g.markOutput("ToneMapper.dst")
    return g

# OUTSIDE SCENE
# m.loadScene("C:/Users/Uporabnik/Faks_local/NRG/Falcor/media/Bistro_v5_2/BistroExterior.pyscene")
# INSIDE SCENE
# m.loadScene("C:/Users/Uporabnik/Faks_local/NRG/Falcor/media/Bistro_v5_2/BistroInterior_Wine.pyscene")
# MY SCENE
m.loadScene("C:/Users/Uporabnik/Faks_local/NRG/Falcor/media/Pillars/Pillars.pyscene")

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
