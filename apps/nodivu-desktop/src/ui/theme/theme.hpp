#pragma once

#include <QColor>
class QApplication;

namespace nodivu {
struct Theme {
    static constexpr int BodyFont = 12;
    static constexpr int DetailFont = 11;
    static constexpr int ControlTarget = 28;
    static constexpr int SidePanelWidth = 372;
    static constexpr int PanelPadding = 20;
    static constexpr int ControlSpacing = 12;
    static constexpr int LibraryWidth = 248;
    static constexpr int NodeWidth = 240;
    static constexpr int InspectorWidth = 348;
    static QColor text() {
        return QColor(219, 227, 239);
    }
    static QColor warningText() {
        return QColor(240, 212, 155);
    }
    static QColor dangerText() {
        return QColor(255, 218, 222);
    }
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
        return QColor(163, 177, 195);
    }
    static void apply(QApplication& application);
};
} // namespace nodivu
