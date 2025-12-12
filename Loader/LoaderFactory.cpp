//
// Created by Celestee on 2025/12/8.
//

#include "LoaderFactory.hpp"
#include "VTKLegacyLoader.hpp"
std::shared_ptr<Loader> LoaderFactory::createLoader(std::filesystem::path file_path)
{
    std::string extension = file_path.extension().string();
    if (extension == ".vtk" || extension == ".VTK") {
        return std::make_shared<VTKLegacyLoader>(file_path);
    }
    return nullptr;

}

