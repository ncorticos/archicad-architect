import json
from archicad import ACConnection
from dimcheck import CT
c=ACConnection.connect(); T=lambda n,p: c.commands.ExecuteAddOnCommand(c.types.AddOnCommandId("TapirCommand",n),p)
LY=json.load(open(__import__('os').path.join(__import__('os').path.dirname(__file__),'data','layers.json'))); NAMES=LY['names']; HID=LY['hidden']
log=[]
for db in json.load(open(__import__('os').path.join(__import__('os').path.dirname(__file__),'data','dbs.json'))):
    if db['type'] not in ('section','elevation','interiorelevation','detail'): continue
    try: T('ChangeWindow',{'databaseId':{'guid':db['guid']}})
    except Exception as ex: print(db['ref'] or db['name'],'sem janela',ex); continue
    d=T('GetDetailsOfElements',{'filter':{'types':['Dimension']},'fields':['elementId','layerIndex']})['detailsOfElements']
    if not d: continue
    lay={x['elementId']['elementId']['guid']:x['layerIndex'] for x in d}
    dd=T('GetDimensionData',{'elements':[{'elementId':x['elementId']['elementId']} for x in d]})['dimensionsData']
    el=[x for x in dd if x['appearance']=='Elevation' and x['elevationOrigin']!='ReferenceLevel1']
    if not el: continue
    hid=sorted({str(lay[x['elementId']['guid']]) for x in el if HID.get(str(lay[x['elementId']['guid']]))})
    for l in hid: T('CreateLayers',{'layerDataArray':[{'name':NAMES[l],'isHidden':False,'isLocked':False}],'overwriteExisting':True})
    try:
        ids=[{'elementId':x['elementId']} for x in el]
        r=T('ModifyAssociativeDimensions',{'dimensionsWithDetails':[{'elementId':x['elementId'],'elevationOrigin':'ReferenceLevel1'} for x in el]})
        for v in ({'x':0.001,'y':0,'z':0},{'x':-0.001,'y':0,'z':0}):
            T('MoveElements',{'elementsWithMoveVectors':[{'elementId':x['elementId'],'moveVector':v} for x in el]})
    finally:
        for l in hid: T('CreateLayers',{'layerDataArray':[{'name':NAMES[l],'isHidden':True}],'overwriteExisting':True})
    after={x['guid']:x for x in CT('GetDimensions',{'useFloorPlanDatabase':False,'db':{'type':db['type'],'guid':db['guid']}})['dimensions']}
    for x in el:
        g=x['elementId']['guid']; a=after.get(g)
        print(db['ref'] or db['name'],g[:8],'ok' if all(v.get('success') for v in r['executionResults']) else r,
              [w['dimensionValue'] for w in x['witnessPoints']],'->',[e['text'] for e in a['elems']] if a else 'DESAPARECEU')
        log.append({'db':db['guid'],'dim':g})
json.dump(log,open(__import__('os').path.join(__import__('os').path.dirname(__file__),'data','elevfix_log.json'),'w'))
T('ChangeWindow',{'windowType':'FloorPlan','storyIndex':0})
