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
#include "Model.hpp"
#include "ModelElement.hpp"
#include "ModelMeshBody.hpp"
#include "Polygon.hpp"
#include "Vertex.hpp"
#include "DG.h"

#include "SolarAnalysis.hpp"

#include <cmath>
#include <vector>
#include <array>
#include <string>
#include <optional>
#include <limits>
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
    return (t > 1e-4 && t < tMax);
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

static int dayOfYear(int month, int day)
{
    static const int dpm[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    int doy = day;
    for (int m = 1; m < month; ++m) doy += dpm[m];
    return doy;
}

static std::optional<SunPos> solarPosition(
    double latDeg, double lonDeg,
    int month, int day, double utcHour)
{
    int    doy = dayOfYear(month, day);
    double B   = (360.0 / 365.0) * (doy - 81) * DEG_TO_RAD;

    double EoT  = 9.87*std::sin(2*B) - 7.53*std::cos(B) - 1.5*std::sin(B);
    double decl = 23.45 * std::sin(B) * DEG_TO_RAD;

    double TC   = 4.0 * lonDeg + EoT;
    double solar = utcHour + TC / 60.0;
    double HRA  = (solar - 12.0) * 15.0 * DEG_TO_RAD;

    double lat  = latDeg * DEG_TO_RAD;
    double sinAlt = std::sin(lat)*std::sin(decl)
                  + std::cos(lat)*std::cos(decl)*std::cos(HRA);
    double altRad = std::asin(sinAlt);
    if (altRad <= 0.0) return std::nullopt;

    double cosAz = (std::sin(decl) - std::sin(lat)*sinAlt)
                 / (std::cos(lat) * std::cos(altRad));
    cosAz = std::max(-1.0, std::min(1.0, cosAz));
    double az = std::acos(cosAz) * RAD_TO_DEG;
    if (std::sin(HRA) > 0) az = 360.0 - az;

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

    // Save current database and switch to Floor Plan (3D model lives there)
    API_WindowInfo savedDB = {};
    GSErrCode err = ACAPI_Database_GetCurrentDatabase(&savedDB);
    DBPrintf("    buildSceneMesh: savedDB typeID=%ld\n", (long)savedDB.typeID);

    API_WindowInfo floorPlan = {};
    floorPlan.typeID = APIWind_FloorPlanID;
    err = ACAPI_Database_ChangeCurrentDatabase(&floorPlan);
    DBPrintf("    buildSceneMesh: switch to FloorPlan err=%d\n", err);
    if (err != NoError) return mesh;

    // Select the 3D Window's existing sight
    void* oldSight = nullptr;
    err = ACAPI_Sight_SelectSight(nullptr, &oldSight);
    DBPrintf("    buildSceneMesh: SelectSight(nullptr) err=%d\n", err);

    // Get body count via legacy ModelAccess API
    Int32 bodyCount = 0;
    err = ACAPI_ModelAccess_GetNum(API_BodyID, &bodyCount);
    DBPrintf("    buildSceneMesh: bodyCount=%d\n", bodyCount);
    if (err != NoError || bodyCount == 0) {
        if (oldSight != nullptr) { void* t=nullptr; ACAPI_Sight_SelectSight(oldSight, &t); }
        ACAPI_Database_ChangeCurrentDatabase(&savedDB);
        return mesh;
    }

    // Iterate bodies using legacy ModelAccess API (1-based indices)
    API_Component3D comp = {};
    for (Int32 bi = 1; bi <= bodyCount; ++bi) {
        comp.header.typeID = API_BodyID;
        comp.header.index = bi;
        err = ACAPI_ModelAccess_GetComponent(&comp);
        DBPrintf("    buildSceneMesh: body[%d] err=%d status=%d nPgon=%d\n",
                 bi, err, comp.body.status, comp.body.nPgon);
        if (err == APIERR_DELETED) continue;
        if (err != NoError) break;

        // Iterate polygons of this body (1-based)
        for (Int32 pi = 1; pi <= comp.body.nPgon; ++pi) {
            comp.header.typeID = API_PgonID;
            comp.header.index = pi;
            err = ACAPI_ModelAccess_GetComponent(&comp);
            DBPrintf("      pgon[%d] err=%d status=%d fpedg=%d lpedg=%d\n",
                     pi, err, comp.pgon.status, comp.pgon.fpedg, comp.pgon.lpedg);
            if (err != NoError) continue;

            // Skip invisible polygons or empty edge ranges
            if (comp.pgon.status & APIPgon_Invis) continue;
            if (comp.pgon.fpedg > comp.pgon.lpedg) {
                DBPrintf("      pgon[%d] SKIP: fpedg > lpedg\n", pi);
                continue;
            }

            // Collect vertices via edge references (fpedg to lpedg)
            std::vector<double> vx, vy, vz;
            DBPrintf("      pgon[%d] iterating %d to %d\n", pi, comp.pgon.fpedg, comp.pgon.lpedg);
            for (Int32 pedgIdx = comp.pgon.fpedg; pedgIdx <= comp.pgon.lpedg; ++pedgIdx) {
                comp.header.typeID = API_PedgID;
                comp.header.index = pedgIdx;
                err = ACAPI_ModelAccess_GetComponent(&comp);
                if (err != NoError || comp.pedg.pedg == 0) continue;

                Int32 edgeIdx = abs(comp.pedg.pedg);
                comp.header.typeID = API_EdgeID;
                comp.header.index = edgeIdx;
                err = ACAPI_ModelAccess_GetComponent(&comp);
                if (err != NoError) continue;

                // Get vertex index (negative pedg = reversed direction)
                Int32 vertIdx = (comp.pedg.pedg < 0) ? comp.edge.vert2 : comp.edge.vert1;
                comp.header.typeID = API_VertID;
                comp.header.index = abs(vertIdx);
                err = ACAPI_ModelAccess_GetComponent(&comp);
                if (err == NoError) {
                    vx.push_back(comp.vert.x);
                    vy.push_back(comp.vert.y);
                    vz.push_back(comp.vert.z);
                }
            }

            // Fan-triangulate the polygon vertices
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

    DBPrintf("    buildSceneMesh: extracted %d triangles\n", (Int32)mesh.triangles.size());

    // Restore sight and database
    if (oldSight != nullptr) { void* t=nullptr; ACAPI_Sight_SelectSight(oldSight, &t); }
    ACAPI_Database_ChangeCurrentDatabase(&savedDB);
    DBPrintf("    buildSceneMesh: restored DB typeID=%ld\n", (long)savedDB.typeID);

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
    DBPrintf("      getLoc: step1 Element_Get\n");
    API_Element elem = {};
    elem.header.guid = guid;
    if (ACAPI_Element_Get(&elem) != NoError) return std::nullopt;

    DBPrintf("      getLoc: step2 GetStorySettings\n");
    API_StoryInfo storyInfo = {};
    GSErrCode err = ACAPI_ProjectSetting_GetStorySettings(&storyInfo, 0);
    DBPrintf("      getLoc: step3 err=%d data=%p first=%d last=%d\n",
             err, (void*)storyInfo.data, storyInfo.firstStory, storyInfo.lastStory);
    if (err != NoError || storyInfo.data == nullptr) return std::nullopt;

    DBPrintf("      getLoc: step4 deref handle\n");
    double storyZ = 0.0;
    API_StoryType* stories = *storyInfo.data;
    for (short s = storyInfo.firstStory; s <= storyInfo.lastStory; ++s) {
        if (stories[s].index == elem.header.floorInd) {
            storyZ = stories[s].level;
            break;
        }
    }

    DBPrintf("      getLoc: step5 cleanup\n");
    // NOTE: BMKillHandle crashes in AC29 — skip it, handle is auto-managed
    // if (storyInfo.data != nullptr) {
    //     BMKillHandle((GS::GSHandle*)storyInfo.data);
    // }

    DBPrintf("      getLoc: step6 return\n");
    return Vec3{
        elem.object.pos.x,
        elem.object.pos.y,
        storyZ + elem.object.level
    };
}

// ═════════════════════════════════════════════════════════════════════════════
// 7. WRITE PARAMETER ARRAY TO ELEMENT (AC29 API)
//    Uses OpenParameters → ChangeAParameter cycle
// ═════════════════════════════════════════════════════════════════════════════

static GSErrCode writeSolarizationParam(
    const API_Guid&              guid,
    Int32                        /*libInd*/,
    const std::array<double,12>& minutesPerMonth)
{
    // Get element data
    API_Element elem = {};
    elem.header.guid = guid;
    GSErrCode err = ACAPI_Element_Get(&elem, 0);
    if (err != NoError) return err;

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
    if (memo.params != nullptr && *memo.params != nullptr) {
        UInt32 totalParams = BMGetHandleSize((GSConstHandle)memo.params) / sizeof(API_AddParType);
        DBPrintf("      writeSolarization: param count=%d\n", totalParams);

        for (UInt32 i = 0; i < totalParams; ++i) {
            API_AddParType& param = (*memo.params)[static_cast<Int32>(i)];
            if (strncmp(param.name, "solarization", API_NameLen) == 0) {
                DBPrintf("      writeSolarization: found '%s' dim1=%d typeID=%d\n",
                         param.name, param.dim1, param.typeID);
                found = true;

                // Resize array if needed (12 values)
                Int32 newSize = 12;
                param.dim1 = newSize;

                param.value.array = BMReallocHandle(param.value.array,
                    newSize * sizeof(double),
                    REALLOC_FULLCLEAR, 0);

                if (param.value.array != nullptr && *param.value.array != nullptr) {
                    double* arr = reinterpret_cast<double*>(*param.value.array);
                    for (int m = 0; m < 12; ++m) {
                        arr[m] = minutesPerMonth[m];
                        DBPrintf("      writeSolarization: [%d]=%.0f\n", m + 1, arr[m]);
                    }
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
    DBPrintf("      writeSolarization: Element_Change err=%d\n", err);

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

// ═════════════════════════════════════════════════════════════════════════════
// 8b. SOLAR ANALYSIS  — per point, full year (21st of each month)
// ═════════════════════════════════════════════════════════════════════════════

static const int ANALYSIS_DAY = 21;
static const int TIMESTEP_MIN = 10;

static std::array<double,12> analyzeSolarization(
    const Vec3& point,
    const Mesh& scene,
    double      latDeg,
    double      lonDeg,
    double      utcOffsetHours = 0.0)
{
    std::array<double,12> result = {};

    for (int month = 1; month <= 12; ++month) {
        auto window = getDaylightWindow(
            latDeg, lonDeg, month, ANALYSIS_DAY, utcOffsetHours);

        if (!window) continue;

        int startMin = static_cast<int>(std::ceil (window->sunriseUTC * 60.0 / TIMESTEP_MIN)) * TIMESTEP_MIN;
        int endMin   = static_cast<int>(std::floor(window->sunsetUTC  * 60.0 / TIMESTEP_MIN)) * TIMESTEP_MIN;

        int solarMinutes = 0;
        int totalTimesteps = 0;
        int occludedCount = 0;

        for (int minOfDay = startMin; minOfDay <= endMin; minOfDay += TIMESTEP_MIN) {
            double utcHour = minOfDay / 60.0 - utcOffsetHours;

            auto sunPos = solarPosition(latDeg, lonDeg, month, ANALYSIS_DAY, utcHour);
            if (!sunPos) continue;

            totalTimesteps++;
            bool occluded = isOccluded(point, sunDirectionFromPos(*sunPos), scene);
            if (occluded) occludedCount++;
            if (!occluded)
                solarMinutes += TIMESTEP_MIN;
        }

        // Debug for June (month 6)
        if (month == 6) {
            DBPrintf("    DEBUG month=6: startMin=%d endMin=%d totalSteps=%d occluded=%d sun=%d\n",
                     startMin, endMin, totalTimesteps, occludedCount, solarMinutes);
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
    for (size_t i = 0; i < scene.triangles.size() && i < 3; ++i) {
        const auto& t = scene.triangles[i];
        DBPrintf("    tri[%zu]: (%f,%f,%f)(%f,%f,%f)(%f,%f,%f)\n",
                 i, t.v0.x, t.v0.y, t.v0.z, t.v1.x, t.v1.y, t.v1.z, t.v2.x, t.v2.y, t.v2.z);
    }

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
    struct AnalysisResult { API_Guid guid; Int32 libInd; std::array<double,12> minutes; };
    std::vector<AnalysisResult> results;

    for (const API_Guid& guid : targets) {
        API_Element elem = {};
        elem.header.guid = guid;
        if (ACAPI_Element_Get(&elem) != NoError) continue;

        auto loc = getElementLocation(guid);
        if (!loc) continue;

        Vec3 sensorPoint = { loc->x, loc->y, loc->z + 0.05 };
        std::array<double,12> minutes = analyzeSolarization(
            sensorPoint, scene, latitudeDeg, longitudeDeg, utcOffsetHours);

        DBPrintf("    solarization: ");
        for (int m = 0; m < 12; ++m) {
            if (m > 0) DBPrintf(",");
            DBPrintf("%d", static_cast<int>(minutes[m]));
        }
        DBPrintf("\n");

        results.push_back({guid, elem.object.libInd, minutes});
    }

    // Write parameters inside undoable command (Element_Change requires it)
    GSErrCode err = ACAPI_CallUndoableCommand(commandName, [&]() -> GSErrCode {
        for (const auto& res : results) {
            DBPrintf("  [4] writing params for element %zu/%zd\n",
                     &res - results.data() + 1, results.size());
            GSErrCode writeErr = writeSolarizationParam(res.guid, res.libInd, res.minutes);
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
