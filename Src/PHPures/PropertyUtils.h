#ifndef _PHPURES_PROPERTY_UTILS_H_
#define _PHPURES_PROPERTY_UTILS_H_

#include "APIEnvir.h"
#include "ACAPinc.h"
#include "HashTable.hpp"

// Get property value by name from an element
bool GetElementPropertyValue(const API_Guid& elemGuid, const GS::UniString& propName, GS::UniString& value);

// Fetch multiple named property values in a single definitions enumeration.
// Only non-empty values are stored in 'outValues'. User-defined properties take
// priority over calculated/all properties of the same name.
void GetElementPropertyValues(const API_Guid& elemGuid,
                              const GS::Array<GS::UniString>& propNames,
                              GS::HashTable<GS::UniString, GS::UniString>& outValues);

// Read AutoText property value
bool GetAutoTextValue(const GS::UniString& propertyName, GS::UniString& value);

#endif // _PHPURES_PROPERTY_UTILS_H_
