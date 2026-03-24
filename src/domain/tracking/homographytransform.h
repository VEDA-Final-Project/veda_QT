#ifndef DOMAIN_TRACKING_HOMOGRAPHYTRANSFORM_H
#define DOMAIN_TRACKING_HOMOGRAPHYTRANSFORM_H

#include <QPointF>
#include <QTransform>

class Config;

namespace tracking {

class HomographyTransform {
public:
  HomographyTransform() = default;
  explicit HomographyTransform(const QTransform &transform);

  static HomographyTransform fromConfig(const Config &config);

  bool isValid() const;
  QPointF mapToGround(const QPointF &imagePoint, bool *ok = nullptr) const;

private:
  QTransform m_transform;
  bool m_valid = false;
};

} // namespace tracking

#endif // DOMAIN_TRACKING_HOMOGRAPHYTRANSFORM_H
