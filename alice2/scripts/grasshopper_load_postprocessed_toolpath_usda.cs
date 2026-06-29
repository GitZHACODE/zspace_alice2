// Grasshopper Script Instance
#region Usings
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text.RegularExpressions;

using Rhino;
using Rhino.Geometry;

using Grasshopper;
using Grasshopper.Kernel;
using Grasshopper.Kernel.Data;
using Grasshopper.Kernel.Types;
#endregion

public class Script_Instance : GH_ScriptInstance
{
private void RunScript(
    string path,
    ref object points,
    ref object pointViz,
    ref object normals,
    ref object printHeight,
    ref object printWidth,
    ref object flags,
    ref object inputMesh,
    ref object contours,
    ref object printMeshes,
    ref object report)
{
  var pointTree = new DataTree<Point3d>();
  var pointVizTree = new DataTree<Mesh>();
  var normalTree = new DataTree<Vector3d>();
  var heightTree = new DataTree<double>();
  var widthTree = new DataTree<double>();
  var flagTree = new DataTree<int>();
  var contourTree = new DataTree<Curve>();
  var printMeshTree = new DataTree<Mesh>();
  Mesh loadedInputMesh = new Mesh();

  if (string.IsNullOrWhiteSpace(path) || !File.Exists(path))
  {
    report = "USD file not found: " + path;
    points = pointTree;
    pointViz = pointVizTree;
    normals = normalTree;
    printHeight = heightTree;
    printWidth = widthTree;
    flags = flagTree;
    inputMesh = loadedInputMesh;
    contours = contourTree;
    printMeshes = printMeshTree;
    return;
  }

  string text = File.ReadAllText(path);
  string inputMeshBody = ExtractPrimBody(text, "Mesh", "inputMesh", 0);
  if (!string.IsNullOrWhiteSpace(inputMeshBody)) loadedInputMesh = ParseMesh(inputMeshBody);

  List<PrimBlock> layerBlocks = ExtractLayerBlocks(text);
  int totalSamples = 0;
  int totalContours = 0;
  int totalPrintMeshes = 0;

  foreach (PrimBlock layerBlock in layerBlocks)
  {
    GH_Path ghPath = new GH_Path(layerBlock.Index);
    List<Point3d> layerPoints = ParsePointArray(layerBlock.Body, "targetPoints");
    foreach (Point3d p in layerPoints)
    {
      pointTree.Add(p, ghPath);
      pointVizTree.Add(CreatePointVizMesh(p, 0.01), ghPath);
    }
    foreach (Vector3d n in ParseVectorArray(layerBlock.Body, "normals")) normalTree.Add(n, ghPath);
    foreach (double h in ParseDoubleArray(layerBlock.Body, "printHeights")) heightTree.Add(h, ghPath);
    foreach (double w in ParseDoubleArray(layerBlock.Body, "printWidths")) widthTree.Add(w, ghPath);
    foreach (int f in ParseIntArray(layerBlock.Body, "featureFlags")) flagTree.Add(f, ghPath);

    string contourBody = ExtractPrimBody(layerBlock.Body, "BasisCurves", "contour", 0);
    foreach (Curve c in ParseBasisCurves(contourBody))
    {
      contourTree.Add(c, ghPath);
      totalContours++;
    }

    string printMeshBody = ExtractPrimBody(layerBlock.Body, "Mesh", "printMesh", 0);
    if (!string.IsNullOrWhiteSpace(printMeshBody))
    {
      Mesh mesh = ParseMesh(printMeshBody);
      if (mesh.Vertices.Count > 0)
      {
        printMeshTree.Add(mesh, ghPath);
        totalPrintMeshes++;
      }
    }

    totalSamples += layerPoints.Count;
  }

  points = pointTree;
  pointViz = pointVizTree;
  normals = normalTree;
  printHeight = heightTree;
  printWidth = widthTree;
  flags = flagTree;
  inputMesh = loadedInputMesh;
  contours = contourTree;
  printMeshes = printMeshTree;
  report = "Loaded layers=" + layerBlocks.Count
    + " samples=" + totalSamples
    + " contours=" + totalContours
    + " printMeshes=" + totalPrintMeshes
    + " inputMeshVertices=" + loadedInputMesh.Vertices.Count;
}

private struct PrimBlock
{
  public int Index;
  public string Body;
}

private static List<PrimBlock> ExtractLayerBlocks(string text)
{
  var result = new List<PrimBlock>();
  foreach (Match m in Regex.Matches(text, "def\\s+Xform\\s+\"layer_(\\d+)\"\\s*\\{"))
  {
    int openBrace = text.IndexOf('{', m.Index);
    if (openBrace < 0) continue;
    int closeBrace = FindMatchingBrace(text, openBrace);
    if (closeBrace < 0) continue;
    result.Add(new PrimBlock {
      Index = int.Parse(m.Groups[1].Value, CultureInfo.InvariantCulture),
      Body = text.Substring(openBrace + 1, closeBrace - openBrace - 1)
    });
  }
  return result;
}

private static string ExtractPrimBody(string text, string primType, string primName, int startIndex)
{
  Match m = Regex.Match(
    text.Substring(Math.Max(0, startIndex)),
    "def\\s+" + Regex.Escape(primType) + "\\s+\"" + Regex.Escape(primName) + "\"\\s*\\{");
  if (!m.Success) return "";

  int absoluteIndex = Math.Max(0, startIndex) + m.Index;
  int openBrace = text.IndexOf('{', absoluteIndex);
  if (openBrace < 0) return "";
  int closeBrace = FindMatchingBrace(text, openBrace);
  if (closeBrace < 0) return "";
  return text.Substring(openBrace + 1, closeBrace - openBrace - 1);
}

private static int FindMatchingBrace(string text, int openBrace)
{
  int depth = 0;
  for (int i = openBrace; i < text.Length; i++)
  {
    if (text[i] == '{') depth++;
    else if (text[i] == '}')
    {
      depth--;
      if (depth == 0) return i;
    }
  }
  return -1;
}

private static Mesh ParseMesh(string body)
{
  Mesh mesh = new Mesh();
  List<Point3d> pts = ParsePointArray(body, "points");
  List<int> counts = ParseIntArray(body, "faceVertexCounts");
  List<int> indices = ParseIntArray(body, "faceVertexIndices");
  foreach (Point3d p in pts) mesh.Vertices.Add(p);

  int cursor = 0;
  foreach (int count in counts)
  {
    if (cursor + count > indices.Count) break;
    if (count == 3) mesh.Faces.AddFace(indices[cursor], indices[cursor + 1], indices[cursor + 2]);
    else if (count == 4) mesh.Faces.AddFace(indices[cursor], indices[cursor + 1], indices[cursor + 2], indices[cursor + 3]);
    else if (count > 4)
    {
      for (int i = 1; i + 1 < count; i++) {
        mesh.Faces.AddFace(indices[cursor], indices[cursor + i], indices[cursor + i + 1]);
      }
    }
    cursor += count;
  }

  mesh.Normals.ComputeNormals();
  mesh.Compact();
  return mesh;
}

private static List<Curve> ParseBasisCurves(string body)
{
  var curves = new List<Curve>();
  if (string.IsNullOrWhiteSpace(body)) return curves;

  List<Point3d> pts = ParsePointArray(body, "points");
  List<int> counts = ParseIntArray(body, "curveVertexCounts");
  int cursor = 0;
  foreach (int count in counts)
  {
    if (count < 2 || cursor + count > pts.Count) break;
    var polylinePoints = new List<Point3d>();
    for (int i = 0; i < count; i++) polylinePoints.Add(pts[cursor + i]);
    curves.Add(new PolylineCurve(polylinePoints));
    cursor += count;
  }
  return curves;
}

private static Mesh CreatePointVizMesh(Point3d p, double radius)
{
  Mesh mesh = Mesh.CreateFromSphere(new Sphere(p, radius), 8, 4);
  mesh.Normals.ComputeNormals();
  mesh.Compact();
  return mesh;
}

private static List<Point3d> ParsePointArray(string body, string name)
{
  var result = new List<Point3d>();
  foreach (var v in ParseTuple3Array(body, name)) result.Add(new Point3d(v[0], v[1], v[2]));
  return result;
}

private static List<Vector3d> ParseVectorArray(string body, string name)
{
  var result = new List<Vector3d>();
  foreach (var v in ParseTuple3Array(body, name)) result.Add(new Vector3d(v[0], v[1], v[2]));
  return result;
}

private static List<double[]> ParseTuple3Array(string body, string name)
{
  var result = new List<double[]>();
  string array = ExtractArray(body, name);
  if (string.IsNullOrWhiteSpace(array)) return result;

  foreach (Match m in Regex.Matches(array, "\\(([^\\)]*)\\)"))
  {
    string[] parts = m.Groups[1].Value.Split(',');
    if (parts.Length < 3) continue;
    result.Add(new double[] {
      ParseDouble(parts[0]),
      ParseDouble(parts[1]),
      ParseDouble(parts[2])
    });
  }
  return result;
}

private static List<double> ParseDoubleArray(string body, string name)
{
  var result = new List<double>();
  string array = ExtractArray(body, name);
  if (string.IsNullOrWhiteSpace(array)) return result;
  foreach (string token in array.Split(','))
  {
    if (string.IsNullOrWhiteSpace(token)) continue;
    result.Add(ParseDouble(token));
  }
  return result;
}

private static List<int> ParseIntArray(string body, string name)
{
  var result = new List<int>();
  string array = ExtractArray(body, name);
  if (string.IsNullOrWhiteSpace(array)) return result;
  foreach (string token in array.Split(','))
  {
    if (string.IsNullOrWhiteSpace(token)) continue;
    result.Add(int.Parse(token.Trim(), CultureInfo.InvariantCulture));
  }
  return result;
}

private static string ExtractArray(string body, string name)
{
  Match m = Regex.Match(
    body,
    "\\b" + Regex.Escape(name) + "\\s*=\\s*\\[(.*?)\\]",
    RegexOptions.Singleline);
  return m.Success ? m.Groups[1].Value : "";
}

private static double ParseDouble(string token)
{
  return double.Parse(token.Trim(), CultureInfo.InvariantCulture);
}

}
