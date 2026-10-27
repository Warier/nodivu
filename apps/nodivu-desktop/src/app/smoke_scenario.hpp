#pragma once
#include <QString>
namespace nodivu {
class MainWindow;
void startNativeSmoke(MainWindow& window, const QString& project, const QString& screenshot,
                      int benchmarkMilliseconds = 0);
} // namespace nodivu
