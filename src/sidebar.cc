// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/sidebar.h"

#include <QDateTime>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolTip>
#include <QWheelEvent>
#include <algorithm>
#include <vector>

#include "src/paint_utils.h"
#include "src/project_appearance.h"
#include "src/search_completion.h"

namespace hermes::ui {
namespace {

bool SessionsEquivalent(const std::vector<SessionSummary>& a,
                        const std::vector<SessionSummary>& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    const SessionSummary& x = a[i];
    const SessionSummary& y = b[i];
    if (x.id != y.id || x.title != y.title || x.preview != y.preview ||
        x.project_id != y.project_id ||
        x.last_active_secs != y.last_active_secs || x.unread != y.unread ||
        x.pinned != y.pinned || x.archived != y.archived) {
      return false;
    }
  }
  return true;
}

constexpr int kOuterPadding = 12;
constexpr int kSessionRowHeight = 30;
constexpr int kSessionGap = 2;
constexpr int kChipSize = 20;
constexpr int kChipGap = 4;
constexpr int kNavTop = 46;
constexpr int kNavHeight = 26;
constexpr int kNavGap = 2;
constexpr int kProjectRowHeight = 26;
constexpr int kCollapsedWidth = 46;
constexpr int kExpandedMinWidth = 180;
constexpr int kExpandedMaxWidth = 440;

// Compact age label: minutes, hours, days. The sidebar has 32px for it.
QString RelativeTime(qint64 seconds) {
  if (seconds <= 0) {
    return QString();
  }
  const qint64 now = QDateTime::currentSecsSinceEpoch();
  const qint64 delta = std::max<qint64>(0, now - seconds);
  if (delta < 60) {
    return QStringLiteral("now");
  }
  if (delta < 3600) {
    return QStringLiteral("%1m").arg(delta / 60);
  }
  if (delta < 86400) {
    return QStringLiteral("%1h").arg(delta / 3600);
  }
  if (delta < 86400 * 7) {
    return QStringLiteral("%1d").arg(delta / 86400);
  }
  return QStringLiteral("%1w").arg(delta / (86400 * 7));
}

}  // namespace

Sidebar::Sidebar(const Theme& theme, QWidget* parent)
    : QWidget(parent), theme_(theme) {
  setMinimumWidth(kExpandedMinWidth);
  setMaximumWidth(kExpandedMaxWidth);
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
  setMouseTracking(true);
  // Top nav, mirroring the desktop's SIDEBAR_NAV rows (icon + name): the
  // workspace pages are entry points, not settings subpages.
  nav_ = {
      {QStringLiteral("capabilities"), QStringLiteral("Capabilities"), 0xEB63},
      {QStringLiteral("messaging"), QStringLiteral("Messaging"), 0xEA6B},
      {QStringLiteral("artifacts"), QStringLiteral("Artifacts"), 0xEAF0},
      {QStringLiteral("kanban"), QStringLiteral("Kanban"), 0xEB5B},
      {QStringLiteral("webhooks"), QStringLiteral("Webhooks"), 0xEAD4},
      {QStringLiteral("cron"), QStringLiteral("Scheduled tasks"), 0xEB7C},
      {QStringLiteral("profiles"), QStringLiteral("Bots"), 0xEC20},
  };
  search_ = new QLineEdit(this);
  search_->setObjectName(QStringLiteral("sessionSearch"));
  search_->setFrame(false);
  search_->setPlaceholderText(QStringLiteral("Search sessions"));
  search_->setClearButtonEnabled(false);
  UpdateSearchGeometry();
  connect(search_, &QLineEdit::textChanged, this, [this](const QString& text) {
    search_query_ = text;
    session_scroll_ = 0;
    update();
  });
  AttachContextCompleter(search_, [this]() {
    QStringList suggestions;
    for (const ProjectNode& project : projects_) {
      if (!project.label.trimmed().isEmpty()) {
        suggestions.append(project.label.trimmed());
      }
    }
    for (const SessionSummary& session : snapshot_.sessions) {
      if (!session.title.trimmed().isEmpty()) {
        suggestions.append(session.title.trimmed());
      }
      if (suggestions.size() >= 250) {
        break;
      }
    }
    return suggestions;
  });
  ApplySearchStyle();
  session_scrollbar_ = new QScrollBar(Qt::Vertical, this);
  session_scrollbar_->setObjectName(QStringLiteral("sessionScrollBar"));
  session_scrollbar_->setCursor(Qt::ArrowCursor);
  session_scrollbar_->hide();
  ApplyScrollBarStyle();
  connect(session_scrollbar_, &QScrollBar::valueChanged, this,
          [this](int value) {
            if (session_scroll_ == value) {
              return;
            }
            session_scroll_ = value;
            RebuildHitRects();
            update();
          });
  resize(theme_.sidebar_width, height());
}

void Sidebar::UpdateSearchGeometry() {
  if (search_ == nullptr) {
    return;
  }
  search_->setGeometry(kOuterPadding + 28, static_cast<int>(SearchTop()) + 4,
                       std::max(0, width() - kOuterPadding * 2 - 36), 22);
}

void Sidebar::SetCollapsed(bool collapsed) {
  if (collapsed_ == collapsed) {
    return;
  }
  collapsed_ = collapsed;
  hovered_collapse_ = false;
  if (collapsed_) {
    setMinimumWidth(kCollapsedWidth);
    setMaximumWidth(kCollapsedWidth);
    if (search_ != nullptr) {
      search_->hide();
    }
    if (session_scrollbar_ != nullptr) {
      session_scrollbar_->hide();
    }
    session_scroll_ = 0;
  } else {
    setMinimumWidth(kExpandedMinWidth);
    setMaximumWidth(kExpandedMaxWidth);
    if (search_ != nullptr) {
      search_->show();
      UpdateSearchGeometry();
    }
  }
  RebuildHitRects();
  update();
}

void Sidebar::ApplyScrollBarStyle() {
  if (session_scrollbar_ == nullptr) {
    return;
  }
  session_scrollbar_->setStyleSheet(
      QStringLiteral(
          "QScrollBar:vertical { background: transparent; width: 8px; "
          "margin: 0; }"
          "QScrollBar::handle:vertical { background: %1; border-radius: 4px; "
          "min-height: 24px; }"
          "QScrollBar::handle:vertical:hover { background: %2; }"
          "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { "
          "height: 0; }"
          "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { "
          "background: transparent; }")
          .arg(Mix(theme_.sidebar, theme_.text_tertiary, 0.45).name(),
               theme_.text_secondary.name()));
}

void Sidebar::ApplySearchStyle() {
  if (search_ == nullptr) {
    return;
  }
  search_->setFont(theme_.UiFont(12, QFont::Normal));
  QPalette palette = search_->palette();
  palette.setColor(QPalette::Text, theme_.foreground);
  palette.setColor(QPalette::PlaceholderText, theme_.text_tertiary);
  palette.setColor(QPalette::Highlight, theme_.selected);
  palette.setColor(QPalette::HighlightedText, theme_.foreground);
  palette.setColor(QPalette::Base, Qt::transparent);
  search_->setPalette(palette);
  search_->setStyleSheet(
      QStringLiteral(
          "QLineEdit { background: transparent; border: none; padding: 0; "
          "color: %1; }")
          .arg(theme_.foreground.name()));
  search_->raise();
}

