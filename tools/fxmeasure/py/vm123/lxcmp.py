"""run LYNX's substitute (fxlynx, translated from a state) on signals and compare to the real plugin's renders"""
import subprocess, os, numpy as np, vm, ana, fit
FX=os.path.expanduser('~/.claude/jobs/f8789b49/tmp/vm123/fxlynx')
def lynx(effect, state, inwav, outwav, params=()):
    env=dict(os.environ,FXQ='1')
    if state: env['SUBST_STATE']=vm.W+'/st/%s.bin'%state
    r=subprocess.run([FX,effect,inwav,outwav]+list(params),env=env,capture_output=True,text=True)
    if r.returncode: print(r.stderr)
    return vm.sig.read_wav(outwav)[0]
def compare(ref, y, lat=0, lo=40, hi=16000):
    n=min(len(ref)-lat,len(y)); r=ref[lat:lat+n,0]; m=y[:n,0]
    nl=ana.null_db(r,m)
    lv=10*np.log10((r**2).sum()/(m**2).sum())
    sd,_=ana.spec_diff(r,m,lo=lo,hi=hi); ed=ana.env_diff(r,m)
    return dict(null=nl,level=lv,spec_max=np.max(np.abs(sd)),spec_rms=np.sqrt((sd**2).mean()),env_p95=np.percentile(np.abs(ed),95))
def fmt(c): return 'null %6.1f dB  level %+.2f  spec max %.2f rms %.2f  env95 %.2f'%(c['null'],c['level'],c['spec_max'],c['spec_rms'],c['env_p95'])
