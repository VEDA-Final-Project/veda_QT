#include "domain/tracking/homographytransform.h"

#include "config/config.h"

namespace tracking {

HomographyTransform::HomographyTransform(const QTransform &transform)
    : m_transform(transform), m_valid(!transform.isIdentity()) {}

HomographyTransform HomographyTransform::fromConfig(const Config &config) {
  if (!config.homographyEnabled()) {
    return HomographyTransform();
  }
  return HomographyTransform(config.homographyTransform());
}

bool HomographyTransform::isValid() const { return m_valid; }

QPointF HomographyTransform::mapToGround(const QPointF &imagePoint,
                                         bool *ok) const {
  if (ok) {
    *ok = false;
  }
  if (!m_valid) {
    return QPointF();
  }

  bool invertible = false;
  const QTransform inverse = m_transform.inverted(&invertible);
  if (!invertible) {
    return QPointF();
  }

  const QPointF groundPoint = inverse.map(imagePoint);
  if (ok) {
    *ok = true;
  }
  return groundPoint;
}

} // namespace tracking
