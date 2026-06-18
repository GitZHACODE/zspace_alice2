# alice2 Agent

The `alice2_agent` is responsible for sketch structure, display, UI flow, and zSpace integration inside alice2.

## Skills

- Understand `alice2::ISketch` lifecycle: `setup`, `update`, `draw`, `cleanup`, and input callbacks.
- Place generated sketches under `alice2/userSrc/`, preferably `alice2/userSrc/zspace/` for zSpace sketches.
- Keep one active sketch with `#define __MAIN__` unless the user asks otherwise.
- Use alice2 rendering through `scene().draw(...)`.
- Use `zDisplayMeshSetting`, `zDisplayGraphSetting`, and `zDisplayPointCloudSetting` for optional display settings.
- Use lowercase `z` for zSpace-related alice2 wrapper/sketch names to match the zSpace library style.
- Use lowercase `z` for related filenames too: `zDisplaySettings.h`, `zSpaceDraw.h/.cpp`, and `zSpaceObject.h/.cpp`.

## Public Sketch API

Keep user-facing sketches simple:

```cpp
zSpace::zObjectMesh mesh;
zSpace::zFnMesh fn(mesh);

scene().draw(mesh);
```

With optional display settings:

```cpp
alice2::zDisplayMeshSetting display;
display.showEdges = true;
display.edgeWidth = 2.0f;
scene().draw(mesh, display);
```

Do not expose adapter or conversion internals to sketch users.

## Include Order

For zSpace sketches, prefer:

```cpp
#include <zspace/interface.h>
#include <alice2.h>
```

This avoids known third-party include conflicts.

## Collaboration

- Ask `zspace_agent` for API and topology details.
- Ask `code_agent` to extract reusable methods when sketch logic grows.
- Hand build verification to `build_agent`.
