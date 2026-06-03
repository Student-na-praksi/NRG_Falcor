# m.loadScene("C:/Users/Uporabnik/Faks_local/NRG/Falcor/media/Arcade/Arcade.pyscene")
from falcor import *
import time
from pathlib import Path


BENCHMARK_START_FRAME = 100
BENCHMARK_END_FRAME = 500

def render_graph_PathTracer():
    g = RenderGraph("PathTracer")

    # Create the VBuffer pass to generate visibility buffer
    VBufferRT = createPass("VBufferRT", {'samplePattern': 'Stratified', 'sampleCount': 16})
    g.addPass(VBufferRT, "VBufferRT")

    # Create the PathTracer pass
    PathTracer = createPass("PathTracer", {
        'samplesPerPixel': 1,
        'maxSurfaceBounces': 0,
        'maxDiffuseBounces': 1,
        'maxSpecularBounces': 1,
        'useNEE': True,
        'useMIS': True
    })
    g.addPass(PathTracer, "PathTracer")

    # Create accumulation pass for temporal denoising/convergence
    AccumulatePass = createPass("AccumulatePass", {'enabled': True, 'precisionMode': 'Single'})
    g.addPass(AccumulatePass, "AccumulatePass")

    # Create tone mapper for display
    ToneMapper = createPass("ToneMapper", {'autoExposure': False, 'exposureCompensation': 0.0})
    g.addPass(ToneMapper, "ToneMapper")

    # Connect the passes
    # VBufferRT -> PathTracer inputs
    g.addEdge("VBufferRT.vbuffer", "PathTracer.vbuffer")
    g.addEdge("VBufferRT.viewW", "PathTracer.viewW")

    # PathTracer -> AccumulatePass
    # full Monte Carlo lighting
    g.addEdge("PathTracer.color", "AccumulatePass.input")

    # AccumulatePass -> ToneMapper
    g.addEdge("AccumulatePass.output", "ToneMapper.src")

    # Mark final output
    g.markOutput("ToneMapper.dst")

    return g

# Load the scene
# m.loadScene("C:/Users/Uporabnik/Faks_local/NRG/Falcor/media/Arcade/Arcade.pyscene")
# m.loadScene("C:/Users/Uporabnik/Faks_local/NRG/Falcor/media/Bistro_v5_2/BistroExterior.pyscene")
#m.loadScene("C:/Users/Uporabnik/Faks_local/NRG/Falcor/media/test_scenes/nested_dielectrics.pyscene")
# m.loadScene("C:/Users/Uporabnik/Faks_local/NRG/Falcor/media/Pillars/Pillars.pyscene")
m.loadScene("C:/Users/Uporabnik/Faks_local/NRG/Falcor/media/Bistro_v5_2/BistroInterior_Wine.pyscene")

# Create and add the graph
PathTracerGraph = render_graph_PathTracer()
try:
    m.addGraph(PathTracerGraph)
except NameError:
    None

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
        result = f"[baseline_pathtracer.py] Benchmark frames {BENCHMARK_START_FRAME}-{BENCHMARK_END_FRAME}: {avg_fps:.2f} FPS ({avg_ms:.2f} ms/frame)"
        print(result)

        log_path = Path(__file__).with_name("baseline_pathtracer_benchmark.txt")
        with log_path.open("a", encoding="utf-8") as f:
            f.write(result + "\n")
