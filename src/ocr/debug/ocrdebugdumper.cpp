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
    // 디버그 저장은 매 호출마다 하지 않고 주기적으로만 수행해 I/O 부담을 줄인다.
    constexpr int kDebugSaveInterval = 10;
    // 단계별 이미지 파일은 최신 N개만 유지한다.
    constexpr int kDebugKeepFilesPerStage = 10;
    // 기본 디버그 출력 폴더명.
    constexpr char kDebugDirName[] = "ocr_debug";

    QDir ensureDebugDir()
    {
      // OCR_DEBUG_DIR 환경변수가 있으면 해당 경로를, 없으면 현재 작업 경로를 기준으로 사용한다.
      const QString envDir = qEnvironmentVariable("OCR_DEBUG_DIR").trimmed();
      const QString basePath = envDir.isEmpty() ? QDir::currentPath() : envDir;
      const QString debugDirName = QString::fromLatin1(kDebugDirName);

      QDir rootDir(basePath);
      if (!rootDir.exists())
      {
        rootDir.mkpath(".");
      }

      // basePath가 이미 ocr_debug면 그대로 쓰고, 아니면 하위에 ocr_debug를 만든다.
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

      // 오래된 파일부터 삭제해 단계별로 keepCount개만 남긴다.
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
      // Qt 문자열 경로를 imwrite에 전달 가능한 로컬 바이트 문자열로 변환한다.
      return cv::imwrite(QFile::encodeName(filePath).constData(), mat);
    }

  } // namespace

  void dumpOcrStages(const cv::Mat &roiRgb, const cv::Mat &gray,
                     const cv::Mat &rectifiedRgb, const cv::Mat &quadDebugRgb,
                     const cv::Mat &binary, const cv::Mat &binaryInv)
  {
    static std::atomic<int> s_callCounter{0};
    static std::atomic<bool> s_loggedPath{false};

    // 멀티스레드 환경에서도 안전하게 호출 횟수를 증가시킨다.
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

    // OpenCV imwrite는 컬러를 BGR 순서로 다루므로 RGB 입력을 변환해서 저장한다.
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
    // 일부 단계는 비활성화될 수 있어 빈 Mat일 때는 저장을 건너뛴다.
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

    // 단계별 파일 개수를 정리해 디버그 폴더가 무한정 커지지 않게 유지한다.
    rotateStageFiles(debugDir, "roi", kDebugKeepFilesPerStage);
    rotateStageFiles(debugDir, "gray", kDebugKeepFilesPerStage);
    rotateStageFiles(debugDir, "rectified", kDebugKeepFilesPerStage);
    rotateStageFiles(debugDir, "quad_debug", kDebugKeepFilesPerStage);
    rotateStageFiles(debugDir, "binary", kDebugKeepFilesPerStage);
    rotateStageFiles(debugDir, "binary_inv", kDebugKeepFilesPerStage);
  }

} // namespace ocr::debug
