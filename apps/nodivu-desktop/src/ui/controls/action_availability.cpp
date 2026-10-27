#include "ui/controls/action_availability.hpp"
#include <QAction>
#include <QEvent>
#include <QHelpEvent>
#include <QToolTip>
#include <QVariant>
#include <QWidget>
namespace nodivu {
namespace {
class ActionHint final : public QObject {
  public:
    explicit ActionHint(QWidget* widget) : QObject(widget) {
        widget->installEventFilter(this);
    }

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        auto* widget = qobject_cast<QWidget*>(watched);
        if (widget && event->type() == QEvent::ToolTip && !widget->toolTip().isEmpty()) {
            const auto* help = static_cast<QHelpEvent*>(event);
            QToolTip::showText(help->globalPos(), widget->toolTip(), widget);
            return true;
        }
        return QObject::eventFilter(watched, event);
    }
};
} // namespace
void setActionAvailability(QAction* action, const QString& purpose, const QString& reason) {
    const auto description =
        reason.isEmpty() ? purpose : purpose + QStringLiteral("\nIndisponível: ") + reason;
    auto tooltip = description.toHtmlEscaped();
    tooltip.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    action->setToolTip(QStringLiteral("<qt>") + tooltip + QStringLiteral("</qt>"));
    action->setStatusTip(description);
    action->setEnabled(reason.isEmpty());
}
void setActionAvailability(QWidget* widget, const QString& purpose, const QString& reason) {
    if (!widget->property("actionHintInstalled").toBool()) {
        new ActionHint(widget);
        widget->setProperty("actionHintInstalled", true);
        widget->setAttribute(Qt::WA_AlwaysShowToolTips);
    }
    const auto description =
        reason.isEmpty() ? purpose : purpose + QStringLiteral("\nIndisponível: ") + reason;
    // Plugin titles may appear in a purpose. Preserve them as text in Qt's rich tooltips.
    auto tooltip = description.toHtmlEscaped();
    tooltip.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    widget->setToolTip(QStringLiteral("<qt>") + tooltip + QStringLiteral("</qt>"));
    widget->setStatusTip(description);
    widget->setAccessibleDescription(description);
    widget->setEnabled(reason.isEmpty());
}
} // namespace nodivu
