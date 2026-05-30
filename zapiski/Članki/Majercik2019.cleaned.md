# Dynamic Diffuse Global Illumination with Ray-Traced Irradiance Fields

Zander Majercik, Jean-Philippe Guertin

NVIDIA, Université de Montréal

Derek Nowrouzezahrai

McGill University

Morgan McGuire

NVIDIA and McGill University

> **Figure 1.** Combined with state-of-the-art glossy ray tracing and deferred direct shading, our method (left) generates full global illumination in dynamic scenes that are visually comparable to offline path-traced results (right) but several orders of magnitude faster: 6 ms/frame, versus 1 min/frame in this scene (on GeForce RTX 2080 Ti at 1920×1080). Insets isolate the direct lighting contribution and visualize the probe locations.

## Abstract

We show how to compute global illumination efficiently in scenes with dynamic objects and lighting. We extend classic irradiance probes to a compact encoding of the full irradiance field in a scene. First, we compute the dynamic irradiance field using an efficient GPU memory layout, geometric ray tracing, and appropriate sampling rates without down-sampling or filtering prohibitively large spherical textures. Second, we devise a robust filtered irradiance query, using a novel visibility-aware moment-based interpolant. We experimentally validate performance and accuracy tradeoffs and show that our method of dynamic diffuse global illumination (DDGI) robustly lights scenes of varying geometric and radiometric complexity (Figure 1). For completeness, we demonstrate results with a state-of-the-art glossy ray-tracing term for sampling the full dynamic light field and include reference GLSL code.

## 1 Introduction

Probe-based global illumination. Synthesizing images with accurate global-illumination (GI) effects contributes significantly to the believability of computer-generated imagery. Accurately solving physics-based GI formulations is a longstanding area of research, and doing so with offline numerical solvers remains a time-consuming cost. In an interactive rendering context, a significant amount of work on generating convincing real-time GI effects has led to many different solutions, each with specific tradeoffs among accuracy, flexibility, and performance.

Recent work on light field probes strikes one such tradeoff. That representation encodes the static local light field of a scene using a specialized encoding of precomputed probes placed statically in a scene [McGuire et al. 2017b]. The probe representation has many benefits, including the ability to perform efficient and accurate world-space (filtered) ray tracing for glossy and near-specular indirect transport, as well as supporting irradiance probe-like queries that are robust to light-leaking artifacts for smoother indirect diffuse illumination.

Given the query and sampling operations exposed by the light field probe representation, many shading algorithms can be implemented using this representation as a basis, often resulting in high-fidelity images generated at high performance rates. The main limitations of standard light field probes lie in their precomputed nature and the manner in which they sample lighting in the scene. Precomputing the probe data can be costly, and therefore only fixed lighting and geometric conditions are handled. Moreover, the irradiance spatial interpolation and prefiltered glossy sampling schemes can lead to aliasing and light-leaking in the diffuse and specular indirect illumination.

### Real-time GI

Unlike offline rendering, global-illumination solutions for real-time applications, such as video games, currently rely fundamentally on lighting data that can be rapidly read from spatial-angular data structures, and it is usually pre-computed or limited to slow updates from static geometry for dynamic lighting. Examples include lightmap representations, irradiance and radiance probes, and voxelized representations of the scene or lighting information. Each of these representations strikes a particular tradeoff between compactness, runtime flexibility, accuracy, and cost. In geometrically and/or radiometrically complex scenes, these methods all have well-documented undesirable artifacts that manifest as a result of undersampling and reconstruction. The most significant of these artifacts is light- and shadow-leaking in areas of complex visibility. Many recent GDC and SIGGRAPH talks isolate and discuss these issues.

Typically, heuristic workarounds are used. These vary with the art and technical constraints of a particular production. In cases where only static geometry and/or lighting are treated, a largely manual post-processing intervention is often done. Of course, such an approach scales poorly with scene complexity and still requires significant offline precomputation. This problem is further exaggerated in the context of dynamic environments, where the scene geometry and lighting can change at run-time, precluding manual intervention. As such, there is a great practical need for automatic caching solutions that are robust to dynamic scenes and do not sacrifice the high-performance nature of pre-cached global-illumination solutions.

