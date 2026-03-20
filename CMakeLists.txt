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
    src/presentation/shell/mainwindow.cpp
    src/presentation/shell/mainwindow.h
    src/presentation/shell/mainwindowuirefs.h
    src/presentation/pages/loginpage.cpp
    src/presentation/pages/loginpage.h
    src/ui/windows/camerachannelruntime.cpp
    src/ui/windows/camerachannelruntime.h
    src/presentation/widgets/controllerdialog.cpp
    src/presentation/widgets/controllerdialog.h
    src/ui/windows/mainwindowcontroller.cpp
    src/ui/windows/mainwindowcontroller.h
    src/ui/windows/dbpanelcontroller.cpp
    src/ui/windows/dbpanelcontroller.h
    src/ui/windows/parkinglogpanelcontroller.cpp
    src/ui/windows/parkinglogpanelcontroller.h
    src/ui/windows/userdbpanelcontroller.cpp
    src/ui/windows/userdbpanelcontroller.h
    src/ui/windows/zonepanelcontroller.cpp
    src/ui/windows/zonepanelcontroller.h
    src/ui/windows/rpipanelcontroller.cpp
    src/ui/windows/rpipanelcontroller.h
    src/ui/windows/recordpanelcontroller.cpp
    src/ui/windows/recordpanelcontroller.h
    src/presentation/widgets/videowidget.cpp
    src/presentation/widgets/videowidget.h
    src/presentation/widgets/roiinteractionstate.cpp
    src/presentation/widgets/roiinteractionstate.h
    src/presentation/widgets/videoframerenderer.cpp
    src/presentation/widgets/videoframerenderer.h
    # Video
    src/video/videothread.cpp
    src/video/videothread.h
    src/video/sharedvideoframe.h
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
    src/ocr/recognition/llmocrrunner.cpp
    src/ocr/recognition/llmocrrunner.h
    src/ocr/recognition/telegramllmrunner.cpp
    src/ocr/recognition/telegramllmrunner.h
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
    src/rpi/rpicontrolclient.cpp
    src/rpi/rpicontrolclient.h
    src/rpi/rpitcpclient.cpp
    src/rpi/rpitcpclient.h
    src/telegram/telegrambotapi.cpp
    src/telegram/telegrambotapi.h
    # Parking
    src/parking/vehicletracker.cpp
    src/parking/vehicletracker.h
    src/parking/parkingfeepolicy.cpp
    src/parking/parkingfeepolicy.h
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

# OpenCV 및 의존성(Protobuf 등)을 vcpkg에서 찾도록 강력한 힌트 추가
list(APPEND CMAKE_PREFIX_PATH 
    "C:/vcpkg/installed/x64-windows"
    "C:/vcpkg/installed/x64-windows/share"
    "C:/vcpkg/installed/x64-windows/share/protobuf"
)

# OpenCV가 내부적으로 Protobuf를 찾을 때 에러가 나지 않도록 수동 힌트 설정
set(Protobuf_DIR "C:/vcpkg/installed/x64-windows/share/protobuf" CACHE PATH "Force vcpkg Protobuf")

find_package(Protobuf REQUIRED CONFIG)
find_package(OpenCV REQUIRED CONFIG)

target_include_directories(rtsp_test PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${OpenCV_LIBS})



# ============================================================
# OpenVINO (Intel GPU / CPU 가속)
# ============================================================
# 1. 먼저 vcpkg 또는 시스템 경로에서 find_package 시도
find_package(OpenVINO CONFIG QUIET)

# 2. 찾지 못했을 경우 수동 지정 경로 시도
if(NOT OpenVINO_FOUND)
    # [수정] 사용자의 실제 설치 경로에 맞게 이 부분을 수정하세요.
    set(OPENVINO_ROOT "C:/intel/openvino_2025.4.1" CACHE PATH "Path to OpenVINO installation")
    
    set(OPENVINO_INCLUDE_DIR "${OPENVINO_ROOT}/runtime/include")

    if(CMAKE_BUILD_TYPE MATCHES "Debug")
        set(OV_SUBDIR "Debug")
        set(OV_SUFFIX "d")
    else()
        set(OV_SUBDIR "Release")
        set(OV_SUFFIX "")
    endif()

    set(OPENVINO_LIB_DIR     "${OPENVINO_ROOT}/runtime/lib/intel64/${OV_SUBDIR}")
    set(OPENVINO_BIN_DIR     "${OPENVINO_ROOT}/runtime/bin/intel64/${OV_SUBDIR}")
    set(OPENVINO_TBB_BIN_DIR "${OPENVINO_ROOT}/runtime/3rdparty/tbb/bin")

    if(EXISTS "${OPENVINO_INCLUDE_DIR}/openvino/openvino.hpp")
        message(STATUS "[OpenVINO] Found manually at ${OPENVINO_ROOT}")
        target_include_directories(${PROJECT_NAME} PRIVATE ${OPENVINO_INCLUDE_DIR})
        target_link_directories(${PROJECT_NAME}    PRIVATE ${OPENVINO_LIB_DIR})
        target_link_libraries(${PROJECT_NAME}      PRIVATE "openvino${OV_SUFFIX}")
        set(HAVE_OPENVINO ON)
    endif()
else()
    message(STATUS "[OpenVINO] Found via find_package: ${OpenVINO_VERSION}")
    target_link_libraries(${PROJECT_NAME} PRIVATE openvino::runtime)
    set(HAVE_OPENVINO ON)
endif()

if(HAVE_OPENVINO)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_OPENVINO)

    # DLL 복사 로직 (find_package로 찾은 경우 vcpkg가 처리해주지만, 수동 설치 시 필요)
    if(NOT OpenVINO_FOUND)
        set(OPENVINO_DLL_NAMES 
            "openvino" "openvino_c" "openvino_intel_cpu_plugin" "openvino_intel_gpu_plugin"
            "openvino_auto_plugin" "openvino_auto_batch_plugin" "openvino_hetero_plugin"
            "openvino_ir_frontend" "openvino_onnx_frontend"
        )
        foreach(name ${OPENVINO_DLL_NAMES})
            set(dll_path "${OPENVINO_BIN_DIR}/${name}${OV_SUFFIX}.dll")
            if(EXISTS "${dll_path}")
                add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${dll_path}" $<TARGET_FILE_DIR:${PROJECT_NAME}>
                )
            endif()
        endforeach()

        set(TBB_DLL_NAMES "tbb12" "tbbbind_2_5" "tbbmalloc" "tbbmalloc_proxy")
        foreach(name ${TBB_DLL_NAMES})
            set(dll_path "${OPENVINO_TBB_BIN_DIR}/${name}${OV_SUFFIX}.dll")
            if(EXISTS "${dll_path}")
                add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${dll_path}" $<TARGET_FILE_DIR:${PROJECT_NAME}>
                )
            endif()
        endforeach()
    endif()
else()
    message(WARNING "[OpenVINO] Not found. ReID will fall back to CPU/DirectML (if available).")
endif()


