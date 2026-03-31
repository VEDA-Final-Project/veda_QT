#include "reidextractor.h"

#include <QFileInfo>

#include <cmath>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <openvino/openvino.hpp>
#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>

namespace {
std::vector<float> extractFeaturesWithRequest(ov::InferRequest &inferRequest,
                                             const cv::Mat &image) {
  if (image.empty()) {
    return {};
  }

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

  ov::Tensor inputTensor(ov::element::f32, {1, 3, 256, 256}, blobData);
  inferRequest.set_input_tensor(inputTensor);
  inferRequest.infer();

  ov::Tensor outputTensor = inferRequest.get_output_tensor();
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
}
} // namespace

struct SharedReidRuntime::Impl {
  std::unique_ptr<ov::Core> core;
  ov::CompiledModel compiledModel;
  mutable std::mutex mutex;
  bool ready = false;
  QString modelPath;
  QString deviceName;
  QString lastError;
};

struct ReidSession::Impl {
  explicit Impl(std::shared_ptr<SharedReidRuntime::Impl> runtimeImpl,
                ov::InferRequest request)
      : runtime(std::move(runtimeImpl)), inferRequest(std::move(request)) {}

  std::shared_ptr<SharedReidRuntime::Impl> runtime;
  ov::InferRequest inferRequest;
  mutable std::mutex mutex;
};

namespace {
bool compileModelForDevice(SharedReidRuntime::Impl &runtime,
                           const std::string &targetDevice,
                           QString *errorMessage) {
  try {
    auto model = runtime.core->read_model(runtime.modelPath.toStdString());

    ov::AnyMap config;
    config[ov::hint::performance_mode.name()] =
        ov::hint::PerformanceMode::LATENCY;
    if (targetDevice == "GPU") {
      config[ov::hint::inference_precision.name()] = ov::element::f16;
    } else {
      config[ov::inference_num_threads.name()] = 2;
    }

    runtime.compiledModel =
        runtime.core->compile_model(model, targetDevice, config);
    runtime.ready = true;
    runtime.deviceName = QString::fromStdString(targetDevice);
    if (errorMessage) {
      errorMessage->clear();
    }
    return true;
  } catch (const std::exception &e) {
    if (errorMessage) {
      *errorMessage = QString::fromUtf8(e.what());
    }
  } catch (...) {
    if (errorMessage) {
      *errorMessage = QStringLiteral("Unknown compile error");
    }
  }

  runtime.ready = false;
  return false;
}
} // namespace

SharedReidRuntime::SharedReidRuntime() : pimpl(std::make_shared<Impl>()) {}

SharedReidRuntime::~SharedReidRuntime() = default;

bool SharedReidRuntime::loadOnce(const QString &modelPath) {
  const QString normalizedModelPath = modelPath.trimmed();
  std::lock_guard<std::mutex> lock(pimpl->mutex);

  if (pimpl->ready) {
    return true;
  }

  if (normalizedModelPath.isEmpty()) {
    pimpl->lastError = QStringLiteral("Model path is empty.");
    return false;
  }

  QFileInfo fi(normalizedModelPath);
  if (!fi.exists()) {
    pimpl->lastError =
        QStringLiteral("Model file not found: %1").arg(normalizedModelPath);
    return false;
  }

  try {
    pimpl->core = std::make_unique<ov::Core>();

    const std::vector<std::string> availableDevices =
        pimpl->core->get_available_devices();
    if (availableDevices.empty()) {
      pimpl->lastError = QStringLiteral(
          "No OpenVINO devices found. Plugins may be missing.");
      return false;
    }

    std::string targetDevice = "CPU";
    for (const auto &device : availableDevices) {
      if (device.find("GPU") != std::string::npos) {
        targetDevice = "GPU";
        break;
      }
    }

    pimpl->modelPath = normalizedModelPath;
    QString compileError;
    if (compileModelForDevice(*pimpl, targetDevice, &compileError)) {
      pimpl->lastError.clear();
      return true;
    }

    if (targetDevice == "GPU") {
      QString cpuError;
      if (compileModelForDevice(*pimpl, "CPU", &cpuError)) {
        pimpl->lastError = QStringLiteral(
            "GPU compile failed, fell back to CPU: %1")
                               .arg(compileError);
        return true;
      }
      pimpl->lastError = QStringLiteral(
          "GPU compile failed (%1), CPU fallback failed (%2)")
                             .arg(compileError, cpuError);
      return false;
    }

    pimpl->lastError =
        QStringLiteral("Compile error on %1: %2")
            .arg(QString::fromStdString(targetDevice), compileError);
    return false;

  } catch (const std::exception &e) {
    pimpl->lastError =
        QStringLiteral("OpenVINO load error: %1").arg(QString::fromUtf8(e.what()));
  } catch (...) {
    pimpl->lastError = QStringLiteral("Unknown OpenVINO load error.");
  }

  pimpl->ready = false;
  return false;
}

