#include "ocrmanager.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QRegularExpression>

OcrManager::OcrManager() : m_tessApi(nullptr) {
  m_tessApi = new tesseract::TessBaseAPI();
}

OcrManager::~OcrManager() {
  if (m_tessApi) {
    m_tessApi->End();
    delete m_tessApi;
  }
}

bool OcrManager::init(const QString &datapath, const QString &language) {
  // 데이터 경로 결정 순서:
  // 1. 매개변수로 전달된 경로
  // 2. TESSDATA_PREFIX 환경 변수
  // 3. vcpkg 기본 경로
  QString tessdataDir = datapath;

  if (tessdataDir.isEmpty()) {
    tessdataDir = qEnvironmentVariable("TESSDATA_PREFIX");
    if (tessdataDir.isEmpty()) {
      tessdataDir = "C:/vcpkg/installed/x64-windows/share/tessdata/";
    }
  }

  // Tesseract API에 전달하기 위해 QByteArray 유지
  QByteArray pathBytes = tessdataDir.toUtf8();
  QByteArray langBytes = language.toUtf8();

  // 데이터 경로 유효성 검사 (디버깅용)
  qDebug() << "Initializing Tesseract with DataPath:" << tessdataDir
           << "Lang:" << language;

  // 언어 데이터 파일 존재 여부 확인 로직은 tessApi->Init 내부에서 처리됨
  // 하지만 "eng.traineddata"가 없으면 실패하므로 사용자에게 안내 필요

  if (m_tessApi->Init(pathBytes.constData(), langBytes.constData())) {
    qDebug() << "Could not initialize tesseract. Path:" << tessdataDir
             << "Lang:" << language;
    return false;
  }

  // 화이트리스트 적용: 숫자 + 한국 자동차 번호판에 사용되는 표준 한글
  // 0-9,
  // 가나다라마거너더러머버서어저고노도로모보소오조구누두루무부수우주아바사자하허호
  // NOTE: Keep the whitelist as a single literal to avoid accidental newlines.
  static const char kWhitelist[] = "0123456789"
                                   "가나다라마거너더러머버서어저고노도로모보소"
                                   "오조구누두루무부수우주아바사자하허호";
  m_tessApi->SetVariable("tessedit_char_whitelist", kWhitelist);

  return true;
}

