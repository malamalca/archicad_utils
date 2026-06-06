// SolarAnalysis.cpp
// ─────────────────
// Solar-exposure analysis library for Archicad Add-Ons (AC29).
//
// Pipeline: find elements by libpart name → extract 3D location →
// raycast against scene mesh → write solarization[12] parameter.
//
// Requires: ArchiCAD API 29+, C++20

#define _USE_MATH_DEFINES

#include "APIEnvir.h"
#include "ACAPinc.h"
#include "API_Guid.hpp"
#include "ACAPI_Environment.h"
#include "ACAPI_Goodies.h"
#include "APIdefs_3D.h"   // Legacy ModelAccess: API_Component3D, API_BodyID, etc.
#include "ACAPI_Automate.h"  // ACAPI_View_Rebuild
#include "Model.hpp"
#include "ModelElement.hpp"
#include "ModelMeshBody.hpp"
#include "Polygon.hpp"
#include "Vertex.hpp"
#include "Vertex.hpp"
#include "DG.h"

#include "SolarAnalysis.hpp"

#include <cmath>
#include <vector>
#include <array>
#include <string>
#include <optional>
#include <limits>
#include <algorithm>
#include <cstring>

// ═════════════════════════════════════════════════════════════════════════════
// 1. MATH PRIMITIVES
// ═════════════════════════════════════════════════════════════════════════════

static constexpr double DEG_TO_RAD = M_PI / 180.0;
static constexpr double RAD_TO_DEG = 180.0 / M_PI;

struct Vec3 {
    double x, y, z;
    Vec3 operator+(const Vec3& b) const { return {x+b.x, y+b.y, z+b.z}; }
    Vec3 operator-(const Vec3& b) const { return {x-b.x, y-b.y, z-b.z}; }
    Vec3 operator*(double t)      const { return {x*t,   y*t,   z*t};   }
    double dot  (const Vec3& b)   const { return x*b.x + y*b.y + z*b.z; }
    Vec3   cross(const Vec3& b)   const {
        return { y*b.z - z*b.y, z*b.x - x*b.z, x*b.y - y*b.x };
    }
    double length()    const { return std::sqrt(dot(*this)); }
    Vec3 normalized()  const { double l = length(); return (l > 1e-12) ? *this * (1.0/l) : Vec3{0,0,1}; }
};

struct Ray      { Vec3 origin, direction; };
struct Triangle { Vec3 v0, v1, v2; };
struct Mesh     { std::vector<Triangle> triangles; };

// ═════════════════════════════════════════════════════════════════════════════
// 2. MÖLLER–TRUMBORE RAY-TRIANGLE INTERSECTION
// ═════════════════════════════════════════════════════════════════════════════

static bool rayHitsTriangle(const Ray& ray, const Triangle& tri, double tMax)
{
    constexpr double EPS = 1e-9;
    Vec3 e1 = tri.v1 - tri.v0;
    Vec3 e2 = tri.v2 - tri.v0;
    Vec3 h  = ray.direction.cross(e2);
    double det = e1.dot(h);
    if (std::fabs(det) < EPS) return false;

    double inv = 1.0 / det;
    Vec3   tv  = ray.origin - tri.v0;
    double u   = tv.dot(h) * inv;
    if (u < 0.0 || u > 1.0) return false;

    Vec3   qv = tv.cross(e1);
    double v  = ray.direction.dot(qv) * inv;
    if (v < 0.0 || u + v > 1.0) return false;

    double t = e2.dot(qv) * inv;
    return (t > 1e-3 && t < tMax);
}

static bool isOccluded(const Vec3& point, const Vec3& sunDir, const Mesh& scene)
{
    Ray shadow{ point, sunDir };
    for (const auto& tri : scene.triangles)
        if (rayHitsTriangle(shadow, tri, 1e9))
            return true;
    return false;
}

// ═════════════════════════════════════════════════════════════════════════════
// 3. SOLAR POSITION  (Spencer / Michalsky simplified algorithm)
// ═════════════════════════════════════════════════════════════════════════════

struct SunPos { double altitude; double azimuth; };

