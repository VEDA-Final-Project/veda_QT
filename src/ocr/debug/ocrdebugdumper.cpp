#include "ocr/debug/ocrdebugdumper.h"
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfoList>
#include <QStringList>
#include <algorithm>
#include <atomic>

namespace ocr::debug
{
namespace
{
constexpr int kDebugSaveInterval = 10;
constexpr int kDebugKeepFilesPerStage = 10;
constexpr char kDebugDirName[] = "ocr_debug";

QDir ensureDebugDir()
{
  const QString envDir = qEnvironmentVariable("OCR_DEBUG_DIR").trimmed();
  const QString basePath = envDir.isEmpty() ? QDir::currentPath() : envDir;
  const QString debugDirName = QString::fromLatin1(kDebugDirName);

  QDir rootDir(basePath);
  if (!rootDir.exists())
  {
    rootDir.mkpath(".");
  }

  const QString debugDirPath = (rootDir.dirName() == debugDirName)
                                   ? rootDir.absolutePath()
                                   : rootDir.filePath(debugDirName);
  QDir(debugDirPath).mkpath(".");
  return QDir(debugDirPath);
}

void rotateStageFiles(const QDir &dir, const QString &prefix, const int keepCount)
{
  if (keepCount < 0)
  {
    return;
  }

  const QStringList filters = {QString("%1_*.png").arg(prefix)};
  const QFileInfoList files =
      dir.entryInfoList(filters, QDir::Files, QDir::Time | QDir::Reversed);

  const int removeCount = files.size() - keepCount;
  for (int i = 0; i < removeCount; ++i)
  {
    QFile::remove(files[i].absoluteFilePath());
  }
}

QString sanitizeTag(const QString &tag)
{
  QString out;
  out.reserve(tag.size());
  for (const QChar ch : tag)
  {
    out.append(ch.isLetterOrNumber() ? ch.toLower() : QChar('_'));
  }
  while (out.contains(QStringLiteral("__")))
  {
    out.replace(QStringLiteral("__"), QStringLiteral("_"));
  }
  return out.isEmpty() ? QStringLiteral("variant") : out;
}

bool savePng(const QString &filePath, const cv::Mat &mat)
{
  return cv::imwrite(QFile::encodeName(filePath).constData(), mat);
}

bool saveRgbPng(const QString &filePath, const cv::Mat &rgb)
{
  if (rgb.empty())
  {
    return false;
  }

  cv::Mat bgr;
  cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);
  return savePng(filePath, bgr);
}

} // namespace

void dumpOcrStages(const cv::Mat &roiRgb, const cv::Mat &normalizedRgb,
                   const cv::Mat &enhancedGray, const cv::Mat &ocrInputRgb,
                   const int objectId)
{
  static std::atomic<int> s_callCounter{0};
  static std::atomic<bool> s_loggedPath{false};

  const int callCount = ++s_callCounter;
  if ((callCount % kDebugSaveInterval) != 0)
  {
    return;
  }

  const QDir debugDir = ensureDebugDir();
  if (!s_loggedPath.exchange(true))
  {
    qDebug() << "[OCR][Debug] stage images saved under:" << debugDir.absolutePath();
  }

  const QString stamp =
      QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz");
  const QString seq = QString("%1").arg(callCount, 6, 10, QChar('0'));
  const QString objectPrefix =
      (objectId >= 0) ? QString("obj_%1_").arg(objectId) : QString();

  if (!roiRgb.empty())
  {
    const QString path = debugDir.filePath(
        QString("roi_%1%2_%3.png").arg(objectPrefix, stamp, seq));
    saveRgbPng(path, roiRgb);
    rotateStageFiles(debugDir, QStringLiteral("roi"), kDebugKeepFilesPerStage);
  }

  if (!normalizedRgb.empty())
  {
    const QString path =
        debugDir.filePath(
            QString("normalized_%1%2_%3.png").arg(objectPrefix, stamp, seq));
    saveRgbPng(path, normalizedRgb);
    rotateStageFiles(debugDir, QStringLiteral("normalized"),
                     kDebugKeepFilesPerStage);
  }

  if (!enhancedGray.empty())
  {
    const QString path =
        debugDir.filePath(
            QString("enhanced_%1%2_%3.png").arg(objectPrefix, stamp, seq));
    savePng(path, enhancedGray);
    rotateStageFiles(debugDir, QStringLiteral("enhanced"),
                     kDebugKeepFilesPerStage);
  }

  if (!ocrInputRgb.empty())
  {
    const QString prefix =
        (objectId >= 0) ? QString("ocr_input_obj_%1").arg(objectId)
                        : QStringLiteral("ocr_input");
    const QString path =
        debugDir.filePath(QString("%1_%2_%3.png").arg(prefix, stamp, seq));
    saveRgbPng(path, ocrInputRgb);
    rotateStageFiles(debugDir, prefix, kDebugKeepFilesPerStage);
  }
}

} // namespace ocr::debug
