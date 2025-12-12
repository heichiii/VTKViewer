//
// Created by Celestee on 2025/12/8.
//

#ifndef UNIFYLOADER_LOADERFACTORY_HPP
#define UNIFYLOADER_LOADERFACTORY_HPP
#include <memory>
#include "Loader.hpp"

class LoaderFactory
{
public:
    // LoaderFactory(std::filesystem::path): file_path(file_path) {}
    // LoaderFactory(std::string): file_path(std::filesystem::path(file_path)) {}
    static std::shared_ptr<Loader> createLoader(std::filesystem::path file_path);
private:
    // std::filesystem::path file_path;
};
#endif //UNIFYLOADER_LOADERFACTORY_HPP