#pragma once

#include "features/projects/project_repository.hpp"
#include "features/workspace/session_controller.hpp"
#include "platform/app_paths.hpp"
#include "ui/dialogs/pending_changes_dialog.hpp"
#include <QDeadlineTimer>
#include <QThreadPool>
#include <QWidget>

namespace nodivu {
class GraphScene;
class GraphView;
class ProjectController final : public QObject {
    Q_OBJECT
  public:
    ProjectController(SessionController& session, GraphScene& scene, GraphView& view,
                      AppPaths paths, QWidget* window);
    QJsonObject document() const;
    bool dirty() const;
    bool busy() const {
        return operation_ || ioBusy_;
    }
    bool recoveryPending() const {
        return recoveryPending_;
    }
    QString recoveryStatus() const;
    QDateTime recoverySavedAt() const {
        return recoverySavedAt_;
    }
    QString path() const {
        return path_;
    }
    QStringList recents() const {
        return recents_;
    }
    QString resourcePath(const QString& id) const;
    QHash<QString, QString> savedResources() const {
        return resources_;
    }
    void open(const QString& file = {});
    void save(const QString& file = {});
    void saveAs();
    QDateTime savedAt() const {
        return savedAt_;
    }
    bool canUndo() const;
    bool canRedo() const;
    void undo();
    void redo();
    void restoreRecovery();
    void discardRecovery();
    void prepareAudio();
    bool confirmDiscard(PendingChangesAction action = PendingChangesAction::Close);
    bool finishClose();
  signals:
    void stateChanged();
    void error(const QString& message);

  private:
    void io(std::function<FileResult()> work, std::function<void(const FileResult&)> completion);
    void checkpoint();
    void openContents(const QByteArray& contents, const QString& file, bool recovery);
    void prepareResources(QJsonArray resources, qsizetype index = 0);
    void remember(const QString& file);
    void updateDirty();
    void protectAfterFault();
    void waitToPrepareAudio();
    void retryPreparedAudio();
    void recordRecoveryResult(const FileResult& result);
    void queueHistory();
    void commitHistory();
    void resetHistory();
    void restoreHistory(int index);
    void restoreHistoryResources(int historyIndex, QJsonArray commands, qsizetype index);
    QString recoveryFile() const;
    SessionController& session_;
    GraphScene& scene_;
    GraphView& view_;
    AppPaths paths_;
    QWidget* window_;
    QThreadPool ioPool_;
    QTimer checkpointTimer_;
    QString path_;
    QStringList recents_;
    QByteArray savedStamp_;
    QByteArray recoveryStamp_;
    QHash<QString, QString> resources_;
    QHash<QString, QJsonObject> captures_;
    bool operation_ = false;
    bool ioBusy_ = false;
    bool recoveryPending_ = false;
    bool restoring_ = false;
    bool faultProtectionPending_ = false;
    bool retryAfterDiscovery_ = false;
    QDeadlineTimer preparationDeadline_;
    QDateTime recoverySavedAt_;
    QString recoveryError_;
    QDateTime savedAt_;
    QTimer historyTimer_;
    QVector<QJsonObject> history_;
    QJsonObject pendingHistory_;
    int historyIndex_ = -1;
    bool historyApplying_ = false;
    bool resetHistoryAfterOpen_ = false;
};
} // namespace nodivu
