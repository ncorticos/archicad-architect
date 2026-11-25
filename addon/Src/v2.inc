// ===================== ClaudeTools 2.0 commands =====================

static void PutPoint (GS::ObjectState& o, const API_Coord& c) { o.Add ("x", c.x); o.Add ("y", c.y); }

// { elements:[guid] } zoom the front window to the elements
GS::ObjectState ZoomToElements (const GS::ObjectState& p)
{
	GS::Array<API_Guid> g = GuidList (p, "elements");
	GSErrCode e = g.IsEmpty () ? ACAPI_View_ZoomToSelected () : ACAPI_View_ZoomToElements (&g);
	if (e != NoError) return Err (e, "Zoom failed");
	return GS::ObjectState ("success", true);
}

// { elements:[guid] } select them (replacing the selection); empty = clear selection
GS::ObjectState SelectElements (const GS::ObjectState& p)
{
	GS::Array<API_Neig> n;
	for (const auto& g : GuidList (p, "elements")) n.Push (API_Neig (g));
	ACAPI_Selection_DeselectAll ();
	GSErrCode e = n.IsEmpty () ? NoError : ACAPI_Selection_Select (n, true);
	if (e != NoError) return Err (e, "Select failed");
	return GS::ObjectState ("selected", (Int32) n.GetSize ());
}

// { elements:[guid] } show only these in 3D; no elements = show all
GS::ObjectState ShowIn3D (const GS::ObjectState& p)
{
	GS::Array<API_Neig> n;
	for (const auto& g : GuidList (p, "elements")) n.Push (API_Neig (g));
	GSErrCode e;
	if (n.IsEmpty ()) e = ACAPI_View_ShowAllIn3D ();
	else { ACAPI_Selection_DeselectAll (); ACAPI_Selection_Select (n, true); e = ACAPI_View_ShowSelectionIn3D (); }
	if (e != NoError) return Err (e, "Show in 3D failed");
	return GS::ObjectState ("success", true);
}

static bool OptAttr (const GS::ObjectState& p, const char* f, API_OverriddenAttribute& a)
{
	if (!p.Contains (f)) return false;
	Int32 v = (Int32) Num (p, f, 0);
	if (v <= 0) a = APINullValue; else a = ACAPI_CreateAttributeIndex (v);
	return true;
}

