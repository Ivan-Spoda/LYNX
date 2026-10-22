import numpy as np, vm, bqf, re, sslmodel as M
SR=48000; K2=2*SR
def load():
    z=np.load(vm.W+'/ir_sslg_filt.npz'); keys=[eval(k) for k in z['keys']]; irs=z['irs']
    gl=M.gets('sslg_filt'); hp=[]; lp=[]
    for k,h,g in zip(keys,irs,gl):
        if k[0]=='hp':
            t=float(re.search(r'\(([\d.]+)',g).group(1))
            b,a,e=bqf.fit(h[1:],order=3,nfft=65536); r=np.roots(a); s=K2*(r-1)/(r+1)
            w=np.real(s[np.argmin(abs(s.imag))])*-1; hp.append((t,w,e))
        elif k[0]=='lp':
            t=float(re.search(r'\(([\d.]+)',g).group(1))*1000
            b,a,e=bqf.fit(h[1:],order=2,nfft=65536); lp.append((t,b,a,e))
    return hp,lp
def hp_ba(w):
    # analog s^3/((s+w)(s^2+ws+w^2)) -> bilinear
    num=np.array([1,0,0,0.]); den=np.polymul([1,w],[1,w,w*w])
    return bilinear(num,den)
def bilinear(num,den):
    N=len(den)-1
    def tr(c):
        c=np.concatenate([np.zeros(N+1-len(c)),c]); out=np.zeros(N+1)
        for i,ci in enumerate(c):
            p=N-i  # power of s
            term=np.array([1.])
            for _ in range(p): term=np.polymul(term,K2*np.array([1,-1.]))
            for _ in range(N-p): term=np.polymul(term,np.array([1,1.]))
            out+=ci*term
        return out
    B=tr(num); A=tr(den); return B/A[0],A/A[0]
