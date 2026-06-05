#include "ReportGenerator.h"
#include "DG.h"
#include <cmath>

// Generate comprehensive change report
void GenerateChangeReport(
    const GS::Array<ChangeReport>& changeReports,
    const GS::Array<GS::UniString>& missingMorphIds,
    const GS::Array<GS::UniString>& missingZoneWarnings)
{
    ACAPI_WriteReport("", false);
    ACAPI_WriteReport("========================================", false);
    ACAPI_WriteReport("SYNCHRONIZATION REPORT", false);
    ACAPI_WriteReport("========================================", false);
    ACAPI_WriteReport("", false);
    
    // Report changes
    int changesCount = 0;
    int unchangedCount = 0;
    
    for (const auto& report : changeReports) {
        if (report.hasChanges) {
            changesCount++;
            ACAPI_WriteReport("CHANGED: %s [%s]", false, 
                            report.id.ToCStr().Get(), 
                            report.isTransparent ? "Transparent" : "Non-transparent");
            
            if (std::abs(report.oldPovrsina - report.newPovrsina) > 0.001) {
                ACAPI_WriteReport("  povrsina: %.2f m² -> %.2f m²", false, 
                                report.oldPovrsina, report.newPovrsina);
            }
            
            if (report.oldStevilo != report.newStevilo && (report.newStevilo > 1 || report.hadSteviloField)) {
                ACAPI_WriteReport("  stevilo: %d -> %d", false, 
                                report.oldStevilo, report.newStevilo);
            }
            
            if (!report.isTransparent) {
                if (report.oldOrientacija != report.newOrientacija && report.newOrientacija != "<Undefined>") {
                    ACAPI_WriteReport("  orientacija: '%s' -> '%s'", false, 
                                    report.oldOrientacija.ToCStr().Get(), 
                                    report.newOrientacija.ToCStr().Get());
                } else if (report.newOrientacija == "<Undefined>" && report.hadOrientacijaField) {
                    ACAPI_WriteReport("  orientacija: '%s' -> (removed)", false, 
                                    report.oldOrientacija.ToCStr().Get());
                }
                
                if (report.oldNaklon != report.newNaklon && report.newNaklon != "<Undefined>") {
                    ACAPI_WriteReport("  naklon: '%s' -> '%s'", false, 
                                    report.oldNaklon.ToCStr().Get(), 
                                    report.newNaklon.ToCStr().Get());
                } else if (report.newNaklon == "<Undefined>" && report.hadNaklonField) {
                    ACAPI_WriteReport("  naklon: '%s' -> (removed)", false, 
                                    report.oldNaklon.ToCStr().Get());
                }
            } else {
                if (report.oldDelezOkvirja != report.newDelezOkvirja && report.newDelezOkvirja != "<Undefined>") {
                    ACAPI_WriteReport("  delezOkvirja: '%s' -> '%s'", false, 
                                    report.oldDelezOkvirja.ToCStr().Get(), 
                                    report.newDelezOkvirja.ToCStr().Get());
                } else if (report.newDelezOkvirja == "<Undefined>" && report.hadDelezOkvirjaField) {
                    ACAPI_WriteReport("  delezOkvirja: '%s' -> (removed)", false, 
                                    report.oldDelezOkvirja.ToCStr().Get());
                }
            }
            ACAPI_WriteReport("", false);
        } else {
            unchangedCount++;
        }
    }
    
    ACAPI_WriteReport("Summary:", false);
    ACAPI_WriteReport("  Elements changed: %d", false, changesCount);
    ACAPI_WriteReport("  Elements unchanged: %d", false, unchangedCount);
    ACAPI_WriteReport("", false);
    
    // Report missing zones for new morphs
    if (!missingZoneWarnings.IsEmpty()) {
        ACAPI_WriteReport("WARNINGS: Zones not found for new morphs:", false);
        for (const auto& warning : missingZoneWarnings) {
            ACAPI_WriteReport("  - %s", false, warning.ToCStr().Get());
        }
        ACAPI_WriteReport("", false);
    }
    
    // Report missing morphs
    if (!missingMorphIds.IsEmpty()) {
        ACAPI_WriteReport("WARNING: IDs in JSON without linked morphs:", true);
        for (const auto& id : missingMorphIds) {
            ACAPI_WriteReport("  - %s", true, id.ToCStr().Get());
        }
        ACAPI_WriteReport("", false);
        
        // Show in dialog
        GS::UniString dialogMsg = "The following IDs from the zone file do not have corresponding morphs in the project:\n\n";
        for (USize i = 0; i < missingMorphIds.GetSize() && i < 10; i++) {
            dialogMsg += "  - " + missingMorphIds[i] + "\n";
        }
        if (missingMorphIds.GetSize() > 10) {
            dialogMsg += GS::UniString::Printf("\n... and %d more (see Report window for full list)", 
                                              (int)(missingMorphIds.GetSize() - 10));
        }
        DGAlert(DG_WARNING, "Missing Morphs", dialogMsg, "", "OK");
    }
    
    ACAPI_WriteReport("========================================", false);
    ACAPI_WriteReport("", false);
}
