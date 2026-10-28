#include "features/projects/project_controller.hpp"
#include "features/graph/graph_scene.hpp"
#include "features/graph/graph_view.hpp"
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QLocale>
#include <QtConcurrentRun>
#include <algorithm>

namespace nodivu {
namespace {
QJsonObject editDocument(QJsonObject document) {
    document.remove(QStringLiteral("camera"));
    auto positions = document.value(QStringLiteral("positions")).toArray();
    QVector<QJsonObject> sorted;
    for (const auto& value : positions)
        sorted.append(value.toObject());
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        return a.value(QStringLiteral("node_id")).toString() <
               b.value(QStringLiteral("node_id")).toString();
    });
    positions = {};
    for (const auto& value : sorted)
        positions.append(value);
    document.insert(QStringLiteral("positions"), positions);
    return document;
}
QJsonObject editKey(QJsonObject document) {
    // File transport is transient. Keep references for restored nodes without
    // turning playback/file discovery notifications into edit commands.
    document.remove(QStringLiteral("resources"));
    return document;
}
QJsonObject topology(const QJsonObject& document) {
    auto graph = document.value(QStringLiteral("graph")).toObject();
    QJsonArray ids;
    for (const auto& node : graph.value(QStringLiteral("nodes")).toArray())
        ids.append(node.toObject().value(QStringLiteral("id")));
    graph.insert(QStringLiteral("nodes"), ids);
    return graph;
}
} // namespace

