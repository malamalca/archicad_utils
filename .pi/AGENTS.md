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

### 3D Scene Mesh Extraction (Legacy API)
`ModelerAPI::Model` crashes in menu handler context. Use **Legacy ModelAccess**:
```cpp
#include "APIdefs_3D.h"   // API_Component3D, API_BodyType, API_PgonType, API_VertType

DBSetCurrentWind(APIWind_FloorPlanID);  // CRITICAL: switch to FloorPlan first!
API_Network network = {};
ACAPI_Network(&network);
Int32 bodyCount;
ACAPI_Database_Count(API_DBID_3DBodyID, &bodyCount);
for (Int32 i = 1; i <= bodyCount; ++i) {  // 1-based!
    API_Component3D* comp = nullptr;
    ACAPI_ModelAccess_GetComponent(APIWind_FloorPlanID, i, &comp);
    if (comp->head.type == API_BodyType && comp->head.pgon != nullptr) {
        // iterate comp->head.pgon → fan-triangulate
    }
}
```

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

## Known Issues
- **PDB lock**: Close Archicad before rebuilding (LNK1201)
- **Debug PDB warning** (`API_c.pdb not found`): harmless, ignore
- **Empty scene mesh**: treat as fully exposed (no occluders = full sun)

## Debugging
```cpp
DBPrintf("=== STEP X: err=%d ===\n", err);  // → Archicad debug log / VS Output
```
