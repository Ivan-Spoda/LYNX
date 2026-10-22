import numpy as np, vm, fit, subprocess, os
def model_out(model, inwav, knobs, p, ref):
    J=fit.jobs(fit.LOC+'/tmp1.jobs',[[inwav, ref, 0, 1]+knobs])
    os.makedirs(fit.LOC+'/mo',exist_ok=True)
    subprocess.run([fit.LAB+'/dynfit',model,J]+['%.10g'%v for v in p],env=dict(os.environ,OUT=fit.LOC+'/mo'),capture_output=True)
    return vm.sig.read_wav(fit.LOC+'/mo/m0.wav')[0]
def gtr(x,y,lat,n=48,floor=0.01):
    """gain in dB per n-sample block using samples where |x| is large"""
    yy=y[lat:]; xx=x[:len(yy)]; m=len(xx)//n
    X=xx[:m*n].reshape(m,n); Y=yy[:m*n].reshape(m,n)
    num=(X*Y).sum(1); den=(X*X).sum(1)
    g=np.where(den>floor**2*n*0.1, num/np.maximum(den,1e-20), np.nan)
    return 20*np.log10(np.abs(g)+1e-12)
