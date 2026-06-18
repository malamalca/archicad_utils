#ifndef _PHPURES_MORPH_UTILS_H_
#define _PHPURES_MORPH_UTILS_H_

#include "APIEnvir.h"
#include "ACAPinc.h"

// Get morph surface area using Archicad API
double GetMorphSurfaceArea(const API_Guid& morphGuid);

// Get the perimeter (circumference) of the morph's main face outline.
// Computed from the morph body geometry: the longest outer contour among the
// body polygons, summing 3D edge lengths in world coordinates (holes excluded).
// Returns 0.0 if the geometry is unavailable.
double GetMorphPerimeter(const API_Guid& morphGuid);

#endif // _PHPURES_MORPH_UTILS_H_
