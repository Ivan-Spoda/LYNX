import vm
names=['st1k','bu','bu2','pk','go']
base=['settext EQBypass On','set "HP Frq" 0','set "LP Frq" 0']
V={}
for thr in [10,5,0,-5,-10,-15,-20]:
    V['s3_t%s_r1.8'%thr]=base+['settext CompThresh %s'%thr,'settext CompRatio 1.8','settext CompFast On','settext CompRelease 0.35']
for r in ['1.2','1.5','1.8','2.0','3.0','4.0','8.0','+Inf']:
    V['s3_t-1.3_r%s'%r]=base+['settext CompThresh -1.3','settext CompRatio %s'%r,'settext CompFast On','settext CompRelease 0.35']
out=vm.run('ssl',None,V,names,vm.W+'/dyn3',settle=3)
print('done')