// { elements:[guid], layer, pen, lineType, height, thickness, level, bottomOffset, offset, width,
//   buildingMaterial, topMat, sideMat, botMat, refMat, oppMat, sidMat, objLoc,
//   fill, fgPen, bgPen, textSize, fillPen, dimTextSize, dimBgPen }
GS::ObjectState SetElementProps (const GS::ObjectState& p)
{
	GS::ObjectState result;
	auto list = result.AddList<GS::ObjectState> ("elements");
	ACAPI_CallUndoableCommand ("Claude: Element properties", [&] () -> GSErrCode {
		for (const auto& g : GuidList (p, "elements")) {
			API_Element el {}; el.header.guid = g;
			if (ACAPI_Element_Get (&el) != NoError) { list (GS::ObjectState ("guid", APIGuidToString (g), "code", (Int32) -1)); continue; }
			API_Element mask; ACAPI_ELEMENT_MASK_CLEAR (mask);
			API_ElementMemo memo {}; UInt64 memoMask = 0;
			const API_ElemTypeID t = el.header.type.typeID;
			if (p.Contains ("layer")) { el.header.layer = ACAPI_CreateAttributeIndex ((Int32) Num (p, "layer", 1)); ACAPI_ELEMENT_MASK_SET (mask, API_Elem_Head, layer); }
			auto BM = [&] (API_AttributeIndex& f) { if (p.Contains ("buildingMaterial")) { f = ACAPI_CreateAttributeIndex ((Int32) Num (p, "buildingMaterial", 1)); return true; } return false; };
			switch (t) {
				case API_WallID:
					if (p.Contains ("height")) { el.wall.height = Num (p, "height", 0); ACAPI_ELEMENT_MASK_SET (mask, API_WallType, height); }
					if (p.Contains ("thickness")) { el.wall.thickness = el.wall.thickness1 = Num (p, "thickness", 0); ACAPI_ELEMENT_MASK_SET (mask, API_WallType, thickness); ACAPI_ELEMENT_MASK_SET (mask, API_WallType, thickness1); }
					if (p.Contains ("bottomOffset")) { el.wall.bottomOffset = Num (p, "bottomOffset", 0); ACAPI_ELEMENT_MASK_SET (mask, API_WallType, bottomOffset); }
					if (BM (el.wall.buildingMaterial)) ACAPI_ELEMENT_MASK_SET (mask, API_WallType, buildingMaterial);
					if (OptAttr (p, "refMat", el.wall.refMat)) ACAPI_ELEMENT_MASK_SET (mask, API_WallType, refMat);
					if (OptAttr (p, "oppMat", el.wall.oppMat)) ACAPI_ELEMENT_MASK_SET (mask, API_WallType, oppMat);
					if (OptAttr (p, "sidMat", el.wall.sidMat)) ACAPI_ELEMENT_MASK_SET (mask, API_WallType, sidMat);
					break;
				case API_SlabID:
					if (p.Contains ("thickness")) { el.slab.thickness = Num (p, "thickness", 0); ACAPI_ELEMENT_MASK_SET (mask, API_SlabType, thickness); }
					if (p.Contains ("level")) { el.slab.level = Num (p, "level", 0); ACAPI_ELEMENT_MASK_SET (mask, API_SlabType, level); }
					if (BM (el.slab.buildingMaterial)) ACAPI_ELEMENT_MASK_SET (mask, API_SlabType, buildingMaterial);
					if (OptAttr (p, "topMat", el.slab.topMat)) ACAPI_ELEMENT_MASK_SET (mask, API_SlabType, topMat);
					if (OptAttr (p, "sideMat", el.slab.sideMat)) ACAPI_ELEMENT_MASK_SET (mask, API_SlabType, sideMat);
					if (OptAttr (p, "botMat", el.slab.botMat)) ACAPI_ELEMENT_MASK_SET (mask, API_SlabType, botMat);
					break;
				case API_MeshID:
					if (p.Contains ("level")) { el.mesh.level = Num (p, "level", 0); ACAPI_ELEMENT_MASK_SET (mask, API_MeshType, level); }
					if (BM (el.mesh.buildingMaterial)) ACAPI_ELEMENT_MASK_SET (mask, API_MeshType, buildingMaterial);
					if (OptAttr (p, "topMat", el.mesh.topMat)) ACAPI_ELEMENT_MASK_SET (mask, API_MeshType, topMat);
					if (OptAttr (p, "sideMat", el.mesh.sideMat)) ACAPI_ELEMENT_MASK_SET (mask, API_MeshType, sideMat);
					if (OptAttr (p, "botMat", el.mesh.botMat)) ACAPI_ELEMENT_MASK_SET (mask, API_MeshType, botMat);
					break;
				case API_RoofID:
					if (p.Contains ("level")) { el.roof.shellBase.level = Num (p, "level", 0); ACAPI_ELEMENT_MASK_SET (mask, API_RoofType, shellBase.level); }
					if (p.Contains ("thickness")) { el.roof.shellBase.thickness = Num (p, "thickness", 0); ACAPI_ELEMENT_MASK_SET (mask, API_RoofType, shellBase.thickness); }
					if (BM (el.roof.shellBase.buildingMaterial)) ACAPI_ELEMENT_MASK_SET (mask, API_RoofType, shellBase.buildingMaterial);
					if (OptAttr (p, "topMat", el.roof.shellBase.topMat)) ACAPI_ELEMENT_MASK_SET (mask, API_RoofType, shellBase.topMat);
					if (OptAttr (p, "sideMat", el.roof.shellBase.sidMat)) ACAPI_ELEMENT_MASK_SET (mask, API_RoofType, shellBase.sidMat);
					if (OptAttr (p, "botMat", el.roof.shellBase.botMat)) ACAPI_ELEMENT_MASK_SET (mask, API_RoofType, shellBase.botMat);
					break;
				case API_ColumnID:
					if (p.Contains ("height")) { el.column.height = Num (p, "height", 0); ACAPI_ELEMENT_MASK_SET (mask, API_ColumnType, height); }
					if (p.Contains ("bottomOffset")) { el.column.bottomOffset = Num (p, "bottomOffset", 0); ACAPI_ELEMENT_MASK_SET (mask, API_ColumnType, bottomOffset); }
					break;
				case API_BeamID:
					if (p.Contains ("level")) { el.beam.level = Num (p, "level", 0); ACAPI_ELEMENT_MASK_SET (mask, API_BeamType, level); }
					if (p.Contains ("offset")) { el.beam.offset = Num (p, "offset", 0); ACAPI_ELEMENT_MASK_SET (mask, API_BeamType, offset); }
					break;
				case API_DoorID: case API_WindowID: {
					API_OpeningBaseType& ob = (t == API_DoorID) ? el.door.openingBase : el.window.openingBase;
					if (p.Contains ("width")) { ob.width = Num (p, "width", 0); if (t == API_DoorID) ACAPI_ELEMENT_MASK_SET (mask, API_DoorType, openingBase.width); else ACAPI_ELEMENT_MASK_SET (mask, API_WindowType, openingBase.width); }
					if (p.Contains ("height")) { ob.height = Num (p, "height", 0); if (t == API_DoorID) ACAPI_ELEMENT_MASK_SET (mask, API_DoorType, openingBase.height); else ACAPI_ELEMENT_MASK_SET (mask, API_WindowType, openingBase.height); }
					if (p.Contains ("objLoc")) { if (t == API_DoorID) { el.door.objLoc = Num (p, "objLoc", 0); ACAPI_ELEMENT_MASK_SET (mask, API_DoorType, objLoc); } else { el.window.objLoc = Num (p, "objLoc", 0); ACAPI_ELEMENT_MASK_SET (mask, API_WindowType, objLoc); } }
					break; }
				case API_LineID:
					if (p.Contains ("pen")) { el.line.linePen.penIndex = (short) Num (p, "pen", 1); ACAPI_ELEMENT_MASK_SET (mask, API_LineType, linePen); }
					if (p.Contains ("lineType")) { el.line.ltypeInd = ACAPI_CreateAttributeIndex ((Int32) Num (p, "lineType", 1)); ACAPI_ELEMENT_MASK_SET (mask, API_LineType, ltypeInd); }
					break;
				case API_PolyLineID:
					if (p.Contains ("pen")) { el.polyLine.linePen.penIndex = (short) Num (p, "pen", 1); ACAPI_ELEMENT_MASK_SET (mask, API_PolyLineType, linePen); }
					if (p.Contains ("lineType")) { el.polyLine.ltypeInd = ACAPI_CreateAttributeIndex ((Int32) Num (p, "lineType", 1)); ACAPI_ELEMENT_MASK_SET (mask, API_PolyLineType, ltypeInd); }
					break;
				case API_HatchID:
					if (p.Contains ("fill")) { el.hatch.fillInd = ACAPI_CreateAttributeIndex ((Int32) Num (p, "fill", 1)); ACAPI_ELEMENT_MASK_SET (mask, API_HatchType, fillInd); }
					if (p.Contains ("fgPen")) { el.hatch.fillPen.penIndex = (short) Num (p, "fgPen", 1); ACAPI_ELEMENT_MASK_SET (mask, API_HatchType, fillPen); }
					if (p.Contains ("bgPen")) { el.hatch.fillBGPen = (short) Num (p, "bgPen", 0); ACAPI_ELEMENT_MASK_SET (mask, API_HatchType, fillBGPen); }
					if (p.Contains ("pen")) { el.hatch.contPen.penIndex = (short) Num (p, "pen", 1); ACAPI_ELEMENT_MASK_SET (mask, API_HatchType, contPen); }
					break;
				case API_TextID:
					if (p.Contains ("textSize")) { el.text.size = Num (p, "textSize", 2); ACAPI_ELEMENT_MASK_SET (mask, API_TextType, size); }
					if (p.Contains ("pen")) { el.text.pen = (short) Num (p, "pen", 1); ACAPI_ELEMENT_MASK_SET (mask, API_TextType, pen); }
					if (p.Contains ("fillPen")) { short fp = (short) Num (p, "fillPen", 0); el.text.usedFill = fp > 0; el.text.fillPen = fp; ACAPI_ELEMENT_MASK_SET (mask, API_TextType, usedFill); ACAPI_ELEMENT_MASK_SET (mask, API_TextType, fillPen); }
					break;
				case API_DimensionID:
					if (p.Contains ("dimTextSize") || p.Contains ("dimBgPen")) {
						if (ACAPI_Element_GetMemo (g, &memo, APIMemoMask_All) == NoError && memo.dimElems != nullptr) {
							Int32 n = (Int32) (BMGetHandleSize ((GSHandle) memo.dimElems) / sizeof (API_DimElem));
							for (Int32 i = 0; i < n; ++i) {
								API_NoteType& nt = (*memo.dimElems)[i].note;
								if (p.Contains ("dimTextSize")) nt.noteSize = Num (p, "dimTextSize", 2);
								if (p.Contains ("dimBgPen")) { short bp = (short) Num (p, "dimBgPen", 0); nt.opaque = bp > 0; nt.backgroundPen = bp; }
							}
							memoMask = APIMemoMask_All;
						}
						if (p.Contains ("dimTextSize")) { el.dimension.defNote.noteSize = Num (p, "dimTextSize", 2); ACAPI_ELEMENT_MASK_SET (mask, API_DimensionType, defNote); }
					}
					break;
				default: break;
			}
			GSErrCode e = ACAPI_Element_Change (&el, &mask, memoMask ? &memo : nullptr, memoMask, true);
			ACAPI_DisposeElemMemoHdls (&memo);
			// beam / column segments: width, height, building material
			Int32 segOk = 0;
			if ((t == API_BeamID || t == API_ColumnID) && (p.Contains ("width") || p.Contains ("height") || p.Contains ("buildingMaterial"))) {
				API_ElementMemo sm {};
				if (ACAPI_Element_GetMemo (g, &sm, t == API_BeamID ? APIMemoMask_BeamSegment : APIMemoMask_ColumnSegment) == NoError) {
					GS::Array<API_Guid> segs;
					if (t == API_BeamID && sm.beamSegments != nullptr) for (UIndex i = 0; i < BMpGetSize ((GSPtr) sm.beamSegments) / sizeof (API_BeamSegmentType); ++i) segs.Push (sm.beamSegments[i].head.guid);
					if (t == API_ColumnID && sm.columnSegments != nullptr) for (UIndex i = 0; i < BMpGetSize ((GSPtr) sm.columnSegments) / sizeof (API_ColumnSegmentType); ++i) segs.Push (sm.columnSegments[i].head.guid);
					for (const auto& sg : segs) {
						API_Element se {}; se.header.guid = sg;
						if (ACAPI_Element_Get (&se) != NoError) continue;
						API_Element sk; ACAPI_ELEMENT_MASK_CLEAR (sk);
						API_AssemblySegmentData& a = (t == API_BeamID) ? se.beamSegment.assemblySegmentData : se.columnSegment.assemblySegmentData;
						if (p.Contains ("width")) { a.nominalWidth = Num (p, "width", 0); }
						if (p.Contains ("height")) { a.nominalHeight = Num (p, "height", 0); }
						if (p.Contains ("buildingMaterial")) a.buildingMaterial = ACAPI_CreateAttributeIndex ((Int32) Num (p, "buildingMaterial", 1));
						if (t == API_BeamID) ACAPI_ELEMENT_MASK_SET (sk, API_BeamSegmentType, assemblySegmentData); else ACAPI_ELEMENT_MASK_SET (sk, API_ColumnSegmentType, assemblySegmentData);
						if (ACAPI_Element_Change (&se, &sk, nullptr, 0, true) == NoError) ++segOk;
					}
				}
				ACAPI_DisposeElemMemoHdls (&sm);
			}
			list (GS::ObjectState ("guid", APIGuidToString (g), "code", (Int32) e, "segments", segOk));
		}
		return NoError;
	});
	return result;
}

