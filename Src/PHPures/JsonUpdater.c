#include "JsonUpdater.h"
#include "JSON/Value.hpp"
#include <cmath>

// Helper to reconstruct the key used in morphDataMap (must match MorphProcessor.c logic)
static GS::UniString GetMorphDataKey(const MorphFaceData& data)
{
    GS::UniString key;
    if (data.isTransparent) {
        key = data.zoneId + "|" + data.id + "|" + GS::ValueToUniString(data.povrsina) + "|" + 
              data.idKonstrukcije + "|" + data.idElementaVgradnje + "|" + data.delezOkvirja + "|" + data.dolzinaStekla;
    } else {
        key = data.zoneId + "|" + data.id + "|" + GS::ValueToUniString(data.povrsina) + "|" + 
              data.idKonstrukcije + "|" + data.orientacija + "|" + data.naklon;
    }
    return key;
}

// Helper to process non-transparent constructions
static void ProcessNonTransparentConstruction(
    JSON::ObjectValueRef zoneObj,
    const GS::UniString& zoneId,
    const GS::UniString& jsonZoneId,
    const GS::HashTable<GS::UniString, MorphFaceData>& morphDataMap,
    GS::HashSet<GS::UniString>& processedMorphIds,
    GS::Array<ChangeReport>& changeReports,
    GS::Array<GS::UniString>& missingMorphIds)
{
    ChangeReport report;
    report.id = zoneId;
    report.isTransparent = false;
    
    // Get old values
    if (zoneObj->HasMember("povrsina")) {
        JSON::ValueRef val = zoneObj->Get("povrsina");
        if (val != nullptr && val->IsNumber()) {
            JSON::NumberValueRef oldVal = JSON::NumberValue::Cast(val);
            if (oldVal != nullptr) report.oldPovrsina = oldVal->GetDouble();
        }
    }
    if (zoneObj->HasMember("obseg")) {
        report.hadObsegField = true;
        JSON::ValueRef val = zoneObj->Get("obseg");
        if (val != nullptr && val->IsNumber()) {
            JSON::NumberValueRef oldVal = JSON::NumberValue::Cast(val);
            if (oldVal != nullptr) report.oldObseg = oldVal->GetDouble();
        }
    }
    if (zoneObj->HasMember("stevilo")) {
        report.hadSteviloField = true;
        JSON::ValueRef val = zoneObj->Get("stevilo");
        if (val != nullptr && val->IsNumber()) {
            JSON::NumberValueRef oldVal = JSON::NumberValue::Cast(val);
            if (oldVal != nullptr) report.oldStevilo = (int)oldVal->GetInt32();
        }
    }
    if (zoneObj->HasMember("orientacija")) {
        report.hadOrientacijaField = true;
        JSON::ValueRef val = zoneObj->Get("orientacija");
        if (val != nullptr && val->IsString()) {
            JSON::StringValueRef oldVal = JSON::StringValue::Cast(val);
            if (oldVal != nullptr) report.oldOrientacija = oldVal->Get();
        }
    }
    if (zoneObj->HasMember("naklon")) {
        report.hadNaklonField = true;
        JSON::ValueRef val = zoneObj->Get("naklon");
        if (val != nullptr) {
            if (val->IsString()) {
                JSON::StringValueRef oldVal = JSON::StringValue::Cast(val);
                if (oldVal != nullptr) report.oldNaklon = oldVal->Get();
            } else if (val->IsNumber()) {
                JSON::NumberValueRef oldVal = JSON::NumberValue::Cast(val);
                if (oldVal != nullptr) report.oldNaklon = GS::ValueToUniString((int)oldVal->GetInt32());
            }
        }
    }
    if (zoneObj->HasMember("idKonstrukcije")) {
        report.hadIdKonstrukcijeField = true;
        JSON::ValueRef val = zoneObj->Get("idKonstrukcije");
        if (val != nullptr && val->IsString()) {
            JSON::StringValueRef oldVal = JSON::StringValue::Cast(val);
            if (oldVal != nullptr) report.oldIdKonstrukcije = oldVal->Get();
        }
    }

    double totalArea = 0.0;
    int totalCount = 0;
    double perimeter = 0.0;  // per single element
    GS::UniString orientation, slope, konstrukcija;
    bool foundMatch = false;

    for (const auto& pair : morphDataMap) {
        const MorphFaceData& data = pair.value;
        // Match by zone ID and construction ID
        if (!data.isTransparent && data.zoneId == jsonZoneId && data.id == zoneId) {
            totalArea += data.povrsina * data.stevilo;
            totalCount += data.stevilo;
            if (!foundMatch) {
                orientation = data.orientacija;
                slope = data.naklon;
                konstrukcija = data.idKonstrukcije;
                perimeter = data.obseg;
                foundMatch = true;
            }
            // Mark this specific entry as processed using the full morphDataMap key
            processedMorphIds.Add(GetMorphDataKey(data));
        }
    }
    
    if (foundMatch) {
        // Store new values
        report.newPovrsina = totalArea;
        report.newObseg = perimeter;
        report.newStevilo = totalCount;
        report.newOrientacija = orientation;
        report.newNaklon = slope;
        report.newIdKonstrukcije = konstrukcija;

        // Check if anything changed (only count reportable changes)
        bool areaChanged = std::abs(report.oldPovrsina - report.newPovrsina) > 0.001;
        bool obsegChanged = report.hadObsegField && std::abs(report.oldObseg - report.newObseg) > 0.001;
        bool steviloChanged = report.oldStevilo != report.newStevilo && (totalCount > 1 || report.hadSteviloField);
        bool orientacijaChanged = report.oldOrientacija != report.newOrientacija && orientation != "<Undefined>";
        bool naklonChanged = report.oldNaklon != report.newNaklon && slope != "<Undefined>" && (slope != "0" || report.hadNaklonField);
        bool konstrukcijaChanged = report.oldIdKonstrukcije != report.newIdKonstrukcije && !konstrukcija.IsEmpty() && konstrukcija != "<Undefined>";

        report.hasChanges = areaChanged || obsegChanged || steviloChanged || orientacijaChanged || naklonChanged || konstrukcijaChanged;

        // Update area if changed
        if (std::abs(report.oldPovrsina - report.newPovrsina) > 0.001) {
            if (zoneObj->HasMember("povrsina")) {
                zoneObj->Remove("povrsina");
            }
            zoneObj->Add("povrsina", totalArea);
        }

        // Update obseg (circumference) only if the key already exists in the JSON
        if (report.hadObsegField && std::abs(report.oldObseg - report.newObseg) > 0.001) {
            if (zoneObj->HasMember("obseg")) {
                zoneObj->Remove("obseg");
            }
            zoneObj->Add("obseg", perimeter);
        }
        
        // Only update stevilo if it changed AND (it's > 1 OR the field existed before)
        if (report.oldStevilo != report.newStevilo && (totalCount > 1 || report.hadSteviloField)) {
            if (zoneObj->HasMember("stevilo")) {
                zoneObj->Remove("stevilo");
            }
            zoneObj->Add("stevilo", (Int32)totalCount);
        }
        
        // Handle orientacija: skip if <Undefined>, remove if it becomes undefined
        if (!orientation.IsEmpty() && orientation != "<Undefined>") {
            if (report.oldOrientacija != report.newOrientacija) {
                if (zoneObj->HasMember("orientacija")) {
                    zoneObj->Remove("orientacija");
                }
                zoneObj->Add("orientacija", orientation);
            }
        } else if (orientation == "<Undefined>" && zoneObj->HasMember("orientacija")) {
            zoneObj->Remove("orientacija");
        }
        
        // Handle naklon: skip if <Undefined>, store as number
        if (!slope.IsEmpty() && slope != "<Undefined>") {
            if (report.oldNaklon != report.newNaklon) {
                double naklonVal = 0.0;
                if (slope.SScanf("%lf", &naklonVal) == 1) {
                    if (zoneObj->HasMember("naklon")) {
                        zoneObj->Remove("naklon");
                    }
                    zoneObj->Add("naklon", naklonVal);
                }
            }
        } else if (slope == "<Undefined>" && zoneObj->HasMember("naklon")) {
            zoneObj->Remove("naklon");
        }

        // Update idKonstrukcije if it changed in Archicad
        if (!konstrukcija.IsEmpty() && konstrukcija != "<Undefined>") {
            if (report.oldIdKonstrukcije != report.newIdKonstrukcije) {
                if (zoneObj->HasMember("idKonstrukcije")) {
                    zoneObj->Remove("idKonstrukcije");
                }
                zoneObj->Add("idKonstrukcije", konstrukcija);
            }
        }

        changeReports.Push(report);
    } else {
        // ID exists in JSON but no morph found
        missingMorphIds.Push(zoneId);
    }
}

