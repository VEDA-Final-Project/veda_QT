cmake_minimum_required(VERSION 3.19)
project(rtsp_test LANGUAGES CXX)

set(APP_TARGET rtsp_test)

# Qt6 필수 구성 요소 찾기
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Network Concurrent Sql Multimedia)

qt_standard_project_setup()

# 실행 파일 생성 및 소스 파일 정의
qt_add_executable(${APP_TARGET}
    WIN32 MACOSX_BUNDLE
    # Main
    src/main.cpp
    # UI
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
    src/presentation/widgets/camerachannelruntime.cpp
    src/presentation/widgets/camerachannelruntime.h
    src/presentation/widgets/controllerdialog.cpp
    src/presentation/widgets/controllerdialog.h
    src/presentation/widgets/toastoverlay.cpp
    src/presentation/widgets/toastoverlay.h
    src/presentation/controllers/mainwindowcontroller.cpp
    src/presentation/controllers/mainwindowcontroller.h
    src/presentation/controllers/camerasessioncontroller.cpp
    src/presentation/controllers/camerasessioncontroller.h
    src/presentation/controllers/cctvcontroller.cpp
    src/presentation/controllers/cctvcontroller.h
    src/presentation/controllers/channelruntimecontroller.cpp
    src/presentation/controllers/channelruntimecontroller.h
    src/presentation/controllers/dbpanelcontroller.cpp
    src/presentation/controllers/dbpanelcontroller.h
    src/presentation/controllers/hardwarecontroller.cpp
    src/presentation/controllers/hardwarecontroller.h
    src/presentation/controllers/notificationcontroller.cpp
    src/presentation/controllers/notificationcontroller.h
    src/presentation/controllers/parkinglogpanelcontroller.cpp
    src/presentation/controllers/parkinglogpanelcontroller.h
    src/presentation/controllers/recordingworkflowcontroller.cpp
    src/presentation/controllers/recordingworkflowcontroller.h
    src/presentation/controllers/userdbpanelcontroller.cpp
    src/presentation/controllers/userdbpanelcontroller.h
    src/presentation/controllers/vehicleinfopanelcontroller.cpp
    src/presentation/controllers/vehicleinfopanelcontroller.h
    src/presentation/controllers/zonepanelcontroller.cpp
    src/presentation/controllers/zonepanelcontroller.h
    src/presentation/controllers/reidcontroller.cpp
    src/presentation/controllers/reidcontroller.h
    src/presentation/controllers/rpipanelcontroller.cpp
    src/presentation/controllers/rpipanelcontroller.h
    src/presentation/controllers/recordpanelcontroller.cpp
    src/presentation/controllers/recordpanelcontroller.h
    src/presentation/controllers/telegrampanelcontroller.cpp
    src/presentation/controllers/telegrampanelcontroller.h
    src/presentation/widgets/videowidget.cpp
    src/presentation/widgets/videowidget.h
    src/presentation/widgets/roiinteractionstate.cpp
    src/presentation/widgets/roiinteractionstate.h
    src/presentation/widgets/videoframerenderer.cpp
    src/presentation/widgets/videoframerenderer.h
    # Video
    src/infrastructure/video/videothread.cpp
    src/infrastructure/video/videothread.h
    src/infrastructure/video/videobuffermanager.cpp
    src/infrastructure/video/videobuffermanager.h
    src/infrastructure/video/mediarecorderworker.cpp
    src/infrastructure/video/mediarecorderworker.h
    # Metadata
    src/infrastructure/metadata/ffmpegmetadatastreamreader.cpp
    src/infrastructure/metadata/ffmpegmetadatastreamreader.h
    src/infrastructure/metadata/metadatathread.cpp
    src/infrastructure/metadata/metadatathread.h
    src/infrastructure/metadata/objectinfo.h
    src/infrastructure/metadata/onvifmetadataparser.cpp
    src/infrastructure/metadata/onvifmetadataparser.h
    # OCR
    src/infrastructure/ocr/ocrmanager.cpp
    src/infrastructure/ocr/ocrmanager.h
    src/infrastructure/ocr/ocrtypes.h
    src/infrastructure/ocr/plateocrcoordinator.cpp
    src/infrastructure/ocr/plateocrcoordinator.h
    src/infrastructure/ocr/recognition/llmocrrunner.cpp
    src/infrastructure/ocr/recognition/llmocrrunner.h
    src/infrastructure/ocr/recognition/telegramllmrunner.cpp
    src/infrastructure/ocr/recognition/telegramllmrunner.h
    # Tracking (ReID)
    src/infrastructure/vision/reidextractor.cpp
    src/infrastructure/vision/reidextractor.h
    # Core
    src/config/config.cpp
    src/config/config.h
    src/config/logfilterconfig.cpp
    src/config/logfilterconfig.h
    src/infrastructure/camera/cameramanager.cpp
    src/infrastructure/camera/cameramanager.h
    src/infrastructure/camera/camerasource.cpp
    src/infrastructure/camera/camerasource.h
    src/infrastructure/camera/camerasessionservice.cpp
    src/infrastructure/camera/camerasessionservice.h
    src/logging/logdeduplicator.cpp
    src/logging/logdeduplicator.h
    src/infrastructure/metadata/metadatasynchronizer.cpp
    src/infrastructure/metadata/metadatasynchronizer.h
    src/infrastructure/camera/rtspurl.cpp
    src/infrastructure/camera/rtspurl.h
    src/infrastructure/persistence/roirepository.cpp
    src/infrastructure/persistence/roirepository.h
    src/application/roi/roiservice.cpp
    src/application/roi/roiservice.h
    src/infrastructure/rpi/rpiauthclient.cpp
    src/infrastructure/rpi/rpiauthclient.h
    src/infrastructure/rpi/rpicontrolclient.cpp
    src/infrastructure/rpi/rpicontrolclient.h
    src/infrastructure/rpi/rpitcpclient.cpp
    src/infrastructure/rpi/rpitcpclient.h
    src/infrastructure/telegram/telegrambotapi.cpp
    src/infrastructure/telegram/telegrambotapi.h
    # Parking
    src/domain/parking/vehicletracker.cpp
    src/domain/parking/vehicletracker.h
    src/domain/parking/parkingfeepolicy.cpp
    src/domain/parking/parkingfeepolicy.h
    src/application/parking/parkingservice.cpp
    src/application/parking/parkingservice.h
    src/infrastructure/persistence/parkingrepository.cpp
    src/infrastructure/persistence/parkingrepository.h
    # SRTP
    src/srtp/srtpsession.cpp
    src/srtp/srtpsession.h
    src/srtp/srtprtspclient.cpp
    src/srtp/srtprtspclient.h
    src/srtp/mikeybuilder.cpp
    src/srtp/mikeybuilder.h
    src/srtp/srtpdecryptor.cpp
    src/srtp/srtpdecryptor.h
    src/srtp/rtpdepacketizer.cpp
    src/srtp/rtpdepacketizer.h
    src/srtp/srtpvideothread.cpp
    src/srtp/srtpvideothread.h
    src/srtp/srtpmetadataparser.cpp
    src/srtp/srtpmetadataparser.h
    src/srtp/srtporchestrator.cpp
    src/srtp/srtporchestrator.h
    # Database (New Phase 9)
    src/infrastructure/persistence/databasecontext.cpp
    src/infrastructure/persistence/databasecontext.h
    src/infrastructure/persistence/userrepository.cpp
    src/infrastructure/persistence/userrepository.h
    src/infrastructure/persistence/hardwarelogrepository.cpp
    src/infrastructure/persistence/hardwarelogrepository.h
    src/infrastructure/persistence/vehiclerepository.cpp
    src/application/db/common/operationresult.h
    src/application/db/parking/parkinglogapplicationservice.cpp
    src/application/db/parking/parkinglogapplicationservice.h
    src/application/db/user/useradminapplicationservice.cpp
    src/application/db/user/useradminapplicationservice.h
    src/application/db/zone/zonequeryapplicationservice.cpp
    src/application/db/zone/zonequeryapplicationservice.h
    src/infrastructure/persistence/mediarepository.cpp
    src/infrastructure/persistence/mediarepository.h
    resources.qrc
)

