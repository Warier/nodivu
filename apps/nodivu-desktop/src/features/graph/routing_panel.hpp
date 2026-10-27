#pragma once
#include "features/workspace/session_controller.hpp"
#include <QWidget>

class QTreeWidget;
class QPushButton;
namespace nodivu {
// A native, keyboard/screen-reader usable view of the same backend graph.
class RoutingPanel final : public QWidget {
    Q_OBJECT
  public:
    explicit RoutingPanel(SessionController& session, QWidget* parent = nullptr);
    void setBlockedReason(const QString& reason);
  signals:
    void inspectRequested(const QString& id);

  protected:
    void showEvent(QShowEvent* event) override;

  private:
    void synchronize();
    void updateActions();
    void connectSelected();
    SessionController& session_;
    QTreeWidget* tree_ = nullptr;
    QPushButton* inspect_ = nullptr;
    QPushButton* remove_ = nullptr;
    QPushButton* connect_ = nullptr;
    QJsonObject graph_;
    QString blockedReason_;
    QJsonObject availabilityGraph_;
    bool possibleConnection_ = false;
};
} // namespace nodivu
