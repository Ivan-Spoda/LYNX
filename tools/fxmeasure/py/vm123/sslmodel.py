"""SSL E-Channel EQ model (round 123): measured grid -> interpolated analog-domain biquad features."""
import pickle, numpy as np, re, vm, an2
SR=48000
def textlaw(param):
    L=[l.split('\t') for l in open(vm.W+'/params/ssl.tsv').read().splitlines()]
    row=[x for x in L[1:] if x[2]==param][0]; vals=row[10:]
    v=np.linspace(0,1,1001); t=[]
    for s in vals:
        m=re.search(r'-?[\d.]+',s); t.append(float(m.group()) if m else np.nan)
    return v,np.array(t)
def gets(name):
    log=open(vm.W+'/ir_%s/log.txt'%name).read()
    return [l for l in log.splitlines() if l.startswith('get ')]
def num(l): return float(re.search(r'\((-?[\d.]+)',l).group(1))
def bandfeat(n,d,wr):
    n=n/d[0]; d=d/d[0]; return np.array([n[0],n[1]/wr,n[2]/wr**2,d[1]/wr,d[2]/wr**2])
def featba(u,wr):
    return an2.digital(np.array([u[0],u[1]*wr,u[2]*wr*wr]),np.array([1,u[3]*wr,u[4]*wr*wr]))
def wref_law(band):
    """text (Hz) -> reference pole frequency (rad/s, analog domain) from the dense f sweep at gain +6 (ssllaw)"""
    R=pickle.load(open(vm.W+'/ssllaw.pkl','rb')); gl=gets('ssllaw')
    unit=1000 if band in ('lmf','hmf','hf') else 1
    pts={}
    for (k,(n,d,e,b,a)),g in zip(R.items(),gl):
        if k[0]!=band.upper(): continue
        t=round(num(g)*unit,3); d=d/d[0]
        if band=='lf': w=np.min(np.abs(np.roots(d)))
        else: w=np.sqrt(abs(d[2]))
        pts.setdefault(t,[]).append(w)
    T=np.array(sorted(pts)); Wt=np.array([np.mean(pts[t]) for t in T])
    return T,Wt
G=[round(-15+1.5*i,1) for i in range(21)]
class Band:
    def __init__(self, band):
        self.band=band; self.T,self.W=wref_law(band); self.bell=band in ('lmf','hmf')
        name='sslg2_'+band; R=pickle.load(open(vm.W+'/%s.pkl'%name,'rb')); gl=gets(name)
        unit=1000 if band in ('lmf','hmf','hf') else 1
        nper=3 if self.bell else 2
        keys=list(R.keys())
        self.nodes={}
        vq,tq=textlaw('%s Q'%band.upper()) if self.bell else (None,None)
        for i,k in enumerate(keys):
            gg=gl[i*nper:(i+1)*nper]; g=num(gg[0]); ft=num(gg[1])*unit
            n,d,e,b,a=R[k]; wr=self.wr(ft)
            if self.bell: key=(g,round(k[1],3),round(k[2],3))
            else: key=(k[0],g,round(k[2],3))
            self.nodes[key]=(bandfeat(n,d,wr),ft)
        if self.bell:
            self.F=sorted(set(k[1] for k in self.nodes)); self.Q=sorted(set(k[2] for k in self.nodes))
            self.vq,self.tq=vq,tq
        else: self.F=sorted(set(k[2] for k in self.nodes))
        # frequency axis value of each F node = log wref(text at node)
        self.fx={}
        for key,(fe,ft) in self.nodes.items():
            fk=key[1] if self.bell else key[2]; self.fx[fk]=np.log(self.wr(ft))
        # flat nodes at g=0
        for key in list(self.nodes):
            if self.bell and key[0]==1.5:
                fe=0.5*(self.nodes[key][0]+self.nodes[(-1.5,key[1],key[2])][0]); self.nodes[(0.0,key[1],key[2])]=(np.array([1,fe[3],fe[4],fe[3],fe[4]]),self.nodes[key][1])
            if (not self.bell) and key[1]==1.5:
                fe=0.5*(self.nodes[key][0]+self.nodes[(key[0],-1.5,key[2])][0]); self.nodes[(key[0],0.0,key[2])]=(np.array([1,fe[3],fe[4],fe[3],fe[4]]),self.nodes[key][1])
    def wr(self,ft): return float(np.interp(ft,self.T,self.W))
    @staticmethod
    def gseg(g):
        e=[-15,-7.5,0,7.5,15]
        for a,b in zip(e[:-1],e[1:]):
            if a<=g<=b: return [x for x in G if a-1e-9<=x<=b+1e-9]
    def predict(self, g, ft, q_or_type, fmode='lin', gmode='cub'):
        g=round(g,1); wr=self.wr(ft); X=np.log(wr)
        fxs=np.array([self.fx[f] for f in self.F]); 
        # frequency weights (linear in log wref)
        j=np.clip(np.searchsorted(fxs,X)-1,0,len(fxs)-2); t=(X-fxs[j])/(fxs[j+1]-fxs[j]); t=min(max(t,0),1)
        fw=[(self.F[j],1-t),(self.F[j+1],t)]
        if self.bell:
            vq=float(np.interp(-q_or_type,-self.tq,self.vq))  # text decreasing with v
            k=np.clip(np.searchsorted(self.Q,vq)-1,0,len(self.Q)-2); s=(vq-self.Q[k])/(self.Q[k+1]-self.Q[k]); s=min(max(s,0),1)
            qw=[(self.Q[k],1-s),(self.Q[k+1],s)]
        gs=self.gseg(g)
        def at(gg):
            u=0
            for f,wf in fw:
                if self.bell:
                    for qv,wq in qw: u=u+wf*wq*self.nodes[(gg,f,qv)][0]
                else: u=u+wf*self.nodes[(q_or_type,gg,f)][0]
            return u
        ys=np.array([at(x) for x in gs])
        import eqgrid
        u=eqgrid.cr(gs,ys,g) if gmode=='cub' else np.array([np.interp(g,gs,ys[:,i]) for i in range(5)])
        return featba(u,wr)
