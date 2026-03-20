#include "reidextractor.h"
#include <algorithm>
#include <iostream>
#include <mutex>
#include <numeric>
#include <onnxruntime_cxx_api.h>
#include <vector>

namespace {
// ONNX Runtime 전역 환경 관리 (PaddleOCR과의 충돌 방지를 위해 정적 변수 사용)
static std::unique_ptr<Ort::Env> g_ortEnv;
static std::mutex g_envMutex;

Ort::Env &getOrtEnv() {
  std::lock_guard<std::mutex> lock(g_envMutex);
  if (!g_ortEnv) {
    g_ortEnv =
        std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "FastReID_Env");
  }
  return *g_ortEnv;
}
} // namespace

struct ReIDFeatureExtractor::Impl {
  Ort::SessionOptions sessionOptions;
  Ort::Session *session = nullptr;

  Impl() {
    sessionOptions.SetIntraOpNumThreads(1);
    sessionOptions.SetGraphOptimizationLevel(
        GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
  }

  ~Impl() {
    if (session) {
      delete session;
    }
  }
};

ReIDFeatureExtractor::ReIDFeatureExtractor()
    : pimpl(std::make_unique<Impl>()) {}

ReIDFeatureExtractor::~ReIDFeatureExtractor() = default;

bool ReIDFeatureExtractor::loadModel(const QString &modelPath) {
  try {
    if (pimpl->session) {
      delete pimpl->session;
      pimpl->session = nullptr;
    }

    // 사용자님이 제공해주신 경로 로직 적용
#ifdef _WIN32
    std::wstring wPath = modelPath.toStdWString();
    pimpl->session =
        new Ort::Session(getOrtEnv(), wPath.c_str(), pimpl->sessionOptions);
#else
    pimpl->session = new Ort::Session(
        getOrtEnv(), modelPath.toStdString().c_str(), pimpl->sessionOptions);
#endif
    std::cout << "[ReID] ONNX Model loaded successfully!" << std::endl;
    return true;
  } catch (const Ort::Exception &e) {
    std::cerr << "[ReID] Model load failed: " << e.what() << std::endl;
    return false;
  }
}

std::vector<float> ReIDFeatureExtractor::extract(const cv::Mat &image) {
  if (!pimpl->session || image.empty())
    return {};

  try {
    // 1. Preprocessing: Stretch to 256x256 (Matching typical ReID model
    // training)
    cv::Mat resized;
    cv::resize(image, resized, cv::Size(256, 256));

    cv::Mat rgb;
    cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);

    // Scaling to [0, 1] and Mean/Std Normalization
    cv::Mat blob = cv::dnn::blobFromImage(
        rgb, 1.0 / 255.0, cv::Size(256, 256),
        cv::Scalar(0.485 * 255.0, 0.456 * 255.0, 0.406 * 255.0), false, false,
        CV_32F);
    
    // Step 2: Divide by Std using SIMD-accelerated OpenCV operations
    float *blobData = reinterpret_cast<float *>(blob.data);
    const float inv_std[] = {1.0f/0.229f, 1.0f/0.224f, 1.0f/0.225f};
    for (int c = 0; c < 3; ++c) {
        cv::Mat plane(256, 256, CV_32F, blobData + (c * 256 * 256));
        plane *= inv_std[c];
    }

    Ort::MemoryInfo memoryInfo =
        Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::vector<int64_t> inputDims = {1, 3, 256, 256};
    Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
        memoryInfo, blobData, blob.total() * blob.channels(), inputDims.data(),
        inputDims.size());

    // Dynamic node names
    Ort::AllocatorWithDefaultOptions allocator;
    auto inputNamePtr = pimpl->session->GetInputNameAllocated(0, allocator);
    auto outputNamePtr = pimpl->session->GetOutputNameAllocated(0, allocator);
    const char *inputNames[] = {inputNamePtr.get()};
    const char *outputNames[] = {outputNamePtr.get()};

    auto outputTensors = pimpl->session->Run(
        Ort::RunOptions{nullptr}, inputNames, &inputTensor, 1, outputNames, 1);

    // 결과 추출
    float *floatArray = outputTensors.front().GetTensorMutableData<float>();
    Ort::TensorTypeAndShapeInfo outputInfo =
        outputTensors.front().GetTensorTypeAndShapeInfo();
    size_t outputCount = outputInfo.GetElementCount();

    std::vector<float> features(floatArray, floatArray + outputCount);

    // L2 Normalization (ReID 매칭을 위해 필수)
    float norm = 0.0f;
    for (float f : features)
      norm += f * f;
    norm = std::sqrt(norm);
    if (norm > 1e-6) {
      for (float &f : features)
        f /= norm;
    }

    return features;

  } catch (const Ort::Exception &e) {
    std::cerr << "[ReID] Inference error: " << e.what() << std::endl;
    return {};
  }
}
