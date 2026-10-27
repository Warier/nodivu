#pragma once
#include "features/workspace/session_controller.hpp"
#include <QDeadlineTimer>
#include <QWidget>
class QComboBox;
class QPushButton;
class QLabel;
namespace nodivu {
class InfoMessage;
// Shared by the card and explicit inspector; capture identities and acquisition
// remain in the existing backend API.
class CaptureSourceWidget final : public QWidget {
    Q_OBJECT
  public:
    CaptureSourceWidget(SessionController& session, QString nodeId, bool compact,
                        QWidget* parent = nullptr);
  signals:
    void geometryChanged();

  private:
    void refresh(bool begin);
    void synchronize();
    void configure(const QJsonObject& params, QDeadlineTimer deadline);
    SessionController& session_;
    QString nodeId_;
    QComboBox* targetsCombo_ = nullptr;
    QPushButton* refresh_ = nullptr;
    InfoMessage* status_ = nullptr;
    QLabel* timing_ = nullptr;
    QJsonArray targets_;
    QDeadlineTimer deadline_;
    bool loading_ = false;
    QString error_;
};
} // namespace nodivu
