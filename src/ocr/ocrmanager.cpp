#include "ocrmanager.h"
#include "config/config.h"
#include "ocr/debug/ocrdebugdumper.h"
#include "ocr/postprocess/platepostprocessor.h"
#include "ocr/preprocess/platepreprocessor.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <mutex>
#include <unordered_map>


namespace {
constexpr int kDefaultInputWidth = 320;
constexpr int kDefaultInputHeight = 48;
constexpr int kRuntimeLogInterval = 10;

bool looksLikeWindowsAbsolutePath(const QString &path) {
  return path.size() >= 3 && path.at(0).isLetter() && path.at(1) == u':' &&
         (path.at(2) == u'/' || path.at(2) == u'\\');
}

QString convertWindowsPathToWslMount(const QString &path) {
#ifdef Q_OS_UNIX
  if (!looksLikeWindowsAbsolutePath(path)) {
    return QString();
  }

  QString normalized = QDir::fromNativeSeparators(path);
  QString suffix = normalized.mid(2);
  if (!suffix.startsWith(u'/')) {
    suffix.prepend(u'/');
  }
  return QStringLiteral("/mnt/%1%2")
      .arg(QString(path.at(0).toLower()))
      .arg(QDir::cleanPath(suffix));
#else
  Q_UNUSED(path);
  return QString();
#endif
}

QStringList lookupRoots() {
  QStringList roots;
  roots << QDir::currentPath();

  const QString appDir = QCoreApplication::applicationDirPath();
  if (!appDir.isEmpty()) {
    roots << appDir << QDir(appDir).absoluteFilePath(QStringLiteral(".."))
          << QDir(appDir).absoluteFilePath(QStringLiteral("../.."));
  }

#ifdef PROJECT_SOURCE_DIR
  roots << QStringLiteral(PROJECT_SOURCE_DIR);
#endif

  for (QString &root : roots) {
    root = QDir::cleanPath(root);
  }
  roots.removeDuplicates();
  return roots;
}

QStringList candidateLookupPaths(const QString &pathOrDir) {
  QStringList candidates;
  const QString trimmed = QDir::fromNativeSeparators(pathOrDir.trimmed());
  if (trimmed.isEmpty()) {
    return candidates;
  }

  candidates << QDir::cleanPath(trimmed);

  const QString wslMountPath = convertWindowsPathToWslMount(trimmed);
  if (!wslMountPath.isEmpty()) {
    candidates << wslMountPath;
  }

  const QFileInfo rawInfo(trimmed);
  if (rawInfo.isRelative()) {
    const QStringList roots = lookupRoots();
    for (const QString &root : roots) {
      candidates << QDir(root).absoluteFilePath(trimmed);
    }
  }

  for (QString &candidate : candidates) {
    candidate = QDir::cleanPath(candidate);
  }
  candidates.removeDuplicates();
  return candidates;
}

QStringList defaultOcrSearchDirs() {
  QStringList searchDirs;
  const QStringList roots = lookupRoots();
  for (const QString &root : roots) {
    searchDirs << root << QDir(root).absoluteFilePath(QStringLiteral("models"))
               << QDir(root).absoluteFilePath(QStringLiteral("models/korean"))
               << QDir(root).absoluteFilePath(QStringLiteral("model"));
  }

  searchDirs << QDir::home().filePath(QStringLiteral("Downloads"));
  for (QString &dir : searchDirs) {
    dir = QDir::cleanPath(dir);
  }
  searchDirs.removeDuplicates();
  return searchDirs;
}

bool shouldLogRuntimeOcrMessage(const int objectId) {
  static std::mutex mutex;
  static std::unordered_map<int, int> countsByObject;

  std::lock_guard<std::mutex> lock(mutex);
  int &count = countsByObject[objectId];
  ++count;
  if (count < kRuntimeLogInterval) {
    return false;
  }

  count = 0;
  return true;
}
} // namespace

OcrManager::OcrManager() = default;

OcrManager::~OcrManager() {
  delete m_llmRunner;
}

QString OcrManager::findFirstFileRecursively(const QString &rootPath,
                                             const QStringList &filters) {
  const QString trimmedRoot = rootPath.trimmed();
  if (trimmedRoot.isEmpty() || filters.isEmpty()) {
    return QString();
  }

  const QFileInfo rootInfo(trimmedRoot);
  if (!rootInfo.exists() || !rootInfo.isDir()) {
    return QString();
  }

  for (const QString &filter : filters) {
    QDirIterator it(rootInfo.absoluteFilePath(), QStringList(filter),
                    QDir::Files, QDirIterator::Subdirectories);
    if (it.hasNext()) {
      return QFileInfo(it.next()).absoluteFilePath();
    }
  }

  return QString();
}

QString OcrManager::resolveFilePath(const QString &pathOrDir,
                                    const QStringList &filters) {
  const QString trimmed = pathOrDir.trimmed();
  if (trimmed.isEmpty()) {
    return QString();
  }

  const QStringList candidates = candidateLookupPaths(trimmed);
  for (const QString &candidate : candidates) {
    const QFileInfo info(candidate);
    if (info.exists() && info.isFile()) {
      return info.absoluteFilePath();
    }

    if (info.exists() && info.isDir()) {
      const QString found =
          findFirstFileRecursively(info.absoluteFilePath(), filters);
      if (!found.isEmpty()) {
        return found;
      }
    }
  }

  return QString();
}

