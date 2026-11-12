// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_SIDEBAR_H_
#define HERMES_UI_SIDEBAR_H_

#include <QColor>
#include <QString>
#include <QWidget>

class QTimer;
#include <functional>
#include <utility>
#include <vector>

#include "src/appearance_prefs.h"
#include "src/click_target.h"
#include "src/model.h"
#include "src/projects.h"
#include "src/theme.h"

class QLineEdit;
class QScrollBar;

namespace hermes::ui {

// One square on the profile rail at the sidebar foot.
struct ProfileChip {
  QString label;
  QColor color;
  bool is_default = false;
  bool connected = false;
  int unread = 0;
};

// Session list plus the profile rail. Custom painted so the 20px squares,
// 30px rows and 1px hairlines match the CSS rather than a platform style.
class Sidebar : public QWidget {
 public:
  explicit Sidebar(const Theme& theme, QWidget* parent = nullptr);

  void SetTheme(const Theme& theme);
  void SetSnapshot(const ProfileSnapshot& snapshot);
  void SetProfileChips(const std::vector<ProfileChip>& chips, int active_index);
  void SetActiveIndex(int active_index);
  // Workspace pages own the nav rows above the session list; the shell lights
  // up the row whose page is showing.
  void SetActivePage(const QString& page_id);
  void SetShowArchived(bool show);
  void SetRunning(bool running);
  // Backend-authoritative project tree. `scope_id` is either AllProjectsId()
  // or one project id from the tree; session filtering uses the node's
  // sessionIds, never a client-side membership guess.
  void SetProjects(const std::vector<ProjectNode>& projects,
                   const QString& scope_id, const QString& active_id,
                   bool available = false);
  void FocusSearch();
  void SetSessionDensity(SessionListDensity density);
  SessionListDensity session_density() const { return session_density_; }
  void SetCollapsed(bool collapsed);
  bool IsCollapsed() const { return collapsed_; }
  std::vector<ClickTarget> ClickTargets();

  std::function<void(int)> on_profile_selected;
  // Fired after the rail's own collapse affordance is clicked. The shell owns
  // the splitter width so the compact rail can stay pinned to 46 px.
  std::function<void(bool)> on_collapsed_changed;
  std::function<void()> on_add_profile;
  std::function<void()> on_new_chat;
  std::function<void(const QString&)> on_nav;
  std::function<void()> on_settings;
  std::function<bool()> completion_sound_on;
  std::function<void(bool)> completion_sound_set;
  std::function<bool()> completion_flash_on;
  std::function<void(bool)> completion_flash_set;
  std::function<void(const QString&)> on_session_selected;
  // Row context menu: "pin" | "unpin" | "rename" | "archive" | "delete".
  std::function<void(const QString&, const QString&)> on_session_action;
  std::function<void(const QString&)> on_project_selected;
  std::function<void()> on_project_add;
  // Project row menu: "edit" | "appearance" | "add_folder" | "remove_folder" |
  // "set_primary" | "set_active" | "delete" | "dismiss_auto" | "new".
  std::function<void(const QString&, const QString&)> on_project_action;

 protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void leaveEvent(QEvent* event) override;

 private:
  struct RowRect {
    QRectF rect;
    int index = 0;
  };

  struct ProjectRect {
    QRectF rect;
    QRectF chevron;
    QString id;
    bool expanded = false;
  };

  void RebuildHitRects();
  QRectF RailRect() const;
  QRectF NewChatRect() const;
  QRectF CollapseRect() const;
  QRectF AddProfileRect() const;
  QRectF MoreRect() const;
  QRectF NavRect(int index) const;
  qreal SearchTop() const;
  qreal SectionLabelTop() const;
  qreal SessionRowHeight() const;
  qreal SessionGap() const;
  qreal SessionsTop() const;
  qreal ProjectsRowsTop() const;
  qreal SessionContentTop() const;
  QRectF ProjectAddRect() const;
  const ProjectNode* FindProjectNode(const QString& project_id) const;
  bool ProjectsVisible() const {
    return !projects_.empty() || projects_available_;
  }
  void ShowSessionMenu(const QPoint& global_position, const QString& session_id,
                       bool pinned, bool archived);
  void ShowProjectMenu(const QPoint& global_position,
                       const QString& project_id);
  void ApplySearchStyle();
  void ApplyScrollBarStyle();
  void SyncSessionScrollBar();
  void UpdateSearchGeometry();

  struct NavRow {
    QString id;
    QString label;
    quint16 icon = 0;
  };

  Theme theme_;
  std::vector<NavRow> nav_;
  QString active_page_;
  int hovered_nav_ = -1;
  ProfileSnapshot snapshot_;
  std::vector<ProfileChip> chips_;
  int active_index_ = 0;
  QString selected_session_id_;
  int hovered_session_ = -1;
  int hovered_chip_ = -1;
  bool hovered_new_chat_ = false;
  bool hovered_collapse_ = false;
  bool collapsed_ = false;
  SessionListDensity session_density_ = SessionListDensity::kCompact;
  int second_section_y_ = -1;
  int session_scroll_ = 0;
  bool show_archived_ = false;
  bool running_ = false;
  int pulse_ = 0;
  QTimer* pulse_timer_ = nullptr;
  int session_scroll_max_ = 0;
  QLineEdit* search_ = nullptr;
  QScrollBar* session_scrollbar_ = nullptr;
  QString search_query_;
  std::vector<int> filtered_sessions_;

  std::vector<ProjectNode> projects_;
  bool projects_available_ = false;
  QString project_scope_ = AllProjectsId();
  QString active_project_id_;
  bool hovered_project_add_ = false;
  QString hovered_project_;
  QString expanded_project_id_;

  std::vector<RowRect> session_rects_;
  std::vector<ProjectRect> project_rects_;
  std::vector<RowRect> chip_rects_;
  std::vector<std::pair<QString, qreal>> section_headers_;
};

}  // namespace hermes::ui

#endif  // HERMES_UI_SIDEBAR_H_