static std::optional<SunPos> solarPosition(
    double latDeg, double lonDeg,
    int month, int day, double localHour, double utcOffsetHours)
{
    // Jean Meeus algorithm (same as getDaylightWindow for consistency)
    int Y = 2000, M = month, D = day;
    if (M <= 2) { Y--; M += 12; }
    double A  = std::floor(Y / 100.0);
    double Bm = 2.0 - A + std::floor(A / 4.0);
    double JD = std::floor(365.25*(Y+4716)) + std::floor(30.6001*(M+1)) + D + Bm - 1524.5;
    double T  = (JD - 2451545.0) / 36525.0;

    double L0 = std::fmod(280.46646 + T*(36000.76983 + T*0.0003032), 360.0);
    double M0 = std::fmod(357.52911 + T*(35999.05029 - T*0.0001537), 360.0);
    double Mrad = M0 * DEG_TO_RAD;
    double C = std::sin(Mrad)*(1.914602-T*(0.004817+0.000014*T))
              + std::sin(2*Mrad)*(0.019993-T*0.000101)
              + std::sin(3*Mrad)*0.000289;
    double sunLon = L0 + C;
    double omega  = 125.04 - 1934.136*T;
    double lambda = sunLon - 0.00569 - 0.00478*std::sin(omega*DEG_TO_RAD);
    double obliq  = (23.0+26.0/60.0+21.448/3600.0)
                   - T*(46.8150/3600.0+T*(0.00059/3600.0-T*0.001813/3600.0))
                   + 0.00256*std::cos(omega*DEG_TO_RAD);
    double decl = std::asin(std::sin(obliq*DEG_TO_RAD)*std::sin(lambda*DEG_TO_RAD));

    // EoT (same as getDaylightWindow)
    double y = std::tan(obliq*DEG_TO_RAD/2.0); y *= y;
    double EoT = 4.0*RAD_TO_DEG*(y*std::sin(2*L0*DEG_TO_RAD)
                  - 2.0*std::sin(Mrad)
                  + 4.0*y*std::sin(Mrad)*std::cos(2*L0*DEG_TO_RAD)
                  - 0.5*y*y*std::sin(4*L0*DEG_TO_RAD)
                  - 1.25*std::sin(2*Mrad));

    // Solar noon in local hours (same formula as getDaylightWindow)
    double solarNoon = (720.0 - 4.0*lonDeg - EoT)/60.0 + utcOffsetHours;
    double HRA = (localHour - solarNoon)*15.0*DEG_TO_RAD;

    double lat  = latDeg * DEG_TO_RAD;
    double sinAlt = std::sin(lat)*std::sin(decl)
                  + std::cos(lat)*std::cos(decl)*std::cos(HRA);
    double altRad = std::asin(sinAlt);
    if (altRad <= 0.0) return std::nullopt;

    // Robust azimuth via atan2 (clockwise from north)
    double azNum = std::sin(-HRA);
    double azDen = std::cos(lat)*std::tan(decl) - std::sin(lat)*std::cos(HRA);
    double az = std::atan2(azNum, azDen) * RAD_TO_DEG;
    if (az < 0.0) az += 360.0;

    return SunPos{ altRad * RAD_TO_DEG, az };
}

static Vec3 sunDirectionFromPos(const SunPos& sp)
{
    double altR = sp.altitude * DEG_TO_RAD;
    double azR  = sp.azimuth  * DEG_TO_RAD;
    return Vec3{
         std::cos(altR) * std::sin(azR),
         std::cos(altR) * std::cos(azR),
         std::sin(altR)
    }.normalized();
}

// ═════════════════════════════════════════════════════════════════════════════
// 4. 3D SCENE EXTRACTION (AC29 ModelAccess API pattern)
// ═════════════════════════════════════════════════════════════════════════════

