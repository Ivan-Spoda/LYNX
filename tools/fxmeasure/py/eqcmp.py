import sys, json, subprocess; sys.path.insert(0,'.')
import os; DATA = os.environ.get('FXMEASURE_DATA', '../data')
import numpy as np
tag = sys.argv[1] if len(sys.argv)>1 else 'pq_zero_48000'
d=np.load(DATA + '/%s.npz'%tag); fq,H,C,sr=d['fq'],d['H'],json.loads(str(d['configs'])),int(d['sr'])
nm={'Bell':'Bell','Low Shelf':'LowShelf','High Shelf':'HighShelf','Low Cut':'LowCut','High Cut':'HighCut','Notch':'Notch','Band Pass':'BandPass','Tilt Shelf':'TiltShelf'}
sel=[i for i,c in enumerate(C) if c['shape'] in nm and c['slope']!='Brickwall' and c['slope']!='0' and not (c['shape'] in ('Bell','Notch','Band Pass') and c['slope']!='12') and not (c['shape'] in('Low Shelf','High Shelf','Tilt Shelf') and c['slope'] not in ('6','12'))]
inp=''.join('%s %g %g %g %s %d\n'%(nm[C[i]['shape']],C[i]['f'],C[i]['g'],C[i]['q'],C[i]['slope'],sr) for i in sel)
out=subprocess.run([os.environ.get('FACET_EQTEST', '../src/eqtest')],input=inp,capture_output=True,text=True).stdout.split('\n')
band=(fq>=20)&(fq<=20000)
res={}
for i,line in zip(sel,out):
    m=np.array([float(v) for v in line.split()]); meas=20*np.log10(np.abs(H[i]))
    ok=band&(meas>-50)
    e=np.max(np.abs(m[ok]-meas[ok]))
    res.setdefault(C[i]['shape'],[]).append((e,C[i]))
for k,v in res.items():
    v.sort(key=lambda x:x[0]); es=[x[0] for x in v]
    print('%-10s n=%3d median %.3f p90 %.3f max %.3f worst %s'%(k,len(v),np.median(es),np.percentile(es,90),es[-1],v[-1][1]))
