#include "reidextractor.h"
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// HAVE_OPENVINO는 CMakeLists.txt에서 정의됨
#ifdef HAVE_OPENVINO
#include <openvino/openvino.hpp>
#endif

#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>

// ============================================================
// OpenVINO 구현부
// ============================================================
#ifdef HAVE_OPENVINO

struct ReIDFeatureExtractor::Impl {
    std::unique_ptr<ov::Core> core;
    ov::CompiledModel compiledModel;
    ov::InferRequest  inferRequest;
    bool              ready = false;
    std::string       device;

    Impl() : core(nullptr), ready(false) {}
};

ReIDFeatureExtractor::ReIDFeatureExtractor()
    : pimpl(std::make_unique<Impl>()) {}

ReIDFeatureExtractor::~ReIDFeatureExtractor() = default;

bool ReIDFeatureExtractor::loadModel(const QString &modelPath) {
    std::cout << "[ReID][OV] Attempting to load: " << modelPath.toStdString() << std::endl << std::flush;

    QFileInfo fi(modelPath);
    if (!fi.exists()) {
        std::cerr << "[ReID][OV] Model file not found: " << modelPath.toStdString() << std::endl << std::flush;
        return false;
    }

    pimpl->ready = false;
    try {
        // Core를 매번 새로 생성하여 플러그인 탐색 시도 (정적 생성 지양)
        pimpl->core = std::make_unique<ov::Core>();
        
        // 플러그인 로드 확인
        std::vector<std::string> availableDevices = pimpl->core->get_available_devices();
        std::cout << "[ReID][OV] Available devices: ";
        for (auto &d : availableDevices) std::cout << d << " ";
        std::cout << std::endl << std::flush;

        if (availableDevices.empty()) {
            std::cerr << "[ReID][OV] NO DEVICES FOUND. OpenVINO plugins may be missing from the EXE folder." << std::endl << std::flush;
            return false;
        }

        // 우선순위 결정
        std::string targetDevice = "CPU"; // 기본값
        for (const auto &d : availableDevices) {
            if (d.find("GPU") != std::string::npos) {
                targetDevice = "GPU"; // GPU 있으면 GPU 사용
                break;
            }
        }

        std::cout << "[ReID][OV] Selected device: " << targetDevice << std::endl << std::flush;

        // 모델 읽기
        auto model = pimpl->core->read_model(modelPath.toStdString());

        // 타겟 디바이스 설정
        ov::AnyMap config;
        if (targetDevice == "GPU") {
            config[ov::hint::inference_precision.name()] = ov::element::f16;
            config[ov::hint::performance_mode.name()]    = ov::hint::PerformanceMode::LATENCY;
        } else {
            config[ov::hint::performance_mode.name()]    = ov::hint::PerformanceMode::LATENCY;
            config[ov::inference_num_threads.name()]     = 2;
        }

        // 모델 컴파일 (이 단계에서 실제 플러그인 DLL 로드 시도)
        pimpl->compiledModel = pimpl->core->compile_model(model, targetDevice, config);
        pimpl->inferRequest  = pimpl->compiledModel.create_infer_request();
        pimpl->device        = targetDevice;
        pimpl->ready         = true;

        std::cout << "[ReID][OV] Model loaded on " << targetDevice << " successfully!" << std::endl << std::flush;
        return true;

    } catch (const std::exception &e) {
        std::cerr << "[ReID][OV] CRITICAL ERROR during load: " << e.what() << std::endl << std::flush;
    } catch (...) {
        std::cerr << "[ReID][OV] UNKNOWN CRITICAL ERROR during load." << std::endl << std::flush;
    }

    pimpl->ready = false;
    return false;
}

std::vector<float> ReIDFeatureExtractor::extract(const cv::Mat &image) {
    if (!pimpl->ready || image.empty()) return {};

    try {
        cv::Mat resized;
        cv::resize(image, resized, cv::Size(256, 256));

        cv::Mat rgb;
        cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);

        cv::Mat blob = cv::dnn::blobFromImage(
            rgb, 1.0 / 255.0, cv::Size(256, 256),
            cv::Scalar(0.485 * 255.0, 0.456 * 255.0, 0.406 * 255.0),
            false, false, CV_32F);

        float *blobData = reinterpret_cast<float *>(blob.data);
        const float invStd[] = {1.0f / 0.229f, 1.0f / 0.224f, 1.0f / 0.225f};
        for (int c = 0; c < 3; ++c) {
            cv::Mat plane(256, 256, CV_32F, blobData + (c * 256 * 256));
            plane *= invStd[c];
        }

        ov::Tensor inputTensor(ov::element::f32, {1, 3, 256, 256}, blobData);
        pimpl->inferRequest.set_input_tensor(inputTensor);
        pimpl->inferRequest.infer();

        const ov::Tensor &outputTensor = pimpl->inferRequest.get_output_tensor();
        const float *outData = outputTensor.data<float>();
        const size_t count   = outputTensor.get_size();

        std::vector<float> features(outData, outData + count);

        float norm = 0.0f;
        for (float f : features) norm += f * f;
        norm = std::sqrt(norm);
        if (norm > 1e-6f)
            for (float &f : features) f /= norm;

        return features;

    } catch (const std::exception &e) {
        std::cerr << "[ReID][OV] Inference error: " << e.what() << std::endl << std::flush;
    } catch (...) {
        std::cerr << "[ReID][OV] Unknown inference error." << std::endl << std::flush;
    }
    return {};
}

// ============================================================
// ONNX Runtime + DirectML 폴백 (HAVE_OPENVINO 미정의 시)
// ============================================================
#else 

struct ReIDFeatureExtractor::Impl {
    bool ready = false;
};

ReIDFeatureExtractor::ReIDFeatureExtractor() : pimpl(std::make_unique<Impl>()) {}
ReIDFeatureExtractor::~ReIDFeatureExtractor() = default;

bool ReIDFeatureExtractor::loadModel(const QString &modelPath) {
    std::cout << "[ReID] OpenVINO not enabled. GPU acceleration disabled." << std::endl;
    return false;
}

std::vector<float> ReIDFeatureExtractor::extract(const cv::Mat &image) {
    return {};
}

#endif