# 헤더 파일 경로 추가 (다른 폴더의 헤더를 include 할 수 있도록 설정)
target_include_directories(${APP_TARGET} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_definitions(${APP_TARGET} PRIVATE
    PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

# 설정 폴더를 빌드 디렉토리로 복사
# 빌드 후 실행 파일 경로 옆으로 config 폴더를 복사합니다 (멀티 컨피그 대응).
add_custom_command(TARGET ${APP_TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/config
            $<TARGET_FILE_DIR:${APP_TARGET}>/config
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/model
            $<TARGET_FILE_DIR:${APP_TARGET}>/model
)

# Qt 라이브러리 링크
target_link_libraries(${APP_TARGET}
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::Network
        Qt::Concurrent
        Qt::Sql
        Qt::Multimedia
)

# 설치 설정
include(GNUInstallDirs)

install(TARGETS ${APP_TARGET}
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/config
    DESTINATION ${CMAKE_INSTALL_BINDIR}/config
)

# 배포 스크립트 생성
qt_generate_deploy_app_script(
    TARGET ${APP_TARGET}
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

set(VCPKG_INCLUDE_PATHS
    "C:/vcpkg/installed/x64-windows/include"
    "D:/vcpkg/installed/x64-windows/include"
)

set(VCPKG_LIB_PATHS
    "C:/vcpkg/installed/x64-windows/lib"
    "D:/vcpkg/installed/x64-windows/lib"
)

# OpenCV 패키지 찾기 (vcpkg 또는 환경 변수에서 자동 검사)
if(DEFINED ENV{OPENCV_DIR})
    set(OpenCV_DIR $ENV{OPENCV_DIR})
endif()

find_package(OpenCV REQUIRED)
target_include_directories(${APP_TARGET} PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(${APP_TARGET} PRIVATE ${OpenCV_LIBS})

# ONNX Runtime은 더 이상 사용하지 않으며 OpenVINO를 주력으로 사용합니다.

if(EXISTS "${CMAKE_SOURCE_DIR}/CMakeLists_OpenVINO_Fragment.txt")
    include("${CMAKE_SOURCE_DIR}/CMakeLists_OpenVINO_Fragment.txt")
endif()

# FFmpeg 라이브러리 연동
unset(FFMPEG_AVCODEC CACHE)
unset(FFMPEG_AVFORMAT CACHE)
unset(FFMPEG_AVUTIL CACHE)
unset(FFMPEG_SWSCALE CACHE)
unset(FFMPEG_INCLUDE_DIR CACHE)
find_path(FFMPEG_INCLUDE_DIR libavformat/avformat.h
    PATHS ${VCPKG_INCLUDE_PATHS}
)
find_library(FFMPEG_AVCODEC NAMES avcodec PATHS ${VCPKG_LIB_PATHS})
find_library(FFMPEG_AVFORMAT NAMES avformat PATHS ${VCPKG_LIB_PATHS})
find_library(FFMPEG_AVUTIL NAMES avutil PATHS ${VCPKG_LIB_PATHS})
find_library(FFMPEG_SWSCALE NAMES swscale PATHS ${VCPKG_LIB_PATHS})

if(FFMPEG_INCLUDE_DIR AND FFMPEG_AVCODEC AND FFMPEG_AVFORMAT AND FFMPEG_AVUTIL AND FFMPEG_SWSCALE)
    target_include_directories(${APP_TARGET} PRIVATE ${FFMPEG_INCLUDE_DIR})
    target_link_libraries(${APP_TARGET} PRIVATE
        ${FFMPEG_AVCODEC} ${FFMPEG_AVFORMAT} ${FFMPEG_AVUTIL} ${FFMPEG_SWSCALE}
    )
else()
    message(FATAL_ERROR "FFmpeg headers/libraries (avcodec, avformat, avutil, swscale) are required. Check your vcpkg installation.")
endif()

# libsrtp2 연동
find_path(SRTP_INCLUDE_DIR srtp2/srtp.h
    PATHS ${VCPKG_INCLUDE_PATHS}
)
find_library(SRTP_LIBRARY NAMES srtp2 PATHS ${VCPKG_LIB_PATHS})
if(SRTP_INCLUDE_DIR AND SRTP_LIBRARY)
    target_include_directories(${APP_TARGET} PRIVATE ${SRTP_INCLUDE_DIR})
    target_link_libraries(${APP_TARGET} PRIVATE ${SRTP_LIBRARY})
else()
    message(WARNING "libsrtp2 not found. Make sure vcpkg is correctly installed.")
endif()
