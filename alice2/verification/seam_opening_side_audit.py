"""Check primary mixed paths enter the long inner route before the outer wall."""
import json
import math
import sys
from pathlib import Path

def audit(source, output):
    data = json.loads(Path(source).read_text())
    rows = []
    for layer, paths in enumerate(data['paths3D']):
        for component, path in enumerate(paths):
            tracks = path['trackIds']
            if not tracks or not any(t < .5 for t in tracks) or not any(t >= .5 for t in tracks):
                continue
            points = path['positions']
            inner = prefix = 0.
            entered_outer = tracks[0] >= .5
            for i in range(1, len(points)):
                if tracks[i] >= .5:
                    entered_outer = True
                if tracks[i-1] < .5 and tracks[i] < .5:
                    length = math.dist(points[i-1], points[i])
                    inner += length
                    if not entered_outer:
                        prefix += length
            chart = data['paths2D'][layer][component]
            xy, labels, widths = chart['positions'], chart['trackIds'], chart['widths']
            wall_ratio = float('inf')
            start = xy[0]
            for j in range(len(xy)-1):
                if labels[j] < .5 or labels[j+1] < .5:
                    continue
                delta = [b-a for a,b in zip(xy[j],xy[j+1])]
                square = sum(v*v for v in delta)
                t = max(0.,min(1.,sum((q-a)*v for q,a,v in zip(start,xy[j],delta))/square)) if square else 0.
                width = (1-t)*widths[j]+t*widths[j+1]
                if width > 0:
                    distance = math.dist(start,[a+t*v for a,v in zip(xy[j],delta)])
                    wall_ratio = min(wall_ratio,distance/width)
            rows.append(dict(layer=layer, component=component, startsInner=tracks[0] < .5,
                             wallRouteDistanceInHalfWidths=wall_ratio,
                             onWallRoute=wall_ratio <= 1.101,
                             initialInnerLength=prefix, totalInnerLength=inner,
                             initialFraction=prefix/inner if inner else 0.,
                             passed=tracks[0] < .5 and prefix >= .5*inner-1e-4 and wall_ratio <= 1.101))
    report = dict(source=str(source), checked=len(rows), layers=len(data['paths3D']),
                  allPassed=bool(rows) and all(row['passed'] for row in rows), rows=rows,
                  maximumWallRouteRatio=max((r['wallRouteDistanceInHalfWidths'] for r in rows),default=None),
                  note='Independent actual-path order and wall-route proximity audit. Exact transported-anchor correspondence is separately recorded by native alignment; this audit does not certify a straight world-space seam.')
    Path(output).write_text(json.dumps(report, indent=2))
    print(json.dumps({k:v for k,v in report.items() if k != 'rows'}, indent=2))
    return report['allPassed']

if __name__ == '__main__':
    raise SystemExit(0 if audit(*sys.argv[1:]) else 1)
