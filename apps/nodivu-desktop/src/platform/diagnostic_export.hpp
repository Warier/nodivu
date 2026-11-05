#pragma once
#include <QByteArray>
#include <QString>
namespace nodivu {
struct DiagnosticReport {
    QByteArray bytes;
    QString error;
};
// Exports only the bounded frontend logs, never the project, PCM or plugin files.
DiagnosticReport diagnosticReport(const QString& profile);
QString redactDiagnostic(QString text);
} // namespace nodivu
