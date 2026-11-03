"""Independent source invariants and exact outer bead-frame comparison."""
import json
from pathlib import Path
p=Path('build_seam_side/review')
def read(path):return json.loads(Path(path).read_text())
base=read('build_bidirectional/review/desktop-original-harmonic-run0.json')
h=read(p/'resolved-harmonic-run0.json');f=read(p/'resolved-full-run0.json');seed=read(p/'resolved-full-run0-seed.json')
report={'harmonicPreservation':{k:h[k]==base[k] for k in ['inputSourceMesh','sections','bracing2D']},'fullSourceUnchanged':f['inputSourceMesh']==base['inputSourceMesh'],'fullBracesUnchanged':f['bracing2D']==seed['bracing2D'],'fullCapsUnchanged':all(f['sections'][i]==seed['sections'][i] for i in [0,-1]),'outerFramesExact':True,'outerNeighbourFramesExact':True,'checkedOuterTargets':0}
report['harmonicPreservation']['junctionPairs']=h['invariants']['junctionPairs']==base['invariants']['junctionPairs']
for l,m in zip(seed['paths3D'],f['paths3D']):
 assert len(l)==len(m)
 for a,b in zip(l,m):
  outer={i for i,t in enumerate(a['trackIds']) if t>=.5};neighbours=set(outer)
  for i,j in zip(a['edges'][::2],a['edges'][1::2]):
   if i in outer or j in outer:neighbours.update((i,j))
  for key in ['positions','normals','heights','widths']:
   report['outerFramesExact'] &= all(a[key][i]==b[key][i] for i in outer)
   report['outerNeighbourFramesExact'] &= all(a[key][i]==b[key][i] for i in neighbours)
  report['checkedOuterTargets']+=len(outer)
(p/'resolved-preservation.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
assert all(report['harmonicPreservation'].values()) and all(report[k] for k in ['fullSourceUnchanged','fullBracesUnchanged','fullCapsUnchanged','outerFramesExact','outerNeighbourFramesExact'])
