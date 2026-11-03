# Accepted seam alignment correction

Accepted by Wo Lin on 3 November 2026. Tracker: project 4163, subtask `subtask-feature-fix-seam-mis-alignment-2irv3t`.

The generic opening-side seam selection and adjacent-section continuation are published in zspace_toolsets on `codex/task-008-print-path-post-processing`, commits `d87497108c2086966017564be356523dbe028f0c` and `a7689d69` (line-ending correction).

Layer-59 steps changed from 221.400/223.397 mm to 7.145/7.456 mm. Both Harmonic and Full Map have maximum adjacent seam movement 10.718 mm on the supplied USD. Native suite: 34/34 pass. TCP/chord ranges match original Harmonic; 4,500 wall probes show no increased absolute error. Boundaries, features and all 183 braces are preserved.

The independent verification scripts in `alice2/verification/seam_*.py` preserve the audit/report procedures. Reports use the shared evidence exports and are not standalone pipeline runners.

Evidence: `\\zaha-hadid.com\Data\Projects\4163_Carbcomn\4163_Codex\history\20261103T164724Z-seam-continuity-2irv3t-Lin`.

Report: `\\zaha-hadid.com\Data\Projects\4163_Carbcomn\4163_Codex\docs\seam-continuity-20261103T164724Z\report.pdf`.

This publication covers the accepted seam task. The desktop validation used the existing experimental Full Map working tree; its broader uncommitted solver work is not included in these seam commits. Remaining waves, the 6-12 mm target and inherited UVW orientation conflicts remain outside completion of this task. New geometry with topology changes requires continuity validation.