// { element:guid } -> polygon coords (and z for meshes), arcs
GS::ObjectState GetPolygon (const GS::ObjectState& p)
{
	GS::Array<API_Guid> g = GuidList (p, "element");
	if (g.IsEmpty ()) return Err (APIERR_BADPARS, "element required");
	API_ElementMemo memo {};
	GSErrCode e = ACAPI_Element_GetMemo (g[0], &memo, APIMemoMask_Polygon | APIMemoMask_MeshPolyZ);
	if (e != NoError || memo.coords == nullptr) { ACAPI_DisposeElemMemoHdls (&memo); return Err (e, "No polygon"); }
	GS::ObjectState r;
	Int32 n = (Int32) (BMGetHandleSize ((GSHandle) memo.coords) / sizeof (API_Coord)) - 1;
	auto pts = r.AddList<GS::ObjectState> ("points");
	for (Int32 i = 1; i <= n; ++i) {
		GS::ObjectState o; PutPoint (o, (*memo.coords)[i]);
		if (memo.meshPolyZ != nullptr) o.Add ("z", (*memo.meshPolyZ)[i]);
		pts (o);
	}
	if (memo.pends != nullptr) {
		GS::Array<Int32> pe; Int32 m = (Int32) (BMGetHandleSize ((GSHandle) memo.pends) / sizeof (Int32));
		for (Int32 i = 0; i < m; ++i) pe.Push ((*memo.pends)[i]);
		r.Add ("pends", pe);
	}
	if (memo.parcs != nullptr) {
		auto arcs = r.AddList<GS::ObjectState> ("arcs"); Int32 m = (Int32) (BMGetHandleSize ((GSHandle) memo.parcs) / sizeof (API_PolyArc));
		for (Int32 i = 0; i < m; ++i) arcs (GS::ObjectState ("begIndex", (*memo.parcs)[i].begIndex, "endIndex", (*memo.parcs)[i].endIndex, "arcAngle", (*memo.parcs)[i].arcAngle));
	}
	ACAPI_DisposeElemMemoHdls (&memo);
	return r;
}

// { mesh:guid, points:[{x,y,z}], tolerance } -> set z of mesh polygon vertices close to the given x,y
GS::ObjectState SetMeshPointHeights (const GS::ObjectState& p)
{
	GS::Array<API_Guid> g = GuidList (p, "mesh");
	if (g.IsEmpty ()) return Err (APIERR_BADPARS, "mesh required");
	GS::Array<GS::ObjectState> pts; p.Get ("points", pts);
	double tol = Num (p, "tolerance", 0.02);
	Int32 changed = 0; GSErrCode inner = NoError;
	ACAPI_CallUndoableCommand ("Claude: Mesh heights", [&] () -> GSErrCode {
		API_Element el {}; el.header.guid = g[0];
		if ((inner = ACAPI_Element_Get (&el)) != NoError) return inner;
		API_ElementMemo memo {};
		if ((inner = ACAPI_Element_GetMemo (g[0], &memo, APIMemoMask_All)) != NoError) return inner;
		Int32 n = (Int32) (BMGetHandleSize ((GSHandle) memo.coords) / sizeof (API_Coord)) - 1;
		for (Int32 i = 1; i <= n; ++i)
			for (const auto& q : pts)
				if (fabs ((*memo.coords)[i].x - Num (q, "x", 0)) < tol && fabs ((*memo.coords)[i].y - Num (q, "y", 0)) < tol) { (*memo.meshPolyZ)[i] = Num (q, "z", 0); ++changed; }
		API_Element mask; ACAPI_ELEMENT_MASK_CLEAR (mask);
		ACAPI_ELEMENT_MASK_SET (mask, API_MeshType, poly);
		inner = ACAPI_Element_Change (&el, &mask, &memo, APIMemoMask_Polygon | APIMemoMask_MeshPolyZ | APIMemoMask_MeshLevel, true);
		ACAPI_DisposeElemMemoHdls (&memo);
		return inner;
	});
	if (inner != NoError) return Err (inner, "Mesh change failed");
	return GS::ObjectState ("changed", changed);
}

