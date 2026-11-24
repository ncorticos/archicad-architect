# Lessons learned

Notes from using an AI agent on a real Archicad 30 project (macOS) through the JSON API, Tapir and the ClaudeTools add-on. Each one cost at least one failed attempt.

## Safety

- **No Undo.** `ACAPI_CallUndoableCommand` does *not* reach Edit › Undo when the call comes through the JSON API in Archicad 30: the menu stays greyed out. Never tell the user to press ⌘Z/Ctrl+Z to revert an API change. Before changing an element, save its data (e.g. `GetDimensions` with `output_file`) so it can be rebuilt; test on one element; then batch.
- **Restarting Archicad** (needed after every add-on build) also loses the Undo history of the user's own work. Ask the user to save or undo first.
- **Never save without approval.** The MCP server refuses `SaveProject`/`SaveProjectAs` unless `user_approved: true`; keep them on `ask` in the client's permissions too.
- **Hidden layers**: their elements cannot be changed (the call fails or does nothing). Show the layer (Tapir `CreateLayers` with `overwriteExisting: true`), change, hide it again, in a `finally` block.
- **The user may be working at the same time.** "Invalid program status (ongoing user input)" (4001) means a tool or dialog is active in Archicad: stop and ask the user to press Esc; don't batch while they draw.

## Dimensions

- **Points bound to wall Openings** (the Opening tool, type "Abertura"/"Opening") are dropped by `ACAPI_Element_Change` whenever the dimension's memo is changed, even if you first turn those bases into static points. Result: a 4-point chain becomes 2 points. `SetDimensionNotes` and `SetDimensionWitness` check the point count and report it, but the change is already made. Use `RebuildDimension`: it creates a new dimension with the same settings and static points, and deletes the old one only if the new one has all its points.
- **Points bound to section elements** ("Corte Elem"/"SectElem") make `ACAPI_Element_Change` fail with APIERR_GENERAL (the dimension stays unchanged). Same fix: `RebuildDimension`.
- **Witness lines**: `witnessForm` 2 ("Large") is a *gap* from the element (so a dimension far from the wall draws a witness line all the way to it); 3 ("Fix") is a *fixed length*. The length is in model metres and is measured past a fixed overshoot of about 0.0935 m: for *L* mm on paper at scale 1:*s*, `val = L·s/1000 − 0.0935`. Check with `GetDrawnSegments`. Only shorten lines longer than the target; where the element is closer than the target, leave it.
- **Dimension text position**: `note.pos` is the text's reference point; `fixPos: true` keeps it. To find a free spot, get the real drawn lines (`GetDrawnSegments`, filtered by boxes around the text) and test the text rectangle against them, with a clear margin.
- **Elevation dimensions in sections** (appearance *Elevation*): changing their origin (Tapir `ModifyAssociativeDimensions` → `elevationOrigin`) does not recompute the shown value. Moving them by 1 mm and back (Tapir `MoveElements`) forces the recomputation. Rebuild of the view does not.
- **Reference level** (Project Preferences): check what the finished floor really is before choosing it. Sum areas per top level, not counts: in our case 105 tiny pieces at +0.10 hid a 291 m² finished floor at 0.00.
- **"Terrain level"** is ambiguous (absolute altitude vs. relative to ±0.00): ask. Check the Archicad project altitude against the survey (thresholds, spot heights): ours was about 1 m off.

## Texts and notes

- `CreateTexts` (ClaudeTools) places the text by its **centre** and in the tool's default style (it can be bold). Fix with Tapir `ModifyTexts`: `style: {bold:false, anchor:"LeftBottom", height}` and `coordinate`.
- Keep at least one text height between stacked texts and ~0.6 text heights from any line. Architects notice cramped text immediately.
- Terrain/path **meshes** come back from `GetDrawnSegments` as hundreds of fill triangles that are not visible lines; vegetation (2D trees) *is* visible. Don't skip whole layer classes blindly: filter by element and look at a capture.

## Views and windows

- Elements of sections/elevations live in their own database: pass `useFloorPlanDatabase: false` and `db: {type, guid}` (`ListDatabases`). Most other ClaudeTools commands switch to the floor plan first.
- `ChangeWindow` per view is slow (seconds). Prefer commands that take `db` and work without opening the window; batch per view.
- A view may come back empty through the API (its layer combination hides everything): check before trusting "no collisions".
- Captures: Tapir `SaveCurrentWindowAsImage` (macOS `screencapture` needs Screen Recording permission). Frame first with `SetZoom`.

## Tooling

- **MCP server instead of shell calls**: one persistent connection (~20 ms per call) instead of one Python process per call; typed tools; `output_file` for big answers; no sandbox exceptions.
- **Python `mcp` package 2.x** removed the `Server.list_tools()/call_tool()` decorators used by many low-level servers: pin `mcp>=1.10,<2` or port to the new API.
- **Claude Desktop / Claude Code desktop** load MCP servers from `claude_desktop_config.json`, not from `mcpServers` in `~/.claude/settings.json`.
- **Claude Code sandbox**: calls to `localhost:19723` from sandboxed shell commands fail with "Operation not permitted". The MCP server runs outside the sandbox; for scripts add the command to `sandbox.excludedCommands` (new session needed).
- **Add-on MDID**: Archicad only loads add-ons with the MDID of the registered developer. Each user builds with their own.
- **Building**: plan add-on changes in bundles: every build needs Archicad closed and restarted.

## Prompts that worked

- "I selected the dimension" (pointing with the selection in Archicad) beats any description.
- State scope (views, floors), units (paper mm vs model m), what not to touch and what "done" means.
- Keep a `CLAUDE.md` with the project's facts and a glossary of your language ↔ Archicad terms (see `CLAUDE.md.example`).
