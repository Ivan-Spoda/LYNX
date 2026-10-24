# build a VST3 host state (JUCE XML) for a Soundtoys plugin from a VST2 chunk text taken from an FL project
# usage: stwrap.py template.state her.bin out.state   (template = savestate of the same plugin in the trial)
import sys, re, struct
sys.path.insert(0, sys.argv[0].rsplit('/',1)[0])
import jucestate as J
t=open(sys.argv[1],'rb').read(); x=t[8:].decode()
comp=J.dec(re.search("<IComponent>(.*?)</IComponent>",x).group(1))
i=comp.index(b'WIDGET'); hdr=bytearray(comp[:i-4])
her=open(sys.argv[2],'rb').read(); her=her[her.index(b'WIDGET'):]
c=hdr.index(b'CcnK')
new=hdr+struct.pack('>I',len(her))+her
struct.pack_into('>I',new,c+4,len(new)-(c+8))
x=re.sub("<IComponent>(.*?)</IComponent>","<IComponent>"+J.enc(bytes(new))+"</IComponent>",x)
xb=x.encode(); open(sys.argv[3],'wb').write(t[:4]+struct.pack('<I',len(xb))+xb)
