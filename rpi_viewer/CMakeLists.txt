cmake_minimum_required(VERSION 3.19)
project(rpi_db_viewer LANGUAGES CXX)

find_package(Qt6 6.0 REQUIRED COMPONENTS Core Widgets Network)
qt_standard_project_setup()

qt_add_executable(rpi_db_viewer
    WIN32 MACOSX_BUNDLE
    main.cpp
)

target_link_libraries(rpi_db_viewer
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::Network
)

# RPi cross-compile tip:
#   cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=/path/to/rpi-toolchain.cmake
#   cmake --build build