ProjectController::ProjectController(SessionController& session, GraphScene& scene, GraphView& view,
                                     AppPaths paths, QWidget* window)
    : QObject(window), session_(session), scene_(scene), view_(view), paths_(std::move(paths)),
      window_(window) {
    ioPool_.setMaxThreadCount(1);
    historyTimer_.setSingleShot(true);
    historyTimer_.setInterval(240);
    connect(&historyTimer_, &QTimer::timeout, this, &ProjectController::commitHistory);
    const QFileInfo recovery(recoveryFile());
    recoveryPending_ = recovery.exists();
    recoverySavedAt_ = recovery.lastModified();
    checkpointTimer_.setInterval(5000);
    connect(&checkpointTimer_, &QTimer::timeout, this, &ProjectController::checkpoint);
    checkpointTimer_.start();
    connect(&session_, &SessionController::documentEdited, this, &ProjectController::updateDirty);
    connect(&session_, &SessionController::changed, this, &ProjectController::updateDirty);
    connect(&session_, &SessionController::disconnected, this,
            &ProjectController::protectAfterFault);
    connect(&session_, &SessionController::devicesRefreshed, this, [this](bool success) {
        if (!retryAfterDiscovery_)
            return;
        retryAfterDiscovery_ = false;
        if (success)
            retryPreparedAudio();
        else {
            operation_ = false;
            emit stateChanged();
        }
    });
    connect(&scene_, &GraphScene::positionsEdited, this, &ProjectController::updateDirty);
    connect(&view_, &GraphView::cameraEdited, this, &ProjectController::updateDirty);
    const auto recentFile = QDir(paths_.profile).filePath(QStringLiteral("recent-projects.json"));
    if (QFileInfo::exists(recentFile))
        io([recentFile] { return ProjectRepository::read(recentFile); },
           [this](const FileResult& result) {
               if (!result.ok()) {
                   emit error(result.error);
                   return;
               }
               const auto data = QJsonDocument::fromJson(result.contents).object();
               const auto files = data.value(QStringLiteral("files")).toArray();
               if (data.value(QStringLiteral("version")).toInt() != 1 ||
                   !data.value(QStringLiteral("files")).isArray() || files.size() > 10) {
                   emit error(QStringLiteral("Lista de recentes inválida; original preservado."));
                   return;
               }
               for (const auto& file : files)
                   if (!file.isString() || !QDir::isAbsolutePath(file.toString()) ||
                       file.toString().contains(QChar::Null) ||
                       file.toString().toUtf8().size() > 4096) {
                       emit error(
                           QStringLiteral("Caminho inválido nos recentes; original preservado."));
                       return;
                   }
               for (const auto& file : files)
                   recents_.append(QDir::cleanPath(file.toString()));
               emit stateChanged();
           });
}
QString ProjectController::recoveryFile() const {
    return QDir(paths_.profile).filePath(QStringLiteral("recovery/recovery.nodivu.json"));
}
QString ProjectController::recoveryStatus() const {
    const auto date =
        QLocale(QLocale::Portuguese, QLocale::Brazil)
            .toString(recoverySavedAt_.toLocalTime(), QStringLiteral("dd/MM/yyyy HH:mm:ss"));
    if (!recoveryError_.isEmpty())
        return recoveryError_ + (recoverySavedAt_.isValid()
                                     ? QStringLiteral("\nÚltima cópia gravada: ") + date
                                     : QString());
    if (recoveryPending_)
        return QStringLiteral(
                   "Há uma cópia de %1. Restaure ou descarte antes de criar outra cópia.")
            .arg(recoverySavedAt_.isValid() ? date : QStringLiteral("um projeto não salvo"));
    if (recoverySavedAt_.isValid())
        return QStringLiteral("Cópia automática: %1. Use Salvar para guardar seu projeto.")
            .arg(date);
    return QStringLiteral("Alterações são copiadas a cada 5 segundos. Não substitui Salvar.");
}
void ProjectController::recordRecoveryResult(const FileResult& result) {
    recoveryError_ =
        result.ok() ? QString() : QStringLiteral("Falha na cópia automática: ") + result.error;
    if (result.ok())
        recoverySavedAt_ = result.writtenAt;
}
void ProjectController::io(std::function<FileResult()> work,
                           std::function<void(const FileResult&)> completion) {
    if (ioBusy_) {
        emit error(QStringLiteral("Aguarde a operação de arquivo."));
        return;
    }
    ioBusy_ = true;
    emit stateChanged();
    auto* watcher = new QFutureWatcher<FileResult>(this);
    connect(watcher, &QFutureWatcher<FileResult>::finished, this,
            [this, watcher, completion = std::move(completion)] {
                const auto result = watcher->result();
                watcher->deleteLater();
                ioBusy_ = false;
                completion(result);
                emit stateChanged();
                if (faultProtectionPending_)
                    QTimer::singleShot(0, this, &ProjectController::protectAfterFault);
            });
    watcher->setFuture(QtConcurrent::run(&ioPool_, std::move(work)));
}
QString ProjectController::resourcePath(const QString& id) const {
    const auto file = session_.snapshot()
                          .value(QStringLiteral("plugin_states"))
                          .toObject()
                          .value(id)
                          .toObject()
                          .value(QStringLiteral("file"))
                          .toString();
    return file.isEmpty() ? resources_.value(id) : file;
}
QJsonObject ProjectController::document() const {
    const auto snapshot = session_.snapshot();
    QJsonArray resources, captures;
    const auto states = snapshot.value(QStringLiteral("plugin_states")).toObject();
    for (const auto& value : snapshot.value(QStringLiteral("graph"))
                                 .toObject()
                                 .value(QStringLiteral("nodes"))
                                 .toArray()) {
        const auto node = value.toObject();
        const auto id = node.value(QStringLiteral("id")).toString();
        const auto runtime = states.value(id).toObject();
        auto file = runtime.value(QStringLiteral("file")).toString();
        if (file.isEmpty())
            file = resources_.value(id);
        if (!file.isEmpty())
            resources.append(
                QJsonObject{{QStringLiteral("node_id"), id}, {QStringLiteral("path"), file}});
        // A present null selection is an explicit clear, not a reason to restore
        // an older persisted intent. Missing plugins still use the saved fallback.
        const auto capture = runtime.contains(QStringLiteral("capture_selection"))
                                 ? runtime.value(QStringLiteral("capture_selection")).toObject()
                                 : captures_.value(id);
        if (!capture.isEmpty())
            captures.append(QJsonObject{{QStringLiteral("node_id"), id},
                                        {QStringLiteral("selection"), capture}});
    }
    return {{QStringLiteral("format"), QStringLiteral("nodivu-project")},
            {QStringLiteral("version"), 1},
            {QStringLiteral("graph"), snapshot.value(QStringLiteral("graph"))},
            {QStringLiteral("positions"), scene_.positions()},
            {QStringLiteral("camera"), view_.camera()},
            {QStringLiteral("resources"), resources},
            {QStringLiteral("captures"), captures}};
}
bool ProjectController::dirty() const {
    return !savedStamp_.isEmpty() &&
           QJsonDocument(document()).toJson(QJsonDocument::Compact) != savedStamp_;
}
void ProjectController::updateDirty() {
    if (restoring_ || !session_.online())
        return;
    if (savedStamp_.isEmpty())
        savedStamp_ = QJsonDocument(document()).toJson(QJsonDocument::Compact);
    queueHistory();
    emit stateChanged();
}
bool ProjectController::canUndo() const {
    return historyIndex_ > 0 || (!pendingHistory_.isEmpty() && historyIndex_ >= 0 &&
                                 editKey(pendingHistory_) != editKey(history_[historyIndex_]));
}
bool ProjectController::canRedo() const {
    return pendingHistory_.isEmpty() && historyIndex_ >= 0 && historyIndex_ + 1 < history_.size();
}
void ProjectController::resetHistory() {
    historyTimer_.stop();
    pendingHistory_ = {};
    history_ = {editDocument(document())};
    historyIndex_ = 0;
}
void ProjectController::queueHistory() {
    if (historyApplying_ || restoring_ || busy() || resetHistoryAfterOpen_ || !session_.online())
        return;
    const auto next = editDocument(document());
    if (historyIndex_ < 0) {
        resetHistory();
        return;
    }
    if (editKey(next) == editKey(history_[historyIndex_])) {
        history_[historyIndex_] = next;
        pendingHistory_ = {};
        historyTimer_.stop();
        return;
    }
    if (next == pendingHistory_)
        return;
    // Consecutive topology operations are distinct; drags and slider changes
    // coalesce until idle, avoiding a history entry for every pixel/sample.
    if (!pendingHistory_.isEmpty() && topology(next) != topology(pendingHistory_))
        commitHistory();
    pendingHistory_ = next;
    historyTimer_.start();
}
void ProjectController::commitHistory() {
    if (historyApplying_ || restoring_ || pendingHistory_.isEmpty())
        return;
    if (!session_.settled() || busy()) {
        historyTimer_.start();
        return;
    }
    historyTimer_.stop();
    history_.resize(historyIndex_ + 1);
    history_.append(pendingHistory_);
    pendingHistory_ = {};
    historyIndex_ = static_cast<int>(history_.size()) - 1;
    qsizetype bytes = 0;
    for (const auto& item : history_)
        bytes += QJsonDocument(item).toJson(QJsonDocument::Compact).size();
    // Bounded frontend-only snapshots; never retain PCM, live handles or telemetry.
    while (history_.size() > 1 && (history_.size() > 100 || bytes > 8LL * 1024 * 1024)) {
        bytes -= QJsonDocument(history_.first()).toJson(QJsonDocument::Compact).size();
        history_.removeFirst();
        --historyIndex_;
    }
    emit stateChanged();
}
void ProjectController::undo() {
    if (busy() || !session_.settled() || historyApplying_)
        return;
    commitHistory();
    if (historyIndex_ > 0)
        restoreHistory(historyIndex_ - 1);
}
void ProjectController::redo() {
    if (busy() || !session_.settled() || historyApplying_)
        return;
    commitHistory();
    if (canRedo())
        restoreHistory(historyIndex_ + 1);
}
void ProjectController::restoreHistory(int index) {
    const auto target = history_[index];
    const auto before = document();
    QJsonArray commands;
    for (const auto& entry : target.value(QStringLiteral("resources")).toArray()) {
        const auto resource = entry.toObject();
        const auto id = resource.value(QStringLiteral("node_id")).toString();
        QJsonObject targetNode;
        for (const auto& value : target.value(QStringLiteral("graph"))
                                     .toObject()
                                     .value(QStringLiteral("nodes"))
                                     .toArray())
            if (value.toObject().value(QStringLiteral("id")) == id)
                targetNode = value.toObject();
        const auto descriptor = session_.catalog().describe(targetNode);
        if (session_.node(id).isEmpty() && descriptor.filePlayer && !descriptor.missing)
            commands.append(QJsonObject{
                {QStringLiteral("method"), QStringLiteral("plugin.command")},
                {QStringLiteral("params"),
                 QJsonObject{{QStringLiteral("node_id"), id},
                             {QStringLiteral("action"), QStringLiteral("load")},
                             {QStringLiteral("path"), resource.value(QStringLiteral("path"))}}}});
    }
    const auto targetCaptures = target.value(QStringLiteral("captures")).toArray();
    const auto beforeCaptures = before.value(QStringLiteral("captures")).toArray();
    for (const auto& node : target.value(QStringLiteral("graph"))
                                .toObject()
                                .value(QStringLiteral("nodes"))
                                .toArray()) {
        const auto id = node.toObject().value(QStringLiteral("id")).toString();
        QJsonObject selection, previous;
        for (const auto& entry : targetCaptures)
            if (entry.toObject().value(QStringLiteral("node_id")) == id)
                selection = entry.toObject().value(QStringLiteral("selection")).toObject();
        for (const auto& entry : beforeCaptures)
            if (entry.toObject().value(QStringLiteral("node_id")) == id)
                previous = entry.toObject().value(QStringLiteral("selection")).toObject();
        if (selection != previous && session_.catalog().describe(node.toObject()).captureSource)
            commands.append(
                QJsonObject{{QStringLiteral("method"), QStringLiteral("capture.configure")},
                            {QStringLiteral("params"),
                             QJsonObject{{QStringLiteral("node_id"), id},
                                         {QStringLiteral("selection"),
                                          selection.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                              : QJsonValue(selection)}}}});
    }
    historyApplying_ = true;
    restoring_ = true;
    operation_ = true;
    emit stateChanged();
    const auto applied = [this, target, commands, index](const BackendReply& reply) {
        if (!reply.ok) {
            historyApplying_ = restoring_ = operation_ = false;
            emit stateChanged();
            return;
        }
        for (const auto& entry : target.value(QStringLiteral("resources")).toArray())
            resources_.insert(entry.toObject().value(QStringLiteral("node_id")).toString(),
                              entry.toObject().value(QStringLiteral("path")).toString());
        captures_.clear();
        for (const auto& entry : target.value(QStringLiteral("captures")).toArray())
            captures_.insert(entry.toObject().value(QStringLiteral("node_id")).toString(),
                             entry.toObject().value(QStringLiteral("selection")).toObject());
        scene_.restorePositions(target.value(QStringLiteral("positions")).toArray());
        restoreHistoryResources(index, commands, 0);
    };
    if (before.value(QStringLiteral("graph")) == target.value(QStringLiteral("graph"))) {
        BackendReply reply;
        reply.ok = true;
        applied(reply);
    } else
        session_.command(QStringLiteral("graph.apply"),
                         {{QStringLiteral("expected_revision"),
                           session_.snapshot().value(QStringLiteral("revision"))},
                          {QStringLiteral("graph"), target.value(QStringLiteral("graph"))}},
                         applied);
}
void ProjectController::restoreHistoryResources(int historyIndex, QJsonArray commands,
                                                qsizetype index) {
    if (index >= commands.size()) {
        historyIndex_ = historyIndex;
        history_[historyIndex_] = editDocument(document());
        historyApplying_ = restoring_ = operation_ = false;
        emit stateChanged();
        return;
    }
    const auto command = commands[index].toObject();
    session_.command(command.value(QStringLiteral("method")).toString(),
                     command.value(QStringLiteral("params")).toObject(),
                     [this, commands, index, historyIndex](const BackendReply&) {
                         restoreHistoryResources(historyIndex, commands, index + 1);
                     });
}

