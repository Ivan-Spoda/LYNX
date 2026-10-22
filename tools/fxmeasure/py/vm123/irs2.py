"""irs2: like irs.measure but configs given as raw command lists; shorter IRs"""
import vm, numpy as np, os, sys
def measure(plugin, cfgs, name, amp=0.01, state=None, N=16384, chunk=400, settle=3.0):
    d=vm.W+'/ir_'+name; os.makedirs(d,exist_ok=True)
    imp=np.zeros(N); imp[1000]=amp; vm.wsig(d,'imp',imp)
    keys=list(cfgs); H={}; logs=[]
    for c0 in range(0,len(keys),chunk):
        ks=keys[c0:c0+chunk]
        var={'c%d'%i:cfgs[k] for i,k in enumerate(ks)}
        out=vm.run(plugin,state,var,['imp'],d,settle=settle); logs.append(out)
        for i,k in enumerate(ks): H[k]=vm.rd(d,'c%d'%i,'imp')[1000:,0]/amp
        print(name, c0+len(ks), '/', len(keys), flush=True)
    open(d+'/log.txt','w').write('\n'.join(logs))
    np.savez(vm.W+'/ir_%s.npz'%name, keys=keys, irs=np.array([H[k] for k in keys]))
    return H
