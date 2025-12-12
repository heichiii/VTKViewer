//
// Created by Celestee on 2025/12/8.
//

#ifndef UNIFYLOADER_LOADER_HPP
#define UNIFYLOADER_LOADER_HPP

#include <map>
#include <string>
#include <filesystem>
#include <vector>
#include <cstdint>
#include <memory>

// Generic container for data arrays (Scalars, Vectors, Fields)
struct DataArray {
    std::string name;
    int64_t num_components;
    int64_t num_tuples;
    std::string data_type; // int, float, double

    // Store data as vectors of basic types. Only one will be active.
    std::vector<float> data_float;
    std::vector<double> data_double;
    std::vector<int32_t> data_int32;
    std::vector<int64_t> data_int64; // Also used for connectivity if needed

    void resize(size_t size) {
        if (data_type == "float") data_float.resize(size);
        else if (data_type == "double") data_double.resize(size);
        else if (data_type == "int") data_int32.resize(size);
        else if (data_type == "vtktypeint64") data_int64.resize(size);
    }
};

struct UnstructuredGrid {
    int64_t num_points;
    int64_t num_cells;

    // Geometry
    std::shared_ptr<DataArray> points;
    std::vector<int32_t> cells;       // Flattened legacy format: [n, id1, id2, ..., n, id1...]
    std::vector<uint8_t> cell_types;

    // Attributes
    std::map<std::string, std::shared_ptr<DataArray>> point_data;
    std::map<std::string, std::shared_ptr<DataArray>> cell_data;
};

class Loader
{
public:
    Loader();
    Loader(std::string file_path): file_path_(file_path) {}
    Loader(std::filesystem::path file_path): file_path_(file_path) {}
    virtual ~Loader() = default;
    virtual bool load()=0;
    void setFilePath(const std::string& path) { file_path_ = path; }
    std::shared_ptr<UnstructuredGrid> getGrid() const { return std::make_shared<UnstructuredGrid>(grid_); }
protected:
    std::string last_error_;

    std::filesystem::path file_path_;

    UnstructuredGrid grid_;

};
#endif //UNIFYLOADER_LOADER_HPP