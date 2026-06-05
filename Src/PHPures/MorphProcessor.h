#ifndef _PHPURES_MORPH_PROCESSOR_H_
#define _PHPURES_MORPH_PROCESSOR_H_

#include "APIEnvir.h"
#include "ACAPinc.h"
#include "HashTable.hpp"

// Structure to hold morph face data
struct MorphFaceData {
    GS::UniString id;
    GS::UniString zoneId;
    GS::UniString idKonstrukcije;
    GS::UniString idElementaVgradnje;
    double povrsina;
    GS::UniString orientacija;
    GS::UniString naklon;
    GS::UniString delezOkvirja;
    GS::UniString dolzinaStekla;
    bool isTransparent;
    bool hasOrientacija;
    bool hasNaklon;
    bool hasDelezOkvirja;
    bool hasDolzinaStekla;
    bool hasIdElementaVgradnje;
    int stevilo;
    
    MorphFaceData() : povrsina(0.0), isTransparent(false), 
                      hasOrientacija(false), hasNaklon(false), 
                      hasDelezOkvirja(false), hasDolzinaStekla(false), 
                      hasIdElementaVgradnje(false), stevilo(1) {}
};

// Process all morphs in the project and extract their data
GSErrCode ProcessMorphElements(GS::HashTable<GS::UniString, MorphFaceData>& morphDataMap, 
                               int& totalFaces, 
                               int& morphsWithProperties);

#endif // _PHPURES_MORPH_PROCESSOR_H_
