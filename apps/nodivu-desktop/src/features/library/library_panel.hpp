#pragma once
#include "contracts/node_descriptor.hpp"
#include <QWidget>
class QScrollArea;
class QLineEdit;
class QLabel;
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
    void focusSearch();
    QStringList favorites() const;
    void setFavorites(const QStringList& favorites);
    QJsonObject workspaceState() const;
    void restoreWorkspaceState(const QJsonObject& state);
  signals:
    void addRequested(const QString& kind, const QString& pluginId);
    void virtualOutputRequested();
    void widthRequested(int width);
    void preferencesChanged();

  protected:
    void resizeEvent(QResizeEvent* event) override;

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
    void filterBlocks();
    void updateMargins();
    QLineEdit* search_ = nullptr;
    QLabel* noResults_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    QVBoxLayout* contentLayout_ = nullptr;
    QWidget* headingLabels_ = nullptr;
    QWidget* intro_ = nullptr;
    QToolButton* toggle_ = nullptr;
    QPushButton* virtualOutput_ = nullptr;
    QVector<Category> categories_;
    bool collapsed_ = false;
    int expandedWidth_ = 248;
    NodeCatalog catalog_;
    QStringList favorites_;
};
} // namespace nodivu