bool ProjectController::confirmDiscard(PendingChangesAction action) {
    return !dirty() || confirmPendingChanges(window_, action);
}
bool ProjectController::finishClose() {
    checkpointTimer_.stop();
    // An older or crash-protected copy belongs to the recovery prompt until the
    // user restores or discards it. A normal exit may remove only our own copy.
    if (recoveryPending_)
        return true;
    const auto result = ProjectRepository::removeRecovery(recoveryFile());
    recordRecoveryResult(result);
    if (!result.ok()) {
        emit error(result.error);
        checkpointTimer_.start();
    }
    return result.ok();
}
void ProjectController::protectAfterFault() {
    faultProtectionPending_ = busy();
    if (faultProtectionPending_) {
        // A failed command may be completing a save/open callback. Retry once that
        // operation releases its snapshot instead of losing crash recovery.
        QTimer::singleShot(100, this, [this] {
            if (faultProtectionPending_)
                protectAfterFault();
        });
        return;
    }
    if (recoveryPending_ || savedStamp_.isEmpty() || !dirty())
        return;
    recoveryPending_ = true;
    const auto project = document();
    const auto file = recoveryFile();
    io([file, project] { return ProjectRepository::write(file, project); },
       [this](const FileResult& result) {
           recordRecoveryResult(result);
           if (!result.ok())
               emit error(QStringLiteral("Não foi possível proteger a recuperação: ") +
                          result.error);
       });
    emit stateChanged();
}
void ProjectController::save(const QString& requestedFile) {
    if (busy() || !session_.settled()) {
        emit error(QStringLiteral("Aguarde a aplicação dos ajustes antes de salvar."));
        return;
    }
    commitHistory();
    QString file = requestedFile.isEmpty() ? path_ : requestedFile;
    if (file.isEmpty())
        file = QFileDialog::getSaveFileName(
            window_, QStringLiteral("Salvar projeto"),
            path_.isEmpty() ? QDir(paths_.projects).filePath(QStringLiteral("Projeto.nodivu.json"))
                            : path_,
            QStringLiteral("Projeto Nodivu (*.nodivu.json)"));
    if (file.isEmpty())
        return;
    operation_ = true;
    const auto contents = QJsonDocument(document()).toJson(QJsonDocument::Compact);
    session_.command(QStringLiteral("project.validate"),
                     {{QStringLiteral("contents"), QString::fromUtf8(contents)},
                      {QStringLiteral("expected_revision"),
                       session_.snapshot().value(QStringLiteral("revision"))}},
                     [this, file, contents](const BackendReply& reply) {
                         if (!reply.ok) {
                             operation_ = false;
                             return;
                         }
                         const auto project = ProjectRepository::resolveResources(
                             reply.result.value(QStringLiteral("project")).toObject(), file, false);
                         io([file, project] { return ProjectRepository::write(file, project); },
                            [this, file, contents](const FileResult& result) {
                                operation_ = false;
                                if (!result.ok()) {
                                    emit error(result.error);
                                    return;
                                }
                                path_ = file;
                                savedAt_ = result.writtenAt;
                                savedStamp_ = contents;
                                if (!dirty() && !recoveryPending_) {
                                    const auto removed =
                                        ProjectRepository::removeRecovery(recoveryFile());
                                    recordRecoveryResult(removed);
                                    if (!removed.ok())
                                        emit error(removed.error);
                                }
                                remember(file);
                                emit stateChanged();
                            });
                     });
}
void ProjectController::saveAs() {
    if (busy() || !session_.settled())
        return;
    const auto file = QFileDialog::getSaveFileName(
        window_, QStringLiteral("Salvar projeto como"),
        path_.isEmpty() ? QDir(paths_.projects).filePath(QStringLiteral("Projeto.nodivu.json"))
                        : path_,
        QStringLiteral("Projeto Nodivu (*.nodivu.json)"));
    if (!file.isEmpty())
        save(file);
}

