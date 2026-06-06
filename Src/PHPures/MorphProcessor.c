#include "MorphProcessor.h"
#include "PropertyUtils.h"
#include "MorphUtils.h"
#include "APICommon.h"
#include <cmath>

// Process all morphs in the project and extract their data
GSErrCode ProcessMorphElements(GS::HashTable<GS::UniString, MorphFaceData>& morphDataMap, 
                               int& totalFaces, 
                               int& morphsWithProperties)
{
    totalFaces = 0;
    morphsWithProperties = 0;
    morphDataMap.Clear();
    
    // Get all morphs from entire project (all floors)
    GS::Array<API_Guid> morphList;
    GSErrCode err = ACAPI_Element_GetElemList(API_MorphID, &morphList, APIFilt_None);
    
    if (err != NoError) {
        WriteReport_Err("Failed to get morph list", err);
        return err;
    }
    
    ACAPI_WriteReport("Found %d morph elements in project", false, morphList.GetSize());
    
    if (morphList.GetSize() == 0) {
        ACAPI_WriteReport("WARNING: No morph elements exist in this project", true);
        return APIERR_BADVALUE;
    }
    
    // Process each morph and extract properties
    for (const API_Guid& morphGuid : morphList) {
        API_Element element;
        BNZeroMemory(&element, sizeof(API_Element));
        element.header.guid = morphGuid;
        
        err = ACAPI_Element_Get(&element);
        if (err != NoError) continue;
        
        // Get element ID from info string
        GS::UniString faceId;
        err = ACAPI_Element_GetElementInfoString(&morphGuid, &faceId);
        
        // Skip if failed or empty
        if (err != NoError || faceId.IsEmpty()) {
            continue; // Skip morphs without ID
        }
        
        // Trim whitespace and skip if empty
        faceId.Trim();
        if (faceId.IsEmpty()) {
            continue; // Skip morphs with empty ID
        }
        
        morphsWithProperties++;
        
        // Get zone ID from ID Zone property (in PHPures group)
        GS::UniString zoneId;
        if (!GetElementPropertyValue(morphGuid, "ID Zone", zoneId) || zoneId.IsEmpty()) {
            ACAPI_WriteReport("Skipping morph '%s': Missing or empty 'ID Zone' property", true, faceId.ToCStr().Get());
            continue; // Skip morphs without zone assignment
        }
        zoneId.Trim();
        
        // Get properties from morph element
        GS::UniString idKonstrukcije, idOknaVrata, idElementaVgradnje;
        GS::UniString orientacija, naklon, delezOkvirja, dolzinaStekla;
        
        GetElementPropertyValue(morphGuid, "ID Konstrukcije", idKonstrukcije);
        bool hasIdOknaVrata = GetElementPropertyValue(morphGuid, "ID OknaVrata", idOknaVrata);
        bool hasIdElementaVgradnje = GetElementPropertyValue(morphGuid, "ID Elementa vgradnje", idElementaVgradnje);
        bool hasOrientacija = GetElementPropertyValue(morphGuid, "Orientacija", orientacija);
        bool hasNaklon = GetElementPropertyValue(morphGuid, "Naklon", naklon);
        bool hasDelezOkvirja = GetElementPropertyValue(morphGuid, "Delež okvirja", delezOkvirja);
        bool hasDolzinaStekla = GetElementPropertyValue(morphGuid, "Dolžina stekla", dolzinaStekla);
        
        // Get morph surface area using Archicad API
        double areaM2 = GetMorphSurfaceArea(morphGuid);
        if (areaM2 <= 0.0) {
            continue; // Skip morphs with invalid area
        }
        
        // Round to 2 decimal places
        areaM2 = std::round(areaM2 * 100.0) / 100.0;
        
        // Create unique key from normalized values so counts are per exported record
        GS::UniString key;
        if (hasIdOknaVrata && !idOknaVrata.IsEmpty()) {
            GS::UniString normIdOknaVrata = idOknaVrata;
            GS::UniString normIdElementaVgradnje;
            GS::UniString normDelezOkvirja;
            GS::UniString normDolzinaStekla;

            normIdOknaVrata.Trim();

            if (hasIdElementaVgradnje) {
                normIdElementaVgradnje = idElementaVgradnje;
                normIdElementaVgradnje.Trim();
            }

            if (hasDelezOkvirja) {
                GS::UniString tmpDelez = delezOkvirja;
                tmpDelez.Trim();

                double delezValue = 0.0;
                if (!tmpDelez.IsEmpty() && tmpDelez.SScanf("%lf", &delezValue) == 1) {
                    normDelezOkvirja = GS::UniString::Printf("%.2f", delezValue);
                } else {
                    normDelezOkvirja = tmpDelez;
                }
            }

            if (hasDolzinaStekla) {
                GS::UniString tmpDolzina = dolzinaStekla;
                tmpDolzina.Trim();

                double dolzinaValue = 0.0;
                if (!tmpDolzina.IsEmpty() && tmpDolzina.SScanf("%lf", &dolzinaValue) == 1) {
                    normDolzinaStekla = GS::UniString::Printf("%.2f", dolzinaValue);
                } else {
                    normDolzinaStekla = tmpDolzina;
                }
            }

            key = zoneId + "|" + faceId + "|" + GS::ValueToUniString(areaM2) + "|" + normIdOknaVrata + "|" + normIdElementaVgradnje + "|" + normDelezOkvirja + "|" + normDolzinaStekla;
        } else {
            GS::UniString normIdKonstrukcije = idKonstrukcije;
            GS::UniString normOrientacija = orientacija;
            GS::UniString normNaklon = naklon;

            normIdKonstrukcije.Trim();
            normOrientacija.Trim();
            normNaklon.Trim();

            key = zoneId + "|" + faceId + "|" + GS::ValueToUniString(areaM2) + "|" + normIdKonstrukcije + "|" + normOrientacija + "|" + normNaklon;
        }
        
        // Store or update face data
        if (!morphDataMap.ContainsKey(key)) {
            MorphFaceData faceData;
            faceData.id = faceId;
            faceData.zoneId = zoneId;
            faceData.povrsina = areaM2;
            
            // Determine if transparent or non-transparent based on properties
            if (hasIdOknaVrata && !idOknaVrata.IsEmpty()) {
                faceData.isTransparent = true;
                faceData.idKonstrukcije = idOknaVrata;
                if (hasIdElementaVgradnje && !idElementaVgradnje.IsEmpty()) {
                    faceData.idElementaVgradnje = idElementaVgradnje;
                    faceData.hasIdElementaVgradnje = true;
                }
                if (hasDelezOkvirja) {
                    // Normalize delezOkvirja format (calculated property may return varying precision)
                    // Parse as number and format to 2 decimal places for consistent comparison
                    double delezValue = 0.0;
                    if (delezOkvirja.SScanf("%lf", &delezValue) == 1) {
                        faceData.delezOkvirja = GS::UniString::Printf("%.2f", delezValue);
                    } else {
                        faceData.delezOkvirja = delezOkvirja; // Keep original if parsing fails
                    }
                    faceData.hasDelezOkvirja = true;
                }
                if (hasDolzinaStekla) {
                    // Normalize dolzinaStekla format
                    double dolzinaValue = 0.0;
                    if (dolzinaStekla.SScanf("%lf", &dolzinaValue) == 1) {
                        faceData.dolzinaStekla = GS::UniString::Printf("%.2f", dolzinaValue);
                    } else {
                        faceData.dolzinaStekla = dolzinaStekla; // Keep original if parsing fails
                    }
                    faceData.hasDolzinaStekla = true;
                }
            } else {
                faceData.isTransparent = false;
                faceData.idKonstrukcije = idKonstrukcije;
                if (hasOrientacija) {
                    faceData.orientacija = orientacija;
                    faceData.hasOrientacija = true;
                }
                if (hasNaklon) {
                    faceData.naklon = naklon;
                    faceData.hasNaklon = true;
                }
            }
            
            faceData.stevilo = 1;
            morphDataMap.Add(key, faceData);
        } else {
            // Increment count for identical morphs
            MorphFaceData& existingData = morphDataMap[key];
            existingData.stevilo++;
        }
        
        totalFaces++;
    }
    
    ACAPI_WriteReport("Processed %d total morphs from %d morphs with properties", false, totalFaces, morphsWithProperties);
    
    return NoError;
}
