import json,subprocess,math,sys
def CT(n,p):
    o=subprocess.run(['python3',__import__('os').path.join(__import__('os').path.dirname(__import__('os').path.abspath(__file__)),'..','client','ct.py'),n,json.dumps(p)],capture_output=True,text=True).stdout
    return json.loads(o)["result"]["addOnCommandResponse"]
SCALE=100.0; CW=0.62   # char width / height
def rect(e):
    h=e['size']*SCALE/1000.0; n=max(1,len(e['text'])); w=n*h*CW
    a=e['angle']; u=(math.cos(a),math.sin(a)); v=(-math.sin(a),math.cos(a))
    x,y=e['nx'],e['ny']; pad=0.02
    pts=[(x-u[0]*pad-v[0]*pad, y-u[1]*pad-v[1]*pad),(x+u[0]*(w+pad)-v[0]*pad, y+u[1]*(w+pad)-v[1]*pad),
         (x+u[0]*(w+pad)+v[0]*(h+pad), y+u[1]*(w+pad)+v[1]*(h+pad)),(x-u[0]*pad+v[0]*(h+pad), y-u[1]*pad+v[1]*(h+pad))]
    return pts,u,v,w,h
def seg_hits_rect(s,pts):
    # separating axis test between segment and convex quad
    def proj(ax,P): vals=[p[0]*ax[0]+p[1]*ax[1] for p in P]; return min(vals),max(vals)
    S=[(s[0],s[1]),(s[2],s[3])]
    axes=[]
    for i in range(4):
        a,b=pts[i],pts[(i+1)%4]; axes.append((-(b[1]-a[1]),b[0]-a[0]))
    dx,dy=s[2]-s[0],s[3]-s[1]; axes.append((-dy,dx))
    for ax in axes:
        if abs(ax[0])+abs(ax[1])<1e-12: continue
        a0,a1=proj(ax,pts); b0,b1=proj(ax,S)
        if a1<b0 or b1<a0: return False
    return True
def check(db, floor=None):
    p={'useFloorPlanDatabase':False,'db':db}
    if floor is not None: p['floor']=floor
    D=CT('GetDimensions',p)['dimensions']
    items=[]
    for d in D:
        for e in d['elems']:
            if not e['text'] or e['text'].strip()=='' : continue
            pts,u,v,w,h=rect(e); items.append((d,e,pts))
    if not items: return D,[]
    boxes=[{'x0':min(q[0] for q in it[2]),'y0':min(q[1] for q in it[2]),'x1':max(q[0] for q in it[2]),'y1':max(q[1] for q in it[2])} for it in items]
    q={'useFloorPlanDatabase':False,'db':db,'boxes':boxes,'margin':1.5}
    if floor is not None: q['floor']=floor
    S=CT('GetDrawnSegments',q)['elements']
    conf=[]
    for d,e,pts in items:
        hits=[]
        for el in S:
            if el['guid']==d['guid']: continue
            sg=el['segs']
            for k in range(0,len(sg),4):
                if seg_hits_rect(sg[k:k+4],pts): hits.append((el['type'],el['guid'],el['layer'])); break
        if hits: conf.append((d['guid'],e['i'],e['text'],hits))
    return D,conf
if __name__=='__main__':
    D,conf=check({'type':'floorplan'},int(sys.argv[1]) if len(sys.argv)>1 else 0)
    print(len(D),'dims',len(conf),'texts in conflict')
    import collections
    print(collections.Counter(h[0] for c in conf for h in c[3]))
    for c in conf[:15]: print(c[0][:8],c[1],c[2],[(h[0],h[2]) for h in c[3]][:4])