// Helper to process transparent constructions
static void ProcessTransparentConstruction(
    JSON::ObjectValueRef zoneObj,
    const GS::UniString& zoneId,
    const GS::UniString& jsonZoneId,
    const GS::HashTable<GS::UniString, MorphFaceData>& morphDataMap,
    GS::HashSet<GS::UniString>& processedMorphIds,
    GS::Array<ChangeReport>& changeReports,
    GS::Array<GS::UniString>& missingMorphIds)
{
    ChangeReport report;
    report.id = zoneId;
    report.isTransparent = true;
    
    // Get old values including idElementaVgradnje and dolzinaStekla to match correctly
    GS::UniString oldIdElementaVgradnje;
    GS::UniString oldDolzinaStekla;
    if (zoneObj->HasMember("povrsina")) {
        JSON::ValueRef val = zoneObj->Get("povrsina");
        if (val != nullptr && val->IsNumber()) {
            JSON::NumberValueRef oldVal = JSON::NumberValue::Cast(val);
            if (oldVal != nullptr) report.oldPovrsina = oldVal->GetDouble();
        }
    }
    if (zoneObj->HasMember("obseg")) {
        report.hadObsegField = true;
        JSON::ValueRef val = zoneObj->Get("obseg");
        if (val != nullptr && val->IsNumber()) {
            JSON::NumberValueRef oldVal = JSON::NumberValue::Cast(val);
            if (oldVal != nullptr) report.oldObseg = oldVal->GetDouble();
        }
    }
    if (zoneObj->HasMember("stevilo")) {
        report.hadSteviloField = true;
        JSON::ValueRef val = zoneObj->Get("stevilo");
        if (val != nullptr && val->IsNumber()) {
            JSON::NumberValueRef oldVal = JSON::NumberValue::Cast(val);
            if (oldVal != nullptr) report.oldStevilo = (int)oldVal->GetInt32();
        }
    }
    if (zoneObj->HasMember("delezOkvirja")) {
        report.hadDelezOkvirjaField = true;
        JSON::ValueRef val = zoneObj->Get("delezOkvirja");
        if (val != nullptr) {
            if (val->IsString()) {
                JSON::StringValueRef oldVal = JSON::StringValue::Cast(val);
                if (oldVal != nullptr) report.oldDelezOkvirja = oldVal->Get();
            } else if (val->IsNumber()) {
                JSON::NumberValueRef oldVal = JSON::NumberValue::Cast(val);
                if (oldVal != nullptr) {
                    // Format to 2 decimal places to match property format
                    double numVal = oldVal->GetDouble();
                    report.oldDelezOkvirja = GS::UniString::Printf("%.2f", numVal);
                }
            }
        }
    }
    if (zoneObj->HasMember("dolzinaStekla")) {
        report.hadDolzinaSteklaField = true;
        JSON::ValueRef val = zoneObj->Get("dolzinaStekla");
        if (val != nullptr) {
            if (val->IsString()) {
                JSON::StringValueRef oldVal = JSON::StringValue::Cast(val);
                if (oldVal != nullptr) {
                    report.oldDolzinaStekla = oldVal->Get();
                    oldDolzinaStekla = oldVal->Get();
                }
            } else if (val->IsNumber()) {
                JSON::NumberValueRef oldVal = JSON::NumberValue::Cast(val);
                if (oldVal != nullptr) {
                    double numVal = oldVal->GetDouble();
                    report.oldDolzinaStekla = GS::UniString::Printf("%.2f", numVal);
                    oldDolzinaStekla = GS::UniString::Printf("%.2f", numVal);
                }
            }
        }
    }
    if (zoneObj->HasMember("idElementaVgradnje")) {
        report.hadIdElementaVgradnjeField = true;
        JSON::ValueRef val = zoneObj->Get("idElementaVgradnje");
        if (val != nullptr && val->IsString()) {
            JSON::StringValueRef oldVal = JSON::StringValue::Cast(val);
            if (oldVal != nullptr) {
                report.oldIdElementaVgradnje = oldVal->Get();
                oldIdElementaVgradnje = oldVal->Get();
            }
        }
    }
    if (zoneObj->HasMember("idKonstrukcije")) {
        report.hadIdKonstrukcijeField = true;
        JSON::ValueRef val = zoneObj->Get("idKonstrukcije");
        if (val != nullptr && val->IsString()) {
            JSON::StringValueRef oldVal = JSON::StringValue::Cast(val);
            if (oldVal != nullptr) report.oldIdKonstrukcije = oldVal->Get();
        }
    }

    double sumArea = 0.0;
    int totalCount = 0;
    double perimeter = 0.0;  // per single element
    GS::UniString framePercentage;
    GS::UniString elementVgradnje;
    GS::UniString dolzinaSteklaValue;
    GS::UniString konstrukcija;
    bool foundMatch = false;
    
    for (const auto& pair : morphDataMap) {
        const MorphFaceData& data = pair.value;
        // Match by zone ID, construction ID, AND idElementaVgradnje to avoid combining different elements
        // Check if idElementaVgradnje matches (or both empty/undefined)
        bool idElementaMatches = false;
        if (oldIdElementaVgradnje.IsEmpty() && (!data.hasIdElementaVgradnje || data.idElementaVgradnje.IsEmpty())) {
            idElementaMatches = true; // Both empty
        } else if (data.hasIdElementaVgradnje && data.idElementaVgradnje == oldIdElementaVgradnje) {
            idElementaMatches = true; // Both match
        }
        
        if (data.isTransparent && data.zoneId == jsonZoneId && data.id == zoneId && idElementaMatches) {
            sumArea += data.povrsina * data.stevilo;
            totalCount += data.stevilo;
            if (!foundMatch) {
                framePercentage = data.delezOkvirja;
                elementVgradnje = data.idElementaVgradnje;
                dolzinaSteklaValue = data.dolzinaStekla;
                konstrukcija = data.idKonstrukcije;
                perimeter = data.obseg;
                foundMatch = true;
            }
            // Mark this specific entry as processed using the full morphDataMap key
            processedMorphIds.Add(GetMorphDataKey(data));
        }
    }
    
    // Calculate per-unit area
    double perUnitArea = (totalCount > 0) ? (sumArea / totalCount) : 0.0;
    
    if (foundMatch) {
        // Store new values
        report.newPovrsina = perUnitArea;
        report.newObseg = perimeter;
        report.newStevilo = totalCount;
        report.newDelezOkvirja = framePercentage;
        report.newIdElementaVgradnje = elementVgradnje;
        report.newDolzinaStekla = dolzinaSteklaValue;
        report.newIdKonstrukcije = konstrukcija;

        // Check if anything changed (only count reportable changes)
        bool areaChanged = std::abs(report.oldPovrsina - report.newPovrsina) > 0.001;
        bool obsegChanged = report.hadObsegField && std::abs(report.oldObseg - report.newObseg) > 0.001;
        bool steviloChanged = report.oldStevilo != report.newStevilo && (totalCount > 1 || report.hadSteviloField);
        bool delezOkvirjaChanged = report.oldDelezOkvirja != report.newDelezOkvirja && framePercentage != "<Undefined>";
        bool dolzinaSteklaChanged = report.oldDolzinaStekla != report.newDolzinaStekla && dolzinaSteklaValue != "<Undefined>";
        bool idElementaVgradnjeChanged = report.oldIdElementaVgradnje != report.newIdElementaVgradnje && elementVgradnje != "<Undefined>";
        bool konstrukcijaChanged = report.oldIdKonstrukcije != report.newIdKonstrukcije && !konstrukcija.IsEmpty() && konstrukcija != "<Undefined>";

        report.hasChanges = areaChanged || obsegChanged || steviloChanged || delezOkvirjaChanged || dolzinaSteklaChanged || idElementaVgradnjeChanged || konstrukcijaChanged;

        // Update area if changed
        if (std::abs(report.oldPovrsina - report.newPovrsina) > 0.001) {
            if (zoneObj->HasMember("povrsina")) {
                zoneObj->Remove("povrsina");
            }
            zoneObj->Add("povrsina", perUnitArea);
        }

        // Update obseg (circumference) only if the key already exists in the JSON
        if (report.hadObsegField && std::abs(report.oldObseg - report.newObseg) > 0.001) {
            if (zoneObj->HasMember("obseg")) {
                zoneObj->Remove("obseg");
            }
            zoneObj->Add("obseg", perimeter);
        }
        
        // Only update stevilo if it changed AND (it's > 1 OR the field existed before)
        if (report.oldStevilo != report.newStevilo && (totalCount > 1 || report.hadSteviloField)) {
            if (zoneObj->HasMember("stevilo")) {
                zoneObj->Remove("stevilo");
            }
            zoneObj->Add("stevilo", (Int32)totalCount);
        }
        
        // Handle delezOkvirja: skip if <Undefined>, remove if it becomes undefined
        if (!framePercentage.IsEmpty() && framePercentage != "<Undefined>") {
            if (report.oldDelezOkvirja != report.newDelezOkvirja) {
                if (zoneObj->HasMember("delezOkvirja")) {
                    zoneObj->Remove("delezOkvirja");
                }
                double delezOkvirjaFloat = 0.0;
                framePercentage.SScanf("%lf", &delezOkvirjaFloat);
                zoneObj->Add("delezOkvirja", delezOkvirjaFloat);
            }
        } else if (framePercentage == "<Undefined>" && zoneObj->HasMember("delezOkvirja")) {
            zoneObj->Remove("delezOkvirja");
        }
        
        // Handle dolzinaStekla: skip if <Undefined>, remove if it becomes undefined
        if (!dolzinaSteklaValue.IsEmpty() && dolzinaSteklaValue != "<Undefined>") {
            if (report.oldDolzinaStekla != report.newDolzinaStekla) {
                if (zoneObj->HasMember("dolzinaStekla")) {
                    zoneObj->Remove("dolzinaStekla");
                }
                double dolzinaSteklaFloat = 0.0;
                dolzinaSteklaValue.SScanf("%lf", &dolzinaSteklaFloat);
                zoneObj->Add("dolzinaStekla", dolzinaSteklaFloat);
            }
        } else if (dolzinaSteklaValue == "<Undefined>" && zoneObj->HasMember("dolzinaStekla")) {
            zoneObj->Remove("dolzinaStekla");
        }
        
        // Handle idElementaVgradnje: add/update if present and valid
        if (!elementVgradnje.IsEmpty() && elementVgradnje != "<Undefined>") {
            if (report.oldIdElementaVgradnje != report.newIdElementaVgradnje) {
                if (zoneObj->HasMember("idElementaVgradnje")) {
                    zoneObj->Remove("idElementaVgradnje");
                }
                zoneObj->Add("idElementaVgradnje", elementVgradnje);
            }
        } else if (elementVgradnje == "<Undefined>" && zoneObj->HasMember("idElementaVgradnje")) {
            zoneObj->Remove("idElementaVgradnje");
        }

        // Update idKonstrukcije if it changed in Archicad
        if (!konstrukcija.IsEmpty() && konstrukcija != "<Undefined>") {
            if (report.oldIdKonstrukcije != report.newIdKonstrukcije) {
                if (zoneObj->HasMember("idKonstrukcije")) {
                    zoneObj->Remove("idKonstrukcije");
                }
                zoneObj->Add("idKonstrukcije", konstrukcija);
            }
        }

        changeReports.Push(report);
    } else {
        // ID exists in JSON but no morph found
        missingMorphIds.Push(zoneId);
    }
}

