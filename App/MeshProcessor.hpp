#ifndef MESHPROCESSOR_HPP
#define MESHPROCESSOR_HPP

#include <QVector3D>
#include <QStringList>
#include <memory>
#include <vector>
#include "Loader.hpp"

// Optimized GPU-ready mesh data with flat shading support
struct GPUMeshData {
    // Interleaved vertex data: position (3) + normal (3) + scalar (1) = 7 floats per vertex
    std::vector<float> vertexData;
    std::vector<uint32_t> triangleIndices;
    std::vector<uint32_t> lineIndices;
    std::vector<uint32_t> pointIndices;
    
    // Mapping from flat-shaded vertex index to original point index
    std::vector<uint32_t> vertexToPointIndex;
    
    // Mapping from flat-shaded vertex index to cell index (for cell data)
    std::vector<uint32_t> vertexToCellIndex;
    
    QVector3D boundingBoxMin;
    QVector3D boundingBoxMax;
    
    size_t vertexCount = 0;
    size_t triangleCount = 0;
    size_t lineCount = 0;
    
    float scalarMin = 0.0f;
    float scalarMax = 1.0f;
    
    // Flat shading: each triangle has its own vertices
    bool useFlatShading = true;
};

// Face structure for sorting-based surface extraction
// Uses sorted indices for comparison and original indices for rendering
struct Face {
    uint32_t sorted[4];  // Sorted indices for canonical representation
    uint32_t orig[4];    // Original indices for correct winding order
    uint32_t cellIdx;    // Index of the cell this face belongs to
    uint8_t n;           // Number of vertices (3 or 4)
    
    // Set triangle face with proper sorting
    void set3(uint32_t a, uint32_t b, uint32_t c, uint32_t cell = 0) {
        n = 3;
        cellIdx = cell;
        orig[0] = a; orig[1] = b; orig[2] = c; orig[3] = 0;
        
        // Sort for canonical representation
        sorted[0] = a; sorted[1] = b; sorted[2] = c; sorted[3] = UINT32_MAX;
        if (sorted[0] > sorted[1]) std::swap(sorted[0], sorted[1]);
        if (sorted[1] > sorted[2]) std::swap(sorted[1], sorted[2]);
        if (sorted[0] > sorted[1]) std::swap(sorted[0], sorted[1]);
    }
    
    // Set quad face with proper sorting
    void set4(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t cell = 0) {
        n = 4;
        cellIdx = cell;
        orig[0] = a; orig[1] = b; orig[2] = c; orig[3] = d;
        
        // Sort for canonical representation (sorting network for 4 elements)
        sorted[0] = a; sorted[1] = b; sorted[2] = c; sorted[3] = d;
        if (sorted[0] > sorted[1]) std::swap(sorted[0], sorted[1]);
        if (sorted[2] > sorted[3]) std::swap(sorted[2], sorted[3]);
        if (sorted[0] > sorted[2]) std::swap(sorted[0], sorted[2]);
        if (sorted[1] > sorted[3]) std::swap(sorted[1], sorted[3]);
        if (sorted[1] > sorted[2]) std::swap(sorted[1], sorted[2]);
    }
    
    // Comparison for std::sort
    bool operator<(const Face& other) const {
        if (n != other.n) return n < other.n;
        for (int i = 0; i < n; ++i) {
            if (sorted[i] != other.sorted[i]) 
                return sorted[i] < other.sorted[i];
        }
        return false;
    }
    
    // Equality for duplicate detection
    bool operator==(const Face& other) const {
        if (n != other.n) return false;
        for (int i = 0; i < n; ++i) {
            if (sorted[i] != other.sorted[i]) return false;
        }
        return true;
    }
};

class MeshProcessor
{
public:
    MeshProcessor() = default;
    
    GPUMeshData process(const std::shared_ptr<UnstructuredGrid>& grid);
    
    void updateScalars(GPUMeshData& meshData, 
                       const std::shared_ptr<UnstructuredGrid>& grid,
                       const std::string& arrayName, 
                       bool isPointData);

    QStringList getPointDataArrayNames() const { return m_pointDataNames; }
    QStringList getCellDataArrayNames() const { return m_cellDataNames; }

private:
    enum VTKCellType {
        VTK_VERTEX = 1,
        VTK_POLY_VERTEX = 2,
        VTK_LINE = 3,
        VTK_POLY_LINE = 4,
        VTK_TRIANGLE = 5,
        VTK_TRIANGLE_STRIP = 6,
        VTK_POLYGON = 7,
        VTK_QUAD = 9,
        VTK_TETRA = 10,
        VTK_VOXEL = 11,
        VTK_HEXAHEDRON = 12,
        VTK_WEDGE = 13,
        VTK_PYRAMID = 14
    };

    void computeBoundingBox(const std::vector<float>& positions,
                            size_t numPoints,
                            QVector3D& min, QVector3D& max);

    QStringList m_pointDataNames;
    QStringList m_cellDataNames;
};

#endif // MESHPROCESSOR_HPP