static Mesh buildSceneMesh()
{
    Mesh mesh;

    // Save current database
    API_DatabaseInfo savedDB = {};
    BNZeroMemory(&savedDB, sizeof(savedDB));
    GSErrCode err = ACAPI_Database_GetCurrentDatabase(&savedDB);

    // Switch to 3D Model database if needed
    bool needsRestore = (savedDB.typeID != APIWind_3DModelID);
    if (needsRestore) {
        API_DatabaseInfo db3D = {};
        BNZeroMemory(&db3D, sizeof(db3D));
        db3D.typeID = APIWind_3DModelID;
        err = ACAPI_Database_ChangeCurrentDatabase(&db3D);
    }

    // Select sight (nullptr = use current 3D window's sight)
    void* origSight = nullptr;
    err = ACAPI_Sight_SelectSight(nullptr, &origSight);

    // Check body count
    { Int32 bc = 0; ACAPI_ModelAccess_GetNum(API_BodyID, &bc);
      DBPrintf("    buildSceneMesh: bodyCount=%d\n", bc); }

    // Try ModelerAPI::Model (works when 3D window is open)
    {
        ModelerAPI::Model model;
        GSErrCode err = ACAPI_Sight_GetSelectedSightModel(model);
        if (err == NoError && model.GetElementCount() > 0) {
            DBPrintf("    buildSceneMesh: ModelerAPI element count=%d\n", model.GetElementCount());
            Int32 triCount = 0;
            Int32 elemCount = model.GetElementCount();
            for (Int32 ei = 1; ei <= elemCount; ++ei) {  // 1-based!
                ModelerAPI::Element element;
                model.GetElement(ei, &element);
                Int32 mbc = element.GetMeshBodyCount();
                for (Int32 bi = 1; bi <= mbc; ++bi) {  // 1-based!
                    ModelerAPI::MeshBody body;
                    element.GetMeshBody(bi, &body);
                    Int32 ptc = body.GetPolygonCount();
                    for (Int32 pi = 1; pi <= ptc; ++pi) {  // 1-based!
                        ModelerAPI::Polygon poly;
                        body.GetPolygon(pi, &poly);
                        Int32 vc = poly.GetEdgeCount();
                        if (vc < 3) continue;
                        ModelerAPI::Vertex v0;
                        body.GetVertex(poly.GetVertexIndex(1), &v0);  // 1-based!
                        for (Int32 vi = 2; vi < vc; ++vi) {  // 1-based! fan from vertex 1
                            ModelerAPI::Vertex va, vb;
                            body.GetVertex(poly.GetVertexIndex(vi), &va);
                            body.GetVertex(poly.GetVertexIndex(vi + 1), &vb);
                            mesh.triangles.push_back({
                                { v0.x, v0.y, v0.z },
                                { va.x, va.y, va.z },
                                { vb.x, vb.y, vb.z }
                            });
                            triCount++;
                        }
                    }
                }
            }
            DBPrintf("    buildSceneMesh: extracted %d triangles (ModelerAPI)\n", triCount);
            return mesh;
        }
    }

    // Fallback: Legacy ModelAccess API (already in FloorPlan)
    DBPrintf("    buildSceneMesh: ModelerAPI returned 0, trying Legacy API\n");

    Int32 bodyCount = 0;
    err = ACAPI_ModelAccess_GetNum(API_BodyID, &bodyCount);
    DBPrintf("    buildSceneMesh: body count=%d\n", bodyCount);

    API_Component3D comp = {};
    for (Int32 bi = 1; bi <= bodyCount; ++bi) {
        BNZeroMemory(&comp, sizeof(comp));
        comp.header.typeID = API_BodyID;
        comp.header.index = bi;
        err = ACAPI_ModelAccess_GetComponent(&comp);
        if (err != NoError) continue;

        for (Int32 pi = 1; pi <= comp.body.nPgon; ++pi) {
            BNZeroMemory(&comp, sizeof(comp));
            comp.header.typeID = API_PgonID;
            comp.header.index = pi;
            err = ACAPI_ModelAccess_GetComponent(&comp);
            if (err != NoError || (comp.pgon.status & APIPgon_Invis)) continue;
            if (comp.pgon.fpedg > comp.pgon.lpedg) continue;

            std::vector<double> vx, vy, vz;
            for (Int32 pedgIdx = comp.pgon.fpedg; pedgIdx <= comp.pgon.lpedg; ++pedgIdx) {
                BNZeroMemory(&comp, sizeof(comp));
                comp.header.typeID = API_PedgID;
                comp.header.index = pedgIdx;
                err = ACAPI_ModelAccess_GetComponent(&comp);
                if (err != NoError || comp.pedg.pedg == 0) continue;

                Int32 edgeIdx = abs(comp.pedg.pedg);
                BNZeroMemory(&comp, sizeof(comp));
                comp.header.typeID = API_EdgeID;
                comp.header.index = edgeIdx;
                err = ACAPI_ModelAccess_GetComponent(&comp);
                if (err != NoError) continue;

                Int32 vertIdx = (comp.pedg.pedg < 0) ? comp.edge.vert2 : comp.edge.vert1;
                BNZeroMemory(&comp, sizeof(comp));
                comp.header.typeID = API_VertID;
                comp.header.index = abs(vertIdx);
                err = ACAPI_ModelAccess_GetComponent(&comp);
                if (err == NoError) {
                    vx.push_back(comp.vert.x);
                    vy.push_back(comp.vert.y);
                    vz.push_back(comp.vert.z);
                }
            }

            Int32 nv = static_cast<Int32>(vx.size());
            if (nv >= 3) {
                for (Int32 v = 1; v + 1 < nv; ++v) {
                    mesh.triangles.push_back({
                        { vx[0], vy[0], vz[0] },
                        { vx[v], vy[v], vz[v] },
                        { vx[v+1], vy[v+1], vz[v+1] }
                    });
                }
            }
        }
    }

    // Restore original database (only if we switched)
    if (needsRestore) {
        ACAPI_Database_ChangeCurrentDatabase(&savedDB);
    }
    DBPrintf("    buildSceneMesh: extracted %d triangles\n", static_cast<int>(mesh.triangles.size()));
    return mesh;
}

// ═════════════════════════════════════════════════════════════════════════════
// 5. FIND ELEMENTS BY LIBPART NAME
// ═════════════════════════════════════════════════════════════════════════════

