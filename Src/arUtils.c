// cmake.exe ..\curl-8.3.0  -DBUILD_STATIC_LIBS=ON -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_USE_OPENSSL=ON -DCURL_USE_OPENSSL=ON -DOPENSSL_ROOT_DIR=..\openssl-1.1.1d -DOPENSSL_INCLUDE_DIR=..\openssl-1.1.1d\include
// cmake.exe --build . --config Release
// ---------------------------------- Includes ---------------------------------


#ifdef DEBUG
#define POGO_NOHANDLERS
#endif

#include	"APIEnvir.h"
#include	"ACAPinc.h"					// also includes APIdefs.h
#include	"ACAPI_Goodies.h"
#include	"ACAPI_Environment.h"
#include	"DG.h"
#include	"ResourceIds.hpp"
#include	"APICommon.h"
#include	"arExportMorphFaces.h"
#include	"arPHPures.h"
#include	"SolarAnalysis.hpp"

const double ALIGN_PRECISION = 1000;

// ta funkcija vzame izrani GDL Objekt, poišče vse ostale iste gdl objekte in jih osveži
void arRefreshElements()
{
	GSErrCode						err;
	API_SelectionInfo				selectionInfo;
	GS::Array<API_Neig>				selNeigs;
	API_Element 					element, elementFilter;
	GS::Array<API_Guid>				elemList;

	GS::Array<API_DatabaseTypeID>   databaseTypes = {APIWind_FloorPlanID, APIWind_SectionID, APIWind_DetailID, APIWind_LayoutID, APIWind_DrawingID, APIWind_ElevationID, APIWind_InteriorElevationID, APIWind_WorksheetID};
	//GS::Array<API_DatabaseTypeID>   databaseTypes = {APIWind_AllWithoutMovieRenderingID};

	API_DatabaseInfo	currentDbInfo {};
	API_DatabaseInfo	prevDbInfo {};
	err = ACAPI_Database_GetCurrentDatabase (&prevDbInfo);
	
	err = ACAPI_Selection_Get(&selectionInfo, &selNeigs, true);
	BMKillHandle((GSHandle*)&selectionInfo.marquee.coords);
	if (err == APIERR_NOSEL) {
		return;
	}

	if (selectionInfo.typeID != API_SelEmpty) {
		GS::Array<API_DatabaseUnId> databases = ACAPI_Database_GetDatabasesForTypes(databaseTypes);

		for (const API_Neig& neig : selNeigs) {		
			BNZeroMemory(&element, sizeof(API_Element));
			element.header.guid = neig.guid;
			if (ACAPI_Element_Get(&element) == NoError && element.header.type.typeID == API_ObjectID) {
				// do on current database
				// fetch all objects for current database
				elemList.Clear();
				ACAPI_Element_GetElemList(API_ObjectID, &elemList);

				for (GS::Array<API_Guid>::ConstIterator it = elemList.Enumerate(); it != NULL; ++it) {
					BNZeroMemory(&elementFilter, sizeof(API_Element));
					elementFilter.header.guid = *it;
					err = ACAPI_Element_Get(&elementFilter);
					if (err == NoError && elementFilter.object.libInd == element.object.libInd) {
						bool blnForce = true;
						err = ACAPI_Element_RefreshElement(&elementFilter.header, &blnForce);

						if (err != NoError) {
							DGAlert(DG_ERROR, "Refresh Element", "An Error Occured", GS::UniString (), "OK");
						}
					}
				}

				// iterate through every database
				for (GS::Array<API_DatabaseUnId>::ConstIterator db = databases.Enumerate(); db != NULL; ++db) {
					BNZeroMemory(&currentDbInfo, sizeof(API_DatabaseInfo));
					currentDbInfo.databaseUnId = *db;

					err = ACAPI_Window_GetDatabaseInfo(&currentDbInfo, nullptr);
					if (err == NoError) {
						err = ACAPI_Database_ChangeCurrentDatabase(&currentDbInfo);
						if (err == NoError) {
							// fetch all objects for current database
							elemList.Clear();
							ACAPI_Element_GetElemList(API_ObjectID, &elemList);

							for (GS::Array<API_Guid>::ConstIterator it = elemList.Enumerate(); it != NULL; ++it) {
								BNZeroMemory(&elementFilter, sizeof(API_Element));
								elementFilter.header.guid = *it;
								err = ACAPI_Element_Get(&elementFilter);
								if (err == NoError && elementFilter.object.libInd == element.object.libInd) {
									bool blnForce = true;
									err = ACAPI_Element_RefreshElement(&elementFilter.header, &blnForce);

									if (err != NoError) {
										DGAlert(DG_ERROR, "Refresh Element", "An Error Occured", GS::UniString (), "OK");
									}
								}
							}
						}
					}
				}
			}
		}

		// switch back
		ACAPI_Database_ChangeCurrentDatabase (&prevDbInfo);
	}
}

