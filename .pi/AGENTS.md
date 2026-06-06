# arUtils — Archicad Add-On (AC29)

**DevKit**: `D:\ArchicadDev\GRAPHISOFT\API Development Kit 29.3100`  
**CMake**: `"D:/Dev/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build out/build/debug29 --config Debug`  
**Output**: `out/build/debug29/Debug/arUtils.apx`

## AC29 Critical Patterns

### Geo Location
```cpp
ACAPI_GeoLocation_GetGeoLocation(&geo);           // not ACAPI_Environment(APIEnv_GetGeoLocationID)
ACAPI_GeoLocation_GetPlaceSets(&placeInfo);        // not APIEnv_GetProjectLocationID
ACAPI_ProjectSetting_GetStorySettings(&info, 0);   // not APIEnv_GetStorySettingsID
// DO NOT BMKillHandle storyInfo->data — it crashes in AC29
```

### 3D Scene Mesh Extraction (ModelerAPI)
**CRITICAL: ModelerAPI uses 1-based indexing** — all loops `for (i = 1; i <= count)`.  
In menu context, must bring 3D window to front and rebuild first:
```cpp
#include "APIdefs_3D.h"       // API_BodyID (Legacy fallback)
#include "ACAPI_Automate.h"   // ACAPI_View_Rebuild, ACAPI_Window_ChangeWindow
#include "Model.hpp"          // ModelerAPI::Model
#include "ModelElement.hpp"   // ModelerAPI::Element
#include "ModelMeshBody.hpp"  // ModelerAPI::MeshBody
#include "Polygon.hpp"        // ModelerAPI::Polygon
#include "Vertex.hpp"         // ModelerAPI::Vertex

// ── Step 1: Switch to 3D database & bring window to front ──
API_WindowInfo savedDB = {};
ACAPI_Database_GetCurrentDatabase(&savedDB);

API_DatabaseInfo planDB = {}; BNZeroMemory(&planDB, sizeof(planDB));
planDB.typeID = APIWind_3DModelID;
ACAPI_Database_ChangeCurrentDatabase(&planDB);

API_WindowInfo windowInfo = {}; BNZeroMemory(&windowInfo, sizeof(windowInfo));
windowInfo.typeID = APIWind_3DModelID;
windowInfo.databaseUnId = planDB.databaseUnId;
ACAPI_Window_ChangeWindow(&windowInfo);  // ← brings 3D window to front

bool doRebuild = true;
ACAPI_View_Rebuild(&doRebuild);  // regenerate 3D

void* origSight = nullptr;
ACAPI_Sight_SelectSight(nullptr, &origSight);  // use 3D window's sight

// ── Step 2: Extract mesh (1-based!) ──
ModelerAPI::Model model;
ACAPI_Sight_GetSelectedSightModel(model);
for (Int32 ei = 1; ei <= model.GetElementCount(); ++ei) {
    ModelerAPI::Element element;
    model.GetElement(ei, &element);  // output pointer
    for (Int32 bi = 1; bi <= element.GetMeshBodyCount(); ++bi) {
        ModelerAPI::MeshBody body;
        element.GetMeshBody(bi, &body);
        for (Int32 pi = 1; pi <= body.GetPolygonCount(); ++pi) {
            ModelerAPI::Polygon poly;
            body.GetPolygon(pi, &poly);
            Int32 vc = poly.GetEdgeCount();
            if (vc < 3) continue;
            // fan-triangulate (1-based vertex indices):
            ModelerAPI::Vertex v0;
            body.GetVertex(poly.GetVertexIndex(1), &v0);
            for (Int32 vi = 2; vi < vc; ++vi) {
                ModelerAPI::Vertex va, vb;
                body.GetVertex(poly.GetVertexIndex(vi), &va);
                body.GetVertex(poly.GetVertexIndex(vi+1), &vb);
                // → triangle (v0, va, vb)
            }
        }
    }
}

// ── Step 3: Restore database ──
ACAPI_Database_ChangeCurrentDatabase(&savedDB);
```
All `GetXxx(idx, &out)` take **output pointer**, not return by value. Indices are **1-based**.

