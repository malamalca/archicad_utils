#ifndef _PHPURES_PROPERTY_UTILS_H_
#define _PHPURES_PROPERTY_UTILS_H_

#include "APIEnvir.h"
#include "ACAPinc.h"

// Get property value by name from an element
bool GetElementPropertyValue(const API_Guid& elemGuid, const GS::UniString& propName, GS::UniString& value);

// Read AutoText property value
bool GetAutoTextValue(const GS::UniString& propertyName, GS::UniString& value);

#endif // _PHPURES_PROPERTY_UTILS_H_