void Sidebar::SetTheme(const Theme& theme) {
  theme_ = theme;
  ApplySearchStyle();
  ApplyScrollBarStyle();
  update();
}

void Sidebar::FocusSearch() {
  if (search_ != nullptr) {
    search_->setFocus(Qt::ShortcutFocusReason);
    search_->selectAll();
  }
}

void Sidebar::SetSnapshot(const ProfileSnapshot& snapshot) {
  if (snapshot.name == snapshot_.name &&
      snapshot.selected_session_id == snapshot_.selected_session_id &&
      snapshot.today_count == snapshot_.today_count &&
      SessionsEquivalent(snapshot.sessions, snapshot_.sessions)) {
    return;
  }
  if (snapshot.name != snapshot_.name) {
    session_scroll_ = 0;
    if (search_ != nullptr) {
      search_->clear();
    }
  }
  // The sidebar never reads transcript, artifacts, attachments, or model
  // catalog state. Keeping a stripped snapshot avoids a deep copy of every
  // message on each streaming publish.
  ProfileSnapshot stripped;
  stripped.name = snapshot.name;
  stripped.selected_session_id = snapshot.selected_session_id;
  stripped.today_count = snapshot.today_count;
  stripped.sessions = snapshot.sessions;
  snapshot_ = std::move(stripped);
  if (!snapshot.selected_session_id.isEmpty()) {
    selected_session_id_ = snapshot.selected_session_id;
  }
  bool selected_exists = false;
  for (const SessionSummary& session : snapshot_.sessions) {
    if (session.id == selected_session_id_) {
      selected_exists = true;
      break;
    }
  }
  if (!selected_exists) {
    selected_session_id_ =
        snapshot_.sessions.empty() ? QString() : snapshot_.sessions.front().id;
  }
  update();
}

void Sidebar::SetProfileChips(const std::vector<ProfileChip>& chips,
                              int active_index) {
  chips_ = chips;
  active_index_ = active_index;
  SyncPulseTimer();
}

void Sidebar::SetActiveIndex(int active_index) {
  active_index_ = active_index;
  update();
}

QRectF Sidebar::NewChatRect() const {
  if (collapsed_) {
    return QRectF((width() - 24) / 2, 46, 24, 24);
  }
  return QRectF(width() - 16 - 24, 14, 24, 24);
}

QRectF Sidebar::CollapseRect() const {
  if (collapsed_) {
    return QRectF((width() - 24) / 2, 12, 24, 24);
  }
  return QRectF(width() - 16 - 24 - 24 - 4, 14, 24, 24);
}

QRectF Sidebar::RailRect() const {
  return QRectF(8, height() - 30, width() - 16, kChipSize);
}

QRectF Sidebar::NavRect(int index) const {
  if (collapsed_) {
    return QRectF((width() - 26) / 2, 82 + index * 32, 26, 26);
  }
  return QRectF(kOuterPadding, kNavTop + index * (kNavHeight + kNavGap),
                width() - kOuterPadding * 2, kNavHeight);
}

QRectF Sidebar::AddProfileRect() const {
  return QRectF(8 + static_cast<int>(chips_.size()) * (kChipSize + kChipGap),
                RailRect().top(), kChipSize, kChipSize);
}

QRectF Sidebar::MoreRect() const {
  return QRectF(width() - 8 - kChipSize, RailRect().top(), kChipSize,
                kChipSize);
}

qreal Sidebar::SearchTop() const {
  return kNavTop + static_cast<int>(nav_.size()) * (kNavHeight + kNavGap) + 8;
}

qreal Sidebar::SectionLabelTop() const { return SearchTop() + 38; }

qreal Sidebar::SessionsTop() const { return SectionLabelTop() + 18; }

qreal Sidebar::SessionRowHeight() const {
  switch (session_density_) {
    case SessionListDensity::kComfortable:
      return 36;
    case SessionListDensity::kDetailed:
      return 52;
    case SessionListDensity::kCompact:
    default:
      return kSessionRowHeight;
  }
}

qreal Sidebar::SessionGap() const {
  switch (session_density_) {
    case SessionListDensity::kComfortable:
      return 4;
    case SessionListDensity::kDetailed:
      return 5;
    case SessionListDensity::kCompact:
    default:
      return kSessionGap;
  }
}

void Sidebar::SetSessionDensity(SessionListDensity density) {
  if (session_density_ == density) {
    return;
  }
  session_density_ = density;
  RebuildHitRects();
  update();
}

qreal Sidebar::ProjectsRowsTop() const { return SectionLabelTop() + 18; }

qreal Sidebar::SessionContentTop() const {
  if (!ProjectsVisible()) {
    return SessionsTop();
  }
  // Project rows and their nested sessions share the session scroll. Keeping
  // the switcher outside the clip made an expanded project's sessions appear
  // detached from their row (and overflow once a few projects existed).
  return ProjectsRowsTop();
}

QRectF Sidebar::ProjectAddRect() const {
  return QRectF(width() - kOuterPadding - 20, SectionLabelTop() + 1, 18, 18);
}

const ProjectNode* Sidebar::FindProjectNode(const QString& project_id) const {
  for (const ProjectNode& node : projects_) {
    if (node.id == project_id) {
      return &node;
    }
  }
  return nullptr;
}

std::vector<ClickTarget> Sidebar::ClickTargets() {
  RebuildHitRects();
  std::vector<ClickTarget> targets;
  targets.push_back({QStringLiteral("sidebar:new-chat"), NewChatRect()});
  for (int i = 0; i < static_cast<int>(nav_.size()); ++i) {
    targets.push_back(
        {QStringLiteral("sidebar:nav:") + nav_[static_cast<std::size_t>(i)].id,
         NavRect(i)});
  }
  if (collapsed_) {
    targets.push_back({QStringLiteral("sidebar:collapse"), CollapseRect()});
    return targets;
  }
  for (int i = 0; i < static_cast<int>(chips_.size()); ++i) {
    targets.push_back({QStringLiteral("sidebar:profile:%1").arg(i),
                       QRectF(8 + i * (kChipSize + kChipGap), RailRect().top(),
                              kChipSize, kChipSize)});
  }
  targets.push_back(
      {QStringLiteral("sidebar:add-profile"), AddProfileRect(), false});
  targets.push_back({QStringLiteral("sidebar:more"), MoreRect(), true});
  if (ProjectsVisible()) {
    targets.push_back(
        {QStringLiteral("sidebar:project-add"), ProjectAddRect()});
    for (const ProjectRect& row : project_rects_) {
      targets.push_back(
          {QStringLiteral("sidebar:project:") +
               (row.id == AllProjectsId() ? QStringLiteral("all") : row.id),
           row.rect});
      if (row.id != AllProjectsId() && !row.chevron.isNull()) {
        targets.push_back(
            {QStringLiteral("sidebar:project-toggle:") + row.id, row.chevron});
      }
    }
  }
  for (const RowRect& row : session_rects_) {
    if (row.index < 0 ||
        row.index >= static_cast<int>(snapshot_.sessions.size())) {
      continue;
    }
    targets.push_back(
        {QStringLiteral("sidebar:session:") +
             snapshot_.sessions[static_cast<std::size_t>(row.index)].id,
         row.rect});
  }
  targets.push_back({QStringLiteral("sidebar:collapse"), CollapseRect()});
  return targets;
}

