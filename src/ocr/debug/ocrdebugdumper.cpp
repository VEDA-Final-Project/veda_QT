#include "ocr/debug/ocrdebugdumper.h"
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfoList>
#include <QStringList>
#include <QtGlobal>
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

    void rotateStageFiles(const QDir &dir, const QString &prefix, int keepCount)
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
        if (!QFile::remove(files[i].absoluteFilePath()))
        {
          qWarning() << "[OCR][Debug] Failed to remove old file:"
                     << files[i].absoluteFilePath();
        }
      }
    }

    bool savePng(const QString &filePath, const cv::Mat &mat)
    {
      return cv::imwrite(QFile::encodeName(filePath).constData(), mat);
    }

  } // namespace

  void dumpOcrStages(const cv::Mat &roiRgb, const cv::Mat &gray,
                     const cv::Mat &rectifiedRgb, const cv::Mat &quadDebugRgb,
                     const cv::Mat &binary, const cv::Mat &binaryInv)
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
      qDebug() << "[OCR][Debug] stage images saved under:"
               << debugDir.absolutePath();
    }

    const QString stamp =
        QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz");
    const QString seq = QString("%1").arg(callCount, 6, 10, QChar('0'));

    cv::Mat roiBgr;
    cv::cvtColor(roiRgb, roiBgr, cv::COLOR_RGB2BGR);

    const QString roiPath = debugDir.filePath(QString("roi_%1_%2.png").arg(stamp, seq));
    const QString grayPath = debugDir.filePath(QString("gray_%1_%2.png").arg(stamp, seq));
    const QString rectifiedPath =
        debugDir.filePath(QString("rectified_%1_%2.png").arg(stamp, seq));
    const QString quadDebugPath =
        debugDir.filePath(QString("quad_debug_%1_%2.png").arg(stamp, seq));
    const QString binaryPath =
        debugDir.filePath(QString("binary_%1_%2.png").arg(stamp, seq));
    const QString binaryInvPath =
        debugDir.filePath(QString("binary_inv_%1_%2.png").arg(stamp, seq));

    cv::Mat rectifiedBgr;
    if (!rectifiedRgb.empty())
    {
      cv::cvtColor(rectifiedRgb, rectifiedBgr, cv::COLOR_RGB2BGR);
    }

    cv::Mat quadDebugBgr;
    if (!quadDebugRgb.empty())
    {
      cv::cvtColor(quadDebugRgb, quadDebugBgr, cv::COLOR_RGB2BGR);
    }

    if (!savePng(roiPath, roiBgr))
    {
      qWarning() << "[OCR][Debug] Failed to save:" << roiPath;
    }
    if (!savePng(grayPath, gray))
    {
      qWarning() << "[OCR][Debug] Failed to save:" << grayPath;
    }
    if (!rectifiedBgr.empty() && !savePng(rectifiedPath, rectifiedBgr))
    {
      qWarning() << "[OCR][Debug] Failed to save:" << rectifiedPath;
    }
    if (!quadDebugBgr.empty() && !savePng(quadDebugPath, quadDebugBgr))
    {
      qWarning() << "[OCR][Debug] Failed to save:" << quadDebugPath;
    }
    if (!savePng(binaryPath, binary))
    {
      qWarning() << "[OCR][Debug] Failed to save:" << binaryPath;
    }
    if (!savePng(binaryInvPath, binaryInv))
    {
      qWarning() << "[OCR][Debug] Failed to save:" << binaryInvPath;
    }

    rotateStageFiles(debugDir, "roi", kDebugKeepFilesPerStage);
    rotateStageFiles(debugDir, "gray", kDebugKeepFilesPerStage);
    rotateStageFiles(debugDir, "rectified", kDebugKeepFilesPerStage);
    rotateStageFiles(debugDir, "quad_debug", kDebugKeepFilesPerStage);
    rotateStageFiles(debugDir, "binary", kDebugKeepFilesPerStage);
    rotateStageFiles(debugDir, "binary_inv", kDebugKeepFilesPerStage);
  }

} // namespace ocr::debug
