"""Write data/layers.json and data/dbs.json for the open project (needed by the other scripts).
Requires the `archicad` Python package (pip install archicad) and the Tapir add-on, plus ClaudeTools."""
import json, os
from archicad import ACConnection
from dimcheck import CT
here = os.path.dirname(os.path.abspath(__file__)); os.makedirs(os.path.join(here, 'data'), exist_ok=True)
c = ACConnection.connect()
T = lambda n, p: c.commands.ExecuteAddOnCommand(c.types.AddOnCommandId("TapirCommand", n), p)
attrs = T('GetAttributesByType', {'attributeType': 'Layer'})['attributes']
det = T('GetLayerAttributes', {'attributeIds': [{'attributeId': a['attributeId']} for a in attrs]})
info = det.get('layerAttributes') or det.get('attributes') or []
names, hidden = {}, {}
for a, d in zip(attrs, info):
    d = d.get('layerAttribute', d)
    names[str(a['index'])] = a['name']; hidden[str(a['index'])] = bool(d.get('isHidden'))
json.dump({'names': names, 'hidden': hidden}, open(os.path.join(here, 'data', 'layers.json'), 'w'), indent=1, ensure_ascii=False)
json.dump(CT('ListDatabases', {'useFloorPlanDatabase': False})['databases'], open(os.path.join(here, 'data', 'dbs.json'), 'w'), indent=1, ensure_ascii=False)
print(len(names), 'layers,', sum(hidden.values()), 'hidden; databases written')
