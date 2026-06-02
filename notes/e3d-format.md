# E3D Format

## Goal

`E3D` is the human and AI collaboration format for authoring pre-mesh 3D
artifacts.

It is not the final mesh format.

It is a component assembly format used to build complex models from:

- primitives
- reusable subcomponents
- repeated instances
- explicit interconnects
- deterministic assembly operations

The compiled mesh is a later output.

## Design Rules

1. `E3D` must stay readable and editable as plain JSON.
2. `E3D` must describe intent and structure, not baked triangle soup.
3. `E3D` must support reuse through named components and instances.
4. `E3D` must expose attachment and movement rules explicitly.
5. `E3D` must be safe for incremental AI edits without rewriting whole files.
6. `E3D` must compile deterministically into runtime render or mesh data.

## Boundary

`E3D` owns:

- component definitions
- composition
- parameters
- materials
- anchors
- interconnects
- assembly operations
- compile hints
- preview data

`E3D` does not own:

- final GPU buffers
- final runtime mesh packing
- renderer-specific command streams

## Top-Level Shape

Current target shape:

- `format`
- `name`
- `units`
- `metadata`
- `imports`
- `parameters`
- `materials`
- `components`
- `instances`
- `connections`
- `operations`
- `anchors`
- `preview_2d`
- `compile`

## Top-Level Fields

### `format`

Use:

- `elara.e3d.component.v1`

This marks the file as a component-oriented pre-mesh authoring document.

### `name`

`name` should be the canonical dotted path from the `3d_artifacts` root.

Examples:

- `scenery.plants.trees.oak.leaf`
- `scenery.plants.trees.oak.v1.oak_trunk`
- `scenery.buildings.houses.simple.house`

This is the import identity for component reuse inside larger composite models.

File layout and `name` should match.

### `metadata`

Expected fields:

- `description`
- `authoring_mode`
- `compile_target`

Recommended values:

- `authoring_mode`: `component_assembly`
- `compile_target`: `mesh`

### `imports`

Use `imports` when a larger artifact should be assembled from reusable external
artifact files.

Expected fields per import entry:

- `id`
- `path`
- optional `component`

Example:

```json
"imports": [
  {
    "id": "front_wall_component",
    "path": "../../components/walls/front_wall.e3d.json",
    "component": "front_wall"
  }
]
```

Instances can then reference the imported alias through `component`.

### `parameters`

Human and AI editable scalar controls for the artifact.

Examples:

- `length`
- `width`
- `thickness`
- `segment_count`

These are authoring inputs, not mesh outputs.

### `materials`

Named material presets used by components or instances.

Start simple:

- `base_color`
- `roughness`

Later this can grow, but the authoring format should stay compact.

## Components

`components` is a dictionary of reusable building blocks.

Each component should be small and focused.

Component kinds:

- `primitive`
- `assembly`

### Primitive Component

Wraps a direct primitive description.

Example fields:

- `kind`
- `primitive`
- `material`
- `size`
- `radius`
- `length`
- `points_ref`
- `local_transform`
- `anchors`

Useful primitive patterns now include:

- `box`
- `capsule`
- `polygon_fan`
- `lobe_tip_chain`
- `thin_shell_from_outline`
- `wall_shell_with_cutouts`
- `rectangular_frame`
- `ring_section`

### `thin_shell_from_outline`

Use this when a 2D outline should become a paper-thin non-overlapping shell
instead of multiple broad surfaces fighting through each other.

Expected fields:

- `primitive`: `thin_shell_from_outline`
- `material`
- `points_ref`
- `thickness`
- optional `side_material`
- optional `back_material`

Behavior:

1. duplicate the 2D outline into a front and back face
2. offset them along local `z` by half the thickness
3. connect each perimeter edge to form side walls

This is a good default for leaves, thin panels, paper, fins, and blade-like
parts where a single filled polygon is too flat and multiple overlapping fills
cause cancellation or ambiguity.

### `wall_shell_with_cutouts`

Use this for thicker architectural or panel-like surfaces where one main outer
shape contains rectangular openings such as:

- house front walls
- facades
- panels with doors
- walls with windows
- thick window frames or trim built from an outer rectangle and inner cutout

Expected fields:

- `primitive`: `wall_shell_with_cutouts`
- `material`
- `points_ref`
- `cutout_refs`
- `thickness`
- optional `surface_normal`
- optional `inner_surfaces`
- optional `side_material`
- optional `back_material`

The main outer shape is duplicated into a carbon-copy back face, the cutout
openings are left empty, and the perimeter plus cutout edges are stitched into
a solid wall thickness.

`surface_normal` declares which local direction is the authored front or paint
face. For the current 2D wall-shell primitive this should normally be
`[0.0, 0.0, 1.0]`.

Surface generation follows one rule:

- the main outer shape and its carbon-copy back shape have joining surfaces
  pointing away from the main outer shape center
- each cutout has joining surfaces pointing toward that cutout's own center