void Sidebar::SetRunning(bool running, const QString& session_id) {
  const QString next_id = session_id.isEmpty() && running
                              ? selected_session_id_
                              : session_id;
  if (running_ == running && running_session_id_ == next_id) {
    return;
  }
  running_ = running;
  running_session_id_ = running ? next_id : QString();
  SyncPulseTimer();
}

bool Sidebar::SessionRunning(const QString& session_id) const {
  if (!running_ || session_id.isEmpty()) {
    return false;
  }
  const QString running_id = running_session_id_.isEmpty()
                                 ? snapshot_.selected_session_id
                                 : running_session_id_;
  return !running_id.isEmpty() && session_id == running_id;
}

bool Sidebar::ProjectRunning(const ProjectNode& node) const {
  if (!running_) {
    return false;
  }
  const QString running_id = running_session_id_.isEmpty()
                                 ? snapshot_.selected_session_id
                                 : running_session_id_;
  if (running_id.isEmpty()) {
    return false;
  }
  if (node.session_ids.contains(running_id)) {
    return true;
  }
  // A freshly created or locally tagged chat may not be in the cached tree
  // yet. SessionSummary carries the backend project id, so use it as the
  // fallback rather than dropping the project-level activity indicator.
  for (const SessionSummary& session : snapshot_.sessions) {
    if (session.id != running_id) {
      continue;
    }
    if (node.is_no_project) {
      return session.project_id.isEmpty();
    }
    return session.project_id == node.id;
  }
  return false;
}

bool Sidebar::AnyChipRunning() const {
  for (const ProfileChip& chip : chips_) {
    if (chip.running) {
      return true;
    }
  }
  return false;
}

void Sidebar::SyncPulseTimer() {
  if (pulse_timer_ == nullptr) {
    pulse_timer_ = new QTimer(this);
    pulse_timer_->setInterval(170);
    connect(pulse_timer_, &QTimer::timeout, this, [this]() {
      ++pulse_;
      update();
    });
  }
  const bool active = running_ || AnyChipRunning();
  if (active) {
    if (!pulse_timer_->isActive()) {
      pulse_timer_->start();
    }
  } else {
    if (pulse_timer_->isActive()) {
      pulse_timer_->stop();
    }
    pulse_ = 0;
  }
  update();
}

void Sidebar::SetShowArchived(bool show) {
  if (show_archived_ == show) {
    return;
  }
  show_archived_ = show;
  session_scroll_ = 0;
  update();
}

void Sidebar::SetActivePage(const QString& page_id) {
  if (active_page_ == page_id) {
    return;
  }
  active_page_ = page_id;
  update();
}

void Sidebar::SetProjects(const std::vector<ProjectNode>& projects,
                          const QString& scope_id, const QString& active_id,
                          bool available) {
  projects_ = projects;
  // The backend's synthetic Home bucket owns every session no project
  // claimed. Pin it above the switcher so those sessions cannot disappear
  // below a long project list.
  std::stable_partition(
      projects_.begin(), projects_.end(),
      [](const ProjectNode& node) { return node.is_no_project; });
  projects_available_ = available;
  active_project_id_ = active_id;
  const QString previous_scope = project_scope_;
  const QString previous_expansion = expanded_project_id_;
  project_scope_ = scope_id.isEmpty() ? AllProjectsId() : scope_id;
  if (project_scope_ != AllProjectsId() &&
      FindProjectNode(project_scope_) == nullptr) {
    project_scope_ = AllProjectsId();
  }
  if (expanded_project_id_ != AllProjectsId() &&
      !expanded_project_id_.isEmpty() &&
      FindProjectNode(expanded_project_id_) == nullptr) {
    expanded_project_id_.clear();
  }
  // An explicit scope is always shown as an open project. Collapsing via the
  // chevron also returns the scope to All projects, so an explicit scope with
  // no expansion can only come from a profile switch or a context-menu entry.
  if (project_scope_ == AllProjectsId()) {
    expanded_project_id_.clear();
  } else {
    expanded_project_id_ = project_scope_;
  }
  // A snapshot refresh re-sends the same tree. Only a real scope/expansion
  // change should jump back to the first row.
  if (project_scope_ != previous_scope ||
      expanded_project_id_ != previous_expansion) {
    session_scroll_ = 0;
  }
  update();
}

