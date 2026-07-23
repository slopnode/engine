# Overview

slopengine is a small hobby project for making first-person games. It is aimed at non-commercial use: personal experiments, learning projects, and weekend games rather than studio pipelines or commercial shipping.

Despite the name, it is not a large self-contained engine. It is an assembly of popular free libraries, with a thin project layer that defines how content is stored, how first-person levels are built, and how tools like Blender feed into a running game. Most of what you would expect from an "engine" (a window, drawing, physics, gameplay structure, scripting) comes from those libraries. What this repository adds is the packaging, formats, and wiring that hold the assembly together.

Its main features are:

- **Package-based content** with plain folders, text descriptors, and binary companions. Mods stack on a base package and override assets by path. [Package structure](package-structure.md)
- **S-expression and Scheme (s7)** for materials, maps, scripts, and related descriptors — readable on disk and easy to generate from custom tools. [Scripting](scripting.md), [Writing s7](s7.md)
- **First-person movement and view**: character capsule, look, eye-space weapon / viewmodel sockets, and package-owned presentation hooks. [Player](player.md), [View frustum culling](frustum.md)
- **Brush CSG levels** compiled through slopbsp → slopvis → optional sloprad lightmaps, with an interactive editor (slopmap). [Maps](maps.md), [slopmap](slopmap.md)
- **Bake-first lighting**: offline lightmaps on diffuse surfaces, plus a small ranked dynamic-light overlay (flashlight and similar). Not a runtime PBR stack. [Lights](lights.md), [Radiosity](rad.md)
- **Albedo + emission materials**, PNG textures, and custom GLSL shaders. [Materials, textures, and shaders](materials.md)
- **Prop and character meshes** (.geo) with optional skeletal skinning from a Blender exporter. [Geometry](geometry.md), [Skeletal animation](animation.md)
- **Doom-style sprites**: multi-rotation frames, hit masks, and .spanim clip banks (world and first-person). [Sprites](sprites.md), [slopsprite](slopsprite.md)
- **Placed things** in maps: static props, usables, actors, and bake / editor lights. [Things](things.md)
- **Audio** via SoLoud (.ogg, procedural Sfxr defs, music bus); optional Valve Steam Audio for HRTF, occlusion, and reflections. [Audio](audio.md)
- **flecs ECS** for runtime entities and systems, with Scheme bindings for gameplay and UI hooks. [Scripting](scripting.md)
- **User data outside packages**: settings, screenshots, and save blobs keyed by the mount stack. [Persistence](persistence.md)
- Built in **C++20** on **raylib**, **flecs**, and **s7**; CMake on Windows, Linux, and macOS.

## A familiar content style

Asset handling is deliberately old-school. Content lives in plain package folders with simple text descriptors and binary companions, closer to classic early-2000s game directories than to a single opaque project file. Materials, meshes, skeletons, animations, and maps are separate files you can open, copy, and override. Mods stack on a base package and replace assets by path.

Much of that text is S-expression data or Scheme (s7) source: materials, package metadata, map brushes, scripts, and related descriptors are readable and easy to generate. The formats are small and stable on purpose. Standard tools ship with the project, but developers are able (and encouraged) to write their own authoring utilities: importers, level editors, batch converters, or whatever fits their workflow. If you can emit the same plain files, the game will load them. Gameplay and presentation hooks are Scheme as well; see [Scripting](scripting.md).

That style favors clarity and direct editing over locking content behind one official editor. You always know where a texture or material lives on disk.

## First-person levels

Level work is built around first-person spaces: rooms and solids authored as convex brush CSG (with box sugar for common cases), compiled for structure and visible faces (slopbsp -> slopvis), then optionally lightmapped with sloprad. Lighting is offline lightmaps on diffuse (albedo) surfaces, not a runtime PBR pipeline, with a small ranked dynamic-light overlay for things like a flashlight. Props and characters come in separately as .geo assets from a modelling tool. The split mirrors the classic approach of world geometry versus placeable models, with tooling focused on walking through baked indoor spaces rather than open-world streaming or cinematic pipelines. See [Lights](lights.md) and [Player](player.md).

## Working with Blender and free tools

Modelling and animation use free, widely available software. A Blender exporter writes .geo, skeletons, and animations into the package formats the game expects. Materials and textures stay ordinary files in the package; Blender material names map to those paths. Level shells stay in the CSG tools rather than being forced through a mesh exporter.

The project prefers that kind of integration: popular free graphics tools on one side, simple package files on the other, with a thin export path between them. The Blender addon is one such path, not the only allowed one. Custom exporters and editors that write the same S-expression and Scheme content are welcome.