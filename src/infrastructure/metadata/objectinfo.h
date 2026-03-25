#ifndef OBJECTINFO_H
#define OBJECTINFO_H

#include <QRectF>
#include <QString>
#include <vector>

struct ObjectInfo {
  int id = -1;
  QString type;      // Person, Vehicle, Face...
  QString extraInfo; // License Plate Number, etc. (Legacy)
  QString plate;     // Explicit plate number
  float score = 0.0f; // Confidence score
  QRectF rect;       // 0~1000 Normalized Coordinate or Pixel Coordinate
  std::vector<float> reidFeatures; // ReID Feature Vector
  QString reidId;                // Persistent ID from ReID matching
};

#endif // OBJECTINFO_H
