#ifndef _PHPURES_REPORT_GENERATOR_H_
#define _PHPURES_REPORT_GENERATOR_H_

#include "APIEnvir.h"
#include "ACAPinc.h"
#include "JsonUpdater.h"

// Generate comprehensive change report
void GenerateChangeReport(
    const GS::Array<ChangeReport>& changeReports,
    const GS::Array<GS::UniString>& missingMorphIds,
    const GS::Array<GS::UniString>& missingZoneWarnings,
    const GS::Array<GS::UniString>& idMismatchWarnings);

#endif // _PHPURES_REPORT_GENERATOR_H_
