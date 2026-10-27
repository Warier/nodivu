#pragma once

#include "contracts/node_descriptor.hpp"
#include <QPushButton>

namespace nodivu {
class LibraryBlockButton final : public QPushButton {
    Q_OBJECT
  public:
    explicit LibraryBlockButton(const NodeDescriptor& descriptor, QWidget* parent = nullptr);
    void setCompact(bool compact);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

  protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

  private:
    NodeDescriptor descriptor_;
    QPoint pressPosition_;
    bool compact_ = false;
    bool dragged_ = false;
};
} // namespace nodivu
