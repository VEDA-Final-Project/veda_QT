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

#include <QtConcurrent>
#include <map>

// ============================================================
// 전역 모델 캐시 관리 (여러 CameraSource가 동일 모델 공유)
// ============================================================
struct ModelCache {
    std::shared_ptr<ov::Core> core;
    std::map<std::string, ov::CompiledModel> compiledModels;
    std::mutex mutex;

    static ModelCache& instance() {
        static ModelCache cache;
        return cache;
    }
    
    ModelCache() : core(std::make_shared<ov::Core>()) {}
};

struct ReIDFeatureExtractor::Impl {
    ov::CompiledModel compiledModel;
    ov::InferRequest  inferRequest;
    std::atomic<bool> ready{false};
    std::string       device;

    Impl() : ready(false) {}
};

ReIDFeatureExtractor::ReIDFeatureExtractor()
    : pimpl(std::make_unique<Impl>()) {}

ReIDFeatureExtractor::~ReIDFeatureExtractor() = default;

bool ReIDFeatureExtractor::loadModel(const QString &modelPath) {
    if (modelPath.isEmpty()) return false;

    QFileInfo fi(modelPath);
    if (!fi.exists()) {
        std::cerr << "[ReID][OV] Model file not found: " << modelPath.toStdString() << std::endl;
        return false;
    }

    pimpl->ready = false;
    
    // 비동기 모델 로딩 (UI 프리징 방지)
    (void)QtConcurrent::run([this, modelPath]() {
        try {
            ModelCache& cache = ModelCache::instance();
            std::lock_guard<std::mutex> lock(cache.mutex);
            
            std::string pathStr = modelPath.toStdString();
            
            // 1. 이미 컴파일된 모델이 있는지 확인
            if (cache.compiledModels.find(pathStr) == cache.compiledModels.end()) {
                std::cout << "[ReID][OV] Loading and compiling new model: " << pathStr << std::endl;
                
                auto model = cache.core->read_model(pathStr);
                
                // 최적의 디바이스 자동 선택
                std::vector<std::string> availableDevices = cache.core->get_available_devices();
                std::string targetDevice = "CPU";
                for (const auto &d : availableDevices) {
                    if (d.find("GPU") != std::string::npos) {
                        targetDevice = "GPU";
                        break;
                    }
                }

                ov::AnyMap config;
                if (targetDevice == "GPU") {
                    config[ov::hint::inference_precision.name()] = ov::element::f16;
                    config[ov::hint::performance_mode.name()]    = ov::hint::PerformanceMode::LATENCY;
                } else {
                    config[ov::hint::performance_mode.name()]    = ov::hint::PerformanceMode::LATENCY;
                    config[ov::inference_num_threads.name()]     = 2;
                }

                cache.compiledModels[pathStr] = cache.core->compile_model(model, targetDevice, config);
            } else {
                std::cout << "[ReID][OV] Using cached model for: " << pathStr << std::endl;
            }

            // 2. 개별 InferRequest 생성
            pimpl->compiledModel = cache.compiledModels[pathStr];
            pimpl->inferRequest  = pimpl->compiledModel.create_infer_request();
            pimpl->ready         = true;

            std::cout << "[ReID][OV] Async model load/ready complete" << std::endl;

        } catch (const std::exception &e) {
            std::cerr << "[ReID][OV] ERROR during async load: " << e.what() << std::endl;
        }
    });

    return true; // 비동기로 시작되었으므로 true 반환 (나중에 pimpl->ready 체크)
}

std::vector<float> ReIDFeatureExtractor::extract(const cv::Mat &image) {
    if (!pimpl || !pimpl->ready || image.empty()) return {};

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
