from falcor import *
import time
from pathlib import Path


USE_PROBES = True
VISUALISE_PROBES = False
BENCHMARK_START_FRAME = 100
BENCHMARK_END_FRAME = 500


def render_graph_ProbePathTracer():
    g = RenderGraph("ProbePathTracer")

    vbuffer = createPass("VBufferRT", {"samplePattern": "Stratified", "sampleCount": 8})
    g.addPass(vbuffer, "VBufferRT")

    direct = createPass(
        "PathTracer",
        {
            "samplesPerPixel": 1,
            "maxSurfaceBounces": 0,
            "maxDiffuseBounces": 1,
            "maxSpecularBounces": 1,
            "useNEE": True,
            "useMIS": True,
        },
    )
    g.addPass(direct, "DirectPathTracer")

    if USE_PROBES:
        probes = createPass(
            "RadianceProbePass",
            {
                "gridSize": uint3(20, 10, 20),
                "autoFitToScene": True,
                "visualizeProbes": VISUALISE_PROBES,
                "updateEveryFrame": False,
                "probeContributionScale": 1,
                "raysPerProbe": 8,
                "propagationIterations": 8,
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
m.loadScene("C:/Users/Uporabnik/Faks_local/NRG/Falcor/media/Bistro_v5_2/BistroInterior_Wine.pyscene")
# MY SCENE
# m.loadScene("C:/Users/Uporabnik/Faks_local/NRG/Falcor/media/Pillars/Pillars.pyscene")

try:
    m.scene.setIsAnimated(False)
except Exception:
    pass

ProbePathTracerGraph = render_graph_ProbePathTracer()
try:
    m.addGraph(ProbePathTracerGraph)
except NameError:
    pass

benchmark_started = False
benchmark_start_time = 0.0

for frame in range(1, BENCHMARK_END_FRAME + 1):
    if frame == BENCHMARK_START_FRAME:
        benchmark_started = True
        benchmark_start_time = time.perf_counter()

    m.renderFrame()

    if frame == BENCHMARK_END_FRAME and benchmark_started:
        benchmark_end_time = time.perf_counter()
        measured_frames = BENCHMARK_END_FRAME - BENCHMARK_START_FRAME + 1
        elapsed = benchmark_end_time - benchmark_start_time
        avg_fps = measured_frames / elapsed if elapsed > 0.0 else 0.0
        avg_ms = (elapsed * 1000.0) / measured_frames if measured_frames > 0 else 0.0
        result = f"Benchmark frames {BENCHMARK_START_FRAME}-{BENCHMARK_END_FRAME}: {avg_fps:.2f} FPS ({avg_ms:.2f} ms/frame)"
        print(result)

        log_path = Path(__file__).with_name("probe_pathtracer_benchmark.txt")
        with log_path.open("a", encoding="utf-8") as f:
            f.write(result + "\n")
