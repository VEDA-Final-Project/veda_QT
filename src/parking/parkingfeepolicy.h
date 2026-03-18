#ifndef PARKINGFEEPOLICY_H
#define PARKINGFEEPOLICY_H

#include <QDateTime>

namespace parking {

struct ParkingFeeResult {
  int totalAmount = 0;
  bool isFree = true;
  qint64 totalMinutes = 0;
  qint64 chargedMinutes = 0;
  qint64 billingUnits = 0;
};

ParkingFeeResult calculateParkingFee(const QDateTime &entryTime,
                                     const QDateTime &targetTime);

} // namespace parking

#endif
