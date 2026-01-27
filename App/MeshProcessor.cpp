#include "MeshProcessor.hpp"
#include <cmath>
#include <algorithm>
#include <limits>
#include <numeric>
#include <QDebug>
#include <QElapsedTimer>
#include <QLoggingCategory>


#ifdef _MSC_VER
#include <execution>
#define PAR_SORT(begin, end) std::sort(std::execution::par, begin, end)
#else
#define PAR_SORT(begin, end) std::sort(begin, end)
#endif

Q_LOGGING_CATEGORY(meshProcessorLog, "VTKViewer.MeshProcessor")

GPUMeshData MeshProcessor::process(const std::shared_ptr<UnstructuredGrid>& grid)
{
    GPUMeshData result;
    result.useFlatShading = true;
    
    if (!grid || !grid->points) {
        return result;
    }

    QElapsedTimer loadTimer;
    loadTimer.start();

    // Store data array names
    m_pointDataNames.clear();
    m_cellDataNames.clear();
    for (const auto& pair : grid->point_data) {
        m_pointDataNames.append(QString::fromStdString(pair.first));
    }
    for (const auto& pair : grid->cell_data) {
        m_cellDataNames.append(QString::fromStdString(pair.first));
    }
    
    // ============ Step 1: Extract point positions ============
    const auto& points = grid->points;
    const size_t numPoints = points->num_tuples;
    const int numComp = static_cast<int>(points->num_components);
    
    const float* fPtr = (points->data_type == "float") ? points->data_float.data() : nullptr;
    const double* dPtr = (points->data_type == "double") ? points->data_double.data() : nullptr;
    
    // Copy positions to temporary buffer
    std::vector<float> positions(numPoints * 3);
    if (fPtr) {
        for (size_t i = 0; i < numPoints; ++i) {
            positions[i*3+0] = fPtr[i*numComp+0];
            positions[i*3+1] = fPtr[i*numComp+1];
            positions[i*3+2] = (numComp > 2) ? fPtr[i*numComp+2] : 0.0f;
        }
    } else if (dPtr) {
        for (size_t i = 0; i < numPoints; ++i) {
            positions[i*3+0] = static_cast<float>(dPtr[i*numComp+0]);
            positions[i*3+1] = static_cast<float>(dPtr[i*numComp+1]);
            positions[i*3+2] = (numComp > 2) ? static_cast<float>(dPtr[i*numComp+2]) : 0.0f;
        }
    }
    
    // Compute bounding box
    computeBoundingBox(positions, numPoints, result.boundingBoxMin, result.boundingBoxMax);
    qInfo(meshProcessorLog) << "Loaded" << numPoints << "points (" << numComp << " components) in" << loadTimer.elapsed() << "ms";
    QElapsedTimer meshTimer;
    meshTimer.start();
    QElapsedTimer stageTimer;
    stageTimer.start();

    // ============ Step 2: Extract all faces from cells ============
    const auto& cells = grid->cells;
    const auto& cellTypes = grid->cell_types;
    const size_t totalCells = grid->num_cells;
    
    std::vector<Face> allFaces;
    allFaces.reserve(totalCells * 4);
    
    size_t cellOffset = 0;
    const int32_t* cellsPtr = cells.data();
    const uint8_t* typesPtr = cellTypes.data();
    
    for (size_t cellIdx = 0; cellIdx < totalCells && cellOffset < cells.size(); ++cellIdx) {
        int32_t n = cellsPtr[cellOffset];
        uint8_t type = (cellIdx < cellTypes.size()) ? typesPtr[cellIdx] : VTK_TRIANGLE;
        const int32_t* c = &cellsPtr[cellOffset + 1];
        uint32_t cIdx = static_cast<uint32_t>(cellIdx);
        
        #define IDX(k) static_cast<uint32_t>(c[k])
        
        switch (type) {
            case VTK_TRIANGLE:
                if (n >= 3) {
                    Face f; f.set3(IDX(0), IDX(1), IDX(2), cIdx);
                    allFaces.push_back(f);
                }
                break;
                
            case VTK_TRIANGLE_STRIP:
                for (int k = 0; k < n - 2; ++k) {
                    Face f;
                    if (k % 2 == 0) f.set3(IDX(k), IDX(k+1), IDX(k+2), cIdx);
                    else f.set3(IDX(k), IDX(k+2), IDX(k+1), cIdx);
                    allFaces.push_back(f);
                }
                break;
                
            case VTK_QUAD:
                if (n >= 4) {
                    Face f; f.set4(IDX(0), IDX(1), IDX(2), IDX(3), cIdx);
                    allFaces.push_back(f);
                }
                break;
                
            case VTK_POLYGON:
                if (n >= 3) {
                    // Fan triangulation
                    for (int k = 1; k < n - 1; ++k) {
                        Face f; f.set3(IDX(0), IDX(k), IDX(k+1), cIdx);
                        allFaces.push_back(f);
                    }
                }
                break;
                
            case VTK_TETRA:
                if (n >= 4) {
                    Face f;
                    f.set3(IDX(0), IDX(1), IDX(3), cIdx); allFaces.push_back(f);
                    f.set3(IDX(1), IDX(2), IDX(3), cIdx); allFaces.push_back(f);
                    f.set3(IDX(2), IDX(0), IDX(3), cIdx); allFaces.push_back(f);
                    f.set3(IDX(0), IDX(2), IDX(1), cIdx); allFaces.push_back(f);
                }
                break;
                
            case VTK_VOXEL:
                if (n >= 8) {
                    Face f;
                    f.set4(IDX(0), IDX(1), IDX(3), IDX(2), cIdx); allFaces.push_back(f); // -Z
                    f.set4(IDX(4), IDX(6), IDX(7), IDX(5), cIdx); allFaces.push_back(f); // +Z
                    f.set4(IDX(0), IDX(2), IDX(6), IDX(4), cIdx); allFaces.push_back(f); // -X
                    f.set4(IDX(1), IDX(5), IDX(7), IDX(3), cIdx); allFaces.push_back(f); // +X
                    f.set4(IDX(0), IDX(4), IDX(5), IDX(1), cIdx); allFaces.push_back(f); // -Y
                    f.set4(IDX(2), IDX(3), IDX(7), IDX(6), cIdx); allFaces.push_back(f); // +Y
                }
                break;
                
            case VTK_HEXAHEDRON:
                if (n >= 8) {
                    Face f;
                    f.set4(IDX(0), IDX(1), IDX(5), IDX(4), cIdx); allFaces.push_back(f); // Front
                    f.set4(IDX(1), IDX(2), IDX(6), IDX(5), cIdx); allFaces.push_back(f); // Right
                    f.set4(IDX(2), IDX(3), IDX(7), IDX(6), cIdx); allFaces.push_back(f); // Back
                    f.set4(IDX(3), IDX(0), IDX(4), IDX(7), cIdx); allFaces.push_back(f); // Left
                    f.set4(IDX(0), IDX(3), IDX(2), IDX(1), cIdx); allFaces.push_back(f); // Bottom
                    f.set4(IDX(4), IDX(5), IDX(6), IDX(7), cIdx); allFaces.push_back(f); // Top
                }
                break;
                
            case VTK_WEDGE:
                if (n >= 6) {
                    Face f;
                    f.set3(IDX(0), IDX(1), IDX(2), cIdx); allFaces.push_back(f); // Bottom tri
                    f.set3(IDX(3), IDX(5), IDX(4), cIdx); allFaces.push_back(f); // Top tri
                    f.set4(IDX(0), IDX(1), IDX(4), IDX(3), cIdx); allFaces.push_back(f);
                    f.set4(IDX(1), IDX(2), IDX(5), IDX(4), cIdx); allFaces.push_back(f);
                    f.set4(IDX(2), IDX(0), IDX(3), IDX(5), cIdx); allFaces.push_back(f);
                }
                break;
                
            case VTK_PYRAMID:
                if (n >= 5) {
                    Face f;
                    f.set4(IDX(0), IDX(3), IDX(2), IDX(1), cIdx); allFaces.push_back(f); // Base
                    f.set3(IDX(0), IDX(1), IDX(4), cIdx); allFaces.push_back(f);
                    f.set3(IDX(1), IDX(2), IDX(4), cIdx); allFaces.push_back(f);
                    f.set3(IDX(2), IDX(3), IDX(4), cIdx); allFaces.push_back(f);
                    f.set3(IDX(3), IDX(0), IDX(4), cIdx); allFaces.push_back(f);
                }
                break;
                
            default:
                break;
        }
        
        #undef IDX
        cellOffset += (n + 1);
    }
    qInfo(meshProcessorLog)
        << "Face extraction" << allFaces.size() << "faces from" << totalCells << "cells in" << stageTimer.elapsed() << "ms";
    stageTimer.restart();

    // ============ Step 3: Sort faces to find unique boundary faces ============
    // Using parallel sort for performance on large meshes
    PAR_SORT(allFaces.begin(), allFaces.end());
    qInfo(meshProcessorLog) << "Face sorting" << allFaces.size() << "faces in" << stageTimer.elapsed() << "ms";
    stageTimer.restart();

    // ============ Step 4: Extract boundary faces (count == 1) ============
    // A face that appears exactly once is on the boundary
    std::vector<const Face*> boundaryFaces;
    boundaryFaces.reserve(allFaces.size() / 2);
    
    size_t i = 0;
    size_t nFaces = allFaces.size();
    while (i < nFaces) {
        size_t j = i + 1;
        while (j < nFaces && allFaces[i] == allFaces[j]) {
            ++j;
        }
        
        // If count is 1, it's a boundary face
        if (j - i == 1) {
            boundaryFaces.push_back(&allFaces[i]);
        }
        
        i = j;
    }
    qInfo(meshProcessorLog)
        << "Boundary selection" << boundaryFaces.size() << "faces in" << stageTimer.elapsed() << "ms";
    stageTimer.restart();

    // ============ Step 5: Generate flat-shaded vertices ============
    // Count total triangles (quads become 2 triangles)
    size_t numTriangles = 0;
    for (const Face* f : boundaryFaces) {
        numTriangles += (f->n == 4) ? 2 : 1;
    }
    
    result.triangleCount = numTriangles;
    result.vertexCount = numTriangles * 3;
    
    // vertex data: position(3) + normal(3) + scalar(1) = 7 floats per vertex
    const size_t stride = 7;
    result.vertexData.resize(result.vertexCount * stride);
    result.vertexToPointIndex.resize(result.vertexCount);
    result.vertexToCellIndex.resize(result.vertexCount);
    
    // Also generate line indices for wireframe
    result.lineIndices.reserve(numTriangles * 6);
    
    size_t vertIdx = 0;
    
    auto emitTriangle = [&](uint32_t i0, uint32_t i1, uint32_t i2, uint32_t cellIdx) {
        // Get positions
        float v0x = positions[i0*3+0], v0y = positions[i0*3+1], v0z = positions[i0*3+2];
        float v1x = positions[i1*3+0], v1y = positions[i1*3+1], v1z = positions[i1*3+2];
        float v2x = positions[i2*3+0], v2y = positions[i2*3+1], v2z = positions[i2*3+2];
        
        // Compute face normal
        float e1x = v1x - v0x, e1y = v1y - v0y, e1z = v1z - v0z;
        float e2x = v2x - v0x, e2y = v2y - v0y, e2z = v2z - v0z;
        
        float nx = e1y * e2z - e1z * e2y;
        float ny = e1z * e2x - e1x * e2z;
        float nz = e1x * e2y - e1y * e2x;
        
        float len = std::sqrt(nx*nx + ny*ny + nz*nz);
        if (len > 1e-8f) {
            float invLen = 1.0f / len;
            nx *= invLen; ny *= invLen; nz *= invLen;
        } else {
            nx = 0.0f; ny = 1.0f; nz = 0.0f;
        }
        
        // Store 3 vertices for this triangle
        size_t baseIdx = vertIdx * stride;
        
        // Vertex 0
        result.vertexData[baseIdx + 0] = v0x;
        result.vertexData[baseIdx + 1] = v0y;
        result.vertexData[baseIdx + 2] = v0z;
        result.vertexData[baseIdx + 3] = nx;
        result.vertexData[baseIdx + 4] = ny;
        result.vertexData[baseIdx + 5] = nz;
        result.vertexData[baseIdx + 6] = 0.5f;
        result.vertexToPointIndex[vertIdx] = i0;
        result.vertexToCellIndex[vertIdx] = cellIdx;
        
        // Vertex 1
        result.vertexData[baseIdx + 7] = v1x;
        result.vertexData[baseIdx + 8] = v1y;
        result.vertexData[baseIdx + 9] = v1z;
        result.vertexData[baseIdx + 10] = nx;
        result.vertexData[baseIdx + 11] = ny;
        result.vertexData[baseIdx + 12] = nz;
        result.vertexData[baseIdx + 13] = 0.5f;
        result.vertexToPointIndex[vertIdx + 1] = i1;
        result.vertexToCellIndex[vertIdx + 1] = cellIdx;
        
        // Vertex 2
        result.vertexData[baseIdx + 14] = v2x;
        result.vertexData[baseIdx + 15] = v2y;
        result.vertexData[baseIdx + 16] = v2z;
        result.vertexData[baseIdx + 17] = nx;
        result.vertexData[baseIdx + 18] = ny;
        result.vertexData[baseIdx + 19] = nz;
        result.vertexData[baseIdx + 20] = 0.5f;
        result.vertexToPointIndex[vertIdx + 2] = i2;
        result.vertexToCellIndex[vertIdx + 2] = cellIdx;
        
        // Line indices for wireframe
        uint32_t vi0 = static_cast<uint32_t>(vertIdx);
        uint32_t vi1 = static_cast<uint32_t>(vertIdx + 1);
        uint32_t vi2 = static_cast<uint32_t>(vertIdx + 2);
        result.lineIndices.push_back(vi0); result.lineIndices.push_back(vi1);
        result.lineIndices.push_back(vi1); result.lineIndices.push_back(vi2);
        result.lineIndices.push_back(vi2); result.lineIndices.push_back(vi0);
        
        vertIdx += 3;
    };
    
    for (const Face* f : boundaryFaces) {
        if (f->n == 3) {
            emitTriangle(f->orig[0], f->orig[1], f->orig[2], f->cellIdx);
        } else if (f->n == 4) {
            // Triangulate quad: 0-1-2 and 0-2-3
            emitTriangle(f->orig[0], f->orig[1], f->orig[2], f->cellIdx);
            emitTriangle(f->orig[0], f->orig[2], f->orig[3], f->cellIdx);
        }
    }
    
    result.lineCount = result.lineIndices.size() / 2;
    
    // Point indices for point rendering (all vertices)
    result.pointIndices.resize(result.vertexCount);
    for (size_t i = 0; i < result.vertexCount; ++i) {
        result.pointIndices[i] = static_cast<uint32_t>(i);
    }
    
    // Triangle indices (sequential for flat shading with glDrawArrays)
    result.triangleIndices.resize(result.vertexCount);
    std::iota(result.triangleIndices.begin(), result.triangleIndices.end(), 0);
    qInfo(meshProcessorLog)
        << "Vertex generation" << result.triangleCount << "tris," << result.vertexCount << "verts in"
        << stageTimer.elapsed() << "ms";
    qInfo(meshProcessorLog)
        << "Processed mesh:" << result.triangleCount << "tris," << result.vertexCount << "verts," << result.lineCount << "lines in"
        << meshTimer.elapsed() << "ms" << "(total" << loadTimer.elapsed() << "ms)";

    return result;
}