// Process all constructions inside a single zone's 'ovoj' object.
// The owning zone id is known by the caller, so no lookup is required.
static void ProcessOvojConstructions(
    JSON::ObjectValueRef ovojObject,
    const GS::UniString& jsonZoneId,
    const GS::HashTable<GS::UniString, MorphFaceData>& morphDataMap,
    GS::HashSet<GS::UniString>& processedMorphIds,
    GS::Array<ChangeReport>& changeReports,
    GS::Array<GS::UniString>& missingMorphIds)
{
    if (ovojObject == nullptr) return;

    // Non-transparent constructions
    if (ovojObject->HasMember("netransparentneKonstrukcije")) {
        JSON::ArrayValueRef nonTransparentArray = JSON::ArrayValue::Cast(ovojObject->Get("netransparentneKonstrukcije"));
        if (nonTransparentArray != nullptr) {
            for (UIndex i = 0; i < nonTransparentArray->GetSize(); i++) {
                JSON::ObjectValueRef zoneObj = JSON::ObjectValue::Cast(nonTransparentArray->Get(i));
                if (zoneObj != nullptr && zoneObj->HasMember("id")) {
                    GS::UniString zoneId;
                    zoneObj->GetMandatoryMember("id", zoneId);
                    ProcessNonTransparentConstruction(zoneObj, zoneId, jsonZoneId, morphDataMap,
                                                      processedMorphIds, changeReports, missingMorphIds);
                }
            }
        }
    }

    // Transparent constructions
    if (ovojObject->HasMember("transparentneKonstrukcije")) {
        JSON::ArrayValueRef transparentArray = JSON::ArrayValue::Cast(ovojObject->Get("transparentneKonstrukcije"));
        if (transparentArray != nullptr) {
            for (UIndex i = 0; i < transparentArray->GetSize(); i++) {
                JSON::ObjectValueRef zoneObj = JSON::ObjectValue::Cast(transparentArray->Get(i));
                if (zoneObj != nullptr && zoneObj->HasMember("id")) {
                    GS::UniString zoneId;
                    zoneObj->GetMandatoryMember("id", zoneId);
                    ProcessTransparentConstruction(zoneObj, zoneId, jsonZoneId, morphDataMap,
                                                   processedMorphIds, changeReports, missingMorphIds);
                }
            }
        }
    }
}

