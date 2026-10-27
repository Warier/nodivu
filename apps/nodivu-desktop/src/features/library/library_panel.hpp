#pragma once
#include "contracts/node_descriptor.hpp"
#include <QWidget>
class QScrollArea;
class QToolButton;
class QVBoxLayout;
class QPushButton;

namespace nodivu {
class LibraryPanel final : public QWidget {
    Q_OBJECT
  public:
    explicit LibraryPanel(QWidget* parent = nullptr);
    void setCatalog(const NodeCatalog& catalog);
    void setVirtualOutputEnabled(bool enabled);
  signals:
    void addRequested(const QString& kind, const QString& pluginId);
    void manageRequested();
    void refreshRequested();
    void retryRequested();
    void virtualOutputRequested();
    void widthRequested(int width);

  private:
    struct Category {
        QString name;
        QWidget* container = nullptr;
        QToolButton* toggle = nullptr;
        QWidget* cards = nullptr;
        QVBoxLayout* layout = nullptr;
        bool expanded = true;
    };
    void toggleCollapsed();
    QScrollArea* scroll_ = nullptr;
    QVBoxLayout* contentLayout_ = nullptr;
    QWidget* headingLabels_ = nullptr;
    QWidget* intro_ = nullptr;
    QWidget* note_ = nullptr;
    QToolButton* toggle_ = nullptr;
    QPushButton* virtualOutput_ = nullptr;
    QVector<Category> categories_;
    QList<QPushButton*> actions_;
    bool collapsed_ = false;
    int expandedWidth_ = 228;
};
} // namespace nodivu
