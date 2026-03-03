#include "ocrmanager.h"
#include "ocr/debug/ocrdebugdumper.h"
#include "ocr/postprocess/platepostprocessor.h"
#include "ocr/preprocess/platepreprocessor.h"
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

namespace
{
constexpr int kDefaultInputWidth = 320;
constexpr int kDefaultInputHeight = 48;
}

OcrManager::OcrManager() = default;

OcrManager::~OcrManager() = default;

QString OcrManager::findFirstFileRecursively(const QString &rootPath,
                                             const QStringList &filters)
{
  const QString trimmedRoot = rootPath.trimmed();
  if (trimmedRoot.isEmpty() || filters.isEmpty())
  {
    return QString();
  }

  const QFileInfo rootInfo(trimmedRoot);
  if (!rootInfo.exists() || !rootInfo.isDir())
  {
    return QString();
  }

  for (const QString &filter : filters)
  {
    QDirIterator it(rootInfo.absoluteFilePath(), QStringList(filter), QDir::Files,
                    QDirIterator::Subdirectories);
    if (it.hasNext())
    {
      return QFileInfo(it.next()).absoluteFilePath();
    }
  }

  return QString();
}

QString OcrManager::resolveFilePath(const QString &pathOrDir,
                                    const QStringList &filters)
{
  const QString candidate = pathOrDir.trimmed();
  if (candidate.isEmpty())
  {
    return QString();
  }

  QFileInfo info(candidate);
  if (!info.exists() && info.isRelative())
  {
    const QDir currentDir(QDir::currentPath());
    info = QFileInfo(currentDir.absoluteFilePath(candidate));
  }

  if (info.exists() && info.isFile())
  {
    return info.absoluteFilePath();
  }

  if (info.exists() && info.isDir())
  {
    return findFirstFileRecursively(info.absoluteFilePath(), filters);
  }

  return QString();
}

QString OcrManager::resolveModelPath(const QString &modelPath) const
{
  const QString explicitPath =
      resolveFilePath(modelPath, QStringList{QStringLiteral("*.onnx")});
  if (!explicitPath.isEmpty())
  {
    return explicitPath;
  }

  const QString downloadsDir = QDir::home().filePath(QStringLiteral("Downloads"));
  return resolveFilePath(downloadsDir, QStringList{QStringLiteral("*.onnx")});
}

QString OcrManager::resolveDictionaryPath(const QString &dictPath,
                                          const QString &resolvedModelPath) const
{
  const QString explicitPath =
      resolveFilePath(dictPath, QStringList{QStringLiteral("dict.txt"),
                                            QStringLiteral("*.txt")});
  if (!explicitPath.isEmpty())
  {
    return explicitPath;
  }

  const QFileInfo modelInfo(resolvedModelPath);
  if (!resolvedModelPath.isEmpty() && modelInfo.exists())
  {
    const QString modelDirPath = modelInfo.absoluteDir().absolutePath();
    const QString nearModel =
        resolveFilePath(modelDirPath, QStringList{QStringLiteral("dict.txt"),
                                                  QStringLiteral("*.txt")});
    if (!nearModel.isEmpty())
    {
      return nearModel;
    }
  }

  const QString downloadsDir = QDir::home().filePath(QStringLiteral("Downloads"));
  return resolveFilePath(downloadsDir, QStringList{QStringLiteral("dict.txt"),
                                                   QStringLiteral("*.txt")});
}

bool OcrManager::init(const QString &modelPath, const QString &dictPath,
                      const int inputWidth, const int inputHeight)
{
  m_inputWidth = (inputWidth > 0) ? inputWidth : kDefaultInputWidth;
  m_inputHeight = (inputHeight > 0) ? inputHeight : kDefaultInputHeight;

  const QString resolvedModelPath = resolveModelPath(modelPath);
  const QString resolvedDictPath =
      resolveDictionaryPath(dictPath, resolvedModelPath);

  qDebug() << "[OCR] Initializing PaddleOCR. model=" << resolvedModelPath
           << "dict="
           << (resolvedDictPath.isEmpty() ? QStringLiteral("<builtin>")
                                          : resolvedDictPath)
           << "input=" << m_inputWidth << "x" << m_inputHeight;

  if (resolvedModelPath.isEmpty())
  {
    qDebug() << "[OCR] Failed to resolve ONNX model path from:" << modelPath;
    return false;
  }

  const QFileInfo modelInfo(resolvedModelPath);
  if (!modelInfo.exists() || !modelInfo.isFile())
  {
    qDebug() << "[OCR] ONNX model file not found:" << resolvedModelPath;
    return false;
  }

  if (resolvedDictPath.isEmpty() && !dictPath.trimmed().isEmpty())
  {
    qDebug() << "[OCR] Dictionary path could not be resolved, using built-in fallback:"
             << dictPath;
  }

  QString error;
  if (!m_runner.init(resolvedModelPath, resolvedDictPath, m_inputWidth,
                     m_inputHeight, &error))
  {
    qDebug() << "Could not initialize PaddleOCR:" << error;
    return false;
  }

  return true;
}

OcrResult OcrManager::performOcrDetailed(const QImage &image, const int objectId)
{
  OcrResult out;

  ocr::preprocess::PlatePreprocessResult preprocessed;
  if (!ocr::preprocess::preprocessPlateImage(image, m_inputWidth, m_inputHeight,
                                             &preprocessed))
  {
    out.dropReason = preprocessed.dropReason.isEmpty()
                         ? QStringLiteral("preprocess failed")
                         : preprocessed.dropReason;
    return out;
  }

  ocr::debug::dumpOcrStages(preprocessed.roiRgb, preprocessed.normalizedRgb,
                            preprocessed.enhancedGray, preprocessed.ocrInputRgb,
                            objectId);

  if (!m_runner.isReady())
  {
    out.dropReason = QStringLiteral("ocr runner not initialized");
    return out;
  }

  const ocr::postprocess::OcrCandidate candidate =
      m_runner.runSingleCandidate(preprocessed.ocrInputRgb);
  const OcrResult result = ocr::postprocess::chooseBestPlateResult(candidate);
  qDebug() << "[OCR][Final] raw=" << result.selectedRawText
           << "text=" << result.text
           << "selected=" << result.selectedCandidate
           << "score=" << result.selectedScore
           << "confidence=" << result.selectedConfidence
           << "dropReason=" << result.dropReason;
  return result;
}

QString OcrManager::performOcr(const QImage &image, const int objectId)
{
  return performOcrDetailed(image, objectId).text;
}
