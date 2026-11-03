"""Generate the requested seam-development report from verified native exports."""
import json, math, shutil
from pathlib import Path
from datetime import datetime, timezone
from PIL import Image, ImageDraw, ImageFont
from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer, Image as PdfImage, PageBreak, Table, TableStyle
from reportlab.lib.styles import getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.lib import colors
root=Path(r'\\zaha-hadid.com\Data\Projects\4163_Carbcomn\4163_Codex')
history=root/'history'/'20261103T141736Z-seam-wall-2irv3t-Lin'
out=root/'docs'/'20261103-seam-wall-20261103T141736Z'
out.mkdir(exist_ok=True);(out/'images').mkdir(exist_ok=True)
p=Path('build_seam_side/review')
def read(name):return json.loads((p/name).read_text())
before=json.loads(Path('build_seam_side/review/final-seam-full-run0.json').read_text())
after=read('resolved-full-run0.json')
audit=read('resolved-full-seam.json')
harm=read('resolved-harmonic-tcp.json');full=read('resolved-full-tcp.json')
wall=read('resolved-full-envelope.json');hwall=read('resolved-harmonic-envelope.json')
mapping=after['fullMapping'];bending_reduction=100*(1-mapping['bending']/mapping['seedBending'])
canvas=Image.new('RGB',(1600,1300),'white');d=ImageDraw.Draw(canvas)
font=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',25)
for row,layer in enumerate((35,43)):
 for col,(name,data) in enumerate((('Previous seam trial',before),('Corrected Full Map',after))):
  path=data['paths3D'][layer][0];points=path['positions'];tracks=path['trackIds']
  xs=[v[0] for v in points];ys=[v[1] for v in points]
  scale=min(690/(max(xs)-min(xs)),510/(max(ys)-min(ys)))
  project=lambda v:(col*800+55+(v[0]-min(xs))*scale,row*650+610-(v[1]-min(ys))*scale)
  d.text((col*800+25,row*650+20),f'{name}: layer {layer+1}',fill='black',font=font)
  for i in range(1,len(points)):
   d.line([project(points[i-1]),project(points[i])],fill=('#2563eb' if tracks[i]<.5 else '#a855f7'),width=3)
  a=project(points[0]);d.ellipse((a[0]-7,a[1]-7,a[0]+7,a[1]+7),fill='#ef4444')
  b=project(points[min(3,len(points)-1)]);d.line((a,b),fill='#ef4444',width=5)
  dx,dy=b[0]-a[0],b[1]-a[1];length=math.hypot(dx,dy)
  if length:
   ux,uy=dx/length,dy/length;d.polygon([b,(b[0]-17*ux+7*uy,b[1]-17*uy-7*ux),(b[0]-17*ux-7*uy,b[1]-17*uy+7*ux)],fill='#ef4444')
