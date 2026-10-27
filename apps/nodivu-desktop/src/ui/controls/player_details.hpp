#pragma once
#include "contracts/playback_state.hpp"
#include <QWidget>

class QLabel;
class QResizeEvent;
namespace nodivu {
class InfoMessage;
// Shared presentation keeps the graph card and inspector consistent.
class PlayerDetails final : public QWidget {
    Q_OBJECT
  public:
    PlayerDetails(bool compact, bool transport, QWidget* parent = nullptr);
    void synchronize(const PlaybackState& state, bool updatePosition = true);
    void previewPosition(int positionMs, int durationMs);

  signals:
    void geometryChanged();

  protected:
    void resizeEvent(QResizeEvent* event) override;

  private:
    void updateTextLayout();
    bool compact_;
    QLabel* time_;
    QLabel* status_;
    InfoMessage* reason_;
    QString statusText_;
};
} // namespace nodivu
