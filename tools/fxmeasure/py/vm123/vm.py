"""vm.py: run Waves plugins (official trials, black-box) in fxmeasure; helpers for round 123."""
import sys, os, subprocess, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import sig
W='/mnt/c/Users/kauan/lynx-test/vm123'; S=r'C:\Program Files\Common Files\VST3\WaveShell1-VST3 17.1_x64.vst3'; sr=48000
PL={'ssl':'SSLChannel Stereo','rc':'RCompressor Stereo','de':'DeEsser Stereo','cla':'CLA-2A Stereo'}
def wsig(d,k,x): os.makedirs(d,exist_ok=True); sig.write_wav(d+'/in_%s.wav'%k,x,sr)
def run(plugin, state, variants, signames, d, settle=None, quiet=True, timeout=14400):
    """variants {tag:[cmds]} -> d/<tag>_<sig>.wav ; state = name of st/<state>.bin or None"""
    plugin=PL.get(plugin,plugin)
    lens={}; pos=[0]
    L=['load "%s" 48000 512 DeEsser Mono'%S,'load "%s" 48000 512 %s'%(S,plugin),'nonrealtime 1']
    for tag,cmds in variants.items():
        L+=['defaults']
        if state: L+=['loadcomponent "%s"'%sig.winpath(W+'/st/%s.bin'%state)]
        L+=list(cmds)
        for k in signames:
            n=lens.setdefault(k, len(sig.read_wav(d+'/in_%s.wav'%k)[0]))
            st=SETTLE if settle is None else int(round(settle*48000/512))*512; pos[0]+=st
            # demo mutes (trial): ~0.96 s every 20 s of processed audio, from 961.9k samples; dodge them with extra silence
            while True:
                bad=[m for m in range(0,400) if pos[0] < M0+m*MP+ML+MG and pos[0]+n > M0+m*MP-MG]
                if not bad: break
                m=bad[0]; e=M0+m*MP+ML+MG; add=((e-pos[0])//512+1)*512; st+=add; pos[0]+=add
            L+=['reset','settle %.9f'%((st+0.25)/48000.0),'process "%s" "%s"'%(sig.winpath(d+'/in_%s.wav'%k),sig.winpath(d+'/%s_%s.wav'%(tag,k)))]
            pos[0]+=n
    open(d+'/s.txt','w').write('\n'.join(L)+'\n')
    out=subprocess.run([W+'/fxmeasure.exe',sig.winpath(d+'/s.txt')],capture_output=True,text=True,cwd=W,timeout=timeout).stdout
    if not quiet: print(out)
    bad=[l for l in out.splitlines() if l.startswith('error')]
    if bad: print('\n'.join(bad))
    for tag in variants:
        for k in signames:
            y=rd(d,tag,k)[:,0]; z=[r for r in zeroruns(y) if r[1]<len(y)]
            if z: print('WARNING mute in',tag,k,z)
    return out
M0=961888; MP=960000; ML=46200; MG=12000; SETTLE=24576
def rd(d,tag,k): return sig.read_wav(d+'/%s_%s.wav'%(tag,k))[0]
def zeroruns(y, minlen=2400):
    z=np.abs(y)<1e-9; dd=np.diff(np.concatenate([[0],z.astype(int),[0]])); s=np.where(dd==1)[0]; e=np.where(dd==-1)[0]
    return [(a,b) for a,b in zip(s,e) if b-a>minlen]