Author the outer loop counter-clockwise when viewed from the front paint face.
Author cutout loops clockwise. The preview/runtime normalizes loops to this
convention before stitching side faces. Cutout side surfaces should semantically
point toward the void or opening center. If a frame needs stable named surfaces,
declare them in `inner_surfaces` with explicit normals, such as left jamb normal
`[1.0, 0.0, 0.0]` and right jamb normal `[-1.0, 0.0, 0.0]`.

Use this primitive, rather than a flat preview-only shape, when a frame needs
real thickness. The 2D outer outline plus cutout outline is converted into front
and back faces with stitched side faces.

Use `tools/e3d_shell_surfaces.py` to normalize shell loops and generate the
`outer_surfaces` and `inner_surfaces` declarations. Do not hand-flip shell
surface winding during normal authoring; regenerate the surfaces from the 2D
loops and let the validator show the declared normals.

### `rectangular_frame`

Use this for simple flat trim, picture-frame guides, and quick rectangular
surround previews.

Expected fields:

- `primitive`: `rectangular_frame`
- `material`
- `outer_size`
- `inner_size`
- optional `surface_normal`
- optional `anchors`

The frame is authored as a single semantic component rather than four loose bar
instances. This keeps AI edits focused on the frame dimensions and bonding
surface instead of manually maintaining four separate rectangles.

### `ring_section`

Use this for stacked cross-sections that will later be lofted or skinned into a
larger volume such as:

- tree trunks
- branches
- horns
- columns
- pipes

Expected fields:

- `primitive`: `ring_section`
- `material`
- `radius_x`
- `radius_z`
- optional `segment_count`
- optional `anchors`

The ring itself is only a section. The full volume is created by placing many
ring instances and lofting them together.

### Assembly Component

Wraps a reusable local subgraph.

Example fields:

- `kind`
- `components`
- `instances`
- `connections`
- `operations`
- `anchors`

This allows a leaf, weapon part, wall section, machine subassembly, or jointed
mechanism to be built once and reused many times.

## Instances

`instances` places components into the artifact.

Each instance should include:

- `id`
- `component`
- `transform`
- optional `material_override`
- optional `parameters`

An instance transform provides the initial placement.

The final structural relationship between instances should be expressed through
`connections`, not hidden only inside transforms.

## Anchors

Anchors are the stable interconnect points of the format.

They are used for:

- composition
- alignment
- articulation
- socketing
- compile-time validation
- later animation or procedural assembly

Anchors should be stable names, not anonymous positions.

Examples:

- `root`
- `stem_base`
- `blade_tip`
- `mount_top`
- `hinge_left`

### Anchor Shape

Each anchor should be an object, not a bare point.

Expected fields:

- `position`
- optional `orientation`
- optional `normal`
- optional `tangent`
- optional `connector`
- optional `tags`

Example:

```json
"anchors": {
  "mount_top": {
    "position": [0.0, 0.5, 0.0],
    "orientation": [0.0, 0.0, 0.0],
    "connector": "rigid_socket",
    "tags": ["mount", "top"]
  }
}
```

### Anchor Coordinate Rule

- component-level anchors are expressed in component local space
- artifact-level anchors are expressed in artifact space
- connection solving must match anchor frames, not only anchor positions

If orientation is omitted, the anchor can only be treated as a point match.
That is acceptable for simple rigid placement, but not preferred for movable
connections.

## Interconnect Mechanism

`connections` is the first-class interconnect mechanism of `E3D`.

A connection joins:

- one instance anchor
- to another instance anchor
- with a specific joint mode
- and explicit allowed motion ranges

This is the contract that lets AI and humans assemble complex artifacts from
small components without prematurely baking them into one mesh.

## Connection Records

`connections` is a list.

Each record describes one explicit attachment between two instance anchors.

Expected fields:

- `id`
- `from_instance`
- `from_anchor`
- `to_instance`
- `to_anchor`
- `joint`
- optional `authoring_pose`
- optional `preload`
- optional `notes`

Example:

```json
{
  "id": "stem_to_blade",
  "from_instance": "stem",
  "from_anchor": "top",
  "to_instance": "blade",
  "to_anchor": "base",
  "joint": {
    "mode": "rigid",
    "translation_limit": {
      "x": [0.0, 0.0],
      "y": [0.0, 0.0],
      "z": [0.0, 0.0]
    },
    "rotation_limit_deg": {
      "pitch": [0.0, 0.0],
      "yaw": [0.0, 0.0],
      "roll": [0.0, 0.0]
    }
  }
}
```

## Joint Modes

Supported authoring modes should start with:

- `rigid`
- `hinge`
- `slider`
- `ball`
- `planar`
- `surface_bond`

These are authoring semantics. The compiler/runtime may later reduce them to a
different internal representation, but the source format should keep these
meanings explicit.

### `rigid`

No relative movement is allowed.

Use this for permanently locked parts.

### `hinge`

Rotation is allowed around one axis, with a bounded angular range.

Other translation and rotation axes should normally be locked to zero.

### `slider`

Translation is allowed along one axis, with a bounded distance range.

Other axes stay locked.

### `ball`

Angular freedom is allowed in multiple axes, with bounded rotation ranges.

### `planar`

Movement is allowed within a plane and optionally around the plane normal.

### `surface_bond`

A surface bond attaches one component surface to another component surface.

