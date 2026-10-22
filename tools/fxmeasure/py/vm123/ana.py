"""ana.py: comparison metrics (round 123): null residual, 1/3-octave and envelope differences, LM fit."""
import numpy as np
sr=48000
def align(a,b,maxlag=4096):
    """lag (samples) such that b[n+lag] ~ a[n]"""
    n=min(len(a),len(b),sr*20); A=np.fft.rfft(a[:n],2*n); B=np.fft.rfft(b[:n],2*n)
    c=np.fft.irfft(np.conj(A)*B); c=np.concatenate([c[-maxlag:],c[:maxlag+1]]); return int(np.argmax(np.abs(c)))-maxlag
def null_db(ref,y,mask=None):
    """residual energy (ref-y) relative to ref energy, dB"""
    n=min(len(ref),len(y)); r=ref[:n]; d=r-y[:n]
    if mask is not None: r=r[mask[:n]]; d=d[mask[:n]]
    return 10*np.log10((d**2).sum()/max((r**2).sum(),1e-30)+1e-30)
TO=1000*2.0**(np.arange(-17,14)/3)  # 20 Hz .. 20 kHz
def third_oct(y,mask=None):
    if mask is not None: y=y[mask[:len(y)]]
    Y=np.abs(np.fft.rfft(y))**2; f=np.fft.rfftfreq(len(y),1/sr)
    return np.array([10*np.log10(Y[(f>=c*2**-(1/6))&(f<c*2**(1/6))].sum()+1e-30) for c in TO])
def spec_diff(ref,y,mask=None,lo=40,hi=16000):
    d=third_oct(ref,mask)-third_oct(y,mask); k=(TO>=lo)&(TO<=hi); return d[k], TO[k]
def env_db(y,win=0.05):
    n=int(win*sr); m=len(y)//n; e=(y[:m*n].reshape(m,n)**2).mean(1); return 10*np.log10(e+1e-20)
def env_diff(ref,y,win=0.05,floor=-50):
    a=env_db(ref,win); b=env_db(y,win); m=min(len(a),len(b)); a=a[:m]; b=b[:m]; k=a>a.max()+floor
    return a[k]-b[k]
def lm(f,p0,args=(),it=60,lam=1e-2,eps=1e-6,verbose=False):
    """Levenberg-Marquardt on residual vector f(p,*args)"""
    p=np.array(p0,float); r=f(p,*args); c=(r**2).sum()
    for k in range(it):
        J=np.empty((len(r),len(p)))
        for j in range(len(p)):
            q=p.copy(); h=eps*max(1,abs(p[j])); q[j]+=h; J[:,j]=(f(q,*args)-r)/h
        A=J.T@J; g=J.T@r
        while True:
            try: dp=np.linalg.solve(A+lam*np.diag(np.diag(A)+1e-12),-g)
            except np.linalg.LinAlgError: lam*=10; continue
            q=p+dp; rq=f(q,*args); cq=(rq**2).sum()
            if cq<c: p,r,c=q,rq,cq; lam=max(lam/3,1e-9); break
            lam*=4
            if lam>1e12: return p,c
        if verbose: print(k,c,p)
    return p,c
