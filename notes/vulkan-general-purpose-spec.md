# Vulkan Surface General-Purpose Spec

## Goal

Turn the Vulkan surface from a narrow primitive compositor into a general-purpose
EPA-driven 3D raster backend.

EPA should own scene orchestration.

The Vulkan surface should own rasterization and frame generation.

## Current State

Current `spirv.dat` capabilities:

- `op=0`: clear color
- `op=1`: filled rect
- `op=2`: line

That is enough for:

- UI composition
- overlays
- wireframe 3D
- debug visuals

That is not enough for:

- solid triangle meshes
- depth-tested scenes
- indexed geometry
- materials
- lighting
- textured surfaces

## Target Boundary

Keep this split:

- EPA describes scene state, draw lists, camera, instances, and materials.
- The Vulkan surface executes a compact render command stream.
- C++ host stays as transport/proxy and should avoid owning scene logic.

## Minimum General-Purpose Target

The first real target is not "full game engine".

It is:

1. triangle rasterization
2. depth buffer
3. indexed meshes
4. per-instance transforms
5. per-vertex or per-material color
6. camera projection
7. backface culling
8. solid fill
9. optional wireframe overlay

Once that exists, EPA can drive arbitrary 3D scene composition without relying
on CPU-side projection hacks.

## Render Command Families

Keep the existing commands and add staged 3D commands.

### Core Surface Commands

- `clear_color`
- `clear_depth`
- `rect`
- `line`
- `text`

### Scene Setup Commands

- `set_camera`
- `set_viewport`
- `set_projection`
- `set_render_flags`

### Resource Commands

- `define_mesh`
- `define_material`
- `define_texture`

### Draw Commands

- `draw_triangles`
- `draw_indexed`
- `draw_lines`
- `draw_rects`
- `draw_mesh_instance`

## Data Model

The renderer should move toward stable IDs instead of ad hoc scene expansion.

### Mesh

A mesh should include:

- mesh id
- vertex buffer
- optional index buffer
- primitive type
- optional normals
- optional UVs
- bounds

### Material

Start simple:

- material id
- base color
- wireframe color
- flags

Later:

- texture id
- normal flags
- shading mode

### Instance

Each instance should include:

- instance id
- mesh id
- material id
- position
- rotation
- scale
- optional tint override

## Staged Feature Plan

Build this in order.

### Stage 1: Solid Triangles

Status: passed on 2026-06-02

Add:

- triangle draw op
- triangle fill rasterization
- barycentric inside-test

Proof:

- one colored triangle
- cube from twelve triangles

Accepted proof:

- pure Python Vulkan proof rendered a filled triangle from `spirv.dat`
- user marked the solid triangle proof as passed

### Stage 2: Depth Buffer

Status: passed on 2026-06-02

Add:

- depth buffer storage
- clear depth
- depth compare
- nearest-surface selection

Proof:

- overlapping triangles at different Z
- rear face hidden by front face

Accepted proof:

- pure Python Vulkan proof rendered overlapping triangles
- the nearer orange triangle remained in front of the later-submitted blue triangle

### Stage 3: Indexed Meshes

Status: passed on 2026-06-02

Add:

- indexed draw command
- shared vertex buffers
- mesh id reuse

Proof:

- indexed cube
- indexed pyramid

Accepted proof:

- pure Python Vulkan proof rendered a solid cube from a shared vertex list plus triangle indices
- user accepted the solid indexed cube proof

### Stage 4: Camera in Renderer

Status: passed on 2026-06-02

Add:

- camera command
- world-to-view transform
- projection in render backend

Proof:

- orbiting cube at three camera angles
- perspective change with FOV variation

Accepted proof:

- pure Python Vulkan proof rendered the same indexed solid cube from three distinct camera views
- user accepted the result

### Stage 5: Instances

Status: passed on 2026-06-02

Add:

- draw mesh instance
- per-instance transforms

Proof:

- many cubes from one mesh
- transform changes per instance

Accepted proof:

- pure Python Vulkan proof rendered multiple cubes from one shared mesh with different transforms and colors
- user accepted the instance proof

### Stage 6: Culling

Status: passed on 2026-06-02

Add:

- backface culling
- near/far clipping behavior

Proof:

- rotating cube with hidden backfaces

Accepted proof:

- pure Python Vulkan proof rendered a solid cube with backfaces removed
- user accepted culling and explicitly treated remaining jagged edges as a later quality issue

### Stage 7: Flat Shading

Status: passed on 2026-06-02

Add:

- face normals or precomputed normals
- directional light
- flat diffuse shading

Proof:

- lit cube with visible face brightness changes

Accepted proof:

- pure Python Vulkan proof rendered a neutral cube with distinct dark, mid, and bright faces
- user accepted the flat-lit cube proof

### Stage 8: Materials

Status: passed on 2026-06-02

Add:

- material lookup
- base color per mesh or face

Proof:

- multi-material test scene

Accepted proof:

- pure Python Vulkan proof rendered multiple cubes with clearly different material colors under the same flat-light model
- user accepted the materials stage and explicitly noted the absence of shadows was expected

### Stage 9: Textures

Only after the above is stable.

Add:

- texture resource upload
- UV sampling

Proof:

- textured quad
- textured cube

## Explicit Non-Goals For First General-Purpose Pass

Do not start with:

- shadows
- transparency sorting
- skeletal animation
- post-processing
- PBR
- multi-pass lighting

Those can come later if needed.

## Command Encoding Direction

The current flat command buffer is acceptable for early stages, but a general
renderer should move toward explicit packed records for:

- frame header
- resource table
- mesh data blocks
- material blocks
- draw command blocks

The important rule is that the binary format must remain deterministic and easy
to generate from EPA.

## Test Protocol

Every feature must be tested with a rendered proof image or short sequence.

For each feature:

1. implement the smallest possible test case
2. render an image
3. inspect the image together
4. user says `passed` or `failed`
5. do not move to the next feature until current feature is accepted

## Immediate Next Feature

Start with:

- `draw_triangles`

That is the first missing piece required to move from wireframe-only rendering
to real general-purpose 3D.