static GS::Array<API_Guid> findElementsByLibpartName(const GS::UniString& targetName)
{
    GS::Array<API_Guid> result;
    GS::Array<API_Guid> allElems;

    ACAPI_Element_GetElemList(API_ObjectID, &allElems, APIElemMask_FromFloorplan);

    for (const API_Guid& guid : allElems) {
        API_Element elem = {};
        elem.header.guid = guid;

        if (ACAPI_Element_Get(&elem) != NoError) continue;

        API_LibPart libPart = {};
        libPart.index = elem.object.libInd;

        if (ACAPI_LibraryPart_Get(&libPart) != NoError) continue;

        GS::UniString partName(libPart.docu_UName);
        if (partName.IsEqual(targetName, GS::CaseInsensitive))
            result.Push(guid);
    }

    return result;
}

// ═════════════════════════════════════════════════════════════════════════════
// 6. EXTRACT ELEMENT'S 3D LOCATION (AC29)
// ═════════════════════════════════════════════════════════════════════════════

static std::optional<Vec3> getElementLocation(const API_Guid& guid)
{
    // DBPrintf("      getLoc: step1 Element_Get\n");
    API_Element elem = {};
    elem.header.guid = guid;
    if (ACAPI_Element_Get(&elem) != NoError) return std::nullopt;

    // DBPrintf("      getLoc: step2 GetStorySettings\n");
    API_StoryInfo storyInfo = {};
    GSErrCode err = ACAPI_ProjectSetting_GetStorySettings(&storyInfo, 0);
    // DBPrintf("      getLoc: step3 err=%d data=%p first=%d last=%d\n",
    //          err, (void*)storyInfo.data, storyInfo.firstStory, storyInfo.lastStory);
    if (err != NoError || storyInfo.data == nullptr) return std::nullopt;

    // DBPrintf("      getLoc: step4 deref handle\n");
    double storyZ = 0.0;
    API_StoryType* stories = *storyInfo.data;
    for (short s = storyInfo.firstStory; s <= storyInfo.lastStory; ++s) {
        if (stories[s].index == elem.header.floorInd) {
            storyZ = stories[s].level;
            break;
        }
    }

    // DBPrintf("      getLoc: step5 cleanup\n");
    // NOTE: BMKillHandle crashes in AC29 — skip it, handle is auto-managed
    // if (storyInfo.data != nullptr) {
    //     BMKillHandle((GS::GSHandle*)storyInfo.data);
    // }

    // DBPrintf("      getLoc: step6 return\n");
    return Vec3{
        elem.object.pos.x,
        elem.object.pos.y,
        storyZ + elem.object.level
    };
}

// ═════════════════════════════════════════════════════════════════════════════
// 6.5 READ GDL PARAMETER VALUE FROM ELEMENT MEMO
// ═════════════════════════════════════════════════════════════════════════════

static std::optional<double> getParamValue(const API_Guid& guid, const char* paramName)
{
    API_ElementMemo memo = {};
    GSErrCode err = ACAPI_Element_GetMemo(guid, &memo, APIMemoMask_AddPars);
    if (err != NoError || memo.params == nullptr || *memo.params == nullptr) {
        DBPrintf("      getParamValue('%s'): GetMemo failed (err=%d)\n", paramName, err);
        ACAPI_DisposeElemMemoHdls(&memo);
        return std::nullopt;
    }

    UInt32 totalParams = BMGetHandleSize((GSConstHandle)memo.params) / sizeof(API_AddParType);
    for (UInt32 i = 0; i < totalParams; ++i) {
        API_AddParType& param = (*memo.params)[static_cast<Int32>(i)];
        if (strncmp(param.name, paramName, API_NameLen) == 0) {
            double val = param.value.real;
            DBPrintf("      getParamValue('%s') = %.4f\n", paramName, val);
            ACAPI_DisposeElemMemoHdls(&memo);
            return val;
        }
    }

    DBPrintf("      getParamValue('%s'): NOT FOUND (scanned %d params)\n", paramName, totalParams);
    ACAPI_DisposeElemMemoHdls(&memo);
    return std::nullopt;
}

// ═════════════════════════════════════════════════════════════════════════════
// 7. WRITE PARAMETER ARRAY TO ELEMENT (AC29 API)
//    Uses OpenParameters → ChangeAParameter cycle
//    Supports variable-size 1D array (nSquares * 12 values)
// ═════════════════════════════════════════════════════════════════════════════