### Parameter Writing (AC29) — Element_Change with cleared mask
```cpp
// Get element + memo
API_Element elem = {}; elem.header.guid = guid;
ACAPI_Element_Get(&elem, 0);

API_ElementMemo memo = {};
ACAPI_Element_GetMemo(guid, &memo, APIMemoMask_AddPars);

// Modify params in memo.params
if (memo.params != nullptr && *memo.params != nullptr) {
    UInt32 n = BMGetHandleSize((GSConstHandle)memo.params) / sizeof(API_AddParType);
    for (UInt32 i = 0; i < n; ++i) {
        API_AddParType& p = (*memo.params)[static_cast<Int32>(i)];
        if (strncmp(p.name, "paramName", API_NameLen) == 0) {
            // resize array
            p.dim1 = newSize;
            p.value.array = BMReallocHandle(p.value.array,
                newSize * sizeof(double), REALLOC_FULLCLEAR, 0);
            // write values into *p.value.array
        }
    }
}

// Persist: cleared mask = no element fields changed, only params
API_Element mask = {}; BNZeroMemory(&mask, sizeof(mask));
ACAPI_Element_Change(&elem, &mask, &memo, APIMemoMask_AddPars, true);  // withDel=true is standard
ACAPI_DisposeElemMemoHdls(&memo);
```

### Element Type: `API_ObjectID` (not `APIProjectObject`)

## Menu Structure
```
arUtils > Polygon Tools > [1] Realign Polygon, [2] Realign Polygon Node
arUtils > Element Tools > [1] Refresh, [2] Export Morph Faces, [3] Sync PHPures, [4] Calculate Insolation
```

## Solar Calculation (Src/SolarAnalysis.cpp)

### Algorithm: Jean Meeus (year 2000 epoch)
Both `getDaylightWindow()` and `solarPosition()` use **same Jean Meeus algorithm** for EoT/declination — mixing algorithms causes ~5hr time shift.
- `getDaylightWindow`: computes sunrise/sunset in local hours via solar noon ± half-day
- `solarPosition`: computes HRA from `localHour - solarNoon` (both in local hours)
- **Zenith = 90.833°** (refraction + solar disk radius). GDL arSunChart uses 90° → ~5min discrepancy
- **Analysis dates**: `{0,21,20,20,20,21,21,21,21,21,21,21,21}` — match GDL arSunChart (Feb/Mar/Apr = 20th)

### Sun Direction (Archicad coords: Z-up, Y-North, X-East)
```cpp
// Azimuth: clockwise from North (0°=N, 90°=E, 180°=S, 270°=W)
Vec3 dir{ cos(alt)*sin(az), cos(alt)*cos(az), sin(alt) };
```
Azimuth computed via **atan2** (not acos) for robustness at noon.

### Raycast Epsilon
`t > 1e-3` (1mm min distance) avoids self-intersection with floor slab. Sensor uses actual element position (no artificial Z offset).

### Parameter Output
12-month array written to `solarization_01`..`solarization_12` (minutes of direct sun per month). Element mask **must be zeroed** before `Element_Change`.

## Known Issues
- **PDB lock**: Close Archicad before rebuilding (LNK1201)
- **Debug PDB warning** (`API_c.pdb not found`): harmless, ignore
- **Empty scene mesh**: treat as fully exposed (no occluders = full sun)
- **ModelerAPI indexing**: All loops are **1-based** (`i=1; i<=count`), unlike C++ standard 0-based vectors. Legacy ModelAccess API also uses 1-based.
- **3D in menu context**: Must switch to `APIWind_3DModelID`, call `Window_ChangeWindow`, then `View_Rebuild(true)` before accessing 3D bodies.
- **Scene mesh resolution**: Only ~24 triangles for entire model. Block geometry may be oversimplified → shadow boundaries inaccurate.

## Debugging
```cpp
DBPrintf("=== STEP X: err=%d ===\n", err);  // → Archicad debug log / VS Output
```
