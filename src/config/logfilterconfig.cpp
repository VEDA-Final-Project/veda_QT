#include "logfilterconfig.h"
#include <QMutexLocker>

LogFilterConfig &LogFilterConfig::instance() {
  static LogFilterConfig s;
  return s;
}

LogFilterConfig::LogFilterConfig() {
  // 카테고리 정의 — 접두사 패턴 + 기본값
  m_categories["OCR"] = {{"[OCR]"}, false}; // 기본 OFF (양이 많음)

  m_categories["Video"] = {{"[Video]"}, false}; // 기본 OFF (양이 많음)

  m_categories["Camera"] = {{"[Camera]"}, true};

  m_categories["Telegram"] = {{"[Telegram]"}, true};

  m_categories["DB"] = {{"[DB]"}, true};

  m_categories["ROI"] = {{"[ROI]"}, true};

  m_categories["OpenCV"] = {{"[ INFO:", "[ WARN:", "[ ERROR:"},
                             false}; // 기본 OFF
}

bool LogFilterConfig::isEnabled(const QString &category) const {
  QMutexLocker lock(&m_mutex);
  auto it = m_categories.find(category);
  if (it == m_categories.end())
    return true; // 알 수 없는 카테고리는 항상 출력
  return it->enabled;
}

void LogFilterConfig::setEnabled(const QString &category, bool enabled) {
  QMutexLocker lock(&m_mutex);
  auto it = m_categories.find(category);
  if (it != m_categories.end()) {
    it->enabled = enabled;
  }
}

QString LogFilterConfig::detectCategory(const QString &message) const {
  QMutexLocker lock(&m_mutex);
  for (auto it = m_categories.constBegin(); it != m_categories.constEnd();
       ++it) {
    for (const QString &prefix : it->prefixes) {
      if (message.contains(prefix)) {
        return it.key();
      }
    }
  }
  return QString(); // 매칭되는 카테고리 없음
}