void Sidebar::RebuildHitRects() {
  session_rects_.clear();
  project_rects_.clear();
  chip_rects_.clear();
  section_headers_.clear();

  if (collapsed_) {
    filtered_sessions_.clear();
    session_scroll_max_ = 0;
    session_scroll_ = 0;
    if (session_scrollbar_ != nullptr) {
      session_scrollbar_->hide();
    }
    return;
  }

  if (expanded_project_id_ == AllProjectsId()) {
    expanded_project_id_.clear();
  }
  if (!expanded_project_id_.isEmpty() &&
      FindProjectNode(expanded_project_id_) == nullptr) {
    expanded_project_id_.clear();
  }

  const bool scoped = project_scope_ != AllProjectsId();
  QSet<QString> scoped_session_ids;
  if (scoped) {
    const ProjectNode* node = FindProjectNode(project_scope_);
    if (node != nullptr) {
      for (const QString& id : node->session_ids) {
        scoped_session_ids.insert(id);
      }
    }
    // `session.create` mints the session before the backend writes its first
    // row, so a brand-new chat is not in `projects.tree` yet. Keep the local
    // placeholder visible under the project that started it; membership still
    // comes from the backend once the row is persisted. A selected chat from
    // another project must not leak into this scope just because it is local.
    if (!snapshot_.selected_session_id.isEmpty() &&
        !scoped_session_ids.contains(snapshot_.selected_session_id)) {
      const auto selected =
          std::find_if(snapshot_.sessions.begin(), snapshot_.sessions.end(),
                       [this](const SessionSummary& session) {
                         return session.id == snapshot_.selected_session_id;
                       });
      if (selected != snapshot_.sessions.end() &&
          selected->project_id == project_scope_ &&
          !ProjectTreeContainsSession(projects_, selected->id)) {
        scoped_session_ids.insert(selected->id);
      }
    }
  }

  const QString query = search_query_.trimmed().toLower();
  const auto matches_filter = [&](const SessionSummary& session) {
    if (!show_archived_ && session.archived) {
      return false;
    }
    return query.isEmpty() || session.title.toLower().contains(query) ||
           session.preview.toLower().contains(query);
  };

  filtered_sessions_.clear();
  for (int index = 0; index < static_cast<int>(snapshot_.sessions.size());
       ++index) {
    const SessionSummary& session =
        snapshot_.sessions[static_cast<std::size_t>(index)];
    if (scoped && !scoped_session_ids.contains(session.id)) {
      continue;
    }
    if (matches_filter(session)) {
      filtered_sessions_.push_back(index);
    }
  }

  const int filtered_count = static_cast<int>(filtered_sessions_.size());
  const bool grouped = query.isEmpty();
  const int today_count =
      grouped ? std::clamp(snapshot_.today_count, 0, filtered_count) : 0;
  const int today_boundary =
      grouped && snapshot_.today_count > 0
          ? std::min(snapshot_.today_count,
                     static_cast<int>(snapshot_.sessions.size()))
          : 0;
  const qint64 today_cutoff =
      today_boundary > 0
          ? snapshot_.sessions[static_cast<std::size_t>(today_boundary - 1)]
                .last_active_secs
          : 0;

  // An expanded project owns the rows directly beneath it. The flat recency
  // list is only rendered when no project is open, so a project's sessions
  // appear in exactly one place instead of being duplicated by the global
  // TODAY/EARLIER sections.
  std::vector<int> nested_sessions;
  if (!expanded_project_id_.isEmpty()) {
    if (const ProjectNode* node = FindProjectNode(expanded_project_id_)) {
      QSet<QString> project_sessions;
      for (const QString& id : node->session_ids) {
        project_sessions.insert(id);
      }
      if (expanded_project_id_ == project_scope_ &&
          !snapshot_.selected_session_id.isEmpty() &&
          !project_sessions.contains(snapshot_.selected_session_id)) {
        const auto selected =
            std::find_if(snapshot_.sessions.begin(), snapshot_.sessions.end(),
                         [this](const SessionSummary& session) {
                           return session.id == snapshot_.selected_session_id;
                         });
        if (selected != snapshot_.sessions.end() &&
            selected->project_id == project_scope_ &&
            !ProjectTreeContainsSession(projects_, selected->id)) {
          project_sessions.insert(selected->id);
        }
      }
      for (int index = 0; index < static_cast<int>(snapshot_.sessions.size());
           ++index) {
        const SessionSummary& session =
            snapshot_.sessions[static_cast<std::size_t>(index)];
        if (project_sessions.contains(session.id) && matches_filter(session)) {
          nested_sessions.push_back(index);
        }
      }
    }
  }
  const bool has_expansion = !nested_sessions.empty() ||
                             (!expanded_project_id_.isEmpty() &&
                              FindProjectNode(expanded_project_id_) != nullptr);

  // Pinned sessions form their own always-visible section at the top, exactly
  // like the desktop sidebar; TODAY/EARLIER split the rest by position in the
  // recency-ordered list (the worker publishes today_count for that boundary).
  std::vector<int> order;
  order.reserve(static_cast<std::size_t>(filtered_count));
  if (!has_expansion && grouped) {
    for (int position : filtered_sessions_) {
      if (snapshot_.sessions[static_cast<std::size_t>(position)].pinned) {
        order.push_back(position);
      }
    }
  }
  if (!has_expansion) {
    for (int position : filtered_sessions_) {
      if (!grouped ||
          !snapshot_.sessions[static_cast<std::size_t>(position)].pinned) {
        order.push_back(position);
      }
    }
  }

  auto section_for = [&](int position) {
    if (!grouped) {
      return QStringLiteral("RESULTS");
    }
    if (snapshot_.sessions[static_cast<std::size_t>(position)].pinned) {
      return QStringLiteral("PINNED");
    }
    if (scoped && today_cutoff > 0) {
      return snapshot_.sessions[static_cast<std::size_t>(position)]
                         .last_active_secs >= today_cutoff
                 ? QStringLiteral("TODAY")
                 : QStringLiteral("EARLIER");
    }
    return position < snapshot_.today_count ? QStringLiteral("TODAY")
                                            : QStringLiteral("EARLIER");
  };

  int header_count = 0;
  QString previous_section;
  for (int position : order) {
    const QString section = section_for(position);
    if (section != previous_section) {
      ++header_count;
      previous_section = section;
    }
  }

  const int project_row_count =
      ProjectsVisible() ? std::max(1, static_cast<int>(projects_.size()) + 1)
                        : 0;
  qreal total = static_cast<qreal>(project_row_count) * kProjectRowHeight;
  if (has_expansion) {
    total += static_cast<qreal>(nested_sessions.size()) *
             (SessionRowHeight() + SessionGap());
  } else {
    total +=
        static_cast<qreal>(order.size()) * (SessionRowHeight() + SessionGap()) +
        header_count * 22;
  }

  const qreal content_top = SessionContentTop();
  const qreal content_bottom = RailRect().top() - 12;
  const qreal available = std::max<qreal>(0, content_bottom - content_top);
  session_scroll_max_ = std::max(0, static_cast<int>(total - available));
  session_scroll_ = std::clamp(session_scroll_, 0, session_scroll_max_);

  qreal y = content_top - session_scroll_;
  if (ProjectsVisible()) {
    ProjectRect all;
    all.id = AllProjectsId();
    all.rect = QRectF(kOuterPadding, y, width() - kOuterPadding * 2,
                      kProjectRowHeight - 2);
    project_rects_.push_back(all);
    y += kProjectRowHeight;

    for (const ProjectNode& node : projects_) {
      ProjectRect row;
      row.id = node.id;
      row.rect = QRectF(kOuterPadding, y, width() - kOuterPadding * 2,
                        kProjectRowHeight - 2);
      row.chevron = QRectF(row.rect.left() + 3, row.rect.top() + 4, 18, 18);
      row.expanded = node.id == expanded_project_id_;
      project_rects_.push_back(row);
      y += kProjectRowHeight;

      if (row.expanded) {
        for (int position : nested_sessions) {
          RowRect session_row;
          session_row.rect =
              QRectF(kOuterPadding + 18, y, width() - kOuterPadding * 2 - 18,
                     SessionRowHeight());
          session_row.index = position;
          if (session_row.rect.bottom() > content_top &&
              session_row.rect.top() < content_bottom) {
            session_rects_.push_back(session_row);
          }
          y += SessionRowHeight() + SessionGap();
        }
      }
    }
  }

  if (!has_expansion) {
    previous_section.clear();
    for (int position : order) {
      const QString section = section_for(position);
      if (section != previous_section) {
        section_headers_.push_back({section, y + 4});
        y += 22;
        previous_section = section;
      }
      RowRect row;
      row.rect = QRectF(kOuterPadding, y, width() - kOuterPadding * 2,
                        SessionRowHeight());
      row.index = position;
      if (row.rect.bottom() > content_top && row.rect.top() < content_bottom) {
        session_rects_.push_back(row);
      }
      y += SessionRowHeight() + SessionGap();
    }
  }
  (void)today_count;

  const qreal rail_y = RailRect().top();
  for (int i = 0; i < static_cast<int>(chips_.size()); ++i) {
    RowRect chip;
    chip.rect =
        QRectF(8 + i * (kChipSize + kChipGap), rail_y, kChipSize, kChipSize);
    chip.index = i;
    chip_rects_.push_back(chip);
  }

  // The session/project list is custom painted, so the scrollbar is an
  // overlay child that mirrors the same offset instead of wrapping the
  // widget in a QScrollArea (which would break the rail's fixed geometry).
  if (session_scrollbar_ != nullptr) {
    const QRectF track(kOuterPadding, content_top, width() - kOuterPadding * 2,
                       std::max<qreal>(0, content_bottom - content_top));
    session_scrollbar_->setGeometry(
        static_cast<int>(width() - 10), static_cast<int>(track.top()) + 2, 8,
        std::max(0, static_cast<int>(track.height()) - 4));
    QSignalBlocker blocker(session_scrollbar_);
    session_scrollbar_->setRange(0, session_scroll_max_);
    session_scrollbar_->setPageStep(std::max(1, static_cast<int>(available)));
    session_scrollbar_->setSingleStep(static_cast<int>(SessionRowHeight()));
    session_scrollbar_->setValue(session_scroll_);
    session_scrollbar_->setVisible(session_scroll_max_ > 0);
    if (session_scroll_max_ > 0) {
      session_scrollbar_->raise();
    }
  }
}

