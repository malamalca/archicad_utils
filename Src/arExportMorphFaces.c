#include "APIEnvir.h"
#include "ACAPinc.h"
#include "APICommon.h"
#include "Model3D/model.h"
#include "Model3D/MeshBody.hpp"
#include "Polygon.hpp"
#include "Vertex.hpp"

#include "arExportMorphFaces.h"

#include <vector>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

// Main export function
GSErrCode ExportMorphFaces()
{
    GSErrCode err = NoError;
    
    // Get selected elements
    API_SelectionInfo selectionInfo;
    GS::Array<API_Neig> selNeigs;
    
    err = ACAPI_Selection_Get(&selectionInfo, &selNeigs, true);
    if (err != NoError) {
        ACAPI_WriteReport("Failed to get selection", false);
        return err;
    }
    
    if (selectionInfo.typeID == API_SelEmpty) {
        ACAPI_WriteReport("No elements selected. Please select morph faces.", false);
        return APIERR_NOSEL;
    }
    
    // Store all face data
    std::vector<FaceData> allFaces;

    std::vector<Modeler::VertexId> vertexIds;
    
    // Process each selected element
    for (const API_Neig& neig : selNeigs) {
        if (neig.neigID != APINeig_None) {
            // Get element GUID
            API_Elem_Head elemHead;
            BNZeroMemory(&elemHead, sizeof(API_Elem_Head));
            elemHead.guid = neig.guid;
            
            // Check if it's a morph
            err = ACAPI_Element_GetHeader(&elemHead);
            if (err == NoError && elemHead.type.typeID == API_MorphID) {
                // Get morph body
                API_ElementMemo memo;
                BNZeroMemory(&memo, sizeof(API_ElementMemo));
                
                API_Element element;
                BNZeroMemory(&element, sizeof(API_Element));
                element.header.guid = neig.guid;
                
                err = ACAPI_Element_Get(&element);
                if (err != NoError) continue;
                
                err = ACAPI_Element_GetMemo(element.header.guid, &memo);
                if (err != NoError) {
                    ACAPI_DisposeElemMemoHdls(&memo);
                    continue;
                }
                
                /////////////////////////////////////////////////////////////
                // Get polygon data from body using proper MeshBody iteration
                if (memo.morphBody != nullptr) {
                    try {
                        Int32 polygonCount = memo.morphBody->GetPolygonCount();
                        
                        // Limit export to reasonable number
                        Int32 exportLimit = (polygonCount > 1000) ? 1000 : polygonCount;
                        
                        for (Int32 polyIdx = 0; polyIdx < exportLimit; ++polyIdx) {
                            GS::Array<VERT> coords;
                            GS::Array<ULong> holeStarts;  // Will receive indices where new contours start
                            memo.morphBody->GetPolygonVertices(polyIdx, coords, &holeStarts);
                            
                            if (coords.GetSize() < 3 || coords.GetSize() > 1000) continue; // skip invalid polygons
                            
                            FaceData faceData;
                            faceData.vertices.reserve(coords.GetSize());
                            
                            for (ULong j = 0; j < coords.GetSize(); ++j) {
                                API_Coord3D coord;
                                coord.x = element.morph.tranmat.tmx[0] * coords[j].x + element.morph.tranmat.tmx[1] * coords[j].y + element.morph.tranmat.tmx[2] * coords[j].z + element.morph.tranmat.tmx[3];
                                coord.y = element.morph.tranmat.tmx[4] * coords[j].x + element.morph.tranmat.tmx[5] * coords[j].y + element.morph.tranmat.tmx[6] * coords[j].z + element.morph.tranmat.tmx[7];
                                coord.z = element.morph.tranmat.tmx[8] * coords[j].x + element.morph.tranmat.tmx[9] * coords[j].y + element.morph.tranmat.tmx[10] * coords[j].z + element.morph.tranmat.tmx[11];
                                faceData.vertices.push_back(coord);
                            }
                            
                            // Store hole indices from API
                            for (ULong k = 0; k < holeStarts.GetSize(); ++k) {
                                faceData.holeIndices.push_back(holeStarts[k]);
                            }

                            if (!faceData.vertices.empty()) {
                                allFaces.push_back(faceData);
                            }
                        }
                    } catch (...) {
                        ACAPI_WriteReport("Error accessing morph body polygons", false);
                    }
                }
                
                ACAPI_DisposeElemMemoHdls(&memo);
            }
        }
    }
    
    // Write to file
    if (!allFaces.empty()) {
        WriteVerticesToFile(allFaces, vertexIds);
        
        std::stringstream msg;
        msg << "Successfully exported " << allFaces.size() << " morph face(s)";
        ACAPI_WriteReport(msg.str().c_str(), false);
    } else {
        ACAPI_WriteReport("No morph faces found in selection", false);
    }
    
    return NoError;
}

