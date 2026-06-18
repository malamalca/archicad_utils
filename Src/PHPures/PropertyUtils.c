#include "PropertyUtils.h"

// Helper function to get property value by name from an element
bool GetElementPropertyValue(const API_Guid& elemGuid, const GS::UniString& propName, GS::UniString& value)
{
    GSErrCode err;
    GS::Array<API_PropertyDefinition> definitions;
    
    // First try user-defined properties
    err = ACAPI_Element_GetPropertyDefinitions(elemGuid, API_PropertyDefinitionFilter_UserDefined, definitions);
    if (err != NoError) {
        return false;
    }
    
    for (const API_PropertyDefinition& def : definitions) {
        if (def.name == propName) {
            API_Property property = {};
            
            err = ACAPI_Element_GetPropertyValue(elemGuid, def.guid, property);
            if (err == NoError && property.status == API_Property_HasValue) {
                err = ACAPI_Property_GetPropertyValueString(property, &value);
                if (err == NoError && !value.IsEmpty()) {
                    return true;
                }
            }
            break;
        }
    }
    
    // If not found in user-defined, try all properties (includes calculated)
    definitions.Clear();
    err = ACAPI_Element_GetPropertyDefinitions(elemGuid, API_PropertyDefinitionFilter_All, definitions);
    if (err == NoError) {
        for (const API_PropertyDefinition& def : definitions) {
            if (def.name == propName) {
                API_Property property = {};
                
                err = ACAPI_Element_GetPropertyValue(elemGuid, def.guid, property);
                if (err == NoError && property.status == API_Property_HasValue) {
                    err = ACAPI_Property_GetPropertyValueString(property, &value);
                    if (err == NoError && !value.IsEmpty()) {
                        return true;
                    }
                }
                break;
            }
        }
    }
    
    return false;
}

// Fetch multiple named property values in a single pair of definitions enumerations.
void GetElementPropertyValues(const API_Guid& elemGuid,
                              const GS::Array<GS::UniString>& propNames,
                              GS::HashTable<GS::UniString, GS::UniString>& outValues)
{
    outValues.Clear();

    // Scan a definition list, storing any requested name that has a non-empty value
    // and hasn't already been captured (preserves user-defined priority).
    auto collectFromFilter = [&](API_PropertyDefinitionFilter filter) {
        GS::Array<API_PropertyDefinition> definitions;
        if (ACAPI_Element_GetPropertyDefinitions(elemGuid, filter, definitions) != NoError) {
            return;
        }

        for (const API_PropertyDefinition& def : definitions) {
            if (!propNames.Contains(def.name) || outValues.ContainsKey(def.name)) {
                continue;
            }

            API_Property property = {};
            if (ACAPI_Element_GetPropertyValue(elemGuid, def.guid, property) == NoError &&
                property.status == API_Property_HasValue) {
                GS::UniString value;
                if (ACAPI_Property_GetPropertyValueString(property, &value) == NoError && !value.IsEmpty()) {
                    outValues.Add(def.name, value);
                }
            }
        }
    };

    collectFromFilter(API_PropertyDefinitionFilter_UserDefined);

    // Only do the (more expensive) full enumeration if something is still missing.
    if (outValues.GetSize() < propNames.GetSize()) {
        collectFromFilter(API_PropertyDefinitionFilter_All);
    }
}

// Helper function to read AutoText property value
bool GetAutoTextValue(const GS::UniString& propertyName, GS::UniString& value)
{
    GS::Array<GS::ArrayFB<GS::UniString, 3>> autotexts;
    API_AutotextType type = APIAutoText_Custom;
    
    GSErrCode err = ACAPI_AutoText_GetAutoTexts(&autotexts, type);
    if (err == NoError) {
        for (ULong i = 0; i < autotexts.GetSize(); i++) {
            if (autotexts[i][0].IsEqual(propertyName)) {
                value = autotexts[i][2];
                return true;
            }
        }
    }
    return false;
}