void Sidebar::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.fillRect(rect(), theme_.sidebar);
  painter.fillRect(QRectF(width() - 1, 0, 1, height()), theme_.stroke);

  if (collapsed_) {
    RebuildHitRects();
    const QRectF collapse = CollapseRect();
    if (hovered_collapse_) {
      FillRounded(painter, collapse, 5, theme_.hover);
    }
    DrawCodicon(painter, collapse, 0xEAB6, theme_.text_secondary, 13);

    const QRectF new_chat = NewChatRect();
    if (hovered_new_chat_) {
      FillRounded(painter, new_chat, 5, theme_.hover);
    }
    DrawCodicon(painter, new_chat, 0xEA60, theme_.text_secondary, 14);

    for (int i = 0; i < static_cast<int>(nav_.size()); ++i) {
      const QRectF rect = NavRect(i);
      const bool active = nav_[static_cast<std::size_t>(i)].id == active_page_;
      if (active) {
        FillRounded(painter, rect, 6, theme_.selected);
      } else if (i == hovered_nav_) {
        FillRounded(painter, rect, 6, theme_.hover);
      }
      DrawCodicon(painter, rect, nav_[static_cast<std::size_t>(i)].icon,
                  active ? theme_.foreground : theme_.text_secondary, 14);
    }
    return;
  }

  painter.setFont(theme_.UiFont(15, QFont::DemiBold));
  painter.setPen(theme_.foreground);
  painter.drawText(QRectF(kOuterPadding + 4, 14, 120, 24),
                   Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("Hermes"));

  const QRectF collapse = CollapseRect();
  if (hovered_collapse_) {
    FillRounded(painter, collapse, 5, theme_.hover);
  }
  DrawCodicon(painter, collapse, 0xEAB5, theme_.text_secondary, 12);

  const QRectF new_chat = NewChatRect();
  if (hovered_new_chat_) {
    FillRounded(painter, new_chat, 5, theme_.hover);
  }
  DrawCodicon(painter, new_chat, 0xEA60, theme_.text_secondary, 14);

  for (int i = 0; i < static_cast<int>(nav_.size()); ++i) {
    const QRectF rect = NavRect(i);
    const bool active = nav_[static_cast<std::size_t>(i)].id == active_page_;
    if (active) {
      FillRounded(painter, rect, 6, theme_.selected);
    } else if (i == hovered_nav_) {
      FillRounded(painter, rect, 6, theme_.hover);
    }
    DrawCodicon(painter, QRectF(rect.left() + 7, rect.top(), 16, rect.height()),
                nav_[static_cast<std::size_t>(i)].icon,
                active ? theme_.foreground : theme_.text_secondary, 13);
    DrawElidedText(
        painter,
        QRectF(rect.left() + 29, rect.top(), rect.width() - 34, rect.height()),
        Qt::AlignVCenter | Qt::AlignLeft,
        nav_[static_cast<std::size_t>(i)].label,
        active ? theme_.foreground : theme_.text_secondary,
        theme_.UiFont(12, active ? QFont::DemiBold : QFont::Normal));
  }

  const qreal search_top = SearchTop();
  const QRectF search_rect(kOuterPadding, search_top,
                           width() - kOuterPadding * 2, 30);
  FillRounded(painter, search_rect, 6,
              Mix(theme_.sidebar, theme_.surface, 0.7));
  StrokeRounded(painter, search_rect, 6, theme_.stroke);
  DrawCodicon(painter,
              QRectF(search_rect.left() + 8, search_rect.top(), 16, 30), 0xEA6D,
              theme_.text_tertiary, 12);

  RebuildHitRects();

  QFont section_font = theme_.UiFont(10, QFont::DemiBold);
  section_font.setLetterSpacing(QFont::AbsoluteSpacing, 0.4);
  painter.setFont(section_font);
  painter.setPen(theme_.text_tertiary);

  if (ProjectsVisible()) {
    painter.setFont(section_font);
    painter.setPen(theme_.text_tertiary);
    painter.drawText(QRectF(kOuterPadding + 4, SectionLabelTop(), 120, 16),
                     Qt::AlignVCenter | Qt::AlignLeft,
                     QStringLiteral("PROJECTS"));
    const QRectF add = ProjectAddRect();
    if (hovered_project_add_) {
      FillRounded(painter, add, 4, theme_.hover);
    }
    DrawCodicon(painter, add, 0xEA60, theme_.text_tertiary, 11);
  }

  const qreal content_top = SessionContentTop();
  const qreal content_bottom = RailRect().top() - 12;
  painter.save();
  painter.setClipRect(
      QRectF(0, content_top, width(), content_bottom - content_top));
  for (const ProjectRect& row : project_rects_) {
    const bool selected = row.id == project_scope_;
    const bool hovered = row.id == hovered_project_;
    if (selected) {
      FillRounded(painter, row.rect, 6, theme_.selected);
    } else if (hovered) {
      FillRounded(painter, row.rect, 6, theme_.hover);
    }
    const bool all = row.id == AllProjectsId();
    const ProjectNode* node = all ? nullptr : FindProjectNode(row.id);
    const QString label =
        all ? QStringLiteral("All projects")
            : (node != nullptr ? (node->is_no_project ? QStringLiteral("Home")
                                                      : node->label)
                               : row.id);
    const int count = all ? static_cast<int>(snapshot_.sessions.size())
                          : (node != nullptr ? node->session_count : 0);
    const bool active = !all && row.id == active_project_id_;
    const bool project_running =
        all ? running_ : (node != nullptr && ProjectRunning(*node));
    qreal icon_left = row.rect.left() + 8;
    qreal label_left = row.rect.left() + 28;
    if (!all) {
      DrawCodicon(painter, row.chevron, row.expanded ? 0xEAB4 : 0xEAB6,
                  theme_.text_tertiary, 11);
      icon_left = row.rect.left() + 24;
      label_left = row.rect.left() + 42;
    }
    QColor project_glyph =
        selected || active ? theme_.foreground : theme_.text_secondary;
    ushort project_icon = 0xEAF0;
    if (!all) {
      project_icon =
          node != nullptr && node->is_no_project && node->icon.isEmpty()
              ? 0xEB06
              : (node == nullptr ? ProjectIconCodepoint(QString())
                                 : ProjectIconCodepoint(node->icon));
      const QColor tint =
          ProjectColorValue(node == nullptr ? QString() : node->color);
      if (tint.isValid()) {
        project_glyph = tint;
      }
    }
    DrawCodicon(painter,
                QRectF(icon_left, row.rect.top(), 15, row.rect.height()),
                project_icon, project_glyph, 12);
    DrawElidedText(
        painter,
        QRectF(label_left, row.rect.top(), row.rect.right() - 38 - label_left,
               row.rect.height()),
        Qt::AlignVCenter | Qt::AlignLeft, label,
        selected ? theme_.foreground : theme_.text_secondary,
        theme_.UiFont(12,
                      selected || active ? QFont::DemiBold : QFont::Normal));
    if (active || project_running) {
      const qreal phase = (pulse_ % 8) / 8.0;
      const qreal wave = phase < 0.5 ? phase * 2 : (1.0 - phase) * 2;
      QColor dot = theme_.accent;
      const qreal radius = project_running ? 2.2 + wave : 2.2;
      if (project_running) {
        dot.setAlphaF(0.35 + 0.65 * wave);
      }
      painter.setPen(Qt::NoPen);
      painter.setBrush(dot);
      painter.drawEllipse(QPointF(icon_left + 12, row.rect.center().y()),
                          radius, radius);
    }
    DrawElidedText(
        painter,
        QRectF(row.rect.right() - 38, row.rect.top(), 30, row.rect.height()),
        Qt::AlignVCenter | Qt::AlignRight,
        count > 0 ? QString::number(count) : QString(), theme_.text_tertiary,
        theme_.UiFont(10, QFont::Normal));
  }
  for (const auto& header : section_headers_) {
    painter.setFont(section_font);
    painter.setPen(theme_.text_tertiary);
    painter.drawText(QRectF(kOuterPadding + 4, header.second, 120, 16),
                     Qt::AlignVCenter | Qt::AlignLeft, header.first);
  }
  for (const RowRect& row : session_rects_) {
    const SessionSummary& session =
        snapshot_.sessions[static_cast<std::size_t>(row.index)];
    const bool selected = session.id == selected_session_id_;
    const bool hovered = row.index == hovered_session_;
    if (selected) {
      FillRounded(painter, row.rect, 6, theme_.selected);
    } else if (hovered) {
      FillRounded(painter, row.rect, 6, theme_.hover);
    }

    const bool detailed = session_density_ == SessionListDensity::kDetailed;
    const qreal line_height = detailed ? 18 : row.rect.height();
    const qreal line_top = detailed ? row.rect.top() + 5 : row.rect.top();
    const qreal dot_y = detailed ? row.rect.top() + 14 : row.rect.center().y();
    qreal text_left = row.rect.left() + 8;
    // The session that is generating right now gets a pulsing accent dot, so
    // the sidebar shows work in progress even when the transcript is scrolled
    // away from the streaming row.
    if (SessionRunning(session.id)) {
      const qreal phase = (pulse_ % 8) / 8.0;
      const qreal wave = phase < 0.5 ? phase * 2 : (1.0 - phase) * 2;
      QColor dot = theme_.accent;
      dot.setAlphaF(0.35 + 0.65 * wave);
      painter.setPen(Qt::NoPen);
      painter.setBrush(dot);
      painter.drawEllipse(QPointF(row.rect.left() + 8, dot_y), 3.2 + wave,
                          3.2 + wave);
      text_left = row.rect.left() + 18;
    } else if (session.unread) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(theme_.accent);
      painter.drawEllipse(QPointF(row.rect.left() + 8, dot_y), 3, 3);
      text_left = row.rect.left() + 18;
    }

    const QColor title_color =
        selected ? theme_.foreground : theme_.text_secondary;
    DrawElidedText(
        painter,
        QRectF(text_left, line_top,
               row.rect.width() - (text_left - row.rect.left()) -
                   (session.pinned ? 62 : 44),
               line_height),
        Qt::AlignVCenter | Qt::AlignLeft, session.title, title_color,
        theme_.UiFont(12, selected ? QFont::DemiBold : QFont::Normal));
    if (detailed && !session.preview.trimmed().isEmpty()) {
      DrawElidedText(
          painter,
          QRectF(text_left, row.rect.top() + 24,
                 row.rect.width() - (text_left - row.rect.left()) - 44, 17),
          Qt::AlignVCenter | Qt::AlignLeft, session.preview,
          theme_.text_tertiary, theme_.UiFont(10, QFont::Normal));
    }

    DrawElidedText(painter,
                   QRectF(row.rect.right() - (session.pinned ? 60 : 40),
                          line_top, session.pinned ? 44 : 32, line_height),
                   Qt::AlignVCenter | Qt::AlignRight,
                   RelativeTime(session.last_active_secs), theme_.text_tertiary,
                   theme_.UiFont(10, QFont::Normal));
    if (session.pinned) {
      DrawCodicon(
          painter,
          QRectF(row.rect.right() - 24,
                 detailed ? row.rect.top() + 7 : row.rect.top() + 9, 14, 12),
          0xEBA0, theme_.accent, 10);
    }
  }
  painter.restore();

  const QRectF rail = RailRect();
  painter.fillRect(
      QRectF(kOuterPadding, rail.top() - 9, width() - kOuterPadding * 2, 1),
      theme_.stroke);

  for (const RowRect& chip_row : chip_rects_) {
    const ProfileChip& chip = chips_[static_cast<std::size_t>(chip_row.index)];
    const bool active = chip_row.index == active_index_;
    const bool hovered = chip_row.index == hovered_chip_;
    painter.save();
    painter.setOpacity(active ? 1.0 : 0.58);
    if (hovered) {
      FillRounded(painter, chip_row.rect.adjusted(-2, -2, 2, 2), 5,
                  theme_.hover);
    }
    const QColor fill = chip.is_default
                            ? Mix(theme_.sidebar, theme_.foreground, 0.08)
                            : Mix(theme_.surface, chip.color, 0.20);
    FillRounded(painter, chip_row.rect, 3, fill);
    if (chip.is_default) {
      DrawCodicon(painter, chip_row.rect, 0xEB06, theme_.text_secondary, 11);
    } else {
      painter.setFont(theme_.UiFont(9, QFont::DemiBold));
      painter.setPen(chip.color);
      painter.drawText(chip_row.rect, Qt::AlignCenter, Initials(chip.label));
    }
    if (active) {
      StrokeRounded(painter, chip_row.rect.adjusted(-1, -1, 1, 1), 4,
                    theme_.accent, 1.4);
    }
    if (chip.unread > 0) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(theme_.accent);
      painter.drawEllipse(
          QPointF(chip_row.rect.right() - 1, chip_row.rect.top() + 1), 2.5,
          2.5);
    }
    if (!chip.connected) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(theme_.warning);
      painter.drawEllipse(
          QPointF(chip_row.rect.left() + 1, chip_row.rect.bottom() - 1), 2, 2);
    }
    if (chip.running) {
      // A profile can keep working while another profile is in front, so the
      // rail itself has to carry the activity signal. Keep the pulse at full
      // opacity even when the inactive chip body is dimmed.
      const qreal phase = (pulse_ % 8) / 8.0;
      const qreal wave = phase < 0.5 ? phase * 2 : (1.0 - phase) * 2;
      QColor dot = theme_.accent;
      dot.setAlphaF(0.35 + 0.65 * wave);
      painter.setOpacity(1.0);
      painter.setPen(Qt::NoPen);
      painter.setBrush(dot);
      painter.drawEllipse(
          QPointF(chip_row.rect.right() - 1, chip_row.rect.bottom() - 1),
          2.2 + wave * 0.8, 2.2 + wave * 0.8);
    }
    painter.restore();
  }

  const int add_x =
      8 + static_cast<int>(chips_.size()) * (kChipSize + kChipGap);
  const QRectF add_rect(add_x, rail.top(), kChipSize, kChipSize);
  FillRounded(painter, add_rect, 3,
              Mix(theme_.sidebar, theme_.foreground, 0.05));
  DrawCodicon(painter, add_rect, 0xEA60, theme_.text_tertiary, 10);
  DrawCodicon(painter, MoreRect(), 0xEA7C, theme_.text_tertiary, 12);
}