static GSErrCode writeSolarizationParam(
    const API_Guid&   guid,
    Int32             /*libInd*/,
    const std::vector<double>& values)  // 1D array: nSquares * 12
{
    DBPrintf("      >>> writeSolarizationParam: %d values to write\n", static_cast<int>(values.size()));

    // Get element data
    API_Element elem = {};
    elem.header.guid = guid;
    GSErrCode err = ACAPI_Element_Get(&elem, 0);
    if (err != NoError) {
        DBPrintf("      writeSolarization: Element_Get failed err=%d\n", err);
        return err;
    }

    // Get memo handles (includes params)
    API_ElementMemo memo = {};
    err = ACAPI_Element_GetMemo(guid, &memo, APIMemoMask_All);
    DBPrintf("      writeSolarization: GetMemo err=%d params=%p\n", err, (void*)memo.params);
    if (err != NoError) {
        ACAPI_DisposeElemMemoHdls(&memo);
        return err;
    }

    // Find 'solarization' parameter in memo.params and write values
    bool found = false;
    Int32 newSize = static_cast<Int32>(values.size());
    if (memo.params != nullptr && *memo.params != nullptr) {
        UInt32 totalParams = BMGetHandleSize((GSConstHandle)memo.params) / sizeof(API_AddParType);
        DBPrintf("      writeSolarization: param count=%d, writing %d values\n", totalParams, newSize);

        for (UInt32 i = 0; i < totalParams; ++i) {
            API_AddParType& param = (*memo.params)[static_cast<Int32>(i)];
            if (strncmp(param.name, "solarization", API_NameLen) == 0) {
                DBPrintf("      writeSolarization: found 'solarization' oldDim1=%d -> newDim1=%d\n",
                         param.dim1, newSize);
                found = true;

                // Resize array if needed (check current dimension)
                param.dim1 = newSize;

                param.value.array = BMReallocHandle(param.value.array,
                    newSize * sizeof(double),
                    REALLOC_FULLCLEAR, 0);

                if (param.value.array != nullptr && *param.value.array != nullptr) {
                    double* arr = reinterpret_cast<double*>(*param.value.array);
                    for (Int32 idx = 0; idx < newSize; ++idx) {
                        arr[idx] = values[static_cast<size_t>(idx)];
                    }
                    // Log first few and last few values
                    Int32 logCount = std::min(newSize, 6);
                    DBPrintf("      writeSolarization: values[0..%d] = ", logCount - 1);
                    for (Int32 idx = 0; idx < logCount; ++idx) {
                        if (idx > 0) DBPrintf(", ");
                        DBPrintf("%.1f", arr[idx]);
                    }
                    DBPrintf(" ...\n");
                } else {
                    DBPrintf("      writeSolarization: ERROR: realloc failed!\n");
                }
                break;
            }
        }
    }

    if (!found) {
        DBPrintf("      writeSolarization: WARNING: param 'solarization' not found!\n");
    }

    // Persist changes — clear element mask (no geometry change), only params
    // withDel=true: recorded as delete+recreate in undo stack (standard pattern)
    API_Element mask = {};
    BNZeroMemory(&mask, sizeof(mask));
    err = ACAPI_Element_Change(&elem, &mask, &memo, APIMemoMask_AddPars, true);
    DBPrintf("      writeSolarization: Element_Change err=%d %s\n",
             err, (found ? "(param found)" : "(param NOT found — values not written!)"));

    ACAPI_DisposeElemMemoHdls(&memo);
    return err;
}

// ═════════════════════════════════════════════════════════════════════════════
// 8a. ANALYTICAL SUNRISE / SUNSET  (NOAA / Meeus)
// ═════════════════════════════════════════════════════════════════════════════

struct DaylightWindow { double sunriseUTC; double sunsetUTC; };

static std::optional<DaylightWindow> getDaylightWindow(
    double latDeg, double lonDeg,
    int month, int day,
    double utcOffsetHours = 0.0)
{
    int Y = 2000, M = month, D = day;
    if (M <= 2) { Y--; M += 12; }
    double A  = std::floor(Y / 100.0);
    double B  = 2.0 - A + std::floor(A / 4.0);
    double JD = std::floor(365.25 * (Y + 4716))
              + std::floor(30.6001 * (M + 1))
              + D + B - 1524.5;

    double T = (JD - 2451545.0) / 36525.0;

    double L0 = std::fmod(280.46646 + T * (36000.76983 + T * 0.0003032), 360.0);
    double M0 = std::fmod(357.52911 + T * (35999.05029 - T * 0.0001537), 360.0);

    double Mrad  = M0 * DEG_TO_RAD;
    double C     = std::sin(  Mrad) * (1.914602 - T * (0.004817 + 0.000014 * T))
                 + std::sin(2*Mrad) * (0.019993 - T *  0.000101)
                 + std::sin(3*Mrad) *  0.000289;
    double sunLon = L0 + C;

    double omega  = 125.04 - 1934.136 * T;
    double lambda = sunLon - 0.00569 - 0.00478 * std::sin(omega * DEG_TO_RAD);
    double obliq  = (23.0 + 26.0/60.0 + 21.448/3600.0)
                  - T * (46.8150/3600.0 + T * (0.00059/3600.0 - T * 0.001813/3600.0))
                  + 0.00256 * std::cos(omega * DEG_TO_RAD);

    double decl = std::asin(std::sin(obliq * DEG_TO_RAD) * std::sin(lambda * DEG_TO_RAD));

    const double ZENITH = 90.833 * DEG_TO_RAD;
    double lat   = latDeg * DEG_TO_RAD;
    double cosHA = (std::cos(ZENITH) - std::sin(lat) * std::sin(decl))
                 / (std::cos(lat)   * std::cos(decl));

    if (cosHA >  1.0) return std::nullopt;
    if (cosHA < -1.0)
        return DaylightWindow{ utcOffsetHours, 24.0 + utcOffsetHours };

    double HA = std::acos(cosHA) * RAD_TO_DEG;

    double y    = std::tan(obliq * DEG_TO_RAD / 2.0);  y *= y;
    double L0r  = L0 * DEG_TO_RAD, M0r = M0 * DEG_TO_RAD;
    double EoT  = 4.0 * RAD_TO_DEG * (
                    y * std::sin(2*L0r)
                  - 2.0 * std::sin(M0r)
                  + 4.0 * y * std::sin(M0r) * std::cos(2*L0r)
                  - 0.5 * y*y * std::sin(4*L0r)
                  - 1.25 * std::sin(2*M0r));

    double solarNoon = (720.0 - 4.0 * lonDeg - EoT) / 60.0 + utcOffsetHours;
    double halfDay   = HA * 4.0 / 60.0;

    return DaylightWindow{ solarNoon - halfDay, solarNoon + halfDay };
}

