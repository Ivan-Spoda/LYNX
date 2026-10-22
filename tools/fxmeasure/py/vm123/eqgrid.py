import pickle, numpy as np, vm, an2, bqf, re
G=[-15,-12,-9,-6,-4.5,-3,-1.5,0,1.5,3,4.5,6,9,12,15]
F=[round(x,3) for x in np.linspace(0,1,11)]
Q=[0.0,0.1,0.2,0.3,0.4,0.5,0.6,0.7,0.8,0.9,0.95,1.0]
def textlaw(param):
    L=[l.split('\t') for l in open(vm.W+'/params/ssl.tsv').read().splitlines()]
    row=[x for x in L[1:] if x[2]==param][0]; vals=row[10:]
    v=np.linspace(0,1,1001); t=[]
    for s in vals:
        m=re.search(r'-?[\d.]+',s); t.append(float(m.group()) if m else np.nan)
    return v,np.array(t)
def feats(n,d,wt):
    n=n/d[0]; d=d/d[0]
    return np.array([n[0], n[1]/wt, n[2]/wt**2, d[1]/wt, d[2]/wt**2])
def unfeats(v,wt):
    n=np.array([v[0],v[1]*wt,v[2]*wt*wt]); d=np.array([1,v[3]*wt,v[4]*wt*wt]); return an2.digital(n,d)
def grid(name, band):
    R=pickle.load(open(vm.W+'/%s.pkl'%name,'rb'))
    vf,tf=textlaw('%s Frq'%band.upper()); unit=1000 if band in ('lmf','hmf','hf') else 1
    wt=lambda f: 2*np.pi*np.interp(f,vf,tf)*unit
    bell=band in ('lmf','hmf')
    out={}
    for k,(n,d,e,b,a) in R.items():
        if bell: g,f,q=k; key=(g,f,q)
        else: t,g,f=k; key=(t,g,f)
        out[key]=feats(n,d,wt(f))
    # g=0 nodes: flat
    if bell:
        for f in F:
            for q in Q:
                dd=0.5*(out[(-1.5,f,q)]+out[(1.5,f,q)]); out[(0,f,q)]=np.array([1,dd[3],dd[4],dd[3],dd[4]])
    else:
        for t in ('Off','On'):
            for f in F:
                dd=0.5*(out[(t,-1.5,f)]+out[(t,1.5,f)]); out[(t,0,f)]=np.array([1,dd[3],dd[4],dd[3],dd[4]])
    return out, wt
def cr(xs, ys, x):
    """Catmull-Rom (non-uniform via local linear param) interpolation of arrays ys over sorted xs at x"""
    xs=np.asarray(xs); i=np.searchsorted(xs,x)-1; i=min(max(i,0),len(xs)-2)
    t=(x-xs[i])/(xs[i+1]-xs[i])
    p1,p2=ys[i],ys[i+1]
    p0=ys[i-1] if i>0 else 2*p1-p2
    p3=ys[i+2] if i+2<len(xs) else 2*p2-p1
    # tangents scaled for nonuniform spacing
    h=xs[i+1]-xs[i]
    m1=(p2-p0)/((xs[i+1]-(xs[i-1] if i>0 else xs[i]-h)))*h
    m2=(p3-p1)/(((xs[i+2] if i+2<len(xs) else xs[i+1]+h)-xs[i]))*h
    t2=t*t; t3=t2*t
    return (2*t3-3*t2+1)*p1+(t3-2*t2+t)*m1+(-2*t3+3*t2)*p2+(t3-t2)*m2
