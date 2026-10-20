#ifndef _PHPURES_JSON_UPDATER_H_
#define _PHPURES_JSON_UPDATER_H_

#include "APIEnvir.h"
#include "ACAPinc.h"
#include "JSON/Value.hpp"
#include "HashTable.hpp"
#include "HashSet.hpp"
#include "MorphProcessor.h"

// Structure to hold change report data
struct ChangeReport {
    GS::UniString id;
    bool isTransparent;
    double oldPovrsina;
    double newPovrsina;
    double oldObseg;
    double newObseg;
    int oldStevilo;
    int newStevilo;
    GS::UniString oldOrientacija;
    GS::UniString newOrientacija;
    GS::UniString oldNaklon;
    GS::UniString newNaklon;
    GS::UniString oldDelezOkvirja;
    GS::UniString newDelezOkvirja;
    GS::UniString oldDolzinaStekla;
    GS::UniString newDolzinaStekla;
    GS::UniString oldIdElementaVgradnje;
    GS::UniString newIdElementaVgradnje;
    GS::UniString oldIdKonstrukcije;
    GS::UniString newIdKonstrukcije;
    bool hasChanges;
    bool hadObsegField;
    bool hadSteviloField;
    bool hadOrientacijaField;
    bool hadNaklonField;
    bool hadDelezOkvirjaField;
    bool hadDolzinaSteklaField;
    bool hadIdElementaVgradnjeField;
    bool hadIdKonstrukcijeField;

    ChangeReport() : isTransparent(false), oldPovrsina(0.0), newPovrsina(0.0),
                    oldObseg(0.0), newObseg(0.0),
                    oldStevilo(0), newStevilo(0), hasChanges(false),
                    hadObsegField(false),
                    hadSteviloField(false), hadOrientacijaField(false),
                    hadNaklonField(false), hadDelezOkvirjaField(false),
                    hadDolzinaSteklaField(false), hadIdElementaVgradnjeField(false),
                    hadIdKonstrukcijeField(false) {}
};

// Update JSON with morph data - existing entries
GSErrCode UpdateExistingJsonEntries(
    JSON::ObjectValueRef& ovojObject,
    JSON::ArrayValueRef& rootArray,
    const GS::HashTable<GS::UniString, MorphFaceData>& morphDataMap,
    GS::HashSet<GS::UniString>& processedMorphIds,
    GS::Array<ChangeReport>& changeReports,
    GS::Array<GS::UniString>& missingMorphIds);

// Add new morphs to JSON that exist in Archicad but not in JSON
GSErrCode AddNewMorphsToJson(
    JSON::ArrayValueRef& rootArray,
    const GS::HashTable<GS::UniString, MorphFaceData>& morphDataMap,
    const GS::HashSet<GS::UniString>& processedMorphIds,
    GS::Array<ChangeReport>& changeReports,
    GS::Array<GS::UniString>& missingZoneWarnings);

#endif // _PHPURES_JSON_UPDATER_H_
