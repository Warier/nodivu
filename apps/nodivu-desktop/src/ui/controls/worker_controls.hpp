#pragma once
#include <QJsonObject>
#include <QWidget>
class QLabel;
class QPushButton;

namespace nodivu {
class WorkerControls final : public QWidget {
    Q_OBJECT
  public:
    WorkerControls(bool compact, QWidget* parent = nullptr);
    void synchronize(const QJsonObject& runtime, bool enabled);
  signals:
    void restartRequested();
    void geometryChanged();

  private:
    QLabel* status_;
    QPushButton* restart_;
};
} // namespace nodivu