// Update JSON with morph data - existing entries
GSErrCode UpdateExistingJsonEntries(
    JSON::ObjectValueRef& ovojObject,
    JSON::ArrayValueRef& rootArray,
    const GS::HashTable<GS::UniString, MorphFaceData>& morphDataMap,
    GS::HashSet<GS::UniString>& processedMorphIds,
    GS::Array<ChangeReport>& changeReports,
    GS::Array<GS::UniString>& missingMorphIds)
{
    processedMorphIds.Clear();
    changeReports.Clear();
    missingMorphIds.Clear();

    try {
        if (rootArray != nullptr) {
            // Array root: process every zone's ovoj, using each zone's own id
            for (USize j = 0; j < rootArray->GetSize(); j++) {
                JSON::ObjectValueRef zone = JSON::ObjectValue::Cast(rootArray->Get(j));
                if (zone == nullptr || !zone->HasMember("ovoj")) continue;

                JSON::ObjectValueRef ovoj = JSON::ObjectValue::Cast(zone->Get("ovoj"));
                if (ovoj == nullptr) continue;

                GS::UniString jsonZoneId;
                if (zone->HasMember("id")) {
                    zone->GetMandatoryMember("id", jsonZoneId);
                }

                ProcessOvojConstructions(ovoj, jsonZoneId, morphDataMap,
                                         processedMorphIds, changeReports, missingMorphIds);
            }
        } else {
            // Single-object root: the extracted ovoj is the only zone available
            ProcessOvojConstructions(ovojObject, GS::UniString(), morphDataMap,
                                     processedMorphIds, changeReports, missingMorphIds);
        }

    } catch (const GS::Exception&) {
        ACAPI_WriteReport("", true);
        ACAPI_WriteReport("ERROR: GS Exception during JSON processing", true);
        return APIERR_GENERAL;
    } catch (...) {
        ACAPI_WriteReport("", true);
        ACAPI_WriteReport("ERROR: Unknown exception during JSON processing", true);
        return APIERR_GENERAL;
    }
    
    return NoError;
}

