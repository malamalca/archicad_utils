#ifndef SOLARANALYSIS_HPP
#define SOLARANALYSIS_HPP

#include "GSRoot.hpp"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RunSolarAnalysis
 * ────────────────
 * Finds all placed objects matching `libpartName`, computes monthly
 * solar exposure (direct sunlight minutes on the 21st of each month)
 * at each object's location via ray-casting against the 3D scene mesh,
 * and writes the 12-month result into the GDL array parameter
 * "solarization" on every matched element.
 *
 * Parameters:
 *   libpartName      - GDL object name to search for (case-insensitive)
 *   latitudeDeg      - Project latitude in degrees  (+N / −S)
 *   longitudeDeg     - Project longitude in degrees (+E / −W)
 *   utcOffsetHours   - Timezone offset from UTC (e.g. +1.0 for CET)
 *
 * Returns: NoError on success, APIERR_BADPARS if no 3D model is open,
 *          APIERR_BADID if no matching elements are found.
 */
GSErrCode RunSolarAnalysis(
    const GS::UniString& libpartName,
    double               latitudeDeg,
    double               longitudeDeg,
    double               utcOffsetHours);

#ifdef __cplusplus
}
#endif

#endif /* SOLARANALYSIS_HPP */