void Sidebar::mousePressEvent(QMouseEvent* event) {
  RebuildHitRects();
  const QPointF position = event->position();

  if (event->button() == Qt::RightButton) {
    for (const ProjectRect& row : project_rects_) {
      if (row.rect.contains(position)) {
        ShowProjectMenu(event->globalPosition().toPoint(), row.id);
        event->accept();
        return;
      }
    }
    for (const RowRect& row : session_rects_) {
      if (row.rect.contains(position)) {
        const SessionSummary& session =
            snapshot_.sessions[static_cast<std::size_t>(row.index)];
        ShowSessionMenu(event->globalPosition().toPoint(), session.id,
                        session.pinned, session.archived);
        event->accept();
        return;
      }
    }
    QWidget::mousePressEvent(event);
    return;
  }

  if (event->button() != Qt::LeftButton) {
    QWidget::mousePressEvent(event);
    return;
  }

  if (CollapseRect().contains(position)) {
    SetCollapsed(!collapsed_);
    if (on_collapsed_changed) {
      on_collapsed_changed(collapsed_);
    }
    return;
  }
  if (collapsed_) {
    if (NewChatRect().contains(position)) {
      if (on_new_chat) {
        on_new_chat();
      }
      return;
    }
    for (int i = 0; i < static_cast<int>(nav_.size()); ++i) {
      if (NavRect(i).contains(position)) {
        if (on_nav) {
          on_nav(nav_[static_cast<std::size_t>(i)].id);
        }
        return;
      }
    }
    return;
  }

  if (NewChatRect().contains(position)) {
    if (on_new_chat) {
      on_new_chat();
    }
    return;
  }
  for (int i = 0; i < static_cast<int>(nav_.size()); ++i) {
    if (NavRect(i).contains(position)) {
      if (on_nav) {
        on_nav(nav_[static_cast<std::size_t>(i)].id);
      }
      return;
    }
  }
  if (AddProfileRect().contains(position)) {
    if (on_add_profile) {
      on_add_profile();
    }
    return;
  }
  if (ProjectsVisible() && ProjectAddRect().contains(position)) {
    if (on_project_add) {
      on_project_add();
    }
    return;
  }
  for (const ProjectRect& row : project_rects_) {
    if (row.id != AllProjectsId() && row.chevron.contains(position)) {
      const bool collapse = row.id == expanded_project_id_;
      expanded_project_id_ = collapse ? QString() : row.id;
      project_scope_ = collapse ? AllProjectsId() : row.id;
      session_scroll_ = 0;
      update();
      if (on_project_selected) {
        on_project_selected(collapse ? QString() : row.id);
      }
      return;
    }
    if (row.rect.contains(position)) {
      project_scope_ = row.id;
      expanded_project_id_ = row.id == AllProjectsId() ? QString() : row.id;
      session_scroll_ = 0;
      update();
      if (on_project_selected) {
        on_project_selected(row.id == AllProjectsId() ? QString() : row.id);
      }
      return;
    }
  }
  if (MoreRect().contains(position)) {
    QMenu menu(this);
    QAction* add = menu.addAction(QStringLiteral("Add profile…"));
    QAction* manage = menu.addAction(QStringLiteral("Manage bots…"));
    QAction* archived = menu.addAction(QStringLiteral("Show archived"));
    archived->setCheckable(true);
    archived->setChecked(show_archived_);
    menu.addSeparator();
    QAction* sound = menu.addAction(QStringLiteral("Completion sound"));
    sound->setCheckable(true);
    sound->setChecked(completion_sound_on ? completion_sound_on() : true);
    QAction* flash = menu.addAction(QStringLiteral("Completion flash"));
    flash->setCheckable(true);
    flash->setChecked(completion_flash_on ? completion_flash_on() : true);
    menu.addSeparator();
    QAction* settings = menu.addAction(QStringLiteral("Settings…"));
    QAction* chosen = menu.exec(event->globalPosition().toPoint());
    if (chosen == add && on_add_profile) {
      on_add_profile();
    } else if (chosen == manage && on_nav) {
      on_nav(QStringLiteral("profiles"));
    } else if (chosen == archived) {
      SetShowArchived(archived->isChecked());
    } else if (chosen == sound) {
      if (completion_sound_set) {
        completion_sound_set(sound->isChecked());
      }
    } else if (chosen == flash) {
      if (completion_flash_set) {
        completion_flash_set(flash->isChecked());
      }
    } else if (chosen == settings && on_settings) {
      on_settings();
    }
    return;
  }
  for (const RowRect& chip : chip_rects_) {
    if (chip.rect.contains(position)) {
      if (on_profile_selected) {
        on_profile_selected(chip.index);
      }
      return;
    }
  }
  if (position.y() < SessionContentTop() ||
      position.y() > RailRect().top() - 12) {
    return;
  }
  for (const RowRect& row : session_rects_) {
    if (row.rect.contains(position)) {
      selected_session_id_ =
          snapshot_.sessions[static_cast<std::size_t>(row.index)].id;
      update();
      if (on_session_selected) {
        on_session_selected(selected_session_id_);
      }
      return;
    }
  }
}

