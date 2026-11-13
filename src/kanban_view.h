#ifndef HERMES_UI_KANBAN_VIEW_H_
#define HERMES_UI_KANBAN_VIEW_H_

#include <QJsonObject>
#include <QString>
#include <QVector>
#include <QWidget>
#include <functional>

#include "src/kanban_model.h"
#include "src/theme.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollBar;
class QTimer;
class QToolButton;

namespace hermes::ui {

class KanbanBoardCanvas;
class KanbanTaskDialog;
class KanbanNewTaskDialog;
class KanbanBoardSettingsDialog;
class KanbanOrchestrationDialog;

// The native Kanban workspace page. It mirrors the desktop plugin: board
// switcher, filter/search toolbar, custom-painted columns and cards, drag/drop
// with the workflow's locked lanes, bulk selection, the task dialog, new-task
// and board settings dialogs, and orchestration settings.
//
// All network I/O goes through on_request, which MainWindow runs on the thread
// pool through GatewayHttp. Nothing here blocks the UI thread.
class KanbanView : public QWidget {
  Q_OBJECT

 public:
  using JsonCallback = std::function<void(const QJsonObject&, const QString&)>;

  explicit KanbanView(Theme theme, QWidget* parent = nullptr);

  void SetTheme(const Theme& theme);
  // Called by PagesView when the page is opened. Reloads boards/board/profile
  // data; subsequent refreshes are polled while visible.
  void Activate();
  void Refresh();
  // Opens the same dialog as the board's New task button.
  void NewTask();
  QString SelectedBoard() const { return selected_board_; }

  std::function<void(const QString& method, const QString& path,
                     const QJsonObject& body, JsonCallback done)>
      on_request;
  std::function<void(const QString& task_id, const QString& local_path,
                     JsonCallback done)>
      on_upload_attachment;
  std::function<void(const QString& attachment_id, const QString& local_path,
                     JsonCallback done)>
      on_download_attachment;
  std::function<QString(const QString& key)> get_pref;
  std::function<void(const QString& key, const QString& value)> set_pref;
  std::function<void(const QString& title, const QString& body)> on_notify;

  // Test seams. They let the board rules be exercised without a gateway.
  void SetBoardForTest(const kanban::KanbanBoard& board, const kanban::KanbanBoardMeta& meta);
  QVector<QRectF> TaskRectsForTest() const;
  QStringList VisibleColumnsForTest() const;
  int VisibleTaskCountForTest() const;
  QStringList SelectionForTest() const;
  void SelectForTest(const QString& task_id);
  QColor CanvasBackgroundForTest() const;

 protected:
  void showEvent(QShowEvent* event) override;
  void hideEvent(QHideEvent* event) override;

 private:
  friend class KanbanBoardCanvas;
  friend class KanbanTaskDialog;
  friend class KanbanNewTaskDialog;
  friend class KanbanBoardSettingsDialog;
  friend class KanbanOrchestrationDialog;

  void BuildUi();
  void ApplyStyle();
  QString BoardPath(const QString& path) const;
  void Request(const QString& method, const QString& path,
               const QJsonObject& body, JsonCallback done);
  void FetchBoards();
  void FetchBoard();
  void FetchRoster();
  void FetchProjects();
  void FetchOrchestration();
  void SelectBoard(const QString& slug, bool persist);
  void SetBoard(const kanban::KanbanBoard& board, const kanban::KanbanBoardMeta& meta);
  void ShowTask(const QString& task_id);
  void ShowNewTask(const QString& column);
  void CreateTask(const kanban::KanbanNewTask& task);
  void ShowBoardMenu();
  void ShowBoardSettings(bool create);
  void ShowOrchestration();
  void ShowFilterMenu();
  void PatchTask(const QString& task_id, const QJsonObject& body,
                 const QString& success);
  void DeleteTask(const QString& task_id);
  void BulkMove(const QStringList& ids, const QString& column);
  void BulkAssign(const QStringList& ids, const QString& assignee);
  void BulkPriority(const QStringList& ids, int priority);
  void BulkArchive(const QStringList& ids);
  void BulkDelete(const QStringList& ids);
  void MoveTask(const QString& task_id, const QString& column);
  void AddTaskToColumn(const QString& column);
  void OpenTask(const QString& task_id);
  void ShowError(const QString& error);
  void ShowStatus(const QString& text);
  void ShowCreated(const QString& task_id, const QString& requested_status);
  void SetBusy(bool busy);
  void UpdateHeader();
  kanban::KanbanBoardMeta CurrentMeta() const;

  Theme theme_;
  QVector<kanban::KanbanBoardMeta> boards_;
  kanban::KanbanBoard board_;
  kanban::KanbanBoardMeta meta_;
  QVector<kanban::KanbanProfile> profiles_;
  QVector<kanban::KanbanProject> projects_;
  kanban::KanbanOrchestration orchestration_;
  QString selected_board_;
  QString boards_signature_;
  QString error_text_;
  int busy_requests_ = 0;
  bool board_pending_ = false;
  QString board_request_slug_;
  int board_request_id_ = 0;
  bool boards_pending_ = false;
  bool roster_pending_ = false;
  bool projects_pending_ = false;
  bool orchestration_pending_ = false;
  int poll_tick_ = 0;
  KanbanTaskDialog* task_dialog_ = nullptr;
  QString task_dialog_id_;

  QLabel* count_ = nullptr;
  QComboBox* board_combo_ = nullptr;
  QToolButton* board_menu_ = nullptr;
  QToolButton* filter_button_ = nullptr;
  QLineEdit* search_ = nullptr;
  QToolButton* orchestration_button_ = nullptr;
  QPushButton* new_task_ = nullptr;
  QLabel* status_ = nullptr;
  KanbanBoardCanvas* canvas_ = nullptr;
  QScrollBar* horizontal_ = nullptr;
  QTimer* poll_timer_ = nullptr;
};

}  // namespace hermes::ui

#endif  // HERMES_UI_KANBAN_VIEW_H_
