#ifndef _PHPURES_JSON_UTILS_H_
#define _PHPURES_JSON_UTILS_H_

#include "APIEnvir.h"
#include "ACAPinc.h"
#include "JSON/Value.hpp"

// Read and parse a JSON file
GSErrCode ReadJsonFile(const GS::UniString& filePath, JSON::ValueRef& parsedValue);

// Write JSON value to file
GSErrCode WriteJsonFile(const GS::UniString& filePath, const JSON::ValueRef& jsonValue);

// Extract ovoj object from parsed JSON
GSErrCode ExtractOvojObject(const JSON::ValueRef& parsedValue, JSON::ObjectValueRef& ovojObject, JSON::ArrayValueRef& rootArray);

#endif // _PHPURES_JSON_UTILS_H_
