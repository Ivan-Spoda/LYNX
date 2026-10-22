import irs2, numpy as np, sys
G=[round(-15+1.5*i,1) for i in range(21)]
F=[round(x,3) for x in np.linspace(0,1,11)]
F21=[round(x,3) for x in np.linspace(0,1,21)]
Q=[0.0,0.1,0.2,0.3,0.4,0.5,0.6,0.7,0.8,0.9,0.95,1.0]
B=['settext DynamicBypass On']
which=sys.argv[1]; C={}
if which in ('hmf','lmf'):
    P=which.upper()
    for g in G:
        if g==0: continue
        for f in F:
            for q in Q: C[(g,f,q)]=B+['settext "%s Gain" %g'%(P,g),'set "%s Frq" %g'%(P,f),'set "%s Q" %g'%(P,q),'get "%s Gain"'%P,'get "%s Frq"'%P,'get "%s Q"'%P]
else:
    P=which.upper()
    for t in ['Off','On']:
        for g in G:
            if g==0: continue
            for f in F21: C[(t,g,f)]=B+['settext "%s Type" %s'%(P,t),'settext "%s Gain" %g'%(P,g),'set "%s Frq" %g'%(P,f),'get "%s Gain"'%P,'get "%s Frq"'%P]
irs2.measure('ssl',{str(k):v for k,v in C.items()},'sslg2_'+which)