// Sort vertices counter-clockwise in the polygon's actual plane
void SortVerticesCounterClockwise(std::vector<API_Coord3D>& vertices)
{
    if (vertices.size() < 3) return;

    // Compute polygon normal from first 3 vertices
    double ux = vertices[1].x - vertices[0].x;
    double uy = vertices[1].y - vertices[0].y;
    double uz = vertices[1].z - vertices[0].z;
    double vx = vertices[2].x - vertices[0].x;
    double vy = vertices[2].y - vertices[0].y;
    double vz = vertices[2].z - vertices[0].z;
    
    double nx = uy*vz - uz*vy;
    double ny = uz*vx - ux*vz;
    double nz = ux*vy - uy*vx;
    
    // Determine dominant axis
    double ax = std::abs(nx), ay = std::abs(ny), az = std::abs(nz);
    
    // Compute centroid
    double cx = 0.0, cy = 0.0, cz = 0.0;
    for (const auto& v : vertices) { cx += v.x; cy += v.y; cz += v.z; }
    cx /= vertices.size(); cy /= vertices.size(); cz /= vertices.size();

    // Sort by angle in the appropriate plane
    if (ax >= ay && ax >= az) {
        // Project to YZ plane
        std::sort(vertices.begin(), vertices.end(), [&](const API_Coord3D& a, const API_Coord3D& b) {
            double angA = atan2(a.z - cz, a.y - cy);
            double angB = atan2(b.z - cz, b.y - cy);
            return (nx > 0) ? (angA < angB) : (angA > angB);
        });
    } else if (ay >= ax && ay >= az) {
        // Project to XZ plane
        std::sort(vertices.begin(), vertices.end(), [&](const API_Coord3D& a, const API_Coord3D& b) {
            double angA = atan2(a.z - cz, a.x - cx);
            double angB = atan2(b.z - cz, b.x - cx);
            return (ny > 0) ? (angA < angB) : (angA > angB);
        });
    } else {
        // Project to XY plane
        std::sort(vertices.begin(), vertices.end(), [&](const API_Coord3D& a, const API_Coord3D& b) {
            double angA = atan2(a.y - cy, a.x - cx);
            double angB = atan2(b.y - cy, b.x - cx);
            return (nz > 0) ? (angA < angB) : (angA > angB);
        });
    }
}

