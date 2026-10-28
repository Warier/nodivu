#pragma once

#include "contracts/node_descriptor.hpp"
#include <QPushButton>
#include <QTextLayout>

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
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

  private:
    NodeDescriptor descriptor_;
    void updateTextLayout(int width);
    QTextLayout titleLayout_;
    QTextLayout subtitleLayout_;
    qreal titleHeight_ = 0;
    qreal subtitleHeight_ = 0;
    qreal labelLeft_ = 54;
    bool narrow_ = false;
    QPoint pressPosition_;
    bool compact_ = false;
    bool dragged_ = false;
};
} // namespace nodivu
