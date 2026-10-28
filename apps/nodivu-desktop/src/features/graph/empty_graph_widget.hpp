#pragma once

#include "contracts/node_descriptor.hpp"
#include <QWidget>

class QPushButton;
class QScrollArea;

namespace nodivu {
class EmptyGraphWidget final : public QWidget {
    Q_OBJECT
  public:
    explicit EmptyGraphWidget(QWidget* viewport);
    void setReady(bool ready);
    void setCatalog(const NodeCatalog& catalog);
    void watchOverlay(QWidget* overlay);

  signals:
    void startRequested();
    void sourceRequested(const QString& kind, const QString& pluginId);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void centerInViewport();
    QPushButton* apps_ = nullptr;
    QPushButton* file_ = nullptr;
    QString appPlugin_;
    QString filePlugin_;
    bool ready_ = false;
    QPushButton* start_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    QWidget* body_ = nullptr;
};
} // namespace nodivu
