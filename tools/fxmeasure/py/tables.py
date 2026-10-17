import json, numpy as np
import os; DATA = os.environ.get('FXMEASURE_DATA', '../data')
D=DATA + '/'
out=[]
P=lambda s='':out.append(s)
# Pro-C static
pc=json.load(open(D+'pc_static.json'))
lv=pc[0]['in']; cols=[i for i,v in enumerate(lv) if round(v) in (-30,-24,-18,-14,-10,-6,-2,0)]
P('| Style | knee | ratio | '+' | '.join('%d'%round(lv[i]) for i in cols)+' |'); P('|'+'---|'*(3+len(cols)))
for c in pc:
    if c['knee'] in (0,12,48) and c['ratio'] in (4,) or c['style'] in ('Clean','Opto') and c['knee']==0:
        P('| %s | %g | %g | '%(c['style'],c['knee'],c['ratio'])+' | '.join('%.2f'%(c['out'][i]-c['in'][i]) for i in cols)+' |')
P()
pt=json.load(open(D+'pc_time.json'))
P('| Style | attack | release | hold | lookahead | 50 % att | 90 % att | 50 % rel | 90 % rel | GR at −6 dBFS |'); P('|'+'---|'*10)
for c in pt:
    P('| %s | %s | %s | %s | %s | %.2f | %.2f | %.1f | %.1f | %.2f |'%(c['style'],c['att'],c['rel'],c['hold'],c.get('look','–'),c['att_0.5'],c['att_0.9'],c['rel_0.5'],c['rel_0.9'],c['gr_hold_db']))
P()
pl=json.load(open(D+'pl_time.json'))
P('| Style | release ms | attack ms | net gain (dB, +10 dB drive) | 50 % attack ms | 90 % attack ms | +3 dB recovery ms | +6 dB recovery ms |'); P('|'+'---|'*8)
for c in pl:
    P('| %s | %s | %s | %.2f | %.2f | %.2f | %s | %s |'%(c['style'],c.get('rel',500),c.get('att',250),c['gr_db'],c['att_0.5'],c['att_0.9'],c['rel_3db'],c['rel_6db']))
P()
sat=json.load(open(D+'sat.json'))
P('| Style | drive % | level dBFS | f Hz | fundamental gain dB | THD % | H2 | H3 | H4 | H5 | H7 | DC |'); P('|'+'---|'*12)
for c in sat:
    if c['drive'] in (0,50,100) and c['level']==-6 and c['f']==1000:
        h=c['h_db']; P('| %s | %d | %d | %d | %.2f | %.2f | %.1f | %.1f | %.1f | %.1f | %.1f | %.4f |'%(c['style'],c['drive'],c['level'],c['f'],c['gain_db'],c['thd_pct'],h[0],h[1],h[2],h[3],h[5],c['dc']))
P()
P('| Style | gain 100 Hz | THD 100 Hz % | gain 1 kHz | THD 1 kHz % | gain 5 kHz | THD 5 kHz % |  (drive 50 %, −6 dBFS)'); P('|'+'---|'*7)
for st in dict.fromkeys(c['style'] for c in sat):
    r={c['f']:c for c in sat if c['style']==st and c['drive']==50 and c['level']==-6}
    P('| %s | '%st+' | '.join('%.2f | %.2f'%(r[f]['gain_db'],r[f]['thd_pct']) for f in (100,1000,5000))+' |')
open(D+'tables.md','w').write('\n'.join(out))
print(len(out))
