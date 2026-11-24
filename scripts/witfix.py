"""Shorten dimension witness lines to ~8 mm on paper. usage: witfix.py [--apply] [--only TYPE] [--limit N]"""
import json,sys,math
from dimcheck import CT
from archicad import ACConnection
MM=8.0; EXTM=0.0935; TOL=0.5   # EXTM: overshoot past dim line, model metres   # paper mm: target, overshoot past dim line, tolerance
LY=json.load(open(__import__('os').path.join(__import__('os').path.dirname(__file__),'data','layers.json'))); NAMES=LY['names']
c=ACConnection.connect(); T=lambda n,p: c.commands.ExecuteAddOnCommand(c.types.AddOnCommandId("TapirCommand",n),p)
hidden={a['name']:a for a in []}
def layer_hidden():
    return {k:{'name':NAMES[k],'isHidden':bool(v)} for k,v in LY['hidden'].items()}
def plan(db,floor=None):
    p={'useFloorPlanDatabase':False}
    if db: p['db']=db
    if floor is not None: p['floor']=floor
    r=CT('GetDimensions',p); s=r.get('scale',100.0); items=[]; nlines=0
    for d in r['dimensions']:
        dx,dy=d['dirX'],d['dirY']; L=math.hypot(dx,dy) or 1; nx,ny=-dy/L,dx/L
        els=[]
        for e in d['elems']:
            dist=abs((e['bx']-e['px'])*nx+(e['by']-e['py'])*ny)
            f,v=e['witnessForm'],e['witnessVal']
            if f==2: vis=dist-v
            elif f==3: vis=v+EXTM
            else: continue
            if vis*1000/s > MM+TOL and dist*1000/s > MM+TOL:
                els.append({'i':e['i'],'form':3,'val':MM*s/1000-EXTM}); nlines+=1
        if els: items.append({'dim':d['guid'],'layer':d['layer'],'elems':els,'n':len(d['elems'])})
    return s,items,nlines
if __name__=='__main__':
    ap='--apply' in sys.argv; only=sys.argv[sys.argv.index('--only')+1] if '--only' in sys.argv else None
    limit=int(sys.argv[sys.argv.index('--limit')+1]) if '--limit' in sys.argv else None
    targets=[] if only not in (None,'floorplan') else [(None,'planta')]
    for db in json.load(open(__import__('os').path.join(__import__('os').path.dirname(__file__),'data','dbs.json'))):
        if db['type'] in ('section','elevation','interiorelevation','detail','worksheet') and (only in (None,db['type'])):
            targets.append(({'type':db['type'],'guid':db['guid']},db['ref']))
    LA=layer_hidden(); log=[]
    for db,name in targets:
        try: s,items,nl=plan(db)
        except Exception as ex: print(f'{name}: erro ao ler ({ex!r})'); continue
        if limit: items=items[:limit]
        print(f'{name}: escala 1:{s:g}, {len(items)} cotas, {nl} linhas a encurtar')
        if not ap or not items: continue
        hid=sorted({str(it['layer']) for it in items if LA.get(str(it['layer']),{}).get('isHidden')})
        for l in hid: T('CreateLayers',{'layerDataArray':[{'name':LA[l]['name'],'isHidden':False,'isLocked':False}],'overwriteExisting':True})
        try:
            p={'useFloorPlanDatabase':False,'items':[{'dim':it['dim'],'elems':it['elems']} for it in items]}
            if db: p['db']=db
            r=CT('SetDimensionWitness',p); log.append({'view':name,'r':r})
            bad=[x for x in r['results'] if x['err'] or x['before']!=x['after']]
            print('   alteradas',len(r['results']),'problemas',bad, r.get('error',''))
            if 'error' in r: raise SystemExit('PARADO: pontos alterados')
        finally:
            for l in hid: T('CreateLayers',{'layerDataArray':[{'name':LA[l]['name'],'isHidden':True}],'overwriteExisting':True})
    json.dump(log,open(__import__('os').path.join(__import__('os').path.dirname(__file__),'data','witfix_log.json'),'w'),indent=1)
