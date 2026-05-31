add_plugin(RadianceProbePass)

target_sources(RadianceProbePass PRIVATE
    RadianceProbePass.cpp
    RadianceProbePass.h
    ProbeUpdate.cs.slang
    ProbeVisualize.cs.slang
)

target_copy_shaders(RadianceProbePass RenderPasses/RadianceProbePass)

target_source_group(RadianceProbePass "RenderPasses")
