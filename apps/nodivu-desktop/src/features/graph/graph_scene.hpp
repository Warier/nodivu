#pragma once

#include "contracts/node_descriptor.hpp"
#include "contracts/playback_state.hpp"
#include "features/workspace/session_controller.hpp"
#include <QGraphicsObject>
#include <QGraphicsPathItem>
#include <QGraphicsScene>

namespace nodivu {
class NodeContent;
class NodeItem final : public QGraphicsObject {
    Q_OBJECT
  public:
    NodeItem(QString id, NodeDescriptor descriptor, SessionController& session);
    ~NodeItem() override;
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    QPointF portPosition(bool output, int index) const;
    void setState(const QJsonObject& block, double peak);
    void setPlayback(const PlaybackState& state);
    void synchronizeControls(const QJsonArray& devices, bool enabled, const QJsonObject& runtime);
    void focusPrimaryControl();
    bool needsInspector() const {
        return descriptor_.needsInspector();
    }
    const QString& nodeId() const {
        return id_;
    }
  signals:
    void moved();
    void inspectRequested(const QString& id);
    void removeRequested(const QString& id);
    void fieldEdited(const QString& id, const QString& field, const QJsonValue& value);
    void actionRequested(const QString& id, const QString& action, const QPoint& screenPosition);
    void gestureRequested(const QString& id, quint32 parameter, bool pressed);

  protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

  private:
    void updateHeaderHover(const QPointF& position);
    void paintHeaderActions(QPainter* painter) const;
    void releaseGesture();
    QString id_;
    NodeDescriptor descriptor_;
    QJsonObject block_;
    double peak_ = 0;
    QTimer gestureTimer_;
    quint32 gestureParameter_ = 0;
    bool holdingGesture_ = false;
    NodeContent* content_ = nullptr;
    qreal bodyHeight_ = 148;
    bool controlsEnabled_ = false;
    int hoveredHeaderAction_ = 0;
};

class GraphScene final : public QGraphicsScene {
    Q_OBJECT
  public:
    explicit GraphScene(SessionController& session, QObject* parent = nullptr);
    QJsonArray positions() const;
    void restorePositions(const QJsonArray& positions);
    void organize();
    void selectNode(const QString& id);
    void placeNode(const QString& id, const QPointF& position);
    NodeItem* nodeItem(const QString& id) const {
        return nodes_.value(id);
    }
    void setSavedResources(const QHash<QString, QString>& resources);
  signals:
    void inspectRequested(const QString& id);
    void positionsEdited();
    void nodeActionRequested(const QString& id, const QString& action, const QPoint& position);

  protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

  private:
    void synchronize();
    void updateConnections(const QString& movedNode = {});
    bool usePort(QGraphicsItem* item);
    SessionController& session_;
    QHash<QString, NodeItem*> nodes_;
    QHash<QString, QGraphicsPathItem*> edges_;
    QHash<QString, QPointF> savedPositions_;
    QHash<QString, QString> savedResources_;
    QGraphicsPathItem* preview_ = nullptr;
    QString armedNode_;
    int armedPort_ = 0;
    bool synchronizing_ = false;
};
} // namespace nodivu
