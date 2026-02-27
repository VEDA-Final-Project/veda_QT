#include "ocr/recognition/paddleocrunner.h"
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace ocr::recognition
{
namespace
{
constexpr int kInvalidScore = std::numeric_limits<int>::min() / 4;

bool resolveTimeAndClassDims(const std::vector<int64_t> &shape,
                             const size_t elementCount, int *timeStepsOut,
                             int *classCountOut)
{
  if (!timeStepsOut || !classCountOut)
  {
    return false;
  }

  *timeStepsOut = 0;
  *classCountOut = 0;

  auto valid = [&](const int t, const int c) {
    if (t <= 0 || c <= 1)
    {
      return false;
    }
    const size_t expected = static_cast<size_t>(t) * static_cast<size_t>(c);
    return expected <= elementCount;
  };

  if (shape.size() == 2)
  {
    const int t = static_cast<int>(shape[0]);
    const int c = static_cast<int>(shape[1]);
    if (valid(t, c))
    {
      *timeStepsOut = t;
      *classCountOut = c;
      return true;
    }
  }

  if (shape.size() == 3)
  {
    if (shape[0] == 1)
    {
      const int t = static_cast<int>(shape[1]);
      const int c = static_cast<int>(shape[2]);
      if (valid(t, c))
      {
        *timeStepsOut = t;
        *classCountOut = c;
        return true;
      }
    }

    if (shape[2] == 1)
    {
      const int t = static_cast<int>(shape[0]);
      const int c = static_cast<int>(shape[1]);
      if (valid(t, c))
      {
        *timeStepsOut = t;
        *classCountOut = c;
        return true;
      }
    }
  }

  if (shape.size() == 4 && shape[0] == 1)
  {
    if (shape[2] == 1)
    {
      const int t = static_cast<int>(shape[1]);
      const int c = static_cast<int>(shape[3]);
      if (valid(t, c))
      {
        *timeStepsOut = t;
        *classCountOut = c;
        return true;
      }
    }

    if (shape[1] == 1)
    {
      const int t = static_cast<int>(shape[2]);
      const int c = static_cast<int>(shape[3]);
      if (valid(t, c))
      {
        *timeStepsOut = t;
        *classCountOut = c;
        return true;
      }
    }

    if (shape[3] == 1)
    {
      const int t = static_cast<int>(shape[1]);
      const int c = static_cast<int>(shape[2]);
      if (valid(t, c))
      {
        *timeStepsOut = t;
        *classCountOut = c;
        return true;
      }
    }
  }

  return false;
}

cv::Mat ensureRgbImage(const cv::Mat &image)
{
  if (image.empty())
  {
    return cv::Mat();
  }

  if (image.type() == CV_8UC3)
  {
    return image;
  }

  if (image.type() == CV_8UC1)
  {
    cv::Mat rgb;
    cv::cvtColor(image, rgb, cv::COLOR_GRAY2RGB);
    return rgb;
  }

  if (image.type() == CV_8UC4)
  {
    cv::Mat rgb;
    cv::cvtColor(image, rgb, cv::COLOR_RGBA2RGB);
    return rgb;
  }

  return cv::Mat();
}

} // namespace

bool PaddleOcrRunner::init(const QString &modelPath, const QString &dictPath,
                           const int inputWidth, const int inputHeight,
                           QString *errorOut)
{
  m_ready = false;
  m_dictionary.clear();
  m_env.reset();
  m_session.reset();
  m_inputNameStorage.clear();
  m_outputNameStorage.clear();
  m_inputNames.clear();
  m_outputNames.clear();

  if (errorOut)
  {
    errorOut->clear();
  }

  const QFileInfo modelInfo(modelPath);
  if (!modelInfo.exists() || !modelInfo.isFile())
  {
    if (errorOut)
    {
      *errorOut = QStringLiteral("ONNX model file not found: %1").arg(modelPath);
    }
    return false;
  }

  m_inputWidth = (inputWidth > 0) ? inputWidth : 320;
  m_inputHeight = (inputHeight > 0) ? inputHeight : 48;

  try
  {
    m_env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "paddleocr");

    Ort::SessionOptions options;
    options.SetIntraOpNumThreads(1);
    options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

#ifdef _WIN32
    const std::wstring modelPathW = modelInfo.absoluteFilePath().toStdWString();
    m_session =
        std::make_unique<Ort::Session>(*m_env, modelPathW.c_str(), options);
#else
    const std::string modelPathA = modelInfo.absoluteFilePath().toStdString();
    m_session =
        std::make_unique<Ort::Session>(*m_env, modelPathA.c_str(), options);
#endif

    Ort::AllocatorWithDefaultOptions allocator;

    const size_t inputCount = m_session->GetInputCount();
    const size_t outputCount = m_session->GetOutputCount();
    if (inputCount == 0 || outputCount == 0)
    {
      if (errorOut)
      {
        *errorOut = QStringLiteral("Invalid ONNX model: no input/output node");
      }
      m_session.reset();
      m_env.reset();
      return false;
    }

    m_inputNameStorage.reserve(inputCount);
    m_outputNameStorage.reserve(outputCount);
    m_inputNames.reserve(inputCount);
    m_outputNames.reserve(outputCount);

    for (size_t i = 0; i < inputCount; ++i)
    {
      Ort::AllocatedStringPtr name = m_session->GetInputNameAllocated(i, allocator);
      if (!name || !name.get() || name.get()[0] == '\0')
      {
        continue;
      }
      m_inputNameStorage.emplace_back(name.get());
    }

    for (size_t i = 0; i < outputCount; ++i)
    {
      Ort::AllocatedStringPtr name = m_session->GetOutputNameAllocated(i, allocator);
      if (!name || !name.get() || name.get()[0] == '\0')
      {
        continue;
      }
      m_outputNameStorage.emplace_back(name.get());
    }

    for (const std::string &name : m_inputNameStorage)
    {
      m_inputNames.push_back(name.c_str());
    }
    for (const std::string &name : m_outputNameStorage)
    {
      m_outputNames.push_back(name.c_str());
    }

    if (m_inputNames.empty() || m_outputNames.empty())
    {
      if (errorOut)
      {
        *errorOut =
            QStringLiteral("Invalid ONNX model: unresolved input/output names");
      }
      m_session.reset();
      m_env.reset();
      return false;
    }
  }
  catch (const Ort::Exception &e)
  {
    if (errorOut)
    {
      *errorOut = QStringLiteral("Failed to load ONNX model (ONNX Runtime): %1")
                      .arg(QString::fromUtf8(e.what()));
    }
    m_session.reset();
    m_env.reset();
    return false;
  }

  if (!loadDictionary(dictPath, errorOut))
  {
    m_session.reset();
    m_env.reset();
    return false;
  }

  m_ready = true;
  return true;
}