The core problem underlying prior techniques is not inherent in the representations, which are often efficient and well-designed for capturing either radiance (light energy along a ray used for the glossy portion of shading) or irradiance (cosine-weighted integral of radiance necessary for the diffuse portion of shading). Rather, the problem is that the techniques lack visibility information and thus cannot encode the full light field or irradiance field (irradiance taking occlusion into account).

This paper describes a method for extending classic irradiance probes to a representation of the full irradiance field, shows how to efficiently update that representation at runtime, and then evaluates the performance and quality of that method. The academic term for the quantity computed is the dynamic indirect irradiance field; we call the new probe technique dynamic diffuse global illumination (DDGI).

### Contributions

- Extension of irradiance probes with accurate and dynamic occlusion information by an incremental scheme that leverages a compact, GPU-tailored data layout and compute schedule for converged multi-bounce diffuse global illumination.
- An algorithm for ray tracing irradiance probes independent of the primary visibility resolution and frame rate, avoiding the cost of denoising or prefiltering prohibitively high-resolution spherical textures.
- A spatial interpolation, occlusion, and filtering scheme more robust to irradiance queries in scenes with temporally-varying geometry and lighting.
- Evaluation of a system for producing results nearly identical to offline path tracing in many cases, combining dynamically-updated occlusion-aware irradiance with GPU ray-traced glossy and specular reflections, reducing aliasing artifacts in these indirect contributions.
- Open source reference shaders for implementing DDGI, taken directly from and compatible with the open source G3D Innovation Engine [McGuire et al. 2017a].

## 2 Related Work

Works on interactive global illumination span several decades. We review the areas most relevant to our work.

### Image-based lighting

Image-based lighting methods form the basis of most interactive pre-caching lighting solutions in modern video games [Martin and Einarsson 2010; Ritschel et al. 2009; McAuley 2012; Hooker 2016]. Here, a common workflow involves placing light probes (of various types) densely inside the volume of a scene, each of which encodes some form of a spherical (ir)radiance map. Prefiltered versions of these maps can also be stored to accelerate diffuse and glossy runtime shading queries.

One interesting variant of traditional light probes allows digital artists to manually place box or sphere proxies in a scene, and these proxies are used to warp probe queries at runtime in a manner that better approximates spatially-localized reflection variations [Lagarde and Zanuttini 2012]. Similarly, manually-placed convex proxy geometry sets are also used to bound blending weights when querying and interpolating between many light probes at runtime, in order to reduce light leaking artifacts—one of the predominant artifacts of such probe-based methods.

> **Figure 2.** Previous interactive GI methods suffer from artifacts that often necessitate heuristic solutions, typically based on art-direction or technical constraints. Visual artifacts in these methods can manifest themselves in various forms: (from left to right) light leaking, lightmap seams, visibility/occlusion undersampling, and inter-voxel visibility mismatches.

These probe- and image-based lighting techniques are ubiquitous in modern offline and real-time rendering, and we refer interested readers to a comprehensive survey [Reinhard et al. 2006].

While production-quality image-based lighting systems generate convincing illumination effects, practitioners agree that eliminating manual probe and proxy placement remains an important open problem in production [Hooker 2016]. Currently, without manual adjustment, it is impossible to automatically avoid probe placements that lead to light and dark (i.e., shadow) leaks or displaced reflection artifacts. To avoid these issues, some engines rely instead on screen-space ray tracing [Valient 2013] for pixel-accurate reflections. These methods, however, fail when a reflected object is not visible from the camera’s point of view, leading to inconsistent lighting and view-dependent (and so temporally unstable) reflection effects.

Light field probes [McGuire et al. 2017b] automatically resolve these issues in scenes with static geometry and lighting by encoding additional information about the scene geometry into spherical probes. A solution for dynamic lighting is presented in [Silvennoinen and Lehtinen 2017], but this solution only supports coarse dynamic occluders and requires complex probe placement based on static geometry. We inherit the advantages of the representation in [McGuire et al. 2017b], which we extend fundamentally to treat dynamic geometry and lighting variations at runtime. No manual placement is necessary and a naïve uniform-grid probe placement results in artifact-free renderings. Reflections appear (consistently) where they should due, in part, to an accurate world-space ray-tracing algorithm.

