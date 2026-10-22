import vm
SET=3
names=['st1k','bu','bu2','pk','go']
base=['settext EQBypass On','set "HP Frq" 0','set "LP Frq" 0']
V={}
for thr in [10,5,0,-5,-10,-15,-20]:
    for r in ['1.2','1.5','2.0','3.0','4.0','8.0','+Inf']:
        V['s3_t%s_r%s'%(thr,r)]=base+['settext CompThresh %s'%thr,'settext CompRatio %s'%r,'settext CompFast On','settext CompRelease 0.35']
for fast in ['Off','On']:
    for rel in [0.1,0.2,0.35,0.6,1.2,2,4]:
        V['s3_f%s_rel%s'%(fast,rel)]=base+['settext CompThresh -10','settext CompRatio 4','settext CompFast %s'%fast,'settext CompRelease %s'%rel]
    for thr in [0,-20]:
        V['s3_f%s_t%s_r2'%(fast,thr)]=base+['settext CompThresh %s'%thr,'settext CompRatio 2','settext CompFast %s'%fast,'settext CompRelease 1.2']
for mode in ['Exp','Gate']:
    for thr in [-30,-20,-10,0]:
        for rg in [10,20,40]:
            V['s3_%s_t%s_g%s'%(mode,thr,rg)]=base+['settext CompRatio 1','settext ExpGate %s'%mode,'settext ExpThresh %s'%thr,'settext ExpRange %s'%rg,'settext ExpRelease 0.35']
    for rel in [0.1,1.2,4]:
        for fa in ['Off','On']:
            V['s3_%s_rel%s_f%s'%(mode,rel,fa)]=base+['settext CompRatio 1','settext ExpGate %s'%mode,'settext ExpThresh -20','settext ExpRange 30','settext ExpRelease %s'%rel,'settext ExpAttack %s'%fa]
out=vm.run('ssl',None,V,names,vm.W+'/dyn3',settle=SET)
open(vm.W+'/dyn3/ssldyn3.log','w').write(out); print('done')