static void debugDaylightWindow(double latDeg, double lonDeg, int month,
                                int day, double utcOffsetHours)
{
    auto window = getDaylightWindow(latDeg, lonDeg, month, day, utcOffsetHours);
    if (window) {
        double hrs = (window->sunsetUTC - window->sunriseUTC);
        DBPrintf("  daylight: month=%02d sunrise=%.1f sunset=%.1f duration=%.1fhrs\n",
                 month, window->sunriseUTC, window->sunsetUTC, hrs);
    } else {
        DBPrintf("  daylight: month=%02d NO SUNRISE\n", month);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// 8b. SOLAR ANALYSIS  — per point, full year (21st of each month)
// ═════════════════════════════════════════════════════════════════════════════

static const int ANALYSIS_DAY[] = {0, 21, 20, 20, 20, 21, 21, 21, 21, 21, 21, 21, 21};  // match GDL arSunChart dates
static const int TIMESTEP_MIN = 1;

static std::array<double,12> analyzeSolarization(
    const Vec3& point,
    const Mesh& scene,
    double      latDeg,
    double      lonDeg,
    double      utcOffsetHours = 0.0)
{
    std::array<double,12> result = {};
    int firstJune = 999, lastJune = 0;  // debug occlusion range

    // Debug: print daylight windows for Jan and Jun
    debugDaylightWindow(latDeg, lonDeg, 1, ANALYSIS_DAY[1], utcOffsetHours);
    debugDaylightWindow(latDeg, lonDeg, 6, ANALYSIS_DAY[6], utcOffsetHours);

    for (int month = 1; month <= 12; ++month) {
        auto window = getDaylightWindow(
            latDeg, lonDeg, month, ANALYSIS_DAY[month], utcOffsetHours);

        if (!window) continue;

        int startMin = static_cast<int>(std::ceil (window->sunriseUTC * 60.0 / TIMESTEP_MIN)) * TIMESTEP_MIN;
        int endMin   = static_cast<int>(std::floor(window->sunsetUTC  * 60.0 / TIMESTEP_MIN)) * TIMESTEP_MIN;

        int solarMinutes = 0;
        int totalTimesteps = 0;
        int occludedCount = 0;

        for (int minOfDay = startMin; minOfDay <= endMin; minOfDay += TIMESTEP_MIN) {
            double localHour = minOfDay / 60.0;

            // solarPosition has EoT mismatch with getDaylightWindow.
            // Trust getDaylightWindow for day/night (sunrise/sunset already computed).
            // Only use solarPosition for sun direction (azimuth) for occlusion raycasting.
            totalTimesteps++;

            Vec3 dir;
            bool hasSunDir = false;
            {
                auto sunPos = solarPosition(latDeg, lonDeg, month, ANALYSIS_DAY[month], localHour, utcOffsetHours);
                if (sunPos) {
                    dir = sunDirectionFromPos(*sunPos);
                    hasSunDir = true;
                }
            }

            bool occluded = false;
            if (hasSunDir) {
                occluded = isOccluded(point, dir, scene);
                // Debug first and last occluded timestep for June
                if (occluded && month == 6) {
                    if (minOfDay < firstJune) firstJune = minOfDay;
                    if (minOfDay > lastJune) lastJune = minOfDay;
                }
            }
            // If solarPosition fails (EoT mismatch), trust getDaylightWindow: timestep is sunlit
            if (occluded) occludedCount++;
            if (!occluded)
                solarMinutes += TIMESTEP_MIN;
        }

        DBPrintf("    month=%02d: steps=%d occluded=%d sun=%dmin (%.1fhrs)\n",
                 month, totalTimesteps, occludedCount, solarMinutes,
                 solarMinutes / 60.0);
        if (month == 6 && occludedCount > 0) {
            DBPrintf("    June occlusion range: %d..%d min of day\n", firstJune, lastJune);
        }

        result[month - 1] = static_cast<double>(solarMinutes);
    }

    return result;
}

// ═════════════════════════════════════════════════════════════════════════════
// 9. MAIN ENTRY POINT (extern "C" for C linkage)
// ═════════════════════════════════════════════════════════════════════════════

extern "C"
GSErrCode RunSolarAnalysis(
    const GS::UniString& libpartName,
    double               latitudeDeg,
    double               longitudeDeg,
    double               utcOffsetHours
)
{
    DBPrintf("=== SolarAnalysis: START ===\n");
    DBPrintf("  lat=%f lon=%f utcOff=%f name='%s'\n",
             latitudeDeg, longitudeDeg, utcOffsetHours, libpartName.ToCStr().Get());

    // ── Step 1: Build scene mesh ────────────────────────────────
    DBPrintf("  [1] building scene mesh...\n");
    Mesh scene = buildSceneMesh();
    DBPrintf("  [1] scene has %d triangles\n", static_cast<int>(scene.triangles.size()));
    // for (size_t i = 0; i < scene.triangles.size() && i < 3; ++i) {
    //     const auto& t = scene.triangles[i];
    //     DBPrintf("    tri[%zu]: (%f,%f,%f)(%f,%f,%f)(%f,%f,%f)\n",
    //              i, t.v0.x, t.v0.y, t.v0.z, t.v1.x, t.v1.y, t.v1.z, t.v2.x, t.v2.y, t.v2.z);
    // }

    if (scene.triangles.empty()) {
        DBPrintf("  [1] WARNING: no scene geometry — treating as fully exposed\n");
        // No occluders = full sun exposure for all months
    }

    ACAPI_WriteReport("Solar Analysis: scene mesh has %d triangles.", false,
                      static_cast<int>(scene.triangles.size()));

    // ── Step 2: Find target elements ────────────────────────────
    DBPrintf("  [2] finding elements...\n");
    GS::Array<API_Guid> targets = findElementsByLibpartName(libpartName);
    DBPrintf("  [2] found %d targets\n", targets.GetSize());

    if (targets.IsEmpty()) {
        GS::UniString msg = GS::UniString::Printf(
            "No objects named '%s' were found in the model.", libpartName.ToCStr().Get());
        DGAlert(DG_WARNING, "Solar Analysis", msg, "", "OK");
        return APIERR_BADID;
    }

    ACAPI_WriteReport("Solar Analysis: found %d target element(s).", false, targets.GetSize());

    GS::UniString commandName = GS::UniString::Printf(
        "Solar Analysis (%s)", libpartName.ToCStr().Get());
    DBPrintf("  [3] starting undoable command...\n");

    // Collect analysis results, then write params inside undoable command
    struct AnalysisResult { API_Guid guid; Int32 libInd; std::vector<double> values; };
    std::vector<AnalysisResult> results;

    for (const API_Guid& guid : targets) {
        API_Element elem = {};
        elem.header.guid = guid;
        if (ACAPI_Element_Get(&elem) != NoError) continue;

        auto loc = getElementLocation(guid);
        if (!loc) continue;

        // Read GDL parameters: divisionsX, divisionsY, A, B, rotation
        DBPrintf("    --- reading sensor params ---\n");
        auto divXOpt   = getParamValue(guid, "divisionsX");
        auto divYOpt   = getParamValue(guid, "divisionsY");
        auto aOpt      = getParamValue(guid, "A");
        auto bOpt      = getParamValue(guid, "B");
        auto rotOpt    = getParamValue(guid, "tilt_angle");

        int divisionsX = divXOpt ? static_cast<int>(std::round(*divXOpt)) : 1;
        int divisionsY = divYOpt ? static_cast<int>(std::round(*divYOpt)) : 1;
        double sensorA = aOpt ? *aOpt : 1.0;   // fallback 1m if not found
        double sensorB = bOpt ? *bOpt : 1.0;
        double rotDeg  = rotOpt ? *rotOpt : 0.0;

        // Clamp divisions to reasonable range
        if (divisionsX < 1) divisionsX = 1;
        if (divisionsY < 1) divisionsY = 1;
        // Clamp rotation 0..90
        if (rotDeg < 0.0) rotDeg = 0.0;
        if (rotDeg > 90.0) rotDeg = 90.0;

        int nSquares = divisionsX * divisionsY;
        double cellW = sensorA / divisionsX;
        double cellH = sensorB / divisionsY;

        double startX = loc->x - sensorA / 2.0;
        double startY = loc->y - sensorB / 2.0;

        // Rotation: around X-axis through bottom edge (Y=startY, Z=loc.z)
        //   0° = flat on floor, 90° = vertical wall
        double rotRad = rotDeg * DEG_TO_RAD;
        double cosR   = std::cos(rotRad);
        double sinR   = std::sin(rotRad);

        DBPrintf("    sensor center=(%.4f, %.4f, %.4f)\n", loc->x, loc->y, loc->z);
        DBPrintf("    sensor dims: A=%.4f B=%.4f  divisions: %dx%d  squares=%d\n",
                 sensorA, sensorB, divisionsX, divisionsY, nSquares);
        DBPrintf("    cell size: %.4f x %.4f\n", cellW, cellH);
        DBPrintf("    rotation: %.1f° (cos=%.4f sin=%.4f) pivot Y=%.4f Z=%.4f\n",
                 rotDeg, cosR, sinR, startY, loc->z);
        DBPrintf("    grid bounds (flat): X=[%.4f .. %.4f]  Y=[%.4f .. %.4f]\n",
                 startX, startX + sensorA, startY, startY + sensorB);

        // Compute grid center points and analyze each
        // Grid spans from (loc.x - A/2, loc.y - B/2) to (loc.x + A/2, loc.y + B/2)
        std::vector<double> allValues;  // flat: nSquares * 12
        allValues.reserve(static_cast<size_t>(nSquares) * 12);

        int squareIdx = 0;
        for (int iy = 0; iy < divisionsY; ++iy) {
            for (int ix = 0; ix < divisionsX; ++ix) {
                // Flat center of square (ix, iy)
                double flatX = startX + cellW * (ix + 0.5);
                double distFromPivot = cellH * (iy + 0.5);  // distance from bottom edge

                // Apply rotation around X-axis through bottom edge:
                //   X unchanged
                //   Y = startY + distFromPivot * cos(rotation)
                //   Z = loc.z   + distFromPivot * sin(rotation)
                Vec3 point{
                    flatX,
                    startY + distFromPivot * cosR,
                    loc->z   + distFromPivot * sinR
                };

                DBPrintf("    square[%d/%d] (%d,%d): dist=%.4f center=(%.4f, %.4f, %.4f)\n",
                         squareIdx + 1, nSquares, ix, iy,
                         distFromPivot, point.x, point.y, point.z);

                std::array<double,12> minutes = analyzeSolarization(
                    point, scene, latitudeDeg, longitudeDeg, utcOffsetHours);

                // Log per-square results summary
                int totalMin = 0;
                for (int m = 0; m < 12; ++m) {
                    totalMin += static_cast<int>(minutes[m]);
                }
                DBPrintf("      square[%d] result: ", squareIdx);
                for (int m = 0; m < 12; ++m) {
                    if (m > 0) DBPrintf(", ");
                    DBPrintf("%d", static_cast<int>(minutes[m]));
                }
                DBPrintf("  (total=%dmin)\n", totalMin);

                for (int m = 0; m < 12; ++m) {
                    allValues.push_back(minutes[m]);
                }
                squareIdx++;
            }
        }

        DBPrintf("    --- sensor complete: %d values collected (%dx12 grid, %zu bytes) ---\n",
                 nSquares * 12, nSquares, allValues.size() * sizeof(double));
        results.push_back({guid, elem.object.libInd, std::move(allValues)});
    }

    // Write parameters inside undoable command (Element_Change requires it)
    GSErrCode err = ACAPI_CallUndoableCommand(commandName, [&]() -> GSErrCode {
        for (const auto& res : results) {
            DBPrintf("  [4] writing params for element %zu/%zd (%d values)\n",
                     &res - results.data() + 1, results.size(),
                     static_cast<int>(res.values.size()));
            GSErrCode writeErr = writeSolarizationParam(res.guid, res.libInd, res.values);
            if (writeErr != NoError) {
                DBPrintf("    WARN: write failed err=%d\n", writeErr);
                return writeErr;
            }
        }
        return NoError;
    });

    if (err != NoError) {
        DGAlert(DG_ERROR, "Solar Analysis",
                GS::UniString::Printf("Write failed with error %d.", static_cast<int>(err)), "", "OK");
        return err;
    }

    GS::UniString successMsg = GS::UniString::Printf(
        "Solar analysis complete.\n%d element(s) processed.", static_cast<int>(results.size()));
    DGAlert(DG_INFORMATION, "Solar Analysis", successMsg, "", "OK");
    DBPrintf("=== SolarAnalysis: DONE ===\n");

    return NoError;
}
