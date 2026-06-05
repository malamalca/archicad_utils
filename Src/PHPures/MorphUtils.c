#include "MorphUtils.h"

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
