#ifndef UTIL_RESULT_H
#define UTIL_RESULT_H

#include <QString>
#include <utility>

template <typename T> struct Result {
  T data;
  QString error;

  bool isOk() const { return error.isEmpty(); }
};

#endif // UTIL_RESULT_H
