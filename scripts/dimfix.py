import json,math,sys,collections
from dimcheck import CT, seg_hits_rect, SCALE, CW
LY=json.load(open(__import__('os').path.join(__import__('os').path.dirname(__file__),'data','layers.json'))); NAMES=LY['names']; HID=LY['hidden']
def disc(l):
    n=NAMES.get(str(l),'')
    return 'S' if n.startswith('S-') else 'A'
def ignore_layer(l):
    n=NAMES.get(str(l),'')
    return n.startswith('Z') or '(oculto)' in n
def mkrect(x,y,a,w,h,pad=0.02):
    u=(math.cos(a),math.sin(a)); v=(-math.sin(a),math.cos(a))
    return [(x-u[0]*pad-v[0]*pad, y-u[1]*pad-v[1]*pad),(x+u[0]*(w+pad)-v[0]*pad, y+u[1]*(w+pad)-v[1]*pad),
            (x+u[0]*(w+pad)+v[0]*(h+pad), y+u[1]*(w+pad)+v[1]*(h+pad)),(x-u[0]*pad+v[0]*(h+pad), y-u[1]*pad+v[1]*(h+pad))]
def rect_overlap(P,Q):
    for poly in (P,Q):
        for i in range(4):
            a,b=poly[i],poly[(i+1)%4]; ax=(-(b[1]-a[1]),b[0]-a[0])
            pa=[p[0]*ax[0]+p[1]*ax[1] for p in P]; pb=[p[0]*ax[0]+p[1]*ax[1] for p in Q]
            if max(pa)<min(pb) or max(pb)<min(pa): return False
    return True
