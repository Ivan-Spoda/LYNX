import json, numpy as np, fit, sys
res=json.load(open(fit.LOC+'/sslcfg3.json'))+json.load(open(fit.LOC+'/sslcfg4.json'))
R={r[0]:r for r in res}
T=[10,5,0,-1.3,-5,-10,-15,-20]; RN=['1.2','1.5','1.8','2.0','3.0','4.0','8.0','+Inf']; U=[1-1/float(r) if r!='+Inf' else 1.0 for r in RN]
rows=[]
for t in T:
    row=[]
    for rn in RN:
        tag,thr,r,rel,fast,mk,p,tot=R['s3_t%s_r%s'%(t,rn)]
        slope=p[3] if rn=='+Inf' else p[2]*(float(rn)-1)
        row.append([p[0],p[1],slope,p[6],p[8],mk])
    rows.append(row)
A=np.array(rows)  # [thr][ratio][6]
relK=[0.1,0.2,0.35,0.6,1.2,2,4]
R2=json.load(open(fit.LOC+'/sslrel2.json'))
def two(f):
    a=[];t1=[];sl=[];at=[]
    for r in relK:
        q=R2['s3_f%s_rel%s'%(f,r)][0]
        if q[12]<0.2: q=list(q); q[12]=0.0
        a.append(q[12]); t1.append(q[13]); sl.append(q[8]*r*1000); at.append(q[7] if f=='Off' else q[6])
    return a,t1,sl,at
fa,ft1,fsl,fat=two('On'); sa,st1,ssl,sat=two('Off')
ref=A[T.index(-10),RN.index('4.0')]
def arr(n,a): a=np.ravel(a); return 'constexpr double %s[%d] = { %s };\n'%(n,len(a),', '.join('%.6g'%v for v in a))
s='''// E-type console dynamics laws (round 123), fitted black-box on the official SSL E-Channel trial (feedback
// compressor model; per setting: sine stairs, 1 kHz bursts, pink noise and her take; tmp/vm123/py/sslcfg3.py, gendyn.py).
// Grid: console threshold x (1 - 1/ratio); per node: threshold offset (dB), knee (dB), slope, fast attack (ms),
// release factor (x release knob, at 0.35 s) and the console's automatic make-up (dB, measured on the stairs).
'''
s+=arr('compThr',T)+arr('compU',U)+arr('compGrid',A)+arr('relKnob',relK)+arr('relFracFast',fa)+arr('relTau1Fast',ft1)+arr('relSlowFast',fsl)+arr('relFracSlow',sa)+arr('relTau1Slow',st1)+arr('relSlowSlow',ssl)+arr('attSlowMs',sat)
s+='constexpr double refAttackFast = %.6g, refReleaseFactor = %.6g;\n'%(ref[3],R['s3_t-10_r4.0'][6][8])
s+=open(sys.argv[2]).read()
open(sys.argv[1],'w').write(s)
print(np.round(A[:,:,5],2))
