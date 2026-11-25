// ClaudeTools — Archicad 30 add-on exposing JSON commands (namespace "ClaudeTools")
// Called through the Archicad JSON API: API.ExecuteAddOnCommand
//   { "addOnCommandId": { "commandNamespace": "ClaudeTools", "commandName": "<Name>" },
//     "addOnCommandParameters": { ... } }

#include "APIEnvir.h"
#include "ACAPinc.h"
#include "RS.hpp"
#include "ObjectState.hpp"
#include "Location.hpp"

#include <functional>
#include <cmath>
#include <algorithm>

#define CT_ADDON_NAME 32000

namespace {

using Handler = std::function<GS::ObjectState (const GS::ObjectState&)>;

GS::ObjectState Err (GSErrCode code, const GS::UniString& msg)
{
	return GS::ObjectState ("error", GS::ObjectState ("code", (Int32) code, "message", msg));
}

class GenericCommand : public API_AddOnCommand {
	GS::String name;
	Handler    handler;
public:
	GenericCommand (const GS::String& n, Handler h) : name (n), handler (std::move (h)) {}
	GS::String GetName () const override { return name; }
	GS::String GetNamespace () const override { return "ClaudeTools"; }
	GS::Optional<GS::UniString> GetSchemaDefinitions () const override { return {}; }
	GS::Optional<GS::UniString> GetInputParametersSchema () const override { return {}; }
	GS::Optional<GS::UniString> GetResponseSchema () const override { return {}; }
	API_AddOnCommandExecutionPolicy GetExecutionPolicy () const override { return API_AddOnCommandExecutionPolicy::ScheduleForExecutionOnMainThread; }
	bool IsProcessWindowVisible () const override { return false; }
	void OnResponseValidationFailed (const GS::ObjectState&) const override {}
	GS::ObjectState Execute (const GS::ObjectState& p, GS::ProcessControl&) const override
	{
		// Model operations work on the floor plan database; switch to it if a layout/section is in front.
		bool useFloorPlan = true;
		p.Get ("useFloorPlanDatabase", useFloorPlan);
		API_DatabaseInfo saved {};
		bool switched = false;
		GS::UniString layoutDb;
		if (p.Get ("layoutDatabase", layoutDb) && ACAPI_Database_GetCurrentDatabase (&saved) == NoError) {
			API_DatabaseInfo ld {};
			ld.typeID = APIWind_LayoutID;
			ld.databaseUnId.elemSetId = APIGuidFromString (layoutDb.ToCStr ().Get ());
			switched = (ACAPI_Database_ChangeCurrentDatabase (&ld) == NoError);
			useFloorPlan = false;
		}
		if (!switched && useFloorPlan && name != "Rebuild" && name != "Ping" &&
			ACAPI_Database_GetCurrentDatabase (&saved) == NoError && saved.typeID != APIWind_FloorPlanID) {
			API_DatabaseInfo fp {};
			fp.typeID = APIWind_FloorPlanID;
			switched = (ACAPI_Database_ChangeCurrentDatabase (&fp) == NoError);
		}
		GS::ObjectState r;
		try {
			r = handler (p);
		} catch (...) {
			r = Err (APIERR_GENERAL, "Exception in " + GS::UniString (name.ToCStr ()));
		}
		if (switched)
			ACAPI_Database_ChangeCurrentDatabase (&saved);
		return r;
	}
};

// ---- helpers ---------------------------------------------------------------

GS::Array<API_Guid> GuidList (const GS::ObjectState& p, const char* field)
{
	GS::Array<API_Guid> out;
	GS::Array<GS::UniString> strs;
	if (p.Get (field, strs)) {
		for (const auto& s : strs)
			out.Push (APIGuidFromString (s.ToCStr ().Get ()));
	} else {
		GS::UniString one;
		if (p.Get (field, one))
			out.Push (APIGuidFromString (one.ToCStr ().Get ()));
	}
	return out;
}

GS::Array<GS::UniString> GuidStrings (const GS::Array<API_Guid>& g)
{
	GS::Array<GS::UniString> out;
	for (const auto& x : g)
		out.Push (APIGuidToString (x));
	return out;
}

bool ParseSolidOp (const GS::UniString& s, API_SolidOperationID& op)
{
	if (s == "subtract")          op = APISolid_Substract;
	else if (s == "subtractUp")   op = APISolid_SubstUp;
	else if (s == "subtractDown") op = APISolid_SubstDown;
	else if (s == "intersect")    op = APISolid_Intersect;
	else if (s == "add")          op = APISolid_Add;
	else return false;
	return true;
}

GS::UniString SolidOpName (API_SolidOperationID op)
{
	switch (op) {
		case APISolid_Substract:  return "subtract";
		case APISolid_SubstUp:    return "subtractUp";
		case APISolid_SubstDown:  return "subtractDown";
		case APISolid_Intersect:  return "intersect";
		case APISolid_Add:        return "add";
		default:                  return "unknown";
	}
}

bool ParseTrim (const GS::UniString& s, API_TrimTypeID& t)
{
	if (s == "keepInside")       t = APITrim_KeepInside;
	else if (s == "keepOutside") t = APITrim_KeepOutside;
	else if (s == "keepAll")     t = APITrim_KeepAll;
	else return false;
	return true;
}

// ---- commands --------------------------------------------------------------

// { targets:[guid], operators:[guid], operation:"subtract|subtractUp|subtractDown|intersect|add",
//   inheritOperatorAttributes:bool, skipPolygonHoles:bool }
GS::ObjectState CreateSolidLinks (const GS::ObjectState& p)
{
	GS::Array<API_Guid> targets = GuidList (p, "targets");
	GS::Array<API_Guid> operators = GuidList (p, "operators");
	GS::UniString opStr ("subtract");
	p.Get ("operation", opStr);
	API_SolidOperationID op;
	if (!ParseSolidOp (opStr, op))
		return Err (APIERR_BADPARS, "operation must be subtract|subtractUp|subtractDown|intersect|add");
	bool inherit = false, skipHoles = false;
	p.Get ("inheritOperatorAttributes", inherit);
	p.Get ("skipPolygonHoles", skipHoles);
	GSFlags flags = 0;
	if (inherit)   flags |= APISolidFlag_OperatorAttrib;
	if (skipHoles) flags |= APISolidFlag_SkipPolygonHoles;

	Int32 ok = 0;
	GS::ObjectState result;
	auto failures = result.AddList<GS::ObjectState> ("failures");
	GSErrCode err = ACAPI_CallUndoableCommand ("Claude: Solid Element Operation", [&] () -> GSErrCode {
		for (const auto& t : targets)
			for (const auto& o : operators) {
				GSErrCode e = ACAPI_Element_SolidLink_Create (t, o, op, flags);
				if (e == NoError) ++ok;
				else failures (GS::ObjectState ("target", APIGuidToString (t), "operator", APIGuidToString (o), "code", (Int32) e));
			}
		return NoError;
	});
	if (err != NoError) return Err (err, "Undoable command failed");
	result.Add ("created", ok);
	return result;
}

// { targets:[guid], operators:[guid] }  (operators omitted = all operators of each target)
GS::ObjectState RemoveSolidLinks (const GS::ObjectState& p)
{
	GS::Array<API_Guid> targets = GuidList (p, "targets");
	GS::Array<API_Guid> operators = GuidList (p, "operators");
	Int32 ok = 0, failed = 0;
	GSErrCode err = ACAPI_CallUndoableCommand ("Claude: Remove Solid Links", [&] () -> GSErrCode {
		for (const auto& t : targets) {
			GS::Array<API_Guid> ops = operators;
			if (ops.IsEmpty ())
				ACAPI_Element_SolidLink_GetOperators (t, &ops);
			for (const auto& o : ops) {
				if (ACAPI_Element_SolidLink_Remove (t, o) == NoError) ++ok; else ++failed;
			}
		}
		return NoError;
	});
	if (err != NoError) return Err (err, "Undoable command failed");
	return GS::ObjectState ("removed", ok, "failed", failed);
}

// { elements:[guid] } -> per element: operators (with operation) and targets
GS::ObjectState GetSolidLinks (const GS::ObjectState& p)
{
	GS::ObjectState result;
	auto list = result.AddList<GS::ObjectState> ("elements");
	for (const auto& g : GuidList (p, "elements")) {
		GS::Array<API_Guid> ops, tgs;
		ACAPI_Element_SolidLink_GetOperators (g, &ops);
		ACAPI_Element_SolidLink_GetTargets (g, &tgs);
		GS::ObjectState e ("guid", APIGuidToString (g));
		auto opList = e.AddList<GS::ObjectState> ("operators");
		for (const auto& o : ops) {
			API_SolidOperationID op = APISolid_Substract;
			ACAPI_Element_SolidLink_GetOperation (g, o, &op);
			opList (GS::ObjectState ("guid", APIGuidToString (o), "operation", SolidOpName (op)));
		}
		e.Add ("targets", GuidStrings (tgs));
		list (e);
	}
	return result;
}

// { elements:[guid], trimmingElement:guid (roof/shell, optional), trimType:"keepInside|keepOutside|keepAll" }
// Without trimmingElement, the roofs/shells must be in 'elements' (like the menu command).
GS::ObjectState TrimElements (const GS::ObjectState& p)
{
	GS::Array<API_Guid> elems = GuidList (p, "elements");
	GS::Array<API_Guid> with = GuidList (p, "trimmingElement");
	GS::UniString tStr ("keepInside");
	p.Get ("trimType", tStr);
	API_TrimTypeID tt;
	if (!ParseTrim (tStr, tt))
		return Err (APIERR_BADPARS, "trimType must be keepInside|keepOutside|keepAll");
	GSErrCode inner = NoError;
	GSErrCode err = ACAPI_CallUndoableCommand ("Claude: Trim Elements", [&] () -> GSErrCode {
		inner = with.IsEmpty () ? ACAPI_Element_Trim_Elements (elems)
		                        : ACAPI_Element_Trim_ElementsWith (elems, with[0], tt);
		return inner;
	});
	if (err != NoError || inner != NoError) return Err (inner != NoError ? inner : err, "Trim failed");
	return GS::ObjectState ("success", true);
}

// { element1:guid, element2:guid }
GS::ObjectState RemoveTrim (const GS::ObjectState& p)
{
	GS::Array<API_Guid> a = GuidList (p, "element1"), b = GuidList (p, "element2");
	if (a.IsEmpty () || b.IsEmpty ()) return Err (APIERR_BADPARS, "element1 and element2 required");
	GSErrCode inner = NoError;
	ACAPI_CallUndoableCommand ("Claude: Remove Trim", [&] () -> GSErrCode {
		inner = ACAPI_Element_Trim_Remove (a[0], b[0]);
		return inner;
	});
	if (inner != NoError) return Err (inner, "Remove trim failed");
	return GS::ObjectState ("success", true);
}

// { elements:[guid] } -> trimmedBy / trims
GS::ObjectState GetTrims (const GS::ObjectState& p)
{
	GS::ObjectState result;
	auto list = result.AddList<GS::ObjectState> ("elements");
	for (const auto& g : GuidList (p, "elements")) {
		GS::Array<API_Guid> trimming, trimmed;
		ACAPI_Element_Trim_GetTrimmingElements (g, &trimming);
		ACAPI_Element_Trim_GetTrimmedElements (g, &trimmed);
		list (GS::ObjectState ("guid", APIGuidToString (g),
		                       "trimmedBy", GuidStrings (trimming),
		                       "trims", GuidStrings (trimmed)));
	}
	return result;
}

// { openings:[guid], flipSide:bool (mirror across wall axis, i.e. swing in/out),
//   flipHinge:bool (mirror along wall, i.e. left/right) }
// or explicit values { reflected:bool, oSide:bool }
GS::ObjectState FlipOpenings (const GS::ObjectState& p)
{
	bool flipSide = false, flipHinge = false;
	p.Get ("flipSide", flipSide);
	p.Get ("flipHinge", flipHinge);
	bool hasRefl = false, hasOSide = false, setRefl = false, setOSide = false;
	hasRefl = p.Get ("reflected", setRefl);
	hasOSide = p.Get ("oSide", setOSide);

	GS::ObjectState result;
	auto list = result.AddList<GS::ObjectState> ("openings");
	GSErrCode err = ACAPI_CallUndoableCommand ("Claude: Flip Openings", [&] () -> GSErrCode {
		for (const auto& g : GuidList (p, "openings")) {
			API_Element el {};
			el.header.guid = g;
			GSErrCode e = ACAPI_Element_Get (&el);
			if (e != NoError) { list (GS::ObjectState ("guid", APIGuidToString (g), "code", (Int32) e)); continue; }
			API_OpeningBaseType* ob = nullptr;
			API_Element mask;
			ACAPI_ELEMENT_MASK_CLEAR (mask);
			if (el.header.type.typeID == API_DoorID) {
				ob = &el.door.openingBase;
				ACAPI_ELEMENT_MASK_SET (mask, API_DoorType, openingBase.reflected);
				ACAPI_ELEMENT_MASK_SET (mask, API_DoorType, openingBase.oSide);
				ACAPI_ELEMENT_MASK_SET (mask, API_DoorType, openingBase.refSide);
			} else if (el.header.type.typeID == API_WindowID) {
				ob = &el.window.openingBase;
				ACAPI_ELEMENT_MASK_SET (mask, API_WindowType, openingBase.reflected);
				ACAPI_ELEMENT_MASK_SET (mask, API_WindowType, openingBase.oSide);
				ACAPI_ELEMENT_MASK_SET (mask, API_WindowType, openingBase.refSide);
			} else {
				list (GS::ObjectState ("guid", APIGuidToString (g), "code", (Int32) APIERR_BADELEMENTTYPE));
				continue;
			}
			if (flipHinge) ob->reflected = !ob->reflected;
			if (flipSide)  ob->oSide = !ob->oSide;
			if (hasRefl)   ob->reflected = setRefl;
			if (hasOSide)  ob->oSide = setOSide;
			ob->refSide = ob->oSide;
			e = ACAPI_Element_Change (&el, &mask, nullptr, 0, true);
			list (GS::ObjectState ("guid", APIGuidToString (g), "code", (Int32) e,
			                       "reflected", ob->reflected, "oSide", ob->oSide));
		}
		return NoError;
	});
	if (err != NoError) return Err (err, "Undoable command failed");
	return result;
}

// { regenerate:bool }  Rebuild (or Rebuild & Regenerate) the front window
GS::ObjectState Rebuild (const GS::ObjectState& p)
{
	bool regen = false;
	p.Get ("regenerate", regen);
	GSErrCode e = ACAPI_View_Rebuild (&regen);
	if (e != NoError) return Err (e, "Rebuild failed");
	return GS::ObjectState ("success", true);
}

// { walls:[guid], value:bool }  Wall "align texture to wall edges"
GS::ObjectState SetWallTextureAlign (const GS::ObjectState& p)
{
	bool value = true;
	p.Get ("value", value);
	Int32 ok = 0, failed = 0;
	ACAPI_CallUndoableCommand ("Claude: Wall Texture Align", [&] () -> GSErrCode {
		for (const auto& g : GuidList (p, "walls")) {
			API_Element el {};
			el.header.guid = g;
			if (ACAPI_Element_Get (&el) != NoError || el.header.type.typeID != API_WallID) { ++failed; continue; }
			API_Element mask;
			ACAPI_ELEMENT_MASK_CLEAR (mask);
			ACAPI_ELEMENT_MASK_SET (mask, API_WallType, alignTexture);
			el.wall.alignTexture = value;
			if (ACAPI_Element_Change (&el, &mask, nullptr, 0, true) == NoError) ++ok; else ++failed;
		}
		return NoError;
	});
	return GS::ObjectState ("changed", ok, "failed", failed);
}

double Num (const GS::ObjectState& o, const char* f, double def)
{
	double v = def;
	if (!o.Get (f, v)) { Int32 i; if (o.Get (f, i)) v = i; }
	return v;
}

// { items:[{x,y,text,layer,size(mm),pen,angle(rad),floor}] }
GS::ObjectState CreateTexts (const GS::ObjectState& p)
{
	GS::Array<GS::ObjectState> items;
	p.Get ("items", items);
	GS::ObjectState result;
	auto list = result.AddList<GS::UniString> ("created");
	auto fails = result.AddList<Int32> ("errors");
	ACAPI_CallUndoableCommand ("Claude: Create Texts", [&] () -> GSErrCode {
		for (const auto& it : items) {
			API_Element el {};
			API_ElementMemo memo {};
			el.header.type = API_TextID;
			if (ACAPI_Element_GetDefaults (&el, &memo) != NoError) { fails ((Int32) -1); continue; }
			ACAPI_DisposeElemMemoHdls (&memo);
			memo = {};
			GS::UniString txt;
			it.Get ("text", txt);
			el.header.floorInd = (short) Num (it, "floor", 0);
			el.header.layer = ACAPI_CreateAttributeIndex ((Int32) Num (it, "layer", 1));
			el.text.loc.x = Num (it, "x", 0);
			el.text.loc.y = Num (it, "y", 0);
			el.text.size = Num (it, "size", 2.0);
			el.text.pen = (short) Num (it, "pen", el.text.pen);
			el.text.angle = Num (it, "angle", 0);
			el.text.anchor = APIAnc_MM;
			el.text.just = APIJust_Center;
			{
				GS::UniString j, an;
				if (it.Get ("just", j)) el.text.just = (j == "left") ? APIJust_Left : (j == "right" ? APIJust_Right : APIJust_Center);
				if (it.Get ("anchor", an)) {
					const char* names[] = { "LT", "MT", "RT", "LM", "MM", "RM", "LB", "MB", "RB" };
					const API_AnchorID ids[] = { APIAnc_LT, APIAnc_MT, APIAnc_RT, APIAnc_LM, APIAnc_MM, APIAnc_RM, APIAnc_LB, APIAnc_MB, APIAnc_RB };
					for (int k = 0; k < 9; ++k) if (an == names[k]) el.text.anchor = ids[k];
				}
			}
			el.text.nonBreaking = true;
			el.text.nLine = 1;
			if (Num (it, "fillPen", 0) > 0) { el.text.usedFill = true; el.text.fillPen = (short) Num (it, "fillPen", 0); }
			memo.textContent = new GS::UniString (txt);
			GSErrCode e = ACAPI_Element_Create (&el, &memo);
			ACAPI_DisposeElemMemoHdls (&memo);
			if (e == NoError) list (APIGuidToString (el.header.guid)); else fails ((Int32) e);
		}
		return NoError;
	});
	return result;
}

// { items:[{points:[{x,y}...], refX, refY, dirX, dirY, layer, floor}] }  static linear dimensions
GS::ObjectState CreateDimensions (const GS::ObjectState& p)
{
	GS::Array<GS::ObjectState> items;
	p.Get ("items", items);
	GS::ObjectState result;
	auto list = result.AddList<GS::UniString> ("created");
	auto fails = result.AddList<Int32> ("errors");
	ACAPI_CallUndoableCommand ("Claude: Create Dimensions", [&] () -> GSErrCode {
		for (const auto& it : items) {
			GS::Array<GS::ObjectState> pts;
			it.Get ("points", pts);
			if (pts.GetSize () < 2) { fails ((Int32) APIERR_BADPARS); continue; }
			API_Element el {};
			API_ElementMemo memo {};
			el.header.type = API_DimensionID;
			if (ACAPI_Element_GetDefaults (&el, &memo) != NoError) { fails ((Int32) -1); continue; }
			ACAPI_DisposeElemMemoHdls (&memo);
			memo = {};
			el.header.floorInd = (short) Num (it, "floor", 0);
			el.header.layer = ACAPI_CreateAttributeIndex ((Int32) Num (it, "layer", 1));
			el.dimension.dimAppear = APIApp_Normal;
			el.dimension.textPos = APIPos_Above;
			el.dimension.textWay = APIDir_Parallel;
			el.dimension.defStaticDim = true;
			el.dimension.usedIn3D = false;
			el.dimension.horizontalText = false;
			double rx = Num (it, "refX", 0), ry = Num (it, "refY", 0);
			double dx = Num (it, "dirX", 1), dy = Num (it, "dirY", 0);
			double len = sqrt (dx * dx + dy * dy); if (len < 1e-9) { dx = 1; dy = 0; len = 1; }
			dx /= len; dy /= len;
			el.dimension.refC.x = rx; el.dimension.refC.y = ry;
			el.dimension.direction.x = dx; el.dimension.direction.y = dy;
			el.dimension.nDimElem = (Int32) pts.GetSize ();
			memo.dimElems = reinterpret_cast<API_DimElem**> (BMhAllClear (el.dimension.nDimElem * sizeof (API_DimElem)));
			for (Int32 i = 0; i < el.dimension.nDimElem; ++i) {
				API_DimElem& d = (*memo.dimElems)[i];
				double px = Num (pts[i], "x", 0), py = Num (pts[i], "y", 0);
				d.base.loc.x = px; d.base.loc.y = py;
				d.note = el.dimension.defNote;
				d.witnessVal = el.dimension.defWitnessVal;
				d.witnessForm = el.dimension.defWitnessForm;
				d.witnessExtensionLinePen = el.dimension.defWitnessExtensionLinePen;
				d.witnessExtensionLineType = el.dimension.defWitnessExtensionLineType;
				d.witnessExtensionGap = el.dimension.defWitnessExtensionGap;
				d.witnessExtensionGapSize = el.dimension.defWitnessExtensionGapSize;
				d.witnessOverrideExtension = el.dimension.defWitnessOverrideExtension;
				d.fixedPos = true;
				if (Num (it, "bgPen", 0) > 0) { d.note.opaque = true; d.note.backgroundPen = (short) Num (it, "bgPen", 0); }
				double t = (px - rx) * dx + (py - ry) * dy;
				d.pos.x = rx + t * dx; d.pos.y = ry + t * dy;
			}
			GSErrCode e = ACAPI_Element_Create (&el, &memo);
			ACAPI_DisposeElemMemoHdls (&memo);
			if (e == NoError) list (APIGuidToString (el.header.guid)); else fails ((Int32) e);
		}
		return NoError;
	});
	return result;
}

// { layer:int, visibleIn:[combination names] }  -> layer visible in those, hidden in all others
GS::ObjectState SetLayerInCombinations (const GS::ObjectState& p)
{
	Int32 layerIdx = (Int32) Num (p, "layer", 0);
	GS::Array<GS::UniString> visibleIn;
	p.Get ("visibleIn", visibleIn);
	API_AttributeIndex li = ACAPI_CreateAttributeIndex (layerIdx);
	GS::Array<API_Attribute> combs;
	ACAPI_Attribute_GetAttributesByType (API_LayerCombID, combs);
	GS::ObjectState result;
	auto list = result.AddList<GS::ObjectState> ("combinations");
	ACAPI_CallUndoableCommand ("Claude: Layer combinations", [&] () -> GSErrCode {
		for (API_Attribute comb : combs) {
			API_AttributeDef defs {};
			if (ACAPI_Attribute_GetDef (API_LayerCombID, comb.header.index, &defs) != NoError) continue;
			GS::UniString name (comb.header.name);
			bool show = visibleIn.Contains (name);
			API_LayerStat st {};
			if (defs.layer_statItems != nullptr && defs.layer_statItems->ContainsKey (li))
				st = defs.layer_statItems->Get (li);
			else if (defs.layer_statItems != nullptr)
				defs.layer_statItems->Add (li, st);
			if (show) st.lFlags &= ~APILay_Hidden; else st.lFlags |= APILay_Hidden;
			if (defs.layer_statItems != nullptr) defs.layer_statItems->Set (li, st);
			GSErrCode e = ACAPI_Attribute_Modify (&comb, &defs);
			list (GS::ObjectState ("name", name, "visible", show, "code", (Int32) e));
			ACAPI_DisposeAttrDefsHdls (&defs);
		}
		return NoError;
	});
	return result;
}

// { elements:[guid], layer:int }
GS::ObjectState MoveToLayer (const GS::ObjectState& p)
{
	Int32 layerIdx = (Int32) Num (p, "layer", 1);
	Int32 ok = 0, failed = 0;
	ACAPI_CallUndoableCommand ("Claude: Move to layer", [&] () -> GSErrCode {
		for (const auto& g : GuidList (p, "elements")) {
			API_Element el {};
			el.header.guid = g;
			if (ACAPI_Element_Get (&el) != NoError) { ++failed; continue; }
			API_Element mask;
			ACAPI_ELEMENT_MASK_CLEAR (mask);
			ACAPI_ELEMENT_MASK_SET (mask, API_Elem_Head, layer);
			el.header.layer = ACAPI_CreateAttributeIndex (layerIdx);
			if (ACAPI_Element_Change (&el, &mask, nullptr, 0, true) == NoError) ++ok; else ++failed;
		}
		return NoError;
	});
	return GS::ObjectState ("changed", ok, "failed", failed);
}

// { elements:[guid] }
GS::ObjectState DeleteElements (const GS::ObjectState& p)
{
	// acts in the current window's database (or layoutDatabase); reports only elements that existed there
	GS::Array<API_Guid> all = GuidList (p, "elements"), g;
	for (const auto& x : all) { API_Elem_Head h {}; h.guid = x; if (ACAPI_Element_GetHeader (&h) == NoError) g.Push (x); }
	GSErrCode inner = NoError;
	if (!g.IsEmpty ()) {
		ACAPI_CallUndoableCommand ("Claude: Delete elements", [&] () -> GSErrCode {
			inner = ACAPI_Element_Delete (g);
			return inner;
		});
	}
	if (inner != NoError) return Err (inner, "Delete failed");
	Int32 left = 0;
	for (const auto& x : g) { API_Elem_Head h {}; h.guid = x; if (ACAPI_Element_GetHeader (&h) == NoError) ++left; }
	return GS::ObjectState ("deleted", (Int32) g.GetSize () - left, "notFound", (Int32) (all.GetSize () - g.GetSize ()), "notDeleted", left);
}

// { maxDistance:m } -> elements in the floor plan whose bounds go beyond maxDistance from origin
GS::ObjectState FarElements (const GS::ObjectState& p)
{
	double maxD = Num (p, "maxDistance", 500);
	GS::Array<API_Guid> all;
	ACAPI_Element_GetElemList (API_ZombieElemID, &all);
	GS::ObjectState result;
	auto list = result.AddList<GS::ObjectState> ("elements");
	for (const auto& g : all) {
		API_Elem_Head h {};
		h.guid = g;
		if (ACAPI_Element_GetHeader (&h) != NoError) continue;
		API_Box3D b {};
		if (ACAPI_Element_CalcBounds (&h, &b) != NoError) continue;
		double m = std::max (std::max (fabs (b.xMin), fabs (b.xMax)), std::max (fabs (b.yMin), fabs (b.yMax)));
		if (m > maxD) {
			GS::UniString tn;
			ACAPI_Element_GetElemTypeName (h.type, tn);
			list (GS::ObjectState ("guid", APIGuidToString (g), "type", tn, "floor", (Int32) h.floorInd,
				"layer", (Int32) h.layer.ToInt32_Deprecated (),
				"xMin", b.xMin, "yMin", b.yMin, "xMax", b.xMax, "yMax", b.yMax));
		}
	}
	result.Add ("scanned", (Int32) all.GetSize ());
	return result;
}

// { drawing:guid } -> placement data of a drawing on its layout (works in the layout database)
GS::ObjectState GetDrawing (const GS::ObjectState& p)
{
	GS::Array<API_Guid> g = GuidList (p, "drawing");
	if (g.IsEmpty ()) return Err (APIERR_BADPARS, "drawing required");
	API_Element el {};
	el.header.guid = g[0];
	GSErrCode e = ACAPI_Element_Get (&el);
	if (e != NoError) return Err (e, "Get drawing failed");
	const API_DrawingType& d = el.drawing;
	GS::ObjectState r ("posX", d.pos.x, "posY", d.pos.y, "angle", d.angle, "ratio", d.ratio,
		"drawingScale", d.drawingScale, "anchor", (Int32) d.anchorPoint, "cut", d.isCutWithFrame,
		"xMin", d.bounds.xMin, "yMin", d.bounds.yMin, "xMax", d.bounds.xMax, "yMax", d.bounds.yMax);
	r.Add ("modelOffsetX", d.modelOffset.x); r.Add ("modelOffsetY", d.modelOffset.y);
	r.Add ("offsetX", d.offset.x); r.Add ("offsetY", d.offset.y);
	r.Add ("useOwnOrigo", d.useOwnOrigoAsAnchor);
	API_ElementMemo memo {};
	if (ACAPI_Element_GetMemo (el.header.guid, &memo, APIMemoMask_Polygon) == NoError && memo.coords != nullptr) {
		auto pts = r.AddList<GS::ObjectState> ("poly");
		for (Int32 i = 1; i <= d.poly.nCoords; ++i)
			pts (GS::ObjectState ("x", (*memo.coords)[i].x, "y", (*memo.coords)[i].y));
	}
	ACAPI_DisposeElemMemoHdls (&memo);
	return r;
}

// { drawing:guid, xMin,yMin,xMax,yMax (layout coords, m), anchor? } -> clip drawing with rectangular frame
GS::ObjectState SetDrawingFrame (const GS::ObjectState& p)
{
	GS::Array<API_Guid> g = GuidList (p, "drawing");
	if (g.IsEmpty ()) return Err (APIERR_BADPARS, "drawing required");
	double x0 = Num (p, "xMin", 0), y0 = Num (p, "yMin", 0), x1 = Num (p, "xMax", 0), y1 = Num (p, "yMax", 0);
	GSErrCode inner = NoError;
	ACAPI_CallUndoableCommand ("Claude: Drawing frame", [&] () -> GSErrCode {
		API_Element el {};
		el.header.guid = g[0];
		inner = ACAPI_Element_Get (&el);
		if (inner != NoError) return inner;
		API_ElementMemo memo {};
		ACAPI_Element_GetMemo (el.header.guid, &memo, APIMemoMask_Polygon);
		ACAPI_DisposeElemMemoHdls (&memo);
		memo = {};
		el.drawing.isCutWithFrame = true;
		el.drawing.poly.nCoords = 5; el.drawing.poly.nSubPolys = 1; el.drawing.poly.nArcs = 0;
		memo.coords = (API_Coord**) BMhAllClear (6 * sizeof (API_Coord));
		(*memo.coords)[1] = { x0, y0 }; (*memo.coords)[2] = { x1, y0 };
		(*memo.coords)[3] = { x1, y1 }; (*memo.coords)[4] = { x0, y1 }; (*memo.coords)[5] = { x0, y0 };
		memo.pends = (Int32**) BMhAllClear (2 * sizeof (Int32));
		(*memo.pends)[0] = 0; (*memo.pends)[1] = 5;
		API_Element mask;
		ACAPI_ELEMENT_MASK_CLEAR (mask);
		ACAPI_ELEMENT_MASK_SET (mask, API_DrawingType, isCutWithFrame);
		ACAPI_ELEMENT_MASK_SET (mask, API_DrawingType, poly);
		inner = ACAPI_Element_Change (&el, &mask, &memo, APIMemoMask_Polygon, true);
		ACAPI_DisposeElemMemoHdls (&memo);
		return inner;
	});
	if (inner != NoError) return Err (inner, "Set frame failed");
	return GS::ObjectState ("success", true);
}

// {} -> pens of the active pen table: index, r, g, b (0..1), width (mm)
GS::ObjectState ListPens (const GS::ObjectState&)
{
	GS::ObjectState r;
	auto list = r.AddList<GS::ObjectState> ("pens");
	for (short i = 1; i <= 255; ++i) {
		API_Pen pen {};
		pen.index = i;
		if (ACAPI_Attribute_GetPen (pen) != NoError) continue;
		list (GS::ObjectState ("index", (Int32) i, "r", pen.rgb.f_red, "g", pen.rgb.f_green, "b", pen.rgb.f_blue, "width", pen.width));
	}
	return r;
}

// { items:[{points:[{x,y}...], layer, fill, fgPen, bgPen, contPen, contLineType, contour:bool, floor}] }
GS::ObjectState CreateHatches (const GS::ObjectState& p)
{
	GS::Array<GS::ObjectState> items;
	p.Get ("items", items);
	GS::ObjectState result;
	auto list = result.AddList<GS::UniString> ("created");
	auto fails = result.AddList<Int32> ("errors");
	ACAPI_CallUndoableCommand ("Claude: Create hatches", [&] () -> GSErrCode {
		for (const auto& it : items) {
			GS::Array<GS::ObjectState> pts;
			it.Get ("points", pts);
			if (pts.GetSize () < 3) { fails ((Int32) APIERR_BADPARS); continue; }
			API_Element el {};
			API_ElementMemo memo {};
			el.header.type = API_HatchID;
			if (ACAPI_Element_GetDefaults (&el, &memo) != NoError) { fails ((Int32) -1); continue; }
			ACAPI_DisposeElemMemoHdls (&memo);
			memo = {};
			el.header.floorInd = (short) Num (it, "floor", 0);
			el.header.layer = ACAPI_CreateAttributeIndex ((Int32) Num (it, "layer", 1));
			el.hatch.fillInd = ACAPI_CreateAttributeIndex ((Int32) Num (it, "fill", 1));
			el.hatch.fillPen.penIndex = (short) Num (it, "fgPen", 1);
			el.hatch.fillBGPen = (short) Num (it, "bgPen", 0);
			el.hatch.contPen.penIndex = (short) Num (it, "contPen", 1);
			el.hatch.ltypeInd = ACAPI_CreateAttributeIndex ((Int32) Num (it, "contLineType", 1));
			el.hatch.showArea = false;
			bool contour = true;
			it.Get ("contour", contour);
			Int32 n = (Int32) pts.GetSize ();
			bool closed = fabs (Num (pts[0], "x", 0) - Num (pts[n - 1], "x", 0)) < 1e-9 && fabs (Num (pts[0], "y", 0) - Num (pts[n - 1], "y", 0)) < 1e-9;
			Int32 nc = closed ? n : n + 1;
			el.hatch.poly.nCoords = nc; el.hatch.poly.nSubPolys = 1; el.hatch.poly.nArcs = 0;
			memo.coords = (API_Coord**) BMhAllClear ((nc + 1) * sizeof (API_Coord));
			for (Int32 i = 0; i < n; ++i) (*memo.coords)[i + 1] = { Num (pts[i], "x", 0), Num (pts[i], "y", 0) };
			if (!closed) (*memo.coords)[nc] = (*memo.coords)[1];
			memo.pends = (Int32**) BMhAllClear (2 * sizeof (Int32));
			(*memo.pends)[0] = 0; (*memo.pends)[1] = nc;
			if (!contour) el.hatch.contPen.penIndex = 0;
			GSErrCode e = ACAPI_Element_Create (&el, &memo);
			ACAPI_DisposeElemMemoHdls (&memo);
			if (e == NoError) list (APIGuidToString (el.header.guid)); else fails ((Int32) e);
		}
		return NoError;
	});
	return result;
}

// { elements:[guid], fillPen } -> opaque background on text elements
GS::ObjectState SetTextBackground (const GS::ObjectState& p)
{
	short pen = (short) Num (p, "fillPen", 19);
	Int32 ok = 0, failed = 0;
	ACAPI_CallUndoableCommand ("Claude: Text background", [&] () -> GSErrCode {
		for (const auto& g : GuidList (p, "elements")) {
			API_Element el {};
			el.header.guid = g;
			if (ACAPI_Element_Get (&el) != NoError || el.header.type.typeID != API_TextID) { ++failed; continue; }
			API_Element mask;
			ACAPI_ELEMENT_MASK_CLEAR (mask);
			ACAPI_ELEMENT_MASK_SET (mask, API_TextType, usedFill);
			ACAPI_ELEMENT_MASK_SET (mask, API_TextType, fillPen);
			el.text.usedFill = pen > 0;
			el.text.fillPen = pen;
			if (ACAPI_Element_Change (&el, &mask, nullptr, 0, true) == NoError) ++ok; else ++failed;
		}
		return NoError;
	});
	return GS::ObjectState ("changed", ok, "failed", failed);
}

#include "v2.inc"
#include <vector>
#include <array>
#include "v3.inc"

// {} -> list of available commands
GS::ObjectState Ping (const GS::ObjectState&)
{
	GS::ObjectState r ("addOn", GS::UniString ("ClaudeTools"), "version", GS::UniString ("2.5.3"));
	GS::Array<GS::UniString> cmds = { "Ping", "CreateSolidLinks", "RemoveSolidLinks", "GetSolidLinks",
		"TrimElements", "RemoveTrim", "GetTrims", "FlipOpenings", "Rebuild", "SetWallTextureAlign",
		"CreateTexts", "CreateDimensions", "SetLayerInCombinations", "MoveToLayer", "DeleteElements", "FarElements", "GetDrawing", "SetDrawingFrame", "ListPens", "CreateHatches", "SetTextBackground", "ZoomToElements", "SelectElements", "ShowIn3D", "SetElementProps", "GetPolygon", "SetMeshPointHeights", "TransformElements", "CreateLines", "CreateObject", "SetGDLParams", "Get3DCamera", "Set3DCamera", "SetDrawingProps", "SaveProject", "GetElementInfo", "SetDrawOrder", "FindElements", "CopyElementSettings", "GetRoof", "SetRoof", "SetPolygon", "SaveProjectAs", "RenderImage", "ListDatabases", "GetDimensions", "GetDrawnSegments", "SetDimensionNotes", "SetDimensionWitness", "RebuildDimension", "GetStoryVisibility", "SetStoryVisibility" };
	r.Add ("commands", cmds);
	return r;
}

} // namespace