// Add new morphs to JSON that exist in Archicad but not in JSON
GSErrCode AddNewMorphsToJson(
    JSON::ArrayValueRef& rootArray,
    const GS::HashTable<GS::UniString, MorphFaceData>& morphDataMap,
    const GS::HashSet<GS::UniString>& processedMorphIds,
    GS::Array<ChangeReport>& changeReports,
    GS::Array<GS::UniString>& missingZoneWarnings)
{
    ACAPI_WriteReport("", false);
    ACAPI_WriteReport("Checking for new morphs to add...", false);
    
    GS::Array<GS::UniString> newMorphKeys;
    missingZoneWarnings.Clear();
    
    // Check each morphDataMap entry using its full key
    for (const auto& pair : morphDataMap) {
        const MorphFaceData& data = pair.value;
        GS::UniString fullKey = GetMorphDataKey(data);
        
        if (!processedMorphIds.Contains(fullKey)) {
            newMorphKeys.Push(fullKey);
        }
    }
    
    if (!newMorphKeys.IsEmpty()) {
        ACAPI_WriteReport("Adding %d new construction(s) to JSON", false, (int)newMorphKeys.GetSize());
        
        // Process each new morph using the full key
        for (const auto& fullKey : newMorphKeys) {
            // Get the morph data from morphDataMap
            const MorphFaceData& data = morphDataMap[fullKey];
            
            // Find zone in JSON using data's zoneId
            JSON::ObjectValueRef targetZone = nullptr;
            if (rootArray != nullptr) {
                for (USize i = 0; i < rootArray->GetSize(); i++) {
                    JSON::ValueRef item = rootArray->Get(i);
                    if (item != nullptr && item->IsObject()) {
                        JSON::ObjectValueRef zoneObj = JSON::ObjectValue::Cast(item);
                        if (zoneObj != nullptr && zoneObj->HasMember("id")) {
                            GS::UniString jsonZoneId;
                            zoneObj->GetMandatoryMember("id", jsonZoneId);
                            if (jsonZoneId == data.zoneId) {
                                targetZone = zoneObj;
                                break;
                            }
                        }
                    }
                }
            }
            
            if (targetZone == nullptr) {
                GS::UniString warning = GS::UniString::Printf("Zone '%s' not found for morph '%s'", 
                                                              data.zoneId.ToCStr().Get(), data.id.ToCStr().Get());
                missingZoneWarnings.Push(warning);
                ACAPI_WriteReport("Warning: %s", true, warning.ToCStr().Get());
                continue;
            }
            
            // Get or create 'ovoj' object
            if (!targetZone->HasMember("ovoj")) {
                targetZone->Add("ovoj", JSON::ObjectValueRef(new JSON::ObjectValue()));
            }
            
            JSON::ValueRef ovojVal = targetZone->Get("ovoj");
            if (ovojVal == nullptr || !ovojVal->IsObject()) continue;
            
            JSON::ObjectValueRef ovojObj = JSON::ObjectValue::Cast(ovojVal);
            
            // Get or create appropriate array
            const char* arrayName = data.isTransparent ? "transparentneKonstrukcije" : "netransparentneKonstrukcije";
            
            if (!ovojObj->HasMember(arrayName)) {
                ovojObj->Add(arrayName, JSON::ArrayValueRef(new JSON::ArrayValue()));
            }
            
            JSON::ValueRef arrVal = ovojObj->Get(arrayName);
            if (arrVal == nullptr || !arrVal->IsArray()) continue;
            
            JSON::ArrayValueRef konstrukcijeArray = JSON::ArrayValue::Cast(arrVal);
            
            // Build the construction object
            JSON::ObjectValueRef newKonstrukcija = JSON::ObjectValueRef(new JSON::ObjectValue());
            newKonstrukcija->Add("id", data.id);
            newKonstrukcija->Add("idKonstrukcije", data.idKonstrukcije);
            newKonstrukcija->Add("povrsina", data.povrsina);
            
            if (!data.isTransparent) {
                // Non-transparent properties
                if (data.hasOrientacija && !data.orientacija.IsEmpty() && data.orientacija != "<Undefined>") {
                    newKonstrukcija->Add("orientacija", data.orientacija);
                }
                if (data.hasNaklon && !data.naklon.IsEmpty() && data.naklon != "<Undefined>") {
                    double naklonVal = 0.0;
                    if (data.naklon.SScanf("%lf", &naklonVal) == 1) {
                        newKonstrukcija->Add("naklon", naklonVal);
                    }
                }
                if (data.stevilo > 1) {
                    newKonstrukcija->Add("stevilo", (Int32)data.stevilo);
                }
            } else {
                // Transparent properties
                if (data.hasIdElementaVgradnje && !data.idElementaVgradnje.IsEmpty() && data.idElementaVgradnje != "<Undefined>") {
                    newKonstrukcija->Add("idElementaVgradnje", data.idElementaVgradnje);
                }
                if (data.stevilo > 1) {
                    newKonstrukcija->Add("stevilo", (Int32)data.stevilo);
                }
                if (data.hasDelezOkvirja && !data.delezOkvirja.IsEmpty() && data.delezOkvirja != "<Undefined>") {
                    double delezOkvirjaFloat = 0.0;
                    data.delezOkvirja.SScanf("%lf", &delezOkvirjaFloat);
                    newKonstrukcija->Add("delezOkvirja", delezOkvirjaFloat);
                }
                if (data.hasDolzinaStekla && !data.dolzinaStekla.IsEmpty() && data.dolzinaStekla != "<Undefined>") {
                    double dolzinaSteklaFloat = 0.0;
                    data.dolzinaStekla.SScanf("%lf", &dolzinaSteklaFloat);
                    newKonstrukcija->Add("dolzinaStekla", dolzinaSteklaFloat);
                }
            }
            
            // Use AddValue() instead of Add() for ValueRef objects
            konstrukcijeArray->AddValue(newKonstrukcija);
            
            // Create report for new addition
            ChangeReport report;
            report.id = data.id;
            report.isTransparent = data.isTransparent;
            report.oldPovrsina = 0.0;
            report.newPovrsina = data.povrsina;
            report.oldStevilo = 0;
            report.newStevilo = data.stevilo;
            report.oldOrientacija = "";
            report.newOrientacija = data.hasOrientacija ? data.orientacija : "";
            report.oldNaklon = "";
            report.newNaklon = data.hasNaklon ? data.naklon : "";
            report.oldDelezOkvirja = "";
            report.newDelezOkvirja = data.hasDelezOkvirja ? data.delezOkvirja : "";
            report.oldDolzinaStekla = "";
            report.newDolzinaStekla = data.hasDolzinaStekla ? data.dolzinaStekla : "";
            report.oldIdElementaVgradnje = "";
            report.newIdElementaVgradnje = data.hasIdElementaVgradnje ? data.idElementaVgradnje : "";
            report.hasChanges = true;
            changeReports.Push(report);
        }
    }
    
    return NoError;
}