QString OcrManager::performOcr(const QImage &image) {
  if (image.isNull())
    return QString();

  // 1. QImage -> cv::Mat 변환 (Format_RGB888 강제 변환)
  // 입력 이미지가 RGB888이 아닐 경우(예: RGB32) CV_8UC3와 호환되지 않아 메모리
  // 참조 오류 발생 가능
  QImage formattedImage = image.convertToFormat(QImage::Format_RGB888);

  cv::Mat matRoi(formattedImage.height(), formattedImage.width(), CV_8UC3,
                 (uchar *)formattedImage.bits(), formattedImage.bytesPerLine());

  // 2. 전처리 (RGB -> Gray)
  cv::Mat gray;
  cv::cvtColor(matRoi, gray, cv::COLOR_RGB2GRAY);

  // === 투영 변환 (Perspective Transform) 로직 ===
  cv::Mat warped = gray.clone();
  bool isWarped = false;

  // 1. 유저 제안: 그레이스케일 -> 캐니 엣지 연산 -> 윤곽선(Contours)
  cv::Mat blurred, canned;
  cv::GaussianBlur(gray, blurred, cv::Size(5, 5), 0);
  cv::Canny(blurred, canned, 50, 150);

  // 끊어진 에지 연결
  cv::dilate(canned, canned, cv::Mat(), cv::Point(-1, -1), 1);

  std::vector<std::vector<cv::Point>> contours;
  cv::findContours(canned, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

  qDebug() << "[OCR] Found" << contours.size() << "total contours.";

  // 2. 최대 윤곽선 추출 (번호판 테두리 찾기)
  // 모든 점을 합치지 않고, 면적이 가장 큰 단일 윤곽선(번호판 외곽선)을 타겟으로
  // 함.
  std::vector<cv::Point> plateContour;
  double maxArea = 0;
  for (const auto &cnt : contours) {
    double area = cv::contourArea(cnt);
    if (area > maxArea) {
      maxArea = area;
      plateContour = cnt;
    }
  }

  if (!plateContour.empty() && maxArea > 500) { // 최소 면적 체크
    // 3. 실제 사각형의 4개 꼭짓점 추출
    // minAreaRect(직사각형) 대신, 실제 찌그러진 번호판의 4개 모서리를 직접
    // 찾습니다.
    std::vector<cv::Point2f> srcPoints(4);

    // 다각형 근사를 통해 4개 점 추출 시도
    std::vector<cv::Point> approx;
    double peri = cv::arcLength(plateContour, true);
    cv::approxPolyDP(plateContour, approx, 0.02 * peri, true);

    if (approx.size() == 4) {
      // 근사 결과가 4개라면 그 포인트를 그대로 사용
      for (int i = 0; i < 4; ++i)
        plateContour[i] = approx[i]; // 임시 저장
    } else {
      // 4개가 아니라면 윤곽선 내에서 합/차가 최대/최소인 4개 극점을 직접 추출
      std::vector<cv::Point> corners(4);
      int tl_idx = 0, br_idx = 0, tr_idx = 0, bl_idx = 0;
      float minSum = (float)plateContour[0].x + plateContour[0].y;
      float maxSum = (float)plateContour[0].x + plateContour[0].y;
      float minDiff = (float)plateContour[0].x - plateContour[0].y;
      float maxDiff = (float)plateContour[0].x - plateContour[0].y;

      for (int i = 0; i < plateContour.size(); ++i) {
        float sum = (float)plateContour[i].x + plateContour[i].y;
        float diff = (float)plateContour[i].x - plateContour[i].y;
        if (sum < minSum) {
          minSum = sum;
          tl_idx = i;
        }
        if (sum > maxSum) {
          maxSum = sum;
          br_idx = i;
        }
        if (diff < minDiff) {
          minDiff = diff;
          bl_idx = i;
        }
        if (diff > maxDiff) {
          maxDiff = diff;
          tr_idx = i;
        }
      }

      // 순서 보장을 위해 임시 벡터에 저장
      std::vector<cv::Point> sortedPlate;
      sortedPlate.push_back(plateContour[tl_idx]); // TL
      sortedPlate.push_back(plateContour[tr_idx]); // TR
      sortedPlate.push_back(plateContour[br_idx]); // BR
      sortedPlate.push_back(plateContour[bl_idx]); // BL
      plateContour = sortedPlate;
    }

    // 최종 srcPoints 설정 (TL, TR, BR, BL 순서)
    for (int i = 0; i < 4; ++i)
      srcPoints[i] = plateContour[i];

    // === 기하학적 유효성 검사 (안전 장치 추가) ===
    // 1. 회전 각도 검사: 상단 변(TL-TR)의 기울기가 45도를 넘으면 비정상으로
    // 간주
    double dx = srcPoints[1].x - srcPoints[0].x;
    double dy = srcPoints[1].y - srcPoints[0].y;
    double angle = std::abs(std::atan2(dy, dx) * 180.0 / 3.14159265);

    // 2. 볼록성 및 최소 크기 검사
    std::vector<cv::Point> srcIntPoints;
    for (const auto &p : srcPoints)
      srcIntPoints.push_back(cv::Point(p.x, p.y));
    bool isConvex = cv::isContourConvex(srcIntPoints);
    double srcArea = cv::contourArea(srcIntPoints);

    if (angle > 45.0 || !isConvex || srcArea < 400) {
      qDebug() << "[OCR] Warping skipped: Invalid geometry. Angle:" << angle
               << "Convex:" << isConvex << "Area:" << srcArea;
    } else {
      qDebug() << "[OCR] Extracted 4 precise corners. Angle:" << angle
               << "Area:" << maxArea;

      // 목적지 좌표 (Tesseract 최적 해상도 반영: 높이 약 40px)
      int targetW = 189, targetH = 40;
      std::vector<cv::Point2f> dstPoints = {{0, 0},
                                            {(float)targetW, 0},
                                            {(float)targetW, (float)targetH},
                                            {0, (float)targetH}};

      cv::Mat M = cv::getPerspectiveTransform(srcPoints, dstPoints);
      cv::warpPerspective(gray, warped, M, cv::Size(targetW, targetH));
      isWarped = true;
    }
  }

  // 3. 이진화 (워핑된 이미지 혹은 원본 그레이 사용)
  cv::Mat binary;
  cv::threshold(warped, binary, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);

  // 4. OCR 수행
  m_tessApi->SetImage(binary.data, binary.cols, binary.rows, 1, binary.step);

  char *outText = m_tessApi->GetUTF8Text();
  QString result = QString::fromUtf8(outText).trimmed();
  delete[] outText;

  // === 번호판 규격 정규식 필터링 (공백 제거 후 추출) ===
  // 1. 모든 공백 제거 (Regex용 임시 문자열)
  QString cleaned = result;
  cleaned =
      cleaned.replace(" ", "").replace("\t", "").replace("\n", "").replace("\r",
                                                                           "");

  // 2. 정규표현식 매칭 (숫자 2~3자리 + 한글 + 숫자 4자리)
  // C++ std::regex는 UTF-8(한글) 처리에 취약할 수 있으므로, Qt의
  // QRegularExpression을 사용합니다.
  QRegularExpression plateRe(QStringLiteral("\\d{2,3}[가-힣]+\\d{4}"));
  QRegularExpressionMatch reMatch = plateRe.match(cleaned);

  if (reMatch.hasMatch()) {
    QString finalPlate = reMatch.captured(0);
    qDebug() << "[OCR] Regex Match Success:" << result << "->" << finalPlate;
    result = finalPlate; // 규격에 맞는 부분만 최종 결과로 채택
  } else {
    qDebug() << "[OCR] Regex Match Failed. Tesseract saw:" << cleaned
             << " (Original was:" << result << ")";
    result = ""; // 규격에 맞지 않으면 빈 문자열 반환
  }

  // === 디버그 이미지 저장 로직 (워핑 성공 시에만 저장) ===
  if (isWarped) {
    static bool debugDirChecked = false;
    QString debugPath = QCoreApplication::applicationDirPath() + "/debug_ocr";
    if (!debugDirChecked) {
      QDir dir(debugPath);
      if (!dir.exists()) {
        dir.mkpath(".");
      }
      debugDirChecked = true;
    }

    QString timestamp =
        QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz");

    // 1. 원본 크롭 저장
    formattedImage.save(
        QString("%1/%2_1_original.png").arg(debugPath).arg(timestamp));

    // 2. 그레이스케일 저장
    cv::imwrite(QString("%1/%2_2a_gray.png")
                    .arg(debugPath)
                    .arg(timestamp)
                    .toLocal8Bit()
                    .toStdString(),
                gray);

    // 3. 투영 변환 결과 저장
    cv::imwrite(QString("%1/%2_3_warped.png")
                    .arg(debugPath)
                    .arg(timestamp)
                    .toLocal8Bit()
                    .toStdString(),
                warped);

    // 2b. 에지 이미지 저장 (유저 제안 기반 디버깅용)
    cv::imwrite(QString("%1/%2_2b_canned.png")
                    .arg(debugPath)
                    .arg(timestamp)
                    .toLocal8Bit()
                    .toStdString(),
                canned);

    // 4. 이진화 이미지 저장
    // 인식 결과(result)가 있으면 파일명에 포함, 없으면 FAIL 표시
    QString plateTag = result.isEmpty() ? "FAIL" : result;
    // 파일명에 부적절한 문자 제거
    plateTag.replace("/", "_").replace("\\", "_").replace(":", "_");

    QString finalPath = QString("%1/%2_4_binary_%3.png")
                            .arg(debugPath)
                            .arg(timestamp)
                            .arg(plateTag);
    cv::imwrite(finalPath.toLocal8Bit().toStdString(), binary);

    qDebug() << "[OCR Debug] Images saved to:" << debugPath
             << "prefix:" << timestamp << "Result:" << plateTag;
  }

  return result;
}
