//
// Created by Celestee on 2025/12/8.
//

#ifndef UNIFYLOADER_VTKLOADER_HPP
#define UNIFYLOADER_VTKLOADER_HPP

#include <memory>
#include <string>
#include <vector>

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <variant>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#include "Loader.hpp" // Assuming this base class exists as per your provided code

// --- Data Structures ---

struct Header {
    std::string version;
    std::string title;
    std::string format; // ASCII or BINARY
    std::string dataset_type;
};



// --- Loader Class ---

class VTKLegacyLoader : public Loader {
public:
    VTKLegacyLoader() = default;
    explicit VTKLegacyLoader(std::filesystem::path file_path) : Loader(file_path) {}
    ~VTKLegacyLoader();

    virtual bool load() override;


    std::string getLastError() const { return last_error_; }

private:
    // Core resource management
    bool mapFile();
    void unmapFile();

    // Parsing flow
    bool parseHeader();
    bool parseDatasetStructure();

    // ASCII Parsers
    bool parsePointsASCII();
    bool parseCellsASCII();
    bool parseCellTypesASCII();
    bool parseDataASCII(bool is_point_data); // Generic for both Point and Cell data

    // Binary Parsers
    bool parsePointsBinary();
    bool parseCellsBinary();
    bool parseCellTypesBinary();
    bool parseDataBinary(bool is_point_data);

    // Helper functions
    void skipWhitespace();
    bool readKeyword(std::string& keyword);
    bool readLine(std::string& line); // Reads until newline

    // ASCII value readers
    bool readInt64(int64_t& value);
    bool readInt32(int32_t& value);
    bool readFloat(float& value);
    bool readDouble(double& value);

    // Binary value readers (handle swapping)
    void swapBytes(void* data, size_t size);
    template<typename T> void swapBuffer(T* data, size_t count);

    template<typename T>
    bool readBinaryArray(T* dest, size_t count);

    // Member variables
#ifdef _WIN32
    HANDLE file_handle_ = INVALID_HANDLE_VALUE;
    HANDLE map_handle_ = nullptr;
#else
    int file_descriptor_ = -1;
#endif
    char* file_data_ = nullptr;
    size_t file_size_ = 0;
    size_t current_pos_ = 0;
    std::string last_error_;

    Header header_;

};

#endif //UNIFYLOADER_VTKLOADER_HPP