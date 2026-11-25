# Archicad Architect

**Let an AI agent (Claude Code, Claude Desktop or any MCP client) work inside Archicad 30 like an architect's assistant:** read and fix dimensions, draw, edit elements, place notes, check drawings, under the rules a careful architect would follow.

It is a companion to [Tapir](https://github.com/ENZYME-APD/tapir-archicad-automation): Tapir covers the broad Archicad JSON API; this project adds the commands Tapir and the official API do not have (dimension texts, witness lines, rebuilding dimensions, the real drawn lines of every element, 2D drafting, solid operations, trims, roof and polygon editing, drawings on layouts) and a set of hard-won lessons on using them safely.

> Built and tested on a real project (a house renovation) with Claude Code, Archicad 30 (macOS, Apple Silicon) and Tapir. Share-alike spirit: open an issue or a PR with what you learn.

## What is in the box

| Folder | What |
|---|---|
| `addon/` | **ClaudeTools** Archicad add-on (C++, DevKit 30), laid out like GRAPHISOFT's [archicad-addon-cmake](https://github.com/GRAPHISOFT/archicad-addon-cmake) template; build tools as a git submodule. 52 JSON commands in the namespace `ClaudeTools`, called through the Archicad JSON API (`API.ExecuteAddOnCommand`). |
| `mcp/` | **arquiteto-ndc** MCP server (Python, one file). One MCP tool per add-on command, read from the add-on source, plus `batch`. Keeps one connection to Archicad (~20 ms per call). |
| `setup.py` | One-command setup for each user: your MDID, DevKit download, build, install, MCP entry. |
| `client/ct.py` | Minimal command-line client: `python3 client/ct.py Ping '{}'`. |
| `scripts/` | Batch jobs built on top: move dimension texts off lines (`dimfix.py`), witness lines to a fixed paper length (`witfix.py`), switch elevation marks to a reference level (`elevfix.py`), write "±0.00 = altitude" next to the zero marks with clear space (`zeronote.py`). `export_data.py` writes the project data they need. |
| `docs/LESSONS.md` | What we learned the hard way (no Undo, dimension points that vanish, calibration of witness lines, sandbox, MCP versions...). **Read it before running anything in batch.** |
| `docs/CLAUDE.md.example` | Template of project instructions for Claude Code (working rules, drawing conventions, glossary pt-PT/en). |

## Quick start (students: start here)

You need: **Archicad 30**, **Python 3**, **git**, **CMake**, and **Xcode** (macOS) or **Visual Studio 2022 with "Desktop development with C++"** (Windows), plus **[uv](https://docs.astral.sh/uv/)** for the MCP server and a Claude app (Claude Desktop or Claude Code).

1. **Register your free Add-On ID** at [archicadapi.graphisoft.com](https://archicadapi.graphisoft.com) (Profile → Add-On ID). You get a *Developer ID* and a *Local ID*. Archicad only loads add-ons built with the ID of the person who registered it, so everyone uses their own.
2. **Get the code** (with the build tools submodule):
   ```bash
   git clone --recurse-submodules https://github.com/ncorticos/archicad-architect.git
   cd archicad-architect
   ```
3. **Close Archicad** and run the setup. It asks for your two IDs and your Archicad Add-Ons folder (*Extensões* in the Portuguese version), downloads the Archicad 30 API DevKit, builds and installs the add-on, and prints the MCP entry:
   ```bash
   python3 setup.py --write-mcp-config
   ```
   (Windows: `py setup.py --write-mcp-config`.) Your answers are kept in `local.json`, which is never shared. Run single steps with `--only tools|mdid|build|install|mcp`; use your own DevKit with `--devkit <folder> --devkit-build <n>`.
4. **Start Archicad**, open a project, then check: `python3 client/ct.py Ping '{}'` → it answers with the version and the 52 commands.
5. **Restart the Claude app.** The tools appear as `arquiteto-ndc`. In Claude Code, allow `mcp__arquiteto-ndc__*` and keep `SaveProject`, `SaveProjectAs` and `DeleteElements` on *ask* (`settings.json` → `permissions`).
6. Copy `docs/CLAUDE.md.example` into your project folder as `CLAUDE.md`, fill in your project's data, and start Claude in that folder.

**Do it by hand instead:** `python3 addon/Tools/BuildAddOn.py -c addon/config.json -v 30` (after writing `addon/RFIX/ClaudeToolsFix.grc` from the `.in` template), then copy `addon/Build/ClaudeTools/30/INT/RelWithDebInfo/ClaudeTools.bundle` (or `.apx`) into the Add-Ons folder. Claude Code CLI: `claude mcp add arquiteto-ndc -- uv run --script /path/to/archicad-architect/mcp/server.py`.

Environment variables of the server: `CLAUDETOOLS_PORT` (default 19723), `CLAUDETOOLS_HOST`, `CLAUDETOOLS_TIMEOUT`, `CLAUDETOOLS_SRC`.

## Calling a command

Every tool takes `{ "params": { ...command parameters... }, "output_file": "/optional/path.json" }`.
`output_file` writes large answers (GetDimensions, GetDrawnSegments) to disk and returns a summary.
In sections, elevations and layouts pass `useFloorPlanDatabase: false` and `db: {type, guid}` (from `ListDatabases`).

## Commands

| Command | Parameters / notes |
|---|---|
| `Ping` | {} -> list of available commands {} -> add-on version and command list. |
| `CreateSolidLinks` | { targets:[guid], operators:[guid], operation:"subtract\|subtractUp\|subtractDown\|intersect\|add", inheritOperatorAttributes:bool, skipPolygonHoles:bool } |
| `RemoveSolidLinks` | { targets:[guid], operators:[guid] }  (operators omitted = all operators of each target) |
| `GetSolidLinks` | { elements:[guid] } -> per element: operators (with operation) and targets |
| `TrimElements` | { elements:[guid], trimmingElement:guid (roof/shell, optional), trimType:"keepInside\|keepOutside\|keepAll" } Without trimmingElement, the roofs/shells must be in 'elements' (like the menu command). |
| `RemoveTrim` | { element1:guid, element2:guid } |
| `GetTrims` | { elements:[guid] } -> trimmedBy / trims |
| `FlipOpenings` | { openings:[guid], flipSide:bool (mirror across wall axis, i.e. swing in/out), flipHinge:bool (mirror along wall, i.e. left/right) } or explicit values { reflected:bool, oSide:bool } |
| `Rebuild` | { regenerate:bool }  Rebuild (or Rebuild & Regenerate) the front window |
| `SetWallTextureAlign` | { walls:[guid], value:bool }  Wall "align texture to wall edges" |
| `CreateTexts` | { items:[{x,y,text,layer,size(mm),pen,angle(rad),floor,just:"left"\|"center"\|"right",anchor:"LT"…"RB"}] } |
| `CreateDimensions` | { items:[{points:[{x,y}...], refX, refY, dirX, dirY, layer, floor}] }  static linear dimensions |
| `SetLayerInCombinations` | { layer:int, visibleIn:[combination names] }  -> layer visible in those, hidden in all others |
| `MoveToLayer` | { elements:[guid], layer:int } |
| `DeleteElements` | { elements:[guid] } → deleted, notFound, notDeleted (current window's database) |
| `FarElements` | { maxDistance:m } -> elements in the floor plan whose bounds go beyond maxDistance from origin |
| `GetDrawing` | { drawing:guid } -> placement data of a drawing on its layout (works in the layout database) |
| `SetDrawingFrame` | { drawing:guid, xMin,yMin,xMax,yMax (layout coords, m), anchor? } -> clip drawing with rectangular frame |
| `ListPens` | {} -> pens of the active pen table: index, r, g, b (0..1), width (mm) |
| `CreateHatches` | { items:[{points:[{x,y}...], layer, fill, fgPen, bgPen, contPen, contLineType, contour:bool, floor}] } |
| `SetTextBackground` | { elements:[guid], fillPen } -> opaque background on text elements |
| `ZoomToElements` | { elements:[guid] } zoom the front window to the elements |
| `SelectElements` | { elements:[guid] } select them (replacing the selection); empty = clear selection |
| `ShowIn3D` | { elements:[guid] } show only these in 3D; no elements = show all |
| `SetElementProps` | { elements:[guid], layer, pen, lineType, height, thickness, level, bottomOffset, offset, width, buildingMaterial, topMat, sideMat, botMat, refMat, oppMat, sidMat, objLoc, fill, fgPen, bgPen, textSize, fillPen, dimTextSize, dimBgPen } |
| `GetPolygon` | { element:guid } -> polygon coords (and z for meshes), arcs |
| `SetMeshPointHeights` | { mesh:guid, points:[{x,y,z}], tolerance } -> set z of mesh polygon vertices close to the given x,y |
| `TransformElements` | { elements:[guid], op:"move"\|"rotate"\|"mirror", dx,dy,dz \| cx,cy,angle(rad) \| ax,ay,bx,by, copy:bool } |
| `CreateLines` | { items:[{type:"line"\|"arc"\|"circle", x1,y1,x2,y2 \| cx,cy,r,a0,a1, layer, pen, lineType, floor}] } |
| `CreateObject` | { libPart:"name", x,y, angle, layer, floor, z, params:{name:value} } |
| `SetGDLParams` | { elements:[guid], params:{name:value} } GDL parameters of objects/doors/windows/lamps |
| `Get3DCamera` | {} -> 3D camera |
| `Set3DCamera` | { posX,posY,cameraZ, targetX,targetY,targetZ, viewCone } -> set perspective camera |
| `SetDrawingProps` | { drawing:guid, name, number, ratio, layoutDatabase } |
| `SaveProject` | {} -> save the project Saves the project. Only with the user's explicit approval: pass user_approved:true. |
| `GetElementInfo` | { elements:[guid] } -> header info: type, layer, floor, bounds |
| `SetDrawOrder` | { elements:[guid], order:"front"\|"back"\|"forward"\|"backward"\|"reset" } |
| `FindElements` | { type:"Wall"\|..., layer?, floor?, select:bool } -> guids (and selects them if select) |
| `CopyElementSettings` | { from:guid, to:[guid] } pick up the settings of one element and inject them into others of the same type (geometry kept) |
| `GetRoof` | { roof:guid } -> roof data (class, level, thickness, plane angle, levels, eaves, pivot edge angles) |
| `SetRoof` | { roof:guid, level, thickness, angle (plane), eavesOverhang, levels:[{height,angle}], edges:[{edge, angle, eavesOverhang}] } |
| `SetPolygon` | { element:guid, contours:[[{x,y,z?}...], ...] } replace the polygon of a slab, hatch or mesh (first contour = outline, others = holes) |
| `SaveProjectAs` | { path:"/abs/file.pln" } save a copy of the project as a new PLN (Save As) Saves a copy as a new PLN. Only with the user's explicit approval: pass user_approved:true. |
| `RenderImage` | { path:"/abs/image.png" } photo-render the current 3D view with the current scene into a file |
| `ListDatabases` | {} -> sections, elevations, interior elevations, details, worksheets and layouts with type/guid/title/ref. Pass the guid back as db:{type,guid}. |
| `GetDimensions` | Pass useFloorPlanDatabase:false; optional floor:int and db:{type,guid} (type: floorplan\|section\|elevation\|interiorelevation\|detail\|worksheet\|layout). Returns scale and, per point, base element, note position/text and witnessForm/witnessVal. |
| `GetDrawnSegments` | { useFloorPlanDatabase:false, db?, floor?, boxes:[{x0,y0,x1,y1}], margin } -> real drawn line segments of every element near the boxes. Mesh/terrain fills come back as many triangles. |
| `SetDimensionNotes` | { dim:guid, notes:[{i, x, y}], db? }  -> moves the text of those dimension elements (fixed position) Moves dimension texts (fixed position). Pass useFloorPlanDatabase:false. Points bound to wall Openings are lost by the change: rebuild such dimensions with RebuildDimension instead. |
| `SetDimensionWitness` | { db?, items:[{dim:guid, form?:int, val?:double, elems?:[{i, form?, val?}]}] } Sets witness-line form/length of each point (and the dimension default). One undo step per dimension. Stops at the first dimension whose number of points changes. Pass useFloorPlanDatabase:false. Witness form: 0 None, 1 Small, 2 Large (gap to element), 3 Fix (fixed length). For 8 mm on paper: form 3, val = 8*scale/1000 - 0.0935 (metres). Dimensions with points on section elements are refused (unchanged). |
| `RebuildDimension` | { db?, dim:guid, points:[{px,py,bx,by,nx?,ny?,fixPos?,keepBase?:int}] } Re-creates the dimension with all its settings (first point as template) and the given points, then deletes the old one. keepBase = index of an existing point whose element link is kept; otherwise static. Pass useFloorPlanDatabase:false. Needs >= 2 points. Save the dimension's data first (GetDimensions) so it can be rebuilt again. |
| `GetStoryVisibility` | ClaudeTools command. |
| `SetStoryVisibility` | ClaudeTools command. |

## Golden rules (also in `docs/LESSONS.md`)

- **There is no Undo** for changes made through the Archicad API: Edit › Undo stays greyed out. Test on one element, check, then run the batch. Save the data of what you change (e.g. `GetDimensions` to JSON) so you can rebuild it.
- **Never save the project without the user's approval.**
- **Hidden layers**: elements on them cannot be changed; show the layer, change, hide it again.
- **Dimension points bound to Openings or section elements** are dropped or refused when the dimension is changed: rebuild the dimension with static points (`RebuildDimension`).
- Look at the result: capture the window before and after (Tapir `SaveCurrentWindowAsImage`).

## Em português (para alunos)

Ferramentas para um agente de IA (Claude Code, Claude Desktop ou outro cliente MCP) trabalhar dentro do Archicad 30 como assistente de arquiteto: corrigir cotas, linhas de chamada, cotas altimétricas e de nível, desenhar, editar elementos e colocar notas sem sobrepor texto a linhas.

**Instalação:**
1. Registem o vosso *Add-On ID* gratuito em archicadapi.graphisoft.com (cada um usa o seu: o Archicad só carrega add-ons com o ID de quem o registou).
2. `git clone --recurse-submodules https://github.com/ncorticos/archicad-architect.git`
3. Com o Archicad **fechado**: `python3 setup.py --write-mcp-config`. O script pede os vossos IDs e a pasta *Extensões* do Archicad 30, descarrega o DevKit, compila, instala e regista o servidor MCP `arquiteto-ndc`. Os vossos dados ficam em `local.json`, que não é partilhado.
4. Abram o Archicad e reiniciem a app do Claude.
5. Copiem `docs/CLAUDE.md.example` para a pasta do vosso projeto como `CLAUDE.md` e preencham-no.

**Regras de ouro:** a API não tem *Desfazer*; testem sempre numa só peça antes de aplicar em lote; nunca gravem sem rever; leiam o `docs/LESSONS.md`.

## Credits

Nuno Dinis Cortiços (architect, FA-ULisboa), with Claude Code. Inspired by and used together with [Tapir](https://github.com/ENZYME-APD/tapir-archicad-automation). Archicad is a trademark of GRAPHISOFT SE; this project is independent from GRAPHISOFT. MIT License.
