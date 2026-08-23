import json, sys
d = json.load(open(sys.argv[1]))
rel = [r for r in d if not r.get('draft')][:25]
print('releases=%d dropped=0' % len(rel))
def lat1(s):
    if not s: return ''
    out=[]
    P={0x2018:"'",0x2019:"'",0x201a:"'",0x201b:"'",0x201c:'"',0x201d:'"',0x201e:'"',0x201f:'"',
       0x2013:'-',0x2014:'-',0x2015:'-',0x2212:'-',0x2026:'...',0x2022:'*',0xb7:'*',
       0x2192:'->',0x2190:'<-',0xa0:' ',0x2009:' ',0x200b:''}
    for ch in s:
        cp=ord(ch)
        if cp in P: out.append(P[cp])
        elif 0x20<=cp<=0xff and cp!=0x7f: out.append(ch)
    return ''.join(out)
for r in rel:
    title = lat1(r.get('name') or '') or r['tag_name']
    print('R|%s|%s|%d|%s' % (r['tag_name'][:31], (r.get('published_at') or r['created_at'])[:10],
                             1 if r['prerelease'] else 0, title[:79]))
    for a in r.get('assets',[])[:12]:
        print('A|%s|%d|%s' % (a['name'][:63], a['size'], a['browser_download_url'][:159]))
