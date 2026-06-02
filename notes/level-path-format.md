# Level Path Format

## Goal

This format is the structural path-layout source for auto-generated levels.

It is not detailed geometry.

It is the coarse navigable-space graph that later systems can expand into:

- terrain
- rooms
- passages
- stairs
- shafts
- doors
- generated architecture

The foundation is intentionally small.

## Format Id

Use:

- `elara.level_path.v1`

## Core Idea

The format starts from only three primitive ideas:

1. `open space`
2. `closed space`
3. `connecting corridor`

The current schema still uses the foundation shape keys:

1. `square`
2. `cube`
3. `line`

Those should be treated as implementation keys, not the preferred design language.

### Open Space

An open space is an open-sky area.

It is an outdoor or uncovered traversable zone.

Typical uses:

- courtyards
- plazas
- roads
- clearings
- rooftops without enclosure

Current key:

- `shape: "square"`

### Closed Space

A closed space is a covered or enclosed volume.

It is a covered or enclosed volume.

Typical uses:

- rooms
- halls
- caves
- tunnels
- basements

Current key:

- `shape: "cube"`

### Connecting Corridor

A connecting corridor is a passage relationship between two areas.

A line may connect:

- cube to cube
- cube to square
- square to square

The connecting corridor defines the passage path, not the final detailed geometry.

The corridor path may be:

- level
- inclined
- declined
- vertical

This means the same primitive can later become:

- corridor
- ramp
- stair
- ladder shaft
- lift shaft
- bridge

Current key:

- `shape: "line"`

## Passage Size Nodes

A connecting corridor can contain ordered nodes.

Each node carries a square section size for the passage at that point.

This is the key idea:

- the line gives the center path
- the nodes give the passage width/height profile along that path

That allows the generator to widen, narrow, taper, or reshape a passage over
its run without changing the basic path topology.

Examples:

- narrow doorway into wide hall
- sloped mine tunnel that narrows
- vertical shaft with wider landing pocket
- outdoor path transitioning into enclosed corridor

## Top-Level Shape

Current target shape:

- `format`
- `name`
- `units`
- `metadata`
- `areas`
- `passages`
- `anchors`
- `compile`

## Naming

`name` should be the canonical dotted path from the `level_paths` root.

Examples:

- `tutorial.basic.two_room`
- `scenery.village.square_to_house`
- `dungeon.shafts.vertical_core`

## Areas

`areas` is a list of navigable regions.

Each area should include:

- `id`
- `kind`
- `shape`
- `transform`
- optional `tags`
- optional `anchors`

### Area `kind`

Allowed foundation values:

- `open_sky`
- `closed_space`

### Area `shape`

Allowed foundation values:

- `square`
- `cube`

### Square Area

Example:

```json
{
  "id": "yard",
  "kind": "open_sky",
  "shape": "square",
  "size": [8.0, 8.0],
  "transform": {
    "position": [0.0, 0.0, 0.0]
  }
}
```

Interpretation:

- `x/z` footprint
- open above

### Cube Area

Example:

```json
{
  "id": "hall",
  "kind": "closed_space",
  "shape": "cube",
  "size": [6.0, 3.0, 8.0],
  "transform": {
    "position": [10.0, 0.0, 0.0]
  }
}
```

Interpretation:

- width
- height
- depth

## Passages

`passages` is a list of graph edges between areas.

Each passage should include:

- `id`
- `shape`
- `from_area`
- `to_area`
- `path`
- `section_nodes`
- optional `mode`
- optional `tags`

### Passage `shape`

Foundation value:

- `line`

### Passage `mode`

Recommended early values:

- `horizontal`
- `incline`
- `decline`
- `vertical`

These are semantic hints for the generator.

### `path`

`path` is the ordered centerline for the passage.

At minimum it should have:

- `start`
- `end`

Optional intermediate points can be added later.

### `section_nodes`

`section_nodes` is the ordered list of local passage-size controls.

Each node should include:

- `t`
- `shape`
- `size`

Foundation rule:

- `shape` is `square`

`t` is the normalized position along the line from `0.0` to `1.0`.

`size` is the square passage profile at that point.

Example:

```json
"section_nodes": [
  { "t": 0.0, "shape": "square", "size": [1.2, 1.2] },
  { "t": 0.5, "shape": "square", "size": [1.6, 1.6] },
  { "t": 1.0, "shape": "square", "size": [1.2, 1.2] }
]
```

Interpretation:

- square cross-section at start
- wider in the middle
- narrows again at the end

For horizontal passages this may map to width/height.
For vertical passages this may map to shaft width/depth.

## Anchors

`anchors` are optional named attachment points for later composition.

They are useful when:

- one level-path graph plugs into another
- a building entrance must align to a street
- a shaft top must match a surface node

## Compile Section

`compile` stores non-authoritative generation hints.

Examples:

- `default_wall_thickness`
- `default_floor_thickness`
- `generator_profile`
- `passage_style`

These are hints, not topology.

## Validation Rules

Minimum foundation rules:

1. every `area.id` must be unique
2. every `passage.id` must be unique
3. `from_area` and `to_area` must exist
4. area `shape` must be `square` or `cube`
5. passage `shape` must be `line`
6. passage `section_nodes` must be ordered by `t`
7. every section node must use `shape: square`
8. section sizes must be positive
9. vertical passages should still provide square section nodes

## Generator Intent

The generator should treat this format as:

- topology first
- space classification second
- geometry later

That means:

- areas define playable regions
- passages define navigable adjacency
- section nodes define passage envelope

Detailed 3D architecture is a later expansion pass.

## Immediate Next Step

Once this foundation is stable, the next useful additions are:

1. intermediate passage control points
2. door or gate markers on passages
3. stair and ladder style hints
4. area semantic tags like `kitchen`, `street`, `attic`, `yard`
