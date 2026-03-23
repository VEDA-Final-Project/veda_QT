#include "reidextractor.h"

#include <QFile>
#include <QFileInfo>
#include <QString>

#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#ifdef HAVE_OPENVINO
#include <openvino/openvino.hpp>
#endif

#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>

#ifdef HAVE_OPENVINO

struct ReIDFeatureExtractor::Impl {
  std::unique_ptr<ov::Core> core;
  ov::CompiledModel compiledModel;
  bool ready = false;
  std::string device;

  Impl() = default;
};

ReIDFeatureExtractor::ReIDFeatureExtractor()
    : pimpl(std::make_unique<Impl>()) {}

ReIDFeatureExtractor::~ReIDFeatureExtractor() = default;

bool ReIDFeatureExtractor::loadModel(const QString &modelPath) {
  QFileInfo fi(modelPath);
  if (!fi.exists()) {
    std::cerr << "[ReID][OV] Model file not found: "
              << modelPath.toStdString() << std::endl
              << std::flush;
    return false;
  }

  pimpl->ready = false;
  try {
    pimpl->core = std::make_unique<ov::Core>();

    const std::vector<std::string> availableDevices =
        pimpl->core->get_available_devices();

    if (availableDevices.empty()) {
      std::cerr << "[ReID][OV] NO DEVICES FOUND. OpenVINO plugins may be "
                   "missing from the EXE folder."
                << std::endl
                << std::flush;
      return false;
    }

    std::string targetDevice = "CPU";
    for (const auto &d : availableDevices) {
      if (d.find("GPU") != std::string::npos) {
        targetDevice = "GPU";
        break;
      }
    }

    auto model = pimpl->core->read_model(modelPath.toStdString());

    ov::AnyMap config;
    config[ov::hint::performance_mode.name()] =
        ov::hint::PerformanceMode::LATENCY;
    if (targetDevice == "GPU") {
      config[ov::hint::inference_precision.name()] = ov::element::f16;
    } else {
      config[ov::inference_num_threads.name()] = 2;
    }

    pimpl->compiledModel =
        pimpl->core->compile_model(model, targetDevice, config);
    pimpl->device = targetDevice;
    pimpl->ready = true;
    return true;

  } catch (const std::exception &e) {
    std::cerr << "[ReID][OV] CRITICAL ERROR during load: " << e.what()
              << std::endl
              << std::flush;
  } catch (...) {
    std::cerr << "[ReID][OV] UNKNOWN CRITICAL ERROR during load." << std::endl
              << std::flush;
  }

  pimpl->ready = false;
  return false;
}

std::vector<float> ReIDFeatureExtractor::extract(const cv::Mat &image) {
  if (!pimpl->ready || image.empty()) {
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

    ov::InferRequest inferRequest = pimpl->compiledModel.create_infer_request();
    ov::Tensor inputTensor(ov::element::f32, {1, 3, 256, 256}, blobData);
    inferRequest.set_input_tensor(inputTensor);
    inferRequest.infer();

    const ov::Tensor &outputTensor = inferRequest.get_output_tensor();
    const float *outData = outputTensor.data<const float>();
    const size_t count = outputTensor.get_size();

    std::vector<float> features(outData, outData + count);

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

  } catch (const std::exception &e) {
    std::cerr << "[ReID][OV] Inference error: " << e.what() << std::endl
              << std::flush;
  } catch (...) {
    std::cerr << "[ReID][OV] Unknown inference error." << std::endl
              << std::flush;
  }

  return {};
}

#else

struct ReIDFeatureExtractor::Impl {
  bool ready = false;
};

ReIDFeatureExtractor::ReIDFeatureExtractor()
    : pimpl(std::make_unique<Impl>()) {}

ReIDFeatureExtractor::~ReIDFeatureExtractor() = default;

bool ReIDFeatureExtractor::loadModel(const QString &modelPath) {
  (void)modelPath;
  std::cout << "[ReID] OpenVINO not enabled. GPU acceleration disabled."
            << std::endl;
  return false;
}

std::vector<float> ReIDFeatureExtractor::extract(const cv::Mat &image) {
  (void)image;
  return {};
}

#endif
