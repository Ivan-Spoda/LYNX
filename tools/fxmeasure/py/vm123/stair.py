import numpy as np, vm
sr=48000
def stair_gain(x,y,dur=0.4,lo=-50,hi=0,step=2,lat=0):
    n=int(dur*sr); L=np.arange(lo,hi+0.01,step); out=[]
    for i,l in enumerate(L):
        a=i*n+int(0.25*sr); b=(i+1)*n-200
        xi=x[a:b]; yi=y[a+lat:b+lat]
        out.append(10*np.log10((yi**2).mean()/(xi**2).mean()))
    return L,np.array(out)
