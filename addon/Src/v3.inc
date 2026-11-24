// ---- v3: dimensions vs. drawing lines -----------------------------------------
// ListDatabases, GetDimensions, GetDrawnSegments, SetDimensionNotes
// All take an optional  db:{type:"floorplan"|"section"|"elevation"|"interiorelevation"|"detail"|"worksheet"|"layout", guid}
// (call them with useFloorPlanDatabase:false so the generic wrapper does not switch first).

namespace v3 {

struct DbSwitch {
	API_DatabaseInfo saved {};
	bool switched = false;
	~DbSwitch () { if (switched) ACAPI_Database_ChangeCurrentDatabase (&saved); }
};

API_DatabaseTypeID DbType (const GS::UniString& t)
{
	if (t == "section")           return APIWind_SectionID;
	if (t == "elevation")         return APIWind_ElevationID;
	if (t == "interiorelevation") return APIWind_InteriorElevationID;
	if (t == "detail")            return APIWind_DetailID;
	if (t == "worksheet")         return APIWind_WorksheetID;
	if (t == "layout")            return APIWind_LayoutID;
	return APIWind_FloorPlanID;
}

GSErrCode Switch (const GS::ObjectState& p, DbSwitch& s)
{
	GS::ObjectState db;
	GS::UniString type ("floorplan"), guid;
	if (p.Get ("db", db)) { db.Get ("type", type); db.Get ("guid", guid); }
	API_DatabaseInfo want {};
	want.typeID = DbType (type);
	if (want.typeID != APIWind_FloorPlanID)
		want.databaseUnId.elemSetId = APIGuidFromString (guid.ToCStr ().Get ());
	if (ACAPI_Database_GetCurrentDatabase (&s.saved) != NoError) return APIERR_GENERAL;
	GSErrCode e = ACAPI_Database_ChangeCurrentDatabase (&want);
	if (e == NoError) s.switched = true;
	return e;
}

// ---------------------------------------------------------------------------
GS::ObjectState ListDatabases (const GS::ObjectState&)
{
	GS::ObjectState r;
	auto list = r.AddList<GS::ObjectState> ("databases");
	API_DatabaseInfo saved {};
	ACAPI_Database_GetCurrentDatabase (&saved);
	struct { const char* n; API_DatabaseTypeID t; GSErrCode (*f)(API_DatabaseUnId**, GS::Array<API_DatabaseUnId>*); } kinds[] = {
		{ "section", APIWind_SectionID, ACAPI_Database_GetSectionDatabases },
		{ "elevation", APIWind_ElevationID, ACAPI_Database_GetElevationDatabases },
		{ "interiorelevation", APIWind_InteriorElevationID, ACAPI_Database_GetInteriorElevationDatabases },
		{ "detail", APIWind_DetailID, ACAPI_Database_GetDetailDatabases },
		{ "worksheet", APIWind_WorksheetID, ACAPI_Database_GetWorksheetDatabases },
		{ "layout", APIWind_LayoutID, ACAPI_Database_GetLayoutDatabases },
	};
	for (auto& k : kinds) {
		GS::Array<API_DatabaseUnId> ids;
		if (k.f (nullptr, &ids) != NoError) continue;
		for (const auto& id : ids) {
			API_DatabaseInfo di {};
			di.typeID = k.t; di.databaseUnId = id;
			GS::UniString title, name, ref;
			if (ACAPI_Database_ChangeCurrentDatabase (&di) == NoError) {
				API_DatabaseInfo cur {};
				if (ACAPI_Database_GetCurrentDatabase (&cur) == NoError) {
					title = GS::UniString (cur.title); name = GS::UniString (cur.name); ref = GS::UniString (cur.ref);
				}
			}
			list (GS::ObjectState ("type", GS::UniString (k.n), "guid", APIGuidToString (id.elemSetId),
				"title", title, "name", name, "ref", ref));
		}
	}
	ACAPI_Database_ChangeCurrentDatabase (&saved);
	return r;
}

// ---------------------------------------------------------------------------
GS::ObjectState GetDimensions (const GS::ObjectState& p)
{
	DbSwitch s;
	if (Switch (p, s) != NoError) return Err (APIERR_BADDATABASE, "cannot switch database");
	Int32 floor = -9999; p.Get ("floor", floor);
	GS::Array<API_Guid> all;
	ACAPI_Element_GetElemList (API_DimensionID, &all);
	GS::ObjectState r;
	{ double sc = 0; if (ACAPI_Drawing_GetDrawingScale (&sc) == NoError) r.Add ("scale", sc); }
	auto list = r.AddList<GS::ObjectState> ("dimensions");
	for (const auto& g : all) {
		API_Element el {}; el.header.guid = g;
		if (ACAPI_Element_Get (&el) != NoError) continue;
		if (floor != -9999 && el.header.floorInd != floor) continue;
		API_ElementMemo memo {};
		if (ACAPI_Element_GetMemo (g, &memo, APIMemoMask_All) != NoError || memo.dimElems == nullptr) { ACAPI_DisposeElemMemoHdls (&memo); continue; }
		Int32 n = (Int32) (BMGetHandleSize ((GSHandle) memo.dimElems) / sizeof (API_DimElem));
		GS::ObjectState d ("guid", APIGuidToString (g), "floor", (Int32) el.header.floorInd,
			"layer", (Int32) el.header.layer.ToInt32_Deprecated (),
			"textPos", (Int32) el.dimension.textPos, "textWay", (Int32) el.dimension.textWay,
			"refX", el.dimension.refC.x, "refY", el.dimension.refC.y,
			"dirX", el.dimension.direction.x, "dirY", el.dimension.direction.y);
		d.Add ("defWitnessForm", (Int32) el.dimension.defWitnessForm); d.Add ("defWitnessVal", el.dimension.defWitnessVal);
		auto es = d.AddList<GS::ObjectState> ("elems");
		for (Int32 i = 0; i < n; ++i) {
			const API_DimElem& de = (*memo.dimElems)[i];
			GS::UniString txt;
			if (de.note.contentUStr != nullptr) txt = *de.note.contentUStr;
			if (txt.IsEmpty ()) txt = GS::UniString (de.note.content);
			GS::UniString btn; ACAPI_Element_GetElemTypeName (de.base.base.type, btn);
			API_Elem_Head bh {}; bh.guid = de.base.base.guid;
			bool bex = (de.base.base.guid != APINULLGuid) && ACAPI_Element_GetHeader (&bh) == NoError;
			es (GS::ObjectState ("i", i, "px", de.pos.x, "py", de.pos.y, "baseType", btn, "baseGuid", APIGuidToString (de.base.base.guid), "baseExists", bex,
				"inIndex", (Int32) de.base.base.inIndex, "special", (Int32) de.base.base.special, "line", de.base.base.line,
				"bx", de.base.loc.x, "by", de.base.loc.y,
				"nx", de.note.pos.x, "ny", de.note.pos.y, "size", de.note.noteSize, "angle", de.note.noteAngle,
				"fixPos", de.note.fixPos, "dimVal", de.dimVal, "text", txt, "contentType", (Int32) de.note.contentType,
				"witnessForm", (Int32) de.witnessForm, "witnessVal", de.witnessVal));
		}
		list (d);
		ACAPI_DisposeElemMemoHdls (&memo);
	}
	return r;
}

// ---------------------------------------------------------------------------
struct Box { double x0, y0, x1, y1; };
static std::vector<std::array<double,4>>* gSegs = nullptr;

static void PushSeg (double x1, double y1, double x2, double y2)
{
	if (gSegs != nullptr) gSegs->push_back ({ x1, y1, x2, y2 });
}

static GSErrCode CollectPrims (const API_PrimElement* pe, const void* par1, const void* par2, const void*)
{
	switch (pe->header.typeID) {
		case API_PrimLineID:
			PushSeg (pe->line.c1.x, pe->line.c1.y, pe->line.c2.x, pe->line.c2.y);
			break;
		case API_PrimArcID: {
			const API_PrimArc& a = pe->arc;
			double b = a.whole ? 0.0 : a.begAng, e = a.whole ? 2.0 * 3.14159265358979 : a.endAng;
			if (e < b) e += 2.0 * 3.14159265358979;
			int n = std::max (4, (int) ((e - b) / 0.1));
			double ry = (a.ratio > 0 ? a.r / a.ratio : a.r);
			double ca = cos (a.angle), sa = sin (a.angle);
			double px = 0, py = 0;
			for (int k = 0; k <= n; ++k) {
				double t = b + (e - b) * k / n;
				double lx = a.r * cos (t), ly = ry * sin (t);
				double x = a.orig.x + lx * ca - ly * sa, y = a.orig.y + lx * sa + ly * ca;
				if (k > 0) PushSeg (px, py, x, y);
				px = x; py = y;
			}
			break;
		}
		case API_PrimPLineID: {
			const API_Coord* c = (const API_Coord*) par1;
			if (c == nullptr) break;
			for (Int32 k = 1; k < pe->pline.nCoords; ++k) PushSeg (c[k].x, c[k].y, c[k + 1].x, c[k + 1].y);
			break;
		}
		case API_PrimTriID:
			for (int k = 0; k < 3; ++k) PushSeg (pe->tri.c[k].x, pe->tri.c[k].y, pe->tri.c[(k + 1) % 3].x, pe->tri.c[(k + 1) % 3].y);
			break;
		case API_PrimPolyID: {
			const API_Coord* c = (const API_Coord*) par1;
			const Int32* ends = (const Int32*) par2;
			if (c == nullptr) break;
			Int32 start = 1;
			for (Int32 sp = 1; sp <= pe->poly.nSubPolys; ++sp) {
				Int32 end = (ends != nullptr) ? ends[sp] : pe->poly.nCoords;
				for (Int32 k = start; k < end; ++k) PushSeg (c[k].x, c[k].y, c[k + 1].x, c[k + 1].y);
				start = end + 1;
			}
			break;
		}
		default: break;
	}
	return NoError;
}

static bool SegHitsBox (const std::array<double,4>& s, const Box& b)
{
	double x0 = std::min (s[0], s[2]), x1 = std::max (s[0], s[2]);
	double y0 = std::min (s[1], s[3]), y1 = std::max (s[1], s[3]);
	if (x1 < b.x0 || x0 > b.x1 || y1 < b.y0 || y0 > b.y1) return false;
	// Liang-Barsky clip
	double dx = s[2] - s[0], dy = s[3] - s[1], t0 = 0, t1 = 1;
	double pp[4] = { -dx, dx, -dy, dy }, qq[4] = { s[0] - b.x0, b.x1 - s[0], s[1] - b.y0, b.y1 - s[1] };
	for (int k = 0; k < 4; ++k) {
		if (fabs (pp[k]) < 1e-12) { if (qq[k] < 0) return false; continue; }
		double t = qq[k] / pp[k];
		if (pp[k] < 0) { if (t > t1) return false; if (t > t0) t0 = t; }
		else { if (t < t0) return false; if (t < t1) t1 = t; }
	}
	return true;
}

GS::ObjectState GetDrawnSegments (const GS::ObjectState& p)
{
	DbSwitch s;
	if (Switch (p, s) != NoError) return Err (APIERR_BADDATABASE, "cannot switch database");
	Int32 floor = -9999; p.Get ("floor", floor);
	double margin = Num (p, "margin", 2.0);
	GS::Array<GS::ObjectState> bx; p.Get ("boxes", bx);
	std::vector<Box> boxes;
	for (const auto& o : bx) boxes.push_back ({ Num (o, "x0", 0), Num (o, "y0", 0), Num (o, "x1", 0), Num (o, "y1", 0) });

	API_DatabaseInfo cur {}; ACAPI_Database_GetCurrentDatabase (&cur);
	bool isPlan = cur.typeID == APIWind_FloorPlanID;
	short savedFloor = 0; bool floorChanged = false;
	if (isPlan && floor != -9999) {
		API_StoryInfo si {};
		if (ACAPI_ProjectSetting_GetStorySettings (&si) == NoError) {
			savedFloor = si.actStory;
			BMKillHandle ((GSHandle*) &si.data);
		}
		API_StoryCmdType cmd {}; cmd.action = APIStory_GoTo; cmd.index = (short) floor;
		floorChanged = (ACAPI_ProjectSetting_ChangeStorySettings (&cmd) == NoError);
	}

	GS::Array<API_Guid> all;
	ACAPI_Element_GetElemList (API_ZombieElemID, &all);
	GS::ObjectState r;
	auto list = r.AddList<GS::ObjectState> ("elements");
	std::vector<std::array<double,4>> segs;
	gSegs = &segs;
	for (const auto& g : all) {
		API_Elem_Head h {}; h.guid = g;
		if (ACAPI_Element_GetHeader (&h) != NoError) continue;
		if (isPlan && floor != -9999 && h.floorInd != floor) continue;
		API_Box3D b {};
		if (ACAPI_Element_CalcBounds (&h, &b) != NoError) continue;
		bool near = boxes.empty ();
		for (const auto& q : boxes)
			if (!(b.xMax + margin < q.x0 || b.xMin - margin > q.x1 || b.yMax + margin < q.y0 || b.yMin - margin > q.y1)) { near = true; break; }
		if (!near) continue;
		segs.clear ();
		if (ACAPI_DrawingPrimitive_ShapePrims (h, CollectPrims) != NoError) continue;
		GS::Array<double> flat;
		for (const auto& sg : segs) {
			bool keep = boxes.empty ();
			for (const auto& q : boxes) if (SegHitsBox (sg, q)) { keep = true; break; }
			if (keep) { flat.Push (sg[0]); flat.Push (sg[1]); flat.Push (sg[2]); flat.Push (sg[3]); }
		}
		if (flat.IsEmpty ()) continue;
		GS::UniString tn; ACAPI_Element_GetElemTypeName (h.type, tn);
		list (GS::ObjectState ("guid", APIGuidToString (g), "type", tn, "layer", (Int32) h.layer.ToInt32_Deprecated (),
			"floor", (Int32) h.floorInd, "segs", flat));
	}
	gSegs = nullptr;
	if (floorChanged) {
		API_StoryCmdType cmd {}; cmd.action = APIStory_GoTo; cmd.index = savedFloor;
		ACAPI_ProjectSetting_ChangeStorySettings (&cmd);
	}
	return r;
}

// ---------------------------------------------------------------------------
// { dim:guid, notes:[{i, x, y}], db? }  -> moves the text of those dimension elements (fixed position)
GS::ObjectState SetDimensionNotes (const GS::ObjectState& p)
{
	DbSwitch s;
	if (Switch (p, s) != NoError) return Err (APIERR_BADDATABASE, "cannot switch database");
	GS::Array<API_Guid> g = GuidList (p, "dim");
	if (g.IsEmpty ()) return Err (APIERR_BADPARS, "dim required");
	GS::Array<GS::ObjectState> notes; p.Get ("notes", notes);
	GSErrCode inner = NoError; Int32 moved = 0; Int32 fixedBasesOut = 0;
	Int32 before = 0, after = 0;
	ACAPI_CallUndoableCommand ("Claude: Dimension text", [&] () -> GSErrCode {
		API_Element el {}; el.header.guid = g[0];
		if ((inner = ACAPI_Element_Get (&el)) != NoError) return inner;
		API_ElementMemo memo {};
		if ((inner = ACAPI_Element_GetMemo (g[0], &memo)) != NoError) return inner;
		if (memo.dimElems == nullptr) { ACAPI_DisposeElemMemoHdls (&memo); inner = APIERR_BADPARS; return inner; }
		Int32 n = el.dimension.nDimElem; before = n;
		bool staticMissing = false; p.Get ("staticizeMissing", staticMissing);
		bool staticAll = false; p.Get ("staticizeAll", staticAll);
		GS::Array<GS::UniString> staticTypes; p.Get ("staticizeTypes", staticTypes);   // localized type names, e.g. "Abertura"
		GS::Array<Int32> staticIdx; p.Get ("staticizeIndices", staticIdx);
		Int32& fixedBases = fixedBasesOut;
		if (staticMissing || staticAll || !staticTypes.IsEmpty () || !staticIdx.IsEmpty ()) {
			for (Int32 i = 0; i < n; ++i) {
				API_Base& b = (*memo.dimElems)[i].base.base;
				if (b.guid == APINULLGuid) continue;   // already static
				API_Elem_Head bh {}; bh.guid = b.guid;
				bool ex = ACAPI_Element_GetHeader (&bh) == NoError;
				GS::UniString tn; ACAPI_Element_GetElemTypeName (b.type, tn);
				bool fix = staticAll || (staticMissing && !ex) || staticTypes.Contains (tn) || staticIdx.Contains (i);
				if (fix) { b.type = API_ZombieElemID; b.guid = APINULLGuid; b.line = false; b.special = 0; b.inIndex = 0; ++fixedBases; }
			}
		}
		for (const auto& o : notes) {
			Int32 i = (Int32) Num (o, "i", -1);
			if (i < 0 || i >= n) continue;
			API_NoteType& nt = (*memo.dimElems)[i].note;
			nt.pos.x = Num (o, "x", nt.pos.x); nt.pos.y = Num (o, "y", nt.pos.y);
			nt.fixPos = true; ++moved;
		}
		API_Element mask; ACAPI_ELEMENT_MASK_CLEAR (mask);
		ACAPI_ELEMENT_MASK_SET (mask, API_DimensionType, textPos);   // same value: forces the change, as in the DevKit example
		inner = ACAPI_Element_Change (&el, &mask, &memo, 0, true);
		ACAPI_DisposeElemMemoHdls (&memo);
		API_Element chk {}; chk.header.guid = g[0];
		if (ACAPI_Element_Get (&chk) == NoError) after = chk.dimension.nDimElem;
		return inner;
	});
	if (inner == NoError && after != before) return GS::ObjectState ("error", GS::ObjectState ("code", (Int32) -1, "message", GS::UniString ("dimension points changed: undo needed")), "before", before, "after", after);
	if (inner != NoError) return Err (inner, "Change failed");
	return GS::ObjectState ("moved", moved, "points", after, "staticized", fixedBasesOut);
}

// ---------------------------------------------------------------------------
// { db?, items:[{dim:guid, form?:int, val?:double, elems?:[{i, form?, val?}]}] }
// Sets witness-line form/length of each point (and the dimension default). One undo step per dimension.
// Stops at the first dimension whose number of points changes.
GS::ObjectState SetDimensionWitness (const GS::ObjectState& p)
{
	DbSwitch s;
	if (Switch (p, s) != NoError) return Err (APIERR_BADDATABASE, "cannot switch database");
	GS::Array<GS::ObjectState> items; p.Get ("items", items);
	GS::ObjectState r;
	auto list = r.AddList<GS::ObjectState> ("results");
	for (const auto& it : items) {
		GS::UniString gs; it.Get ("dim", gs);
		API_Guid g = APIGuidFromString (gs.ToCStr ().Get ());
		Int32 form = -1; it.Get ("form", form);
		double val = Num (it, "val", -1.0);
		GS::Array<GS::ObjectState> per; it.Get ("elems", per);
		GSErrCode inner = NoError; Int32 before = 0, after = 0, changed = 0;
		ACAPI_CallUndoableCommand ("Claude: Witness lines", [&] () -> GSErrCode {
			API_Element el {}; el.header.guid = g;
			if ((inner = ACAPI_Element_Get (&el)) != NoError) return inner;
			API_ElementMemo memo {};
			if ((inner = ACAPI_Element_GetMemo (g, &memo)) != NoError) return inner;
			if (memo.dimElems == nullptr) { ACAPI_DisposeElemMemoHdls (&memo); inner = APIERR_BADPARS; return inner; }
			Int32 n = el.dimension.nDimElem; before = n;
			API_Element mask; ACAPI_ELEMENT_MASK_CLEAR (mask);
			ACAPI_ELEMENT_MASK_SET (mask, API_DimensionType, textPos);
			if (form >= 0) { el.dimension.defWitnessForm = (API_WitnessID) form; ACAPI_ELEMENT_MASK_SET (mask, API_DimensionType, defWitnessForm); }
			if (val >= 0)  { el.dimension.defWitnessVal = val; ACAPI_ELEMENT_MASK_SET (mask, API_DimensionType, defWitnessVal); }
			for (Int32 i = 0; i < n; ++i) {
				API_DimElem& de = (*memo.dimElems)[i];
				Int32 f = form; double v = val;
				for (const auto& o : per) if ((Int32) Num (o, "i", -1) == i) { o.Get ("form", f); v = Num (o, "val", v); }
				if (f >= 0) de.witnessForm = (API_WitnessID) f;
				if (v >= 0) de.witnessVal = v;
				if (f >= 0 || v >= 0) ++changed;
			}
			inner = ACAPI_Element_Change (&el, &mask, &memo, 0, true);
			ACAPI_DisposeElemMemoHdls (&memo);
			API_Element chk {}; chk.header.guid = g;
			if (ACAPI_Element_Get (&chk) == NoError) after = chk.dimension.nDimElem;
			return inner;
		});
		GS::ObjectState one ("dim", gs, "before", before, "after", after, "changed", changed, "err", (Int32) inner);
		list (one);
		if (inner == NoError && after != before) { r.Add ("error", GS::ObjectState ("code", (Int32) -1, "message", GS::UniString ("dimension points changed: undo needed"), "dim", gs)); break; }
	}
	return r;
}

// ---------------------------------------------------------------------------
// { db?, dim:guid, points:[{px,py,bx,by,nx?,ny?,fixPos?,keepBase?:int}] }
// Re-creates the dimension with all its settings (first point as template) and the given points,
// then deletes the old one. keepBase = index of an existing point whose element link is kept; otherwise static.
GS::ObjectState RebuildDimension (const GS::ObjectState& p)
{
	DbSwitch s;
	if (Switch (p, s) != NoError) return Err (APIERR_BADDATABASE, "cannot switch database");
	GS::Array<API_Guid> g = GuidList (p, "dim");
	if (g.IsEmpty ()) return Err (APIERR_BADPARS, "dim required");
	GS::Array<GS::ObjectState> pts; p.Get ("points", pts);
	if (pts.GetSize () < 2) return Err (APIERR_BADPARS, "points < 2");
	GSErrCode inner = NoError; API_Guid newG = APINULLGuid; Int32 after = 0;
	ACAPI_CallUndoableCommand ("Claude: Rebuild dimension", [&] () -> GSErrCode {
		API_Element el {}; el.header.guid = g[0];
		if ((inner = ACAPI_Element_Get (&el)) != NoError) return inner;
		API_ElementMemo old {};
		if ((inner = ACAPI_Element_GetMemo (g[0], &old)) != NoError) return inner;
		if (old.dimElems == nullptr) { ACAPI_DisposeElemMemoHdls (&old); inner = APIERR_BADPARS; return inner; }
		Int32 nOld = el.dimension.nDimElem;
		Int32 n = (Int32) pts.GetSize ();
		API_ElementMemo memo {};
		memo.dimElems = reinterpret_cast<API_DimElem**> (BMhAllClear (n * sizeof (API_DimElem)));
		for (Int32 i = 0; i < n; ++i) {
			API_DimElem& d = (*memo.dimElems)[i];
			d = (*old.dimElems)[0];
			d.note.contentUStr = nullptr;
			const GS::ObjectState& o = pts[i];
			d.pos.x = Num (o, "px", 0); d.pos.y = Num (o, "py", 0);
			d.base.loc.x = Num (o, "bx", d.pos.x); d.base.loc.y = Num (o, "by", d.pos.y);
			Int32 kb = -1; o.Get ("keepBase", kb);
			if (kb >= 0 && kb < nOld) d.base.base = (*old.dimElems)[kb].base.base;
			else { d.base.base = API_Base {}; d.base.base.type = API_ZombieElemID; d.base.base.guid = APINULLGuid; }
			d.note.pos.x = Num (o, "nx", d.pos.x); d.note.pos.y = Num (o, "ny", d.pos.y);
			bool fp = false; o.Get ("fixPos", fp); d.note.fixPos = fp;
			d.dimVal = 0;
		}
		ACAPI_DisposeElemMemoHdls (&old);
		el.header.guid = APINULLGuid;
		el.dimension.nDimElem = n;
		inner = ACAPI_Element_Create (&el, &memo);
		ACAPI_DisposeElemMemoHdls (&memo);
		if (inner != NoError) return inner;
		newG = el.header.guid;
		API_Element chk {}; chk.header.guid = newG;
		if (ACAPI_Element_Get (&chk) == NoError) after = chk.dimension.nDimElem;
		if (after != n) return inner;   // keep the old one
		GS::Array<API_Guid> del; del.Push (g[0]);
		inner = ACAPI_Element_Delete (del);
		return inner;
	});
	if (inner != NoError) return Err (inner, "Rebuild failed");
	return GS::ObjectState ("newGuid", APIGuidToString (newG), "points", after, "oldDeleted", after == (Int32) pts.GetSize ());
}

// ---------------------------------------------------------------------------
// Story visibility ("Show on stories") of slabs, meshes, morphs, objects, lamps, walls, columns, beams.
// GetStoryVisibility { elements:[guid] } -> per element the current settings
// SetStoryVisibility { elements:[guid], showOnHome?, showAllAbove?, showAllBelow?, showRelAbove?:int, showRelBelow?:int,
//                      viewDepth?:"entire"|"range"|"absolute" (morphs) }  -> "stored" = what Archicad kept
//   Only the given fields change; automatic ("all relevant stories") visibility is switched off.
static GS::ObjectState VisToOS (const API_StoryVisibility& v)
{
	return GS::ObjectState ("showOnHome", v.showOnHome, "showAllAbove", v.showAllAbove, "showAllBelow", v.showAllBelow,
		"showRelAbove", (Int32) v.showRelAbove, "showRelBelow", (Int32) v.showRelBelow);
}

static void ApplyVis (API_StoryVisibility& v, const GS::ObjectState& p)
{
	bool b; Int32 n;
	if (p.Get ("showOnHome", b))   v.showOnHome = b;
	if (p.Get ("showAllAbove", b)) v.showAllAbove = b;
	if (p.Get ("showAllBelow", b)) v.showAllBelow = b;
	if (p.Get ("showRelAbove", n)) v.showRelAbove = (short) n;
	if (p.Get ("showRelBelow", n)) v.showRelBelow = (short) n;
}

// ACAPI_ELEMENT_MASK_SET marks only the first byte of a field: for struct fields
// (API_StoryVisibility) every byte must be marked, or Archicad ignores the sub-fields.
#define CT_MASK_FULL(rec,typ,varnam) memset (&(((typ *) &rec)->varnam), 0xFF, sizeof (((typ *) &rec)->varnam))

static GS::ObjectState StoryVisibility (const GS::ObjectState& p, bool write)
{
	GS::Array<API_Guid> gs = GuidList (p, "elements");
	GS::ObjectState r;
	auto list = r.AddList<GS::ObjectState> ("elements");
	for (const auto& g : gs) {
		API_Element el {}; el.header.guid = g;
		GSErrCode e = ACAPI_Element_Get (&el);
		GS::UniString tn; ACAPI_Element_GetElemTypeName (el.header.type, tn);
		GS::ObjectState one ("guid", APIGuidToString (g), "type", tn);
		if (e != NoError) { one.Add ("error", (Int32) e); list (one); continue; }
		API_Element mask; ACAPI_ELEMENT_MASK_CLEAR (mask);
		bool supported = true;
		switch (el.header.type.typeID) {
			case API_SlabID:
				if (write) { ApplyVis (el.slab.visibilityCont, p); ApplyVis (el.slab.visibilityFill, p); el.slab.isAutoOnStoryVisibility = false;
					CT_MASK_FULL (mask, API_SlabType, visibilityCont); CT_MASK_FULL (mask, API_SlabType, visibilityFill); ACAPI_ELEMENT_MASK_SET (mask, API_SlabType, isAutoOnStoryVisibility); }
				one.Add ("contour", VisToOS (el.slab.visibilityCont)); one.Add ("fill", VisToOS (el.slab.visibilityFill)); break;
			case API_MeshID:
				if (write) { ApplyVis (el.mesh.visibilityCont, p); ApplyVis (el.mesh.visibilityFill, p);
					CT_MASK_FULL (mask, API_MeshType, visibilityCont); CT_MASK_FULL (mask, API_MeshType, visibilityFill); }
				one.Add ("contour", VisToOS (el.mesh.visibilityCont)); one.Add ("fill", VisToOS (el.mesh.visibilityFill)); break;
			case API_MorphID:
				if (write) { ApplyVis (el.morph.showContour, p); ApplyVis (el.morph.showFill, p); el.morph.isAutoOnStoryVisibility = false;
					CT_MASK_FULL (mask, API_MorphType, showContour); CT_MASK_FULL (mask, API_MorphType, showFill); ACAPI_ELEMENT_MASK_SET (mask, API_MorphType, isAutoOnStoryVisibility); }
				if (write) { GS::UniString vd; if (p.Get ("viewDepth", vd)) {
					el.morph.viewDepthLimitation = vd == "entire" ? API_EntireElement : (vd == "absolute" ? API_ToAbsoluteLimit : API_ToFloorPlanRange);
					CT_MASK_FULL (mask, API_MorphType, viewDepthLimitation); } }
				one.Add ("contour", VisToOS (el.morph.showContour)); one.Add ("fill", VisToOS (el.morph.showFill));
				one.Add ("viewDepth", (Int32) el.morph.viewDepthLimitation); break;
			case API_ObjectID:
			case API_LampID:
				if (write) { ApplyVis (el.object.visibility, p); el.object.isAutoOnStoryVisibility = false;
					CT_MASK_FULL (mask, API_ObjectType, visibility); ACAPI_ELEMENT_MASK_SET (mask, API_ObjectType, isAutoOnStoryVisibility); }
				one.Add ("visibility", VisToOS (el.object.visibility)); break;
			case API_WallID:
				if (write) { ApplyVis (el.wall.visibility, p); el.wall.isAutoOnStoryVisibility = false;
					CT_MASK_FULL (mask, API_WallType, visibility); ACAPI_ELEMENT_MASK_SET (mask, API_WallType, isAutoOnStoryVisibility); }
				one.Add ("visibility", VisToOS (el.wall.visibility)); break;
			case API_ColumnID:
				if (write) { ApplyVis (el.column.visibility, p); el.column.isAutoOnStoryVisibility = false;
					CT_MASK_FULL (mask, API_ColumnType, visibility); ACAPI_ELEMENT_MASK_SET (mask, API_ColumnType, isAutoOnStoryVisibility); }
				one.Add ("visibility", VisToOS (el.column.visibility)); break;
			case API_BeamID:
				if (write) { ApplyVis (el.beam.visibility, p); el.beam.isAutoOnStoryVisibility = false;
					CT_MASK_FULL (mask, API_BeamType, visibility); ACAPI_ELEMENT_MASK_SET (mask, API_BeamType, isAutoOnStoryVisibility); }
				one.Add ("visibility", VisToOS (el.beam.visibility)); break;
			default: supported = false; one.Add ("error", GS::UniString ("type not supported")); break;
		}
		if (write && supported) {
			GSErrCode ce = NoError;
			ACAPI_CallUndoableCommand ("Claude: Story visibility", [&] () -> GSErrCode { ce = ACAPI_Element_Change (&el, &mask, nullptr, 0, true); return ce; });
			one.Add ("changed", ce == NoError);
			if (ce != NoError) one.Add ("error", (Int32) ce);
			API_Element chk {}; chk.header.guid = g;
			if (ACAPI_Element_Get (&chk) == NoError) {
				const API_StoryVisibility* v = nullptr;
				switch (chk.header.type.typeID) {
					case API_SlabID: v = &chk.slab.visibilityCont; break;
					case API_MeshID: v = &chk.mesh.visibilityCont; break;
					case API_MorphID: v = &chk.morph.showContour; break;
					case API_ObjectID: case API_LampID: v = &chk.object.visibility; break;
					case API_WallID: v = &chk.wall.visibility; break;
					case API_ColumnID: v = &chk.column.visibility; break;
					case API_BeamID: v = &chk.beam.visibility; break;
					default: break;
				}
				if (v != nullptr) one.Add ("stored", VisToOS (*v));
			}
		}
		list (one);
	}
	return r;
}

GS::ObjectState GetStoryVisibility (const GS::ObjectState& p) { return StoryVisibility (p, false); }
GS::ObjectState SetStoryVisibility (const GS::ObjectState& p) { return StoryVisibility (p, true); }

} // namespace v3
