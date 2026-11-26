# Differential Laboratory

From the `alice2` directory on Windows, run `build.bat --differential` and
`run.bat --differential`. The equivalent Linux commands are
`./build.sh --differential` and `./run.sh --differential`.

The sketch opens on a torus with an angular field. Adjust **Exact**, **Coexact**,
and **Harmonic** to change their contribution to the displayed field. The
diagnostics report the weighted component norms, energy, area-weighted curl and
weak divergence, and the relative error of the best scalar integration of the
weighted field. The weak divergence includes any flux at the boundary. In vector
mode, **Traces** follows the reconstructed per-triangle vectors. Drag to orbit,
press **F** to frame, and press **R** to recompute and reload selected imports.

- **Plane**, **X extrema 0 / 1**, **Scalar** or **Contours** shows the
  Dirichlet solution. The left and right sides are fixed and the other
  sides have natural boundary conditions.
- **Plane**, **Rotational**, **Vectors** shows the target field. As **Rotation**
  increases, the integration residual increases. **Scalar** and **Contours**
  show the best-fit potential when **Natural BC** is selected.
- **Annulus**, **Angular** has one harmonic mode. With **Cut annulus**,
  **Natural BC**, **Exact** set to 1 and **Scalar** selected, the two sides of
  the cut differ by `2π`. **Torus** and **Genus two** have two and four modes.
- **Saddle** updates the Whitney mass matrices as **Saddle bend** changes.
  The signed incidence matrices are reused.
- Place a triangulated `input.obj` and/or a `field.csv` in the executable's
  `data` directory. Select **Import OBJ** or **Import CSV**. The CSV takes one
  finite `x,y,z` vector per triangle in input face order. The solver projects
  those samples onto the Whitney 1-form space. Imported meshes may have
  multiple components and boundaries. They must be orientable triangle
  manifolds with nondegenerate faces and no isolated vertices.

## Library and checks

Link against `alice2::differential` and pass an Alice2 `MeshData` to
`DiscreteExteriorCalculus::build`. `updateMetric` accepts new positions on the
same indexed, ordered faces. The library uses lumped vertex mass, consistent
Whitney edge mass, and inverse face area for integrated face forms. Integration
fixes an additive constant per component. The Hodge decomposition uses the
unrestricted cochain complex and removes one dependent circulation constraint
per closed component. `harmonicBasis()` returns edge cochains orthonormal in the
Whitney mass inner product.

```powershell
cmake -S . -B build/dec-headless -DALICE2_BUILD_VIEWER=OFF -DALICE2_BUILD_DIFFERENTIAL=ON
cmake --build build/dec-headless --config Release --target alice2_differential_checks
ctest --test-dir build/dec-headless -C Release -R differential --output-on-failure
```

Headless builds use a system Eigen3 package or the local `depends/eigen`
headers. Pass `-DALICE2_EIGEN_INCLUDE_DIR=<path>` to point to another Eigen include
directory. The check executable accepts `--benchmark <subdivisions> <regular|irregular>`.
Subdivisions 31, 99, and 315 give 1,024, 10,000, and 99,856 vertices.
The output gives mesh type, vertex count, assembly time, estimated factorisation
time (first decomposition minus cached solve), cached decomposition time, and
peak process working set in MiB. Times are in milliseconds.