### Interactive ray tracing and shading

Many recent interactive rendering approaches treat the problem of resolving point-to-point visibility queries, shaping modern solutions used in practice today. Ritschel et al.’s [2008] imperfect shadow maps encode a sparse, low-resolution representation of point-to-point visibility in a scene, which is then used to compute accurate secondary diffuse and glossy reflections using virtual point lights (generated, for example, with a ray tracer). Our work is motivated by another such solution: voxel cone tracing. At a high level, one can interpret our ray-tracing technique as tracing rays against a spherical voxelized representation of the scene (i.e., as opposed to the octree representation constructed for traditional voxel cone tracing). Two important differences that contribute to many of the practical advantages of our representation are: first, that we explicitly encode geometric scene information (i.e., radial depth and depth-squared) instead of relying on the implicit octree structure to resolve local and global visibility details; and, second, that neither our spatial parameterization nor our filtering rely on scene geometry. This allows us to completely sidestep the light- (and dark-) leaking artifacts present in traditional voxel cone tracing. Finally, we are able to resolve centimeter-scale geometry at about the same cost (in space and time) as a voxel cone tracer that operates at meter-scale.

### Representation

We use Cigolle et al.’s [2014] octahedral mapping from the sphere to the unit square to store and query our spherical distributions. This parameterization has slightly less distortion and provides simpler border management than, for example, cube maps. Since we target true world-space ray tracing in a pixel shader, and not just screen-space ray tracing, our technique can be seen as a generalization of many previous real-time environment map Monte Carlo integration methods [Stachowiak and Uludag 2015; Wyman 2005; Toth et al. 2015; Jendersie et al. 2016].

We are also motivated by preliminary investigations that validate the accuracy of single-probe ray tracing and the feasibility of multi-probe traversal. Specifically, a single probe can perfectly sample the geometry of a region with star-shape topology, and thus ray tracing with a single probe in these regions will incur no visibility error (outside of errors due to probe-directional discretization).

## 3 Dynamic Diffuse Global Illumination Probes: Overview

As in previous work, we encode geometric and radiometric scene data into spherical distributions at discrete probe locations. We combine efficient GPU ray tracing to enable accurate shader-based world-space ray tracing (using either a probe-based marching approach, or native GPU ray-tracing APIs), with filtered irradiance queries to compute diffuse, glossy, and specular indirect illumination at real-time rates.

Specifically, we encode the spherical diffuse irradiance (in GL_R11G11B10F format at 8×8 octahedral resolution), spherical distance and squared distances to the nearest geometry (both in GL_RG16F format at 16×16 octahedral resolution). We pack each of these square probe textures into a single 2D texture atlas with duplicated gutter regions to allow bilinear interpolation without any boundary artifacts (see Figure 3).

Instead of precomputing the probe data once at scene initialization, we dynamically update the probes to capture variations due to dynamic geometry and lighting. This allows us to enable truly dynamic high-fidelity global illumination. Our method retains the high performance of prior work; Figure 4 illustrates our ability to compute fully converged multi-bounce global illumination.

At each frame we are able to efficiently blend updated ray-traced illumination into our probe atlas in addition to interpolating probe depth information to adapt to changes in scene geometry. In a forward or deferred shading render pass, probes can effectively be treated as indirect lighting buffers analogous to standard precomputed environment maps.

> **Figure 3.** Spherical irradiance and depth textures. We encode spherical data in an octahedral parameterization, packing all the probes in an atlas. One-pixel texture gutter/border ensures correct bilinear interpolation, and additional padding aligns probes on 4×4 write boundaries.

> **Figure 4.** Left to right: direct illumination only, direct illumination with one bounce of indirect diffuse illumination (computed with spherical irradiance updated by our dynamic filtered ray-casting approach), and the fully converged multi-bounce global illumination that iteratively incorporates bounced lighting computed across probes.

We detail our method for updating dynamic diffuse global illumination probe distributions in Section 4 before discussing how to use probes at runtime to efficiently compute dynamic global illumination in Section 5.

---

*Cleaned from a PDF copy; page numbers, headers, footers, and duplicate journal metadata were removed. Figure captions and image descriptions were preserved.*
