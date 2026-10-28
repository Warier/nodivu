#pragma once
#include "platform/app_paths.hpp"
#include <QObject>
#include <QSettings>
#include <QTimer>
class QMainWindow;
class QAction;
class QSplitter;
namespace nodivu {
class GraphScene;
class GraphView;
class LibraryPanel;
class ProjectController;
class MiniMap;
class WorkspaceTools final : public QObject {
    Q_OBJECT
  public:
    WorkspaceTools(QMainWindow& window, GraphScene& scene, GraphView& view, LibraryPanel& library,
                   ProjectController& projects, AppPaths paths);
    ~WorkspaceTools() override;
    void savePreferences();
    void saveTemplate(const QString& file);
    void insertTemplate(const QString& file);
    void duplicateSelection();
  signals:
    void message(const QString& text);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void rename(const QString& id);
    void findNode();
    void updateActions();
    QPointF insertionPoint() const;
    QMainWindow& window_;
    GraphScene& scene_;
    GraphView& view_;
    LibraryPanel& library_;
    ProjectController& projects_;
    AppPaths paths_;
    QSettings settings_;
    QTimer saveTimer_;
    MiniMap* miniMap_ = nullptr;
    QSplitter* splitter_ = nullptr;
    QAction* rename_ = nullptr;
    QAction* duplicate_ = nullptr;
    QAction* remove_ = nullptr;
    QAction* alignHorizontal_ = nullptr;
    QAction* alignVertical_ = nullptr;
    QAction* frameSelection_ = nullptr;
    QAction* saveTemplate_ = nullptr;
    QAction* insertTemplate_ = nullptr;
    QAction* compact_ = nullptr;
    QAction* miniMapToggle_ = nullptr;
    QAction* contrast_ = nullptr;
    bool restoring_ = true;
    bool settingsErrorShown_ = false;
};
} // namespace nodivu
