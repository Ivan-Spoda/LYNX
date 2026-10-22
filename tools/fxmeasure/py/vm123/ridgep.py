"""Ridge Strip (E voicing) parameter list from console settings (mirror of translateWavesSslChannel)."""
import numpy as np
NAMES=['Input','HPF','LPF','LF gain','LF freq.','LF bell','LMF gain','LMF freq.','LMF Q','HMF gain','HMF freq.','HMF Q','HF gain','HF freq.','HF bell','EQ curve','EQ on','Ratio','Comp. threshold','Comp. release','Fast attack','Gate range','Gate threshold','Gate release','Gate fast','Expander','Dynamics on','Order','Filters to SC','Output','Phase','Comp. mix','Voicing','Filter split']
QT=[3.5,3.25,3.0,2.75,2.5,2.25,2.0,1.88,1.75,1.63,1.5,1.37,1.25,1.13,1.0,0.88,0.75,0.62,0.5,0.3,0.1]
def dialQ(q):
    for i in range(20):
        if QT[i]>=q>=QT[i+1]: return (i+(QT[i]-q)/(QT[i]-QT[i+1]))/20
    return 1.0
ln=lambda v,lo,hi: float(np.clip((v-lo)/(hi-lo),0,1)); lg=lambda v,lo,hi: float(np.clip(np.log(v/lo)/np.log(hi/lo),0,1))
def params(thr=0,ratio=1,rel=0.4,fast=0,hp=0,lfg=0,lff=60,lmfg=0,hmfg=0,hmff=3.5,hmfq=1.5,hfg=0,hff=8,eq=1,dyn=1,split=1,expthr=-30,exprange=0,exprel=0.2,expfast=0,gate=0,order=1,out=0):
    p={'Voicing':1,'EQ on':eq,'Dynamics on':dyn,'Filter split':split,'Order':order,'Comp. mix':1,
       'Ratio': 1.0 if ratio>1000 else np.sqrt(np.clip((ratio-1)/19,0,0.998)),'Comp. threshold':ln(thr,-20,10),'Comp. release':lg(rel,0.1,4),'Fast attack':fast,
       'HPF': lg(hp,16,350) if hp>=16.5 else 0,'LF gain':ln(lfg,-15,15),'LF freq.':lg(lff,30,450),'LMF gain':ln(lmfg,-15,15),'HMF gain':ln(hmfg,-15,15),'HMF freq.':lg(hmff*1000,600,7000),'HMF Q':1-dialQ(hmfq),
       'HF gain':ln(hfg,-15,15),'HF freq.':lg(hff*1000,1500,16000),'Gate range':ln(exprange,0,40),'Gate threshold':ln(expthr,-30,10),'Gate release':lg(exprel,0.1,4),'Gate fast':expfast,'Expander':0 if gate else 1,'Output':ln(out,-20,20)}
    return ['%s=%.7f'%(k,v) for k,v in p.items()]
