#pragma once
#include <QCheckBox>

namespace nodivu {
// Keep native checkbox semantics with the Studio's compact indicator and label.
class InlineCheckBox final : public QCheckBox {
  public:
    explicit InlineCheckBox(const QString& text, QWidget* parent = nullptr);

  protected:
    void paintEvent(QPaintEvent* event) override;
};
} // namespace nodivu
