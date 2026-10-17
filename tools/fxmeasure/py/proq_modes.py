import sys; sys.path.insert(0,'.')
from proq_meas import *
sub=[c for c in configs() if c['slope'] in ('12','24') and c['f'] in (1000,10000) and c['q'] in (0.5,1,2,5) and c['g'] in (0,-12,12)]
print(len(sub))
for mode in ['Natural Phase','Linear Phase']:
    run(mode,48000,sub,'pq_%s_48000'%mode.split()[0].lower())
for sr in (44100,96000):
    run('Zero Latency',sr,sub,'pq_zero_%d'%sr)
