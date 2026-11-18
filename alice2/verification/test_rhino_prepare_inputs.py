"""No Rhino required: exporter geometry, colors, units and safe-save checks."""
import importlib.util
import pathlib
import tempfile
import unittest
from types import SimpleNamespace

script = pathlib.Path(__file__).parents[1] / 'scripts/rhino_prepare_pattern_inputs.py'
spec = importlib.util.spec_from_file_location('rhino_export', script)
export = importlib.util.module_from_spec(spec)
spec.loader.exec_module(export)


class ExportTests(unittest.TestCase):
    def test_units_without_python_reserved_enum_member(self):
        rhino = SimpleNamespace(
            UnitSystem=SimpleNamespace(Meters='Meters'),
            RhinoMath=SimpleNamespace(UnitScale=lambda source, target: .001))
        doc = SimpleNamespace(ModelUnitSystem='Millimeters')
        self.assertEqual(export.document_scale(doc, rhino), .001)
        for value in ('None', 'None_', 'UnitSystem.None_', 'Unset', 'CustomUnits'):
            doc.ModelUnitSystem = value
            with self.assertRaises(ValueError):
                export.document_scale(doc, rhino)

    def setUp(self):
        self.points = [(0, 0, 0), (1000, 0, 0), (1000, 1000, 0),
                       (0, 1000, 0), (2000, 0, 0), (2000, 1000, 0)]
        self.faces = [[0, 1, 2, 3], [1, 4, 5, 2]]
        self.colors = [(0., 0., 0.), (1., 0., 0.), (1., 0., 0.),
                       (0., 0., 0.), (0., 0., 0.), (0., 0., 0.)]

    def test_units_and_red_regions(self):
        points = [(x*1000, y*1000, 0) for y in range(3) for x in range(3)]
        faces = [[0, 1, 4, 3], [1, 2, 5, 4], [3, 4, 7, 6], [4, 5, 8, 7]]
        colors = [(1., 0., 0.) if i in (1, 4, 7) else (0., 0., 0.) for i in range(9)]
        p, f, c, report = export.prepare_arrays(points, faces, colors, 'Stereotomy', .001)
        self.assertEqual(p[-1], (2., 2., 0.))
        self.assertEqual(report['red_edges'], 2)
        self.assertEqual(report['region_face_counts'], [2, 2])
        points.append(points[4]); colors.append(colors[4]); faces[1][3] = 9
        welded = export.prepare_arrays(points, faces, colors, 'Stereotomy', .001)
        self.assertEqual(welded[:3], (p, f, c))
        self.assertEqual(welded[3]['merged_exact_vertices'], 1)

    def test_pattern_keeps_color_seams(self):
        p = self.points + [self.points[1]]
        faces = [self.faces[0], [6, 4, 5, 2]]
        colors = self.colors + [(0., 0., .5)]
        result = export.prepare_arrays(p, faces, colors, 'Pattern', .001)
        self.assertEqual(len(result[0]), 7)
        self.assertEqual(result[2][-1], (0., 0., .5))
        loose = export.prepare_arrays(p+[(500, 500, 1)], faces,
                                      colors+[(0., 0., 1.)], 'Pattern', .001)
        self.assertEqual(len(loose[0]), 8)
        with self.assertRaisesRegex(ValueError, 'different colors'):
            export.prepare_arrays(p, faces, colors, 'Stereotomy', .001)

    def test_rejects_invalid_geometry_and_colors(self):
        for faces, colors, role in [([[0, 1, 2]], self.colors, 'Stereotomy'),
                                     (self.faces, [], 'Pattern'),
                                     ([self.faces[0]]*2, self.colors, 'Pattern'),
                                     ([[0, 1, 4]], self.colors, 'Pattern'),
                                     (self.faces, [(1., 1., 1.)]*6, 'Stereotomy')]:
            with self.assertRaises(ValueError):
                export.prepare_arrays(self.points, faces, colors, role)

    def test_mesh_colors_and_atomic_save(self):
        p, f, c, report = export.prepare_arrays(self.points, self.faces, self.colors, 'Pattern', .001)
        text = export.usda_text(p, f, c, 'Pattern')
        self.assertIn('upAxis = "Z"', text)
        self.assertIn('metersPerUnit = 1', text)
        self.assertIn('primvars:vertexColors', text)
        self.assertIn('interpolation = "vertex"', text)
        self.assertNotIn('xformOp:', text)
        with tempfile.TemporaryDirectory(dir=str(script.parent)) as folder:
            path = pathlib.Path(folder) / 'pattern.usda'
            export.save_atomic(str(path), 'previous')
            export.save_atomic(str(path), text)
            self.assertEqual(path.read_text(), text)
            self.assertEqual(len(list(path.parent.iterdir())), 1)


if __name__ == '__main__':
    unittest.main()