bool PaddleOcrRunner::isReady() const
{
  return m_ready;
}

std::vector<postprocess::OcrCandidate>
PaddleOcrRunner::collectCandidates(
    const std::vector<preprocess::OcrInputVariant> &variants)
{
  std::vector<postprocess::OcrCandidate> candidates;
  candidates.reserve(variants.size());

  for (const preprocess::OcrInputVariant &variant : variants)
  {
    if (variant.imageRgb.empty())
    {
      continue;
    }

    const postprocess::OcrCandidate candidate =
        runOcrOnImage(variant.imageRgb, variant.tag);
    if (!candidate.normalizedText.isEmpty() || !candidate.rawText.isEmpty())
    {
      candidates.push_back(candidate);
    }
  }

  return candidates;
}

bool PaddleOcrRunner::loadDictionary(const QString &dictPath, QString *errorOut)
{
  m_dictionary.clear();
  m_dictionary.push_back(QString());

  const QFileInfo dictInfo(dictPath);
  if (!dictPath.trimmed().isEmpty() && dictInfo.exists() && dictInfo.isFile())
  {
    QFile file(dictInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
      if (errorOut)
      {
        *errorOut = QStringLiteral("Failed to open dictionary: %1")
                        .arg(dictInfo.absoluteFilePath());
      }
      return false;
    }

    QTextStream in(&file);
    while (!in.atEnd())
    {
      const QString line = in.readLine().trimmed();
      if (!line.isEmpty())
      {
        m_dictionary.push_back(line);
      }
    }
    file.close();

    if (m_dictionary.size() > 1)
    {
      return true;
    }

    if (errorOut)
    {
      *errorOut = QStringLiteral("Dictionary file is empty: %1")
                      .arg(dictInfo.absoluteFilePath());
    }
    return false;
  }

  m_dictionary = fallbackDictionary();
  return !m_dictionary.empty();
}

std::vector<QString> PaddleOcrRunner::fallbackDictionary()
{
  const QString chars =
      QStringLiteral("0123456789"
                     "가나다라마바사아자하"
                     "거너더러머버서어저허"
                     "고노도로모보소오조호"
                     "구누두루무부수우주");

  std::vector<QString> dictionary;
  dictionary.reserve(chars.size() + 1);
  dictionary.push_back(QString());
  for (const QChar ch : chars)
  {
    dictionary.push_back(QString(ch));
  }
  return dictionary;
}

