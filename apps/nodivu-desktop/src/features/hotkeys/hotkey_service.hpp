#pragma once
#include "features/workspace/session_controller.hpp"
#include <QAbstractNativeEventFilter>
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QKeySequence>
#include <functional>
class QWidget;
namespace nodivu {
struct HotkeyAction {
    QString id, label, node, operation, field;
    ControlDescriptor control;
    bool needsValue = false;
};
// Control-thread only. No keyboard hook, injected code, audio or plugin code here.
class HotkeyService final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
  public:
    HotkeyService(SessionController& session, QString profile, QWidget* window);
    ~HotkeyService() override;
    QVector<HotkeyAction> actions() const;
    QJsonObject config() const {
        return config_;
    }
    QString state(const QString& id) const {
        return states_.value(id);
    }
    QString loadError() const {
        return loadError_;
    }
    QString save(const QJsonObject& config);
    void suspend(bool value);
    void fire(const QString& id, bool test = false);
    void setBlocked(std::function<bool()> blocked) {
        blocked_ = std::move(blocked);
    }
    static QString validate(const QJsonObject& config);
    static QString keyError(const QKeySequence& sequence);
    static QKeySequence sequence(QString accelerator);
    static QString accelerator(const QKeySequence& sequence);
    bool nativeEventFilter(const QByteArray&, void*, qintptr*) override;
  signals:
    void statusChanged();
    void result(bool ok, const QString& message);

  protected:
    bool eventFilter(QObject*, QEvent*) override;

  private:
    void reconcile();
    void unregister();
    void execute(const HotkeyAction&, const QJsonObject&);
    void command(const QString&, const QJsonObject&, std::function<void()> next = {});
    void finish(bool ok, const QString& message);
    SessionController& session_;
    QWidget* window_;
    QString file_, loadError_, actionFingerprint_, monitorEndpoint_;
    QJsonObject config_;
    QHash<QString, QString> states_;
    QHash<int, QString> registered_;
    QHash<QString, qint64> last_;
    QElapsedTimer clock_;
    QDeadlineTimer deadline_;
    std::function<bool()> blocked_;
    bool suspended_ = false, pending_ = false;
};
void showHotkeys(HotkeyService& service, QWidget* parent);
} // namespace nodivu
