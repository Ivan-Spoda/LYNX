"""dsig: dynamics test signals (round 123), each <= 18 s, 48 kHz."""
import numpy as np, sig, vm
sr=48000
def tone(f,n,ph=0): return np.sin(2*np.pi*f*np.arange(n)/sr+ph)
def stairs(f=1000, lo=-50, hi=0, step=2, dur=0.4):
    out=[]; 
    for L in np.arange(lo,hi+0.01,step):
        n=int(dur*sr); out.append(10**(L/20)*tone(f,n))
    return np.concatenate(out)
def bursts(f=1000, base=-40, top=-6, lens=(0.005,0.02,0.1,0.5,2.0), gap=1.5):
    out=[np.zeros(int(0.2*sr))]
    t=0
    for L in lens:
        nb=int(gap*sr); nt=int(L*sr)
        out.append(10**(base/20)*tone(f,nb)); out.append(10**(top/20)*tone(f,nt))
    out.append(10**(base/20)*tone(f,int(gap*sr)))
    x=np.concatenate(out)
    return 10**(base/20)*tone(f,len(x))*(np.abs(x)<10**((base+1)/20))+x*(np.abs(x)>=10**((base+1)/20)) if False else x
def pink(n, seed=0):
    X=np.fft.rfft(np.random.default_rng(seed).standard_normal(n)); f=np.fft.rfftfreq(n,1/sr); f[0]=f[1]
    X/=np.sqrt(f); X[(f<20)]=0; y=np.fft.irfft(X,n); return y/np.sqrt((y**2).mean())
def takes(sec=17):
    out={}
    for k in ['go','kwe','lu']:
        x,_=sig.read_wav('/mnt/c/Users/kauan/lynx-test/w119/ns3/in_%s.wav'%k); out[k]=x[:int(sec*sr)]
    return out
def all_sigs():
    S={'st1k':stairs(1000),'st100':stairs(100),'st6k':stairs(6000),'bu':bursts(),'bu2':bursts(top=-20,base=-50)}
    p=pink(int(16*sr)); env=np.concatenate([np.full(int(4*sr),10**(l/20)) for l in (-40,-25,-15,-6)])
    S['pk']=p*env[:len(p)]*0.5
    S.update(takes())
    return S
def write(d):
    S=all_sigs()
    for k,x in S.items(): vm.wsig(d,k,x)
    return list(S)
