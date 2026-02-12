cmake_minimum_required(VERSION 3.19)
project(rtsp_test LANGUAGES CXX)

# Qt6 필수 구성 요소 찾기
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Network Concurrent Sql)

qt_standard_project_setup()

# 실행 파일 생성 및 소스 파일 정의
qt_add_executable(rtsp_test
    WIN32 MACOSX_BUNDLE
    # Main
    src/main.cpp
    # UI
    src/ui/windows/mainwindow.cpp
    src/ui/windows/mainwindow.h
    src/ui/windows/mainwindowcontroller.cpp
    src/ui/windows/mainwindowcontroller.h
    src/ui/video/videowidget.cpp
    src/ui/video/videowidget.h
    src/ui/roi/roiinteractionstate.cpp
    src/ui/roi/roiinteractionstate.h
    src/ui/video/videoframerenderer.cpp
    src/ui/video/videoframerenderer.h
    # Video
    src/video/videothread.cpp
    src/video/videothread.h
    # Metadata
    src/metadata/metadatathread.cpp
    src/metadata/metadatathread.h
    # OCR
    src/ocr/ocrmanager.cpp
    src/ocr/ocrmanager.h
    src/ocr/plateocrcoordinator.cpp
    src/ocr/plateocrcoordinator.h
    # Core
    src/config/config.cpp
    src/config/config.h
    src/camera/cameramanager.cpp
    src/camera/cameramanager.h
    src/camera/camerasessionservice.cpp
    src/camera/camerasessionservice.h
    src/logging/logdeduplicator.cpp
    src/logging/logdeduplicator.h
    src/metadata/metadatasynchronizer.cpp
    src/metadata/metadatasynchronizer.h
    src/roi/roirepository.cpp
    src/roi/roirepository.h
    src/roi/roiservice.cpp
    src/roi/roiservice.h
    src/rpi/rpitcpclient.cpp
    src/rpi/rpitcpclient.h
    src/telegram/telegrambotapi.cpp
    src/telegram/telegrambotapi.h
    # Parking
    src/parking/vehicletracker.cpp
    src/parking/vehicletracker.h
    src/parking/parkingrepository.cpp
    src/parking/parkingrepository.h
    src/parking/parkingservice.cpp
    src/parking/parkingservice.h
    # Database (New Phase 9)
    src/database/databasecontext.cpp
    src/database/databasecontext.h
    src/database/userrepository.cpp
    src/database/userrepository.h
)

# 헤더 파일 경로 추가 (다른 폴더의 헤더를 include 할 수 있도록 설정)
target_include_directories(rtsp_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_definitions(rtsp_test PRIVATE
    PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

# 설정 폴더를 빌드 디렉토리로 복사
# 빌드 후 실행 파일 경로 옆으로 config 폴더를 복사합니다 (멀티 컨피그 대응).
add_custom_command(TARGET rtsp_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/config
            $<TARGET_FILE_DIR:rtsp_test>/config
)

# Qt 라이브러리 링크
target_link_libraries(rtsp_test
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::Network
        Qt::Concurrent
        Qt::Sql
)

# 설치 설정
include(GNUInstallDirs)

install(TARGETS rtsp_test
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/config
    DESTINATION ${CMAKE_INSTALL_BINDIR}/config
)

# 배포 스크립트 생성
qt_generate_deploy_app_script(
    TARGET rtsp_test
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})

# ============================================================
# 의존성 설정 (vcpkg 툴체인 또는 환경 변수 사용)
# ============================================================
# 빌드 시 다음 명령어로 vcpkg 툴체인을 지정하세요:
#   cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=[vcpkg-root]/scripts/buildsystems/vcpkg.cmake
#
# 또는 환경 변수로 개별 경로 지정 가능:
#   - OPENCV_DIR: OpenCV 설치 경로
#   - TESSERACT_DIR: Tesseract 설치 경로
# ============================================================

# OpenCV 패키지 찾기 (vcpkg 또는 환경 변수에서 자동 검사)
if(DEFINED ENV{OPENCV_DIR})
    set(OpenCV_DIR $ENV{OPENCV_DIR})
endif()

find_package(OpenCV REQUIRED)
target_include_directories(rtsp_test PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${OpenCV_LIBS})

# Tesseract & Leptonica 찾기 (vcpkg에서 자동 검사)
find_package(Tesseract REQUIRED)
find_package(Leptonica REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE Tesseract::libtesseract leptonica)