void ProjectController::open(const QString& requestedFile) {
    if (busy() || !session_.settled() || !confirmDiscard(PendingChangesAction::Open))
        return;
    const auto file = requestedFile.isEmpty()
                          ? QFileDialog::getOpenFileName(window_, QStringLiteral("Abrir projeto"),
                                                         paths_.projects,
                                                         QStringLiteral("Projeto Nodivu (*.json)"))
                          : requestedFile;
    if (file.isEmpty())
        return;
    operation_ = true;
    emit stateChanged();
    io([file] { return ProjectRepository::read(file); },
       [this, file](const FileResult& result) {
           if (!result.ok()) {
               operation_ = false;
               emit error(result.error);
               return;
           }
           openContents(result.contents, file, false);
       });
}
void ProjectController::openContents(const QByteArray& contents, const QString& file,
                                     bool recovery) {
    session_.command(
        QStringLiteral("project.validate"),
        {{QStringLiteral("contents"), QString::fromUtf8(contents)},
         {QStringLiteral("expected_revision"),
          session_.snapshot().value(QStringLiteral("revision"))}},
        [this, file, recovery](const BackendReply& validated) {
            if (!validated.ok) {
                operation_ = false;
                return;
            }
            const auto project = ProjectRepository::resolveResources(
                validated.result.value(QStringLiteral("project")).toObject(), file, true);
            session_.command(
                QStringLiteral("project.open"),
                {{QStringLiteral("contents"),
                  QString::fromUtf8(QJsonDocument(project).toJson(QJsonDocument::Compact))},
                 {QStringLiteral("expected_revision"),
                  session_.snapshot().value(QStringLiteral("revision"))}},
                [this, project, file, recovery](const BackendReply& reply) {
                    if (!reply.ok) {
                        operation_ = false;
                        return;
                    }
                    restoring_ = true;
                    resetHistoryAfterOpen_ = true;
                    historyTimer_.stop();
                    pendingHistory_ = {};
                    resources_.clear();
                    captures_.clear();
                    for (const auto& value : project.value(QStringLiteral("resources")).toArray())
                        resources_.insert(
                            value.toObject().value(QStringLiteral("node_id")).toString(),
                            value.toObject().value(QStringLiteral("path")).toString());
                    for (const auto& value : project.value(QStringLiteral("captures")).toArray())
                        captures_.insert(
                            value.toObject().value(QStringLiteral("node_id")).toString(),
                            value.toObject().value(QStringLiteral("selection")).toObject());
                    session_.restoreSnapshot(
                        reply.result.value(QStringLiteral("snapshot")).toObject());
                    scene_.restorePositions(project.value(QStringLiteral("positions")).toArray());
                    view_.restoreCamera(project.value(QStringLiteral("camera")).toObject());
                    path_ = recovery ? QString() : file;
                    savedAt_ = recovery ? QDateTime() : QFileInfo(file).lastModified();
                    if (recovery)
                        recoveryPending_ = false;
                    recoveryStamp_.clear();
                    if (!recovery && !recoveryPending_) {
                        const auto removed = ProjectRepository::removeRecovery(recoveryFile());
                        recordRecoveryResult(removed);
                        if (!removed.ok())
                            emit error(removed.error);
                    }
                    savedStamp_ = recovery
                                      ? QByteArray("unsaved-recovery")
                                      : QJsonDocument(document()).toJson(QJsonDocument::Compact);
                    restoring_ = false;
                    if (!recovery)
                        remember(file);
                    session_.command(
                        QStringLiteral("audio.retry"), {}, [this, project](const BackendReply&) {
                            prepareResources(project.value(QStringLiteral("resources")).toArray());
                        });
                    emit stateChanged();
                });
        });
}
void ProjectController::prepareAudio() {
    if (busy() || !session_.online())
        return;
    operation_ = true;
    preparationDeadline_ = QDeadlineTimer(10000);
    emit stateChanged();
    waitToPrepareAudio();
}
void ProjectController::waitToPrepareAudio() {
    if (!session_.online() || preparationDeadline_.hasExpired()) {
        operation_ = false;
        emit error(QStringLiteral(
            "Não foi possível preparar o áudio. Confira o motor e tente novamente."));
        emit stateChanged();
        return;
    }
    if (!session_.settled() || session_.devicesRefreshing()) {
        QTimer::singleShot(25, this, &ProjectController::waitToPrepareAudio);
        return;
    }
    retryAfterDiscovery_ = true;
    session_.refreshDevices();
}
void ProjectController::retryPreparedAudio() {
    QJsonArray resources;
    const auto states = session_.snapshot().value(QStringLiteral("plugin_states")).toObject();
    for (const auto& value : document().value(QStringLiteral("resources")).toArray()) {
        const auto resource = value.toObject();
        const auto runtime =
            states.value(resource.value(QStringLiteral("node_id")).toString()).toObject();
        if (runtime.value(QStringLiteral("file")).toString().isEmpty() ||
            runtime.value(QStringLiteral("status")).toInt() <= 0)
            resources.append(resource);
    }
    session_.command(QStringLiteral("audio.retry"), {},
                     [this, resources](const BackendReply& reply) {
                         if (reply.ok)
                             prepareResources(resources);
                         else {
                             operation_ = false;
                             emit stateChanged();
                         }
                     });
}
void ProjectController::prepareResources(QJsonArray resources, qsizetype index) {
    if (index >= resources.size()) {
        operation_ = false;
        if (resetHistoryAfterOpen_) {
            resetHistoryAfterOpen_ = false;
            resetHistory();
        }
        emit stateChanged();
        return;
    }
    const auto resource = resources[index].toObject();
    const auto id = resource.value(QStringLiteral("node_id")).toString();
    if (!session_.catalog().describe(session_.node(id)).filePlayer) {
        prepareResources(resources, index + 1);
        return;
    }
    session_.command(
        QStringLiteral("plugin.command"),
        {{QStringLiteral("node_id"), id},
         {QStringLiteral("action"), QStringLiteral("load")},
         {QStringLiteral("path"), resource.value(QStringLiteral("path"))}},
        [this, resources, index](const BackendReply&) { prepareResources(resources, index + 1); });
}
void ProjectController::remember(const QString& file) {
    for (auto it = recents_.begin(); it != recents_.end();)
        it = it->compare(file, Qt::CaseInsensitive) == 0 ? recents_.erase(it) : std::next(it);
    recents_.prepend(QDir::cleanPath(file));
    while (recents_.size() > 10)
        recents_.removeLast();
    QJsonArray list;
    for (const auto& recent : recents_)
        list.append(recent);
    const QJsonObject data{{QStringLiteral("version"), 1}, {QStringLiteral("files"), list}};
    const auto destination = QDir(paths_.profile).filePath(QStringLiteral("recent-projects.json"));
    io([destination, data] { return ProjectRepository::write(destination, data); },
       [this](const FileResult& result) {
           if (!result.ok())
               emit error(result.error);
       });
}
void ProjectController::checkpoint() {
    if (busy() || recoveryPending_ || restoring_ || !session_.settled() || !dirty())
        return;
    const auto project = document();
    const auto contents = QJsonDocument(project).toJson(QJsonDocument::Compact);
    if (contents == recoveryStamp_)
        return;
    operation_ = true;
    session_.command(QStringLiteral("project.validate"),
                     {{QStringLiteral("contents"), QString::fromUtf8(contents)},
                      {QStringLiteral("expected_revision"),
                       session_.snapshot().value(QStringLiteral("revision"))}},
                     [this, contents, project](const BackendReply& reply) {
                         if (!reply.ok) {
                             operation_ = false;
                             return;
                         }
                         const auto file = recoveryFile();
                         io([file, project] { return ProjectRepository::write(file, project); },
                            [this, contents](const FileResult& result) {
                                operation_ = false;
                                recordRecoveryResult(result);
                                if (!result.ok())
                                    emit error(result.error);
                                else
                                    recoveryStamp_ = contents;
                            });
                     });
}
void ProjectController::restoreRecovery() {
    if (busy() || !session_.settled() || !confirmDiscard(PendingChangesAction::Restore))
        return;
    operation_ = true;
    const auto file = recoveryFile();
    io([file] { return ProjectRepository::read(file); },
       [this, file](const FileResult& result) {
           if (result.ok())
               openContents(result.contents, file, true);
           else {
               operation_ = false;
               recoveryError_ =
                   QStringLiteral("Não foi possível ler a cópia de recuperação: ") + result.error;
               emit error(result.error);
           }
       });
}
void ProjectController::discardRecovery() {
    if (busy())
        return;
    const auto file = recoveryFile();
    io([file] { return ProjectRepository::removeRecovery(file); },
       [this](const FileResult& result) {
           recordRecoveryResult(result);
           if (result.ok())
               recoveryPending_ = false;
           else
               emit error(result.error);
       });
}
} // namespace nodivu