// =============================================================================

API_AddonType CheckEnvironment (API_EnvirParams* envir)
{
	RSGetIndString (&envir->addOnInfo.name,        CT_ADDON_NAME, 1, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, CT_ADDON_NAME, 2, ACAPI_GetOwnResModule ());
	return APIAddon_Preload;
}

GSErrCode RegisterInterface (void)
{
	return NoError;
}

GSErrCode Initialize (void)
{
	struct { const char* n; Handler h; } cmds[] = {
		{ "Ping", Ping },
		{ "CreateSolidLinks", CreateSolidLinks },
		{ "RemoveSolidLinks", RemoveSolidLinks },
		{ "GetSolidLinks", GetSolidLinks },
		{ "TrimElements", TrimElements },
		{ "RemoveTrim", RemoveTrim },
		{ "GetTrims", GetTrims },
		{ "FlipOpenings", FlipOpenings },
		{ "Rebuild", Rebuild },
		{ "SetWallTextureAlign", SetWallTextureAlign },
		{ "CreateTexts", CreateTexts },
		{ "CreateDimensions", CreateDimensions },
		{ "SetLayerInCombinations", SetLayerInCombinations },
		{ "MoveToLayer", MoveToLayer },
		{ "DeleteElements", DeleteElements },
		{ "FarElements", FarElements },
		{ "GetDrawing", GetDrawing },
		{ "SetDrawingFrame", SetDrawingFrame },
		{ "ListPens", ListPens },
		{ "CreateHatches", CreateHatches },
		{ "SetTextBackground", SetTextBackground },
		{ "ZoomToElements", ZoomToElements },
		{ "SelectElements", SelectElements },
		{ "ShowIn3D", ShowIn3D },
		{ "SetElementProps", SetElementProps },
		{ "GetPolygon", GetPolygon },
		{ "SetMeshPointHeights", SetMeshPointHeights },
		{ "TransformElements", TransformElements },
		{ "CreateLines", CreateLines },
		{ "CreateObject", CreateObject },
		{ "SetGDLParams", SetGDLParams },
		{ "Get3DCamera", Get3DCamera },
		{ "Set3DCamera", Set3DCamera },
		{ "SetDrawingProps", SetDrawingProps },
		{ "SaveProject", SaveProject },
		{ "GetElementInfo", GetElementInfo },
		{ "SetDrawOrder", SetDrawOrder },
		{ "FindElements", FindElements },
		{ "CopyElementSettings", CopyElementSettings },
		{ "GetRoof", GetRoof },
		{ "SetRoof", SetRoof },
		{ "SetPolygon", SetPolygon },
		{ "SaveProjectAs", SaveProjectAs },
		{ "RenderImage", RenderImage },
		{ "ListDatabases", v3::ListDatabases },
		{ "GetDimensions", v3::GetDimensions },
		{ "GetDrawnSegments", v3::GetDrawnSegments },
		{ "SetDimensionNotes", v3::SetDimensionNotes },
		{ "SetDimensionWitness", v3::SetDimensionWitness },
		{ "RebuildDimension", v3::RebuildDimension },
		{ "GetStoryVisibility", v3::GetStoryVisibility },
		{ "SetStoryVisibility", v3::SetStoryVisibility },
	};
	GSErrCode err = NoError;
	for (auto& c : cmds) {
		GSErrCode e = ACAPI_AddOnAddOnCommunication_InstallAddOnCommandHandler (GS::NewOwned<GenericCommand> (c.n, c.h));
		if (e != NoError) err = e;
	}
	return err;
}

GSErrCode FreeData (void)
{
	return NoError;
}
