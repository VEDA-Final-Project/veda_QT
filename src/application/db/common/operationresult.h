#ifndef APPLICATION_DB_COMMON_OPERATIONRESULT_H
#define APPLICATION_DB_COMMON_OPERATIONRESULT_H

#include <QString>

struct OperationResult {
  bool success = false;
  QString message;
  bool shouldRefresh = false;
};

#endif // APPLICATION_DB_COMMON_OPERATIONRESULT_H
