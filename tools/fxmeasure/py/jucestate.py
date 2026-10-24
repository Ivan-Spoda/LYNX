# JUCE VST3 host state <-> raw IComponent bytes (JUCE MemoryBlock base64 "size.chars")
import sys, re
A=".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"
def dec(s):
    n,_,c=s.partition('.'); n=int(n); out=bytearray(n); pos=0
    for ch in c:
        v=A.index(ch)
        for b in range(6):
            if pos>=n*8: break
            if v>>b&1: out[pos>>3]|=1<<(pos&7)
            pos+=1
    return bytes(out)
def enc(b):
    bits=len(b)*8; chars=[]
    for p in range(0,bits,6):
        v=0
        for k in range(6):
            q=p+k
            if q<bits and b[q>>3]>>(q&7)&1: v|=1<<k
        chars.append(A[v])
    return "%d.%s"%(len(b),"".join(chars))
if __name__=="__main__":
    if sys.argv[1]=="dump":
        d=open(sys.argv[2],'rb').read()
        x=d[8:].decode('utf-8','replace')
        for tag in ("IComponent","IEditController"):
            m=re.search("<%s>(.*?)</%s>"%(tag,tag),x)
            if m: sys.stdout.buffer.write(b"== "+tag.encode()+b"\n"+dec(m.group(1))+b"\n")
    elif sys.argv[1]=="make": # make template.state rawcomponent.bin out.state
        d=open(sys.argv[2],'rb').read(); x=d[8:].decode()
        raw=open(sys.argv[3],'rb').read()
        x=re.sub("<IComponent>(.*?)</IComponent>","<IComponent>"+enc(raw)+"</IComponent>",x)
        xb=x.encode(); hdr=d[:4]+len(xb).to_bytes(4,'little')
        open(sys.argv[4],'wb').write(hdr+xb)
