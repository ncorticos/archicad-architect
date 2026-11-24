#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = [
#   "mcp>=1.10,<2",
# ]
# ///
"""
Arquiteto NDC MCP server (ClaudeTools add-on) — exposes the ClaudeTools Archicad add-on (JSON API, port 19723)
as one MCP tool per add-on command, plus `batch` and `ping`.

- Commands and their descriptions are read from the add-on source (Src/*.cpp, *.inc):
  the registration table gives the names, the `// { ... }` comment above each handler
  gives the parameters. Rebuilding the add-on and restarting this server keeps them in sync.
- One keep-alive HTTP connection to Archicad, reopened when Archicad drops it.
- `output_file` (any tool): write the full JSON result to that file and return a short summary,
  for large answers such as GetDimensions or GetDrawnSegments.
- SaveProject / SaveProjectAs refuse to run unless `user_approved: true` is passed
  (the user's explicit approval is required to save the project).
"""

import http.client
import json
import os
import re
import sys
import time
from pathlib import Path
from typing import Any

from mcp.server import Server
from mcp.server.stdio import stdio_server
from mcp import types

HOST = os.environ.get("CLAUDETOOLS_HOST", "127.0.0.1")
PORT = int(os.environ.get("CLAUDETOOLS_PORT", "19723"))
TIMEOUT = float(os.environ.get("CLAUDETOOLS_TIMEOUT", "600"))
_here = Path(__file__).resolve().parent.parent
SRC = Path(os.environ.get("CLAUDETOOLS_SRC", _here / "addon" / "Src" if (_here / "addon" / "Src").exists() else _here / "Src"))

NEEDS_APPROVAL = {"SaveProject", "SaveProjectAs"}
DESTRUCTIVE = {"DeleteElements", "RebuildDimension", "SetPolygon", "SetRoof", "SaveProject", "SaveProjectAs"}
READ_ONLY_PREFIXES = ("Get", "List", "Find", "Far", "Ping")

# Notes added to what the source comments say.
EXTRA = {
    "GetDimensions": "Pass useFloorPlanDatabase:false; optional floor:int and db:{type,guid} (type: floorplan|section|elevation|interiorelevation|detail|worksheet|layout). Returns scale and, per point, base element, note position/text and witnessForm/witnessVal.",
    "GetDrawnSegments": "{ useFloorPlanDatabase:false, db?, floor?, boxes:[{x0,y0,x1,y1}], margin } -> real drawn line segments of every element near the boxes. Mesh/terrain fills come back as many triangles.",
    "ListDatabases": "{} -> sections, elevations, interior elevations, details, worksheets and layouts with type/guid/title/ref. Pass the guid back as db:{type,guid}.",
    "SetDimensionNotes": "Moves dimension texts (fixed position). Pass useFloorPlanDatabase:false. Points bound to wall Openings are lost by the change: rebuild such dimensions with RebuildDimension instead.",
    "SetDimensionWitness": "Pass useFloorPlanDatabase:false. Witness form: 0 None, 1 Small, 2 Large (gap to element), 3 Fix (fixed length). For 8 mm on paper: form 3, val = 8*scale/1000 - 0.0935 (metres). Dimensions with points on section elements are refused (unchanged).",
    "RebuildDimension": "Pass useFloorPlanDatabase:false. Needs >= 2 points. Save the dimension's data first (GetDimensions) so it can be rebuilt again.",
    "SaveProject": "Saves the project. Only with the user's explicit approval: pass user_approved:true.",
    "SaveProjectAs": "Saves a copy as a new PLN. Only with the user's explicit approval: pass user_approved:true.",
    "Ping": "{} -> add-on version and command list.",
}

GENERAL = ("Most commands switch to the floor-plan database first; pass useFloorPlanDatabase:false "
           "to act on the current or given db (sections, elevations, layouts), or layoutDatabase:<guid>. "
           "Elements on hidden layers cannot be changed. The Archicad API has no Undo for these changes.")


def read_commands() -> dict[str, str]:
    """Command name -> description, parsed from the add-on source."""
    files = [SRC / "ClaudeTools.cpp", SRC / "v2.inc", SRC / "v3.inc"]
    text = "\n".join(f.read_text(errors="replace") for f in files if f.exists())
    names = re.findall(r'\{\s*"([A-Za-z0-9]+)",\s*(?:v3::)?[A-Za-z0-9]+\s*\}', text)
    docs: dict[str, str] = {}
    lines = text.splitlines()
    for i, line in enumerate(lines):
        m = re.match(r"GS::ObjectState\s+([A-Za-z0-9]+)\s*\(const GS::ObjectState", line)
        if not m:
            continue
        com = []
        j = i - 1
        while j >= 0 and lines[j].strip().startswith("//"):
            s = lines[j].strip()[2:].strip()
            if s and not set(s) <= set("-=") and "ClaudeTools 2." not in s:
                com.insert(0, s)
            j -= 1
        docs[m.group(1)] = " ".join(com)
    out = {}
    for n in dict.fromkeys(names):
        d = " ".join(x for x in (docs.get(n, ""), EXTRA.get(n, "")) if x)
        out[n] = d or "ClaudeTools command."
    return out


