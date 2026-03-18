#include "parking/parkingfeepolicy.h"

namespace parking 
{
  namespace 
  {
  constexpr qint64 kFreeMinutes = 5;
  constexpr qint64 kBillingMinutes = 30;
  constexpr int kUnitFee = 3000;
}

ParkingFeeResult calculateParkingFee(const QDateTime &entryTime, const QDateTime &targetTime) 
{
  ParkingFeeResult result;
  if (!entryTime.isValid() || !targetTime.isValid() || targetTime <= entryTime) {
    return result;
  }

  const qint64 totalSeconds = entryTime.secsTo(targetTime);
  result.totalMinutes = totalSeconds / 60;

  const qint64 freeSeconds = kFreeMinutes * 60;
  if (totalSeconds <= freeSeconds) {
    return result;
  }

  const qint64 chargedSeconds = totalSeconds - freeSeconds;
  result.chargedMinutes = chargedSeconds / 60;
  result.billingUnits =
      (chargedSeconds + (kBillingMinutes * 60) - 1) / (kBillingMinutes * 60);
  result.totalAmount = static_cast<int>(result.billingUnits) * kUnitFee;
  result.isFree = result.totalAmount <= 0;
  return result;
}

} // namespace parking