def run(db,floor,apply=False,scale=SCALE,verify=False):
    p={'useFloorPlanDatabase':False,'db':db}
    if floor is not None: p['floor']=floor
    D=CT('GetDimensions',p)['dimensions']
    D=[d for d in D if not ignore_layer(d['layer'])]
    texts=[]
    ORIG={}
    try: ORIG={d['guid']:d for d in json.load(open(__import__('os').path.join(__import__('os').path.dirname(__file__),'data','dims_plan.json')))} if db.get('type')=='floorplan' else {}
    except Exception: pass
    for d in D:
        els=d['elems']
        for k,e in enumerate(els):
            if not e['text'].strip() or abs(e['dimVal'])<1e-9 or k==0: continue
            h=e['size']*scale/1000.0; w=len(e['text'])*h*CW
            o=ORIG.get(d['guid'])
            ox,oy=(o['elems'][k]['nx'],o['elems'][k]['ny']) if (o and len(o['elems'])==len(els)) else (e['nx'],e['ny'])
            p0=(els[k-1]['px'],els[k-1]['py']); p1=(e['px'],e['py'])
            texts.append({'d':d,'e':e,'w':w,'h':h,'x':e['nx'],'y':e['ny'],'ox':ox,'oy':oy,'a':e['angle'],'p0':p0,'p1':p1})
    if not texts: return 0,0,[]
    boxes=[]
    for t in texts:
        R=mkrect(t['x'],t['y'],t['a'],t['w'],t['h']); xs=[q[0] for q in R]; ys=[q[1] for q in R]
        boxes.append({'x0':min(xs)-1.2,'y0':min(ys)-1.2,'x1':max(xs)+1.2,'y1':max(ys)+1.2})
    q={'useFloorPlanDatabase':False,'db':db,'boxes':boxes,'margin':1.5}
    if floor is not None: q['floor']=floor
    S=CT('GetDrawnSegments',q)['elements']
    segs=[]  # (seg, guid, layer)
    for el in S:
        if ignore_layer(el['layer']) or HID.get(str(el['layer'])): continue
        sg=el['segs']
        for k in range(0,len(sg),4): segs.append((sg[k:k+4],el['guid'],el['layer'],el['type']))
    def hits(R,t):
        dg=t['d']['guid']; ds=disc(t['d']['layer']); out=[]
        for s,g,l,ty in segs:
            if g==dg: continue
            if disc(l)!=ds and ty!='Cota': 
                if ds=='A' and NAMES.get(str(l),'').startswith('S-'): continue
                if ds=='S' and not NAMES.get(str(l),'').startswith('S-') and ty in ('Cota','Texto','Linha','Polilinha','Elemento de Grelha'): continue
            xs=[q[0] for q in R]; ys=[q[1] for q in R]
            if max(s[0],s[2])<min(xs) or min(s[0],s[2])>max(xs) or max(s[1],s[3])<min(ys) or min(s[1],s[3])>max(ys): continue
            if seg_hits_rect(s,R): out.append(ty)
        return out
    placed={id(t):mkrect(t['x'],t['y'],t['a'],t['w'],t['h']) for t in texts}
    def along(t,x,y):
        a=t['a']; u=(math.cos(a),math.sin(a))
        c=(x+u[0]*t['w']/2, y+u[1]*t['w']/2)
        s0=(t['p0'][0]-c[0])*u[0]+(t['p0'][1]-c[1])*u[1]; s1=(t['p1'][0]-c[0])*u[0]+(t['p1'][1]-c[1])*u[1]
        return min(s0,s1),max(s0,s1)   # signed distances from text centre to the segment ends
    # restart from the original position of any text that was moved before (so every text is re-placed under the rules)
    if verify:
        bad=[t for t in texts if hits(placed[id(t)],t)]
        ovl=sum(1 for t in texts for o in texts if o is not t and id(t)<id(o) and rect_overlap(placed[id(t)],placed[id(o)]))
        return len(texts),len(bad),bad,{'overlaps':ovl}
    for t in texts:
        if abs(t['x']-t['ox'])>1e-6 or abs(t['y']-t['oy'])>1e-6:
            t['x'],t['y']=t['ox'],t['oy']; placed[id(t)]=mkrect(t['x'],t['y'],t['a'],t['w'],t['h'])
    conflicts=[t for t in texts if hits(placed[id(t)],t)]
    moves=collections.defaultdict(list); unresolved=[]
    for t in texts:
        if t in conflicts: continue
        if abs(t['e']['nx']-t['x'])>1e-6 or abs(t['e']['ny']-t['y'])>1e-6:
            moves[t['d']['guid']].append({'i':t['e']['i'],'x':t['x'],'y':t['y']})   # back to original
    for t in conflicts:
        a=t['a']; u=(math.cos(a),math.sin(a)); v=(-math.sin(a),math.cos(a))
        lo,hi=along(t,t['x'],t['y']); seg=hi-lo
        room=max(0.0,(seg-t['w'])/2)              # free slide that keeps the text inside its segment
        cands=[]
        for side,cost_side in ((0,0.0),(1,0.35)):
            dv=0 if side==0 else -(t['h']+0.08)
            for k in range(0,60):
                for sgn in (1,-1):
                    du=sgn*0.025*k
                    nlo,nhi=lo-du,hi-du
                    inside = nlo<=-t['w']/2+1e-6 and nhi>=t['w']/2-1e-6
                    out = max(0.0, (-t['w']/2)-nlo if nlo>-t['w']/2 else 0.0, t['w']/2-nhi if nhi<t['w']/2 else 0.0)
                    if not inside and out>0.6*t['w']: continue   # never more than ~half a text beyond its segment
                    cands.append((abs(du)+cost_side+(0 if inside else 1.0+out*3),du,dv))
        cands.sort()
        best=None
        for cost,du,dv in cands:
            x=t['x']+u[0]*du+v[0]*dv; y=t['y']+u[1]*du+v[1]*dv
            R=mkrect(x,y,a,t['w'],t['h'])
            if hits(R,t): continue
            if any(rect_overlap(R,placed[id(o)]) for o in texts if o is not t): continue
            best=(x,y,R); break
        if best:
            placed[id(t)]=best[2]; t['x'],t['y']=best[0],best[1]
            moves[t['d']['guid']].append({'i':t['e']['i'],'x':best[0],'y':best[1]})
        else:
            unresolved.append(t)
            if abs(t['e']['nx']-t['x'])>1e-6 or abs(t['e']['ny']-t['y'])>1e-6:
                moves[t['d']['guid']].append({'i':t['e']['i'],'x':t['x'],'y':t['y']})
    skipped=[]
    if apply:
        bt={d['guid']:set(e.get('baseType','') for e in d['elems']) for d in D}
        from archicad import ACConnection
        _c=ACConnection.connect(); _T=lambda n,p: _c.commands.ExecuteAddOnCommand(_c.types.AddOnCommandId("TapirCommand",n),p)
        dl={d['guid']:d['layer'] for d in D}
        hidl=sorted(set(dl[g] for g in moves if HID.get(str(dl[g]))))
        for l in hidl: _T('CreateLayers',{'layerDataArray':[{'name':NAMES[str(l)],'isHidden':False,'isLocked':False}],'overwriteExisting':True})
        for g,notes in moves.items():
            if 'Abertura' in bt.get(g,()): skipped.append(g); continue
            r=CT('SetDimensionNotes',{'useFloorPlanDatabase':False,'db':db,'dim':g,'notes':notes})
            if 'moved' not in r:
                print('ERR',g,r)
                if 'points' in str(r) or 'before' in r: raise SystemExit('stopped: undo last change')
        for l in hidl: _T('CreateLayers',{'layerDataArray':[{'name':NAMES[str(l)],'isHidden':True}],'overwriteExisting':True})
    print('skipped (opening-bound):',[g[:8] for g in skipped])
    return len(texts),len(conflicts),unresolved,moves
if __name__=='__main__':
    fl=int(sys.argv[1]); ap='--apply' in sys.argv
    if '--verify' in sys.argv:
        n,nb,bad,x=run({'type':'floorplan'},fl,verify=True); print('VERIFY floor',fl,'texts',n,'still touching lines',nb,'text overlaps',x['overlaps']); [print('  ',t['d']['guid'][:8],t['e']['text']) for t in bad]; raise SystemExit
    n,nc,un,mv=run({'type':'floorplan'},fl,ap)
    print('floor',fl,'texts',n,'conflicts',nc,'moved',sum(len(v) for v in mv.values()),'unresolved',len(un))
    for t in un[:10]: print('  unresolved',t['d']['guid'][:8],t['e']['text'])
    json.dump({g:v for g,v in mv.items()},open(f'moves_f{fl}.json','w'))