void arRealignPolygonNode()
{
	API_ElementMemo		memo;
	API_Neig			neig;
	API_ElemType		type;
	API_Coord3D			begC;
	Int32				nSubPolys, i, begInd, endInd;
	GSErrCode			err = NoError;

	if (!ClickAnElem("Click a polygon node to realign", API_ZombieElemID, &neig, &type, nullptr, &begC)) {
		DGAlert(DG_ERROR, "Realign Node", "No Node Was Clicked", GS::UniString (), "OK");
		return;
	}

	switch (neig.neigID) {
		case APINeig_Ceil:
		case APINeig_Roof:
		case APINeig_Mesh:
		case APINeig_Room:
		case APINeig_Hatch:
		case APINeig_PolyLine:
		case APINeig_DetailPoly:
		case APINeig_WorksheetPoly:
		case APINeig_DrawingFrame:
		case APINeig_InteriorElevation:
			break;

		default:
			DGAlert(DG_ERROR, "Realign Node", "Only polygon nodes are accepted", GS::UniString (), "OK");
			return;
	}

	err = ACAPI_Element_GetMemo(neig.guid, &memo);
	if (err == NoError) {
		double roundedX = std::round((float)(*memo.coords)[neig.inIndex].x * ALIGN_PRECISION) / ALIGN_PRECISION;
		double roundedY = std::round((float)(*memo.coords)[neig.inIndex].y * ALIGN_PRECISION) / ALIGN_PRECISION;

		if (abs(roundedX - (*memo.coords)[neig.inIndex].x) < 1/ALIGN_PRECISION) {
			(*memo.coords)[neig.inIndex].x = roundedX;
		}
		if (abs(roundedY - (*memo.coords)[neig.inIndex].y) < 1/ALIGN_PRECISION) {
			(*memo.coords)[neig.inIndex].y = roundedY;
		}
		
		nSubPolys = BMGetHandleSize ((GSHandle) memo.pends) / sizeof (Int32) - 1;
		for (i = 1; i <= nSubPolys; i++) {
			begInd = (*memo.pends)[i - 1] + 1;
			endInd = (*memo.pends)[i];
			if (neig.inIndex == begInd)
				(*memo.coords)[endInd] = (*memo.coords)[neig.inIndex];
			else if (neig.inIndex == endInd)
				(*memo.coords)[begInd] = (*memo.coords)[neig.inIndex];
		}
		
		API_ElementMemo tmpMemo = {};
		tmpMemo.coords = memo.coords;
		tmpMemo.pends = memo.pends;
		tmpMemo.parcs = memo.parcs;
		tmpMemo.vertexIDs = memo.vertexIDs;
		tmpMemo.edgeIDs = memo.edgeIDs;
		tmpMemo.edgeTrims = memo.edgeTrims;
		tmpMemo.contourIDs = memo.contourIDs;
		tmpMemo.meshPolyZ = memo.meshPolyZ;
		err = ACAPI_CallUndoableCommand ("Element Test API Function", [&neig, &tmpMemo] () -> GSErrCode {
			return ACAPI_Element_ChangeMemo (neig.guid, APIMemoMask_All, &tmpMemo);
		});
		
		if (err != NoError)
			ErrorBeep ("ACAPI_Element_ChangeMemo", err);
	}

	ACAPI_DisposeElemMemoHdls (&memo);
}

void arRealignPolygon()
{
	GSErrCode						err;
	API_SelectionInfo				selectionInfo;
	GS::Array<API_Neig>				selNeigs;
	API_Element 					element;
	API_ElementMemo					memo;
	
	err = ACAPI_Selection_Get(&selectionInfo, &selNeigs, true);
	BMKillHandle((GSHandle*)&selectionInfo.marquee.coords);

	BNZeroMemory(&element, sizeof(API_Element));

	if (err == APIERR_NOSEL) {
		err = NoError;

		return;
	}

	if (selectionInfo.typeID != API_SelEmpty) {
		for (const API_Neig& neig : selNeigs) {		
			element.header.guid = neig.guid;
			if (ACAPI_Element_Get(&element) == NoError) {
				err = ACAPI_Element_GetMemo (neig.guid, &memo);
				if (err == NoError) {
					int nCoords = BMGetHandleSize ((GSHandle) memo.coords) / sizeof (API_Coord) - 1;
					for (int i = 1; i < nCoords; i++) {
						double roundedX = std::round((float)(*memo.coords)[i].x * ALIGN_PRECISION) / ALIGN_PRECISION;
						double roundedY = std::round((float)(*memo.coords)[i].y * ALIGN_PRECISION) / ALIGN_PRECISION;

						if (abs(roundedX - (*memo.coords)[i].x) < 1/ALIGN_PRECISION) {
							(*memo.coords)[i].x = roundedX;
						}
						if (abs(roundedY - (*memo.coords)[i].y) < 1/ALIGN_PRECISION) {
							(*memo.coords)[i].y = roundedY;
						}
					

						int nSubPolys = BMGetHandleSize ((GSHandle) memo.pends) / sizeof (Int32) - 1;
						int begInd;
						int endInd;
						for (int j = 1; j <= nSubPolys; j++) {
							begInd = (*memo.pends)[j - 1] + 1;
							endInd = (*memo.pends)[j];
							if (i == begInd)
								(*memo.coords)[endInd] = (*memo.coords)[i];
							else if (i == endInd)
								(*memo.coords)[begInd] = (*memo.coords)[i];
						}
					}
				}
				if (err == NoError) {
					API_ElementMemo tmpMemo = {};
					tmpMemo.coords = memo.coords;
					tmpMemo.pends = memo.pends;
					tmpMemo.parcs = memo.parcs;
					tmpMemo.vertexIDs = memo.vertexIDs;
					tmpMemo.edgeIDs = memo.edgeIDs;
					tmpMemo.edgeTrims = memo.edgeTrims;
					tmpMemo.contourIDs = memo.contourIDs;
					tmpMemo.meshPolyZ = memo.meshPolyZ;

					err = ACAPI_CallUndoableCommand ("Element Test API Function", [&element, &tmpMemo] () -> GSErrCode {
						return ACAPI_Element_ChangeMemo (element.header.guid, APIMemoMask_All, &tmpMemo);
						//return ACAPI_Element_Change(element, 0, tmpMemo, APIMemoMask_All, true);
					});

					if (err != NoError) {
						DGAlert(DG_ERROR, "Realign Polygon", GS::UniString::Printf("Error %d", err), GS::UniString (), "OK");
					} else {
						DGAlert(DG_INFORMATION, "Realign Polygon", "Sucessfuly Modified Polygon", GS::UniString (), "OK");
					}
				}

				ACAPI_DisposeElemMemoHdls (&memo);
			}
		}
	}
}