Use `axis_lock_frame` to declare which frame the locks are relative to. For
bonding detail components onto an imported wall, use:

```json
"axis_lock_frame": "from_anchor_surface"
```

This means the `x`, `y`, and `z` lock axes are taken from the surface frame on
the receiving/imported object's `from_anchor`, not from world space and not from
the free component's own local transform.

Surface anchors used this way should expose a `surface_frame`:

```json
"surface_frame": {
  "x": [1.0, 0.0, 0.0],
  "y": [0.0, 1.0, 0.0],
  "z": [0.0, 0.0, 1.0]
}
```

For a flat wall, `z` is normally the surface normal, while `x` and `y` are the
surface tangent axes. Use `axis_lock` to state whether each surface-relative
axis is independently locked:

```json
"axis_lock": {
  "x": "locked",
  "y": "locked",
  "z": "locked"
}
```

Use `axis_offset` to keep a bonded component offset from the receiving surface
along any of those same surface-frame axes:

```json
"axis_offset": {
  "x": 0.0,
  "y": 0.0,
  "z": 0.04
}
```

For example, a window frame bonded to a wall can lock all three axes relative to
the wall surface while offsetting along surface `z` so the frame sits proud of
the wall instead of z-fighting with the wall face.

When all three axes are locked and `integrate_when_all_axes_locked` is true, the
compiler may treat the bonded instance as part of the receiving component for
mesh generation and later editing.

## Allowed Movement Ranges

Every non-rigid connection should describe its allowed range explicitly.

Do not rely on implied defaults for movable parts.

### Translation Limits

Use per-axis ranges in local joint space:

```json
"translation_limit": {
  "x": [-0.05, 0.05],
  "y": [0.0, 0.0],
  "z": [0.0, 0.0]
}
```

### Rotation Limits

Use degrees in authoring documents:

```json
"rotation_limit_deg": {
  "pitch": [-10.0, 10.0],
  "yaw": [0.0, 0.0],
  "roll": [0.0, 45.0]
}
```

### Rest Pose

Optional `authoring_pose` can describe the nominal rest pose inside the allowed
range.

This is useful for:

- editor handles
- procedural compile passes
- animation defaults

## Connector Compatibility

Anchors may declare a `connector` type.

Examples:

- `rigid_socket`
- `hinge_pin_y`
- `leaf_vein_mount`
- `weapon_rail_small`

Connections should only be considered valid when the anchor pair is compatible.

This matters for AI-assisted editing because it provides a structural rule
instead of relying on spatial guesswork alone.

## Assembly Rule

`instances` place parts.

`connections` constrain and interconnect parts.

Both are required for meaningful assembly.

An instance transform gives an initial placement.
A connection record states how that placement is locked or allowed to move.

The compiler should treat the connection as authoritative.

## Artifact-Level Anchors

Top-level `anchors` still matter.

They should define:

- root placement points
- export sockets
- later mount points for larger assemblies

Artifact-level anchors use the same object shape as component anchors.

## Operations

`operations` are deterministic assembly passes.

Examples:

- `mirror`
- `duplicate_along_curve`
- `smooth`
- `boolean_union`
- `weld`
- `loft_sections`

These should remain declarative.

The operation list is part of the source-of-truth artifact, not an opaque
compiler-only stage.

### `loft_sections`

`loft_sections` is the standard section-stack pattern for trunks and similar
forms.

Expected fields:

- `op`: `loft_sections`
- `section_instances`
- optional `material`
- optional `side_material`
- optional `cap_start`
- optional `cap_end`

This should be authored as:

1. place a ring every 10 to 20 cm
2. vary the radii gradually
3. loft adjacent rings together
4. apply smoothing or later bark/detail passes afterward

## Preview

`preview_2d` is allowed as an IDE/editor preview aid.

It is not the authoritative model definition.

The authoritative structure is:

- `components`
- `instances`
- `connections`
- `operations`

## Compile Section

`compile` contains non-authoritative build hints.

Examples:

- `mesh_resolution`
- `generate_normals`
- `preserve_part_ids`

These are compile preferences, not authoring semantics.

## Validation Rules

Minimum validation for `connections`:

1. `from_instance` and `to_instance` must exist.
2. `from_anchor` and `to_anchor` must exist on the resolved components.
3. joint `mode` must be recognized.
4. movement ranges must be well-ordered `[min, max]`.
5. rigid joints should normally have zero ranges on all axes.
6. incompatible connector types should fail validation.
7. one anchor should not be ambiguously connected unless the component
   explicitly allows multi-connect.

## Migration Rule

Older `nodes`-based templates should move toward:

- `components`
- `instances`
- `connections`

If a direct primitive is needed, define it once in `components` and place it in
`instances`.

Do not keep growing one flat `nodes` list for complex artifacts.

## Immediate IDE Direction

The IDE currently only previews `preview_2d`.

Next E3D work should align to this order:

1. create and save component-oriented `.e3d.json`
2. validate anchors and `connections`
3. preview component structure and interconnects in the IDE
4. compile E3D to a mesh/runtime artifact
5. render compiled output through the common Vulkan path
