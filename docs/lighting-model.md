@page lighting_model Lighting model

No PBR stack: materials are albedo + optional emission, surfaces are diffuse only. Nearly all lighting is baked offline by `sloprad` and sampled cheaply at runtime; a small number of real-time lights and probe-lit dynamic objects layer on top. See @ref tut_csg for the compiler pipeline.

# Authoring {#lighting-authoring}

Two ways to put light into a map, described in @ref tut_first_map "the first-map tutorial":

- Light things: point, spot, and area lights, plus a sun light that pairs with brushes using a sky material to emulate directional sunlight through an opening.
- Emissive materials: the emissive channel of a texture turns the surface itself into an area light, so lit geometry (screens, lava, neon) doesn't need a hand-placed light thing alongside it.

Both feed the same bake; there's no runtime difference between them once compiled.

# Baking {#lighting-baking}

`sloprad` is a real (multi-bounce) radiosity solver, not Quake's single-bounce-plus-fudge approximation: `RadiositySettings` sets a bounce count and per-bounce sample count, and both the direct and bounce passes have an optional GPU compute path over the CPU reference implementation.

## Direct lighting {#lighting-direct}

Light things and emissive faces are both accumulated into the same per-luxel irradiance, but arrive at it differently:

- Point/spot lights fall off smoothly with distance out to their configured range, and spot lights additionally shape a cone with a soft edge.
- Sun light is directional rather than positional, with no distance falloff; its shadow edge can be baked sharp or softened into a penumbra.
- Emissive faces act as area lights: each receiving point integrates light from a stratified grid of samples spread across the emitter face, weighted by distance and by how the two surfaces are angled toward each other. Emissive faces also bleed a small amount of light across a seam into nearly-coplanar neighbors (e.g. a light fixture meeting the wall it's mounted on), so those seams don't read as a hard line.

All three soften the light/dark terminator at a surface's horizon with a small wrap term instead of a hard clamp.

## Bounce lighting {#lighting-bounce}

Each of the `bounces` passes is a stochastic gather rather than a full form-factor solve: every surface point samples the hemisphere above it and reads back what the *previous* pass computed at wherever those samples land. Bounce 0 is direct lighting only; each subsequent pass propagates light one additional bounce, and the result converges toward the true multi-bounce solution as `bounces` increases.

## Outputs {#lighting-outputs}

Two things come out of the bake, both indexed by `.rad` (metadata) with pixels in separate PNGs under `rad/`:

- Lightmap atlases — one UV chart per baked face, packed into a small number of shared atlas textures. Charts store HDR values with Ward-style RGBE8 shared-exponent encoding (see @ref encodergbe) rather than the older tonemapped-LDR path.
- Light probe grids — two volumetric grids (coarse and fine cell size) of points scattered through walkable space, each storing incident light as a low-order spherical-harmonics fit built from rays traced outward against the already-lit scene. The sun's contribution is added analytically on top rather than relying on those rays to reliably catch it.

# RAD Options {#lighting-rad-options}

## Luxels per meter (LPM) {#lighting-lpm}

<table>
<tr>
<td valign="top" width="50%">

The number of light texels baked per meter of world space. Higher numbers give sharper shadow edges and finer detail in the baked lighting, at the cost of larger charts — push it far enough on a big level and chart packing can overflow a single atlas, forcing the bake to allocate additional atlas textures to hold the overflow. Lower LPM keeps atlas count and bake time down, but shadow edges get visibly blockier and softer, since each texel now covers more world space.

The comparison to the right bakes the same scene from 64 down to 4 LPM: notice how the pillar shadows go from a crisp, well-defined edge at 64 LPM to a blurred, low-resolution smear by 4 LPM.

</td>
<td valign="top" width="50%">
<img src="lpm.png" alt="Lightmap resolution compared at 64, 32, 16, 8, and 4 luxels per meter" style="width:100%"/>
</td>
</tr>
</table>

## Bounces {#lighting-bounces}

<table>
<tr>
<td valign="top" width="50%">

The bounce count controls how many indirect-light passes @ref lighting-bounce runs after the direct pass. At zero bounces, only directly-lit surfaces receive any light at all — anything only reachable by light bouncing off another surface first (the far side of the pillars, the corners tucked out of the key light's line of sight) stays pitch black, exactly as seen in the "No bounce" row below.

Each additional bounce gathers light that landed on other surfaces during the previous pass and redistributes it further into the scene, so those dark areas progressively fill in with bounced, color-tinted light — note the red and blue wall color bleeding onto the pillars and floor as the bounce count climbs. Returns diminish quickly: most of the visible change happens in the first bounce or two, and each pass after that costs roughly the same additional bake time for a progressively smaller improvement.

</td>
<td valign="top" width="50%">
<img src="bounces.png" alt="Indirect bounce lighting compared at no bounce, 1, 2, and 4 bounces" style="width:100%"/>
</td>
</tr>
</table>

## Samples {#lighting-samples}

<table>
<tr>
<td valign="top" width="50%">

The sample count sets how many rays each surface point fires into the hemisphere above it during every bounce pass. Because that gather is a statistical estimate rather than an exact integral, too few samples leaves visible noise in the indirect lighting — most obvious on a large, slowly-varying surface like the back wall below, where the graininess at 4 and 8 samples stands out clearly against the smoother result at 32 and 64. More samples average that noise away, at a proportional cost in ray casts (and bake time) per bounce pass.

Sample count and bounce count compound: with more bounce passes, sample noise from an early pass gets gathered and re-spread by later passes, so a noisy low-sample bake tends to look worse as more bounces are added, not better — raise both together rather than cranking bounces alone.

</td>
<td valign="top" width="50%">
<img src="samples.png" alt="Indirect bounce noise compared at 64, 32, 16, 8, and 4 samples" style="width:100%"/>
</td>
</tr>
</table>

# Sampling at runtime {#lighting-runtime}

Static (BSP) surfaces read their own baked lightmap directly: the renderer looks up the face's chart, samples the atlas texel under that point, and decodes it back to linear irradiance before display.

Dynamic objects (actors, pickups, viewmodel) have no lightmap UVs, so they instead sample the probe grid at their world position: the nearest surrounding probes (fine grid first, falling back to coarse where fine data is missing) are blended together and evaluated toward the surface normal or view direction. This is the same probe data a package can query directly for faux-shading sprites or viewmodels.

<table>
<tr>
<td width="50%"><img src="spriteprobes.png" alt="Sprite sample points used for faux-shading, marked at the top and bottom of each sprite" style="width:100%"/></td>
<td width="50%"><img src="gridprobes.png" alt="Volumetric light probe grid visualized as points scattered through a room" style="width:100%"/></td>
</tr>
</table>

Dynamic lights (muzzle flashes, etc.) are the one part of the model that isn't baked — they composite on top of the already-tonemapped baked color at draw time. Because a straight add after tonemapping would look wrong (tonemapping is nonlinear), the compositor undoes the tonemap on the baked color, adds the dynamic light in linear space, and re-tonemaps the sum. Static lightmap texels never pay that cost — only the pixels a dynamic light actually touches are re-tonemapped per frame.
