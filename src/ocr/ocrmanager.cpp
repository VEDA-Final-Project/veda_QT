#include "ocrmanager.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QPainter>
#include <QRegularExpression>
#include <algorithm>

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

    // 다각형 근사 또는 극점 추출을 통해 4개 후보점 확보
    std::vector<cv::Point> candidates;
    std::vector<cv::Point> approx;
    double peri = cv::arcLength(plateContour, true);
    cv::approxPolyDP(plateContour, approx, 0.02 * peri, true);

    if (approx.size() == 4) {
      for (const auto &p : approx)
        candidates.push_back(p);
    } else {
      cv::Point tl_p = plateContour[0], br_p = plateContour[0],
                tr_p = plateContour[0], bl_p = plateContour[0];
      float minSum = (float)plateContour[0].x + plateContour[0].y;
      float maxSum = (float)plateContour[0].x + plateContour[0].y;
      float minDiff = (float)plateContour[0].x - plateContour[0].y;
      float maxDiff = (float)plateContour[0].x - plateContour[0].y;

      for (const auto &p : plateContour) {
        float sum = (float)p.x + p.y;
        float diff = (float)p.x - p.y;
        if (sum < minSum) {
          minSum = sum;
          tl_p = p;
        }
        if (sum > maxSum) {
          maxSum = sum;
          br_p = p;
        }
        if (diff < minDiff) {
          minDiff = diff;
          bl_p = p;
        }
        if (diff > maxDiff) {
          maxDiff = diff;
          tr_p = p;
        }
      }
      candidates = {tl_p, tr_p, br_p, bl_p};
    }

    // === 4개 점을 TL, TR, BR, BL 순서로 강제 정렬 (뒤집힘 방지) ===
    // 1. Y축 기준 정렬 (위쪽 2개, 아래쪽 2개 분리)
    std::sort(candidates.begin(), candidates.end(),
              [](const cv::Point &a, const cv::Point &b) { return a.y < b.y; });

    // 2. 상단 2개 중 X가 작은게 TL, 큰게 TR
    if (candidates[0].x > candidates[1].x)
      std::swap(candidates[0], candidates[1]);
    // 3. 하단 2개 중 X가 큰게 BR, 작은게 BL
    if (candidates[2].x < candidates[3].x)
      std::swap(candidates[2], candidates[3]);

    // 최종 srcPoints 설정 (TL, TR, BR, BL)
    srcPoints[0] = candidates[0];
    srcPoints[1] = candidates[1];
    srcPoints[2] = candidates[2];
    srcPoints[3] = candidates[3];

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

  // === 3.5. [Fast-Path] 전체 이미지를 한 줄로 우선 인식 (Line-First Strategy)
  // ===
  m_tessApi->SetPageSegMode(tesseract::PSM_SINGLE_LINE);
  m_tessApi->SetImage(binary.data, binary.cols, binary.rows, 1, binary.step);
  m_tessApi->SetSourceResolution(300);
  char *lineOutText = m_tessApi->GetUTF8Text();
  QString lineResult = QString::fromUtf8(lineOutText).trimmed();
  delete[] lineOutText;

  QString cleanedLine = lineResult;
  cleanedLine =
      cleanedLine.replace(" ", "").replace("\t", "").replace("\n", "").replace(
          "\r", "");
  QRegularExpression plateRe(QStringLiteral("\\d{2,3}[가-힣]+\\d{4}"));
  QRegularExpressionMatch reMatchLine = plateRe.match(cleanedLine);

  if (reMatchLine.hasMatch()) {
    QString finalPlate = reMatchLine.captured(0);
    qDebug() << "[OCR] Line-First Strategy Success:" << lineResult << "->"
             << finalPlate;

    // Line-First 성공 시, 간단히 디버그 이미지 생성 후 즉시 반환
    if (isWarped) {
      static bool debugDirChecked = false;
      QString debugPath = QCoreApplication::applicationDirPath() + "/debug_ocr";
      if (!debugDirChecked) {
        QDir dir(debugPath);
        if (!dir.exists())
          dir.mkpath(".");
        debugDirChecked = true;
      }
      QString timestamp =
          QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz");
      QImage vizImg(binary.data, binary.cols, binary.rows, binary.step,
                    QImage::Format_Grayscale8);
      vizImg = vizImg.convertToFormat(QImage::Format_RGB32);
      QPainter painter(&vizImg);
      QFont font = painter.font();
      font.setPixelSize(14);
      font.setBold(true);
      painter.setFont(font);
      painter.setPen(Qt::blue);
      painter.drawText(10, 20, finalPlate);

      QString savePath =
          debugPath + "/_LINE_RESULT_" + finalPlate + "_" + timestamp + ".png";
      vizImg.save(savePath);
    }
    return finalPlate;
  }

  qDebug() << "[OCR] Line-First Strategy Failed (raw:" << lineResult
           << "). Falling back to character segmentation.";

  // === 4. 한 글자씩 개별 OCR 수행 (Fallback Strategy) ===
  // 4-1. 문자 영역 분리를 위해 반전 이미지에서 contour 추출
  //      (흰 배경 + 검은 글자 → 반전하여 흰 글자 contour를 찾음)
  cv::Mat inverted;
  cv::bitwise_not(binary, inverted);

  // === 글자 뭉침(Connected Components) 방지를 위한 모폴로지 연산 ===
  // 얇고 가벼운 침식 연산만 수행하여 미세한 노이즈 선만 다듬습니다.
  cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(2, 2));
  cv::Mat eroded;
  cv::erode(inverted, eroded, kernel);

  std::vector<std::vector<cv::Point>> charContours;
  cv::findContours(eroded, charContours, cv::RETR_EXTERNAL,
                   cv::CHAIN_APPROX_SIMPLE);

  // 4-2. 유효한 문자 bounding rect만 필터링
  int imgH = binary.rows;
  int imgW = binary.cols;
  double minCharH = imgH * 0.3; // 이미지 높이의 30% 이상
  double maxCharH = imgH * 1.0;
  double minCharW = 5.0;        // 최소 너비 5px (얇은 노이즈 제거)
  double maxCharW = imgW * 0.8; // 여러 글자가 뭉친 경우를 위해 상향 허용 (80%)

  struct CharRect {
    cv::Rect rect;
  };
  std::vector<CharRect> charRects;

  for (const auto &cnt : charContours) {
    cv::Rect br = cv::boundingRect(cnt);
    double aspectRatio = (double)br.width / br.height;

    // 1. 기본 크기 조건 필터링
    if (br.height >= minCharH && br.height <= maxCharH &&
        br.width >= minCharW && br.width <= maxCharW) {

      // 2. 종횡비(가로/세로) 기반 노이즈 필터링 및 뭉친 글자 기하학적 분할 처리
      if (aspectRatio < 0.15) {
        // 세로로 매우 얇은 노이즈 무시
        continue;
      } else if (aspectRatio > 0.85 && aspectRatio <= 1.5) {
        // 2글자가 가로로 완전히 뭉친 경우 (예: '12') -> 가로를 2등분
        int w1 = br.width / 2;
        int w2 = br.width - w1;
        charRects.push_back({cv::Rect(br.x, br.y, w1, br.height)});
        charRects.push_back({cv::Rect(br.x + w1, br.y, w2, br.height)});
      } else if (aspectRatio > 1.5 && aspectRatio <= 2.5) {
        // 3글자가 가로로 뭉친 경우 (예: '568') -> 가로를 3등분
        int w1 = br.width / 3;
        int w2 = br.width / 3;
        int w3 = br.width - (w1 + w2);
        charRects.push_back({cv::Rect(br.x, br.y, w1, br.height)});
        charRects.push_back({cv::Rect(br.x + w1, br.y, w2, br.height)});
        charRects.push_back({cv::Rect(br.x + w1 + w2, br.y, w3, br.height)});
      } else if (aspectRatio > 2.5) {
        // 4글자 이상이거나 번호판 상/하단 가로줄 노이즈일 확률이 높아 무시
        continue;
      } else {
        // 정상적인 1글자 종횡비 (0.15 ~ 0.85)
        charRects.push_back({br});
      }
    }
  }

  // === 겹치는 박스 병합(Merge Overlapping Bounding Boxes) ===
  // 하나의 글자('2', '가' 등)가 여러 개의 영역으로 나뉘어 검색된 경우 박스
  // 합치기
  bool merged;
  do {
    merged = false;
    for (size_t i = 0; i < charRects.size(); ++i) {
      for (size_t j = i + 1; j < charRects.size(); ++j) {
        cv::Rect r1 = charRects[i].rect;
        cv::Rect r2 = charRects[j].rect;
        cv::Rect intersection = r1 & r2;

        int minArea = std::min(r1.area(), r2.area());

        // 1. 겹치는 영역 기반 병합 (30% 이상 겹치면 합침)
        bool conditionOverlap = (intersection.area() > minArea * 0.3);

        // 2. 근접성 기반 병합 (가까이 있으면 '가' 처럼 합침)
        int dx = std::max(0, std::max(r1.x, r2.x) -
                                 std::min(r1.x + r1.width, r2.x + r2.width));
        int dy_overlap =
            std::max(0, std::min(r1.y + r1.height, r2.y + r2.height) -
                            std::max(r1.y, r2.y));

        bool conditionProximity = false;
        // 가로 간격 6px 이하, 세로 50% 이상 겹침 조건
        if (dx <= 6 && dy_overlap > std::min(r1.height, r2.height) * 0.5) {
          cv::Rect mergedRect = r1 | r2;
          double mergedRatio = (double)mergedRect.width / mergedRect.height;
          // 합쳐진 결과가 너무 가로로 길어지지 않는 범위(1.2 이하) 내에서만
          // 합침
          if (mergedRatio <= 1.2) {
            conditionProximity = true;
          }
        }

        if (conditionOverlap || conditionProximity) {
          charRects[i].rect = r1 | r2; // 두 박스를 감싸는 가장 큰 박스로 합침
          charRects.erase(charRects.begin() + j); // 흡수된 박스는 제거
          merged = true;
          break;
        }
      }
      if (merged)
        break; // 다시 처음부터 검사
    }
  } while (merged);

  // 4-3. x 좌표 기준 왼쪽→오른쪽 정렬
  std::sort(
      charRects.begin(), charRects.end(),
      [](const CharRect &a, const CharRect &b) { return a.rect.x < b.rect.x; });

  qDebug() << "[OCR] Character segmentation: found" << charRects.size()
           << "candidate chars (from" << charContours.size() << "contours)";

  // 4-4. 각 문자를 개별 인식 (단어 모드로 둥근 윤곽선 유지)
  QString result;
  struct CharDebugInfo {
    cv::Rect rect;
    QString text;
    cv::Mat image;
  };
  std::vector<CharDebugInfo> debugChars;

  if (charRects.empty()) {
    qDebug()
        << "[OCR] Character segmentation yielded 0 rects. Returning empty.";
  }

  // PSM_SINGLE_CHAR(10)을 기반으로 인식 (패딩 최소화)
  m_tessApi->SetPageSegMode(tesseract::PSM_SINGLE_CHAR);

  for (const auto &cr : charRects) {
    // 문자 영역 잘라내기 (패딩을 2px로 축소하여 타이트하게 추출)
    int pad = 2;
    int x = std::max(0, cr.rect.x - pad);
    int y = std::max(0, cr.rect.y - pad);
    int w = std::min(cr.rect.width + pad * 2, imgW - x);
    int h = std::min(cr.rect.height + pad * 2, imgH - y);
    cv::Mat charRoi = binary(cv::Rect(x, y, w, h));

    // Tesseract 최적 크기로 리사이즈 (높이 48px 기준)
    int targetH = 48;
    double scale = (double)targetH / charRoi.rows;
    int targetW = (int)(charRoi.cols * scale);
    if (targetW < 10)
      targetW = 10;
    cv::Mat resized;
    cv::resize(charRoi, resized, cv::Size(targetW, targetH), 0, 0,
               cv::INTER_CUBIC);

    // 흰색 패딩 보더 추가 (Tesseract가 문자를 더 잘 인식)
    cv::Mat padded;
    int border = 10;
    cv::copyMakeBorder(resized, padded, border, border, border, border,
                       cv::BORDER_CONSTANT, cv::Scalar(255));

    m_tessApi->SetImage(padded.data, padded.cols, padded.rows, 1, padded.step);
    m_tessApi->SetSourceResolution(300);
    char *outChar = m_tessApi->GetUTF8Text();

    // 공백 및 특수문자 일부 정리
    QString chRaw = QString::fromUtf8(outChar).trimmed();
    chRaw = chRaw.replace(" ", "").replace("\n", "").replace("\r", "");
    delete[] outChar;

    // PSM_SINGLE_CHAR 인데도 여러 글자를 뱉는 노이즈 케이스 방지
    // 가장 첫 번째 유효한 글자 하나만 취득 (Qt QString은 유니코드 1글자를
    // length 1로 처리함)
    QString ch;
    if (!chRaw.isEmpty()) {
      // 특수기호나 알파벳 등 번호판에서 안 쓰이는 노이즈를 걸러낼 수도
      // 있지만, 최종 Regex 필터링에서 걸러지므로 여기서는 1글자 제약만 강제.
      ch = chRaw.at(0);
    }

    if (!ch.isEmpty()) {
      // === 비정상 위치의 한글 노이즈 필터링 ===
      // 한글 문자의 유니코드 범위: 0xAC00 ~ 0xD7A3
      QChar firstChar = ch.at(0);
      bool isHangul =
          (firstChar.unicode() >= 0xAC00 && firstChar.unicode() <= 0xD7A3);

      if (isHangul) {
        double centerX = cr.rect.x + (cr.rect.width / 2.0);
        double relativePos = centerX / imgW;

        // 번호판 구조상 한글은 가운데 영역에만 위치함
        // 한글이 발견 가능한 범위를 가운데 30% 영역(0.35~0.65)으로 더 좁혀서
        // 양 끝단의 숫자나 노이즈가 한글로 오인되는 것을 방지
        if (relativePos < 0.35 || relativePos > 0.65) {
          qDebug() << "[OCR] Ignored Hangul out of bounds at" << cr.rect.x
                   << "(rel:" << relativePos << ") char:" << ch;
          continue;
        }
      }

      result += ch;
      debugChars.push_back({cr.rect, ch, padded.clone()});
      qDebug() << "[OCR]   Char rect" << cr.rect.x << "," << cr.rect.y
               << cr.rect.width << "x" << cr.rect.height << " -> raw:'" << chRaw
               << "' filtered:'" << ch << "'";
    }
  }

  // === 번호판 규격 정규식 필터링 (공백 제거 후 추출) ===
  QString cleaned = result;
  cleaned =
      cleaned.replace(" ", "").replace("\t", "").replace("\n", "").replace("\r",
                                                                           "");

  QRegularExpression fallbackPlateRe(QStringLiteral("\\d{2,3}[가-힣]+\\d{4}"));
  QRegularExpressionMatch reMatch = fallbackPlateRe.match(cleaned);

  if (reMatch.hasMatch()) {
    QString finalPlate = reMatch.captured(0);
    qDebug() << "[OCR] Regex Match Success:" << result << "->" << finalPlate;
    result = finalPlate;
  } else {
    qDebug() << "[OCR] Regex Match Failed. Tesseract saw:" << cleaned
             << " (Original was:" << result << ")";
    result = "";
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

    // === 통합 시각화 이미지 생성 (QPainter를 사용하여 한글 깨짐 방지) ===
    // 1. 최종 인식용 binary 이미지를 QImage로 변환
    QImage vizImg(binary.data, binary.cols, binary.rows, binary.step,
                  QImage::Format_Grayscale8);
    // 2. 컬러 표시를 위해 RGB32로 변환
    vizImg = vizImg.convertToFormat(QImage::Format_RGB32);

    QPainter painter(&vizImg);
    QFont font = painter.font();
    font.setPixelSize(12);
    font.setBold(true);
    painter.setFont(font);

    for (const auto &dc : debugChars) {
      QRect qRect(dc.rect.x, dc.rect.y, dc.rect.width, dc.rect.height);

      // 빨간색 박스
      painter.setPen(QPen(Qt::red, 1));
      painter.drawRect(qRect);

      // 파란색 텍스트 (한글 지원)
      painter.setPen(QPen(Qt::blue));
      painter.drawText(qRect.left(), qRect.top() - 2, dc.text);
    }
    painter.end();

    // 최종 인식 결과 태그
    QString plateTag = result.isEmpty() ? "FAIL" : result;
    plateTag.replace("/", "_").replace("\\", "_").replace(":", "_");

    // 통합 시각화 결과 저장
    QString finalPath = QString("%1/%2_RESULT_%3.png")
                            .arg(debugPath)
                            .arg(timestamp)
                            .arg(plateTag);
    vizImg.save(finalPath);

    qDebug() << "[OCR Debug] Integrated result (Hangul supported) saved to:"
             << finalPath;
  }

  return result;
}
