import importlib.util, json, os, sys

# De keuze van de bestanden en de naam van de broncode-zip komen uit de
# proxy zelf: dan bewaakt deze vergelijking ook dat de directe verbinding
# en de proxy dezelfde lijst tonen.
os.environ.setdefault("GITFETCH_SECRET", "test")
here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location(
    "gp", os.path.join(here, "..", "server", "gitfetch_proxy.py"))
gp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gp)

d = json.load(open(sys.argv[1]))
rel = [r for r in d if not r.get('draft') and r.get('tag_name')][:25]
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
    tag = r['tag_name'][:31]
    title = lat1(r.get('name') or '') or tag
    print('R|%s|%s|%d|%s' % (tag, (r.get('published_at') or r['created_at'])[:10],
                             1 if r['prerelease'] else 0, title[:79]))
    items = [(lat1(a['name'])[:63], a['size'], a['browser_download_url'][:159])
             for a in r.get('assets', []) if a.get('name') and a.get('browser_download_url')]
    kept, skipped = gp.select_assets(items)
    if r.get('zipball_url'):
        if len(kept) < gp.MAX_ASSETS_PER_RELEASE:
            kept.append((gp.source_name(r['zipball_url'], tag), 0, r['zipball_url'][:159]))
        else:
            skipped += 1
    for name, size, url in kept:
        print('A|%s|%d|%s' % (name, size, url))
    if skipped:
        print('S|%d' % skipped)