// -----------------------------------------------------------------------------
// Handles menu commands
// -----------------------------------------------------------------------------
GSErrCode MenuCommandHandler (const API_MenuParams *menuParams)
{
	switch (menuParams->menuItemRef.menuResID) {
		case ID_MENU_MAIN:
			switch (menuParams->menuItemRef.itemIndex) {
				case 1:
					arRealignPolygon();
					break;
				case 2:
					arRealignPolygonNode();
					break;
			}
			break;
		case ID_MENU_MAIN2:
			switch (menuParams->menuItemRef.itemIndex) {
				case 1:
					arRefreshElements();
					break;
				case 2:
					ExportMorphFaces();
					break;
				case 3:
					SyncPhpuresZones();
					break;
				case 4:
				{
					// Fetch lat/lon from project Geo Location settings (AC29 API)
					API_GeoLocation geoLocation = {};
					GSErrCode geoErr = ACAPI_GeoLocation_GetGeoLocation(&geoLocation);
					if (geoErr != NoError) {
						DGAlert(DG_WARNING, "Solar Analysis",
							"Could not read project geolocation.\nPlease set it in Project > Location.",
							"", "OK");
						break;
					}

					RunSolarAnalysis(
						"SolarSensor 26",           // libpart name to search for
						geoLocation.placeInfo.latitude,   // already in degrees
						geoLocation.placeInfo.longitude,
						1.0                         // UTC+1 (CET)
					);
					break;
				}
			}
			break;
	}

	return NoError;
} // MenuCommandHandler


// =============================================================================
//
// Required functions
//
// =============================================================================

// -----------------------------------------------------------------------------
// Dependency definitions
// -----------------------------------------------------------------------------
API_AddonType CheckEnvironment (API_EnvirParams* envir)
{
	RSGetIndString (&envir->addOnInfo.name, ID_ADDON_INFO, 1, ACAPI_GetOwnResModule());
	RSGetIndString (&envir->addOnInfo.description, ID_ADDON_INFO, 2, ACAPI_GetOwnResModule());

	return APIAddon_Normal;
} // CheckEnvironment


// -----------------------------------------------------------------------------
// Interface definitions
// -----------------------------------------------------------------------------
GSErrCode RegisterInterface (void)
{
	GSErrCode err;

	err = ACAPI_MenuItem_RegisterMenu (ID_MENU_MAIN, ID_MENU_PROMPT_MAIN, MenuCode_UserDef, MenuFlag_Default);
	if (DBERROR (err != NoError))
		return err;

	err = ACAPI_MenuItem_RegisterMenu (ID_MENU_MAIN2, ID_MENU_PROMPT_MAIN2, MenuCode_UserDef, MenuFlag_Default);
	if (DBERROR (err != NoError))
		return err;
	return NoError;
} // RegisterInterface


// -----------------------------------------------------------------------------
// Called when the Add-On has been loaded into memory
// to perform an operation
// -----------------------------------------------------------------------------
GSErrCode Initialize(void)
{
	GSErrCode err;
	err = ACAPI_MenuItem_InstallMenuHandler(ID_MENU_MAIN, MenuCommandHandler);
	err = ACAPI_MenuItem_InstallMenuHandler(ID_MENU_MAIN2, MenuCommandHandler);
	if (DBERROR (err != NoError))
		return err;
	return NoError;
} // Initialize


// -----------------------------------------------------------------------------
// FreeData
//		called when the Add-On is going to be unloaded
// -----------------------------------------------------------------------------
GSErrCode FreeData(void)
{
	return NoError;
} // FreeData