// { elements:[guid], op:"move"|"rotate"|"mirror", dx,dy,dz | cx,cy,angle(rad) | ax,ay,bx,by, copy:bool }
GS::ObjectState TransformElements (const GS::ObjectState& p)
{
	GS::Array<API_Neig> n;
	for (const auto& g : GuidList (p, "elements")) n.Push (API_Neig (g));
	GS::UniString op ("move"); p.Get ("op", op);
	bool copy = false; p.Get ("copy", copy);
	API_EditPars ep {};
	ep.withDelete = !copy;
	if (op == "move") { ep.typeID = APIEdit_Drag; ep.begC = { 0, 0, 0 }; ep.endC = { Num (p, "dx", 0), Num (p, "dy", 0), Num (p, "dz", 0) }; }
	else if (op == "rotate") { double cx = Num (p, "cx", 0), cy = Num (p, "cy", 0), a = Num (p, "angle", 0);
		ep.typeID = APIEdit_Rotate; ep.origC = { cx, cy }; ep.begC = { cx + 1, cy, 0 }; ep.endC = { cx + cos (a), cy + sin (a), 0 }; }
	else if (op == "mirror") { ep.typeID = APIEdit_Mirror; ep.begC = { Num (p, "ax", 0), Num (p, "ay", 0), 0 }; ep.endC = { Num (p, "bx", 0), Num (p, "by", 1), 0 }; }
	else if (op == "stretch") { ep.typeID = APIEdit_Stretch; ep.begC = { 0, 0, 0 }; ep.endC = { Num (p, "dx", 0), Num (p, "dy", 0), 0 }; }
	else return Err (APIERR_BADPARS, "op must be move|rotate|mirror|stretch");
	// stretch: { op:"stretch", elements, marquee:{x0,y0,x1,y1}, dx, dy, multiStory?:true }
	// Nodes inside the marquee move by (dx,dy), like Archicad's Stretch with a marquee.
	bool marqueeSet = false;
	if (op == "stretch") {
		GS::ObjectState mq; if (!p.Get ("marquee", mq)) return Err (APIERR_BADPARS, "stretch needs marquee:{x0,y0,x1,y1}");
		double x0 = Num (mq, "x0", 0), y0 = Num (mq, "y0", 0), x1 = Num (mq, "x1", 0), y1 = Num (mq, "y1", 0);
		bool multi = true; p.Get ("multiStory", multi);
		API_SelectionInfo si {};
		si.typeID = API_MarqueePoly; si.multiStory = multi;
		si.marquee.nCoords = 5;
		si.marquee.coords = (API_Coord**) BMAllocateHandle (5 * sizeof (API_Coord), ALLOCATE_CLEAR, 0);
		API_Coord* c = *si.marquee.coords;
		c[0] = { x0, y0 }; c[1] = { x1, y0 }; c[2] = { x1, y1 }; c[3] = { x0, y1 }; c[4] = c[0];
		GSErrCode me = ACAPI_Selection_SetMarquee (&si);
		BMKillHandle ((GSHandle*) &si.marquee.coords);
		if (me != NoError) return Err (me, "SetMarquee failed");
		marqueeSet = true;
	}
	GSErrCode inner = NoError;
	ACAPI_CallUndoableCommand ("Claude: Transform", [&] () -> GSErrCode { inner = ACAPI_Element_Edit (&n, ep); return inner; });
	if (marqueeSet) { API_SelectionInfo none {}; none.typeID = API_SelEmpty; ACAPI_Selection_SetMarquee (&none); }
	if (inner != NoError) return Err (inner, "Edit failed");
	GS::Array<GS::UniString> out; for (const auto& x : n) out.Push (APIGuidToString (x.guid));
	return GS::ObjectState ("elements", out);
}


// { items:[{type:"line"|"arc"|"circle", x1,y1,x2,y2 | cx,cy,r,a0,a1, layer, pen, lineType, floor}] }
GS::ObjectState CreateLines (const GS::ObjectState& p)
{
	GS::Array<GS::ObjectState> items; p.Get ("items", items);
	GS::ObjectState result; auto list = result.AddList<GS::UniString> ("created"); auto fails = result.AddList<Int32> ("errors");
	ACAPI_CallUndoableCommand ("Claude: Create lines", [&] () -> GSErrCode {
		for (const auto& it : items) {
			GS::UniString ty ("line"); it.Get ("type", ty);
			API_Element el {};
			el.header.type = (ty == "line") ? API_LineID : (ty == "circle" ? API_CircleID : API_ArcID);
			if (ACAPI_Element_GetDefaults (&el, nullptr) != NoError) { fails ((Int32) -1); continue; }
			el.header.floorInd = (short) Num (it, "floor", 0);
			el.header.layer = ACAPI_CreateAttributeIndex ((Int32) Num (it, "layer", 1));
			short pen = (short) Num (it, "pen", 1); Int32 lt = (Int32) Num (it, "lineType", 1);
			if (ty == "line") { el.line.begC = { Num (it, "x1", 0), Num (it, "y1", 0) }; el.line.endC = { Num (it, "x2", 0), Num (it, "y2", 0) }; el.line.linePen.penIndex = pen; el.line.ltypeInd = ACAPI_CreateAttributeIndex (lt); }
			else if (ty == "circle") { el.circle.origC = { Num (it, "cx", 0), Num (it, "cy", 0) }; el.circle.r = Num (it, "r", 1); el.circle.ratio = 1; el.circle.linePen.penIndex = pen; el.circle.ltypeInd = ACAPI_CreateAttributeIndex (lt); }
			else { el.arc.origC = { Num (it, "cx", 0), Num (it, "cy", 0) }; el.arc.r = Num (it, "r", 1); el.arc.ratio = 1; el.arc.begAng = Num (it, "a0", 0); el.arc.endAng = Num (it, "a1", 1); el.arc.linePen.penIndex = pen; el.arc.ltypeInd = ACAPI_CreateAttributeIndex (lt); }
			GSErrCode e = ACAPI_Element_Create (&el, nullptr);
			if (e == NoError) list (APIGuidToString (el.header.guid)); else fails ((Int32) e);
		}
		return NoError;
	});
	return result;
}