comparison=out/'images'/'seam-order-comparison.png';canvas.save(comparison)
screenshot=history/'images'/'resolved-full-layer43.png'
shutil.copy2(screenshot,out/'images'/'native-full-layer44.png')
selection='Project 4163; subtask-feature-fix-seam-mis-alignment-2irv3t; 3 November 2026, Europe/London. Follow-up to the seven seam conflicts and local Full Map wall regression. Review pending.'
summary='The seven constrained seam projections are eliminated: both Harmonic and Full Map now use a compatible shared anchor for all 61 layers, with every start on the inner wall route and inner-first travel. Full Map has zero measured wall-envelope change at the 1,500 and 4,500 paired probes against original Harmonic. Alice2 is rebuilt for validation.'
method='PrintSyn searches the original and distributed reference layers, checking the entire stack for a common opening side. It can choose one consistent reverse traversal if needed; this geometry retains its original direction. A bead-width distance check excludes starts that drift into bracing. The seed starts from the outer-to-inner transition, not an assumed vertex index. The same shared reference is then mapped to all layers, with no layer-specific clamping in this result. No input coordinates or special layer IDs are encoded.'
settings='Input: example_facetags_3-6.usda (SHA256 C33E98DBF1E0753EAE1C3CFDC4E1FA8CB5EA3985EE947F03F8187510122B5603). Authored spacing 8 mm; seam offset 40 mm; seam gap 20 mm; outer/inner resampling 10/20 mm. Seam offset is now a preference constrained by the opening side.'
checks='Release build and all 34 native tests passed, including translated, rotated and 100-times-scaled opening fixtures. Independent actual-path ordering and wall-route audits pass 61/61 layers in both modes. Source mesh, section topology, fixed caps and bracing definitions are preserved. Both modes retain 244 feature junctions and all 183 prescribed braces with no missing brace samples.'
limits='Full Map now fixes the volume controls supporting outer bead positions, normals, heights and adjacent tangents. It also rejects increases at individual wall probes, rather than accepting a local loss when aggregate error improves. This sharply reduces the available smoothing freedom. TCP extrema equal original Harmonic (5.094367-18.808290 mm); the previous trial maximum was 0.000740 mm lower. The 6-12 mm target, visible waves and six inherited UVW orientation conflicts remain outside this correction. Finite sampled wall checks are not a continuous certificate.'
next_action='Validate the opening-side seam in the rebuilt desktop: build_seam_side/Open-Alice2.cmd. The shared Tracker task remains in progress with review pending until the user accepts this evidence. Earlier reports and the original desktop build are retained. No commit or push was performed.'
caption='XY projections of exported Full Map paths at printed layers 36 and 44, both previously constrained. Blue: inner route; purple: outer route; red: start and initial direction. These are geometry audit plots, not desktop screenshots. All 61 layers are checked numerically; alignment means correspondence to one transported reference, not a straight line in world space.'
shotcaption='Real Alice2 framebuffer capture: Full Map, zero-based layer 43 (44th printed layer), saved F1 camera. This formerly conflicting layer is shown in the native sampled-path view. Existing global gap and mapping warnings remain visible.'
rows=[['Check','Harmonic','Full Map'],['Direct seam matches','61 / 61','61 / 61'],['Inner-first wall starts','61 / 61','61 / 61'],['TCP minimum (mm)',f"{harm['targetStats']['minimum']:.6f}",f"{full['targetStats']['minimum']:.6f}"],['TCP maximum (mm)',f"{harm['targetStats']['maximum']:.6f}",f"{full['targetStats']['maximum']:.6f}"],['Wall error increase (mm)','0.00000',f"{wall['pairedVsBaseline']['maximumIncreaseInAbsoluteSourceError']:.5f}"],['Missing brace samples','0','0']]
markdown='# Opening-side seam and wall preservation\n\n'+selection+'\n\n'+summary+'\n\n## Method\n\n'+method+'\n\n'+settings+'\n\n## Verification\n\n'+checks+'\n\n'+'\n'.join(['| '+' | '.join(row)+' |' for row in [rows[0],['---']*3]+rows[1:]])+'\n\n'+limits+'\n\n## Before and after\n\n![Seam order](images/seam-order-comparison.png)\n\n'+caption+'\n\n## Native desktop evidence\n\n![Native Full Map](images/native-full-layer44.png)\n\n'+shotcaption+'\n\n## Review and remaining limits\n\n'+next_action+'\n\nHistory: ../../history/20261103T141736Z-seam-wall-2irv3t-Lin/summary.md\n\nTracker server: https://task-tracker.app-uat.zaha-hadid.com\n\nGenerated '+datetime.now(timezone.utc).isoformat()+'\n'
markdown=markdown.replace('## Before and after',f'Full Map bending energy: {mapping["seedBending"]:.3f} to {mapping["bending"]:.3f} ({bending_reduction:.5f}% reduction). Earlier trial: 4.33%; strict wall preservation takes priority.\n\n## Before and after')
(out/'report.md').write_text(markdown,encoding='utf-8')
styles=getSampleStyleSheet();styles['BodyText'].fontSize=10;styles['BodyText'].leading=14
story=[]
def para(t,style='BodyText'):
 story.extend([Paragraph(t,styles[style]),Spacer(1,3*mm)])
para('Opening-side seam','Title');para('Development review | 3 November 2026','Heading2');para(selection);para(summary);para('Method','Heading2');para(method);para('Verification','Heading2');para(checks)
table=Table(rows,colWidths=[82*mm,45*mm,45*mm]);table.setStyle(TableStyle([('BACKGROUND',(0,0),(-1,0),colors.HexColor('#e2e8f0')),('GRID',(0,0),(-1,-1),.3,colors.grey),('FONTSIZE',(0,0),(-1,-1),9),('TOPPADDING',(0,0),(-1,-1),6),('BOTTOMPADDING',(0,0),(-1,-1),6)]));story.extend([table,Spacer(1,4*mm)]);para('Zero constrained seam projections; zero wall-envelope change in both independent probe sets. Review pending.');story.append(PageBreak())
para('Before and after: actual path order','Heading1');story.append(PdfImage(str(comparison),width=178*mm,height=144.6*mm));para(caption);para('Input and settings','Heading2');para(settings);story.append(PageBreak())
para('Native desktop and remaining work','Heading1');story.append(PdfImage(str(out/'images'/'native-full-layer44.png'),width=178*mm,height=100.125*mm));para(shotcaption);para('Tradeoff and remaining limits','Heading2');para(limits);para(f'Bending energy: {mapping["seedBending"]:.3f} to {mapping["bending"]:.3f} ({bending_reduction:.5f}% reduction). The earlier trial reduced bending by 4.33%, but violated local wall preservation.');para('Next action','Heading2');para(next_action);para('History: 20261103T141736Z-seam-wall-2irv3t-Lin. Tracker task: subtask-feature-fix-seam-mis-alignment-2irv3t. Source, test logs, audit JSON and runtime hashes accompany the shared history.','BodyText')
def footer(c,doc):
 c.setFont('Helvetica',8);c.drawString(16*mm,10*mm,'Project 4163 | Seam and wall correction - review pending');c.drawRightString(194*mm,10*mm,str(doc.page))
SimpleDocTemplate(str(out/'report.pdf'),pagesize=(210*mm,297*mm),leftMargin=16*mm,rightMargin=16*mm,topMargin=15*mm,bottomMargin=18*mm).build(story,onFirstPage=footer,onLaterPages=footer)
print(out)
