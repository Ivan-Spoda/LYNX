import fit, numpy as np, vm, stair, sys, json
from concurrent.futures import ThreadPoolExecutor
d=fit.LOC+'/dyn3'; S=['st1k','bu','pk','go']
x=vm.sig.read_wav(d+'/in_st1k.wav')[0][:,0]
cfgs=[('s3_t%s_r1.8'%t,t,1.8,0.35,1) for t in [10,5,0,-5,-10,-15,-20]]+[('s3_t-1.3_r%s'%r,-1.3,1e9 if r=='+Inf' else float(r),0.35,1) for r in ['1.2','1.5','1.8','2.0','3.0','4.0','8.0','+Inf']]
def one(c):
    tag,thr,r,rel,fast=c
    y=vm.sig.read_wav(d+'/%s_st1k.wav'%tag)[0][:,0]; L,g=stair.stair_gain(x,y,lat=1); mk=g[0]
    rows=[[d+'/in_%s.wav'%s, d+'/%s_%s.wav'%(tag,s), 1, 1, 'thr=%g'%thr,'ratio=%g'%min(r,1e6),'rel=%g'%rel,'fast=%d'%fast,'mk=%.4f'%mk] for s in S]
    J=fit.jobs(fit.LOC+'/c3_%s.jobs'%tag,rows)
    p0=[-19,2.2,0.7,3,0.021,0.064,3.5 if fast else 1.67,100,0.13,0.9,1,0]
    free=[1,1,1 if r<1e6 else 0,0 if r<1e6 else 1,0,0,1 if fast else 0,0 if fast else 1,1,0,0,0]
    p=fit.run('ssl',J,p0,free,it=20,verbose=False)
    log=fit.evaluate('ssl',J,p,'log')
    return tag,thr,r,rel,fast,mk,list(p),float(log.strip().splitlines()[-1].split()[1])
res=[]
with ThreadPoolExecutor(5) as ex:
    for t in ex.map(one,cfgs):
        res.append(t); tag,thr,r,rel,fast,mk,p,tot=t
        print('%-18s mk %.2f Toff %.2f K %.2f rs %.3f sinf %.2f attF %.2f attS %.2f relS %.4f  %.1f'%(tag,mk,p[0],p[1],p[2],p[3],p[6],p[7],p[8],tot),flush=True)
json.dump(res,open(fit.LOC+'/sslcfg4.json','w'))