static void SetParams (API_AddParType** pars, const GS::ObjectState& params, Int32& set)
{
	if (pars == nullptr) return;
	Int32 n = (Int32) (BMGetHandleSize ((GSHandle) pars) / sizeof (API_AddParType));
	for (Int32 i = 0; i < n; ++i) {
		API_AddParType& a = (*pars)[i];
		const char* nm = a.name;
		if (!params.Contains (nm)) continue;
		if (a.typeID == APIParT_CString) { GS::UniString s; params.Get (nm, s); GS::ucsncpy (a.value.uStr, s.ToUStr ().Get (), API_UAddParStrLen - 1); ++set; }
		else if (a.typeID == APIParT_Boolean) { bool b = false; if (!params.Get (nm, b)) b = Num (params, nm, 0) != 0; a.value.real = b ? 1 : 0; ++set; }
		else if (a.dim1 == 0 && a.dim2 == 0) { a.value.real = Num (params, nm, a.value.real); ++set; }
	}
}

// { libPart:"name", x,y, angle, layer, floor, z, params:{name:value} }
GS::ObjectState CreateObject (const GS::ObjectState& p)
{
	GS::UniString name; p.Get ("libPart", name);
	API_LibPart lp {};
	GS::ucsncpy (lp.docu_UName, name.ToUStr ().Get (), API_UniLongNameLen - 1);
	GSErrCode e = ACAPI_LibraryPart_Search (&lp, false, true);
	delete lp.location;
	if (e != NoError) return Err (e, "Library part not found: " + name);
	double a = 0, b = 0; Int32 nPar = 0; API_AddParType** pars = nullptr;
	if ((e = ACAPI_LibraryPart_GetParams (lp.index, &a, &b, &nPar, &pars)) != NoError) return Err (e, "GetParams failed");
	GS::ObjectState params; p.Get ("params", params);
	Int32 set = 0; SetParams (pars, params, set);
	API_Element el {}; API_ElementMemo memo {};
	el.header.type = API_ObjectID;
	ACAPI_Element_GetDefaults (&el, &memo);
	ACAPI_DisposeElemMemoHdls (&memo); memo = {};
	el.header.floorInd = (short) Num (p, "floor", 0);
	if (p.Contains ("layer")) el.header.layer = ACAPI_CreateAttributeIndex ((Int32) Num (p, "layer", 1));
	el.object.libInd = lp.index; el.object.pos = { Num (p, "x", 0), Num (p, "y", 0) };
	el.object.angle = Num (p, "angle", 0); el.object.level = Num (p, "z", 0);
	el.object.xRatio = a; el.object.yRatio = b;
	memo.params = pars;
	GSErrCode inner = NoError;
	ACAPI_CallUndoableCommand ("Claude: Create object", [&] () -> GSErrCode { inner = ACAPI_Element_Create (&el, &memo); return inner; });
	ACAPI_DisposeElemMemoHdls (&memo);
	if (inner != NoError) return Err (inner, "Create object failed");
	return GS::ObjectState ("guid", APIGuidToString (el.header.guid), "paramsSet", set);
}

// { elements:[guid], params:{name:value} } GDL parameters of objects/doors/windows/lamps
GS::ObjectState SetGDLParams (const GS::ObjectState& p)
{
	GS::ObjectState params; p.Get ("params", params);
	GS::ObjectState result; auto list = result.AddList<GS::ObjectState> ("elements");
	ACAPI_CallUndoableCommand ("Claude: GDL parameters", [&] () -> GSErrCode {
		for (const auto& g : GuidList (p, "elements")) {
			API_Element el {}; el.header.guid = g;
			if (ACAPI_Element_Get (&el) != NoError) continue;
			API_ElementMemo memo {};
			if (ACAPI_Element_GetMemo (g, &memo, APIMemoMask_AddPars) != NoError) continue;
			Int32 set = 0; SetParams (memo.params, params, set);
			API_Element mask; ACAPI_ELEMENT_MASK_CLEAR (mask);
			GSErrCode e = ACAPI_Element_Change (&el, &mask, &memo, APIMemoMask_AddPars, true);
			ACAPI_DisposeElemMemoHdls (&memo);
			list (GS::ObjectState ("guid", APIGuidToString (g), "set", set, "code", (Int32) e));
		}
		return NoError;
	});
	return result;
}

// {} -> 3D camera
GS::ObjectState Get3DCamera (const GS::ObjectState&)
{
	API_3DProjectionInfo pi {};
	GSErrCode e = ACAPI_View_Get3DProjectionSets (&pi);
	if (e != NoError) return Err (e, "Get projection failed");
	GS::ObjectState r ("isPersp", pi.isPersp, "azimuth", pi.u.persp.azimuth, "viewCone", pi.u.persp.viewCone, "distance", pi.u.persp.distance,
		"cameraZ", pi.u.persp.cameraZ, "targetZ", pi.u.persp.targetZ);
	r.Add ("posX", pi.u.persp.pos.x); r.Add ("posY", pi.u.persp.pos.y); r.Add ("targetX", pi.u.persp.target.x); r.Add ("targetY", pi.u.persp.target.y);
	return r;
}

// { posX,posY,cameraZ, targetX,targetY,targetZ, viewCone } -> set perspective camera
GS::ObjectState Set3DCamera (const GS::ObjectState& p)
{
	API_3DProjectionInfo pi {};
	GSErrCode e = ACAPI_View_Get3DProjectionSets (&pi);
	if (e != NoError) return Err (e, "Get projection failed");
	pi.isPersp = true;
	pi.u.persp.pos.x = Num (p, "posX", pi.u.persp.pos.x); pi.u.persp.pos.y = Num (p, "posY", pi.u.persp.pos.y);
	pi.u.persp.target.x = Num (p, "targetX", pi.u.persp.target.x); pi.u.persp.target.y = Num (p, "targetY", pi.u.persp.target.y);
	pi.u.persp.cameraZ = Num (p, "cameraZ", pi.u.persp.cameraZ); pi.u.persp.targetZ = Num (p, "targetZ", pi.u.persp.targetZ);
	pi.u.persp.viewCone = Num (p, "viewCone", pi.u.persp.viewCone);
	double dx = pi.u.persp.target.x - pi.u.persp.pos.x, dy = pi.u.persp.target.y - pi.u.persp.pos.y;
	pi.u.persp.azimuth = atan2 (dy, dx) * 180.0 / PI; pi.u.persp.distance = sqrt (dx * dx + dy * dy);
	e = ACAPI_View_Change3DProjectionSets (&pi);
	if (e != NoError) return Err (e, "Set projection failed");
	return GS::ObjectState ("success", true);
}

