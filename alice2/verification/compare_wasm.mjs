// Usage: node compare_wasm.mjs VIEWER_ROOT NATIVE_REPORT.json [...]
// Consumes an independently built runtime; does not rebuild or change the viewer.
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {createRequire} from 'node:module';
import {resolve,join} from 'node:path';
import {pathToFileURL} from 'node:url';
const [viewerArg,...reports]=process.argv.slice(2);
assert(viewerArg && reports.length,'Usage: node compare_wasm.mjs VIEWER_ROOT NATIVE_REPORT.json [...]');
const viewer=resolve(viewerArg);
const require=createRequire(join(viewer,'package.json'));
const {build}=require('esbuild');
const bundle=await build({stdin:{contents:"export * from './src/kernels/slicingWasmSession';",resolveDir:viewer},bundle:true,write:false,platform:'node',format:'esm'});
const {SlicingWasmSession}=await import('data:text/javascript;base64,'+Buffer.from(bundle.outputFiles[0].text).toString('base64'));
const factory=(await import(pathToFileURL(join(viewer,'wasm/zspace_slicing.js')).href)).default;
const module=await factory({wasmBinary:await readFile(join(viewer,'wasm/zspace_slicing.wasm')),print(){}});
function normalizedEdges(value) {
  const pairs=[];
  for(let i=0;i<value.length;i+=2) pairs.push([Math.min(value[i],value[i+1]),Math.max(value[i],value[i+1])]);
  return pairs.sort((a,b)=>a[0]-b[0] || a[1]-b[1]).flat();
}
// TetGen can triangulate the same slice differently across native and WASM.
// Compare represented surfaces and boundaries, not internal vertex IDs. Paths
// and all print attributes below still retain their strict indexed comparison.
const sub=(a,b)=>a.map((v,i)=>v-b[i]);
const dot=(a,b)=>a.reduce((s,v,i)=>s+v*b[i],0);
const cross=(a,b)=>[a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]];
const norm=a=>Math.hypot(...a);
const midpoint=(a,b)=>a.map((v,i)=>(v+b[i])/2);
function segmentDistance(p,a,b) {
  const ab=sub(b,a),den=dot(ab,ab),t=den?Math.max(0,Math.min(1,dot(sub(p,a),ab)/den)):0;
  return norm(sub(p,a.map((v,i)=>v+t*ab[i])));
}
function triangleDistance(p,[a,b,c]) {
  const ab=sub(b,a),ac=sub(c,a),n=cross(ab,ac),nn=dot(n,n);
  if(nn>0) {
    const ap=sub(p,a),v=dot(cross(ap,ac),n)/nn,w=dot(cross(ab,ap),n)/nn;
    if(v>=0 && w>=0 && v+w<=1) return Math.abs(dot(ap,n))/Math.sqrt(nn);
  }
  return Math.min(segmentDistance(p,a,b),segmentDistance(p,b,c),segmentDistance(p,c,a));
}
function surface(mesh) {
  const values=mesh.vertexPositions;assert(values.length%3===0 && values.every(Number.isFinite),'Invalid slice points');
  const vertices=Array.from({length:values.length/3},(_,i)=>values.slice(i*3,i*3+3));
  const triangles=[],edges=new Map();let offset=0,area=0;
  for(const count of mesh.faceCounts) {
    assert.equal(count,3,'Harmonic slice must contain triangles');
    const ids=mesh.faceConnects.slice(offset,offset+3);offset+=3;
    assert(ids.every(i=>Number.isInteger(i)&&i>=0&&i<vertices.length),'Invalid triangle index');
    const triangle=ids.map(i=>vertices[i]);triangles.push(triangle);
    area+=norm(cross(sub(triangle[1],triangle[0]),sub(triangle[2],triangle[0])))/2;
    for(let i=0;i<3;++i) {
      const a=ids[i],b=ids[(i+1)%3],key=[Math.min(a,b),Math.max(a,b)].join(',');
      const e=edges.get(key);if(e)e.count++;else edges.set(key,{count:1,a:vertices[a],b:vertices[b]});
    }
  }
  assert.equal(offset,mesh.faceConnects.length);assert(area>0,'Empty slice surface');
  assert([...edges.values()].every(e=>e.count<=2),'Nonmanifold slice');
  const boundary=[...edges.values()].filter(e=>e.count===1).map(e=>[e.a,e.b]);
  assert(boundary.length>0,'Missing slice boundary');
  const samples=[...vertices,...triangles.map(t=>t[0].map((v,i)=>(v+t[1][i]+t[2][i])/3))];
  const boundarySamples=boundary.flatMap(([a,b])=>[a,b,midpoint(a,b)]);
  return {vertices,triangles,area,boundary,samples,boundarySamples};
}
function compareHarmonicSurface(expected,actual,label) {
  const a=surface(expected),b=surface(actual);
  const scale=Math.max(...a.vertices.flat().map(Math.abs),...b.vertices.flat().map(Math.abs));
  const tolerance=1e-4+1e-5*scale;
  assert(Math.abs(a.area-b.area)<=1e-4+1e-5*Math.max(a.area,b.area),label+' surface area differs');
  let maxSurface=0,maxBoundary=0;
  for(const [from,to] of [[a,b],[b,a]]) {
    for(const p of from.samples) maxSurface=Math.max(maxSurface,Math.min(...to.triangles.map(t=>triangleDistance(p,t))));
    for(const p of from.boundarySamples) maxBoundary=Math.max(maxBoundary,Math.min(...to.boundary.map(([u,v])=>segmentDistance(p,u,v))));
  }
  assert(maxSurface<=tolerance,`${label} surface distance ${maxSurface} > ${tolerance}`);
  assert(maxBoundary<=tolerance,`${label} boundary distance ${maxBoundary} > ${tolerance}`);
  return {maxSurface,maxBoundary,areaDifference:Math.abs(a.area-b.area)};
}
for(const reportPath of reports) {
  const report=JSON.parse(await readFile(reportPath,'utf8'));
  const session=new SlicingWasmSession(module);
  let comparisons=0,maxAbs=0; const surfaceChecks=[];
  function compare(expected,actual,path='result') {
    if(report.result.mode===1 && path.endsWith('.slice')) {surfaceChecks.push(compareHarmonicSurface(expected,actual,path));return;}
    if(path.endsWith('.edgeConnects')) {assert.deepEqual(normalizedEdges(actual),normalizedEdges(expected),path);return;}
    if(Array.isArray(expected)) {
      assert.equal(actual.length,expected.length,path+' length');
      expected.forEach((value,index)=>compare(value,actual[index],`${path}[${index}]`));return;
    }
    if(expected!==null && typeof expected==='object') {
      for(const key of Object.keys(expected)) compare(expected[key],actual[key],`${path}.${key}`); return;
    }
    if(typeof expected==='number') {
      assert(Number.isFinite(actual),path+' is not finite');
      if(/faceCounts|faceConnects|trackIds|\.mode$/.test(path)) {assert.equal(actual,expected,path);return;}
      const difference=Math.abs(expected-actual),tolerance=1e-4+1e-5*Math.max(Math.abs(expected),Math.abs(actual));
      assert(difference<=tolerance,`${path}: native=${expected}, WASM=${actual}, difference=${difference}, tolerance=${tolerance}`);
      ++comparisons;maxAbs=Math.max(maxAbs,difference);return;
    }
    assert.equal(actual,expected,path);
  }
  try {
    const actual=session.compute(report.input);
    compare(report.result,actual);
    console.log(`PASS ${reportPath}: ${comparisons} numeric comparisons; maximum absolute difference ${maxAbs}`);
    if(surfaceChecks.length) console.log("Harmonic surface equivalence:",JSON.stringify(surfaceChecks));
  } finally {session.dispose();}
}