QString OcrManager::resolveModelPath(const QString &modelPath) const {
  const QString explicitPath =
      resolveFilePath(modelPath, QStringList{QStringLiteral("*.onnx")});
  if (!explicitPath.isEmpty()) {
    return explicitPath;
  }

  const QStringList searchDirs = defaultOcrSearchDirs();
  for (const QString &searchDir : searchDirs) {
    const QString found =
        resolveFilePath(searchDir, QStringList{QStringLiteral("*.onnx")});
    if (!found.isEmpty()) {
      return found;
    }
  }

  return QString();
}

QString
OcrManager::resolveDictionaryPath(const QString &dictPath,
                                  const QString &resolvedModelPath) const {
  const QString explicitPath =
      resolveFilePath(dictPath, QStringList{QStringLiteral("dict.txt"),
                                            QStringLiteral("*.txt")});
  if (!explicitPath.isEmpty()) {
    return explicitPath;
  }

  const QFileInfo modelInfo(resolvedModelPath);
  if (!resolvedModelPath.isEmpty() && modelInfo.exists()) {
    const QString modelDirPath = modelInfo.absoluteDir().absolutePath();
    const QString nearModel =
        resolveFilePath(modelDirPath, QStringList{QStringLiteral("dict.txt"),
                                                  QStringLiteral("*.txt")});
    if (!nearModel.isEmpty()) {
      return nearModel;
    }
  }

  const QStringList searchDirs = defaultOcrSearchDirs();
  for (const QString &searchDir : searchDirs) {
    const QString found = resolveFilePath(
        searchDir, QStringList{QStringLiteral("dict.txt"),
                               QStringLiteral("*.txt")});
    if (!found.isEmpty()) {
      return found;
    }
  }

  return QString();
}

bool OcrManager::init(const QString &modelPath, const QString &dictPath,
                      const int inputWidth, const int inputHeight) {
  const QString type = Config::instance().ocrType();
  if (type == "LLM") {
    if (!m_llmRunner) {
      m_llmRunner = new ocr::recognition::LlmOcrRunner();
    }
    qDebug() << "[OCR] Initialized with LLM mode (Gemini)";
    return true;
  }

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

  if (resolvedModelPath.isEmpty()) {
    qDebug() << "[OCR] Failed to resolve ONNX model path from:" << modelPath;
    return false;
  }

  const QFileInfo modelInfo(resolvedModelPath);
  if (!modelInfo.exists() || !modelInfo.isFile()) {
    qDebug() << "[OCR] ONNX model file not found:" << resolvedModelPath;
    return false;
  }

  if (resolvedDictPath.isEmpty() && !dictPath.trimmed().isEmpty()) {
    qDebug() << "[OCR] Dictionary path could not be resolved, using built-in "
                "fallback:"
             << dictPath;
  }

  QString error;
  if (!m_runner.init(resolvedModelPath, resolvedDictPath, m_inputWidth,
                     m_inputHeight, &error)) {
    qDebug() << "Could not initialize PaddleOCR:" << error;
    return false;
  }

  return true;
}

OcrResult OcrManager::performOcrDetailed(const QImage &image,
                                         const int objectId) {
  OcrResult out;

  const QString type = Config::instance().ocrType();
  if (type == "LLM" && m_llmRunner) {
    // LLM 모드: 전처리 없이 원본 크롭(AABB+padding) 이미지 그대로 전달
    return m_llmRunner->runSingleCandidate(image, objectId);
  }

  ocr::preprocess::PlatePreprocessResult preprocessed;
  if (!ocr::preprocess::preprocessPlateImage(image, m_inputWidth, m_inputHeight,
                                             &preprocessed)) {
    return out;
  }

  ocr::debug::dumpOcrStages(preprocessed.roiRgb, preprocessed.normalizedRgb,
                            preprocessed.enhancedGray, preprocessed.ocrInputRgb,
                            objectId);

  if (!m_runner.isReady()) {
    return out;
  }

  const ocr::postprocess::OcrCandidate candidate =
      m_runner.runSingleCandidate(preprocessed.ocrInputRgb);
  const OcrResult result = ocr::postprocess::chooseBestPlateResult(candidate);
  if (shouldLogRuntimeOcrMessage(objectId)) {
    qDebug() << "[OCR][Final] objectId=" << objectId
             << "raw=" << result.selectedRawText << "text=" << result.text
             << "selected=" << result.selectedCandidate
             << "score=" << result.selectedScore
             << "confidence=" << result.selectedConfidence;
  }
  return result;
}

OcrFullResult OcrManager::performOcr(const QImage &image, const int objectId) {
  QElapsedTimer timer;
  timer.start();

  OcrResult res = performOcrDetailed(image, objectId);

  OcrFullResult out;
  out.raw = res.selectedRawText;
  out.filtered = res.text;
  out.latencyMs = static_cast<int>(timer.elapsed());

  return out;
}
