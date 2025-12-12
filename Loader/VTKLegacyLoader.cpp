//
// Created by Celestee on 2025/12/8.
//

#include "VTKLegacyLoader.hpp"

#include <iostream>
#include <sstream>
#include <cmath>

// ==========================================
// Resource Management & Main Flow
// ==========================================

VTKLegacyLoader::~VTKLegacyLoader() {
    unmapFile();
}

bool VTKLegacyLoader::load() {
    if (!mapFile()) return false;

    if (!parseHeader()) {
        unmapFile();
        return false;
    }

    bool is_binary = (header_.format == "BINARY");

    // Order usually matters in VTK files
    if (!parseDatasetStructure()) {
         unmapFile(); return false;
    }

    // Main parsing loop for sections
    while (current_pos_ < file_size_) {
        skipWhitespace();
        if (current_pos_ >= file_size_) break;

        size_t saved_pos = current_pos_;
        std::string keyword;
        if (!readKeyword(keyword)) break;

        bool success = true;

        if (keyword == "POINTS") {
            success = is_binary ? parsePointsBinary() : parsePointsASCII();
        }
        else if (keyword == "CELLS") {
            success = is_binary ? parseCellsBinary() : parseCellsASCII();
        }
        else if (keyword == "CELL_TYPES") {
            success = is_binary ? parseCellTypesBinary() : parseCellTypesASCII();
        }
        else if (keyword == "POINT_DATA") {
            success = is_binary ? parseDataBinary(true) : parseDataASCII(true);
        }
        else if (keyword == "CELL_DATA") {
            success = is_binary ? parseDataBinary(false) : parseDataASCII(false);
        }
        else if (keyword == "METADATA") {
            // Simple skip for metadata block for now to prevent errors
            // In a full implementation, you'd parse INFORMATION blocks here
            std::string subkey;
            readKeyword(subkey);
            if(subkey == "INFORMATION") {
                int64_t count;
                readInt64(count);
                // logic to skip lines or parse would go here
            }
        }
        else {
            // Unknown keyword or end of meaningful data
            // For robustness, we might just try to read next line
             std::string dummy;
             readLine(dummy);
        }

        if (!success) {
            unmapFile();
            return false;
        }
    }

    unmapFile();
    return true;
}

