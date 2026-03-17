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
    src/ui/windows/mainwindowuirefs.h
    src/ui/windows/loginpage.cpp
    src/ui/windows/loginpage.h
    src/ui/windows/camerachannelruntime.cpp
    src/ui/windows/camerachannelruntime.h
    src/ui/windows/mainwindowcontroller.cpp
    src/ui/windows/mainwindowcontroller.h
    src/ui/windows/dbpanelcontroller.cpp
    src/ui/windows/dbpanelcontroller.h
    src/ui/windows/parkinglogpanelcontroller.cpp
    src/ui/windows/parkinglogpanelcontroller.h
    src/ui/windows/userdbpanelcontroller.cpp
    src/ui/windows/userdbpanelcontroller.h
    src/ui/windows/vehicleinfopanelcontroller.cpp
    src/ui/windows/vehicleinfopanelcontroller.h
    src/ui/windows/zonepanelcontroller.cpp
    src/ui/windows/zonepanelcontroller.h
    src/ui/windows/rpipanelcontroller.cpp
    src/ui/windows/rpipanelcontroller.h
    src/ui/windows/recordpanelcontroller.cpp
    src/ui/windows/recordpanelcontroller.h
    src/ui/video/videowidget.cpp
    src/ui/video/videowidget.h
    src/ui/roi/roiinteractionstate.cpp
    src/ui/roi/roiinteractionstate.h
    src/ui/video/videoframerenderer.cpp
    src/ui/video/videoframerenderer.h
    # Video
    src/video/videothread.cpp
    src/video/videothread.h
    src/video/videobuffermanager.cpp
    src/video/videobuffermanager.h
    src/video/mediarecorderworker.cpp
    src/video/mediarecorderworker.h
    # Metadata
    src/metadata/metadatathread.cpp
    src/metadata/metadatathread.h
    # OCR
    src/ocr/ocrmanager.cpp
    src/ocr/ocrmanager.h
    src/ocr/ocrtypes.h
    src/ocr/plateocrcoordinator.cpp
    src/ocr/plateocrcoordinator.h
    src/ocr/preprocess/platepreprocessor.cpp
    src/ocr/preprocess/platepreprocessor.h
    src/ocr/recognition/paddleocrunner.cpp
    src/ocr/recognition/paddleocrunner.h
    src/ocr/recognition/llmocrrunner.cpp
    src/ocr/recognition/llmocrrunner.h
    src/ocr/postprocess/platepostprocessor.cpp
    src/ocr/postprocess/platepostprocessor.h
    src/ocr/debug/ocrdebugdumper.cpp
    src/ocr/debug/ocrdebugdumper.h
    # Tracking (ReID)
    src/tracking/reidextractor.cpp
    src/tracking/reidextractor.h
    # Core
    src/config/config.cpp
    src/config/config.h
    src/config/logfilterconfig.cpp
    src/config/logfilterconfig.h
    src/camera/cameramanager.cpp
    src/camera/cameramanager.h
    src/camera/camerasource.cpp
    src/camera/camerasource.h
    src/camera/camerasessionservice.cpp
    src/camera/camerasessionservice.h
    src/logging/logdeduplicator.cpp
    src/logging/logdeduplicator.h
    src/metadata/metadatasynchronizer.cpp
    src/metadata/metadatasynchronizer.h
    src/util/rtspurl.cpp
    src/util/rtspurl.h
    src/roi/roirepository.cpp
    src/roi/roirepository.h
    src/roi/roiservice.cpp
    src/roi/roiservice.h
    src/rpi/rpiauthclient.cpp
    src/rpi/rpiauthclient.h
    src/rpi/rpitcpclient.cpp
    src/rpi/rpitcpclient.h
    src/telegram/telegrambotapi.cpp
    src/telegram/telegrambotapi.h
    # Parking
    src/parking/vehicletracker.cpp
    src/parking/vehicletracker.h
    src/parking/parkingservice.cpp
    src/parking/parkingservice.h
    src/parking/parkingrepository.cpp
    src/parking/parkingrepository.h
    # Database (New Phase 9)
    src/database/databasecontext.cpp
    src/database/databasecontext.h
    src/database/userrepository.cpp
    src/database/userrepository.h
    src/database/hardwarelogrepository.cpp
    src/database/hardwarelogrepository.h
    src/database/vehiclerepository.cpp
    src/database/mediarepository.cpp
    src/database/mediarepository.h
    resources.qrc
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
# ============================================================

