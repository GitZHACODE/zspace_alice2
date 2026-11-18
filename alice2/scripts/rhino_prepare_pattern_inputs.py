"""Prepare colored mesh inputs for Alice2. Rhino RunPythonScript, Python 2.7/3.

Choose Stereotomy or Pattern, select a mesh, and save the USDA file.
Stereotomy: quad mesh; red channel > 0.9 at BOTH edge ends marks a joint.
Use ExistingColors, or JointCurves to mark vertices near selected edge curves.
Pattern: existing mesh vertex RGB; normalized blue controls displacement.
Object/layer/material colors are not vertex colors.

Export uses metres and Rhino's Z-up coordinates, without moving the model.
Alice2 converts this declared frame internally. Export both meshes from the
same document/world placement. Source objects are never changed.
Stereotomy export merges exact coincident vertices only when colors agree.
No remeshing, triangulation, tolerance welding or inferred joint snapping.
"""
from __future__ import print_function
import math
import os
import tempfile
from collections import defaultdict


def finite(value):
    return not math.isnan(value) and not math.isinf(value)


def document_scale(document, rhino):
    # Python bindings expose the reserved enum member None differently
    # (e.g. None_). Inspect the selected value without looking up that member.
    units = document.ModelUnitSystem
    name = units.ToString() if hasattr(units, 'ToString') else str(units)
    name = name.rsplit('.', 1)[-1].rstrip('_').lower()
    if name in ('none', 'unset', 'customunits'):
        raise ValueError('Set explicit document units (e.g. millimetres/metres) first.')
    scale = rhino.RhinoMath.UnitScale(units, rhino.UnitSystem.Meters)
    if not finite(scale) or scale <= 0:
        raise ValueError('Cannot convert the document units to metres.')
    return scale


