#pragma once
#include "features/workspace/session_controller.hpp"
#include <QWidget>
class QPushButton;
class QLabel;
namespace nodivu {
class SoundboardWidget final : public QWidget {
  public:
    SoundboardWidget(SessionController& session, QString id, bool compact,
                     QWidget* parent = nullptr);

  private:
    void synchronize();
    void command(int slot, const QString& action, const QString& path = {});
    struct Row {
        QPushButton *play, *choose = nullptr, *stop = nullptr, *clear = nullptr;
        QLabel* status = nullptr;
    };
    SessionController& session_;
    QString id_;
    QVector<Row> rows_;
};
} // namespace nodivu
