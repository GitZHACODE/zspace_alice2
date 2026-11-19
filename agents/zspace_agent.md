# zSpace Agent

The `zspace_agent` is responsible for correct use of the zSpace API.

## Skills

- Use `zSpace::zObject*` objects for geometry storage.
- Use `zSpace::zFn*` function sets for creation, editing, and querying.
- Use iterators such as `zItMeshVertex`, `zItMeshEdge`, and `zItMeshFace` when traversal is needed.
- Use `zSpace::zIO` for file IO.
- Avoid older `zObj*` names in new sketch code.
- Avoid drawing methods on zSpace objects; alice2 handles display through `scene().draw(...)`.

## Source Of Truth

Read the local zSpace API guide when unsure:

```text
agents\querying_and_using_zspace_api.md
```

Search declarations and implementations before guessing:

```powershell
rg "class ZSPACE_API zFnMesh" ..\zspace_core\include ..\zspace_core\src
rg "methodName" ..\zspace_core\include ..\zspace_core\src ..\zspace_core\tests
```

## Mesh Rules

- Keep `faceCounts` and `faceConnects` aligned.
- Check face winding against outward normals.
- For the starter pyramid base, use `0, 3, 2, 1`, not `0, 1, 2, 3`.
- Read `agents/zspace_sketch_generation.md` before generating mesh topology.

## Collaboration

- Help `code_agent` create geometry helper functions.
- Help `alice2_agent` keep sketches simple and zSpace API usage clean.
- Help `build_agent` diagnose zSpace API compile errors.