// Write vertices to text file with hole information
void WriteVerticesToFile(const std::vector<FaceData>& allFaces, const std::vector<Modeler::VertexId>& /*vertexIds*/)
{
    // Get desktop path or use temp directory
    IO::Location folderLoc;
    API_SpecFolderID specID = API_UserDocumentsFolderID;
    ACAPI_ProjectSettings_GetSpecFolder(&specID, &folderLoc);

    // Create file path
    IO::Name fileName("morph_faces.html");
    IO::Location fileLoc(folderLoc);
    fileLoc.AppendToLocal(fileName);

    // Open file for writing
    std::ofstream outFile;
    GS::UniString path;
    fileLoc.ToPath(&path);

    outFile.open(path.ToCStr(), std::ios::out | std::ios::trunc);

    if (!outFile.is_open()) {
        ACAPI_WriteReport("Failed to create output file", false);
        return;
    }

    // Build JSON-like JS array of faces with hole information
    std::ostringstream facesJs;
    facesJs << std::fixed << std::setprecision(6);
    facesJs << "var faces = [\n";
    for (size_t i = 0; i < allFaces.size(); ++i) {
        facesJs << "  {\n";
        facesJs << "    vertices: [\n";
        const auto& faceData = allFaces[i];
        for (size_t j = 0; j < faceData.vertices.size(); ++j) {
            // Transform from ArchiCAD Z-up to Three.js Y-up: swap Y and Z
            facesJs << "      [" << faceData.vertices[j].x << ", " << faceData.vertices[j].z << ", " << -faceData.vertices[j].y << "]";
            if (j + 1 < faceData.vertices.size()) facesJs << ",\n";
            else facesJs << "\n";
        }
        facesJs << "    ],\n";
        facesJs << "    holes: [";
        for (size_t k = 0; k < faceData.holeIndices.size(); ++k) {
            facesJs << faceData.holeIndices[k];
            if (k + 1 < faceData.holeIndices.size()) facesJs << ", ";
        }
        facesJs << "]\n";
        facesJs << "  }";
        if (i + 1 < allFaces.size()) facesJs << ",\n";
        else facesJs << "\n";
    }
    facesJs << "];\n";

    // Write HTML + Three.js + Earcut
    outFile << "<!doctype html>\n<html lang=\"en\">\n<head>\n  <meta charset=\"utf-8\">\n  <title>Morph Faces Preview</title>\n  <style>body{margin:0;overflow:hidden}#info{position:absolute;top:10px;left:10px;color:#fff;background:rgba(0,0,0,0.3);padding:6px;border-radius:4px;font-family:Arial}#controls{position:absolute;top:50px;left:10px;color:#fff;background:rgba(0,0,0,0.3);padding:6px;border-radius:4px;font-family:Arial}label{display:block;margin:4px 0;cursor:pointer}</style>\n</head>\n<body>\n<div id=\"info\">Drag to orbit, scroll to zoom</div>\n<div id=\"controls\">\n  <label><input type=\"checkbox\" id=\"wireframe\"> Wireframe</label>\n  <label><input type=\"checkbox\" id=\"doubleSide\" checked> Double-sided</label>\n  <label><input type=\"checkbox\" id=\"showNormals\"> Show Normals</label>\n  <label><input type=\"checkbox\" id=\"colorByPlane\"> Color by Plane</label>\n</div>\n<script src=\"https://unpkg.com/earcut@2.2.4/dist/earcut.min.js\"></script>\n<script type=\"importmap\">{\n  \"imports\": {\n    \"three\": \"https://cdn.jsdelivr.net/npm/three@0.159.0/build/three.module.js\",\n    \"three/addons/\": \"https://cdn.jsdelivr.net/npm/three@0.159.0/examples/jsm/\"\n  }\n}</script>\n<script type=\"module\">\n";

    outFile << facesJs.str();

    outFile << R"JS(
import * as THREE from 'three';
import { OrbitControls } from 'https://cdn.jsdelivr.net/npm/three@0.159.0/examples/jsm/controls/OrbitControls.js';
import { VertexNormalsHelper } from 'three/addons/helpers/VertexNormalsHelper.js';

// Scene setup
var scene = new THREE.Scene();
scene.background = new THREE.Color(0x222222);
var camera = new THREE.PerspectiveCamera(45, window.innerWidth/window.innerHeight, 0.1, 10000);
camera.position.set(0, 200, 400);
var renderer = new THREE.WebGLRenderer({antialias:true});
renderer.setSize(window.innerWidth, window.innerHeight);
document.body.appendChild(renderer.domElement);
var controls = new OrbitControls(camera, renderer.domElement);
controls.target.set(0,0,0);

// Add grid + light
var grid = new THREE.GridHelper(1000, 50, 0x444444, 0x222222);
scene.add(grid);
var amb = new THREE.AmbientLight(0x888888);
scene.add(amb);
var dir = new THREE.DirectionalLight(0xffffff, 0.9);
dir.position.set(100,200,100);
scene.add(dir);
// Group to hold the model meshes (so grid doesn't affect bbox)
var modelGroup = new THREE.Group();
scene.add(modelGroup);

// Build meshes from faces; project polygons to their dominant plane for triangulation
for (var fi = 0; fi < faces.length; ++fi) {
    var faceData = faces[fi];
    if (!faceData || !faceData.vertices || faceData.vertices.length < 3) continue;
    
    var vertices = [];
    for (var vi = 0; vi < faceData.vertices.length; ++vi) {
        var v = faceData.vertices[vi];
        vertices.push(v[0], v[1], v[2]);
    }

    // Compute polygon normal (using first three vertices)
    var p0 = faceData.vertices[0], p1 = faceData.vertices[1], p2 = faceData.vertices[2];
    var ux = p1[0]-p0[0], uy = p1[1]-p0[1], uz = p1[2]-p0[2];
    var vx = p2[0]-p0[0], vy = p2[1]-p0[1], vz = p2[2]-p0[2];
    var nx = uy*vz - uz*vy;
    var ny = uz*vx - ux*vz;
    var nz = ux*vy - uy*vx;
    var ax = Math.abs(nx), ay = Math.abs(ny), az = Math.abs(nz);

    // Choose projection: 0 -> XY, 1 -> XZ, 2 -> YZ
    var proj = 0;
    if (ax >= ay && ax >= az) proj = 2; // normal mostly along X -> project to YZ
    else if (ay >= ax && ay >= az) proj = 1; // normal mostly along Y -> project to XZ
    else proj = 0; // normal mostly along Z -> project to XY

    // Build 2D contour for triangulation
    var vertices2D = [];
    for (var vi2 = 0; vi2 < faceData.vertices.length; ++vi2) {
        var vv = faceData.vertices[vi2];
        if (proj === 0) {
            vertices2D.push(vv[0], vv[1]);
        } else if (proj === 1) {
            vertices2D.push(vv[0], vv[2]);
        } else {
            vertices2D.push(vv[1], vv[2]);
        }
    }

    var indices = [];
    var triangulated = false;
    
    // Use hole indices from ArchiCAD if provided
    var holeIndices = (faceData.holes && faceData.holes.length > 0) ? faceData.holes : null;
    
    if (holeIndices) {
        try {
            indices = earcut(vertices2D, holeIndices);
            if (indices.length > 0) {
                triangulated = true;
                console.log('Face', fi, 'with holes at', holeIndices, 'tris:', indices.length/3);
            }
        } catch (e) {
            console.warn('Face', fi, 'earcut with holes failed:', e);
        }
    }
    
    // Fallback: try as simple polygon
    if (!triangulated) {
        try {
            indices = earcut(vertices2D, null);
            if (indices.length > 0) {
                triangulated = true;
                console.log('Face', fi, 'simple polygon, tris:', indices.length/3);
            }
        } catch (e) {
            console.warn('Face', fi, 'earcut simple failed:', e);
        }
    }
    
    // Final fallback: fan triangulation
    if (!triangulated || indices.length === 0) {
        console.warn('Face', fi, 'using fan fallback');
        for (var t = 1; t < faceData.vertices.length - 1; ++t) {
            indices.push(0, t, t + 1);
        }
    }

    var geometry = new THREE.BufferGeometry();
    geometry.setAttribute('position', new THREE.BufferAttribute(new Float32Array(vertices), 3));
    geometry.setIndex(indices);
    geometry.computeVertexNormals();
    
    // Color based on projection plane: XY=blue, XZ=green, YZ=red
    var baseColor = proj === 0 ? 0x2194ce : proj === 1 ? 0x21ce94 : 0xce2194;
    var material = new THREE.MeshStandardMaterial({color:baseColor, metalness:0.2, roughness:0.6, side:THREE.DoubleSide, wireframe:false});
    var mesh = new THREE.Mesh(geometry, material);
    mesh.userData.material = material;
    mesh.userData.projection = proj;
    mesh.userData.baseColor = baseColor;
    mesh.userData.faceIndex = fi;
    modelGroup.add(mesh);
    
    // Create normal helper (hidden by default)
    var normalHelper = new VertexNormalsHelper(mesh, 0.5, 0xffff00);
    normalHelper.visible = false;
    mesh.userData.normalHelper = normalHelper;
    scene.add(normalHelper);
}

// Wireframe and double-side toggle controls
document.getElementById('wireframe').addEventListener('change', function(e) {
    modelGroup.children.forEach(function(mesh) {
        if (mesh.userData.material) mesh.userData.material.wireframe = e.target.checked;
    });
});
document.getElementById('doubleSide').addEventListener('change', function(e) {
    modelGroup.children.forEach(function(mesh) {
        if (mesh.userData.material) mesh.userData.material.side = e.target.checked ? THREE.DoubleSide : THREE.FrontSide;
    });
});
document.getElementById('showNormals').addEventListener('change', function(e) {
    modelGroup.children.forEach(function(mesh) {
        if (mesh.userData.normalHelper) mesh.userData.normalHelper.visible = e.target.checked;
    });
});
document.getElementById('colorByPlane').addEventListener('change', function(e) {
    modelGroup.children.forEach(function(mesh) {
        if (mesh.userData.material) {
            if (e.target.checked) {
                mesh.userData.material.color.setHex(mesh.userData.baseColor);
            } else {
                mesh.userData.material.color.setHex(0x2194ce);
            }
        }
    });
});

// Auto-fit camera to model bounding sphere
var bbox = new THREE.Box3().setFromObject(modelGroup);
var radius = 50; // default if empty
if (!bbox.isEmpty()) {
    var sphere = bbox.getBoundingSphere(new THREE.Sphere());
    var center = sphere.center;
    radius = sphere.radius;
    var fitDistance = Math.max(radius * 3.0, 10.0);
    var camPos = new THREE.Vector3(fitDistance * 0.7, fitDistance * 0.7, fitDistance * 0.7).add(center);
    camera.position.copy(camPos);
    camera.near = Math.max(0.01, radius / 100);
    camera.far = fitDistance * 20.0;
    camera.updateProjectionMatrix();
    camera.lookAt(center);
    controls.target.copy(center);
    controls.update();
    
    // Resize grid relative to model size
    var sizeForGrid = Math.max(radius * 4.0, 10.0);
    scene.remove(grid);
    grid = new THREE.GridHelper(sizeForGrid, Math.max(10, Math.round(sizeForGrid/10)), 0x444444, 0x222222);
    scene.add(grid);
}

function onWindowResize(){
    camera.aspect = window.innerWidth/window.innerHeight;
    camera.updateProjectionMatrix();
    renderer.setSize(window.innerWidth, window.innerHeight);
}
window.addEventListener('resize', onWindowResize, false);

function animate(){
    requestAnimationFrame(animate);
    controls.update();
    renderer.render(scene, camera);
}
animate();

)JS";

    outFile << "</script>\n</body>\n</html>\n";

    outFile.close();

    // Report file location
    GS::UniString reportMsg = GS::UniString("HTML preview exported to: ") + path;
    ACAPI_WriteReport(reportMsg, false);
}