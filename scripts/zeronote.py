import json,sys,math
from dimcheck import CT,seg_hits_rect
TXT=__import__('os').environ.get('ZERO_NOTE','= 100.00 (altitude)')   # text written next to the ±0.00 mark
Z=json.load(open(__import__('os').path.join(__import__('os').path.dirname(__file__),'data','zero_marks.json')))
LY=json.load(open(__import__('os').path.join(__import__('os').path.dirname(__file__),'data','layers.json')))
def skip(el): return LY['hidden'].get(str(el['layer'])) or LY['names'].get(str(el['layer']),'').endswith('.SITE')
def find(db):
    z=Z[db]; e=z['e']; s=z['scale']; h=e['size']*s/1000; w=len(TXT)*h*0.62; w0=len(e['text'])*h*0.62; nx,ny=e['nx'],e['ny']
    S=CT('GetDrawnSegments',{'useFloorPlanDatabase':False,'db':{'type':'section','guid':db},'boxes':[{'x0':nx-w-12*h,'y0':ny-6*h,'x1':nx+w+12*h,'y1':ny+10*h}],'margin':0.5})['elements']
    segs=[el['segs'][k:k+4] for el in S if el['guid']!=z['dim'] and not skip(el) for k in range(0,len(el['segs']),4)]
    # own marker/text area must stay clear too
    own=[(nx-0.3*h,e['py']-0.1*h),(nx+w0+0.3*h,ny+1.3*h)]
    c=[]
    for i in range(-int((w+10*h)/(0.25*h)),int(10*h/(0.25*h))):
        for j in range(-12,40):
            x=nx+w0+0.5*h+i*0.25*h; y=ny+j*0.25*h
            if x+w>own[0][0] and x<own[1][0] and y+h>own[0][1] and y<own[1][1]: continue
            d=math.hypot(x-(nx+w0+0.5*h),(y-ny)*1.5)
            c.append((d,x,y))
    c.sort()
    for d,x,y in c:
        R=[(x-0.3*h,y-0.3*h),(x+w+0.3*h,y-0.3*h),(x+w+0.3*h,y+h+0.3*h),(x-0.3*h,y+h+0.3*h)]
        if not any(seg_hits_rect(sg,R) for sg in segs): return x,y,h,z
    return None
def apply(db):
    r=find(db)
    if not r: return 'sem posição livre'
    x,y,h,z=r
    out=CT('CreateTexts',{'useFloorPlanDatabase':False,'items':[{'text':TXT,'x':x,'y':y,'size':z['e']['size'],'layer':z['layer'],'pen':2}]})
    return (round(x,3),round(y,3),out)
if __name__=='__main__':
    for db in [a for a in sys.argv[1:] if not a.startswith('--')] or Z:
        print(db[:8],apply(db) if '--apply' in sys.argv else find(db)[:2] if find(db) else None)
