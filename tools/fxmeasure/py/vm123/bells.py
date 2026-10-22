import numpy as np, bqf, an2, re, pickle, vm
def table(name):
    keys,irs,log=an2.load(name)
    R={}
    for k,h in zip(keys,irs):
        b,a,e=bqf.fit(h[1:],nfft=32768)
        n,d=an2.analog(b,a); R[k]=(n,d,e,b,a)
    pickle.dump(R,open(vm.W+'/%s.pkl'%name,'wb')); return R
if __name__=='__main__':
    import sys
    for nm in sys.argv[1:]: table(nm); print('done',nm)
