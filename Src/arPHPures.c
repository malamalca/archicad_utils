#include "APIEnvir.h"
#include "ACAPinc.h"
#include "APICommon.h"
#include "DG.h"

#include "arPhPures.h"

// PHPures library modules
#include "PHPures/PropertyUtils.h"
#include "PHPures/MorphUtils.h"
#include "PHPures/JsonUtils.h"
#include "PHPures/MorphProcessor.h"
#include "PHPures/JsonUpdater.h"
#include "PHPures/ReportGenerator.h"

#include "HashTable.hpp"
#include "HashSet.hpp"

// Main sync function
GSErrCode SyncPhpuresZones()
{
    GSErrCode err = NoError;
    
    // Show confirmation dialog
    DGAlert(DG_INFORMATION, "PHPures Sync", "Starting PHPures zones synchronization...", "", "OK");
    
    ACAPI_WriteReport("=== SyncPhpuresZones Started ===", false);
    
    // Step 1: Read zone file path from AutoText property
    GS::UniString zoneFilePath;
    if (!GetAutoTextValue("PHPuresZonesFile", zoneFilePath)) {
        DGAlert(DG_ERROR, "PHPures Sync", "AutoText property 'PHPuresZonesFile' not found", "Please create a Custom AutoText named 'PHPuresZonesFile' containing the path to your zones JSON file", "OK");
        ACAPI_WriteReport("AutoText property 'PHPuresZonesFile' not found", true);
        ACAPI_WriteReport("Please create a Custom AutoText named 'PHPuresZonesFile' containing the path to your zones JSON file", true);
        return APIERR_BADPROPERTY;
    }
    
    if (zoneFilePath.IsEmpty()) {
        DGAlert(DG_ERROR, "PHPures Sync", "AutoText property 'PHPuresZonesFile' is empty", "", "OK");
        ACAPI_WriteReport("AutoText property 'PHPuresZonesFile' is empty", true);
        return APIERR_BADVALUE;
    }
    
    GS::UniString msg = GS::UniString::Printf("Zone file: %s", zoneFilePath.ToCStr().Get());
    DGAlert(DG_INFORMATION, "PHPures Sync", msg, "", "OK");
    
    ACAPI_WriteReport("Zone file path: %s", false, zoneFilePath.ToCStr().Get());
    
    // Step 2: Process all morphs and extract their data
    GS::HashTable<GS::UniString, MorphFaceData> morphDataMap;
    int totalFaces = 0;
    int morphsWithProperties = 0;
    
    err = ProcessMorphElements(morphDataMap, totalFaces, morphsWithProperties);
    if (err != NoError) {
        return err;
    }
    
    // Step 3: Read and parse JSON zone file
    JSON::ValueRef parsedValue;
    err = ReadJsonFile(zoneFilePath, parsedValue);
    if (err != NoError) {
        return err;
    }
    
    // Step 4: Extract ovoj object from JSON
    JSON::ObjectValueRef ovojObject;
    JSON::ArrayValueRef rootArray;
    err = ExtractOvojObject(parsedValue, ovojObject, rootArray);
    if (err != NoError) {
        return err;
    }
    
    // Step 5: Update JSON with morph data
    GS::HashSet<GS::UniString> processedMorphIds;
    GS::Array<ChangeReport> changeReports;
    GS::Array<GS::UniString> missingMorphIds;
    
    err = UpdateExistingJsonEntries(ovojObject, rootArray, morphDataMap, processedMorphIds, changeReports, missingMorphIds);
    if (err != NoError) {
        return err;
    }
    
    // Step 6: Add new morphs to JSON
    GS::Array<GS::UniString> missingZoneWarnings;
    err = AddNewMorphsToJson(rootArray, morphDataMap, processedMorphIds, changeReports, missingZoneWarnings);
    if (err != NoError) {
        return err;
    }
    
    // Step 7: Generate comprehensive change report
    GenerateChangeReport(changeReports, missingMorphIds, missingZoneWarnings);
    
    // Step 8: Write updated JSON back to file
    ACAPI_WriteReport("STEP 8: Writing JSON file...", false);
    
    // Safety check: don't write if no morphs were processed
    if (morphDataMap.IsEmpty()) {
        ACAPI_WriteReport("WARNING: No morphs with ID property found - JSON file NOT updated", true);
        ACAPI_WriteReport("Make sure your morphs have a user-defined property called 'ID'", true);
        return APIERR_BADPROPERTY;
    }
    
    // Validate JSON structure before writing
    if (parsedValue == nullptr || 
        (parsedValue->IsArray() && JSON::ArrayValue::Cast(parsedValue)->GetSize() == 0) ||
        ovojObject == nullptr) {
        ACAPI_WriteReport("ERROR: JSON structure corrupted - NOT writing file to prevent data loss", true);
        return APIERR_BADVALUE;
    }
    
    err = WriteJsonFile(zoneFilePath, parsedValue);
    if (err != NoError) {
        ACAPI_WriteReport("Failed to write updated JSON to file", true);
        return err;
    }
    
    ACAPI_WriteReport("", false);
    ACAPI_WriteReport("=== SyncPhpuresZones Completed Successfully ===", false);
    ACAPI_WriteReport("Zone file updated: %s", false, zoneFilePath.ToCStr().Get());
    ACAPI_WriteReport("Total unique morph faces processed: %d", false, morphDataMap.GetSize());
    
    return NoError;
}