// { drawing:guid, name, number, ratio, layoutDatabase }
GS::ObjectState SetDrawingProps (const GS::ObjectState& p)
{
	GS::Array<API_Guid> g = GuidList (p, "drawing");
	if (g.IsEmpty ()) return Err (APIERR_BADPARS, "drawing required");
	GSErrCode inner = NoError;
	ACAPI_CallUndoableCommand ("Claude: Drawing properties", [&] () -> GSErrCode {
		API_Element el {}; el.header.guid = g[0];
		if ((inner = ACAPI_Element_Get (&el)) != NoError) return inner;
		API_Element mask; ACAPI_ELEMENT_MASK_CLEAR (mask);
		GS::UniString s;
		if (p.Get ("name", s)) { CHTruncate (s.ToCStr (CC_UTF8).Get (), el.drawing.name, sizeof (el.drawing.name)); el.drawing.nameType = APIName_CustomName; ACAPI_ELEMENT_MASK_SET (mask, API_DrawingType, name); ACAPI_ELEMENT_MASK_SET (mask, API_DrawingType, nameType); }
		if (p.Get ("number", s)) { CHTruncate (s.ToCStr (CC_UTF8).Get (), el.drawing.customNumber, sizeof (el.drawing.customNumber)); el.drawing.numberingType = APINumbering_CustomNum; ACAPI_ELEMENT_MASK_SET (mask, API_DrawingType, customNumber); ACAPI_ELEMENT_MASK_SET (mask, API_DrawingType, numberingType); }
		if (p.Contains ("ratio")) { el.drawing.ratio = Num (p, "ratio", 1); ACAPI_ELEMENT_MASK_SET (mask, API_DrawingType, ratio); }
		inner = ACAPI_Element_Change (&el, &mask, nullptr, 0, true);
		return inner;
	});
	if (inner != NoError) return Err (inner, "Drawing change failed");
	return GS::ObjectState ("success", true);
}

// {} -> save the project
GS::ObjectState SaveProject (const GS::ObjectState&)
{
	GSErrCode e = ACAPI_ProjectOperation_Save ();
	if (e != NoError) return Err (e, "Save failed");
	return GS::ObjectState ("success", true);
}

// { elements:[guid] } -> header info: type, layer, floor, bounds
GS::ObjectState GetElementInfo (const GS::ObjectState& p)
{
	GS::ObjectState r; auto list = r.AddList<GS::ObjectState> ("elements");
	for (const auto& g : GuidList (p, "elements")) {
		API_Elem_Head h {}; h.guid = g;
		if (ACAPI_Element_GetHeader (&h) != NoError) continue;
		GS::UniString tn; ACAPI_Element_GetElemTypeName (h.type, tn);
		API_Box3D b {}; ACAPI_Element_CalcBounds (&h, &b);
		list (GS::ObjectState ("guid", APIGuidToString (g), "type", tn, "layer", (Int32) h.layer.ToInt32_Deprecated (), "floor", (Int32) h.floorInd,
			"xMin", b.xMin, "yMin", b.yMin, "xMax", b.xMax, "yMax", b.yMax));
	}
	return r;
}

// ===================== ClaudeTools 2.1 =====================

// { elements:[guid], order:"back"|"front"|int }
GS::ObjectState SetDrawOrder (const GS::ObjectState& p)
{
	// order: front | back | forward | backward | reset  (Edit > Display Order); acts in the current window's database
	GS::UniString o ("back"); p.Get ("order", o);
	API_ToolCmdID cmd = APITool_SendToBack;
	if (o == "front") cmd = APITool_BringToFront;
	else if (o == "forward") cmd = APITool_BringForward;
	else if (o == "backward") cmd = APITool_SendBackward;
	else if (o == "reset") cmd = APITool_ResetOrder;
	GS::Array<API_Guid> g = GuidList (p, "elements");
	Int32 found = 0;
	for (const auto& x : g) { API_Elem_Head h {}; h.guid = x; if (ACAPI_Element_GetHeader (&h) == NoError) ++found; }
	GSErrCode err = NoError;
	ACAPI_CallUndoableCommand ("Claude: Display order", [&] () -> GSErrCode {
		err = ACAPI_Grouping_Tool (g, cmd, nullptr);
		return err;
	});
	return GS::ObjectState ("changed", (err == NoError) ? found : 0, "failed", (Int32) g.GetSize () - ((err == NoError) ? found : 0), "lastError", (Int32) err);
}

static bool TypeFromName (const GS::UniString& n, API_ElemTypeID& t)
{
	struct { const char* n; API_ElemTypeID t; } m[] = {
		{"Wall",API_WallID},{"Column",API_ColumnID},{"Beam",API_BeamID},{"Window",API_WindowID},{"Door",API_DoorID},{"Object",API_ObjectID},
		{"Lamp",API_LampID},{"Slab",API_SlabID},{"Roof",API_RoofID},{"Mesh",API_MeshID},{"Dimension",API_DimensionID},{"LevelDimension",API_LevelDimensionID},
		{"Text",API_TextID},{"Label",API_LabelID},{"Zone",API_ZoneID},{"Hatch",API_HatchID},{"Line",API_LineID},{"PolyLine",API_PolyLineID},
		{"Arc",API_ArcID},{"Circle",API_CircleID},{"Spline",API_SplineID},{"Hotspot",API_HotspotID},{"Shell",API_ShellID},{"Morph",API_MorphID},
		{"Skylight",API_SkylightID},{"Opening",API_OpeningID},{"Drawing",API_DrawingID},{"Section",API_CutPlaneID},{"Stair",API_StairID},{"Railing",API_RailingID} };
	for (auto& x : m) if (n == x.n) { t = x.t; return true; }
	return false;
}