void Sidebar::ShowSessionMenu(const QPoint& global_position,
                              const QString& session_id, bool pinned,
                              bool archived) {
  if (!on_session_action || session_id.isEmpty()) {
    return;
  }
  QMenu menu(this);
  const auto emit_action = [this, session_id](const QString& action) {
    if (on_session_action) {
      on_session_action(session_id, action);
    }
  };
  QAction* pin = menu.addAction(pinned ? QStringLiteral("Unpin")
                                       : QStringLiteral("Pin to top"));
  QAction* rename = menu.addAction(QStringLiteral("Rename…"));
  menu.addSeparator();
  QAction* archive = menu.addAction(archived ? QStringLiteral("Unarchive")
                                             : QStringLiteral("Archive"));
  QAction* remove = menu.addAction(QStringLiteral("Delete…"));
  QAction* chosen = menu.exec(global_position);
  if (chosen == pin) {
    emit_action(pinned ? QStringLiteral("unpin") : QStringLiteral("pin"));
  } else if (chosen == rename) {
    emit_action(QStringLiteral("rename"));
  } else if (chosen == archive) {
    emit_action(archived ? QStringLiteral("unarchive")
                         : QStringLiteral("archive"));
  } else if (chosen == remove) {
    emit_action(QStringLiteral("delete"));
  }
}

