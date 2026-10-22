import irs2, numpy as np
B=['settext DynamicBypass On']; C={}
V=np.linspace(0,1,401)
for band in ['LMF','HMF']:
    for v in V: C[str((band,'f',round(v,5)))]=B+['settext "%s Gain" 6'%band,'set "%s Frq" %.6f'%(band,v),'set "%s Q" 0.5'%band,'get "%s Frq"'%band]
for band in ['LF','HF']:
    for v in V: C[str((band,'f',round(v,5)))]=B+['settext "%s Gain" 6'%band,'set "%s Frq" %.6f'%(band,v),'get "%s Frq"'%band]
for v in V[1:]: C[str(('HP','f',round(v,5)))]=B+['set "HP Frq" %.6f'%v,'get "HP Frq"']
for v in V[1:]: C[str(('LP','f',round(v,5)))]=B+['set "LP Frq" %.6f'%v,'get "LP Frq"']
irs2.measure('ssl',C,'ssllaw',settle=2.0)
