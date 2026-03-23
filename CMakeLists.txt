cmake_minimum_required(VERSION 3.19)
project(rtsp_test LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Policies and toolchain fallbacks
# ---------------------------------------------------------------------------

if(POLICY CMP0144)
    cmake_policy(SET CMP0144 NEW)
endif()
set(CMAKE_POLICY_DEFAULT_CMP0144 NEW)

if(MSVC AND CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES STREQUAL "")
    set(_msvc_fallback_include_dirs "")
    set(_msvc_fallback_link_dirs "")

    get_filename_component(_msvc_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(_msvc_toolchain_dir "${_msvc_bin_dir}/../../.." ABSOLUTE)
    set(_msvc_include_dir "${_msvc_toolchain_dir}/include")
    set(_msvc_lib_dir "${_msvc_toolchain_dir}/lib/x64")

    if(EXISTS "${_msvc_include_dir}")
        list(APPEND _msvc_fallback_include_dirs "${_msvc_include_dir}")
    endif()
    if(EXISTS "${_msvc_lib_dir}")
        list(APPEND _msvc_fallback_link_dirs "${_msvc_lib_dir}")
    endif()

    if(DEFINED CMAKE_MT AND CMAKE_MT)
        get_filename_component(_winsdk_bin_arch_dir "${CMAKE_MT}" DIRECTORY)
        get_filename_component(_winsdk_ver_dir "${_winsdk_bin_arch_dir}" DIRECTORY)
        get_filename_component(_winsdk_ver "${_winsdk_ver_dir}" NAME)
        get_filename_component(_winsdk_bin_dir "${_winsdk_ver_dir}" DIRECTORY)
        get_filename_component(_winsdk_root "${_winsdk_bin_dir}" DIRECTORY)
        set(_winsdk_include_root "${_winsdk_root}/Include/${_winsdk_ver}")
        set(_winsdk_lib_root "${_winsdk_root}/Lib/${_winsdk_ver}")

        foreach(_subdir ucrt shared um winrt cppwinrt)
            if(EXISTS "${_winsdk_include_root}/${_subdir}")
                list(APPEND _msvc_fallback_include_dirs "${_winsdk_include_root}/${_subdir}")
            endif()
        endforeach()

        foreach(_subdir ucrt um)
            if(EXISTS "${_winsdk_lib_root}/${_subdir}/x64")
                list(APPEND _msvc_fallback_link_dirs "${_winsdk_lib_root}/${_subdir}/x64")
            endif()
        endforeach()
    endif()

    if(_msvc_fallback_include_dirs)
        list(REMOVE_DUPLICATES _msvc_fallback_include_dirs)
        message(WARNING
            "MSVC implicit include directories were not detected. "
            "Injecting fallback standard include paths for this configure.")
        include_directories(SYSTEM ${_msvc_fallback_include_dirs})
    endif()

    if(_msvc_fallback_link_dirs)
        list(REMOVE_DUPLICATES _msvc_fallback_link_dirs)
        message(WARNING
            "MSVC implicit link directories were not detected. "
            "Injecting fallback standard library paths for this configure.")
        link_directories(${_msvc_fallback_link_dirs})
    endif()
endif()

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------

list(APPEND CMAKE_PREFIX_PATH
    "C:/vcpkg/installed/x64-windows"
    "C:/vcpkg/installed/x64-windows/share"
    "C:/vcpkg/installed/x64-windows/share/protobuf"
)

set(Protobuf_DIR "C:/vcpkg/installed/x64-windows/share/protobuf" CACHE PATH
    "Force vcpkg Protobuf")

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Network Concurrent Sql)
find_package(Protobuf REQUIRED CONFIG)
find_package(OpenCV REQUIRED CONFIG)

if(DEFINED OPENVINO_ROOT)
    set(VEDA_OPENVINO_ROOT "${OPENVINO_ROOT}" CACHE PATH
        "Path to OpenVINO installation" FORCE)
    unset(OPENVINO_ROOT CACHE)
elseif(DEFINED ENV{OPENVINO_ROOT} AND NOT DEFINED VEDA_OPENVINO_ROOT)
    set(VEDA_OPENVINO_ROOT "$ENV{OPENVINO_ROOT}" CACHE PATH
        "Path to OpenVINO installation")
endif()

find_package(OpenVINO CONFIG QUIET)

qt_standard_project_setup()

# ---------------------------------------------------------------------------
# Source layout
# ---------------------------------------------------------------------------

set(PRESENTATION_SOURCES
    src/presentation/shell/mainwindow.cpp
    src/presentation/shell/mainwindow.h
    src/presentation/shell/mainwindowuirefs.h
    src/presentation/shell/headerbarview.cpp
    src/presentation/shell/headerbarview.h
    src/presentation/pages/loginpage.cpp
    src/presentation/pages/loginpage.h
    src/presentation/pages/cctvdashboardview.cpp
    src/presentation/pages/cctvdashboardview.h
    src/presentation/pages/cctvsplashpageview.cpp
    src/presentation/pages/cctvsplashpageview.h
    src/presentation/pages/dbpageview.cpp
    src/presentation/pages/dbpageview.h
    src/presentation/pages/recordpageview.cpp
    src/presentation/pages/recordpageview.h
    src/presentation/pages/telegrampageview.cpp
    src/presentation/pages/telegrampageview.h
    src/presentation/widgets/controllerdialog.cpp
    src/presentation/widgets/controllerdialog.h
    src/presentation/widgets/camerachannelruntime.cpp
    src/presentation/widgets/camerachannelruntime.h
    src/presentation/widgets/videowidget.cpp
    src/presentation/widgets/videowidget.h
    src/presentation/widgets/roiinteractionstate.cpp
    src/presentation/widgets/roiinteractionstate.h
    src/presentation/widgets/videoframerenderer.cpp
    src/presentation/widgets/videoframerenderer.h
    src/presentation/controllers/mainwindowcontroller.cpp
    src/presentation/controllers/mainwindowcontroller.h
    src/presentation/controllers/dbpanelcontroller.cpp
    src/presentation/controllers/dbpanelcontroller.h
    src/presentation/controllers/parkinglogpanelcontroller.cpp
    src/presentation/controllers/parkinglogpanelcontroller.h
    src/presentation/controllers/userdbpanelcontroller.cpp
    src/presentation/controllers/userdbpanelcontroller.h
    src/presentation/controllers/zonepanelcontroller.cpp
    src/presentation/controllers/zonepanelcontroller.h
    src/presentation/controllers/rpipanelcontroller.cpp
    src/presentation/controllers/rpipanelcontroller.h
    src/presentation/controllers/camerasessioncontroller.cpp
    src/presentation/controllers/camerasessioncontroller.h
    src/presentation/controllers/channelruntimecontroller.cpp
    src/presentation/controllers/channelruntimecontroller.h
    src/presentation/controllers/recordpanelcontroller.cpp
    src/presentation/controllers/recordpanelcontroller.h
    src/presentation/controllers/recordingworkflowcontroller.cpp
    src/presentation/controllers/recordingworkflowcontroller.h
    src/presentation/controllers/telegrampanelcontroller.cpp
    src/presentation/controllers/telegrampanelcontroller.h
    src/presentation/controllers/hardwarecontroller.cpp
    src/presentation/controllers/hardwarecontroller.h
    src/presentation/controllers/cctvcontroller.cpp
    src/presentation/controllers/cctvcontroller.h
    src/presentation/controllers/reidcontroller.cpp
    src/presentation/controllers/reidcontroller.h
)

set(APPLICATION_SOURCES
    src/application/db/common/operationresult.h
    src/application/db/parking/parkinglogapplicationservice.cpp
    src/application/db/parking/parkinglogapplicationservice.h
    src/application/db/user/useradminapplicationservice.cpp
    src/application/db/user/useradminapplicationservice.h
    src/application/db/zone/zonequeryapplicationservice.cpp
    src/application/db/zone/zonequeryapplicationservice.h
    src/application/parking/parkingservice.cpp
    src/application/parking/parkingservice.h
    src/application/roi/roiservice.cpp
    src/application/roi/roiservice.h
)

set(DOMAIN_SOURCES
    src/domain/parking/vehicletracker.cpp
    src/domain/parking/vehicletracker.h
    src/domain/parking/parkingfeepolicy.cpp
    src/domain/parking/parkingfeepolicy.h
)

set(INFRASTRUCTURE_SOURCES
    src/infrastructure/camera/cameramanager.cpp
    src/infrastructure/camera/cameramanager.h
    src/infrastructure/camera/camerasource.cpp
    src/infrastructure/camera/camerasource.h
    src/infrastructure/camera/camerasessionservice.cpp
    src/infrastructure/camera/camerasessionservice.h
    src/infrastructure/camera/rtspurl.cpp
    src/infrastructure/camera/rtspurl.h
    src/infrastructure/metadata/metadatathread.cpp
    src/infrastructure/metadata/metadatathread.h
    src/infrastructure/metadata/metadatasynchronizer.cpp
    src/infrastructure/metadata/metadatasynchronizer.h
    src/infrastructure/ocr/ocrmanager.cpp
    src/infrastructure/ocr/ocrmanager.h
    src/infrastructure/ocr/ocrtypes.h
    src/infrastructure/ocr/plateocrcoordinator.cpp
    src/infrastructure/ocr/plateocrcoordinator.h
    src/infrastructure/ocr/recognition/llmocrrunner.cpp
    src/infrastructure/ocr/recognition/llmocrrunner.h
    src/infrastructure/ocr/recognition/telegramllmrunner.cpp
    src/infrastructure/ocr/recognition/telegramllmrunner.h
    src/infrastructure/persistence/databasecontext.cpp
    src/infrastructure/persistence/databasecontext.h
    src/infrastructure/persistence/hardwarelogrepository.cpp
    src/infrastructure/persistence/hardwarelogrepository.h
    src/infrastructure/persistence/mediarepository.cpp
    src/infrastructure/persistence/mediarepository.h
    src/infrastructure/persistence/parkingrepository.cpp
    src/infrastructure/persistence/parkingrepository.h
    src/infrastructure/persistence/roirepository.cpp
    src/infrastructure/persistence/roirepository.h
    src/infrastructure/persistence/userrepository.cpp
    src/infrastructure/persistence/userrepository.h
    src/infrastructure/rpi/rpiauthclient.cpp
    src/infrastructure/rpi/rpiauthclient.h
    src/infrastructure/rpi/rpicontrolclient.cpp
    src/infrastructure/rpi/rpicontrolclient.h
    src/infrastructure/rpi/rpitcpclient.cpp
    src/infrastructure/rpi/rpitcpclient.h
    src/infrastructure/telegram/telegrambotapi.cpp
    src/infrastructure/telegram/telegrambotapi.h
    src/infrastructure/video/videothread.cpp
    src/infrastructure/video/videothread.h
    src/infrastructure/video/sharedvideoframe.h
    src/infrastructure/video/videobuffermanager.cpp
    src/infrastructure/video/videobuffermanager.h
    src/infrastructure/video/mediarecorderworker.cpp
    src/infrastructure/video/mediarecorderworker.h
    src/infrastructure/vision/reidextractor.cpp
    src/infrastructure/vision/reidextractor.h
)

set(SHARED_SOURCES
    src/config/config.cpp
    src/config/config.h
    src/config/logfilterconfig.cpp
    src/config/logfilterconfig.h
    src/logging/logdeduplicator.cpp
    src/logging/logdeduplicator.h
    src/shared/result.h
)

set(APP_SOURCES
    src/main.cpp
    resources.qrc
)

# ---------------------------------------------------------------------------
# Target
# ---------------------------------------------------------------------------

qt_add_executable(rtsp_test WIN32 MACOSX_BUNDLE)

target_sources(rtsp_test PRIVATE
    ${APP_SOURCES}
    ${PRESENTATION_SOURCES}
    ${APPLICATION_SOURCES}
    ${DOMAIN_SOURCES}
    ${INFRASTRUCTURE_SOURCES}
    ${SHARED_SOURCES}
)

target_include_directories(rtsp_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${OpenCV_INCLUDE_DIRS}
)

target_compile_definitions(rtsp_test PRIVATE
    PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(rtsp_test PRIVATE
    Qt::Core
    Qt::Widgets
    Qt::Network
    Qt::Concurrent
    Qt::Sql
    ${OpenCV_LIBS}
)

# ---------------------------------------------------------------------------
# Runtime assets and install
# ---------------------------------------------------------------------------

add_custom_command(TARGET rtsp_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/config
            $<TARGET_FILE_DIR:rtsp_test>/config
)

include(GNUInstallDirs)

install(TARGETS rtsp_test
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/config
    DESTINATION ${CMAKE_INSTALL_BINDIR}/config
)

qt_generate_deploy_app_script(
    TARGET rtsp_test
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})

# ---------------------------------------------------------------------------
# OpenVINO
# ---------------------------------------------------------------------------

if(NOT OpenVINO_FOUND)
    set(VEDA_OPENVINO_ROOT "C:/intel/openvino_2025.4.1" CACHE PATH
        "Path to OpenVINO installation")
    set(OPENVINO_INCLUDE_DIR "${VEDA_OPENVINO_ROOT}/runtime/include")

    if(CMAKE_BUILD_TYPE MATCHES "Debug")
        set(OV_SUBDIR "Debug")
        set(OV_SUFFIX "d")
    else()
        set(OV_SUBDIR "Release")
        set(OV_SUFFIX "")
    endif()

    set(OPENVINO_LIB_DIR "${VEDA_OPENVINO_ROOT}/runtime/lib/intel64/${OV_SUBDIR}")
    set(OPENVINO_BIN_DIR "${VEDA_OPENVINO_ROOT}/runtime/bin/intel64/${OV_SUBDIR}")
    set(OPENVINO_TBB_BIN_DIR "${VEDA_OPENVINO_ROOT}/runtime/3rdparty/tbb/bin")

    if(EXISTS "${OPENVINO_INCLUDE_DIR}/openvino/openvino.hpp")
        message(STATUS "[OpenVINO] Found manually at ${VEDA_OPENVINO_ROOT}")
        target_include_directories(rtsp_test PRIVATE ${OPENVINO_INCLUDE_DIR})
        target_link_directories(rtsp_test PRIVATE ${OPENVINO_LIB_DIR})
        target_link_libraries(rtsp_test PRIVATE "openvino${OV_SUFFIX}")
        set(HAVE_OPENVINO ON)
    endif()
else()
    message(STATUS "[OpenVINO] Found via find_package: ${OpenVINO_VERSION}")
    target_link_libraries(rtsp_test PRIVATE openvino::runtime)
    set(HAVE_OPENVINO ON)
endif()

if(HAVE_OPENVINO)
    target_compile_definitions(rtsp_test PRIVATE HAVE_OPENVINO)

    if(NOT OpenVINO_FOUND)
        set(OPENVINO_DLL_NAMES
            openvino
            openvino_c
            openvino_intel_cpu_plugin
            openvino_intel_gpu_plugin
            openvino_auto_plugin
            openvino_auto_batch_plugin
            openvino_hetero_plugin
            openvino_ir_frontend
            openvino_onnx_frontend
        )

        foreach(name ${OPENVINO_DLL_NAMES})
            set(dll_path "${OPENVINO_BIN_DIR}/${name}${OV_SUFFIX}.dll")
            if(EXISTS "${dll_path}")
                add_custom_command(TARGET rtsp_test POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${dll_path}" $<TARGET_FILE_DIR:rtsp_test>
                )
            endif()
        endforeach()

        set(TBB_DLL_NAMES tbb12 tbbbind_2_5 tbbmalloc tbbmalloc_proxy)
        foreach(name ${TBB_DLL_NAMES})
            set(dll_path "${OPENVINO_TBB_BIN_DIR}/${name}${OV_SUFFIX}.dll")
            if(EXISTS "${dll_path}")
                add_custom_command(TARGET rtsp_test POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${dll_path}" $<TARGET_FILE_DIR:rtsp_test>
                )
            endif()
        endforeach()
    endif()
else()
    message(WARNING
        "[OpenVINO] Not found. ReID will fall back to CPU/DirectML (if available).")
endif()