bool SharedReidRuntime::isReady() const {
  std::lock_guard<std::mutex> lock(pimpl->mutex);
  return pimpl->ready;
}

QString SharedReidRuntime::deviceName() const {
  std::lock_guard<std::mutex> lock(pimpl->mutex);
  return pimpl->deviceName;
}

QString SharedReidRuntime::lastError() const {
  std::lock_guard<std::mutex> lock(pimpl->mutex);
  return pimpl->lastError;
}

std::shared_ptr<ReidSession> SharedReidRuntime::createSession() const {
  std::lock_guard<std::mutex> lock(pimpl->mutex);
  if (!pimpl->ready) {
    return nullptr;
  }

  try {
    ov::InferRequest inferRequest = pimpl->compiledModel.create_infer_request();
    return std::shared_ptr<ReidSession>(
        new ReidSession(std::make_unique<ReidSession::Impl>(pimpl, std::move(inferRequest))));
  } catch (const std::exception &e) {
    if (pimpl->deviceName.compare(QStringLiteral("GPU"), Qt::CaseInsensitive) ==
        0) {
      QString cpuError;
      if (compileModelForDevice(*pimpl, "CPU", &cpuError)) {
        pimpl->lastError = QStringLiteral(
            "GPU session creation failed, fell back to CPU: %1")
                               .arg(QString::fromUtf8(e.what()));
        try {
          ov::InferRequest inferRequest =
              pimpl->compiledModel.create_infer_request();
          return std::shared_ptr<ReidSession>(new ReidSession(
              std::make_unique<ReidSession::Impl>(pimpl, std::move(inferRequest))));
        } catch (const std::exception &retryError) {
          pimpl->lastError = QStringLiteral(
              "CPU fallback session creation failed: %1")
                                 .arg(QString::fromUtf8(retryError.what()));
        } catch (...) {
          pimpl->lastError =
              QStringLiteral("CPU fallback session creation failed.");
        }
      } else {
        pimpl->lastError = QStringLiteral(
            "GPU session creation failed (%1), CPU fallback failed (%2)")
                               .arg(QString::fromUtf8(e.what()), cpuError);
      }
    } else {
      pimpl->lastError = QStringLiteral("Session creation failed: %1")
                             .arg(QString::fromUtf8(e.what()));
    }
    return nullptr;
  } catch (...) {
    pimpl->lastError = QStringLiteral("Session creation failed.");
    return nullptr;
  }
}

ReidSession::ReidSession() = default;

ReidSession::ReidSession(std::unique_ptr<Impl> impl) : pimpl(std::move(impl)) {}

ReidSession::~ReidSession() = default;

bool ReidSession::isReady() const {
  if (!pimpl || !pimpl->runtime) {
    return false;
  }

  std::lock_guard<std::mutex> lock(pimpl->runtime->mutex);
  return pimpl->runtime->ready;
}

std::vector<float> ReidSession::extract(const cv::Mat &image) {
  if (!pimpl || !isReady() || image.empty()) {
    return {};
  }

  std::lock_guard<std::mutex> lock(pimpl->mutex);
  try {
    return extractFeaturesWithRequest(pimpl->inferRequest, image);
  } catch (...) {
    return {};
  }
}
