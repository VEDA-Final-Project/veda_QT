#ifndef LOGFILTERCONFIG_H
#define LOGFILTERCONFIG_H

#include <QMap>
#include <QMutex>
#include <QString>
#include <QStringList>

/**
 * @brief 카테고리별 로그 필터 설정 (싱글턴)
 *
 * qDebug 메시지의 접두사 ([OCR], [Video] 등)를 기준으로
 * 카테고리별 출력 여부를 제어합니다.
 */
class LogFilterConfig {
public:
  static LogFilterConfig &instance();

  /// 해당 카테고리의 활성화 여부
  bool isEnabled(const QString &category) const;

  /// 카테고리 활성화/비활성화 설정
  void setEnabled(const QString &category, bool enabled);

  /// 메시지 텍스트에서 카테고리를 추출 (예: "[OCR]..." → "OCR")
  /// 매칭되는 카테고리가 없으면 빈 문자열 반환
  QString detectCategory(const QString &message) const;

private:
  LogFilterConfig();
  LogFilterConfig(const LogFilterConfig &) = delete;
  LogFilterConfig &operator=(const LogFilterConfig &) = delete;

  mutable QMutex m_mutex;

  struct CategoryInfo {
    QStringList prefixes; // 매칭할 접두사 목록
    bool enabled;         // 기본 활성화 여부
  };

  QMap<QString, CategoryInfo> m_categories; // key = category id
};

#endif // LOGFILTERCONFIG_H