// { type:"Wall"|..., layer?, floor?, select:bool } -> guids (and selects them if select)
GS::ObjectState FindElements (const GS::ObjectState& p)
{
	GS::UniString tn; p.Get ("type", tn);
	API_ElemTypeID t;
	if (!TypeFromName (tn, t)) return Err (APIERR_BADPARS, "unknown type");
	GS::Array<API_Guid> all, out;
	ACAPI_Element_GetElemList (API_ElemType (t), &all);
	bool hasLayer = p.Contains ("layer"), hasFloor = p.Contains ("floor");
	for (const auto& g : all) {
		API_Elem_Head h {}; h.guid = g;
		if (ACAPI_Element_GetHeader (&h) != NoError) continue;
		if (hasLayer && h.layer.ToInt32_Deprecated () != (Int32) Num (p, "layer", 0)) continue;
		if (hasFloor && h.floorInd != (short) Num (p, "floor", 0)) continue;
		out.Push (g);
	}
	bool sel = false; p.Get ("select", sel);
	if (sel) { GS::Array<API_Neig> n; for (const auto& g : out) n.Push (API_Neig (g)); ACAPI_Selection_DeselectAll (); if (!n.IsEmpty ()) ACAPI_Selection_Select (n, true); }
	return GS::ObjectState ("elements", GuidStrings (out));
}

// { from:guid, to:[guid] } pick up the settings of one element and inject them into others of the same type (geometry kept)
GS::ObjectState CopyElementSettings (const GS::ObjectState& p)
{
	GS::Array<API_Guid> from = GuidList (p, "from"), to = GuidList (p, "to");
	if (from.IsEmpty () || to.IsEmpty ()) return Err (APIERR_BADPARS, "from and to required");
	API_Element src {}; src.header.guid = from[0];
	GSErrCode e = ACAPI_Element_Get (&src);
	if (e != NoError) return Err (e, "source not found");
	API_ElementMemo memo {};
	ACAPI_Element_GetMemo (from[0], &memo, APIMemoMask_AddPars);
	API_Element mask; ACAPI_ELEMENT_MASK_SETFULL (mask);
	GSErrCode inner = NoError;
	ACAPI_CallUndoableCommand ("Claude: Copy settings", [&] () -> GSErrCode {
		inner = ACAPI_Element_ChangeParameters (to, &src, memo.params != nullptr ? &memo : nullptr, &mask);
		return inner;
	});
	ACAPI_DisposeElemMemoHdls (&memo);
	if (inner != NoError) return Err (inner, "ChangeParameters failed");
	return GS::ObjectState ("changed", (Int32) to.GetSize ());
}

// { roof:guid } -> roof data (class, level, thickness, plane angle, levels, eaves, pivot edge angles)
GS::ObjectState GetRoof (const GS::ObjectState& p)
{
	GS::Array<API_Guid> g = GuidList (p, "roof");
	if (g.IsEmpty ()) return Err (APIERR_BADPARS, "roof required");
	API_Element el {}; el.header.guid = g[0];
	GSErrCode e = ACAPI_Element_Get (&el);
	if (e != NoError || el.header.type.typeID != API_RoofID) return Err (e ? e : APIERR_BADELEMENTTYPE, "not a roof");
	const API_RoofType& r = el.roof;
	GS::ObjectState o ("class", GS::UniString (r.roofClass == API_PlaneRoofID ? "plane" : "poly"), "level", r.shellBase.level, "thickness", r.shellBase.thickness);
	if (r.roofClass == API_PlaneRoofID) { o.Add ("angle", r.u.planeRoof.angle); }
	else {
		o.Add ("eavesOverhang", r.u.polyRoof.eavesOverHang);
		auto lv = o.AddList<GS::ObjectState> ("levels");
		for (short i = 0; i < r.u.polyRoof.levelNum; ++i) lv (GS::ObjectState ("height", r.u.polyRoof.levelData[i].levelHeight, "angle", r.u.polyRoof.levelData[i].levelAngle));
		API_ElementMemo memo {};
		if (ACAPI_Element_GetMemo (g[0], &memo, APIMemoMask_All) == NoError && memo.pivotPolyEdges != nullptr) {
			auto ed = o.AddList<GS::ObjectState> ("edges");
			Int32 n = (Int32) (BMpGetSize ((GSPtr) memo.pivotPolyEdges) / sizeof (API_PivotPolyEdgeData));
			for (Int32 i = 0; i < n; ++i) {
				const API_PivotPolyEdgeData& d = memo.pivotPolyEdges[i];
				if (d.levelEdgeData == nullptr) { ed (GS::ObjectState ("edge", i)); continue; }
				ed (GS::ObjectState ("edge", i, "angle", d.levelEdgeData[0].angle, "eavesOverhang", d.levelEdgeData[0].eavesOverhang));
			}
		}
		ACAPI_DisposeElemMemoHdls (&memo);
	}
	return o;
}

// { roof:guid, level, thickness, angle (plane), eavesOverhang, levels:[{height,angle}], edges:[{edge, angle, eavesOverhang}] }
GS::ObjectState SetRoof (const GS::ObjectState& p)
{
	GS::Array<API_Guid> g = GuidList (p, "roof");
	if (g.IsEmpty ()) return Err (APIERR_BADPARS, "roof required");
	GSErrCode inner = NoError;
	ACAPI_CallUndoableCommand ("Claude: Roof", [&] () -> GSErrCode {
		API_Element el {}; el.header.guid = g[0];
		if ((inner = ACAPI_Element_Get (&el)) != NoError) return inner;
		API_Element mask; ACAPI_ELEMENT_MASK_CLEAR (mask);
		API_RoofType& r = el.roof;
		if (p.Contains ("level")) { r.shellBase.level = Num (p, "level", 0); ACAPI_ELEMENT_MASK_SET (mask, API_RoofType, shellBase.level); }
		if (p.Contains ("thickness")) { r.shellBase.thickness = Num (p, "thickness", 0); ACAPI_ELEMENT_MASK_SET (mask, API_RoofType, shellBase.thickness); }
		if (p.Contains ("angle") && r.roofClass == API_PlaneRoofID) { r.u.planeRoof.angle = Num (p, "angle", 0); ACAPI_ELEMENT_MASK_SET (mask, API_RoofType, u.planeRoof.angle); }
		API_ElementMemo memo {}; UInt64 mm = 0;
		if (r.roofClass == API_PolyRoofID) {
			if (p.Contains ("eavesOverhang")) { r.u.polyRoof.eavesOverHang = Num (p, "eavesOverhang", 0); ACAPI_ELEMENT_MASK_SET (mask, API_RoofType, u.polyRoof.eavesOverHang); }
			GS::Array<GS::ObjectState> lv;
			if (p.Get ("levels", lv) && !lv.IsEmpty ()) {
				r.u.polyRoof.levelNum = (short) std::min<USize> (lv.GetSize (), 16);
				for (short i = 0; i < r.u.polyRoof.levelNum; ++i) { r.u.polyRoof.levelData[i].levelHeight = Num (lv[i], "height", 0); r.u.polyRoof.levelData[i].levelAngle = Num (lv[i], "angle", 0.5); }
				ACAPI_ELEMENT_MASK_SET (mask, API_RoofType, u.polyRoof.levelNum); ACAPI_ELEMENT_MASK_SET (mask, API_RoofType, u.polyRoof.levelData);
			}
			GS::Array<GS::ObjectState> ed;
			if (p.Get ("edges", ed) && !ed.IsEmpty () && ACAPI_Element_GetMemo (g[0], &memo, APIMemoMask_All) == NoError && memo.pivotPolyEdges != nullptr) {
				Int32 n = (Int32) (BMpGetSize ((GSPtr) memo.pivotPolyEdges) / sizeof (API_PivotPolyEdgeData));
				for (const auto& x : ed) {
					Int32 i = (Int32) Num (x, "edge", -1);
					if (i < 0 || i >= n || memo.pivotPolyEdges[i].levelEdgeData == nullptr) continue;
					if (x.Contains ("angle")) memo.pivotPolyEdges[i].levelEdgeData[0].angle = Num (x, "angle", 0);
					if (x.Contains ("eavesOverhang")) memo.pivotPolyEdges[i].levelEdgeData[0].eavesOverhang = Num (x, "eavesOverhang", 0);
				}
				mm = APIMemoMask_All;
			}
		}
		inner = ACAPI_Element_Change (&el, &mask, mm ? &memo : nullptr, mm, true);
		ACAPI_DisposeElemMemoHdls (&memo);
		return inner;
	});
	if (inner != NoError) return Err (inner, "Roof change failed");
	return GS::ObjectState ("success", true);
}