bool VTKLegacyLoader::mapFile() {
#ifdef _WIN32
    file_handle_ = CreateFileA(file_path_.string().c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (file_handle_ == INVALID_HANDLE_VALUE) {
        last_error_ = "Failed to open file: " + file_path_.string();
        return false;
    }

    LARGE_INTEGER file_size;
    if (!GetFileSizeEx((HANDLE)file_handle_, &file_size)) {
        last_error_ = "Failed to get file size";
        CloseHandle(file_handle_);
        file_handle_ = INVALID_HANDLE_VALUE;
        return false;
    }
    file_size_ = static_cast<size_t>(file_size.QuadPart);

    map_handle_ = CreateFileMappingA(file_handle_, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!map_handle_) {
        last_error_ = "Failed to create file mapping";
        CloseHandle(file_handle_);
        file_handle_ = INVALID_HANDLE_VALUE;
        return false;
    }

    file_data_ = static_cast<char*>(MapViewOfFile(map_handle_, FILE_MAP_READ, 0, 0, 0));
    if (!file_data_) {
        last_error_ = "Failed to map view of file";
        CloseHandle(map_handle_);
        CloseHandle(file_handle_);
        map_handle_ = nullptr;
        file_handle_ = INVALID_HANDLE_VALUE;
        return false;
    }
#else
    file_descriptor_ = open(file_path_.string().c_str(), O_RDONLY);
    if (file_descriptor_ == -1) {
        last_error_ = "Failed to open file";
        return false;
    }
    struct stat sb;
    fstat(file_descriptor_, &sb);
    file_size_ = sb.st_size;

    file_data_ = static_cast<char*>(mmap(nullptr, file_size_, PROT_READ, MAP_PRIVATE, file_descriptor_, 0));
    if (file_data_ == MAP_FAILED) {
        last_error_ = "Failed to mmap file";
        close(file_descriptor_);
        return false;
    }
#endif
    current_pos_ = 0;
    return true;
}

void VTKLegacyLoader::unmapFile() {
    if (file_data_) {
#ifdef _WIN32
        UnmapViewOfFile(file_data_);
        if (map_handle_) CloseHandle(map_handle_);
        if (file_handle_ != INVALID_HANDLE_VALUE) CloseHandle(file_handle_);
        map_handle_ = nullptr;
        file_handle_ = INVALID_HANDLE_VALUE;
#else
        munmap(file_data_, file_size_);
        if (file_descriptor_ != -1) close(file_descriptor_);
        file_descriptor_ = -1;
#endif
        file_data_ = nullptr;
    }
}

// ==========================================
// Parsing Logic
// ==========================================

bool VTKLegacyLoader::parseHeader() {
    if (current_pos_ + 23 > file_size_) {
        last_error_ = "File too small";
        return false;
    }

    // Verify magic prefix
    std::string magic(file_data_ + current_pos_, 23);
    if (magic.find("# vtk DataFile Version") != 0) {
        last_error_ = "Invalid VTK file header";
        return false;
    }

    // Read the full first line and extract only the version number (e.g., "5.1")
    std::string first_line;
    readLine(first_line); // advances current_pos_ past the newline
    // first_line should be like: "# vtk DataFile Version X.Y"
    // Extract the trailing token as version
    {
        std::stringstream vss(first_line);
        std::string tok;
        while (vss >> tok) { /* consume */ }
        header_.version = tok; // last token after splitting
    }

    // Read Title line
    readLine(header_.title);

    // Read Format (ASCII/BINARY)
    std::string format_line;
    readLine(format_line);
    std::stringstream ss(format_line);
    ss >> header_.format;

    if (header_.format != "ASCII" && header_.format != "BINARY") {
        last_error_ = "Unknown format: " + header_.format;
        return false;
    }

    return true;
}

bool VTKLegacyLoader::parseDatasetStructure() {
    skipWhitespace();
    std::string keyword;
    readKeyword(keyword);
    if (keyword != "DATASET") {
        last_error_ = "Expected DATASET keyword";
        return false;
    }
    readKeyword(header_.dataset_type);

    if (header_.dataset_type != "UNSTRUCTURED_GRID") {
        last_error_ = "Only UNSTRUCTURED_GRID supported, got " + header_.dataset_type;
        return false;
    }
    return true;
}

// ---------------- ASCII Parsers ----------------

bool VTKLegacyLoader::parsePointsASCII() {
    // Note: Keyword "POINTS" already consumed in main loop
    int64_t num_points;
    if (!readInt64(num_points)) return false;
    grid_.num_points = num_points;

    std::string data_type;
    readKeyword(data_type); // e.g. "float"

    grid_.points = std::make_shared<DataArray>();
    grid_.points->name = "Points";
    grid_.points->num_components = 3;
    grid_.points->num_tuples = num_points;
    grid_.points->data_type = data_type;

    size_t total_values = num_points * 3;

    if (data_type == "float") {
        grid_.points->data_float.resize(total_values);
        for(size_t i=0; i<total_values; ++i) {
            if(!readFloat(grid_.points->data_float[i])) return false;
        }
    } else if (data_type == "double") {
        grid_.points->data_double.resize(total_values);
        for(size_t i=0; i<total_values; ++i) {
            if(!readDouble(grid_.points->data_double[i])) return false;
        }
    } else {
        last_error_ = "Unsupported point data type: " + data_type;
        return false;
    }

    return true;
}

bool VTKLegacyLoader::parseCellsASCII() {
    int64_t num_cells, size_param;
    if (!readInt64(num_cells)) return false;
    // num_cells--; // Removed incorrect decrement
    if (!readInt64(size_param)) return false;

    grid_.num_cells = num_cells;

    // Check if new format (OFFSETS/CONNECTIVITY) or old format
    size_t saved_pos = current_pos_;
    skipWhitespace();
    std::string next_keyword;

    // Peek keyword
    if (readKeyword(next_keyword) && next_keyword == "OFFSETS") {
        // Modern XML-style format inside legacy file
        std::string offset_type;
        readKeyword(offset_type); // vtktypeint64

        std::vector<int64_t> offsets;
        offsets.resize(num_cells + 1);
        for(int64_t i=0; i<=num_cells; ++i) {
            if (!readInt64(offsets[i])) return false;
        }

        skipWhitespace();
        std::string conn_kw;
        readKeyword(conn_kw); // CONNECTIVITY
        if (conn_kw != "CONNECTIVITY") return false;

        std::string conn_type;
        readKeyword(conn_type);

        int64_t total_conn = offsets.back();
        std::vector<int64_t> connectivity(total_conn);
        for(int64_t i=0; i<total_conn; ++i) {
            if (!readInt64(connectivity[i])) return false;
        }

        // Convert to legacy flat format [n, p0, p1, ..., n, p0...]
        grid_.cells.clear();
        grid_.cells.reserve(static_cast<size_t>(num_cells) + static_cast<size_t>(total_conn));
        for(int64_t i=0; i<num_cells; ++i) {
            int64_t n = offsets[i+1] - offsets[i];
            grid_.cells.push_back(static_cast<int32_t>(n));
            for(int64_t k=0; k<n; ++k) {
                grid_.cells.push_back(static_cast<int32_t>(connectivity[static_cast<size_t>(offsets[i] + k)]));
            }
        }
    } else {
        // Old format: direct integer list
        current_pos_ = saved_pos; // Restore
        grid_.cells.clear();
        grid_.cells.resize(static_cast<size_t>(size_param));
        for(int64_t i=0; i<size_param; ++i) {
            if (!readInt32(grid_.cells[static_cast<size_t>(i)])) return false;
        }
    }
    return true;
}

bool VTKLegacyLoader::parseCellTypesASCII() {
    int64_t num_types;
    if (!readInt64(num_types)) return false;

    grid_.cell_types.resize(num_types);
    for(int64_t i=0; i<num_types; ++i) {
        int32_t t;
        readInt32(t);
        grid_.cell_types[i] = static_cast<uint8_t>(t);
    }
    return true;
}

// Handles parsing of POINT_DATA or CELL_DATA blocks and their underlying arrays (SCALARS, VECTORS, FIELD)
bool VTKLegacyLoader::parseDataASCII(bool is_point_data) {
    int64_t num_tuples;
    readInt64(num_tuples); // Count of data points

    // We keep reading keywords until we hit something that isn't a data definition or EOF
    while (true) {
        skipWhitespace();
        if (current_pos_ >= file_size_) break;

        size_t saved_pos = current_pos_;
        std::string keyword;
        if (!readKeyword(keyword)) break;

        if (keyword == "SCALARS") {
            std::string name, type;
            int64_t components = 1;
            readKeyword(name);
            readKeyword(type);

            // Optional component count
            size_t comp_pos = current_pos_;
            int64_t temp_comp;
            if (readInt64(temp_comp)) {
                components = temp_comp;
            } else {
                current_pos_ = comp_pos; // backtrack
            }

            // LOOKUP_TABLE line
            skipWhitespace();
            std::string lut_kw;
            readKeyword(lut_kw); // LOOKUP_TABLE
            std::string lut_name;
            readKeyword(lut_name); // default

            auto array = std::make_shared<DataArray>();
            array->name = name;
            array->data_type = type;
            array->num_components = components;
            array->num_tuples = num_tuples;
            size_t total = components * num_tuples;

            if (type == "float") {
                array->data_float.resize(total);
                for(size_t i=0; i<total; ++i) readFloat(array->data_float[i]);
            } else if (type == "double") {
                array->data_double.resize(total);
                for(size_t i=0; i<total; ++i) readDouble(array->data_double[i]);
            } else if (type == "int") {
                array->data_int32.resize(total);
                for(size_t i=0; i<total; ++i) readInt32(array->data_int32[i]);
            }

            if (is_point_data) grid_.point_data[name] = array;
            else grid_.cell_data[name] = array;

        } else if (keyword == "VECTORS") {
            std::string name, type;
            readKeyword(name);
            readKeyword(type);

            auto array = std::make_shared<DataArray>();
            array->name = name;
            array->data_type = type;
            array->num_components = 3;
            array->num_tuples = num_tuples;
            size_t total = 3 * num_tuples;

            // Reading implementation similar to SCALARS...
             if (type == "float") {
                array->data_float.resize(total);
                for(size_t i=0; i<total; ++i) readFloat(array->data_float[i]);
            }
            // ... (double implementation)

            if (is_point_data) grid_.point_data[name] = array;
            else grid_.cell_data[name] = array;

        } else if (keyword == "FIELD") {
            std::string field_name; // Usually "FieldData"
            readKeyword(field_name);
            int64_t num_arrays;
            readInt64(num_arrays);

            for (int i = 0; i < num_arrays; ++i) {
                std::string array_name;
                int64_t comps, tuples;
                std::string type;
                readKeyword(array_name);
                readInt64(comps);
                readInt64(tuples);
                readKeyword(type);

                auto array = std::make_shared<DataArray>();
                array->name = array_name;
                array->data_type = type;
                array->num_components = comps;
                array->num_tuples = tuples;

                size_t total = comps * tuples;
                if (type == "float") {
                    array->data_float.resize(total);
                    for(size_t k=0; k<total; ++k) readFloat(array->data_float[k]);
                } else if (type == "double") {
                     array->data_double.resize(total);
                    for(size_t k=0; k<total; ++k) readDouble(array->data_double[k]);
                } else if (type == "int") {
                     array->data_int32.resize(total);
                    for(size_t k=0; k<total; ++k) readInt32(array->data_int32[k]);
                }

                if (is_point_data) grid_.point_data[array_name] = array;
                else grid_.cell_data[array_name] = array;
            }

        } else {
            // Found a keyword that belongs to the next section (e.g. CELL_DATA found while parsing POINT_DATA)
            current_pos_ = saved_pos;
            return true;
        }
    }
    return true;
}

// ---------------- Binary Parsers ----------------

bool VTKLegacyLoader::parsePointsBinary() {
    int64_t num_points;
    if (!readInt64(num_points)) return false;
    grid_.num_points = num_points;

    std::string data_type;
    readKeyword(data_type);

    // In binary, we need to skip the newline character after the type declaration
    while (current_pos_ < file_size_ && std::isspace(static_cast<unsigned char>(file_data_[current_pos_]))) current_pos_++;

    grid_.points = std::make_shared<DataArray>();
    grid_.points->name = "Points";
    grid_.points->num_components = 3;
    grid_.points->num_tuples = num_points;
    grid_.points->data_type = data_type;

    size_t total = num_points * 3;

    if (data_type == "float") {
        grid_.points->data_float.resize(total);
        if (!readBinaryArray(grid_.points->data_float.data(), total)) return false;
    } else if (data_type == "double") {
        grid_.points->data_double.resize(total);
        if (!readBinaryArray(grid_.points->data_double.data(), total)) return false;
    }
    return true;
}

bool VTKLegacyLoader::parseCellsBinary() {
    int64_t num_cells, size_param;
    if (!readInt64(num_cells)) return false;
    if (!readInt64(size_param)) return false;
    grid_.num_cells = num_cells;

    // Check OFFSETS/CONNECTIVITY vs Legacy
    size_t saved_pos = current_pos_;
    skipWhitespace();
    std::string next_keyword;

    if (readKeyword(next_keyword) && next_keyword == "OFFSETS") {
        // --- Modern Binary Format ---
        std::string offset_type;
        readKeyword(offset_type);
        // Consume newline
        while (current_pos_ < file_size_ && std::isspace(static_cast<unsigned char>(file_data_[current_pos_]))) current_pos_++;

        std::vector<int64_t> offsets(num_cells + 1);
        if(!readBinaryArray(offsets.data(), num_cells + 1)) return false;

        skipWhitespace();
        std::string conn_kw, conn_type;
        readKeyword(conn_kw);
        readKeyword(conn_type);
        while (current_pos_ < file_size_ && std::isspace(static_cast<unsigned char>(file_data_[current_pos_]))) current_pos_++;

        int64_t total_conn = offsets.back();
        std::vector<int64_t> connectivity(total_conn);
        if(!readBinaryArray(connectivity.data(), total_conn)) return false;

        // Convert to legacy structure for internal consistency
        grid_.cells.reserve(static_cast<size_t>(num_cells) + static_cast<size_t>(total_conn));
        for(int64_t i=0; i<num_cells; ++i) {
            int64_t n = offsets[i+1] - offsets[i];
            grid_.cells.push_back((int32_t)n);
            for(int64_t k=0; k<n; ++k) {
                grid_.cells.push_back((int32_t)connectivity[offsets[i] + k]);
            }
        }

    } else {
        // --- Legacy Binary Format ---
        current_pos_ = saved_pos;
        while (current_pos_ < file_size_ && std::isspace(static_cast<unsigned char>(file_data_[current_pos_]))) current_pos_++;

        grid_.cells.resize(size_param);
        // Legacy binary cells are always 32-bit int
        if(!readBinaryArray(grid_.cells.data(), static_cast<size_t>(size_param))) return false;
    }

    return true;
}

bool VTKLegacyLoader::parseCellTypesBinary() {
    int64_t num_types;
    if (!readInt64(num_types)) return false;

    while (current_pos_ < file_size_ && std::isspace(static_cast<unsigned char>(file_data_[current_pos_]))) current_pos_++;

    std::vector<int32_t> temp_types(num_types);
    if (!readBinaryArray(temp_types.data(), static_cast<size_t>(num_types))) return false;

    grid_.cell_types.resize(num_types);
    for(size_t i=0; i<static_cast<size_t>(num_types); ++i) grid_.cell_types[i] = static_cast<uint8_t>(temp_types[i]);

    return true;
}

bool VTKLegacyLoader::parseDataBinary(bool is_point_data) {
    int64_t num_tuples;
    readInt64(num_tuples);

    while (true) {
        skipWhitespace();
        if (current_pos_ >= file_size_) break;
        size_t saved_pos = current_pos_;
        std::string keyword;
        if (!readKeyword(keyword)) break;

        if (keyword == "FIELD") {
            std::string field_name;
            readKeyword(field_name);
            int64_t num_arrays;
            readInt64(num_arrays);

            for (int i = 0; i < num_arrays; ++i) {
                std::string array_name, type;
                int64_t comps, tuples;
                readKeyword(array_name);
                readInt64(comps);
                readInt64(tuples);
                readKeyword(type);

                // Eat newline before binary block
                while (current_pos_ < file_size_ && std::isspace(static_cast<unsigned char>(file_data_[current_pos_]))) current_pos_++;

                auto array = std::make_shared<DataArray>();
                array->name = array_name;
                array->data_type = type;
                array->num_components = comps;
                array->num_tuples = tuples;

                size_t total = static_cast<size_t>(comps) * static_cast<size_t>(tuples);
                if (type == "float") {
                    array->data_float.resize(total);
                    readBinaryArray(array->data_float.data(), total);
                } else if (type == "double") {
                    array->data_double.resize(total);
                    readBinaryArray(array->data_double.data(), total);
                } else if (type == "int") {
                    array->data_int32.resize(total);
                    readBinaryArray(array->data_int32.data(), total);
                }

                if (is_point_data) grid_.point_data[array->name] = array;
                else grid_.cell_data[array->name] = array;
            }
        }
        else if (keyword == "SCALARS" || keyword == "VECTORS") {
            // Minimal fallback: skip block header and consume newline to avoid isspace assertion
            std::string name, type;
            readKeyword(name);
            readKeyword(type);
            while (current_pos_ < file_size_ && std::isspace(static_cast<unsigned char>(file_data_[current_pos_]))) current_pos_++;
            // Not fully implemented; rely on FIELD parsing for provided files.
        }
        else {
            current_pos_ = saved_pos;
            return true;
        }
    }
    return true;
}

// ==========================================
// Helpers & Low Level IO
// ==========================================

void VTKLegacyLoader::skipWhitespace() {
    while (current_pos_ < file_size_ && std::isspace(static_cast<unsigned char>(file_data_[current_pos_]))) {
        current_pos_++;
    }
}

bool VTKLegacyLoader::readKeyword(std::string &keyword) {
    skipWhitespace();
    if (current_pos_ >= file_size_) return false;

    size_t start = current_pos_;
    while (current_pos_ < file_size_ && !std::isspace(static_cast<unsigned char>(file_data_[current_pos_]))) {
        current_pos_++;
    }
    keyword = std::string(file_data_ + start, current_pos_ - start);
    return true;
}

bool VTKLegacyLoader::readLine(std::string &line) {
    line.clear();
    // Skip optional leading newline if we are exactly on one
    if (current_pos_ < file_size_ && file_data_[current_pos_] == '\n') current_pos_++;

    while (current_pos_ < file_size_) {
        char c = file_data_[current_pos_];
        current_pos_++;
        if (c == '\n') break;
        if (c != '\r') line += c;
    }
    return true;
}

bool VTKLegacyLoader::readInt64(int64_t& value) {
    skipWhitespace();
    if (current_pos_ >= file_size_) return false;
    char* end;
    value = std::strtoll(file_data_ + current_pos_, &end, 10);
    if (end == file_data_ + current_pos_) return false;
    current_pos_ = end - file_data_;
    return true;
}

bool VTKLegacyLoader::readInt32(int32_t& value) {
    int64_t v64;
    if(!readInt64(v64)) return false;
    value = static_cast<int32_t>(v64);
    return true;
}

bool VTKLegacyLoader::readFloat(float& value) {
    skipWhitespace();
    if (current_pos_ >= file_size_) return false;
    char* end;
    value = std::strtof(file_data_ + current_pos_, &end);
    if (end == file_data_ + current_pos_) return false;
    current_pos_ = end - file_data_;
    return true;
}

bool VTKLegacyLoader::readDouble(double& value) {
    skipWhitespace();
    if (current_pos_ >= file_size_) return false;
    char* end;
    value = std::strtod(file_data_ + current_pos_, &end);
    if (end == file_data_ + current_pos_) return false;
    current_pos_ = end - file_data_;
    return true;
}

// --- Binary Helpers ---

template<typename T>
void VTKLegacyLoader::swapBuffer(T* data, size_t count) {
    // VTK Binary is Big Endian. Convert to Little Endian (if on x86)
    // Assuming host is Little Endian.
    char* bytes = reinterpret_cast<char*>(data);
    size_t elem_size = sizeof(T);
    for(size_t i=0; i<count; ++i) {
        char* ptr = bytes + (i * elem_size);
        for(size_t j=0; j<elem_size/2; ++j) {
            std::swap(ptr[j], ptr[elem_size - 1 - j]);
        }
    }
}

template<typename T>
bool VTKLegacyLoader::readBinaryArray(T* dest, size_t count) {
    size_t bytes_needed = count * sizeof(T);
    if (current_pos_ + bytes_needed > file_size_) {
        last_error_ = "Unexpected EOF in binary block";
        return false;
    }
    std::memcpy(dest, file_data_ + current_pos_, bytes_needed);
    swapBuffer(dest, count);
    current_pos_ += bytes_needed;
    return true;
}