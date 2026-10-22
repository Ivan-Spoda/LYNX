import numpy as np, bqf, vm
sr=48000; k2=2*sr
def analog(b,a):
    n=np.array([b[0]-b[1]+b[2], 2*k2*(b[0]-b[2]), k2*k2*(b[0]+b[1]+b[2])])
    d=np.array([a[0]-a[1]+a[2], 2*k2*(a[0]-a[2]), k2*k2*(a[0]+a[1]+a[2])])
    return n/d[0], d/d[0]   # s^2, s, 1 coefficients
def digital(n,d):
    # bilinear s = k2 (1-z^-1)/(1+z^-1)
    def conv(c): # c = [c2,c1,c0] ; c2 s^2 + c1 s + c0 -> times (1+z^-1)^2
        c2,c1,c0=c; K=k2
        return np.array([c2*K*K + c1*K + c0, 2*c0 - 2*c2*K*K, c2*K*K - c1*K + c0])
    B=conv(n); A=conv(d); return B/A[0], A/A[0]
def load(name):
    z=np.load(vm.W+'/ir_%s.npz'%name); keys=[eval(k) for k in z['keys']]
    log=open(vm.W+'/ir_%s/log.txt'%name).read()
    return keys, z['irs'], log
