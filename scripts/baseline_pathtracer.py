# m.loadScene("C:/Users/Uporabnik/Faks_local/NRG/Falcor/media/Arcade/Arcade.pyscene")
from falcor import *

def render_graph_PathTracer():
    g = RenderGraph("PathTracer")
    
    # Create the VBuffer pass to generate visibility buffer
    VBufferRT = createPass("VBufferRT", {'samplePattern': 'Stratified', 'sampleCount': 16})
    g.addPass(VBufferRT, "VBufferRT")
    
    # Create the PathTracer pass
    PathTracer = createPass("PathTracer", {
        'samplesPerPixel': 1,
        'maxSurfaceBounces': 3,
        'maxDiffuseBounces': 3, #later set to 1
        'maxSpecularBounces': 3,
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
m.loadScene("C:/Users/Uporabnik/Faks_local/NRG/Falcor/media/Bistro_v5_2/BistroExterior.pyscene")
#m.loadScene("C:/Users/Uporabnik/Faks_local/NRG/Falcor/media/test_scenes/nested_dielectrics.pyscene")

# Create and add the graph
PathTracerGraph = render_graph_PathTracer()
try: 
    m.addGraph(PathTracerGraph)
except NameError: 
    None

# Render frames
for i in range(1):
    m.renderFrame()
