#ifndef	_EXPORTMORPHFACES_H_
#define	_EXPORTMORPHFACES_H_

#include "GSRoot.hpp"
#include "Model3D/model.h"
#include "Model3D/MeshBody.hpp"

// Structure to hold face data with hole information
struct FaceData {
    std::vector<API_Coord3D> vertices;
    std::vector<ULong> holeIndices;
};

GSErrCode ExportMorphFaces();
GSErrCode ProcessMorphElement(const API_Guid& elemGuid);
void SortVerticesCounterClockwise(std::vector<API_Coord3D>& vertices);
API_Coord3D FindTopLeftVertex(const std::vector<API_Coord3D>& vertices);
void WriteVerticesToFile(const std::vector<FaceData>& allFaces, const std::vector<Modeler::VertexId>& vertexIds);

#endif
