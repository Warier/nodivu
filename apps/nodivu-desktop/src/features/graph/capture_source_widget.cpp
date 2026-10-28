#include "features/graph/capture_source_widget.hpp"
#include "ui/controls/device_selector.hpp"
#include "ui/controls/info_message.hpp"
#include <QFileInfo>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QVBoxLayout>

namespace nodivu {
CaptureSourceWidget::CaptureSourceWidget(SessionController& session, QString nodeId, bool compact,
                                         QWidget* parent)
    : QWidget(parent), session_(session), nodeId_(std::move(nodeId)) {
    setObjectName(QStringLiteral("captureSourceControls"));
    setStyleSheet(
        QStringLiteral(
            "QLabel#captureOrigin { color: #97a6bb; font-size: %1px; background: transparent; }"
            "QLabel#captureTiming { color: #8b95a7; font-size: 11px; background: transparent; }"
            "QComboBox { color: #dbe3ef; background: #121924; border: 1px solid #344257; "
            "border-radius: 7px; padding: 7px; font-size: %2px; }"
            "QComboBox:hover { border-color: #65748a; }"
            "QComboBox:focus { border-color: #83ddc5; }"
            "QComboBox:disabled { color: #758193; }"
            "QComboBox QAbstractItemView { background: #121924; color: #dbe3ef; "
            "selection-background-color: #243e39; selection-color: #dbe3ef; font-size: %2px; }"
            "QPushButton { font-size: %2px; padding: %3px; border: 1px solid #384353; "
            "border-radius: 7px; color: #dbe3ef; background: #262e3b; }"
            "QPushButton:enabled:hover { background: #354153; border-color: #65748a; }"
            "QPushButton:focus { border-color: #83ddc5; }"
            "QPushButton:disabled { color: #626b78; background: #202733; border-color: #2b3543; }")
            .arg(compact ? 10 : 11)
            .arg(compact ? 10 : 11)
            .arg(compact ? 6 : 8));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(5);
    auto* origin = new QLabel(QStringLiteral("Origem do áudio"), this);
    origin->setObjectName(QStringLiteral("captureOrigin"));
    layout->addWidget(origin);
    targetsCombo_ = new DeviceSelector(this);
    targetsCombo_->setObjectName(QStringLiteral("captureTarget"));
    targetsCombo_->setAccessibleName(QStringLiteral("Aplicativo ou som do PC"));
    targetsCombo_->setFixedHeight(30);
    origin->setBuddy(targetsCombo_);
    setFocusProxy(targetsCombo_);
    layout->addWidget(targetsCombo_);
    refresh_ = new QPushButton(QStringLiteral("Atualizar aplicativos"), this);
    refresh_->setObjectName(QStringLiteral("captureRefresh"));
    refresh_->setCursor(Qt::ArrowCursor);
    layout->addWidget(refresh_);
    layout->addSpacing(compact ? 7 : 12);
    status_ = new InfoMessage(compact, this);
    status_->setObjectName(QStringLiteral("captureStatus"));
    status_->setAccessibleName(QStringLiteral("Estado da captura de aplicativos"));
    connect(status_, &InfoMessage::geometryChanged, this, &CaptureSourceWidget::geometryChanged);
    layout->addWidget(status_);
    layout->addSpacing(compact ? 7 : 6);
    if (!compact) {
        timing_ = new QLabel(this);
        timing_->setObjectName(QStringLiteral("captureTiming"));
        timing_->setWordWrap(true);
        timing_->setTextFormat(Qt::PlainText);
        layout->addWidget(timing_);
    }
    connect(refresh_, &QPushButton::clicked, this, [this] { refresh(true); });
    connect(targetsCombo_, &QComboBox::activated, this, [this](int) {
        const auto token = targetsCombo_->currentData().toString();
        if (token == QStringLiteral("saved"))
            return;
        QJsonObject params{{QStringLiteral("node_id"), nodeId_},
                           {QStringLiteral("selection"), QJsonValue(QJsonValue::Null)}};
        if (token == QStringLiteral("system"))
            params.insert(QStringLiteral("selection"),
                          QJsonObject{{QStringLiteral("mode"), QStringLiteral("system")}});
        else if (!token.isEmpty())
            for (const auto& value : targets_) {
                const auto target = value.toObject();
                if (target.value(QStringLiteral("token")).toString() == token) {
                    params.insert(
                        QStringLiteral("selection"),
                        QJsonObject{{QStringLiteral("mode"), QStringLiteral("application")},
                                    {QStringLiteral("executable"),
                                     target.value(QStringLiteral("executable"))}});
                    params.insert(QStringLiteral("token"), token);
                }
            }
        configure(params, QDeadlineTimer(6000));
    });
    connect(&session_, &SessionController::changed, this, &CaptureSourceWidget::synchronize);
    synchronize();
    QTimer::singleShot(0, this, [this] { refresh(true); });
}
void CaptureSourceWidget::configure(const QJsonObject& params, QDeadlineTimer deadline) {
    if (!session_.online() || session_.node(nodeId_).isEmpty())
        return;
    if (deadline.hasExpired()) {
        error_ = QStringLiteral("A operação demorou demais. Selecione novamente.");
        synchronize();
        return;
    }
    if (!session_.settled()) {
        QTimer::singleShot(80, this, [this, params, deadline] { configure(params, deadline); });
        return;
    }
    error_.clear();
    session_.command(QStringLiteral("capture.configure"), params);
}
void CaptureSourceWidget::refresh(bool begin) {
    if (begin && !loading_) {
        loading_ = true;
        error_.clear();
        deadline_ = QDeadlineTimer(6000);
    }
    if (!session_.online() || session_.node(nodeId_).isEmpty() || deadline_.hasExpired()) {
        loading_ = false;
        error_ = QStringLiteral("Não foi possível atualizar os aplicativos. Tente novamente.");
        synchronize();
        return;
    }
    synchronize();
    if (session_.busy()) {
        // Retain both the initial request and its deadline while edits finish.
        QTimer::singleShot(100, this, [this, begin] { refresh(begin); });
        return;
    }
    const QPointer<CaptureSourceWidget> guard(this);
    session_.command(QStringLiteral("capture.targets"),
                     begin ? QJsonObject{{QStringLiteral("refresh"), true}} : QJsonObject{},
                     [guard](const BackendReply& reply) {
                         if (!guard)
                             return;
                         if (!reply.ok) {
                             guard->loading_ = false;
                             guard->error_ = reply.message;
                         } else if (reply.result.value(QStringLiteral("refreshing")).toBool()) {
                             QTimer::singleShot(100, guard, [guard] {
                                 if (guard)
                                     guard->refresh(false);
                             });
                             return;
                         } else {
                             guard->targets_ =
                                 reply.result.value(QStringLiteral("targets")).toArray();
                             guard->loading_ = false;
                         }
                         guard->synchronize();
                     });
}
void CaptureSourceWidget::synchronize() {
    const auto runtime = session_.snapshot()
                             .value(QStringLiteral("plugin_states"))
                             .toObject()
                             .value(nodeId_)
                             .toObject();
    const auto selection = runtime.value(QStringLiteral("capture_selection")).toObject();
    const auto state = runtime.value(QStringLiteral("capture")).toObject();
    QList<QPair<QString, QString>> choices{
        {QStringLiteral("Escolher aplicativo…"), QString()},
        {QStringLiteral("Som do PC · experimental · sem Nodivu"), QStringLiteral("system")}};
    QString selected;
    if (selection.value(QStringLiteral("mode")).toString() == QStringLiteral("system"))
        selected = QStringLiteral("system");
    for (const auto& value : targets_) {
        const auto target = value.toObject();
        const auto token = target.value(QStringLiteral("token")).toString();
        choices.append(
            {target.value(QStringLiteral("label")).toString() +
                 QStringLiteral(" · %1").arg(target.value(QStringLiteral("process_id")).toInt()),
             token});
        if (selection.value(QStringLiteral("mode")).toString() == QStringLiteral("application") &&
            selection.value(QStringLiteral("executable")) ==
                target.value(QStringLiteral("executable")) &&
            state.value(QStringLiteral("process_id")) == target.value(QStringLiteral("process_id")))
            selected = token;
    }
    if (selection.value(QStringLiteral("mode")).toString() == QStringLiteral("application") &&
        selected.isEmpty()) {
        selected = QStringLiteral("saved");
        choices.append(
            {QStringLiteral("Selecionado: ") +
                 QFileInfo(selection.value(QStringLiteral("executable")).toString()).fileName(),
             selected});
    }
    const QSignalBlocker blocker(targetsCombo_);
    bool same = choices.size() == targetsCombo_->count();
    for (int i = 0; same && i < targetsCombo_->count(); ++i)
        same = targetsCombo_->itemText(i) == choices[i].first &&
               targetsCombo_->itemData(i).toString() == choices[i].second;
    if (!same) {
        targetsCombo_->clear();
        for (const auto& choice : choices)
            targetsCombo_->addItem(choice.first, choice.second);
        if (auto* model = qobject_cast<QStandardItemModel*>(targetsCombo_->model()))
            if (auto* item = model->item(targetsCombo_->findData(QStringLiteral("saved"))))
                item->setEnabled(false);
    }
    targetsCombo_->setCurrentIndex(std::max(0, targetsCombo_->findData(selected)));
    targetsCombo_->setToolTip(targetsCombo_->currentText());
    targetsCombo_->setEnabled(session_.online() && !session_.busy() && !loading_);
    refresh_->setEnabled(session_.online() && !session_.busy() && !loading_);
    QString message;
    if (!error_.isEmpty())
        message = error_;
    else if (loading_)
        message = QStringLiteral("Atualizando aplicativos…");
    else if (!state.value(QStringLiteral("control_error")).toString().isEmpty())
        message = state.value(QStringLiteral("control_error")).toString();
    else if (selection.isEmpty())
        message = QStringLiteral("Selecione um aplicativo ou o som do PC.");
    else
        switch (state.value(QStringLiteral("status")).toInt()) {
        case 2:
            message =
                QStringLiteral("Capturando · conecte a saída deste bloco ao Mixer ou à Saída.");
            break;
        case 3:
            message = QStringLiteral(
                "Aplicativo ausente ou encerrado. Inicie o áudio, atualize e selecione novamente.");
            break;
        case 4:
            message =
                QStringLiteral("Há várias instâncias desse aplicativo. Atualize e escolha uma.");
            break;
        case 5:
            message = QStringLiteral(
                "Não foi possível capturar. Atualize a lista e selecione novamente.");
            if (const auto code = state.value(QStringLiteral("error")).toInteger(); code != 0)
                message += QStringLiteral(" (0x%1)").arg(static_cast<quint32>(code), 8, 16,
                                                         QLatin1Char('0'));
            break;
        default:
            message = QStringLiteral("Preparando captura…");
            break;
        }
    if (status_->toPlainText() != message) {
        status_->setMessage(message);
        emit geometryChanged();
    }
    if (timing_) {
        const bool capturing = state.value(QStringLiteral("status")).toInt() == 2;
        const auto text =
            capturing
                ? QStringLiteral("Fila: %1 ms · aquisição média: %2 ms · descartados: %3")
                      .arg(state.value(QStringLiteral("queued_frames")).toDouble() / 48, 0, 'f', 1)
                      .arg(state.value(QStringLiteral("packet_age_us")).toDouble() / 1000, 0, 'f',
                           1)
                      .arg(state.value(QStringLiteral("dropped_frames")).toInteger())
                : QString();
        if (timing_->text() != text || timing_->isHidden() == capturing) {
            timing_->setText(text);
            timing_->setVisible(capturing);
            emit geometryChanged();
        }
    }
}
} // namespace nodivu