void Sidebar::ShowProjectMenu(const QPoint& global_position,
                              const QString& project_id) {
  if (project_id == AllProjectsId()) {
    if (!on_project_add) {
      return;
    }
    QMenu menu(this);
    QAction* create = menu.addAction(QStringLiteral("New project…"));
    if (menu.exec(global_position) == create) {
      on_project_add();
    }
    return;
  }
  if (!on_project_action) {
    return;
  }
  const ProjectNode* node = FindProjectNode(project_id);
  const bool explicit_project =
      node != nullptr && !node->is_auto && !node->is_no_project;
  const bool auto_project = node != nullptr && node->is_auto;
  const bool has_path = node != nullptr && !node->path.isEmpty();
  QMenu menu(this);
  QAction* open = menu.addAction(QStringLiteral("Enter project"));
  QAction* create_chat = menu.addAction(QStringLiteral("New chat in project"));
  QAction* reveal_path = nullptr;
  QAction* copy_path = nullptr;
  if (has_path) {
    menu.addSeparator();
    reveal_path = menu.addAction(QStringLiteral("Reveal in file manager"));
    copy_path = menu.addAction(QStringLiteral("Copy path"));
  }
  QAction* edit = nullptr;
  QAction* save_as_project = nullptr;
  QAction* appearance = nullptr;
  QAction* add_folder = nullptr;
  QAction* remove_folder = nullptr;
  QAction* set_primary = nullptr;
  QAction* set_active = nullptr;
  QAction* remove = nullptr;
  if (explicit_project) {
    menu.addSeparator();
    edit = menu.addAction(QStringLiteral("Edit details…"));
    appearance = menu.addAction(QStringLiteral("Appearance…"));
    add_folder = menu.addAction(QStringLiteral("Add folder…"));
    remove_folder = menu.addAction(QStringLiteral("Remove folder…"));
    set_primary = menu.addAction(QStringLiteral("Set primary folder…"));
    menu.addSeparator();
    set_active = menu.addAction(QStringLiteral("Set as active project"));
    remove = menu.addAction(QStringLiteral("Delete project…"));
  } else if (auto_project) {
    menu.addSeparator();
    if (has_path) {
      save_as_project = menu.addAction(QStringLiteral("Save as project…"));
      appearance = menu.addAction(QStringLiteral("Appearance…"));
      menu.addSeparator();
    }
    remove = menu.addAction(QStringLiteral("Remove from sidebar"));
  }
  QAction* chosen = menu.exec(global_position);
  if (chosen == nullptr) {
    return;
  }
  const auto emit_action = [this, project_id](const QString& action) {
    if (on_project_action) {
      on_project_action(project_id, action);
    }
  };
  if (chosen == open) {
    project_scope_ = project_id;
    expanded_project_id_ = project_id;
    session_scroll_ = 0;
    update();
    emit_action(QStringLiteral("scope"));
  } else if (chosen == create_chat) {
    emit_action(QStringLiteral("new"));
  } else if (chosen == reveal_path) {
    emit_action(QStringLiteral("reveal_path"));
  } else if (chosen == copy_path) {
    emit_action(QStringLiteral("copy_path"));
  } else if (chosen == edit) {
    emit_action(QStringLiteral("edit"));
  } else if (chosen == save_as_project) {
    emit_action(QStringLiteral("save_as_project"));
  } else if (chosen == appearance) {
    emit_action(QStringLiteral("appearance"));
  } else if (chosen == add_folder) {
    emit_action(QStringLiteral("add_folder"));
  } else if (chosen == remove_folder) {
    emit_action(QStringLiteral("remove_folder"));
  } else if (chosen == set_primary) {
    emit_action(QStringLiteral("set_primary"));
  } else if (chosen == set_active) {
    emit_action(QStringLiteral("set_active"));
  } else if (chosen == remove) {
    emit_action(explicit_project ? QStringLiteral("delete")
                                 : QStringLiteral("dismiss_auto"));
  }
}

void Sidebar::mouseMoveEvent(QMouseEvent* event) {
  RebuildHitRects();
  const QPointF position = event->position();
  int next_session = -1;
  for (const RowRect& row : session_rects_) {
    if (row.rect.contains(position)) {
      next_session = row.index;
      break;
    }
  }
  int next_nav = -1;
  for (int i = 0; i < static_cast<int>(nav_.size()); ++i) {
    if (NavRect(i).contains(position)) {
      next_nav = i;
      break;
    }
  }
  int next_chip = -1;
  for (const RowRect& chip : chip_rects_) {
    if (chip.rect.contains(position)) {
      next_chip = chip.index;
      break;
    }
  }
  QString next_project;
  for (const ProjectRect& row : project_rects_) {
    if (row.rect.contains(position)) {
      next_project = row.id;
      break;
    }
  }
  const bool next_project_add =
      ProjectsVisible() && ProjectAddRect().contains(position);
  const bool next_new_chat = NewChatRect().contains(position);
  const bool next_collapse = CollapseRect().contains(position);
  const bool next_add_profile =
      !collapsed_ && AddProfileRect().contains(position);
  if (next_session != hovered_session_ || next_chip != hovered_chip_ ||
      next_new_chat != hovered_new_chat_ ||
      next_collapse != hovered_collapse_ || next_nav != hovered_nav_ ||
      next_project != hovered_project_ ||
      next_project_add != hovered_project_add_) {
    hovered_session_ = next_session;
    hovered_chip_ = next_chip;
    hovered_new_chat_ = next_new_chat;
    hovered_collapse_ = next_collapse;
    hovered_nav_ = next_nav;
    hovered_project_ = next_project;
    hovered_project_add_ = next_project_add;
    setCursor((next_session >= 0 || next_chip >= 0 || next_new_chat ||
               next_collapse || next_nav >= 0 || next_add_profile ||
               next_project_add || !next_project.isEmpty())
                  ? Qt::PointingHandCursor
                  : Qt::ArrowCursor);
    if (next_collapse) {
      QToolTip::showText(event->globalPosition().toPoint(),
                         collapsed_ ? QStringLiteral("Expand sidebar")
                                    : QStringLiteral("Collapse sidebar"),
                         this);
    } else if (next_new_chat) {
      QToolTip::showText(event->globalPosition().toPoint(),
                         QStringLiteral("New chat"), this);
    } else if (next_chip >= 0) {
      QToolTip::showText(event->globalPosition().toPoint(),
                         chips_[static_cast<std::size_t>(next_chip)].label,
                         this);
    } else if (next_nav >= 0) {
      QToolTip::showText(event->globalPosition().toPoint(),
                         nav_[static_cast<std::size_t>(next_nav)].label, this);
    } else if (!next_project.isEmpty()) {
      const ProjectNode* node = FindProjectNode(next_project);
      QToolTip::showText(
          event->globalPosition().toPoint(),
          node != nullptr
              ? (node->is_no_project ? QStringLiteral("Home") : node->label)
              : QStringLiteral("All projects"),
          this);
    } else if (next_project_add) {
      QToolTip::showText(event->globalPosition().toPoint(),
                         QStringLiteral("New project"), this);
    } else if (next_add_profile) {
      QToolTip::showText(event->globalPosition().toPoint(),
                         QStringLiteral("Add profile"), this);
    } else {
      QToolTip::hideText();
    }
    update();
  }
  QWidget::mouseMoveEvent(event);
}

void Sidebar::leaveEvent(QEvent* event) {
  hovered_session_ = -1;
  hovered_chip_ = -1;
  hovered_new_chat_ = false;
  hovered_collapse_ = false;
  hovered_project_.clear();
  hovered_project_add_ = false;
  setCursor(Qt::ArrowCursor);
  update();
  QWidget::leaveEvent(event);
}

void Sidebar::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  UpdateSearchGeometry();
  RebuildHitRects();
}

void Sidebar::wheelEvent(QWheelEvent* event) {
  if (collapsed_ || session_scroll_max_ <= 0) {
    QWidget::wheelEvent(event);
    return;
  }
  const int delta = event->angleDelta().y();
  if (delta == 0) {
    QWidget::wheelEvent(event);
    return;
  }
  session_scroll_ -= delta;
  RebuildHitRects();
  update();
  event->accept();
}

}  // namespace hermes::ui