// { element:guid, contours:[[{x,y,z?}...], ...] } replace the polygon of a slab, hatch or mesh (first contour = outline, others = holes)
GS::ObjectState SetPolygon (const GS::ObjectState& p)
{
	GS::Array<API_Guid> g = GuidList (p, "element");
	GS::Array<GS::ObjectState> contoursObj;
	GS::Array<GS::Array<GS::ObjectState>> contours;
	{ GS::ObjectState holder; }
	if (g.IsEmpty ()) return Err (APIERR_BADPARS, "element required");
	// contours passed as list of {points:[...]}
	p.Get ("contours", contoursObj);
	for (const auto& c : contoursObj) { GS::Array<GS::ObjectState> pts; c.Get ("points", pts); if (pts.GetSize () >= 3) contours.Push (pts); }
	if (contours.IsEmpty ()) return Err (APIERR_BADPARS, "contours:[{points:[...]}] required");
	GSErrCode inner = NoError;
	ACAPI_CallUndoableCommand ("Claude: Polygon", [&] () -> GSErrCode {
		API_Element el {}; el.header.guid = g[0];
		if ((inner = ACAPI_Element_Get (&el)) != NoError) return inner;
		API_ElemTypeID t = el.header.type.typeID;
		if (t != API_SlabID && t != API_HatchID && t != API_MeshID) { inner = APIERR_BADELEMENTTYPE; return inner; }
		Int32 nc = 0; for (const auto& c : contours) nc += (Int32) c.GetSize () + 1;
		API_ElementMemo memo {};
		memo.coords = (API_Coord**) BMhAllClear ((nc + 1) * sizeof (API_Coord));
		memo.pends = (Int32**) BMhAllClear ((contours.GetSize () + 1) * sizeof (Int32));
		if (t == API_MeshID) memo.meshPolyZ = (double**) BMhAllClear ((nc + 1) * sizeof (double));
		Int32 k = 0; (*memo.pends)[0] = 0;
		for (UIndex ci = 0; ci < contours.GetSize (); ++ci) {
			const auto& c = contours[ci]; Int32 start = k + 1;
			for (const auto& q : c) { ++k; (*memo.coords)[k] = { Num (q, "x", 0), Num (q, "y", 0) }; if (memo.meshPolyZ) (*memo.meshPolyZ)[k] = Num (q, "z", 0); }
			++k; (*memo.coords)[k] = (*memo.coords)[start]; if (memo.meshPolyZ) (*memo.meshPolyZ)[k] = (*memo.meshPolyZ)[start];
			(*memo.pends)[ci + 1] = k;
		}
		API_Polygon* poly = (t == API_SlabID) ? &el.slab.poly : (t == API_HatchID ? &el.hatch.poly : &el.mesh.poly);
		poly->nCoords = nc; poly->nSubPolys = (Int32) contours.GetSize (); poly->nArcs = 0;
		API_Element mask; ACAPI_ELEMENT_MASK_CLEAR (mask);
		if (t == API_SlabID) ACAPI_ELEMENT_MASK_SET (mask, API_SlabType, poly); else if (t == API_HatchID) ACAPI_ELEMENT_MASK_SET (mask, API_HatchType, poly); else ACAPI_ELEMENT_MASK_SET (mask, API_MeshType, poly);
		inner = ACAPI_Element_Change (&el, &mask, &memo, APIMemoMask_Polygon | (t == API_MeshID ? APIMemoMask_MeshPolyZ : 0), true);
		ACAPI_DisposeElemMemoHdls (&memo);
		return inner;
	});
	if (inner != NoError) return Err (inner, "Polygon change failed");
	return GS::ObjectState ("success", true);
}

// { path:"/abs/file.pln" } save a copy of the project as a new PLN (Save As)
GS::ObjectState SaveProjectAs (const GS::ObjectState& p)
{
	GS::UniString path; p.Get ("path", path);
	if (path.IsEmpty ()) return Err (APIERR_BADPARS, "path required");
	IO::Location loc (path);
	API_FileSavePars fsp {};
	fsp.fileTypeID = APIFType_PlanFile;
	fsp.file = &loc;
	GSErrCode e = ACAPI_ProjectOperation_Save (&fsp);
	if (e != NoError) return Err (e, "Save as failed");
	return GS::ObjectState ("success", true);
}

// { path:"/abs/image.png" } photo-render the current 3D view with the current scene into a file
GS::ObjectState RenderImage (const GS::ObjectState& p)
{
	GS::UniString path; p.Get ("path", path);
	if (path.IsEmpty ()) return Err (APIERR_BADPARS, "path required");
	IO::Location loc (path);
	API_PhotoRenderPars pr {};
	pr.fileTypeID = path.EndsWith (".jpg") ? APIFType_JPEGFile : (path.EndsWith (".tif") ? APIFType_TIFFFile : APIFType_PNGFile);
	pr.file = &loc;
	pr.colorDepth = APIColorDepth_MiC;
	GSErrCode e = ACAPI_Rendering_PhotoRender (&pr);
	if (e != NoError) return Err (e, "Render failed");
	return GS::ObjectState ("success", true);
}