class Archicad:
    def __init__(self) -> None:
        self.conn: http.client.HTTPConnection | None = None

    def _post(self, body: bytes) -> dict:
        if self.conn is None:
            self.conn = http.client.HTTPConnection(HOST, PORT, timeout=TIMEOUT)
        self.conn.request("POST", "/", body, {"Content-Type": "application/json", "Connection": "keep-alive"})
        resp = self.conn.getresponse()
        data = resp.read()
        if resp.getheader("Connection", "").lower() == "close":
            self.close()
        return json.loads(data)

    def close(self) -> None:
        if self.conn is not None:
            try:
                self.conn.close()
            finally:
                self.conn = None

    def call(self, command: str, params: dict | None) -> dict:
        body = json.dumps({"command": "API.ExecuteAddOnCommand", "parameters": {
            "addOnCommandId": {"commandNamespace": "ClaudeTools", "commandName": command},
            "addOnCommandParameters": params or {}}}).encode()
        try:
            r = self._post(body)
        except (http.client.HTTPException, ConnectionError, OSError):
            # stale keep-alive socket or Archicad restarted: reconnect once
            self.close()
            try:
                r = self._post(body)
            except (ConnectionError, OSError) as e:
                self.close()
                raise RuntimeError(f"Archicad is not answering on {HOST}:{PORT} ({e}). Is Archicad open with the project?")
        if not r.get("succeeded", False):
            raise RuntimeError(f"Archicad error: {json.dumps(r.get('error', r), ensure_ascii=False)}")
        res = r.get("result", {}).get("addOnCommandResponse", r.get("result"))
        return res


AC = Archicad()
COMMANDS = read_commands()
app = Server("arquiteto-ndc")

PARAMS_SCHEMA = {
    "type": "object",
    "properties": {
        "params": {"type": "object", "description": "Parameters of the add-on command (see the tool description).",
                   "additionalProperties": True},
        "output_file": {"type": "string", "description": "Optional absolute path: write the full JSON result there and return only a summary."},
    },
    "additionalProperties": False,
}


def summarize(res: Any) -> str:
    if isinstance(res, dict):
        parts = []
        for k, v in res.items():
            parts.append(f"{k}: {len(v)} items" if isinstance(v, list) else f"{k}: {json.dumps(v, ensure_ascii=False)[:120]}")
        return "; ".join(parts)
    return json.dumps(res, ensure_ascii=False)[:500]


def deliver(res: Any, output_file: str | None, elapsed: float) -> list[types.TextContent]:
    if output_file:
        p = Path(output_file).expanduser()
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(json.dumps(res, ensure_ascii=False, indent=1))
        return [types.TextContent(type="text", text=f"Written to {p} ({p.stat().st_size} bytes, {elapsed:.1f}s). {summarize(res)}")]
    text = json.dumps(res, ensure_ascii=False, indent=1)
    if len(text) > 60000:
        text = text[:60000] + f"\n... [truncated {len(text) - 60000} chars: call again with output_file]"
    return [types.TextContent(type="text", text=text)]


@app.list_tools()
async def list_tools() -> list[types.Tool]:
    tools = []
    for name, doc in COMMANDS.items():
        ann = types.ToolAnnotations(
            readOnlyHint=name.startswith(READ_ONLY_PREFIXES),
            destructiveHint=name in DESTRUCTIVE,
            openWorldHint=False,
        )
        tools.append(types.Tool(name=name, description=f"{doc}\n\n{GENERAL}", inputSchema=PARAMS_SCHEMA, annotations=ann))
    tools.append(types.Tool(
        name="batch",
        description=("Run several ClaudeTools commands in order over one connection. Stops at the first error unless "
                     "continue_on_error is true. Returns one result per call. SaveProject/SaveProjectAs are not allowed here."),
        inputSchema={"type": "object", "properties": {
            "calls": {"type": "array", "items": {"type": "object", "properties": {
                "command": {"type": "string"}, "params": {"type": "object", "additionalProperties": True}},
                "required": ["command"]}},
            "continue_on_error": {"type": "boolean"},
            "output_file": {"type": "string"}},
            "required": ["calls"]},
        annotations=types.ToolAnnotations(destructiveHint=True, openWorldHint=False)))
    return tools


@app.call_tool()
async def call_tool(name: str, arguments: dict) -> list[types.TextContent]:
    arguments = arguments or {}
    t0 = time.time()
    if name == "batch":
        out = []
        for c in arguments.get("calls", []):
            cmd = c.get("command", "")
            if cmd not in COMMANDS or cmd in NEEDS_APPROVAL:
                out.append({"command": cmd, "error": "unknown or not allowed in batch"})
                if not arguments.get("continue_on_error"):
                    break
                continue
            try:
                out.append({"command": cmd, "result": AC.call(cmd, c.get("params"))})
            except Exception as e:  # noqa: BLE001
                out.append({"command": cmd, "error": str(e)})
                if not arguments.get("continue_on_error"):
                    break
        return deliver(out, arguments.get("output_file"), time.time() - t0)
    if name not in COMMANDS:
        raise ValueError(f"Unknown ClaudeTools command: {name}")
    params = dict(arguments.get("params") or {})
    if name in NEEDS_APPROVAL:
        if not params.pop("user_approved", False):
            raise ValueError(f"{name} needs the user's explicit approval: ask the user, then pass user_approved:true.")
    res = AC.call(name, params)
    return deliver(res, arguments.get("output_file"), time.time() - t0)


async def main() -> None:
    async with stdio_server() as (r, w):
        await app.run(r, w, app.create_initialization_options())


if __name__ == "__main__":
    if "--list" in sys.argv:
        for n, d in COMMANDS.items():
            print(f"{n}: {d[:110]}")
        sys.exit(0)
    if "--call" in sys.argv:  # quick test: server.py --call Ping '{}'
        i = sys.argv.index("--call")
        print(json.dumps(AC.call(sys.argv[i + 1], json.loads(sys.argv[i + 2]) if len(sys.argv) > i + 2 else {}), indent=1)[:2000])
        sys.exit(0)
    import asyncio
    asyncio.run(main())