postprocess::OcrCandidate PaddleOcrRunner::runOcrOnImage(const cv::Mat &imageRgb,
                                                         const QString &sourceTag)
{
  postprocess::OcrCandidate out;
  out.sourceTag = sourceTag;
  out.score = kInvalidScore;
  out.confidence = -1;

  if (!m_ready || !m_session)
  {
    return out;
  }

  cv::Mat blob = makeInputBlob(imageRgb);
  if (blob.empty() || !blob.isContinuous())
  {
    return out;
  }

  Ort::MemoryInfo memoryInfo =
      Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
  std::array<int64_t, 4> inputShape = {1, 3, m_inputHeight, m_inputWidth};

  Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
      memoryInfo, blob.ptr<float>(), static_cast<size_t>(blob.total()),
      inputShape.data(), inputShape.size());

  std::vector<Ort::Value> outputTensors;
  try
  {
    outputTensors = m_session->Run(Ort::RunOptions{nullptr}, m_inputNames.data(),
                                   &inputTensor, 1, m_outputNames.data(),
                                   m_outputNames.size());
  }
  catch (const Ort::Exception &)
  {
    return out;
  }

  if (outputTensors.empty() || !outputTensors.front().IsTensor())
  {
    return out;
  }

  const Ort::Value &outputTensor = outputTensors.front();
  const Ort::TensorTypeAndShapeInfo shapeInfo =
      outputTensor.GetTensorTypeAndShapeInfo();
  if (shapeInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
  {
    return out;
  }

  const std::vector<int64_t> outputShape = shapeInfo.GetShape();
  const size_t elementCount = shapeInfo.GetElementCount();

  int timeSteps = 0;
  int classCount = 0;
  if (!resolveTimeAndClassDims(outputShape, elementCount, &timeSteps,
                               &classCount))
  {
    return out;
  }

  const float *raw = outputTensor.GetTensorData<float>();
  if (!raw)
  {
    return out;
  }

  cv::Mat scores(timeSteps, classCount, CV_32F, const_cast<float *>(raw));
  out.rawText = decodeCtcGreedy(scores, classCount, &out.confidence);
  out.normalizedText = normalizeSingleCandidate(out.rawText);
  out.score = 0;
  return out;
}

cv::Mat PaddleOcrRunner::makeInputBlob(const cv::Mat &imageRgb) const
{
  cv::Mat rgb = ensureRgbImage(imageRgb);
  if (rgb.empty() || m_inputWidth <= 0 || m_inputHeight <= 0)
  {
    return cv::Mat();
  }

  const float ratio = static_cast<float>(rgb.cols) / std::max(1, rgb.rows);
  const int resizedWidth = std::clamp(
      static_cast<int>(std::ceil(m_inputHeight * ratio)), 1, m_inputWidth);

  cv::Mat resized;
  cv::resize(rgb, resized, cv::Size(resizedWidth, m_inputHeight), 0.0, 0.0,
             cv::INTER_LINEAR);

  cv::Mat canvas = cv::Mat::zeros(m_inputHeight, m_inputWidth, CV_8UC3);
  resized.copyTo(canvas(cv::Rect(0, 0, resizedWidth, m_inputHeight)));

  return cv::dnn::blobFromImage(canvas, 1.0 / 127.5,
                                cv::Size(m_inputWidth, m_inputHeight),
                                cv::Scalar(127.5, 127.5, 127.5), false, false,
                                CV_32F);
}

QString PaddleOcrRunner::decodeCtcGreedy(const cv::Mat &scores,
                                         const int classCount,
                                         int *confidenceOut) const
{
  if (confidenceOut)
  {
    *confidenceOut = -1;
  }

  if (scores.empty() || classCount <= 1)
  {
    return QString();
  }

  const int blankIndex = 0;
  int prevIndex = blankIndex;

  QString decoded;
  double confSum = 0.0;
  int confCount = 0;

  for (int t = 0; t < scores.rows; ++t)
  {
    const cv::Mat row = scores.row(t);
    double maxVal = 0.0;
    cv::Point maxLoc;
    cv::minMaxLoc(row, nullptr, &maxVal, nullptr, &maxLoc);

    const int index = maxLoc.x;
    if (index == blankIndex)
    {
      prevIndex = index;
      continue;
    }

    if (index == prevIndex)
    {
      continue;
    }

    prevIndex = index;

    if (index < 0 || index >= static_cast<int>(m_dictionary.size()))
    {
      continue;
    }

    decoded.append(m_dictionary[index]);
    confSum += maxVal;
    ++confCount;
  }

  if (confidenceOut && confCount > 0)
  {
    *confidenceOut = static_cast<int>(std::lround((confSum / confCount) * 100.0));
  }

  return decoded;
}

QString PaddleOcrRunner::normalizeSingleCandidate(const QString &raw)
{
  return postprocess::normalizePlateTextWithConfusableFix(raw);
}

} // namespace ocr::recognition
