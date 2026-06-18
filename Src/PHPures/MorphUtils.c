#include "MorphUtils.h"
#include "Model3D/model.h"
#include "Model3D/MeshBody.hpp"
#include "Polygon.hpp"
#include "Vertex.hpp"
#include <cmath>

// Helper function to get morph surface area using Archicad API
double GetMorphSurfaceArea(const API_Guid& morphGuid)
{
    GSErrCode err;
    API_Quantities quantities = {};
    API_QuantitiesMask mask;
    API_QuantityPar params = {};
    
    // Allocate elements union
    quantities.elements = reinterpret_cast<API_ElementQuantity*>(BMAllocatePtr(sizeof(API_ElementQuantity), ALLOCATE_CLEAR, 0));
    if (quantities.elements == nullptr) {
        return 0.0;
    }
    
    ACAPI_ELEMENT_QUANTITY_MASK_CLEAR(mask);
    ACAPI_ELEMENT_QUANTITY_MASK_SET(mask, morph, surface);
    
    err = ACAPI_Element_GetQuantities(morphGuid, &params, &quantities, &mask);
    
    double surface = 0.0;
    if (err == NoError) {
        surface = quantities.elements->morph.surface;
    }
    
    BMKillPtr(reinterpret_cast<GSPtr*>(&quantities.elements));

    return surface;
}

// Transform a body-local vertex to world coordinates using the morph transformation matrix.
static API_Coord3D TransformMorphVertex(const API_Tranmat& tm, const VERT& v)
{
    API_Coord3D c;
    c.x = tm.tmx[0] * v.x + tm.tmx[1] * v.y + tm.tmx[2]  * v.z + tm.tmx[3];
    c.y = tm.tmx[4] * v.x + tm.tmx[5] * v.y + tm.tmx[6]  * v.z + tm.tmx[7];
    c.z = tm.tmx[8] * v.x + tm.tmx[9] * v.y + tm.tmx[10] * v.z + tm.tmx[11];
    return c;
}

// Get the perimeter (circumference) of the morph's main face outline.
double GetMorphPerimeter(const API_Guid& morphGuid)
{
    API_Element element;
    BNZeroMemory(&element, sizeof(API_Element));
    element.header.guid = morphGuid;
    if (ACAPI_Element_Get(&element) != NoError) {
        return 0.0;
    }

    API_ElementMemo memo;
    BNZeroMemory(&memo, sizeof(API_ElementMemo));
    if (ACAPI_Element_GetMemo(morphGuid, &memo) != NoError) {
        ACAPI_DisposeElemMemoHdls(&memo);
        return 0.0;
    }

    double maxPerimeter = 0.0;

    if (memo.morphBody != nullptr) {
        try {
            Int32 polygonCount = memo.morphBody->GetPolygonCount();
            for (Int32 polyIdx = 0; polyIdx < polygonCount; ++polyIdx) {
                GS::Array<VERT> coords;
                GS::Array<ULong> holeStarts;  // indices where hole (inner) contours start
                memo.morphBody->GetPolygonVertices(polyIdx, coords, &holeStarts);

                ULong n = coords.GetSize();
                if (n < 3) continue;

                // The outer contour runs from index 0 up to the first hole start (if any)
                ULong outerEnd = n;
                if (holeStarts.GetSize() > 0 && holeStarts[0] >= 3 && holeStarts[0] < n) {
                    outerEnd = holeStarts[0];
                }
                if (outerEnd < 3) continue;

                // Sum the 3D edge lengths around the closed outer contour
                double perimeter = 0.0;
                for (ULong j = 0; j < outerEnd; ++j) {
                    ULong k = (j + 1) % outerEnd;  // wrap to close the contour
                    API_Coord3D a = TransformMorphVertex(element.morph.tranmat, coords[j]);
                    API_Coord3D b = TransformMorphVertex(element.morph.tranmat, coords[k]);
                    double dx = b.x - a.x;
                    double dy = b.y - a.y;
                    double dz = b.z - a.z;
                    perimeter += std::sqrt(dx * dx + dy * dy + dz * dz);
                }

                if (perimeter > maxPerimeter) maxPerimeter = perimeter;
            }
        } catch (...) {
            // Leave maxPerimeter at its best-so-far value on geometry access errors
        }
    }

    ACAPI_DisposeElemMemoHdls(&memo);

    return maxPerimeter;
}