void MeshProcessor::computeBoundingBox(const std::vector<float>& positions, size_t numPoints,
                                       QVector3D& min, QVector3D& max)
{
    if (numPoints == 0) {
        min = QVector3D(0, 0, 0);
        max = QVector3D(1, 1, 1);
        return;
    }
    
    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float minZ = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();
    float maxZ = std::numeric_limits<float>::lowest();
    
    for (size_t i = 0; i < numPoints; ++i) {
        float x = positions[i*3+0];
        float y = positions[i*3+1];
        float z = positions[i*3+2];
        
        if (x < minX) minX = x;
        if (y < minY) minY = y;
        if (z < minZ) minZ = z;
        if (x > maxX) maxX = x;
        if (y > maxY) maxY = y;
        if (z > maxZ) maxZ = z;
    }
    
    min = QVector3D(minX, minY, minZ);
    max = QVector3D(maxX, maxY, maxZ);
}

void MeshProcessor::updateScalars(GPUMeshData& meshData, 
                                  const std::shared_ptr<UnstructuredGrid>& grid,
                                  const std::string& arrayName, 
                                  bool isPointData)
{
    if (!grid || meshData.vertexData.empty()) return;
    
    std::shared_ptr<DataArray> dataArray;
    
    if (isPointData) {
        auto it = grid->point_data.find(arrayName);
        if (it != grid->point_data.end()) {
            dataArray = it->second;
        }
    } else {
        auto it = grid->cell_data.find(arrayName);
        if (it != grid->cell_data.end()) {
            dataArray = it->second;
        }
    }
    
    if (!dataArray) return;
    
    const int numComp = static_cast<int>(dataArray->num_components);
    
    // Get scalar value at tuple index (handles vectors by computing magnitude)
    auto getScalar = [&](size_t tupleIdx) -> float {
        if (numComp == 1) {
            // Scalar data: direct access
            if (dataArray->data_type == "float" && tupleIdx < dataArray->data_float.size()) {
                return dataArray->data_float[tupleIdx];
            } else if (dataArray->data_type == "double" && tupleIdx < dataArray->data_double.size()) {
                return static_cast<float>(dataArray->data_double[tupleIdx]);
            } else if (dataArray->data_type == "int" && tupleIdx < dataArray->data_int32.size()) {
                return static_cast<float>(dataArray->data_int32[tupleIdx]);
            }
        } else {
            // Vector data: compute magnitude
            size_t baseIdx = tupleIdx * numComp;
            float sumSq = 0.0f;
            
            if (dataArray->data_type == "float") {
                for (int c = 0; c < numComp && (baseIdx + c) < dataArray->data_float.size(); ++c) {
                    float v = dataArray->data_float[baseIdx + c];
                    sumSq += v * v;
                }
            } else if (dataArray->data_type == "double") {
                for (int c = 0; c < numComp && (baseIdx + c) < dataArray->data_double.size(); ++c) {
                    float v = static_cast<float>(dataArray->data_double[baseIdx + c]);
                    sumSq += v * v;
                }
            } else if (dataArray->data_type == "int") {
                for (int c = 0; c < numComp && (baseIdx + c) < dataArray->data_int32.size(); ++c) {
                    float v = static_cast<float>(dataArray->data_int32[baseIdx + c]);
                    sumSq += v * v;
                }
            }
            return std::sqrt(sumSq);
        }
        return 0.0f;
    };
    
    // Find min/max
    size_t numTuples = static_cast<size_t>(dataArray->num_tuples);
    float minVal = std::numeric_limits<float>::max();
    float maxVal = std::numeric_limits<float>::lowest();
    
    for (size_t i = 0; i < numTuples; ++i) {
        float val = getScalar(i);
        if (val < minVal) minVal = val;
        if (val > maxVal) maxVal = val;
    }
    
    meshData.scalarMin = minVal;
    meshData.scalarMax = maxVal;
    
    float range = maxVal - minVal;
    if (range < 1e-10f) range = 1.0f;
    
    const size_t stride = 7;
    const size_t numVerts = meshData.vertexCount;
    
    if (isPointData && !meshData.vertexToPointIndex.empty()) {
        // Use the mapping from render vertices to original point indices
        for (size_t v = 0; v < numVerts; ++v) {
            uint32_t origIdx = meshData.vertexToPointIndex[v];
            float scalar = 0.5f;
            if (origIdx < numTuples) {
                scalar = (getScalar(origIdx) - minVal) / range;
            }
            meshData.vertexData[v * stride + 6] = scalar;
        }
    } else if (!isPointData && !meshData.vertexToCellIndex.empty()) {
        // Cell data: use the mapping from render vertices to cell indices
        for (size_t v = 0; v < numVerts; ++v) {
            uint32_t cellIdx = meshData.vertexToCellIndex[v];
            float scalar = 0.5f;
            if (cellIdx < numTuples) {
                scalar = (getScalar(cellIdx) - minVal) / range;
            }
            meshData.vertexData[v * stride + 6] = scalar;
        }
    } else {
        // Fallback: uniform scalar
        for (size_t v = 0; v < numVerts; ++v) {
            meshData.vertexData[v * stride + 6] = 0.5f;
        }
    }
}
