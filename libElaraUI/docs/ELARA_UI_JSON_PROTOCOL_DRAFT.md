# Elara UI JSON Protocol Draft v1

The JSON file is a portable layout/control descriptor. It should describe *what exists* and *how it is configured*, while widget classes keep ownership of rendering, events, data normalization, and domain logic.

## Top-level shape

```json
{
  "elara_ui_protocol": 1,
  "window": {
    "title": "libElaraUI Demo",
    "width": 800,
    "height": 600,
    "backend_id": "org.elara.ui.demo"
  },
  "theme": {
    "mode": "light"
  },
  "root": {
    "content": "demo.tabs",
    "popup": "demo.popup"
  },
  "widgets": []
}
```

## Widget descriptor

```json
{
  "id": "demo.density",
  "type": "elara.widgets.density_map",
  "properties": {},
  "children": []
}
```

- `id` is the external string representation of the widget handle.
- `type` is the factory key.
- `properties` are interpreted by the widget/factory, not by the generic loader.
- `children` are optional and only meaningful to container/layout widgets.

## Tabs

```json
{
  "id": "demo.tabs",
  "type": "elara.widgets.tabs",
  "tabs": [
    {
      "title": "Density map",
      "widget": {
        "id": "demo.density",
        "type": "elara.widgets.density_map"
      }
    }
  ]
}
```

## Grid layout

```json
{
  "id": "demo.grid",
  "type": "elara.layouts.grid",
  "columns": [
    { "mode": "exact", "size": 24 },
    { "mode": "fill" }
  ],
  "rows": [
    { "mode": "exact", "size": 46 },
    { "mode": "fill" }
  ],
  "children": [
    {
      "id": "demo.button",
      "type": "elara.widgets.button",
      "cell": { "column": 0, "row": 0, "column_span": 1, "row_span": 1 },
      "properties": { "text": "Press Me", "action": "grid.demo.press" }
    }
  ]
}
```

## Density map

```json
{
  "id": "demo.density",
  "type": "elara.widgets.density_map",
  "properties": {
    "base_capacity": 8,
    "capacity_multiplier": 2,
    "layer_count": 16
  },
  "demo_data": {
    "type": "modulo_sequence",
    "sample_count": 65536,
    "sample_multiplier": 2
  }
}
```

Runtime RPC should use the same `id` and `type` language:

```json
{
  "op": "widget.update",
  "target": "demo.density",
  "data": {
    "sample": 123456
  }
}
```
