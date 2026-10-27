#pragma once
#include <QString>
namespace nodivu {
// Copies only application documents/trust metadata, never signing keys or media.
QString prepareQtProfileBackup(const QString& profile);
} // namespace nodivu
