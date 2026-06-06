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
 * at each grid point via ray-casting against the 3D scene mesh.
 *
 * Each sensor element is divided into divisionsX × divisionsY squares
 * (read from GDL parameters A, B, divisionsX, divisionsY). The center
 * of each square is analyzed independently. If divisionsX/divisionsY
 * are not found, defaults to 1×1 (single-point analysis).
 *
 * Results are written into the GDL array parameter "solarization" as
 * a flat 1D array of nSquares × 12 values (row-major order:
 * square(0,0) months[1..12], square(0,1) months[1..12], ...).
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
