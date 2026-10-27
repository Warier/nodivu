#pragma once

#include <QWidget>

class QPushButton;

namespace nodivu {
class EmptyGraphWidget final : public QWidget {
    Q_OBJECT
  public:
    explicit EmptyGraphWidget(QWidget* viewport);
    void setReady(bool ready);

  signals:
    void startRequested();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void centerInViewport();
    QPushButton* start_ = nullptr;
};
} // namespace nodivu
