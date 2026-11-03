"""Publish the layer-59 follow-up with measured evidence, preserving prior reports."""
import json, shutil, hashlib
from pathlib import Path
from datetime import datetime, timezone
from PIL import Image, ImageDraw, ImageFont
from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer, Image as PdfImage, PageBreak
from reportlab.lib.styles import getSampleStyleSheet
from reportlab.lib.units import mm

root=Path(r'\\zaha-hadid.com\Data\Projects\4163_Carbcomn\4163_Codex')
local=Path('build_seam_side'); evidence=local/'review'
stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
history=root/'history'/f'{stamp}-seam-continuity-2irv3t-Lin'
out=root/'docs'/f'seam-continuity-{stamp}'
for folder in [history/'checks',history/'source',history/'images',out/'images']:
    folder.mkdir(parents=True,exist_ok=True)
def read(name):return json.loads((evidence/name).read_text())
before=read('resolved-full-continuity.json'); after=read('continuity-full-continuity.json')
plot=Image.new('RGB',(1440,450),'white');draw=ImageDraw.Draw(plot)
font=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',22)
for y in [0,50,100,150,200,250]:
    py=370-y*1.25;draw.line((90,py,1400,py),fill='#dddddd');draw.text((15,py-10),str(y),font=font,fill='black')
for x in [0,10,20,30,40,50,60]:draw.text((90+x*21,385),str(x),font=font,fill='black')
for d,label,color in [(before,'Previous build','#c53d37'),(after,'Corrected build','#007b88')]:
    rows=[r for r in d['rows'] if r['component']==0]
    draw.line([(90+r['layer']*21,370-r['distance']*1.25) for r in rows],fill=color,width=4)
    draw.text((200 if label.startswith('Previous') else 550,35),label,font=font,fill=color)
