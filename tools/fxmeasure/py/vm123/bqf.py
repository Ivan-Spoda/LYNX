"""bqf: fast biquad fit of an impulse response (frequency-domain Steiglitz-McBride, numpy)."""
import numpy as np
NF=4096
def fit(h, nfft=8192, it=12, order=2):
    H=np.fft.rfft(h, nfft)[:-1]; w=np.linspace(0,np.pi,len(H),endpoint=False)
    E=np.exp(-1j*np.outer(w,np.arange(order+1)))
    Aprev=np.ones(len(w))
    for k in range(it):
        # B(w) - H(w) A'(w) = H  with A = 1 + a1 e^-jw + a2 e^-2jw
        M=np.concatenate([E, -(H[:,None]*E[:,1:])],1)/Aprev[:,None]
        rhs=H/Aprev
        M2=np.concatenate([M.real,M.imag]); r2=np.concatenate([rhs.real,rhs.imag])
        th=np.linalg.lstsq(M2,r2,rcond=None)[0]
        b=th[:order+1]; a=np.concatenate([[1],th[order+1:]])
        Aprev=E@a
    Hf=(E@b)/(E@a); err=10*np.log10((np.abs(Hf-H)**2).sum()/(np.abs(H)**2).sum()+1e-30)
    return b,a,err
def resp(b,a,f,sr=48000):
    z=np.exp(-1j*2*np.pi*np.asarray(f)/sr); n=sum(b[i]*z**i for i in range(len(b))); d=sum(a[i]*z**i for i in range(len(a))); return n/d
