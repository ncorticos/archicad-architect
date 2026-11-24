"""Call ClaudeTools add-on commands: python3 ct.py <Command> '<json params>'"""
import json, sys, urllib.request
def call(cmd, params=None, port=19723):
    body = {"command": "API.ExecuteAddOnCommand", "parameters": {
        "addOnCommandId": {"commandNamespace": "ClaudeTools", "commandName": cmd},
        "addOnCommandParameters": params or {}}}
    req = urllib.request.Request(f"http://127.0.0.1:{port}", json.dumps(body).encode(), {"Content-Type": "application/json"})
    return json.load(urllib.request.urlopen(req, timeout=120))
if __name__ == "__main__":
    print(json.dumps(call(sys.argv[1], json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}), indent=1))