draw.text((90,3),'Adjacent seam step (mm)',font=font,fill='black');draw.text((500,420),'Layer index (Alice2 display)',font=font,fill='black')
plot.save(out/'images/continuity.png')
shots=sorted((local/'bin_continuity/Release/src/screenshots').glob('*_F1.png'))
shot=shots[-1]; assert Image.open(shot).size==(3840,2160)
shutil.copy2(shot,out/'images/layer59.png');shutil.copy2(shot,history/'images/layer59.png')
paragraphs=[
 'Review pending. Project 4163; subtask subtask-feature-fix-seam-mis-alignment-2irv3t. This follow-up covers the reported layer-59 opening jump on example_facetags_3-6.usda, 3 November 2026 (Europe/London). It supersedes the previous continuity claim, not its retained evidence.',
 'Cause: independent nearest-point projections of one distant world reference switched correspondence near the top. Per-layer inner-first and wall-route checks missed this. The correction transports the reference through adjacent sections in both directions, updating the world point at every step, then retains the existing opening-side selection. No layer IDs, fixture coordinates or shape-specific exceptions are used.',
 'Result in Harmonic and Full Map: layer 58 to 59 changes from 221.400 mm to 7.145 mm; layer 59 to 60 changes from 223.397 mm to 7.456 mm. Maximum over the complete stack is 10.718 mm. All 61 mixed paths remain inner-first and on the inner wall route. The new independent three-times-median diagnostic flags the old failure and no corrected steps.',
 'Checks: 34/34 native tests pass, including an independent rotating stack at scales 1 and 100 in serial and parallel, plus existing translated/rotated opening fixtures. TCP range remains 5.094367-18.808290 mm; 1 mm chord range remains 5.094615-18.432577 mm, identical to original Harmonic. No missing target hits. Source boundaries, 244 feature junctions and all 183 prescribed braces are preserved; 3,702 brace probes have no missing samples.',
 'At 4,500 identical source-wall probes, both modes have no increased absolute wall error and no changed hit status. One probe improves by 0.006428 mm; the others are unchanged. All 8,916 Full Map outer TCP frames and their neighbouring frames remain exact against the same-run seed. Caps and bracing are unchanged. Positive XYZ deformation checks pass.',
 'Limits: gap ranges are preserved, not every resampled statistic. Full Map target weighted P95 deviation is 3.007339 mm versus original Harmonic 3.006694 mm (+0.000645 mm); see saved JSON for all statistics. The 6-12 mm target, existing waves and six inherited opposite-orientation UVW elements remain unresolved. Adjacent continuation can drift or become ambiguous on topology changes or nearby competing sheets; the continuity flag is a diagnostic, not a general topology certificate. Test new geometry before fabrication.',
 'Native screenshot below: Full Map, displayed layer 59, saved F1 camera, native 3840x2160 with 1x UI. Experimental partial-result display is explicitly enabled because the pre-existing fabrication qualification conflicts remain; it is not production approval.'
]
md='# Layer-59 seam continuity correction\n\n'+ '\n\n'.join(paragraphs)+ '\n\n![Seam steps](images/continuity.png)\n\n![Native layer 59](images/layer59.png)\n'
(out/'report.md').write_text(md,encoding='utf-8')
styles=getSampleStyleSheet(); story=[Paragraph('Layer-59 seam continuity correction',styles['Title'])]
for p in paragraphs[:5]:story.extend([Paragraph(p,styles['BodyText']),Spacer(1,3*mm)])
story.append(PdfImage(str(out/'images/continuity.png'),width=175*mm,height=54.4*mm))
story.append(PageBreak())
story.extend([Paragraph('Native validation and remaining limits',styles['Heading1']),Paragraph(paragraphs[6],styles['BodyText']),Spacer(1,4*mm),PdfImage(str(out/'images/layer59.png'),width=180*mm,height=101.25*mm),Spacer(1,5*mm),Paragraph(paragraphs[5],styles['BodyText'])])
SimpleDocTemplate(str(out/'report.pdf'),pagesize=(210*mm,297*mm),leftMargin=15*mm,rightMargin=15*mm,topMargin=14*mm,bottomMargin=14*mm).build(story)
for p in evidence.glob('continuity-*.json'):shutil.copy2(p,history/'checks'/p.name)
for p in evidence.glob('continuity-*.npz'):shutil.copy2(p,history/'checks'/p.name)
for p in local.glob('continuity-*.log'):shutil.copy2(p,history/'checks'/p.name)
shutil.copy2(evidence/'resolved-full-continuity.json',history/'checks/resolved-full-continuity.json')
toolsets=Path(r'C:\Users\Wo.Lin\source\repos\zspace_toolsets\build\worktrees\task-008-print-path-post-processing')
sources=[toolsets/'src/zToolsets/fabrication/slicing/printsyn/zPrintSyn.cpp',toolsets/'include/zspace/zToolsets/fabrication/slicing/printsyn/zPrintSyn.h',toolsets/'tests/smoke/zspace_toolsets_printsyn.cpp',Path(__file__),Path('verification/seam_continuity_audit.py')]
for p in sources:shutil.copy2(p,history/'source'/p.name)
summary=md+'\n## Pickup\n\nRecord state: Review pending. Contributor: Wo Lin / Codex. Reviewer: Wo Lin. No commit or push. Existing dirty work preserved.\n\nToolsets checkout: '+str(toolsets)+'; branch codex/task-008-print-path-post-processing, HEAD c74cb29fb438e97b9fe2c9fa83dc0c14cb45c8ab. Alice2 HEAD 32399a473c86acd50c4d418549f0253e57f6d98f, branch codex/task-006-print-mesh. Changes are uncommitted; exact source snapshots are in source/.\n\nBuild: '+str(Path.cwd()/local/'Open-Alice2-Continuity.cmd')+'\n\nReport: '+str(out/'report.pdf')+'\n\nPrevious record: ../20261103T141736Z-seam-wall-2irv3t-Lin/summary.md. Previous build failed reviewer validation at layer 59.\n\nNext: reviewer validates layer 59 and neighbouring layers, then tests additional geometry. No acceptance received. Tracker publication pending readback.\n'
# The plot link belongs to the report; retain a portable history copy too.
shutil.copy2(out/'images/continuity.png',history/'images/continuity.png')
(history/'summary.md').write_text(summary,encoding='utf-8')
hashes={str(p.relative_to(history)):hashlib.sha256(p.read_bytes()).hexdigest() for p in history.rglob('*') if p.is_file()}
(history/'checks/artifact-sha256.json').write_text(json.dumps(hashes,indent=2))
(local/'continuity-publication.json').write_text(json.dumps(dict(history=str(history),report=str(out)),indent=2))
print(json.dumps(dict(history=str(history),report=str(out))))
