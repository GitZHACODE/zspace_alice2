import hashlib,json,re
from pathlib import Path
from PIL import Image
repo=Path(__file__).resolve().parents[2]
images=repo/'agents/docs/images'
reports=[]
for manifest in sorted(images.glob('*verified.capture.json')):
 data=json.loads(manifest.read_text(encoding='utf-8-sig'))
 picture=Path(data['finalImage']);im=Image.open(picture)
 assert im.size==(3840,2160),(picture,im.size)
 assert hashlib.sha256(picture.read_bytes()).hexdigest().upper()==data['finalImageSHA256']
 assert data['sourceImageSHA256']==data['finalImageSHA256']
 assert hashlib.sha256(Path(data['cameraPath']).read_bytes()).hexdigest().upper()==data['cameraSHA256']
 if 'stdout' in data:
  log=Path(data['stdout']).read_text(errors='replace')
  assert '[SCREENSHOT] Native render 3840x2160, UI scale=1' in log
  assert '[Print mesh]' not in Path(data['stderr']).read_text(errors='replace')
  match=re.search(r'printLayers=(\d+)',log)
  if match:data['printableLayers']=int(match[1])
  match=re.search(r'heightSurfaces=(\d+)',log)
  if match:data['localSurfaceCorrection']=bool(int(match[1]))
  if 'print_mesh' in picture.name:data['layer']='all'
  match=re.search(r'target printheightdomain = \[([\d.]+), ([\d.]+)\].*tolerance \+/-([\d.]+)',log)
  if match:data['heightDomainMM']=[float(match[1]),float(match[2])];data['heightToleranceMM']=float(match[3])
  manifest.write_text(json.dumps(data,indent=2)+'\n')
 reports.append({'file':picture.name,'dimensions':list(im.size),'cameraUnchanged':True,'byteIdenticalToNativeCapture':True})
print(json.dumps(reports,indent=2))
