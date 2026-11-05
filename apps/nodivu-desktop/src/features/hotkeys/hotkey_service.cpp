#include "features/hotkeys/hotkey_service.hpp"
#include "contracts/monitor_state.hpp"
#include "contracts/playback_state.hpp"
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QRegularExpression>
#include <QSaveFile>
#include <QWidget>
#include <algorithm>
#include <cmath>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace nodivu {
QKeySequence HotkeyService::sequence(QString value) {
    return QKeySequence::fromString(
        value.replace(QStringLiteral("Control+"), QStringLiteral("Ctrl+")),
        QKeySequence::PortableText);
}
QString HotkeyService::accelerator(const QKeySequence& value) {
    return value.toString(QKeySequence::PortableText)
        .replace(QStringLiteral("Ctrl+"), QStringLiteral("Control+"));
}
QString HotkeyService::keyError(const QKeySequence& sequence) {
    if (sequence.count() != 1)
        return QStringLiteral("Escolha uma combinação única, sem sequências.");
    const auto key = sequence[0].key();
    const auto mods = sequence[0].keyboardModifiers();
    const bool ctrl = mods.testFlag(Qt::ControlModifier), alt = mods.testFlag(Qt::AltModifier);
    if ((mods & ~(Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier)) ||
        key == Qt::Key_F12 ||
        (alt && (key == Qt::Key_F4 || key == Qt::Key_Tab || key == Qt::Key_Escape ||
                 key == Qt::Key_Space)) ||
        (ctrl && key == Qt::Key_Escape) || (ctrl && alt && key == Qt::Key_Delete))
        return QStringLiteral("Combinação reservada pelo Windows.");
    if (ctrl && !alt &&
        ((key <= Qt::Key_Z &&
          QStringLiteral("SOCVXZAFPWNR").contains(QChar(static_cast<char16_t>(key)))) ||
         key == Qt::Key_Comma))
        return QStringLiteral("Combinação reservada para edição e menus.");
    if ((key >= Qt::Key_A && key <= Qt::Key_Z) || (key >= Qt::Key_0 && key <= Qt::Key_9) ||
        key == Qt::Key_Space || key == Qt::Key_Tab || key == Qt::Key_Return ||
        key == Qt::Key_Escape || key == Qt::Key_Backspace || key == Qt::Key_Delete ||
        key == Qt::Key_Insert || (key >= Qt::Key_Home && key <= Qt::Key_PageDown)) {
        if (ctrl || alt)
            return {};
    } else if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
        return {};
    return QStringLiteral(
        "Use Ctrl/Alt com uma tecla ou F1–F24 (exceto F12). Macros do mouse podem enviar F13–F24.");
}
QString HotkeyService::validate(const QJsonObject& config) {
    const auto bindings = config.value(QStringLiteral("bindings")).toArray();
    if (config.value(QStringLiteral("version")).toInt() != 1 ||
        !config.value(QStringLiteral("enabled")).isBool() ||
        !config.value(QStringLiteral("bindings")).isArray() || bindings.size() > 64 ||
        QJsonDocument(config).toJson().size() > 65536)
        return QStringLiteral("Configuração inválida: máximo de 64 atalhos / 64 KiB.");
    QSet<QString> ids, keys;
    for (const auto& item : bindings) {
        const auto b = item.toObject();
        const auto id = b.value(QStringLiteral("id")).toString();
        const auto key = sequence(b.value(QStringLiteral("accelerator")).toString());
        const auto scope = b.value(QStringLiteral("scope")).toString();
        const auto action = b.value(QStringLiteral("action")).toString();
        const auto label = b.value(QStringLiteral("label"));
        if (!QRegularExpression(QStringLiteral("^[A-Za-z0-9_-]{1,64}$")).match(id).hasMatch() ||
            ids.contains(id) ||
            (scope != QStringLiteral("global") && scope != QStringLiteral("local")) ||
            !b.value(QStringLiteral("enabled")).isBool() || action.isEmpty() ||
            action.size() > 256 || !label.isString() || label.toString().size() > 240 ||
            (b.contains(QStringLiteral("value")) &&
             (!b.value(QStringLiteral("value")).isDouble() ||
              !std::isfinite(b.value(QStringLiteral("value")).toDouble()))))
            return QStringLiteral("Associação de atalho inválida.");
        const auto error = keyError(key);
        if (!error.isEmpty())
            return error;
        const auto normalized = accelerator(key);
        if (b.value(QStringLiteral("enabled")).toBool() && keys.contains(normalized))
            return QStringLiteral("Combinação duplicada: ") + normalized;
        if (b.value(QStringLiteral("enabled")).toBool())
            keys.insert(normalized);
        ids.insert(id);
    }
    return {};
}
HotkeyService::HotkeyService(SessionController& session, QString profile, QWidget* window)
    : QObject(window), session_(session), window_(window),
      file_(QDir(profile).filePath(QStringLiteral("hotkeys.json"))),
      config_{{QStringLiteral("version"), 1},
              {QStringLiteral("enabled"), true},
              {QStringLiteral("bindings"), QJsonArray()}} {
    clock_.start();
    QFile input(file_);
    if (input.exists()) {
        if (!input.open(QIODevice::ReadOnly) || input.size() > 65536)
            loadError_ = QStringLiteral(
                "Não foi possível ler os atalhos (limite 64 KiB). Arquivo preservado.");
        else {
            const auto candidate = QJsonDocument::fromJson(input.readAll()).object();
            loadError_ = validate(candidate);
            if (loadError_.isEmpty())
                config_ = candidate;
        }
    }
    qApp->installNativeEventFilter(this);
    qApp->installEventFilter(this);
    connect(&session_, &SessionController::changed, this, [this] {
        const auto endpoint = session_.snapshot()
                                  .value(QStringLiteral("monitor"))
                                  .toObject()
                                  .value(QStringLiteral("endpoint_id"))
                                  .toString();
        if (!endpoint.isEmpty())
            monitorEndpoint_ = endpoint;
        QStringList ids;
        for (const auto& a : actions())
            ids.append(a.id);
        const auto fingerprint = ids.join(QLatin1Char('\n'));
        if (fingerprint != actionFingerprint_) {
            actionFingerprint_ = fingerprint;
            reconcile();
        }
    });
    connect(&session_, &SessionController::catalogChanged, this, &HotkeyService::reconcile);
    reconcile();
}
HotkeyService::~HotkeyService() {
    unregister();
    qApp->removeNativeEventFilter(this);
    qApp->removeEventFilter(this);
}
QVector<HotkeyAction> HotkeyService::actions() const {
    QVector<HotkeyAction> result;
    const auto add = [&](QString id, QString label, QString node, QString op, QString field = {},
                         ControlDescriptor control = {}, bool value = false) {
        result.append({id, label.left(240), node, op, field, control, value});
    };
    add(QStringLiteral("app.show"), QStringLiteral("Aplicativo · mostrar janela"), {},
        QStringLiteral("show"));
    if (session_.snapshot().isEmpty())
        return result;
    for (const auto& pair :
         {qMakePair("mute.toggle", "alternar silêncio"), qMakePair("mute.on", "silenciar tudo"),
          qMakePair("mute.off", "restaurar som"),
          qMakePair("monitor.toggle", "alternar escuta local")})
        add(QStringLiteral("app.") + QLatin1String(pair.first),
            QStringLiteral("Áudio · ") + QString::fromUtf8(pair.second), {},
            QLatin1String(pair.first));
    QSet<QString> missing;
    for (const auto& p : session_.snapshot().value(QStringLiteral("missing_plugins")).toArray())
        missing.insert(p.toObject().value(QStringLiteral("node_id")).toString());
    for (const auto& value : session_.snapshot()
                                 .value(QStringLiteral("graph"))
                                 .toObject()
                                 .value(QStringLiteral("nodes"))
                                 .toArray()) {
        const auto node = value.toObject();
        const auto id = node.value(QStringLiteral("id")).toString();
        const auto descriptor = session_.catalog().describe(node);
        if (descriptor.missing || missing.contains(id))
            continue;
        const auto prefix =
            id + QLatin1Char('/') +
            (descriptor.pluginId.isEmpty() ? descriptor.kind : descriptor.pluginId) +
            QLatin1Char('/');
        const auto label = descriptor.title + QStringLiteral(" [%1] · ").arg(id.left(8));
        if (descriptor.filePlayer) {
            add(prefix + QStringLiteral("player.play"), label + QStringLiteral("reproduzir"), id,
                QStringLiteral("player.play"));
            if (descriptor.fileTransport)
                for (const auto& pair :
                     {qMakePair("toggle", "play / pause"), qMakePair("pause", "pausar"),
                      qMakePair("restart", "recomeçar"),
                      qMakePair("stop", "parar e voltar ao início")})
                    add(prefix + QStringLiteral("player.") + QLatin1String(pair.first),
                        label + QString::fromUtf8(pair.second), id,
                        QStringLiteral("player.") + QLatin1String(pair.first));
        }
        for (const auto& c : descriptor.controls) {
            if (c.readonly || c.kind == ControlKind::Momentary || c.kind == ControlKind::Device)
                continue;
            if (c.kind == ControlKind::Toggle)
                add(prefix + c.key + QStringLiteral(".toggle"),
                    label + c.label + QStringLiteral(" · alternar"), id, QStringLiteral("toggle"),
                    c.key, c);
            else {
                add(prefix + c.key + QStringLiteral(".set"),
                    label + c.label + QStringLiteral(" · definir"), id, QStringLiteral("set"),
                    c.key, c, true);
                if (c.kind != ControlKind::Select)
                    add(prefix + c.key + QStringLiteral(".adjust"),
                        label + c.label + QStringLiteral(" · somar (+/−)"), id,
                        QStringLiteral("adjust"), c.key, c, true);
            }
        }
    }
    return result;
}
void HotkeyService::unregister() {
#ifdef Q_OS_WIN
    for (auto it = registered_.cbegin(); it != registered_.cend(); ++it)
        UnregisterHotKey(nullptr, it.key());
#endif
    registered_.clear();
}
void HotkeyService::reconcile() {
    unregister();
    states_.clear();
    QSet<QString> available;
    for (const auto& a : actions())
        available.insert(a.id);
    int nativeId = 0x5100;
    for (const auto& item : config_.value(QStringLiteral("bindings")).toArray()) {
        const auto b = item.toObject();
        const auto id = b.value(QStringLiteral("id")).toString();
        QString state;
        if (!config_.value(QStringLiteral("enabled")).toBool() ||
            !b.value(QStringLiteral("enabled")).toBool())
            state = QStringLiteral("Desativado");
        else if (!available.contains(b.value(QStringLiteral("action")).toString()))
            state = QStringLiteral("Bloco/ação ausente neste projeto");
        else if (suspended_)
            state = QStringLiteral("Suspenso durante configuração");
        else if (b.value(QStringLiteral("scope")).toString() == QStringLiteral("local"))
            state = QStringLiteral("Ativo somente no Nodivu");
        else {
#ifdef Q_OS_WIN
            const auto key = sequence(b.value(QStringLiteral("accelerator")).toString())[0];
            const auto mods = key.keyboardModifiers();
            UINT flags = MOD_NOREPEAT | (mods.testFlag(Qt::ControlModifier) ? MOD_CONTROL : 0) |
                         (mods.testFlag(Qt::AltModifier) ? MOD_ALT : 0) |
                         (mods.testFlag(Qt::ShiftModifier) ? MOD_SHIFT : 0);
            UINT vk = 0;
            const auto k = key.key();
            if (k >= Qt::Key_A && k <= Qt::Key_Z)
                vk = UINT(k);
            else if (k >= Qt::Key_0 && k <= Qt::Key_9)
                vk = UINT(k);
            else if (k >= Qt::Key_F1 && k <= Qt::Key_F24)
                vk = VK_F1 + UINT(k - Qt::Key_F1);
            else {
                const QHash<int, UINT> map{
                    {Qt::Key_Space, VK_SPACE},    {Qt::Key_Tab, VK_TAB},
                    {Qt::Key_Return, VK_RETURN},  {Qt::Key_Escape, VK_ESCAPE},
                    {Qt::Key_Backspace, VK_BACK}, {Qt::Key_Delete, VK_DELETE},
                    {Qt::Key_Insert, VK_INSERT},  {Qt::Key_Home, VK_HOME},
                    {Qt::Key_End, VK_END},        {Qt::Key_Left, VK_LEFT},
                    {Qt::Key_Up, VK_UP},          {Qt::Key_Right, VK_RIGHT},
                    {Qt::Key_Down, VK_DOWN},      {Qt::Key_PageUp, VK_PRIOR},
                    {Qt::Key_PageDown, VK_NEXT}};
                vk = map.value(k);
            }
            if (vk && RegisterHotKey(nullptr, nativeId, flags, vk)) {
                registered_.insert(nativeId++, id);
                state = QStringLiteral("Global ativo");
            } else
                state = QStringLiteral("Conflito: combinação ocupada ou recusada pelo Windows");
#else
            state = QStringLiteral("Atalho global disponível somente no Windows");
#endif
        }
        states_.insert(id, state);
    }
    emit statusChanged();
}
QString HotkeyService::save(const QJsonObject& config) {
    const auto error = validate(config);
    if (!error.isEmpty())
        return error;
    if (!QDir().mkpath(QFileInfo(file_).absolutePath()))
        return QStringLiteral("Pasta de atalhos indisponível.");
    if (!loadError_.isEmpty() && QFile::exists(file_) &&
        !QFile::copy(file_,
                     file_ +
                         QStringLiteral(".preserved-%1").arg(QDateTime::currentMSecsSinceEpoch())))
        return QStringLiteral("Não foi possível preservar o arquivo inválido.");
    QSaveFile output(file_);
    output.setDirectWriteFallback(false);
    const auto bytes = QJsonDocument(config).toJson();
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() ||
        !output.commit())
        return QStringLiteral("Falha ao salvar os atalhos.");
    config_ = config;
    loadError_.clear();
    last_.clear();
    reconcile();
    return {};
}
void HotkeyService::suspend(bool value) {
    suspended_ = value;
    reconcile();
}
bool HotkeyService::nativeEventFilter(const QByteArray& type, void* message, qintptr* result) {
#ifdef Q_OS_WIN
    if (type == QByteArrayLiteral("windows_generic_MSG") ||
        type == QByteArrayLiteral("windows_dispatcher_MSG")) {
        const auto* msg = static_cast<MSG*>(message);
        if (msg->message == WM_HOTKEY && registered_.contains(int(msg->wParam))) {
            const auto id = registered_.value(int(msg->wParam));
            // Return to Qt before showing windows or issuing a backend command.
            QTimer::singleShot(0, this, [this, id] { fire(id); });
            *result = 0;
            return true;
        }
    }
#else
    Q_UNUSED(type);
    Q_UNUSED(message);
    Q_UNUSED(result);
#endif
    return false;
}
bool HotkeyService::eventFilter(QObject*, QEvent* event) {
    if (suspended_ || !window_->isActiveWindow() || QApplication::activeModalWidget() ||
        event->type() != QEvent::KeyPress)
        return false;
    const auto* key = static_cast<QKeyEvent*>(event);
    if (key->isAutoRepeat())
        return false;
    const auto sequence = QKeySequence(key->keyCombination());
    for (const auto& item : config_.value(QStringLiteral("bindings")).toArray()) {
        const auto b = item.toObject();
        if (b.value(QStringLiteral("scope")).toString() == QStringLiteral("local") &&
            b.value(QStringLiteral("enabled")).toBool() &&
            config_.value(QStringLiteral("enabled")).toBool() &&
            HotkeyService::sequence(b.value(QStringLiteral("accelerator")).toString()) ==
                sequence &&
            states_.value(b.value(QStringLiteral("id")).toString()) ==
                QStringLiteral("Ativo somente no Nodivu")) {
            fire(b.value(QStringLiteral("id")).toString());
            return true;
        }
    }
    return false;
}
void HotkeyService::finish(bool ok, const QString& message) {
    pending_ = false;
    emit result(ok, message);
}
void HotkeyService::fire(const QString& id, bool test) {
    QJsonObject binding;
    for (const auto& item : config_.value(QStringLiteral("bindings")).toArray())
        if (item.toObject().value(QStringLiteral("id")).toString() == id)
            binding = item.toObject();
    if (binding.isEmpty() ||
        (!test && (suspended_ || !config_.value(QStringLiteral("enabled")).toBool() ||
                   !binding.value(QStringLiteral("enabled")).toBool())))
        return;
    if (pending_) {
        emit result(false, QStringLiteral("Outra ação está em andamento; atalho ignorado."));
        return;
    }
    if (!test && last_.contains(id) && clock_.elapsed() - last_.value(id) < 250)
        return;
    last_.insert(id, clock_.elapsed());
    for (const auto& a : actions())
        if (a.id == binding.value(QStringLiteral("action")).toString()) {
            pending_ = true;
            deadline_ = QDeadlineTimer(7000);
            if (a.operation == QStringLiteral("show"))
                execute(a, binding);
            else
                command(QStringLiteral("session.snapshot"), {}, [this, binding] {
                    for (const auto& current : actions())
                        if (current.id == binding.value(QStringLiteral("action")).toString()) {
                            execute(current, binding);
                            return;
                        }
                    finish(false, QStringLiteral("O bloco não está mais disponível."));
                });
            return;
        }
    emit result(false, QStringLiteral("Bloco/ação ausente neste projeto."));
}
void HotkeyService::command(const QString& name, const QJsonObject& params,
                            std::function<void()> next) {
    if (deadline_.hasExpired() || (blocked_ && blocked_()) || !session_.settled()) {
        finish(false, QStringLiteral("Ação expirada ou projeto ocupado; etapa não executada."));
        return;
    }
    session_.command(name, params, [this, next = std::move(next)](const BackendReply& reply) {
        if (!reply.ok)
            finish(false, reply.message);
        else if (next)
            next();
        else
            finish(true, QStringLiteral("Atalho executado."));
    });
}
void HotkeyService::execute(const HotkeyAction& a, const QJsonObject& b) {
    if (a.operation == QStringLiteral("show")) {
        window_->showNormal();
        window_->raise();
        window_->activateWindow();
        finish(true, QStringLiteral("Janela aberta."));
        return;
    }
    if (!session_.settled() || (blocked_ && blocked_())) {
        finish(false, QStringLiteral("Aguarde o projeto ou reconecte o motor."));
        return;
    }
    if (a.operation.startsWith(QStringLiteral("mute."))) {
        const bool muted = a.operation == QStringLiteral("mute.on") ||
                           (a.operation == QStringLiteral("mute.toggle") &&
                            !session_.snapshot().value(QStringLiteral("muted")).toBool());
        command(QStringLiteral("audio.mute"), {{QStringLiteral("muted"), muted}});
        return;
    }
    if (a.operation == QStringLiteral("monitor.toggle")) {
        const auto state =
            monitorState(session_.snapshot(), session_.devices(), true, false, monitorEndpoint_);
        if (!state.toggleReason.isEmpty()) {
            finish(false, state.toggleReason);
            return;
        }
        command(QStringLiteral("audio.monitor"),
                {{QStringLiteral("endpoint_id"),
                  state.active ? QJsonValue(QJsonValue::Null) : QJsonValue(monitorEndpoint_)}});
        return;
    }
    const auto node = session_.node(a.node);
    if (a.operation.startsWith(QStringLiteral("player."))) {
        const auto state =
            playbackState(session_.snapshot(), node, session_.devices(), true, false);
        const bool pause = a.operation == QStringLiteral("player.pause") ||
                           a.operation == QStringLiteral("player.stop") ||
                           (a.operation == QStringLiteral("player.toggle") && state.playing);
        if (!pause && !state.canPlay) {
            finish(false, state.reason);
            return;
        }
        if (pause) {
            command(QStringLiteral("plugin.command"),
                    {{QStringLiteral("action"), QStringLiteral("pause")},
                     {QStringLiteral("node_id"), a.node}},
                    a.operation == QStringLiteral("player.stop") ? std::function<void()>([this, a] {
                        command(QStringLiteral("plugin.command"),
                                {{QStringLiteral("action"), QStringLiteral("seek")},
                                 {QStringLiteral("node_id"), a.node},
                                 {QStringLiteral("position_ms"), 0}});
                    })
                                                                 : std::function<void()>());
        } else if (a.operation == QStringLiteral("player.restart")) {
            command(QStringLiteral("plugin.command"),
                    {{QStringLiteral("action"), QStringLiteral("seek")},
                     {QStringLiteral("node_id"), a.node},
                     {QStringLiteral("position_ms"), 0}},
                    [this, a] {
                        command(QStringLiteral("plugin.command"),
                                {{QStringLiteral("action"), QStringLiteral("play")},
                                 {QStringLiteral("node_id"), a.node}});
                    });
        } else
            command(QStringLiteral("plugin.command"),
                    {{QStringLiteral("action"), QStringLiteral("play")},
                     {QStringLiteral("node_id"), a.node}});
        return;
    }
    auto graph = session_.snapshot().value(QStringLiteral("graph")).toObject();
    auto nodes = graph.value(QStringLiteral("nodes")).toArray();
    for (qsizetype i = 0; i < nodes.size(); ++i) {
        auto n = nodes[i].toObject();
        if (n.value(QStringLiteral("id")).toString() != a.node)
            continue;
        auto block = n.value(QStringLiteral("block")).toObject();
        const auto previous = fieldValue(block, a.field);
        QJsonValue value;
        if (a.operation == QStringLiteral("toggle")) {
            const bool next = !(previous.isBool() ? previous.toBool() : previous.toDouble() != 0);
            value = a.field.startsWith(QStringLiteral("parameter:")) ? QJsonValue(next ? 1 : 0)
                                                                     : QJsonValue(next);
        } else {
            if (!b.value(QStringLiteral("value")).isDouble()) {
                finish(false, QStringLiteral("Informe um valor numérico."));
                return;
            }
            double number = b.value(QStringLiteral("value")).toDouble();
            if (a.operation == QStringLiteral("adjust"))
                number =
                    std::clamp(previous.toDouble() + number, a.control.minimum, a.control.maximum);
            bool option = a.control.kind != ControlKind::Select;
            for (const auto& item : a.control.options)
                option |= item.toObject().value(QStringLiteral("value")).toDouble() == number;
            if (!std::isfinite(number) || number < a.control.minimum ||
                number > a.control.maximum || !option ||
                (a.control.stepped && std::floor(number) != number)) {
                finish(false, QStringLiteral("Valor fora do contrato do controle."));
                return;
            }
            value = number;
        }
        setFieldValue(block, a.field, value);
        n.insert(QStringLiteral("block"), block);
        nodes[i] = n;
    }
    graph.insert(QStringLiteral("nodes"), nodes);
    command(QStringLiteral("graph.apply"),
            {{QStringLiteral("graph"), graph},
             {QStringLiteral("expected_revision"),
              session_.snapshot().value(QStringLiteral("revision"))}},
            [this] {
                emit session_.documentEdited();
                finish(true, QStringLiteral("Parâmetro ajustado."));
            });
}
} // namespace nodivu