# OpenCV 패키지 찾기 (vcpkg 또는 환경 변수에서 자동 검사)
if(DEFINED ENV{OPENCV_DIR})
    set(OpenCV_DIR $ENV{OPENCV_DIR})
endif()

find_package(OpenCV REQUIRED)
target_include_directories(rtsp_test PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${OpenCV_LIBS})

# ONNX Runtime 패키지 찾기 (vcpkg 또는 ONNXRUNTIME_ROOT 환경 변수)
find_package(ONNXRUNTIME CONFIG QUIET)

if(TARGET onnxruntime::onnxruntime)
    target_link_libraries(${PROJECT_NAME} PRIVATE onnxruntime::onnxruntime)
elseif(TARGET onnxruntime)
    target_link_libraries(${PROJECT_NAME} PRIVATE onnxruntime)
else()
    set(_onnxruntime_hints)

    if(DEFINED ONNXRUNTIME_ROOT AND ONNXRUNTIME_ROOT)
        list(APPEND _onnxruntime_hints "${ONNXRUNTIME_ROOT}")
    endif()
    if(DEFINED ENV{ONNXRUNTIME_ROOT} AND NOT "$ENV{ONNXRUNTIME_ROOT}" STREQUAL "")
        list(APPEND _onnxruntime_hints "$ENV{ONNXRUNTIME_ROOT}")
    endif()
    if(DEFINED ENV{ONNXRUNTIME_DIR} AND NOT "$ENV{ONNXRUNTIME_DIR}" STREQUAL "")
        list(APPEND _onnxruntime_hints "$ENV{ONNXRUNTIME_DIR}")
    endif()
    if(WIN32)
        list(APPEND _onnxruntime_hints
            "$ENV{USERPROFILE}/Downloads/onnxruntime-win-x64-1.24.2/onnxruntime-win-x64-1.24.2"
            "$ENV{USERPROFILE}/Downloads/onnxruntime-win-x64-1.23.2/onnxruntime-win-x64-1.23.2"
        )
    endif()

    list(REMOVE_DUPLICATES _onnxruntime_hints)

    find_path(ONNXRUNTIME_INCLUDE_DIR
        NAMES onnxruntime_cxx_api.h
        HINTS ${_onnxruntime_hints}
        PATH_SUFFIXES include
    )

    find_library(ONNXRUNTIME_LIBRARY
        NAMES onnxruntime
        HINTS ${_onnxruntime_hints}
        PATH_SUFFIXES lib lib64
    )

    find_file(ONNXRUNTIME_DLL
        NAMES onnxruntime.dll
        HINTS ${_onnxruntime_hints}
        PATH_SUFFIXES bin lib lib64
    )

    if(ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIBRARY)
        target_include_directories(rtsp_test PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
        target_link_libraries(${PROJECT_NAME} PRIVATE ${ONNXRUNTIME_LIBRARY})

        if(ONNXRUNTIME_DLL)
            add_custom_command(TARGET rtsp_test POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        ${ONNXRUNTIME_DLL}
                        $<TARGET_FILE_DIR:rtsp_test>
            )
        endif()
    else()
        message(FATAL_ERROR
            "ONNX Runtime package not found. Set ONNXRUNTIME_ROOT to the extracted folder "
            "(it must contain include/onnxruntime_cxx_api.h and lib/onnxruntime.lib).")
    endif()
endif()
