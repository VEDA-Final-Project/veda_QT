#include "reidextractor.h"
#include <cmath>
#include <iostream>
#include <mutex>
#include <onnxruntime_cxx_api.h>

namespace {
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

#ifdef _WIN32
    const std::wstring wPath = modelPath.toStdWString();
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
  if (!pimpl->session || image.empty()) {
    return {};
  }

  try {
    cv::Mat resized;
    cv::resize(image, resized, cv::Size(256, 256));

    cv::Mat rgb;
    cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);

    cv::Mat blob = cv::dnn::blobFromImage(
        rgb, 1.0 / 255.0, cv::Size(256, 256),
        cv::Scalar(0.485 * 255.0, 0.456 * 255.0, 0.406 * 255.0), false, false,
        CV_32F);

    float *blobData = reinterpret_cast<float *>(blob.data);
    const float invStd[] = {1.0f / 0.229f, 1.0f / 0.224f, 1.0f / 0.225f};
    for (int c = 0; c < 3; ++c) {
      cv::Mat plane(256, 256, CV_32F, blobData + (c * 256 * 256));
      plane *= invStd[c];
    }

    Ort::MemoryInfo memoryInfo =
        Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::vector<int64_t> inputDims = {1, 3, 256, 256};
    Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
        memoryInfo, blobData, blob.total() * blob.channels(), inputDims.data(),
        inputDims.size());

    Ort::AllocatorWithDefaultOptions allocator;
    auto inputNamePtr = pimpl->session->GetInputNameAllocated(0, allocator);
    auto outputNamePtr = pimpl->session->GetOutputNameAllocated(0, allocator);
    const char *inputNames[] = {inputNamePtr.get()};
    const char *outputNames[] = {outputNamePtr.get()};

    auto outputTensors = pimpl->session->Run(
        Ort::RunOptions{nullptr}, inputNames, &inputTensor, 1, outputNames, 1);

    float *floatArray = outputTensors.front().GetTensorMutableData<float>();
    const size_t outputCount =
        outputTensors.front().GetTensorTypeAndShapeInfo().GetElementCount();

    std::vector<float> features(floatArray, floatArray + outputCount);

    float norm = 0.0f;
    for (float f : features) {
      norm += f * f;
    }
    norm = std::sqrt(norm);
    if (norm > 1e-6f) {
      for (float &f : features) {
        f /= norm;
      }
    }

    return features;
  } catch (const Ort::Exception &e) {
    std::cerr << "[ReID] Inference error: " << e.what() << std::endl;
    return {};
  }
}
