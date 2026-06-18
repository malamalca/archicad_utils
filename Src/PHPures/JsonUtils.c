#include "JsonUtils.h"
#include "File.hpp"
#include "Location.hpp"
#include "MemoryIBinaryChannel.hpp"
#include "JSON/JDOMParser.hpp"
#include "JSON/JDOMWriter.hpp"

// Read and parse a JSON file
GSErrCode ReadJsonFile(const GS::UniString& filePath, JSON::ValueRef& parsedValue)
{
    IO::Location jsonLocation(filePath);
    IO::File jsonFile(jsonLocation);
    
    if (jsonFile.GetStatus() != NoError) {
        ACAPI_WriteReport("Failed to access zone file: %s", true, filePath.ToCStr().Get());
        return APIERR_BADVALUE;
    }
    
    UInt64 fileSize = 0;
    GSErrCode err = jsonFile.GetDataLength(&fileSize);
    if (err != NoError || fileSize == 0) {
        ACAPI_WriteReport("Zone file is empty or unreadable", true);
        return APIERR_BADVALUE;
    }
    
    GS::Array<char> buffer;
    buffer.SetSize((USize)fileSize);
    
    err = jsonFile.Open(IO::File::ReadMode);
    if (err != NoError) {
        ACAPI_WriteReport("Failed to open zone file for reading", true);
        return err;
    }
    
    err = jsonFile.ReadBin(buffer.GetContent(), (USize)fileSize);
    jsonFile.Close();
    if (err != NoError) {
        ACAPI_WriteReport("Failed to read zone file contents", true);
        return err;
    }

    // Parse JSON
    try {
        GS::MemoryIBinaryChannel input(buffer.GetContent(), (USize)fileSize);
        JSON::JDOMParser parser;
        parsedValue = parser.Parse(input);
    } catch (...) {
        ACAPI_WriteReport("Failed to parse JSON file", true);
        return APIERR_BADVALUE;
    }
    
    if (parsedValue == nullptr) {
        ACAPI_WriteReport("Failed to parse JSON", true);
        return APIERR_BADVALUE;
    }
    
    return NoError;
}

// Write JSON value to file
GSErrCode WriteJsonFile(const GS::UniString& filePath, const JSON::ValueRef& jsonValue)
{
    // Validate JSON before writing
    if (jsonValue == nullptr) {
        ACAPI_WriteReport("ERROR: JSON value is null - NOT writing file", true);
        return APIERR_BADVALUE;
    }
    
    GS::UniString jsonOutput;
    try {
        JSON::JDOMStringWriter writer(*jsonValue, jsonOutput, true); // true = format output
    } catch (...) {
        ACAPI_WriteReport("Failed to serialize JSON", true);
        return APIERR_GENERAL;
    }
    
    // Verify output is not empty before writing
    if (jsonOutput.IsEmpty()) {
        ACAPI_WriteReport("ERROR: Generated JSON is empty - NOT writing file to prevent data loss", true);
        return APIERR_GENERAL;
    }
    
    IO::Location jsonLocation(filePath);
    IO::File jsonFile(jsonLocation);
    
    GSErrCode err = jsonFile.Open(IO::File::WriteEmptyMode);
    if (err != NoError) {
        ACAPI_WriteReport("Failed to open zone file for writing", true);
        return err;
    }
    
    // Convert UniString to UTF-8 and write
    auto utf8CStr = jsonOutput.ToCStr(0, MaxUSize, CC_UTF8);
    const char* utf8Text = utf8CStr.Get();
    USize textLength = (USize)strlen(utf8Text);
    err = jsonFile.WriteBin(utf8Text, textLength);
    jsonFile.Close();
    
    ACAPI_WriteReport("", false);
    ACAPI_WriteReport("JSON FILE WRITE COMPLETED", false);
    ACAPI_WriteReport("File path: %s", false, filePath.ToCStr().Get());
    ACAPI_WriteReport("Bytes written: %d", false, (int)textLength);
    ACAPI_WriteReport("Write error code: %d (0=success)", false, (int)err);
    ACAPI_WriteReport("", false);
    
    return err;
}

// Extract ovoj object from parsed JSON
GSErrCode ExtractOvojObject(const JSON::ValueRef& parsedValue, JSON::ObjectValueRef& ovojObject, JSON::ArrayValueRef& rootArray)
{
    rootArray = nullptr;
    ovojObject = nullptr;
    
    if (parsedValue->IsArray()) {
        // Root is array - get first zone element
        rootArray = JSON::ArrayValue::Cast(parsedValue);
        if (rootArray == nullptr || rootArray->GetSize() == 0) {
            ACAPI_WriteReport("JSON array is empty", true);
            return APIERR_BADVALUE;
        }
        
        JSON::ObjectValueRef zoneObject = JSON::ObjectValue::Cast(rootArray->Get(0));
        if (zoneObject == nullptr || !zoneObject->HasMember("ovoj")) {
            ACAPI_WriteReport("Zone object does not have 'ovoj' property", true);
            return APIERR_BADVALUE;
        }
        
        ovojObject = JSON::ObjectValue::Cast(zoneObject->Get("ovoj"));
        if (ovojObject == nullptr) {
            ACAPI_WriteReport("'ovoj' is not an object", true);
            return APIERR_BADVALUE;
        }
    } else if (parsedValue->IsObject()) {
        // Root is object - check if it has ovoj or is ovoj itself
        JSON::ObjectValueRef rootObject = JSON::ObjectValue::Cast(parsedValue);
        if (rootObject->HasMember("ovoj")) {
            ovojObject = JSON::ObjectValue::Cast(rootObject->Get("ovoj"));
        } else if (rootObject->HasMember("netransparentneKonstrukcije") || rootObject->HasMember("transparentneKonstrukcije")) {
            ovojObject = rootObject;
        } else {
            ACAPI_WriteReport("JSON structure not recognized - expected 'ovoj' property or construction arrays", true);
            return APIERR_BADVALUE;
        }
    } else {
        ACAPI_WriteReport("JSON root is neither array nor object", true);
        return APIERR_BADVALUE;
    }
    
    return NoError;
}
