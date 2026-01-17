cmake_minimum_required(VERSION 3.16)
project(SimpleViewer VERSION 1.0 LANGUAGES CXX)

set(CMAKE_C_COMPILER "cl")
set(CMAKE_CXX_COMPILER "cl")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Performance flags
if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /arch:AVX2 /fp:fast /GL")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG")
else()
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -march=native -ffast-math -flto")
endif()

set(Qt6_DIR "D:/dev/libs/Qt6.9/6.9.1/msvc2022_64/lib/cmake/Qt6")
# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)

# Find OpenMP for parallel processing
find_package(OpenMP)

# Source files
set(LOADER_SOURCES
    Loader/Loader.cpp
    Loader/Loader.hpp
    Loader/LoaderFactory.cpp
    Loader/LoaderFactory.hpp
    Loader/VTKLegacyLoader.cpp
    Loader/VTKLegacyLoader.hpp
)

set(APP_SOURCES
    App/main.cpp
    App/MainWindow.cpp
    App/MainWindow.hpp
    App/GLWidget.cpp
    App/GLWidget.hpp
    App/MeshProcessor.cpp
    App/MeshProcessor.hpp
    App/Camera.hpp
)

set(SHADER_RESOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Resources/shaders.qrc
)

add_executable(SimpleViewer
    ${LOADER_SOURCES}
    ${APP_SOURCES}
    ${SHADER_RESOURCES}
)

target_include_directories(SimpleViewer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Loader
    ${CMAKE_CURRENT_SOURCE_DIR}/App
)

target_link_libraries(SimpleViewer PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
)

if(OpenMP_CXX_FOUND)
    target_link_libraries(SimpleViewer PRIVATE OpenMP::OpenMP_CXX)
    target_compile_definitions(SimpleViewer PRIVATE USE_OPENMP)
endif()

# Copy VTK files to build directory for testing
#file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/VTKFile DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
