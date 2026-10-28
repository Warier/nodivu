#pragma once
#include <QGraphicsView>
#include <QJsonObject>
#include <QPointer>

namespace nodivu {
class GraphView final : public QGraphicsView {
    Q_OBJECT
  public:
    explicit GraphView(QGraphicsScene* scene, QWidget* parent = nullptr);
    void fitNodes();
    void fitSelection();
    void focusPosition(const QPointF& point);
    void setOverlayWidget(QWidget* overlay);
    void addOverlayWidget(QWidget* overlay);
    QRect availableViewportRect() const;
    void revealNode(QGraphicsItem* item, bool placeInView = false);
    void zoomBy(double factor);
    void setZoomEnabled(bool enabled) {
        zoomEnabled_ = enabled;
    }
    QJsonObject camera() const;
    void restoreCamera(const QJsonObject& camera);
  signals:
    void cameraEdited();
    void zoomChanged(double scale);
    void blockDropped(const QString& kind, const QString& pluginId, const QPointF& position);

  protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    bool panning_ = false;
    bool spacePan_ = false;
    void cancelPan();
    bool zoomEnabled_ = false;
    QPoint previous_;
    QList<QPointer<QWidget>> overlays_;
};
} // namespace nodivu
