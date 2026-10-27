#pragma once
#include "contracts/audio_state.hpp"
#include <QFrame>
class QLabel;
class QPushButton;
namespace nodivu {
class AudioStatusPanel final : public QFrame {
    Q_OBJECT
  public:
    explicit AudioStatusPanel(QWidget* parent = nullptr);
    void synchronize(const AudioState& state, const QString& blockedReason = {});
  signals:
    void actionRequested();

  private:
    QLabel* title_ = nullptr;
    QLabel* detail_ = nullptr;
    QPushButton* action_ = nullptr;
};
} // namespace nodivu
