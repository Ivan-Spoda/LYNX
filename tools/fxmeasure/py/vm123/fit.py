"""fit.py: LM fit of a dynfit model. fit.run(model, jobs, p0, free) -> p"""
import subprocess, numpy as np, ana, os
LAB=os.path.expanduser('~/.claude/jobs/f8789b49/tmp/vm123/lab'); LOC=os.path.expanduser('~/.claude/jobs/f8789b49/tmp/vm123/loc')
def evaluate(model,jobs,p,want='res',gain=False):
    env=dict(os.environ); 
    if gain: env['ERRGAIN']='1'
    r=subprocess.run([LAB+'/dynfit',model,jobs]+['%.10g'%v for v in p],capture_output=True,text=True,env=env)
    if want=='log': return r.stderr
    res=[]
    for l in r.stdout.splitlines(): res+= [float(v) for v in l.split()[1:]]
    return np.array(res)
def run(model,jobs,p0,free,it=30,verbose=True,bounds=None,gain=False):
    p0=np.array(p0,float); idx=[i for i,f in enumerate(free) if f]
    def f(q):
        p=p0.copy(); p[idx]=q
        if bounds:
            for i,(lo,hi) in bounds.items(): p[i]=min(max(p[i],lo),hi)
        return evaluate(model,jobs,p,gain=gain)
    q,c=ana.lm(f,p0[idx],it=it,eps=1e-4,verbose=False)
    p=p0.copy(); p[idx]=q
    if bounds:
        for i,(lo,hi) in bounds.items(): p[i]=min(max(p[i],lo),hi)
    if verbose: print('cost %.2f dB'%(10*np.log10(c/ max(1,1))), np.round(p,4)); print(evaluate(model,jobs,p,'log'))
    return p
def jobs(path, rows):
    with open(path,'w') as f:
        for r in rows: f.write(' '.join(str(x) for x in r)+'\n')
    return path
