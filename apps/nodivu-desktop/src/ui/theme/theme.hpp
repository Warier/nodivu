#pragma once

#include <QColor>
class QApplication;

namespace nodivu {
struct Theme {
    static constexpr int LibraryWidth = 228;
    static constexpr int NodeWidth = 218;
    static constexpr int InspectorWidth = 316;
    static QColor background() {
        return QColor(16, 21, 27);
    }
    static QColor panel() {
        return QColor(28, 37, 48);
    }
    static QColor headerBackground() {
        return QColor(23, 29, 37);
    }
    static QColor mint() {
        return QColor(112, 217, 189);
    }
    static QColor muted() {
        return QColor(149, 161, 178);
    }
    static void apply(QApplication& application);
};
} // namespace nodivu
