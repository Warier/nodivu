#include "ui/theme/theme.hpp"
#include "ui/theme/brand_icon.hpp"

#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QPalette>
#include <QStyleFactory>

namespace nodivu {
void Theme::apply(QApplication& application) {
    QIcon icon;
    for (const int size : {16, 24, 32, 48, 64, 128, 256})
        icon.addPixmap(QPixmap::fromImage(brandImage(size)));
    application.setWindowIcon(icon);
    application.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    application.setFont(QFont(QStringLiteral("Segoe UI"), 10));
    auto palette = application.palette();
    palette.setColor(QPalette::Window, background());
    palette.setColor(QPalette::WindowText, QColor(219, 227, 239));
    palette.setColor(QPalette::Base, QColor(18, 25, 36));
    palette.setColor(QPalette::AlternateBase, panel());
    palette.setColor(QPalette::Text, QColor(219, 227, 239));
    palette.setColor(QPalette::Button, panel());
    palette.setColor(QPalette::ButtonText, QColor(219, 227, 239));
    palette.setColor(QPalette::Highlight, QColor(38, 79, 69));
    palette.setColor(QPalette::HighlightedText, QColor(219, 227, 239));
    palette.setColor(QPalette::Disabled, QPalette::Text, muted());
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, muted());
    application.setPalette(palette);
    application.setStyleSheet(QStringLiteral(
        "QPushButton { padding: 7px 10px; border: 1px solid #3d4b5a; border-radius: 5px; }"
        "QPushButton:hover { border-color: #70d9bd; }"
        "QPushButton:focus { border: 2px solid #70d9bd; }"
        "QPushButton:disabled { color: #626b78; border-color: #2b3543; }"
        "QSlider::groove:horizontal { height: 5px; background: #101722; border-radius: 2px; }"
        "QSlider::sub-page:horizontal { background: #70d9bd; border-radius: 2px; }"
        "QSlider::handle:horizontal { background: #70d9bd; width: 14px; margin: -5px 0; "
        "border-radius: 7px; }"
        "QGroupBox { border: 1px solid #344453; border-radius: 7px; margin-top: 12px; padding: "
        "14px 10px 10px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; color: #adbed2; "
        "}"
        "QScrollBar:vertical { background: #151e28; width: 8px; }"
        "QScrollBar::handle:vertical { background: #3b4b5b; min-height: 24px; border-radius: 3px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QComboBox, QSpinBox, QDoubleSpinBox, QLineEdit { padding: 6px; border: 1px solid #344257; "
        "border-radius: 5px; }"
        "QToolTip { color: #dbe3ef; background: #202a35; border: 1px solid #516373; }"));
}
} // namespace nodivu
