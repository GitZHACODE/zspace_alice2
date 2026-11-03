"""Adjacent seam movement diagnostic, independent of native anchor matching.

The three-times-median flag is scale invariant, not a geometric guarantee.
Topology changes require review instead of interpreting a passing flag as proof.
"""
import argparse
import json
import math
import statistics
from pathlib import Path


def audit(source):
    data = json.loads(source.read_text())
    starts = {}
    for layer, paths in enumerate(data['paths3D']):
        for component, path in enumerate(paths):
            if path['positions']:
                starts[layer, component] = path['positions'][0]
    rows = [dict(layer=l, component=c, distance=math.dist(p, starts[l-1, c]))
            for (l, c), p in starts.items() if (l-1, c) in starts]
    typical = statistics.median(r['distance'] for r in rows)
    threshold = 3 * typical
    flagged = [r for r in rows if r['distance'] > threshold + 1e-6]
    return dict(source=str(source), medianStep=typical,
                maximumStep=max(r['distance'] for r in rows),
                diagnosticThreshold=threshold, flagged=flagged, rows=rows,
                note='Primary component indices must correspond; topology changes require review. '
                     'Three-times-median diagnostic is not a continuous seam certificate.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    result = audit(args.source)
    args.output.write_text(json.dumps(result, indent=2))
    print(json.dumps({k:v for k,v in result.items() if k != 'rows'}, indent=2))
    raise SystemExit(bool(result['flagged']))
