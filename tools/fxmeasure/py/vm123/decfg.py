import fit, numpy as np, json
from concurrent.futures import ThreadPoolExecutor
d=fit.LOC+'/dyn2'; S=['st6k','pk','go','kwe','lu']
base=np.load(fit.LOC+'/de_4_0.npy')
cfgs=[('DeEsser_0',5506,-24.2,0,0)]+[('de_t%s'%t,5506,t,0,0) for t in [-40,-30,-15,-5]]+[('de_f%s'%f,f,-24.2,0,0) for f in [2462,4141,8574,14420]]
cfgs+=[('de_BandPass_Split',5506,-24.2,1,0),('de_BandPass_Wideband',5506,-24.2,1,1),('de_HighPass_Wideband',5506,-24.2,0,1),('de_HighPass_Split',5506,-24.2,0,0)]
def one(c):
    tag,f,t,bp,wide=c
    rows=[[d+'/in_%s.wav'%s, d+'/%s_%s.wav'%(tag,s), 0, 1, 'freq=%g'%f,'thr=%g'%t,'bp=%d'%bp,'wide=%d'%wide] for s in S]
    J=fit.jobs(fit.LOC+'/de_%s.jobs'%tag,rows)
    log0=fit.evaluate('de',J,base,'log').strip().splitlines()[-1]
    p=fit.run('de',J,list(base),[1,1,1,1,1,1,0,1,0,0],it=15,verbose=False)
    log=fit.evaluate('de',J,p,'log').strip().splitlines()[-1]
    return tag,list(p),log0,log
res=[]
with ThreadPoolExecutor(4) as ex:
    for tag,p,l0,l in ex.map(one,cfgs):
        res.append((tag,p)); print('%-22s base %s -> %s  Toff %.2f R %.2f K %.2f att %.2f rel %.1f split %.4f scf %.3f'%(tag,l0,l,p[0],p[1],p[2],p[3],p[4],p[5],p[7]),flush=True)
json.dump(res,open(fit.LOC+'/decfg.json','w'))