def prepare_arrays(points, faces, colors, role, metres_per_unit=1.0):
    """Pure export preflight. Inputs are XYZ, index lists, normalized RGB."""
    if role not in ('Stereotomy', 'Pattern'):
        raise ValueError('Choose Stereotomy or Pattern.')
    if not finite(metres_per_unit) or metres_per_unit <= 0:
        raise ValueError('Set a real Rhino document unit system before exporting.')
    if not points or not faces:
        raise ValueError('The mesh has no vertices or faces.')
    if len(colors) != len(points):
        raise ValueError('The mesh needs one vertex color for every vertex. '
                         'Object, layer and material colors do not qualify.')
    for p in points:
        if len(p) != 3 or not all(finite(float(v)) for v in p):
            raise ValueError('The mesh contains invalid coordinates.')
    for c in colors:
        if len(c) != 3 or not all(finite(float(v)) and 0 <= v <= 1 for v in c):
            raise ValueError('Vertex RGB must be normalized to 0..1.')
    for f in faces:
        if len(f) not in (3, 4) or (role == 'Stereotomy' and len(f) != 4):
            raise ValueError('Stereotomy requires an all-quad mesh. '
                             'Pattern meshes may contain triangles or quads.')
        if any(int(v) != v or v < 0 or v >= len(points) for v in f):
            raise ValueError('Invalid face vertex index.')

    # Compact unused stereotomy vertices. Keep ALL pattern vertices because
    # even an unreferenced vertex participates in the nearest-color field.
    used = set(v for f in faces for v in f)
    new_points, new_colors, remap, by_position = [], [], {}, {}
    merged = 0
    for i, p in enumerate(points):
        if role == 'Stereotomy' and i not in used:
            continue
        p, c = tuple(map(float, p)), tuple(map(float, colors[i]))
        if role == 'Stereotomy' and p in by_position:
            target = by_position[p]
            if c != new_colors[target]:
                raise ValueError('Coincident stereotomy vertices have different '
                                 'colors. Make seam colors agree before export.')
            remap[i] = target
            merged += 1
        else:
            remap[i] = len(new_points)
            by_position[p] = remap[i]
            new_points.append(p)
            new_colors.append(c)
    new_faces = [[remap[v] for v in f] for f in faces]
    edges, seen_faces = defaultdict(list), set()
    for fi, f in enumerate(new_faces):
        if len(set(f)) != len(f):
            raise ValueError('A face collapses after exact seam welding.')
        key = tuple(sorted(f))
        if key in seen_faces:
            raise ValueError('The mesh contains duplicate faces.')
        seen_faces.add(key)
        # Reject collinear/zero-area faces without changing any point positions.
        origin = new_points[f[0]]
        area = 0.0
        for j in range(1, len(f)-1):
            a = [new_points[f[j]][k]-origin[k] for k in range(3)]
            b = [new_points[f[j+1]][k]-origin[k] for k in range(3)]
            cross = (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
            area += sum(v*v for v in cross)
        if not finite(area) or area <= 0:
            raise ValueError('The mesh contains a zero-area or invalid face.')
        for a, b in zip(f, f[1:] + f[:1]):
            edges[tuple(sorted((a, b)))].append((fi, a, b))
    components = []
    red_edges = 0
    if role == 'Stereotomy':
        neighbors = [set() for _ in new_faces]
        for (a, b), incident in edges.items():
            if len(incident) > 2:
                raise ValueError('The stereotomy mesh has nonmanifold edges.')
            if len(incident) == 2 and incident[0][1:] == incident[1][1:]:
                raise ValueError('Adjacent faces have inconsistent winding. '
                                 'Unify the mesh normals before exporting.')
            cut = new_colors[a][0] > .9 and new_colors[b][0] > .9
            if cut:
                red_edges += 1
            else:
                for first in incident:
                    for second in incident:
                        if first[0] != second[0]:
                            neighbors[first[0]].add(second[0])
        if not red_edges:
            raise ValueError('No red-marked mesh edges found. Both end vertices '
                             'must have red > 0.9; joint curves must follow edges.')
        unseen = set(range(len(new_faces)))
        while unseen:
            stack, size = [min(unseen)], 0
            unseen.remove(stack[0])
            while stack:
                fi = stack.pop()
                size += 1
                for next_face in neighbors[fi]:
                    if next_face in unseen:
                        unseen.remove(next_face)
                        stack.append(next_face)
            components.append(size)
        if max(components) == 1:
            raise ValueError('Every face is isolated by red marking. Use black '
                             'away from joints; white/yellow also have high red.')
    scaled = [tuple(v * metres_per_unit for v in p) for p in new_points]
    if any(not finite(v) or abs(v) > 3.4e38 for p in scaled for v in p):
        raise ValueError('Coordinates exceed the USD point3f range.')
    report = dict(vertices=len(scaled), faces=len(new_faces),
                  merged_exact_vertices=merged, unused_vertices=len(points)-len(used),
                  boundary_edges=sum(len(v) == 1 for v in edges.values()),
                  red_edges=red_edges, region_face_counts=sorted(components, reverse=True))
    return scaled, new_faces, new_colors, report


def usda_text(points, faces, colors, role):
    number = lambda v: format(float(v), '.17g')
    triples = lambda values: '[' + ', '.join('(' + ', '.join(number(v) for v in p) + ')' for p in values) + ']'
    integers = lambda values: '[' + ', '.join(str(int(v)) for v in values) + ']'
    return ('#usda 1.0\n(\n    defaultPrim = "Input"\n'
            '    metersPerUnit = 1\n    upAxis = "Z"\n)\n'
            'def Mesh "Input" {\n'
            '    point3f[] points = ' + triples(points) + '\n'
            '    int[] faceVertexCounts = ' + integers(map(len, faces)) + '\n'
            '    int[] faceVertexIndices = ' + integers(v for f in faces for v in f) + '\n'
            '    color3f[] primvars:vertexColors = ' + triples(colors) +
            ' (\n        interpolation = "vertex"\n    )\n'
            '    color3f[] primvars:displayColor = ' + triples(colors) +
            ' (\n        interpolation = "vertex"\n    )\n'
            '    uniform token subdivisionScheme = "none"\n'
            '    uniform token orientation = "rightHanded"\n'
            '    custom string alice2:inputRole = "' + role + '"\n}\n')


def save_atomic(path, content):
    """Stage beside destination, then replace; never delete the old file first."""
    path = os.path.abspath(path)
    if not path.lower().endswith('.usda'):
        raise ValueError('Choose a .usda filename.')
    fd, temp = tempfile.mkstemp(prefix='.alice2_', suffix='.tmp', dir=os.path.dirname(path))
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(content.encode('ascii'))
            stream.flush()
            os.fsync(stream.fileno())
        if hasattr(os, 'replace'):
            os.replace(temp, path)
        elif os.path.exists(path):
            from System.IO import File
            File.Replace(temp, path, None)
        else:
            os.rename(temp, path)
    finally:
        if os.path.exists(temp):
            os.remove(temp)


def _option(prompt, choices):
    import Rhino
    getter = Rhino.Input.Custom.GetOption()
    try:
        getter.SetCommandPrompt(prompt + ' (Enter = ' + choices[0] + ')')
        indices = dict((getter.AddOption(s), s) for s in choices)
        getter.AcceptNothing(True)
        result = getter.Get()
        if result == Rhino.Input.GetResult.Nothing:
            return choices[0]
        if result == Rhino.Input.GetResult.Option:
            return indices[getter.OptionIndex()]
        return None
    finally:
        getter.Dispose()


def _mesh():
    import Rhino
    getter = Rhino.Input.Custom.GetObject()
    try:
        getter.SetCommandPrompt('Select the mesh to prepare for Alice2')
        getter.GeometryFilter = Rhino.DocObjects.ObjectType.Mesh
        getter.SubObjectSelect = False
        getter.Get()
        if getter.CommandResult() != Rhino.Commands.Result.Success:
            return None
        return getter.Object(0).Mesh().DuplicateMesh()
    finally:
        getter.Dispose()


def _joint_colors(points, tolerance_default):
    import Rhino
    getter = Rhino.Input.Custom.GetObject()
    try:
        getter.SetCommandPrompt('Select joint curves following the mesh edges; Enter to finish')
        getter.GeometryFilter = Rhino.DocObjects.ObjectType.Curve
        getter.SubObjectSelect = False
        getter.EnablePreSelect(False, True)
        getter.GetMultiple(1, 0)
        if getter.CommandResult() != Rhino.Commands.Result.Success:
            return None
        curves = [getter.Object(i).Curve().DuplicateCurve() for i in range(getter.ObjectCount)]
    finally:
        getter.Dispose()
    number = Rhino.Input.Custom.GetNumber()
    try:
        number.SetCommandPrompt('Vertex-to-joint tolerance in Rhino document units')
        number.SetDefaultNumber(tolerance_default)
        number.SetLowerLimit(0, True)
        number.Get()
        if number.CommandResult() != Rhino.Commands.Result.Success:
            for curve in curves:
                curve.Dispose()
            return None
        tolerance = number.Number()
    finally:
        number.Dispose()
    marked = set()
    try:
        for ci, curve in enumerate(curves):
            hits = 0
            for i, p in enumerate(points):
                point = Rhino.Geometry.Point3d(*p)
                success, t = curve.ClosestPoint(point)
                if success and point.DistanceTo(curve.PointAt(t)) <= tolerance:
                    marked.add(i)
                    hits += 1
            if hits < 2:
                raise ValueError('Joint curve {0} reaches fewer than two mesh vertices. '
                                 'Use curves along mesh edges or adjust the tolerance.'.format(ci+1))
    finally:
        for curve in curves:
            curve.Dispose()
    return [(1., 0., 0.) if i in marked else (0., 0., 0.) for i in range(len(points))]


def main():
    import Rhino
    doc = globals().get('__rhino_doc__') or Rhino.RhinoDoc.ActiveDoc
    if doc is None:
        return
    try:
        role = _option('Alice2 input type', ['Stereotomy', 'Pattern'])
        if role is None:
            return
        scale = document_scale(doc, Rhino)
        mesh = _mesh()
        if mesh is None:
            return
        try:
            points = []
            for i in range(mesh.Vertices.Count):
                p = mesh.Vertices.Point3dAt(i)
                points.append((p.X, p.Y, p.Z))
            faces = []
            for f in mesh.Faces:
                faces.append([f.A, f.B, f.C, f.D] if f.IsQuad else [f.A, f.B, f.C])
            colors = [(c.R/255., c.G/255., c.B/255.) for c in mesh.VertexColors]
            if role == 'Stereotomy':
                choices = ['ExistingColors', 'JointCurves'] if len(colors) == len(points) else ['JointCurves', 'ExistingColors']
                mode = _option('Joint marking', choices)
                if mode is None:
                    return
                if mode == 'JointCurves':
                    colors = _joint_colors(points, max(doc.ModelAbsoluteTolerance, 1e-9))
                    if colors is None:
                        return
            data = prepare_arrays(points, faces, colors, role, scale)
        finally:
            mesh.Dispose()
        points, faces, colors, report = data
        Rhino.RhinoApp.WriteLine('Ready: {0} vertices, {1} faces; {2} exact seam vertices merged.'
                                .format(report['vertices'], report['faces'], report['merged_exact_vertices']))
        if role == 'Stereotomy':
            sizes = report['region_face_counts']
            Rhino.RhinoApp.WriteLine('Red edges: {0}; substantial regions: {1}; isolated painted faces: {2}.'
                                    .format(report['red_edges'], sum(n > 1 for n in sizes), sizes.count(1)))
            Rhino.RhinoApp.WriteLine('Topology slicing still requires continuous quad columns and suitable joint loops; '
                                    'this export check does not certify every region as sliceable.')
        dialog = Rhino.UI.SaveFileDialog()
        dialog.Title = 'Save Alice2 ' + role + ' input'
        dialog.Filter = 'USD ASCII (*.usda)|*.usda'
        dialog.DefaultExt = 'usda'
        dialog.FileName = 'sterotomy.usda' if role == 'Stereotomy' else 'patterns.usda'
        if doc.Path:
            dialog.InitialDirectory = os.path.dirname(doc.Path)
        if not dialog.ShowSaveDialog():
            return
        path = dialog.FileName
        if not path.lower().endswith('.usda'):
            path += '.usda'
        save_atomic(path, usda_text(points, faces, colors, role))
        Rhino.RhinoApp.WriteLine('Saved ' + path)
        Rhino.RhinoApp.WriteLine('Metres, Z-up; original world placement retained. Source mesh unchanged.')
    except Exception as error:
        Rhino.RhinoApp.WriteLine('Alice2 export stopped: ' + str(error))
        Rhino.UI.Dialogs.ShowMessageBox(str(error), 'Alice2 mesh preparation')


if __name__ == '__main__':
    main()
