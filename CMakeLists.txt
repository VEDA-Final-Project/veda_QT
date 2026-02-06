cmake_minimum_required(VERSION 3.19)
project(rtsp_test LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Network Concurrent)

qt_standard_project_setup()

qt_add_executable(rtsp_test
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    videothread.cpp
    videothread.h
    metadatathread.cpp
    metadatathread.h
    ocrmanager.cpp
    ocrmanager.h
)

target_link_libraries(rtsp_test
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::Network
        Qt::Concurrent
)

include(GNUInstallDirs)

install(TARGETS rtsp_test
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET rtsp_test
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})

# OpenCV 패키지 찾기
set(OpenCV_DIR "D:/opencv/build/x64/vc16/lib")
find_package(OpenCV REQUIRED)

# 헤더 파일 경로 포함
include_directories(${OpenCV_INCLUDE_DIRS})

# 실행 파일에 OpenCV 라이브러리 연결feature/video-receiver
# 실행 파일에 OpenCV 라이브러리 연결feature/video-receiver
target_link_libraries(${PROJECT_NAME} PRIVATE ${OpenCV_LIBS})

# Tesseract & Leptonica 찾기 (vcpkg)
# vcpkg 경로 추가 (사용자 환경에 맞게)
list(APPEND CMAKE_PREFIX_PATH "C:/vcpkg/installed/x64-windows")

find_package(Tesseract REQUIRED)
find_package(Leptonica REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE Tesseract::libtesseract leptonica)
