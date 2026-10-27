#pragma once
#include <QString>

namespace nodivu {
class DiagnosticLog {
  public:
    explicit DiagnosticLog(QString profile);
    void record(const QString& category, const QString& message) const;

  private:
    QString directory_;
};
} // namespace nodivu
