#pragma once
#include "features/store/store_controller.hpp"
#include <QDialog>
class QTabBar;
class QLineEdit;
class QTreeWidget;
class QPushButton;
class QLabel;
class QProgressBar;
class QToolButton;
class QAction;

namespace nodivu {
class ReadmeView;
class StoreWindow final : public QDialog {
    Q_OBJECT
  public:
    explicit StoreWindow(StoreController& controller, QWidget* parent = nullptr);

  private:
    void render();
    void renderProgress();
    void selectedAction();
    void updateSelection();
    void synchronizeSecondaryActions();
    QList<QPair<QAction*, QPushButton*>> secondary_;
    QToolButton* more_ = nullptr;
    QToolButton* errorDetails_ = nullptr;
    StoreController& controller_;
    QTabBar* tabs_ = nullptr;
    QLineEdit* search_ = nullptr;
    QTreeWidget* list_ = nullptr;
    QLabel* message_ = nullptr;
    QLabel* count_ = nullptr;
    QLabel* selectionInfo_ = nullptr;
    QPushButton* action_ = nullptr;
    QPushButton* details_ = nullptr;
    QPushButton* enable_ = nullptr;
    QPushButton* remove_ = nullptr;
    QPushButton* rollback_ = nullptr;
    QPushButton* cancelUpdate_ = nullptr;
    QPushButton* cancel_ = nullptr;
    QProgressBar* progress_ = nullptr;
    ReadmeView* readme_ = nullptr;
    QTimer progressTimer_;
};
} // namespace nodivu
