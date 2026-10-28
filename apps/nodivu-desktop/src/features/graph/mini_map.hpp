#pragma once
#include <QTransform>
#include <QWidget>

namespace nodivu {
class GraphView;
class GraphScene;
class MiniMap final : public QWidget {
    Q_OBJECT
  public:
    MiniMap(GraphView& view, GraphScene& scene);
  signals:
    void closeRequested();

  protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void reposition();
    QTransform diagramTransform() const;
    GraphView& view_;
    GraphScene& scene_;
};
} // namespace nodivu
