// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/right_rail.h"

#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QGuiApplication>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSplitter>
#include <QStyle>
#include <QSvgRenderer>
#include <QSvgWidget>
#include <QTemporaryFile>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextOption>
#include <QTimer>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "src/flow_layout.h"
#include "src/gateway_events.h"
#include "src/markdown_preview_style.h"
#include "src/search_completion.h"
#include "src/subagents.h"
#include "src/todo_state.h"

namespace hermes::ui {
namespace {

void ClearLayout(QLayout* layout) {
  while (QLayoutItem* item = layout->takeAt(0)) {
    if (QWidget* widget = item->widget()) {
      widget->deleteLater();
    }
    if (QLayout* child = item->layout()) {
      ClearLayout(child);
      delete child;
    }
    delete item;
  }
}

QString RailColorRole(const Theme& theme, const QColor& color) {
  if (!color.isValid() || color == theme.foreground) {
    return QStringLiteral("foreground");
  }
  if (color == theme.text_secondary) {
    return QStringLiteral("secondary");
  }
  if (color == theme.text_tertiary) {
    return QStringLiteral("tertiary");
  }
  if (color == theme.accent) {
    return QStringLiteral("accent");
  }
  if (color == theme.success) {
    return QStringLiteral("success");
  }
  if (color == theme.warning) {
    return QStringLiteral("warning");
  }
  if (color == theme.danger) {
    return QStringLiteral("danger");
  }
  return QString();
}

QColor RailColorForRole(const Theme& theme, const QString& role) {
  if (role == QStringLiteral("secondary")) {
    return theme.text_secondary;
  }
  if (role == QStringLiteral("tertiary")) {
    return theme.text_tertiary;
  }
  if (role == QStringLiteral("accent")) {
    return theme.accent;
  }
  if (role == QStringLiteral("success")) {
    return theme.success;
  }
  if (role == QStringLiteral("warning")) {
    return theme.warning;
  }
  if (role == QStringLiteral("danger")) {
    return theme.danger;
  }
  return theme.foreground;
}

void StyleRailLabel(QLabel* label, const Theme& theme) {
  const QString role = label->property("railColorRole").toString();
  if (role.isEmpty()) {
    return;
  }
  const int size = label->property("railFontSize").toInt();
  const auto weight =
      static_cast<QFont::Weight>(label->property("railFontWeight").toInt());
  label->setFont(theme.UiFont(size > 0 ? size : 12, weight));
  label->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
                           .arg(RailColorForRole(theme, role).name()));
}

QLabel* RailLabel(QWidget* parent, const Theme& theme, const QString& text,
                  int size, QFont::Weight weight = QFont::Normal,
                  const QColor& color = QColor()) {
  auto* label = new QLabel(text, parent);
  label->setWordWrap(true);
  label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  label->setProperty("railColorRole", RailColorRole(theme, color));
  label->setProperty("railFontSize", size);
  label->setProperty("railFontWeight", static_cast<int>(weight));
  StyleRailLabel(label, theme);
  return label;
}

QString WrappedToolTip(const QString& text, int width = 360) {
  QString escaped = text.toHtmlEscaped();
  escaped.replace(QStringLiteral("\n"), QStringLiteral("<br>"));
  return QStringLiteral(
             "<div style=\"width:%1px; white-space:pre-wrap; "
             "word-wrap:break-word;\">%2</div>")
      .arg(width)
      .arg(escaped);
}

// A multiline QPushButton only paints its first line, so timeline rows use a
// button as the click/focus surface and child QLabels for wrapped content.
class TimelineRowButton : public QPushButton {
 public:
  TimelineRowButton(const TimelineEntry& entry, int index, QWidget* parent)
      : QPushButton(parent) {
    setObjectName(QStringLiteral("rightRailTimeline_%1").arg(index));
    setProperty("railTimelineRow", true);
    setFlat(true);
    setText(QString());
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(entry.preview);
    QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    setMinimumHeight(36);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(2);
    if (entry.timestamp.isValid()) {
      timestamp_ = new QLabel(
          entry.timestamp.toString(QStringLiteral("MMM d, HH:mm")), this);
      timestamp_->setObjectName(
          QStringLiteral("rightRailTimelineTime_%1").arg(index));
      timestamp_->setProperty("railTimelineTime", true);
      timestamp_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
      timestamp_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
      layout->addWidget(timestamp_);
    }
    preview_ = new QLabel(entry.preview, this);
    preview_->setObjectName(
        QStringLiteral("rightRailTimelinePreview_%1").arg(index));
    preview_->setProperty("railTimelinePreview", true);
    preview_->setWordWrap(true);
    preview_->setTextFormat(Qt::PlainText);
    preview_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    preview_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    QSizePolicy preview_policy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    preview_policy.setHeightForWidth(true);
    preview_->setSizePolicy(preview_policy);
    layout->addWidget(preview_);

    setToolTip(WrappedToolTip(entry.full_text.isEmpty() ? entry.preview
                                                        : entry.full_text));
  }

  void SetTheme(const Theme& theme) {
    setFont(theme.UiFont(11, QFont::Normal));
    if (timestamp_ != nullptr) {
      timestamp_->setFont(theme.UiFont(10, QFont::Normal));
    }
    if (preview_ != nullptr) {
      preview_->setFont(theme.UiFont(11, QFont::Normal));
    }
  }

  QLabel* preview_label() const { return preview_; }
  QLabel* timestamp_label() const { return timestamp_; }

  bool hasHeightForWidth() const override { return true; }

  int heightForWidth(int width) const override {
    if (layout() == nullptr) {
      return QPushButton::heightForWidth(width);
    }
    return layout()->heightForWidth(width);
  }

  QSize sizeHint() const override {
    if (layout() == nullptr) {
      return QPushButton::sizeHint();
    }
    QSize hint = layout()->sizeHint();
    const int height = layout()->heightForWidth(std::max(width(), 220));
    if (height > 0) {
      hint.setHeight(height);
    }
    return hint;
  }

  QSize minimumSizeHint() const override { return QSize(0, 36); }

 protected:
  void resizeEvent(QResizeEvent* event) override {
    QPushButton::resizeEvent(event);
    if (layout() == nullptr) {
      return;
    }
    const int target = layout()->heightForWidth(width());
    if (target > 0 && height() != target) {
      setFixedHeight(target);
    }
  }

 private:
  QLabel* timestamp_ = nullptr;
  QLabel* preview_ = nullptr;
};

QString ComposeArtifactHtml(const QString& content) {
  static const QRegularExpression document(
      QStringLiteral("<html[\\s>]|<!doctype\\s+html"),
      QRegularExpression::CaseInsensitiveOption);
  if (document.match(content).hasMatch()) {
    return content;
  }
  return QStringLiteral(
             "<!doctype html><html><head><meta charset=\"utf-8\">"
             "<meta name=\"viewport\" content=\"width=device-width, "
             "initial-scale=1\"><style>"
             ":root{color-scheme:light dark}"
             "body{margin:0;padding:20px;font-family:system-ui,-apple-system,"
             "BlinkMacSystemFont,'Segoe UI',sans-serif;font-size:14px;"
             "line-height:1.6;color:#1f2328;background:#ffffff;"
             "max-width:760px;margin:0 auto;overflow-wrap:anywhere;"
             "word-break:break-word}"
             "h1,h2,h3{line-height:1.3;margin:1.2em 0 .5em}"
             "h1{font-size:1.7em}h2{font-size:1.35em}h3{font-size:1.12em}"
             "p{margin:0 0 1em}ul,ol{padding-left:1.5em;margin:0 0 1em}"
             "a{color:#4c8dff;text-decoration:none}a:hover{text-decoration:"
             "underline}code,kbd,pre{font-family:'JetBrains Mono',ui-monospace,"
             "SFMono-Regular,Menlo,monospace}code{font-size:.9em;padding:.1em "
             ".3em;border-radius:4px;background:rgba(127,127,127,.16)}"
             "pre{padding:14px;border-radius:8px;overflow:auto;"
             "background:rgba(127,127,127,.12)}pre code{padding:0;"
             "background:transparent}blockquote{margin:0 0 1em;"
             "padding:.65em .9em .65em 1em;border-left:3px solid "
             "rgba(127,127,127,.55);border-radius:0 6px 6px 0;background:"
             "rgba(127,127,127,.08);color:#57606a;overflow-wrap:anywhere;"
             "word-break:break-word}blockquote>*:last-child{margin-bottom:0}"
             "table{border-collapse:collapse;width:100%;"
             "margin:0 0 1em}th,td{padding:7px 10px;text-align:left;"
             "border-bottom:1px solid rgba(127,127,127,.28)}th{background:"
             "rgba(127,127,127,.10);font-weight:600}img{max-width:100%;"
             "height:auto}@media(prefers-color-scheme:dark){body{"
             "color:#e6edf3;background:#0d1117}blockquote{color:#8b949e;"
             "background:rgba(127,127,127,.12)}a{color:#79a8ff}}"
             "</style></head><body>") +
         content + QStringLiteral("</body></html>");
}

QString CurrentArtifactContent(const SessionArtifact& artifact) {
  if (artifact.versions.empty()) {
    return QString();
  }
  const int index =
      std::max(0, std::min(artifact.version_index,
                           static_cast<int>(artifact.versions.size()) - 1));
  return artifact.versions[static_cast<std::size_t>(index)].content;
}

ArtifactKind RailArtifactKind(const QString& kind) {
  if (kind == QStringLiteral("html")) {
    return ArtifactKind::kHtml;
  }
  if (kind == QStringLiteral("svg")) {
    return ArtifactKind::kSvg;
  }
  return ArtifactKind::kCode;
}

bool OpenArtifactHtmlInBrowser(const QString& content) {
  QTemporaryFile file(QDir::tempPath() +
                      QStringLiteral("/hermes-artifact-XXXXXX.html"));
  file.setAutoRemove(false);
  if (!file.open()) {
    return false;
  }
  const QByteArray bytes = ComposeArtifactHtml(content).toUtf8();
  if (file.write(bytes) != bytes.size()) {
    return false;
  }
  const QString path = file.fileName();
  file.close();
  return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

QPushButton* PreviewAction(QWidget* parent, const QString& text,
                           const QString& name) {
  auto* button = new QPushButton(text, parent);
  button->setObjectName(name);
  button->setProperty("railAction", true);
  button->setFlat(true);
  button->setCursor(Qt::PointingHandCursor);
  return button;
}

QString GitPaneObjectName(const QString& base, const QString& suffix,
                          int row = -1) {
  QString name = base + suffix;
  if (row >= 0) {
    name += QStringLiteral("_%1").arg(row);
  }
  return name;
}

struct GitFileChange {
  QString status;
  QString path;
  QString original_path;
  bool staged = false;
};

struct GitStatusSnapshot {
  QString branch;
  std::vector<GitFileChange> changes;
};

QString GitBranchText(const QString& header) {
  QString text = header;
  if (text.startsWith(QStringLiteral("## "))) {
    text = text.mid(3);
  }
  return text.trimmed();
}

QString GitChangeDescription(const QString& status) {
  if (status == QStringLiteral("A")) {
    return QStringLiteral("Added");
  }
  if (status == QStringLiteral("M")) {
    return QStringLiteral("Modified");
  }
  if (status == QStringLiteral("D")) {
    return QStringLiteral("Deleted");
  }
  if (status == QStringLiteral("R")) {
    return QStringLiteral("Renamed");
  }
  if (status == QStringLiteral("C")) {
    return QStringLiteral("Copied");
  }
  if (status == QStringLiteral("U")) {
    return QStringLiteral("Conflicted");
  }
  if (status == QStringLiteral("?")) {
    return QStringLiteral("Untracked");
  }
  if (status == QStringLiteral("T")) {
    return QStringLiteral("Type changed");
  }
  return QStringLiteral("Changed");
}

QString GitChangeKind(const QString& status) {
  if (status == QStringLiteral("A")) {
    return QStringLiteral("added");
  }
  if (status == QStringLiteral("D")) {
    return QStringLiteral("deleted");
  }
  if (status == QStringLiteral("R") || status == QStringLiteral("C")) {
    return QStringLiteral("renamed");
  }
  if (status == QStringLiteral("?")) {
    return QStringLiteral("untracked");
  }
  if (status == QStringLiteral("U")) {
    return QStringLiteral("conflicted");
  }
  return QStringLiteral("modified");
}

void StyleGitBadge(QLabel* badge, const Theme& theme) {
  if (badge == nullptr) {
    return;
  }
  const QString kind = badge->property("gitChangeKind").toString();
  QColor color = theme.accent;
  QColor background = theme.code_background;
  if (kind == QStringLiteral("added")) {
    color = theme.success;
  } else if (kind == QStringLiteral("deleted")) {
    color = theme.danger;
  } else if (kind == QStringLiteral("renamed")) {
    color = theme.warning;
  } else if (kind == QStringLiteral("untracked")) {
    color = theme.text_secondary;
  } else if (kind == QStringLiteral("conflicted")) {
    color = theme.danger;
  }
  badge->setFont(theme.MonoFont(10, QFont::Bold));
  badge->setStyleSheet(
      QStringLiteral("color: %1; background: %2; border-radius: 4px;")
          .arg(color.name(), background.name()));
}

GitStatusSnapshot ParseGitStatus(const QString& output) {
  GitStatusSnapshot snapshot;
  const QStringList records = output.split(QChar('\0'), Qt::SkipEmptyParts);
  for (int i = 0; i < records.size(); ++i) {
    const QString& record = records[i];
    if (i == 0 && record.startsWith(QStringLiteral("## "))) {
      snapshot.branch = GitBranchText(record);
      continue;
    }
    if (record.size() < 4) {
      continue;
    }
    const QChar index_status = record[0];
    const QChar worktree_status = record[1];
    const QString path = record.mid(3);
    if (path.isEmpty()) {
      continue;
    }
    QString original_path;
    if (index_status == QChar('R') || index_status == QChar('C') ||
        worktree_status == QChar('R') || worktree_status == QChar('C')) {
      if (i + 1 < records.size()) {
        original_path = records[++i];
      }
    }
    if (index_status == QChar('?') && worktree_status == QChar('?')) {
      snapshot.changes.push_back(
          GitFileChange{QStringLiteral("?"), path, QString(), false});
      continue;
    }
    if (index_status != QChar(' ') && index_status != QChar('!')) {
      snapshot.changes.push_back(
          GitFileChange{index_status, path, original_path, true});
    }
    if (worktree_status != QChar(' ') && worktree_status != QChar('!')) {
      snapshot.changes.push_back(
          GitFileChange{worktree_status, path, original_path, false});
    }
  }
  return snapshot;
}

QStringList GitChangePaths(const GitFileChange& change) {
  QStringList paths;
  if (!change.original_path.isEmpty() &&
      change.original_path != change.path) {
    paths << change.original_path;
  }
  paths << change.path;
  return paths;
}

QString GitChangeDisplayPath(const GitFileChange& change) {
  if (!change.original_path.isEmpty() &&
      change.original_path != change.path) {
    return QStringLiteral("%1 → %2").arg(change.original_path, change.path);
  }
  return change.path;
}

QString GitignorePathPattern(const QString& path) {
  QString pattern = path;
  pattern.replace(QChar('\\'), QChar('/'));
  if (pattern.startsWith(QChar('#')) || pattern.startsWith(QChar('!'))) {
    pattern.prepend(QChar('\\'));
  }
  return pattern;
}

QString GitignoreDirectoryPattern(const QString& path) {
  QString directory = path;
  while (directory.endsWith(QChar('/'))) {
    directory.chop(1);
  }
  const int slash = directory.lastIndexOf(QChar('/'));
  if (slash >= 0) {
    return directory.left(slash + 1);
  }
  return QString();
}

QString GitignoreExtensionPattern(const QString& path) {
  QString file = path;
  while (file.endsWith(QChar('/'))) {
    file.chop(1);
  }
  const int slash = file.lastIndexOf(QChar('/'));
  if (slash >= 0) {
    file = file.mid(slash + 1);
  }
  const int dot = file.lastIndexOf(QChar('.'));
  if (dot <= 0 || dot == file.size() - 1) {
    return QString();
  }
  return QStringLiteral("*%1").arg(file.mid(dot));
}

QString GitignoreFilePath(const QString& root) {
  return QDir(root).filePath(QStringLiteral(".gitignore"));
}

QStringList ReadGitignoreEntries(const QString& root) {
  QFile file(GitignoreFilePath(root));
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return {};
  }
  QStringList entries;
  const QStringList lines =
      QString::fromUtf8(file.readAll()).split(QChar('\n'));
  for (const QString& line : lines) {
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith(QChar('#'))) {
      continue;
    }
    entries << trimmed;
  }
  return entries;
}

bool AppendGitignoreEntry(const QString& root, const QString& pattern,
                          QString* error) {
  const QString normalized = pattern.trimmed();
  if (normalized.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("Enter a file, directory, or pattern.");
    }
    return false;
  }
  if (ReadGitignoreEntries(root).contains(normalized)) {
    return true;
  }
  QFile file(GitignoreFilePath(root));
  if (!file.open(QIODevice::ReadWrite | QIODevice::Text)) {
    if (error != nullptr) {
      *error = QStringLiteral("Could not open .gitignore for writing.");
    }
    return false;
  }
  const QByteArray existing = file.readAll();
  if (!existing.isEmpty() && !existing.endsWith('\n')) {
    file.write("\n");
  }
  file.write(normalized.toUtf8());
  file.write("\n");
  if (file.error() != QFileDevice::NoError) {
    if (error != nullptr) {
      *error = QStringLiteral("Could not update .gitignore.");
    }
    return false;
  }
  return true;
}

bool RemoveGitignoreEntry(const QString& root, const QString& pattern,
                          QString* error) {
  QFile source(GitignoreFilePath(root));
  if (!source.open(QIODevice::ReadOnly | QIODevice::Text)) {
    if (error != nullptr) {
      *error = QStringLiteral("Could not read .gitignore.");
    }
    return false;
  }
  const QStringList lines =
      QString::fromUtf8(source.readAll()).split(QChar('\n'));
  QStringList kept;
  bool removed = false;
  for (const QString& line : lines) {
    if (line.trimmed() == pattern) {
      removed = true;
      continue;
    }
    kept << line;
  }
  source.close();
  if (!removed) {
    return true;
  }
  while (!kept.isEmpty() && kept.last().isEmpty()) {
    kept.removeLast();
  }
  QSaveFile target(GitignoreFilePath(root));
  if (!target.open(QIODevice::WriteOnly | QIODevice::Text)) {
    if (error != nullptr) {
      *error = QStringLiteral("Could not write .gitignore.");
    }
    return false;
  }
  if (!kept.isEmpty()) {
    target.write((kept.join(QChar('\n')) + QChar('\n')).toUtf8());
  }
  if (!target.commit()) {
    if (error != nullptr) {
      *error = QStringLiteral("Could not save .gitignore.");
    }
    return false;
  }
  return true;
}

void AppendSignatureField(QString* signature, const QString& value) {
  signature->append(value);
  signature->append(QChar(0x1f));
}

QString TodosSignature(const std::vector<TodoItem>& todos) {
  QString signature;
  signature.reserve(todos.size() * 48);
  for (const TodoItem& todo : todos) {
    AppendSignatureField(&signature, todo.id);
    AppendSignatureField(&signature, todo.content);
    AppendSignatureField(&signature, todo.status);
    AppendSignatureField(&signature, todo.parent);
  }
  return signature;
}

QString TimelineSignature(const std::vector<TimelineEntry>& entries,
                          const QString& query) {
  QString signature;
  signature.reserve(entries.size() * 64);
  AppendSignatureField(&signature, query);
  for (const TimelineEntry& entry : entries) {
    AppendSignatureField(&signature, QString::number(entry.row_id));
    AppendSignatureField(&signature, entry.preview);
    // The preview already carries the visible 160 characters. Its full length
    // catches edits beyond that without keeping a second copy of long prompts.
    AppendSignatureField(&signature,
                         QString::number(entry.full_text.size()));
    AppendSignatureField(
        &signature,
        QString::number(entry.timestamp.toMSecsSinceEpoch()));
  }
  return signature;
}

QString ArtifactRenderSignature(const std::vector<SessionArtifact>& artifacts,
                                const ProfileSnapshot& snapshot) {
  QString signature;
  signature.reserve(artifacts.size() * 96);
  AppendSignatureField(&signature, snapshot.selected_session_id);
  AppendSignatureField(&signature, snapshot.home);
  for (const SessionArtifact& artifact : artifacts) {
    AppendSignatureField(&signature, artifact.label);
    AppendSignatureField(&signature, artifact.value);
    AppendSignatureField(&signature, artifact.kind);
    AppendSignatureField(&signature, artifact.id);
    AppendSignatureField(&signature, artifact.language);
    AppendSignatureField(&signature, artifact.generated ? QStringLiteral("1")
                                                        : QStringLiteral("0"));
    AppendSignatureField(&signature, QString::number(artifact.version_index));
    AppendSignatureField(&signature, QString::number(artifact.versions.size()));
    for (const ArtifactVersion& version : artifact.versions) {
      AppendSignatureField(&signature, version.hash);
      AppendSignatureField(&signature, QString::number(version.created_at_ms));
      if (version.hash.isEmpty()) {
        AppendSignatureField(&signature, version.content);
      }
    }
  }
  return signature;
}

QString CompactRailText(QString text, int max = 180) {
  text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
  text = text.trimmed();
  if (text.size() > max) {
    text = text.left(max - 1) + QChar(0x2026);
  }
  return text;
}

QColor SubagentStatusColor(const Theme& theme, SubagentStatus status) {
  switch (status) {
    case SubagentStatus::kQueued:
      return theme.text_secondary;
    case SubagentStatus::kRunning:
      return theme.accent;
    case SubagentStatus::kCompleted:
      return theme.success;
    case SubagentStatus::kFailed:
      return theme.danger;
    case SubagentStatus::kInterrupted:
      return theme.warning;
  }
  return theme.foreground;
}

QString AgentTitle(const SubagentProgress& agent, qint64 now_ms) {
  QString title =
      SubagentStatusText(agent.status) + QStringLiteral(" · ") + agent.goal;
  if (agent.task_count > 1) {
    title += QStringLiteral(" (%1/%2)")
                 .arg(std::min(agent.task_index, agent.task_count))
                 .arg(agent.task_count);
  }
  if (agent.duration_seconds > 0.0) {
    title +=
        QStringLiteral(" · ") + FormatSubagentDuration(agent.duration_seconds);
  } else if (!SubagentIsTerminal(agent.status) && agent.started_at_ms > 0 &&
             now_ms > agent.started_at_ms) {
    title += QStringLiteral(" · ") +
             FormatSubagentDuration((now_ms - agent.started_at_ms) / 1000.0);
  }
  return title;
}

QString AgentActivity(const SubagentProgress& agent) {
  if (SubagentIsTerminal(agent.status)) {
    if (!agent.summary.isEmpty()) {
      return QStringLiteral("Result: ") + agent.summary;
    }
    if (!agent.stream.empty()) {
      return agent.stream.back().text;
    }
    return QStringLiteral("Finished.");
  }
  if (agent.status == SubagentStatus::kQueued) {
    return QStringLiteral("Waiting to start.");
  }
  if (!agent.stream.empty()) {
    return CompactRailText(agent.stream.back().text);
  }
  if (!agent.current_tool.isEmpty()) {
    return QStringLiteral("Using ") + agent.current_tool;
  }
  return QStringLiteral("Waiting for activity.");
}

QString AgentMeta(const SubagentProgress& agent, qint64 now_ms) {
  QStringList parts;
  if (agent.task_count > 1) {
    parts.append(QStringLiteral("Task %1/%2")
                     .arg(std::min(agent.task_index, agent.task_count))
                     .arg(agent.task_count));
  }
  if (!agent.model.isEmpty()) {
    parts.append(agent.model);
  }
  if (agent.tool_count > 0) {
    parts.append(QStringLiteral("%1 tools").arg(agent.tool_count));
  }
  const qint64 tokens = agent.input_tokens + agent.output_tokens;
  if (tokens > 0) {
    parts.append(QStringLiteral("%1 tokens").arg(FormatSubagentTokens(tokens)));
  }
  if (agent.cost_usd > 0.0) {
    parts.append(QStringLiteral("$%1").arg(
        QString::number(agent.cost_usd, 'f', agent.cost_usd < 0.01 ? 4 : 3)));
  }
  if (!agent.files_written.empty()) {
    parts.append(QStringLiteral("%1 written").arg(agent.files_written.size()));
  }
  if (!agent.files_read.empty()) {
    parts.append(QStringLiteral("%1 read").arg(agent.files_read.size()));
  }
  if (agent.duration_seconds > 0.0) {
    parts.append(FormatSubagentDuration(agent.duration_seconds));
  } else if (!SubagentIsTerminal(agent.status) && agent.updated_at_ms > 0) {
    parts.append(QStringLiteral("updated %1")
                     .arg(FormatSubagentAge(agent.updated_at_ms, now_ms)));
  }
  return parts.isEmpty() ? QStringLiteral("Waiting for progress.")
                         : parts.join(QStringLiteral(" · "));
}

QString AgentStreamSignature(const SubagentProgress& agent) {
  QStringList fields;
  fields.append(SubagentStatusText(agent.status));
  fields.append(agent.summary);
  fields.append(QString::number(agent.stream.size()));
  const std::size_t start =
      agent.stream.size() > 8 ? agent.stream.size() - 8 : 0;
  for (std::size_t i = start; i < agent.stream.size(); ++i) {
    fields.append(agent.stream[i].kind);
    fields.append(agent.stream[i].text);
    fields.append(agent.stream[i].error ? QStringLiteral("1")
                                        : QStringLiteral("0"));
  }
  return fields.join(QChar(0x1f));
}

}  // namespace

QString CompactUsageTokens(qint64 value) {
  value = std::max<qint64>(0, value);
  if (value >= 1000000) {
    const double millions = value / 1000000.0;
    return QStringLiteral("%1M").arg(millions, 0, 'f', millions < 10.0 ? 1 : 0);
  }
  if (value >= 1000) {
    const double thousands = value / 1000.0;
    return QStringLiteral("%1k").arg(thousands, 0, 'f',
                                     thousands < 10.0 ? 1 : 0);
  }
  return QString::number(value);
}

std::vector<SessionArtifact> CollectSessionArtifacts(
    const std::vector<ChatMessage>& messages) {
  std::vector<SessionArtifact> artifacts;
  const std::vector<ArtifactRecord> generated =
      CollectGeneratedArtifacts(messages, QString());
  for (const ArtifactRecord& record : generated) {
    SessionArtifact artifact;
    artifact.id = record.id;
    artifact.label = record.title;
    artifact.kind = ArtifactKindName(record.kind);
    artifact.language = record.language;
    artifact.generated = true;
    artifact.versions = record.versions;
    artifact.version_index =
        std::max(0, static_cast<int>(record.versions.size()) - 1);
    artifacts.push_back(std::move(artifact));
  }

  QSet<QString> seen;
  for (auto message = messages.rbegin(); message != messages.rend();
       ++message) {
    for (auto block = message->blocks.rbegin(); block != message->blocks.rend();
         ++block) {
      if (block->kind != BlockKind::kArtifact &&
          block->kind != BlockKind::kImage) {
        continue;
      }
      const QString value = block->detail.trimmed();
      if (value.isEmpty() || seen.contains(value)) {
        continue;
      }
      seen.insert(value);
      SessionArtifact artifact;
      artifact.value = value;
      artifact.kind = block->kind == BlockKind::kImage
                          ? QStringLiteral("image")
                          : QStringLiteral("artifact");
      artifact.label = block->text.trimmed();
      if (artifact.label.isEmpty()) {
        artifact.label = block->language.trimmed();
      }
      if (artifact.label.isEmpty()) {
        artifact.label = QFileInfo(value).fileName();
      }
      if (artifact.label.isEmpty()) {
        artifact.label = value;
      }
      artifacts.push_back(std::move(artifact));
    }
  }
  return artifacts;
}

std::vector<TimelineEntry> CollectTimelineEntries(
    const std::vector<ChatMessage>& messages) {
  std::vector<TimelineEntry> entries;
  for (const ChatMessage& message : messages) {
    if (message.role != MessageRole::kUser) {
      continue;
    }
    QString preview;
    for (const MessageBlock& block : message.blocks) {
      if (block.kind == BlockKind::kTool) {
        continue;
      }
      preview = block.text.trimmed();
      if (!preview.isEmpty()) {
        break;
      }
    }
    preview = preview.simplified();
    if (preview.isEmpty()) {
      continue;
    }
    const QString full_text = preview;
    if (preview.size() > 160) {
      preview = preview.left(157) + QStringLiteral("…");
    }
    entries.push_back(
        TimelineEntry{message.row_id, preview, message.timestamp, full_text});
  }
  return entries;
}

// Rows are buttons with child labels because QPushButton paints only its first
// line. The title/activity labels also make live status updates cheap: the
// roster can refresh without rebuilding the steer field and losing focus.
class AgentHeaderButton : public QPushButton {
 public:
  AgentHeaderButton(const QString& id, const Theme& theme, QWidget* parent)
      : QPushButton(parent), theme_(theme) {
    setObjectName(QStringLiteral("rightRailAgentHeader_") + id);
    setProperty("railAgentRow", true);
    setFlat(true);
    setText(QString());
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    setMinimumHeight(38);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(2);
    auto* top = new QHBoxLayout();
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(6);
    title_ = new QLabel(this);
    title_->setObjectName(QStringLiteral("rightRailAgent_") + id);
    title_->setProperty("railAgentTitle", true);
    title_->setWordWrap(true);
    title_->setTextFormat(Qt::PlainText);
    title_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    QSizePolicy title_policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    title_policy.setHeightForWidth(true);
    title_->setSizePolicy(title_policy);
    top->addWidget(title_, 1);
    chevron_ = new QLabel(this);
    chevron_->setObjectName(QStringLiteral("rightRailAgentChevron_") + id);
    chevron_->setText(QStringLiteral("▸"));
    chevron_->setProperty("railAgentChevron", true);
    chevron_->setAlignment(Qt::AlignTop | Qt::AlignRight);
    chevron_->setFixedWidth(12);
    chevron_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    top->addWidget(chevron_);
    layout->addLayout(top);

    activity_ = new QLabel(this);
    activity_->setObjectName(QStringLiteral("rightRailAgentActivity_") + id);
    activity_->setProperty("railAgentActivity", true);
    activity_->setWordWrap(true);
    activity_->setTextFormat(Qt::PlainText);
    activity_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    QSizePolicy activity_policy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    activity_policy.setHeightForWidth(true);
    activity_->setSizePolicy(activity_policy);
    layout->addWidget(activity_);
    SetTheme(theme_);
  }

  void SetTheme(const Theme& theme) {
    theme_ = theme;
    setFont(theme.UiFont(11, QFont::Normal));
    title_->setFont(theme.UiFont(11, QFont::DemiBold));
    activity_->setFont(theme.UiFont(10, QFont::Normal));
    chevron_->setFont(theme.UiFont(10, QFont::Normal));
    chevron_->setStyleSheet(
        QStringLiteral("color: %1; background: transparent;")
            .arg(theme.text_secondary.name()));
    ApplyColors();
  }

  void SetContent(const SubagentProgress& agent, int depth, qint64 now_ms) {
    title_->setText(AgentTitle(agent, now_ms));
    activity_->setText(AgentActivity(agent));
    title_color_ = SubagentStatusColor(theme_, agent.status);
    activity_color_ =
        !agent.stream.empty() && agent.stream.back().error
            ? theme_.danger
            : (agent.status == SubagentStatus::kQueued ? theme_.text_tertiary
                                                       : theme_.text_secondary);
    ApplyColors();
    if (auto* box = qobject_cast<QVBoxLayout*>(layout())) {
      box->setContentsMargins(8 + std::max(0, depth) * 12, 6, 8, 6);
    }
    setToolTip(WrappedToolTip(agent.goal + QStringLiteral("\n\n") +
                              AgentMeta(agent, now_ms)));
    setAccessibleName(title_->text());
    updateGeometry();
  }

  void SetExpanded(bool expanded) {
    expanded_ = expanded;
    chevron_->setText(expanded ? QStringLiteral("▾") : QStringLiteral("▸"));
    setProperty("expanded", expanded);
    style()->unpolish(this);
    style()->polish(this);
  }

  bool hasHeightForWidth() const override { return true; }

  int heightForWidth(int width) const override {
    if (layout() == nullptr) {
      return QPushButton::heightForWidth(width);
    }
    return layout()->heightForWidth(width);
  }

  QSize sizeHint() const override {
    if (layout() == nullptr) {
      return QPushButton::sizeHint();
    }
    QSize hint = layout()->sizeHint();
    const int height = layout()->heightForWidth(std::max(width(), 220));
    if (height > 0) {
      hint.setHeight(height);
    }
    return hint;
  }

  QSize minimumSizeHint() const override { return QSize(0, 38); }

 protected:
  void resizeEvent(QResizeEvent* event) override {
    QPushButton::resizeEvent(event);
    if (layout() == nullptr) {
      return;
    }
    const int target = layout()->heightForWidth(width());
    if (target > 0 && height() != target) {
      setFixedHeight(target);
    }
  }

 private:
  void ApplyColors() {
    title_->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
                              .arg(title_color_.name()));
    activity_->setStyleSheet(
        QStringLiteral("color: %1; background: transparent;")
            .arg(activity_color_.name()));
  }

  Theme theme_;
  QLabel* title_ = nullptr;
  QLabel* activity_ = nullptr;
  QLabel* chevron_ = nullptr;
  QColor title_color_;
  QColor activity_color_;
  bool expanded_ = false;
};

// A repository header keeps the name and change count visible while the card
// body is collapsed; the full path stays in the tooltip. Child labels are
// mouse-transparent so the whole row is one expand/collapse target.
class GitRepoHeaderButton : public QPushButton {
 public:
  GitRepoHeaderButton(const Theme& theme, const QString& suffix,
                      QWidget* parent)
      : QPushButton(parent) {
    setObjectName(QStringLiteral("rightRailGitRepoHeader") + suffix);
    setProperty("gitRepoHeader", true);
    setProperty("expanded", true);
    setFlat(true);
    setText(QString());
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumHeight(28);
    setMinimumWidth(0);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(3, 2, 3, 2);
    layout->setSpacing(6);
    chevron_ = new QLabel(this);
    chevron_->setObjectName(QStringLiteral("rightRailGitRepoChevron") +
                            suffix);
    chevron_->setText(QStringLiteral("\u25be"));
    chevron_->setFixedWidth(12);
    chevron_->setAlignment(Qt::AlignCenter);
    chevron_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    layout->addWidget(chevron_);
    title_ = new QLabel(this);
    title_->setObjectName(QStringLiteral("rightRailGitRepoName") + suffix);
    title_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    title_->setMinimumWidth(0);
    title_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    layout->addWidget(title_, 1);
    badge_ = new QLabel(this);
    badge_->setObjectName(QStringLiteral("rightRailGitRepoBadge") + suffix);
    badge_->setProperty("gitRepoBadge", true);
    badge_->setAlignment(Qt::AlignCenter);
    badge_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    badge_->hide();
    layout->addWidget(badge_);
    SetTheme(theme);
  }

  void SetTheme(const Theme& theme) {
    chevron_->setFont(theme.UiFont(10, QFont::Normal));
    title_->setFont(theme.UiFont(11, QFont::DemiBold));
    badge_->setFont(theme.MonoFont(9, QFont::Bold));
    chevron_->setStyleSheet(
        QStringLiteral("color: %1; background: transparent;")
            .arg(theme.text_secondary.name()));
    title_->setStyleSheet(
        QStringLiteral("color: %1; background: transparent;")
            .arg(theme.foreground.name()));
    badge_->setStyleSheet(
        QStringLiteral("color: %1; background: %2; border-radius: 7px;"
                       "padding: 1px 6px;")
            .arg(theme.accent.name(), theme.code_background.name()));
    UpdateTooltip();
  }

  void SetRepo(const QString& label, const QString& path) {
    label_ = label;
    path_ = path;
    title_->setText(label_);
    UpdateTooltip();
  }

  void SetExpanded(bool expanded) {
    expanded_ = expanded;
    chevron_->setText(expanded ? QStringLiteral("\u25be")
                               : QStringLiteral("\u25b8"));
    setProperty("expanded", expanded);
    style()->unpolish(this);
    style()->polish(this);
    UpdateTooltip();
  }

  void SetChangeCount(int count) {
    changes_ = std::max(0, count);
    badge_->setText(changes_ > 0 ? QStringLiteral("\u25cf %1").arg(changes_)
                                 : QString());
    badge_->setVisible(changes_ > 0);
    badge_->setToolTip(
        changes_ > 0
            ? QStringLiteral("%1 changed file%2")
                  .arg(changes_)
                  .arg(changes_ == 1 ? QString() : QStringLiteral("s"))
            : QString());
    UpdateTooltip();
  }

 private:
  void UpdateTooltip() {
    QString text = label_;
    if (!path_.isEmpty()) {
      text += QStringLiteral("\n") + QDir::toNativeSeparators(path_);
    }
    if (changes_ > 0) {
      text += QStringLiteral("\n%1 changed file(s)").arg(changes_);
    }
    text += expanded_ ? QStringLiteral("\nClick to collapse.")
                      : QStringLiteral("\nClick to expand.");
    setToolTip(text);
    QString accessible = label_;
    if (changes_ > 0) {
      accessible += QStringLiteral(", %1 changed files").arg(changes_);
    }
    accessible += expanded_ ? QStringLiteral(", expanded")
                            : QStringLiteral(", collapsed");
    setAccessibleName(accessible);
  }

  QLabel* chevron_ = nullptr;
  QLabel* title_ = nullptr;
  QLabel* badge_ = nullptr;
  QString label_;
  QString path_;
  int changes_ = 0;
  bool expanded_ = true;
};

RightRail::RightRail(const Theme& theme, QWidget* parent)
    : QWidget(parent), theme_(theme) {
  setObjectName(QStringLiteral("rightRail"));
  setMinimumWidth(260);
  setMaximumWidth(800);
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
  setAttribute(Qt::WA_StyledBackground, true);
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  // The rail is an accordion, not a tab strip: each surface is a collapsible
  // row with the same disclosure affordance as a tool call. One pane scroll
  // owns the rail so expanding a section never leaves a tiny nested viewport.
  workspace_ = new QWidget(this);
  workspace_->setObjectName(QStringLiteral("rightRailWorkspace"));
  workspace_->setAttribute(Qt::WA_StyledBackground, true);
  auto* workspace_layout = new QVBoxLayout(workspace_);
  workspace_layout->setContentsMargins(0, 0, 0, 0);
  workspace_layout->setSpacing(0);
  auto* header = new QWidget(workspace_);
  header->setObjectName(QStringLiteral("rightRailHeader"));
  header->setAttribute(Qt::WA_StyledBackground, true);
  auto* header_layout = new QHBoxLayout(header);
  header_layout->setContentsMargins(12, 7, 7, 7);
  header_layout->setSpacing(6);
  auto* title = new QLabel(QStringLiteral("Workspace"), header);
  title->setObjectName(QStringLiteral("rightRailTitle"));
  title->setFont(theme_.UiFont(12, QFont::DemiBold));
  header_layout->addWidget(title, 1);
  auto* collapse = PreviewAction(header, QStringLiteral("›"),
                                 QStringLiteral("rightRailCollapse"));
  collapse->setFixedSize(22, 22);
  collapse->setToolTip(QStringLiteral("Collapse sidebar"));
  header_layout->addWidget(collapse);
  workspace_layout->addWidget(header);
  sections_scroll_ = new QScrollArea(workspace_);
  sections_scroll_->setObjectName(QStringLiteral("rightRailSectionsScroll"));
  sections_scroll_->setWidgetResizable(true);
  sections_scroll_->setFrameShape(QFrame::NoFrame);
  sections_scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  sections_host_ = new QWidget(sections_scroll_);
  sections_host_->setObjectName(QStringLiteral("rightRailSections"));
  sections_host_->setAttribute(Qt::WA_StyledBackground, true);
  sections_host_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  sections_layout_ = new QVBoxLayout(sections_host_);
  sections_layout_->setContentsMargins(0, 0, 0, 10);
  sections_layout_->setSpacing(0);
  sections_scroll_->setWidget(sections_host_);
  workspace_layout->addWidget(sections_scroll_, 1);
  root->addWidget(workspace_);
  QObject::connect(collapse, &QPushButton::clicked, collapse, [this]() {
    SetCollapsed(true);
    if (on_collapsed_changed) {
      on_collapsed_changed(true);
    }
  });

  // Project files, git and a local shell live beside the chat transcript.
  // They are scoped by SetProjectContext rather than inferred from messages.
  project_pane_ = new QWidget(sections_host_);
  project_pane_->setObjectName(QStringLiteral("rightRailProjectPane"));
  auto* project_layout = new QVBoxLayout(project_pane_);
  project_layout->setContentsMargins(10, 10, 10, 10);
  project_layout->setSpacing(6);
  project_root_label_ = RailLabel(project_pane_, theme_, QString(), 11,
                                  QFont::DemiBold, theme_.text_secondary);
  project_root_label_->setObjectName(QStringLiteral("rightRailProjectRoot"));
  project_layout->addWidget(project_root_label_);
  auto* project_split = new QSplitter(Qt::Vertical, project_pane_);
  project_split->setObjectName(QStringLiteral("rightRailProjectSplit"));
  project_model_ = new QFileSystemModel(project_pane_);
  project_model_->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);
  project_model_->setRootPath(QDir::homePath());
  project_tree_ = new QTreeView(project_split);
  project_tree_->setObjectName(QStringLiteral("rightRailProjectTree"));
  project_tree_->setModel(project_model_);
  project_tree_->setRootIndex(project_model_->index(QDir::homePath()));
  project_tree_->setHeaderHidden(false);
  project_tree_->setAnimated(false);
  project_tree_->setUniformRowHeights(true);
  project_tree_->setTextElideMode(Qt::ElideMiddle);
  project_tree_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  project_tree_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
  project_tree_->setMinimumHeight(120);
  project_tree_->setFont(theme_.UiFont(11, QFont::Normal));
  QHeaderView* project_header = project_tree_->header();
  project_header->setStretchLastSection(false);
  project_header->setMinimumSectionSize(48);
  project_header->setSectionResizeMode(QHeaderView::Interactive);
  project_header->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  project_header->resizeSection(0, 240);  // Name
  project_header->resizeSection(1, 80);   // Size
  project_header->resizeSection(2, 110);  // Type
  project_header->resizeSection(3, 150);  // Date Modified
  project_split->addWidget(project_tree_);
  project_preview_ = new QTextBrowser(project_split);
  project_preview_->setObjectName(QStringLiteral("rightRailProjectPreview"));
  project_preview_->setOpenExternalLinks(true);
  project_preview_->setLineWrapMode(QTextEdit::WidgetWidth);
  project_preview_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
  project_preview_->setMinimumHeight(120);
  project_preview_->setFont(theme_.UiFont(11, QFont::Normal));
  project_split->addWidget(project_preview_);
  project_split->setSizes({240, 180});
  project_layout->addWidget(project_split, 1);
  QObject::connect(
      project_tree_, &QTreeView::clicked, project_tree_,
      [this](const QModelIndex& index) {
        if (project_model_ == nullptr || !project_model_->isDir(index)) {
          ShowProjectPreview(project_model_->filePath(index));
        }
      });
  QObject::connect(
      project_tree_, &QTreeView::doubleClicked, project_tree_,
      [this](const QModelIndex& index) {
        if (project_model_ == nullptr || project_model_->isDir(index)) {
          return;
        }
        QDesktopServices::openUrl(
            QUrl::fromLocalFile(project_model_->filePath(index)));
      });
  project_pane_->setMinimumHeight(360);
  AddSection(QStringLiteral("project"), QStringLiteral("Project"),
             project_pane_);

  git_pane_ = new QWidget(sections_host_);
  git_pane_->setObjectName(QStringLiteral("rightRailGitPane"));
  auto* git_layout = new QVBoxLayout(git_pane_);
  git_layout->setContentsMargins(10, 10, 10, 10);
  git_layout->setSpacing(6);
  git_root_label_ = RailLabel(git_pane_, theme_, QString(), 11, QFont::DemiBold,
                              theme_.text_secondary);
  git_root_label_->setObjectName(QStringLiteral("rightRailGitRoot"));
  git_layout->addWidget(git_root_label_);
  git_repositories_host_ = new QWidget(git_pane_);
  git_repositories_host_->setObjectName(
      QStringLiteral("rightRailGitRepositories"));
  git_repositories_layout_ = new QVBoxLayout(git_repositories_host_);
  git_repositories_layout_->setContentsMargins(0, 0, 0, 0);
  git_repositories_layout_->setSpacing(10);
  git_layout->addWidget(git_repositories_host_, 1);
  git_repositories_host_->setVisible(false);
  git_empty_ =
      RailLabel(git_pane_, theme_, QStringLiteral("No git repositories found."),
                11, QFont::Normal, theme_.text_tertiary);
  git_empty_->setObjectName(QStringLiteral("rightRailGitEmpty"));
  git_layout->addWidget(git_empty_);
  git_pane_->setMinimumHeight(240);
  AddSection(QStringLiteral("git"), QStringLiteral("Git"), git_pane_);

  terminal_pane_ = new QWidget(sections_host_);
  terminal_pane_->setObjectName(QStringLiteral("rightRailTerminalPane"));
  auto* terminal_layout = new QVBoxLayout(terminal_pane_);
  terminal_layout->setContentsMargins(8, 8, 8, 8);
  terminal_layout->setSpacing(5);
  auto* terminal_actions = new FlowLayout(0, 4, 4);
  auto* terminal_title =
      RailLabel(terminal_pane_, theme_, QStringLiteral("Terminal"), 11,
                QFont::DemiBold, theme_.text_secondary);
  terminal_layout->addWidget(terminal_title);
  terminal_copy_ = PreviewAction(terminal_pane_, QStringLiteral("Copy"),
                                 QStringLiteral("rightRailTerminalCopy"));
  terminal_copy_->setToolTip(
      QStringLiteral("Copy selected terminal text (Cmd/Ctrl+Shift+C)"));
  terminal_actions->addWidget(terminal_copy_);
  terminal_paste_ = PreviewAction(terminal_pane_, QStringLiteral("Paste"),
                                  QStringLiteral("rightRailTerminalPaste"));
  terminal_paste_->setToolTip(
      QStringLiteral("Paste clipboard text (Cmd/Ctrl+Shift+V)"));
  terminal_actions->addWidget(terminal_paste_);
  terminal_clear_ = PreviewAction(terminal_pane_, QStringLiteral("Clear"),
                                  QStringLiteral("rightRailTerminalClear"));
  terminal_actions->addWidget(terminal_clear_);
  terminal_restart_ = PreviewAction(terminal_pane_, QStringLiteral("Restart"),
                                    QStringLiteral("rightRailTerminalRestart"));
  terminal_actions->addWidget(terminal_restart_);
  terminal_layout->addLayout(terminal_actions);
  terminal_ = new TerminalWidget(terminal_pane_);
  terminal_->setMinimumHeight(260);
  terminal_layout->addWidget(terminal_, 1);
  QObject::connect(terminal_copy_, &QPushButton::clicked, terminal_copy_,
                   [this]() {
                     if (terminal_ != nullptr) {
                       terminal_->CopySelection();
                       terminal_->setFocus(Qt::OtherFocusReason);
                     }
                   });
  QObject::connect(terminal_paste_, &QPushButton::clicked, terminal_paste_,
                   [this]() {
                     if (terminal_ != nullptr) {
                       terminal_->PasteClipboard();
                       terminal_->setFocus(Qt::OtherFocusReason);
                     }
                   });
  QObject::connect(terminal_clear_, &QPushButton::clicked, terminal_clear_,
                   [this]() {
                     if (terminal_ != nullptr) {
                       terminal_->ClearDisplay();
                     }
                   });
  QObject::connect(terminal_restart_, &QPushButton::clicked, terminal_restart_,
                   [this]() { RestartTerminal(); });
  QObject::connect(terminal_, &TerminalWidget::StartFailed, this,
                   [this](const QString& error) {
                     if (terminal_ != nullptr) {
                       terminal_->FeedForTest(
                           QStringLiteral("\r\n[%1]\r\n").arg(error).toUtf8());
                     }
                   });
  terminal_pane_->setMinimumHeight(360);
  AddSection(QStringLiteral("terminal"), QStringLiteral("Terminal"),
             terminal_pane_);

  collapsed_bar_ = new QWidget(this);
  collapsed_bar_->setObjectName(QStringLiteral("rightRailCollapsedBar"));
  auto* collapsed_layout = new QVBoxLayout(collapsed_bar_);
  collapsed_layout->setContentsMargins(5, 8, 5, 8);
  collapsed_layout->setSpacing(5);
  auto* expand = PreviewAction(collapsed_bar_, QStringLiteral("‹"),
                               QStringLiteral("rightRailExpand"));
  expand->setFixedSize(32, 26);
  collapsed_layout->addWidget(expand);
  QObject::connect(expand, &QPushButton::clicked, expand, [this]() {
    SetCollapsed(false);
    if (on_collapsed_changed) {
      on_collapsed_changed(false);
    }
  });
  collapsed_layout->addStretch(1);
  collapsed_bar_->hide();
  root->addWidget(collapsed_bar_);

  context_pane_ = new QWidget(sections_host_);
  context_pane_->setObjectName(QStringLiteral("rightRailContextPane"));
  auto* context_layout = new QVBoxLayout(context_pane_);
  context_layout->setContentsMargins(12, 10, 12, 12);
  context_layout->setSpacing(8);
  context_pane_->setMinimumHeight(220);
  AddSection(QStringLiteral("context"), QStringLiteral("Context"),
             context_pane_);
  QWidget* context_content = context_pane_;
  context_session_ =
      RailLabel(context_content, theme_, QString(), 13, QFont::DemiBold);
  context_session_->setObjectName(QStringLiteral("rightRailSession"));
  context_layout->addWidget(context_session_);
  context_summary_ = RailLabel(context_content, theme_, QString(), 12);
  context_summary_->setObjectName(QStringLiteral("rightRailContextSummary"));
  context_layout->addWidget(context_summary_);
  context_bar_ = new QProgressBar(context_content);
  context_bar_->setObjectName(QStringLiteral("rightRailContextBar"));
  context_bar_->setRange(0, 100);
  context_bar_->setTextVisible(false);
  context_bar_->setFixedHeight(6);
  context_layout->addWidget(context_bar_);
  context_detail_ = RailLabel(context_content, theme_, QString(), 11,
                              QFont::Normal, theme_.text_secondary);
  context_detail_->setObjectName(QStringLiteral("rightRailContextDetail"));
  context_layout->addWidget(context_detail_);
  context_control_ = RailLabel(context_content, theme_, QString(), 11,
                               QFont::Normal, theme_.text_secondary);
  context_control_->setObjectName(QStringLiteral("rightRailContextControl"));
  context_control_->setWordWrap(true);
  context_layout->addWidget(context_control_);
  context_notice_ = RailLabel(context_content, theme_, QString(), 11,
                              QFont::Normal, theme_.text_secondary);
  context_notice_->setObjectName(QStringLiteral("rightRailContextNotice"));
  context_notice_->setWordWrap(true);
  context_layout->addWidget(context_notice_);
  context_layout->addStretch(1);

  agents_pane_ = new QWidget(sections_host_);
  agents_pane_->setObjectName(QStringLiteral("rightRailAgentsPane"));
  auto* agents_layout = new QVBoxLayout(agents_pane_);
  agents_layout->setContentsMargins(12, 10, 12, 12);
  agents_layout->setSpacing(8);
  agents_pane_->setMinimumHeight(180);
  AddSection(QStringLiteral("agents"), QStringLiteral("Agents"), agents_pane_);
  QWidget* agents_content = agents_pane_;
  agents_empty_ = RailLabel(agents_content, theme_,
                            QStringLiteral("No delegated agents yet."), 12,
                            QFont::Normal, theme_.text_secondary);
  agents_empty_->setObjectName(QStringLiteral("rightRailAgentsEmpty"));
  agents_layout->addWidget(agents_empty_);
  agents_list_ = new QWidget(agents_content);
  agents_list_->setObjectName(QStringLiteral("rightRailAgentsList"));
  auto* agent_rows = new QVBoxLayout(agents_list_);
  agent_rows->setContentsMargins(0, 0, 0, 0);
  agent_rows->setSpacing(6);
  agents_layout->addWidget(agents_list_);
  agents_layout->addStretch(1);

  todos_pane_ = new QWidget(sections_host_);
  todos_pane_->setObjectName(QStringLiteral("rightRailTodosPane"));
  auto* todos_layout = new QVBoxLayout(todos_pane_);
  todos_layout->setContentsMargins(12, 10, 12, 12);
  todos_layout->setSpacing(8);
  todos_pane_->setMinimumHeight(180);
  AddSection(QStringLiteral("todos"), QStringLiteral("Todo"), todos_pane_);
  QWidget* todos_content = todos_pane_;
  todos_summary_ = RailLabel(todos_content, theme_, QString(), 11,
                             QFont::Normal, theme_.text_secondary);
  todos_summary_->setObjectName(QStringLiteral("rightRailTodoSummary"));
  todos_layout->addWidget(todos_summary_);
  todos_empty_ = RailLabel(todos_content, theme_,
                           QStringLiteral("No task list for this chat."), 12,
                           QFont::Normal, theme_.text_secondary);
  todos_empty_->setObjectName(QStringLiteral("rightRailTodosEmpty"));
  todos_layout->addWidget(todos_empty_);
  todos_list_ = new QWidget(todos_content);
  todos_list_->setObjectName(QStringLiteral("rightRailTodosList"));
  auto* todo_rows = new QVBoxLayout(todos_list_);
  todo_rows->setContentsMargins(0, 0, 0, 0);
  todo_rows->setSpacing(5);
  todos_layout->addWidget(todos_list_);
  todos_layout->addStretch(1);

  timeline_pane_ = new QWidget(sections_host_);
  timeline_pane_->setObjectName(QStringLiteral("rightRailTimelinePane"));
  auto* timeline_layout = new QVBoxLayout(timeline_pane_);
  timeline_layout->setContentsMargins(12, 10, 12, 12);
  timeline_layout->setSpacing(8);
  timeline_pane_->setMinimumHeight(240);
  AddSection(QStringLiteral("timeline"), QStringLiteral("Timeline"),
             timeline_pane_);
  QWidget* timeline_content = timeline_pane_;
  timeline_search_ = new QLineEdit(timeline_content);
  timeline_search_->setObjectName(QStringLiteral("rightRailTimelineSearch"));
  timeline_search_->setPlaceholderText(QStringLiteral("Search prompts"));
  timeline_search_->setClearButtonEnabled(true);
  timeline_layout->addWidget(timeline_search_);
  timeline_empty_ =
      RailLabel(timeline_content, theme_,
                QStringLiteral("Prompts from this chat appear here."), 12,
                QFont::Normal, theme_.text_secondary);
  timeline_empty_->setObjectName(QStringLiteral("rightRailTimelineEmpty"));
  timeline_layout->addWidget(timeline_empty_);
  timeline_list_ = new QWidget(timeline_content);
  timeline_list_->setObjectName(QStringLiteral("rightRailTimelineList"));
  auto* timeline_rows = new QVBoxLayout(timeline_list_);
  timeline_rows->setContentsMargins(0, 0, 0, 0);
  timeline_rows->setSpacing(4);
  timeline_layout->addWidget(timeline_list_);
  timeline_layout->addStretch(1);
  QObject::connect(timeline_search_, &QLineEdit::textChanged, timeline_search_,
                   [this]() { RebuildTimelineRows(); });
  AttachContextCompleter(timeline_search_, [this]() {
    QStringList prompts;
    for (const TimelineEntry& entry : timeline_entries_) {
      if (!entry.preview.trimmed().isEmpty()) {
        prompts.append(entry.preview.trimmed());
      }
      if (prompts.size() >= 200) {
        break;
      }
    }
    return prompts;
  });

  artifacts_pane_ = new QWidget(sections_host_);
  artifacts_pane_->setObjectName(QStringLiteral("rightRailArtifactsPane"));
  auto* artifacts_layout = new QVBoxLayout(artifacts_pane_);
  artifacts_layout->setContentsMargins(12, 10, 12, 12);
  artifacts_layout->setSpacing(8);
  artifacts_pane_->setMinimumHeight(320);
  AddSection(QStringLiteral("artifacts"), QStringLiteral("Outputs"),
             artifacts_pane_);
  QWidget* artifacts_content = artifacts_pane_;
  artifacts_empty_ = RailLabel(
      artifacts_content, theme_,
      QStringLiteral("Artifacts and images from this chat appear here."), 12,
      QFont::Normal, theme_.text_secondary);
  artifacts_empty_->setObjectName(QStringLiteral("rightRailArtifactsEmpty"));
  artifacts_layout->addWidget(artifacts_empty_);
  artifacts_list_ = new QWidget(artifacts_content);
  artifacts_list_->setObjectName(QStringLiteral("rightRailArtifactsList"));
  auto* artifact_rows = new QVBoxLayout(artifacts_list_);
  artifact_rows->setContentsMargins(0, 0, 0, 0);
  artifact_rows->setSpacing(6);
  artifacts_layout->addWidget(artifacts_list_);
  artifact_preview_ = new QWidget(artifacts_content);
  artifact_preview_->setObjectName(QStringLiteral("rightRailArtifactPreview"));
  auto* preview_layout = new QVBoxLayout(artifact_preview_);
  preview_layout->setContentsMargins(8, 8, 8, 8);
  preview_layout->setSpacing(6);
  artifact_preview_title_ = RailLabel(artifact_preview_, theme_, QString(), 11,
                                      QFont::DemiBold, theme_.foreground);
  artifact_preview_title_->setObjectName(
      QStringLiteral("rightRailArtifactPreviewTitle"));
  preview_layout->addWidget(artifact_preview_title_);

  auto* version_row = new QWidget(artifact_preview_);
  version_row->setObjectName(QStringLiteral("rightRailArtifactVersionRow"));
  auto* version_layout = new QHBoxLayout(version_row);
  version_layout->setContentsMargins(0, 0, 0, 0);
  version_layout->setSpacing(4);
  artifact_version_prev_ =
      PreviewAction(version_row, QStringLiteral("‹"),
                    QStringLiteral("rightRailArtifactVersionPrev"));
  artifact_version_prev_->setFixedWidth(22);
  version_layout->addWidget(artifact_version_prev_);
  artifact_version_label_ = RailLabel(version_row, theme_, QString(), 10,
                                      QFont::DemiBold, theme_.text_secondary);
  artifact_version_label_->setObjectName(
      QStringLiteral("rightRailArtifactVersionLabel"));
  artifact_version_label_->setAlignment(Qt::AlignCenter);
  version_layout->addWidget(artifact_version_label_, 1);
  artifact_version_next_ =
      PreviewAction(version_row, QStringLiteral("›"),
                    QStringLiteral("rightRailArtifactVersionNext"));
  artifact_version_next_->setFixedWidth(22);
  version_layout->addWidget(artifact_version_next_);
  preview_layout->addWidget(version_row);
  version_row->hide();

  artifact_preview_body_ = RailLabel(artifact_preview_, theme_, QString(), 11,
                                     QFont::Normal, theme_.text_secondary);
  artifact_preview_body_->setObjectName(
      QStringLiteral("rightRailArtifactPreviewBody"));
  artifact_preview_body_->setWordWrap(true);
  artifact_preview_body_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  preview_layout->addWidget(artifact_preview_body_);

  artifact_preview_document_ = new QTextBrowser(artifact_preview_);
  artifact_preview_document_->setObjectName(
      QStringLiteral("rightRailArtifactPreviewDocument"));
  artifact_preview_document_->setMinimumHeight(240);
  artifact_preview_document_->setOpenExternalLinks(true);
  preview_layout->addWidget(artifact_preview_document_);
  artifact_preview_document_->hide();

  artifact_preview_svg_ = new QSvgWidget(artifact_preview_);
  artifact_preview_svg_->setObjectName(
      QStringLiteral("rightRailArtifactPreviewSvg"));
  artifact_preview_svg_->setMinimumHeight(220);
  preview_layout->addWidget(artifact_preview_svg_);
  artifact_preview_svg_->hide();

  auto* action_row = new QWidget(artifact_preview_);
  action_row->setObjectName(QStringLiteral("rightRailArtifactPreviewActions"));
  auto* action_layout = new FlowLayout(action_row, 0, 4, 4);
  artifact_preview_copy_ =
      PreviewAction(action_row, QStringLiteral("Copy"),
                    QStringLiteral("rightRailArtifactPreviewCopy"));
  action_layout->addWidget(artifact_preview_copy_);
  artifact_preview_save_ =
      PreviewAction(action_row, QStringLiteral("Save…"),
                    QStringLiteral("rightRailArtifactPreviewSave"));
  action_layout->addWidget(artifact_preview_save_);
  artifact_preview_source_ =
      PreviewAction(action_row, QStringLiteral("Source"),
                    QStringLiteral("rightRailArtifactPreviewSource"));
  action_layout->addWidget(artifact_preview_source_);
  artifact_preview_browser_ =
      PreviewAction(action_row, QStringLiteral("Browser"),
                    QStringLiteral("rightRailArtifactPreviewBrowser"));
  action_layout->addWidget(artifact_preview_browser_);
  artifact_preview_open_ =
      PreviewAction(action_row, QStringLiteral("Open"),
                    QStringLiteral("rightRailArtifactPreviewOpen"));
  action_layout->addWidget(artifact_preview_open_);
  preview_layout->addWidget(action_row);
  artifact_preview_copy_->hide();
  artifact_preview_save_->hide();
  artifact_preview_source_->hide();
  artifact_preview_browser_->hide();

  QObject::connect(
      artifact_version_prev_, &QPushButton::clicked, artifact_version_prev_,
      [this]() { ShowArtifactVersion(preview_artifact_.version_index - 1); });
  QObject::connect(
      artifact_version_next_, &QPushButton::clicked, artifact_version_next_,
      [this]() { ShowArtifactVersion(preview_artifact_.version_index + 1); });
  QObject::connect(
      artifact_preview_copy_, &QPushButton::clicked, artifact_preview_copy_,
      [this]() {
        const QString content = CurrentArtifactContent(preview_artifact_);
        if (content.isEmpty()) {
          return;
        }
        QGuiApplication::clipboard()->setText(content);
        artifact_preview_copy_->setText(QStringLiteral("Copied"));
        QTimer::singleShot(1200, this, [this]() {
          if (artifact_preview_copy_ != nullptr) {
            artifact_preview_copy_->setText(QStringLiteral("Copy"));
          }
        });
      });
  QObject::connect(
      artifact_preview_save_, &QPushButton::clicked, artifact_preview_save_,
      [this]() {
        if (on_artifact_save == nullptr) {
          return;
        }
        on_artifact_save(
            preview_artifact_,
            ArtifactDownloadName(RailArtifactKind(preview_artifact_.kind),
                                 preview_artifact_.language,
                                 preview_artifact_.label));
      });
  QObject::connect(artifact_preview_source_, &QPushButton::clicked,
                   artifact_preview_source_, [this]() {
                     artifact_show_source_ = !artifact_show_source_;
                     RenderArtifactPreview();
                   });
  QObject::connect(artifact_preview_browser_, &QPushButton::clicked,
                   artifact_preview_browser_, [this]() {
                     const QString content =
                         CurrentArtifactContent(preview_artifact_);
                     if (!content.isEmpty()) {
                       OpenArtifactHtmlInBrowser(content);
                     }
                   });
  QObject::connect(
      artifact_preview_open_, &QPushButton::clicked, artifact_preview_open_,
      [this]() {
        if (on_artifact_open && !preview_artifact_.value.isEmpty()) {
          on_artifact_open(preview_artifact_);
        }
      });
  artifact_preview_->hide();
  artifacts_layout->addWidget(artifact_preview_);
  artifacts_layout->addStretch(1);

  project_root_ = QDir::homePath();
  project_label_ = QStringLiteral("Home");
  project_root_label_->setText(
      QStringLiteral("Home\n%1").arg(QDir::toNativeSeparators(project_root_)));
  git_root_label_->setText(
      QStringLiteral("Home\n%1").arg(QDir::toNativeSeparators(project_root_)));

  const QStringList collapsed_labels = {
      QStringLiteral("F"), QStringLiteral("G"), QStringLiteral(">_"),
      QStringLiteral("C"), QStringLiteral("A"), QStringLiteral("T"),
      QStringLiteral("L"), QStringLiteral("O")};
  for (std::size_t i = 0; i < sections_.size(); ++i) {
    const Section& section = sections_[i];
    auto* button = PreviewAction(
        collapsed_bar_,
        collapsed_labels.value(static_cast<int>(i), section.title.left(1)),
        QStringLiteral("rightRailCollapsedTab%1").arg(i));
    button->setFixedSize(32, 26);
    button->setToolTip(section.title);
    collapsed_layout->insertWidget(collapsed_layout->count() - 1, button);
    const QString id = section.id;
    QObject::connect(button, &QPushButton::clicked, button, [this, id]() {
      ExpandSection(id);
      if (on_collapsed_changed) {
        on_collapsed_changed(false);
      }
    });
  }

  SetSectionExpanded(QStringLiteral("project"), true);
  SetTheme(theme);
  SetCollapsed(false);
}

RightRail::~RightRail() {
  // Git jobs are child QProcesses. Disconnect them before killing: a finished
  // signal emitted from the child-destruction path would otherwise call back
  // into already-destroyed document widgets. The terminal owns its PTY and
  // shuts it down from TerminalWidget's own destructor.
  const QList<QProcess*> processes = findChildren<QProcess*>();
  for (QProcess* process : processes) {
    process->disconnect();
    if (process->state() != QProcess::NotRunning) {
      process->kill();
      process->waitForFinished(300);
    }
  }
}

void RightRail::AddSection(const QString& id, const QString& title,
                           QWidget* content) {
  if (content == nullptr || sections_layout_ == nullptr) {
    return;
  }
  auto* header = new QPushButton(content->parentWidget());
  header->setObjectName(QStringLiteral("rightRailSection_%1").arg(id));
  header->setProperty("rightRailSectionHeader", true);
  header->setProperty("expanded", false);
  header->setFlat(true);
  header->setCursor(Qt::PointingHandCursor);
  header->setText(QStringLiteral("\u25b8  %1").arg(title));
  header->setFocusPolicy(Qt::NoFocus);
  content->setProperty("rightRailSectionContent", true);
  content->setVisible(false);
  sections_layout_->addWidget(header);
  sections_layout_->addWidget(content);
  sections_.push_back(Section{id, title, header, content});
  QObject::connect(header, &QPushButton::clicked, header, [this, id]() {
    Section* section = FindSection(id);
    if (section == nullptr) {
      return;
    }
    const bool expanded = section->content->isVisible();
    SetSectionExpanded(id, !expanded);
  });
}

void RightRail::SetSectionTitle(const QString& id, const QString& title) {
  Section* section = FindSection(id);
  if (section == nullptr || section->header == nullptr ||
      section->title == title) {
    return;
  }
  section->title = title;
  const bool expanded = section->header->property("expanded").toBool();
  section->header->setText(QStringLiteral("%1  %2").arg(
      expanded ? QStringLiteral("▾") : QStringLiteral("▸"), title));
}

void RightRail::SetSectionExpanded(const QString& id, bool expanded) {
  Section* section = FindSection(id);
  if (section == nullptr || section->header == nullptr ||
      section->content == nullptr) {
    return;
  }
  section->header->setText(QStringLiteral("%1  %2").arg(
      expanded ? QStringLiteral("\u25be") : QStringLiteral("\u25b8"),
      section->title));
  section->header->setProperty("expanded", expanded);
  section->header->style()->unpolish(section->header);
  section->header->style()->polish(section->header);
  section->content->setVisible(expanded);
  if (expanded && id == QStringLiteral("terminal")) {
    EnsureTerminal();
  }
  if (expanded && id == QStringLiteral("git")) {
    RefreshGitStatuses();
  }
  if (expanded && sections_scroll_ != nullptr) {
    QTimer::singleShot(0, this, [this, id]() {
      Section* current = FindSection(id);
      if (current != nullptr && current->header != nullptr) {
        sections_scroll_->ensureWidgetVisible(current->header, 0, 8);
      }
    });
  }
}

RightRail::Section* RightRail::FindSection(const QString& id) {
  for (Section& section : sections_) {
    if (section.id == id) {
      return &section;
    }
  }
  return nullptr;
}

void RightRail::ExpandSection(const QString& id) {
  if (collapsed_) {
    SetCollapsed(false);
  }
  SetSectionExpanded(id, true);
}

void RightRail::SetTheme(const Theme& theme) {
  theme_ = theme;
  setStyleSheet(
      QStringLiteral(
          "QWidget#rightRail { background: %1; border-left: 1px solid %2; }"
          "QWidget#rightRailWorkspace, QWidget#rightRailHeader, "
          "QWidget#rightRailSections { background: %1; }"
          "QWidget#rightRailHeader { border-bottom: 1px solid %2; }"
          "QLabel#rightRailTitle { color: %4; }"
          "QScrollArea#rightRailSectionsScroll { background: %1; border: 0; }"
          "QScrollArea#rightRailSectionsScroll > QWidget > QWidget { "
          "background: %1; }"
          "QPushButton[rightRailSectionHeader=\"true\"] { "
          "text-align: left; padding: 8px 10px; border: 0; "
          "border-bottom: 1px solid %2; background: %1; color: %3; "
          "font-weight: 600; }"
          "QPushButton[rightRailSectionHeader=\"true\"]:hover { "
          "background: %7; color: %4; }"
          "QPushButton[rightRailSectionHeader=\"true\"][expanded=\"true\"] "
          "{ color: %4; background: %6; }"
          "QWidget[rightRailSectionContent=\"true\"] { background: %1; "
          "border-bottom: 1px solid %2; }"
          "QWidget[gitRepoCard=\"true\"] { background: %6; "
          "border: 1px solid %2; border-radius: 6px; }"
          "QPushButton[gitRepoHeader=\"true\"] { background: transparent; "
          "border: 0; padding: 0; }"
          "QPushButton[gitRepoHeader=\"true\"]:hover { background: %7; "
          "border-radius: 5px; }"
          "QPushButton[gitRepoHeader=\"true\"]:focus, "
          "QPushButton[gitRepoHeader=\"true\"]:pressed { background: %6; "
          "border-radius: 5px; }"
          "QPushButton[railAction=\"true\"] { background: transparent; "
          "color: %3; border: 1px solid %2; border-radius: 5px; "
          "padding: 4px 7px; }"
          "QPushButton[gitChangePath=\"true\"] { text-align: left; "
          "padding: 0; border: 0; background: transparent; color: %4; }"
          "QPushButton[gitChangePath=\"true\"]:hover, "
          "QPushButton[gitChangePath=\"true\"]:focus { color: %5; }"
          "QPushButton[railAction=\"true\"]:hover { color: %4; "
          "background: %7; }"
          "QPushButton[railAction=\"true\"]:disabled { color: %2; "
          "border-color: %2; }"
          "QPushButton[railTimelineRow=\"true\"] { text-align: left; "
          "padding: 0; border: 1px solid transparent; border-radius: 6px; "
          "color: %4; background: transparent; }"
          "QPushButton[railTimelineRow=\"true\"]:hover { background: %7; }"
          "QPushButton[railTimelineRow=\"true\"]:focus, "
          "QPushButton[railTimelineRow=\"true\"]:pressed, "
          "QPushButton[railTimelineRow=\"true\"]:checked { "
          "background: %6; border-color: %5; color: %4; }"
          "QPushButton[railTimelineRow=\"true\"] QLabel { "
          "background: transparent; }"
          "QPushButton[railTimelineRow=\"true\"] "
          "QLabel[railTimelinePreview=\"true\"] { color: %4; }"
          "QPushButton[railTimelineRow=\"true\"] "
          "QLabel[railTimelineTime=\"true\"] { color: %3; }"
          "QPushButton[railTimelineRow=\"true\"]:focus "
          "QLabel[railTimelinePreview=\"true\"], "
          "QPushButton[railTimelineRow=\"true\"]:pressed "
          "QLabel[railTimelinePreview=\"true\"] { color: %4; }"
          "QWidget[railAgentCard=\"true\"] { background: %6; "
          "border: 1px solid %2; border-radius: 6px; }"
          "QPushButton[railAgentRow=\"true\"] { text-align: left; "
          "padding: 0; border: 1px solid transparent; border-radius: 6px; "
          "color: %4; background: transparent; }"
          "QPushButton[railAgentRow=\"true\"]:hover { background: %7; }"
          "QPushButton[railAgentRow=\"true\"]:focus, "
          "QPushButton[railAgentRow=\"true\"]:pressed { background: %6; "
          "border-color: %5; color: %4; }"
          "QPushButton[railAgentRow=\"true\"] QLabel { "
          "background: transparent; }"
          "QLineEdit[railAgentSteer=\"true\"] { background: %6; color: %4; "
          "border: 1px solid %2; border-radius: 5px; padding: 3px 6px; }"
          "QPushButton[railArtifactCard=\"true\"] { text-align: left; "
          "padding: 7px 8px; border: 1px solid %2; border-radius: 6px; "
          "color: %3; background: %6; }"
          "QPushButton[railArtifactCard=\"true\"]:hover { background: %7; }"
          "QProgressBar { background: %6; border: 0; border-radius: 3px; }"
          "QProgressBar::chunk { background: %5; border-radius: 3px; }"
          "QLineEdit#rightRailTimelineSearch, "
          "QLineEdit#rightRailProjectSearch { background: %6; color: %4; "
          "border: 1px solid %2; border-radius: 5px; padding: 4px 6px; }"
          "QLineEdit#rightRailGitIgnoreInput, "
          "QPlainTextEdit#rightRailGitMessage, "
          "QPlainTextEdit[gitRepoField=\"true\"], "
          "QTextBrowser#rightRailProjectPreview, "
          "QTreeView#rightRailProjectTree { background: %6; color: %4; "
          "border: 1px solid %2; border-radius: 5px; }"
          "QTreeView#rightRailProjectTree QHeaderView::section { "
          "background: %6; color: %3; border: 0; "
          "border-right: 1px solid %2; border-bottom: 1px solid %2; "
          "padding: 4px 6px; }"
          "QSplitter#rightRailProjectSplit::handle { background: %2; }"
          "QWidget#rightRailCollapsedBar { background: %1; "
          "border-left: 1px solid %2; }"
          "QPushButton#rightRailCollapse, QPushButton#rightRailExpand { "
          "background: transparent; color: %3; border: 0; padding: 0; }"
          "QPushButton#rightRailCollapse:hover, "
          "QPushButton#rightRailExpand:hover { background: %7; "
          "border-radius: 5px; }"
          "QWidget#rightRailCollapsedBar QPushButton:hover { "
          "background: %7; border-radius: 5px; }")
          .arg(theme.surface.name(), theme.stroke.name(),
               theme.text_secondary.name(), theme.foreground.name(),
               theme.accent.name(), theme.code_background.name(),
               theme.hover.name()));
  if (auto* title = findChild<QLabel*>(QStringLiteral("rightRailTitle"))) {
    title->setFont(theme_.UiFont(12, QFont::DemiBold));
  }
  for (Section& section : sections_) {
    if (section.header != nullptr) {
      section.header->setFont(theme_.UiFont(11, QFont::DemiBold));
    }
  }
  for (QLabel* label : findChildren<QLabel*>()) {
    StyleRailLabel(label, theme_);
    if (label->property("gitBadge").toBool()) {
      StyleGitBadge(label, theme_);
    }
  }
  for (auto& entry : agent_cards_) {
    AgentCard& card = entry.second;
    if (card.header != nullptr) {
      card.header->SetTheme(theme_);
    }
    if (card.steer_input != nullptr) {
      card.steer_input->setFont(theme_.UiFont(10, QFont::Normal));
    }
    if (card.steer != nullptr) {
      card.steer->setFont(theme_.UiFont(10, QFont::Normal));
    }
    if (card.stop != nullptr) {
      card.stop->setFont(theme_.UiFont(10, QFont::Normal));
    }
  }
  if (timeline_search_ != nullptr) {
    timeline_search_->setFont(theme_.UiFont(11, QFont::Normal));
  }
  for (QPushButton* button : findChildren<QPushButton*>()) {
    if (auto* timeline_row = dynamic_cast<TimelineRowButton*>(button)) {
      timeline_row->SetTheme(theme_);
      continue;
    }
    if (button->property("railArtifactCard").toBool() ||
        button->property("railAction").toBool()) {
      button->setFont(theme_.UiFont(11, QFont::Normal));
    } else if (button->property("gitChangePath").toBool()) {
      button->setFont(theme_.UiFont(10, QFont::Normal));
    }
  }
  for (GitRepoPane& repo : git_repositories_) {
    if (auto* header = dynamic_cast<GitRepoHeaderButton*>(repo.header)) {
      header->SetTheme(theme_);
    }
    if (repo.branch != nullptr) {
      repo.branch->setFont(theme_.UiFont(11, QFont::DemiBold));
    }
    if (repo.status != nullptr) {
      repo.status->setFont(theme_.UiFont(10, QFont::Normal));
    }
    if (repo.ignore_input != nullptr) {
      repo.ignore_input->setFont(theme_.UiFont(10, QFont::Normal));
    }
    if (repo.diff_view != nullptr) {
      repo.diff_view->setFont(theme_.MonoFont(10, QFont::Normal));
    }
    if (repo.message != nullptr) {
      repo.message->setFont(theme_.UiFont(11, QFont::Normal));
    }
  }
  if (terminal_ != nullptr) {
    terminal_->SetTheme(theme_);
  }
  if (project_tree_ != nullptr) {
    project_tree_->setFont(theme_.UiFont(11, QFont::Normal));
    if (project_tree_->header() != nullptr) {
      project_tree_->header()->setFont(theme_.UiFont(10, QFont::DemiBold));
    }
  }
  if (project_preview_ != nullptr) {
    project_preview_->setFont(theme_.UiFont(11, QFont::Normal));
  }
  if (!project_preview_path_.isEmpty()) {
    ShowProjectPreview(project_preview_path_);
  }
  if (preview_artifact_.generated && !preview_artifact_.versions.empty() &&
      artifact_preview_ != nullptr && artifact_preview_->isVisible()) {
    RenderArtifactPreview();
  }
  timeline_render_signature_.clear();
  RebuildTimelineRows();
  update();
}

void RightRail::SetCollapsed(bool collapsed) {
  collapsed_ = collapsed;
  if (workspace_ != nullptr) {
    workspace_->setVisible(!collapsed_);
  }
  if (collapsed_bar_ != nullptr) {
    collapsed_bar_->setVisible(collapsed_);
  }
  setMinimumWidth(collapsed_ ? 44 : 260);
  setMaximumWidth(collapsed_ ? 44 : 800);
  updateGeometry();
}

void RightRail::SetProjectContext(
    const QString& project_id, const QString& label, const QString& root,
    const std::vector<ProjectRepoInfo>& repositories) {
  QString resolved = root.trimmed();
  if (resolved.isEmpty() || !QFileInfo(resolved).isDir()) {
    resolved = QDir::homePath();
  }
  resolved = QFileInfo(resolved).absoluteFilePath();
  bool repositories_equal = repositories.size() == project_repositories_.size();
  if (repositories_equal) {
    for (std::size_t i = 0; i < repositories.size(); ++i) {
      const ProjectRepoInfo& left = repositories[i];
      const ProjectRepoInfo& right = project_repositories_[i];
      if (left.id != right.id || left.label != right.label ||
          left.path != right.path) {
        repositories_equal = false;
        break;
      }
    }
  }
  if (project_id == project_id_ && label == project_label_ &&
      resolved == project_root_ && repositories_equal) {
    for (const GitRepoPane& repository : git_repositories_) {
      if (repository.status != nullptr &&
          repository.status->text().isEmpty()) {
        RefreshGitStatus(repository.path, repository.serial);
      }
    }
    return;
  }
  const bool root_changed = resolved != project_root_;
  project_id_ = project_id;
  project_label_ = label;
  project_root_ = resolved;
  project_repositories_ = repositories;
  SyncProjectViews();
  if (root_changed && terminal_ != nullptr) {
    terminal_->SetWorkingDirectory(project_root_);
    if (terminal_->IsRunning()) {
      RestartTerminal();
    }
  }
}

void RightRail::RebuildGitRepositories(
    const std::vector<ProjectRepoInfo>& repositories) {
  std::vector<ProjectRepoInfo> resolved;
  QSet<QString> seen;
  const auto append = [&resolved, &seen](const ProjectRepoInfo& repo) {
    const QString path = repo.path.trimmed();
    if (path.isEmpty()) {
      return;
    }
    const QString key = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    if (key.isEmpty() || seen.contains(key)) {
      return;
    }
    seen.insert(key);
    ProjectRepoInfo next = repo;
    next.path = path;
    if (next.label.trimmed().isEmpty()) {
      next.label = QFileInfo(path).fileName();
      if (next.label.isEmpty()) {
        next.label = path;
      }
    }
    resolved.push_back(std::move(next));
  };
  for (const ProjectRepoInfo& repository : repositories) {
    append(repository);
  }
  if (resolved.empty() && !project_root_.isEmpty() &&
      QFileInfo(QDir(project_root_).filePath(QStringLiteral(".git")))
          .exists()) {
    ProjectRepoInfo repository;
    repository.id = project_root_;
    repository.path = project_root_;
    repository.label = project_label_.trimmed().isEmpty()
                           ? QFileInfo(project_root_).fileName()
                           : project_label_;
    append(repository);
  }

  bool unchanged = resolved.size() == git_repositories_.size();
  if (unchanged) {
    for (std::size_t i = 0; i < resolved.size(); ++i) {
      if (resolved[i].id != git_repositories_[i].id ||
          resolved[i].label != git_repositories_[i].label ||
          resolved[i].path != git_repositories_[i].path) {
        unchanged = false;
        break;
      }
    }
  }
  if (unchanged) {
    for (std::size_t i = 0; i < resolved.size() && i < git_repositories_.size();
         ++i) {
      GitRepoPane& pane = git_repositories_[i];
      pane.id = resolved[i].id;
      pane.label = resolved[i].label;
      if (pane.root != nullptr) {
        pane.root->setText(QDir::toNativeSeparators(pane.path));
      }
      if (auto* header = dynamic_cast<GitRepoHeaderButton*>(pane.header)) {
        header->SetRepo(pane.label, pane.path);
      }
    }
    if (git_empty_ != nullptr) {
      git_empty_->setVisible(resolved.empty());
    }
    if (git_repositories_host_ != nullptr) {
      git_repositories_host_->setVisible(!resolved.empty());
    }
    return;
  }

  ClearLayout(git_repositories_layout_);
  git_repositories_.clear();
  if (git_empty_ != nullptr) {
    git_empty_->setVisible(resolved.empty());
  }
  if (git_repositories_host_ != nullptr) {
    git_repositories_host_->setVisible(!resolved.empty());
  }

  for (std::size_t index = 0; index < resolved.size(); ++index) {
    const ProjectRepoInfo& repository = resolved[index];
    const int ordinal = static_cast<int>(index);
    const QString suffix =
        ordinal == 0 ? QString() : QStringLiteral("_%1").arg(ordinal);

    GitRepoPane pane;
    pane.id = repository.id;
    pane.label = repository.label;
    pane.path = repository.path;
    pane.suffix = suffix;
    pane.serial = ++git_repository_serial_;

    auto* card = new QWidget(git_repositories_host_);
    card->setObjectName(QStringLiteral("rightRailGitRepo_%1").arg(ordinal));
    card->setProperty("gitRepoCard", true);
    card->setAttribute(Qt::WA_StyledBackground, true);
    auto* card_layout = new QVBoxLayout(card);
    card_layout->setContentsMargins(8, 8, 8, 8);
    card_layout->setSpacing(6);

    pane.card = card;
    auto* header = new GitRepoHeaderButton(theme_, suffix, card);
    header->SetRepo(repository.label, repository.path);
    pane.header = header;
    card_layout->addWidget(header);

    pane.body = new QWidget(card);
    pane.body->setObjectName(
        QStringLiteral("rightRailGitRepoBody%1").arg(suffix));
    auto* body_layout = new QVBoxLayout(pane.body);
    body_layout->setContentsMargins(0, 0, 0, 0);
    body_layout->setSpacing(6);
    card_layout->addWidget(pane.body);

    pane.root = RailLabel(pane.body, theme_,
                          QDir::toNativeSeparators(repository.path), 9,
                          QFont::Normal, theme_.text_tertiary);
    pane.root->setObjectName(
        QStringLiteral("rightRailGitRepoRoot%1").arg(suffix));
    pane.root->setToolTip(repository.path);
    body_layout->addWidget(pane.root);

    pane.branch = RailLabel(pane.body, theme_,
                            QStringLiteral("Checking branch…"), 11,
                            QFont::DemiBold, theme_.foreground);
    pane.branch->setObjectName(
        QStringLiteral("rightRailGitBranch%1").arg(suffix));
    pane.branch->setWordWrap(false);
    pane.branch->setToolTip(QStringLiteral("Current branch"));
    body_layout->addWidget(pane.branch);

    pane.status = RailLabel(pane.body, theme_,
                            QStringLiteral("Reading changes…"), 10,
                            QFont::Normal, theme_.text_secondary);
    pane.status->setObjectName(
        QStringLiteral("rightRailGitSummary%1").arg(suffix));
    pane.status->setToolTip(
        QStringLiteral("Staged and unstaged changes in this repository"));
    body_layout->addWidget(pane.status);

    pane.changes_host = new QWidget(pane.body);
    pane.changes_host->setObjectName(
        QStringLiteral("rightRailGitChanges%1").arg(suffix));
    pane.changes_host->setAttribute(Qt::WA_StyledBackground, true);
    pane.changes_layout = new QVBoxLayout(pane.changes_host);
    pane.changes_layout->setContentsMargins(0, 2, 0, 2);
    pane.changes_layout->setSpacing(2);
    body_layout->addWidget(pane.changes_host);

    pane.diff_host = new QWidget(pane.body);
    pane.diff_host->setObjectName(
        QStringLiteral("rightRailGitDiffPanel%1").arg(suffix));
    pane.diff_host->setAttribute(Qt::WA_StyledBackground, true);
    pane.diff_host->setVisible(false);
    auto* diff_layout = new QVBoxLayout(pane.diff_host);
    diff_layout->setContentsMargins(0, 2, 0, 4);
    diff_layout->setSpacing(3);
    auto* diff_header = new QHBoxLayout();
    diff_header->setContentsMargins(0, 0, 0, 0);
    diff_header->setSpacing(4);
    pane.diff_title = RailLabel(pane.diff_host, theme_, QString(), 10,
                                QFont::DemiBold, theme_.text_secondary);
    pane.diff_title->setObjectName(
        QStringLiteral("rightRailGitDiffTitle%1").arg(suffix));
    pane.diff_title->setWordWrap(false);
    pane.diff_title->setSizePolicy(QSizePolicy::Ignored,
                                   QSizePolicy::Preferred);
    pane.diff_title->setMinimumWidth(0);
    diff_header->addWidget(pane.diff_title, 1);
    pane.diff_close = PreviewAction(
        pane.diff_host, QStringLiteral("×"),
        QStringLiteral("rightRailGitDiffClose%1").arg(suffix));
    pane.diff_close->setFixedSize(18, 20);
    pane.diff_close->setToolTip(QStringLiteral("Close diff"));
    diff_header->addWidget(pane.diff_close);
    diff_layout->addLayout(diff_header);
    pane.diff_view = new QPlainTextEdit(pane.diff_host);
    pane.diff_view->setObjectName(
        QStringLiteral("rightRailGitDiff%1").arg(suffix));
    pane.diff_view->setProperty("gitRepoField", true);
    pane.diff_view->setReadOnly(true);
    pane.diff_view->setLineWrapMode(QPlainTextEdit::NoWrap);
    pane.diff_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    pane.diff_view->setFont(theme_.MonoFont(10, QFont::Normal));
    pane.diff_view->setMinimumHeight(110);
    pane.diff_view->setMaximumHeight(220);
    diff_layout->addWidget(pane.diff_view);
    body_layout->addWidget(pane.diff_host);

    auto* ignore_title = RailLabel(pane.body, theme_,
                                   QStringLiteral("Ignore rules"), 10,
                                   QFont::DemiBold, theme_.text_secondary);
    ignore_title->setObjectName(
        QStringLiteral("rightRailGitIgnoreTitle%1").arg(suffix));
    ignore_title->setToolTip(
        QStringLiteral("Add files, folders, or patterns to .gitignore"));
    body_layout->addWidget(ignore_title);

    auto* ignore_input_row = new QHBoxLayout();
    ignore_input_row->setContentsMargins(0, 0, 0, 0);
    ignore_input_row->setSpacing(4);
    pane.ignore_input = new QLineEdit(pane.body);
    pane.ignore_input->setObjectName(
        QStringLiteral("rightRailGitIgnoreInput%1").arg(suffix));
    pane.ignore_input->setProperty("gitRepoField", true);
    pane.ignore_input->setPlaceholderText(
        QStringLiteral("file, dir/, or *.pattern"));
    pane.ignore_input->setClearButtonEnabled(true);
    pane.ignore_input->setFont(theme_.UiFont(10, QFont::Normal));
    pane.ignore_input->setMinimumHeight(24);
    ignore_input_row->addWidget(pane.ignore_input, 1);
    pane.ignore_add = PreviewAction(
        pane.body, QStringLiteral("Add"),
        QStringLiteral("rightRailGitIgnoreAdd%1").arg(suffix));
    pane.ignore_add->setToolTip(QStringLiteral("Add to .gitignore"));
    ignore_input_row->addWidget(pane.ignore_add);
    body_layout->addLayout(ignore_input_row);

    pane.ignore_host = new QWidget(pane.body);
    pane.ignore_host->setObjectName(
        QStringLiteral("rightRailGitIgnoreEntries%1").arg(suffix));
    pane.ignore_host->setAttribute(Qt::WA_StyledBackground, true);
    pane.ignore_layout = new QVBoxLayout(pane.ignore_host);
    pane.ignore_layout->setContentsMargins(0, 0, 0, 4);
    pane.ignore_layout->setSpacing(1);
    body_layout->addWidget(pane.ignore_host);

    pane.message = new QPlainTextEdit(pane.body);
    pane.message->setObjectName(
        QStringLiteral("rightRailGitMessage%1").arg(suffix));
    pane.message->setProperty("gitRepoField", true);
    pane.message->setPlaceholderText(QStringLiteral("Commit message"));
    pane.message->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    pane.message->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    pane.message->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    pane.message->setFont(theme_.UiFont(11, QFont::Normal));
    pane.message->setFixedHeight(64);
    body_layout->addWidget(pane.message);

    pane.message_status = RailLabel(pane.body, theme_, QString(), 10,
                                    QFont::Normal, theme_.text_secondary);
    pane.message_status->setObjectName(
        QStringLiteral("rightRailGitMessageStatus%1").arg(suffix));
    body_layout->addWidget(pane.message_status);

    auto* actions = new FlowLayout(0, 4, 4);
    pane.refresh =
        PreviewAction(pane.body, QStringLiteral("Refresh"),
                      QStringLiteral("rightRailGitRefresh%1").arg(suffix));
    actions->addWidget(pane.refresh);
    pane.generate =
        PreviewAction(pane.body, QStringLiteral("AI Generate"),
                      QStringLiteral("rightRailGitGenerate%1").arg(suffix));
    pane.generate->setToolTip(
        QStringLiteral("Draft a commit message from this repository's staged "
                       "changes"));
    actions->addWidget(pane.generate);
    pane.commit =
        PreviewAction(pane.body, QStringLiteral("Commit"),
                      QStringLiteral("rightRailGitCommit%1").arg(suffix));
    pane.commit->setToolTip(QStringLiteral("Commit staged changes"));
    pane.commit->setEnabled(false);
    actions->addWidget(pane.commit);
    pane.push = PreviewAction(pane.body, QStringLiteral("Commit & Push"),
                              QStringLiteral("rightRailGitPush%1").arg(suffix));
    actions->addWidget(pane.push);
    body_layout->addLayout(actions);

    const QString path = pane.path;
    const quint64 serial = pane.serial;
    QObject::connect(
        pane.refresh, &QPushButton::clicked, pane.refresh,
        [this, path, serial]() { RefreshGitStatus(path, serial); });
    QObject::connect(
        pane.generate, &QPushButton::clicked, pane.generate,
        [this, path, serial]() { GenerateCommitMessage(path, serial); });
    QObject::connect(pane.commit, &QPushButton::clicked, pane.commit,
                     [this, path, serial]() { CommitGit(path, serial); });
    QObject::connect(pane.push, &QPushButton::clicked, pane.push,
                     [this, path, serial]() { PushGit(path, serial); });
    QObject::connect(pane.diff_close, &QPushButton::clicked, pane.diff_close,
                     [this, path, serial]() {
                       GitRepoPane* repo = FindGitRepository(path, serial);
                       if (repo == nullptr) {
                         return;
                       }
                       ++repo->diff_generation;
                       if (repo->diff_host != nullptr) {
                         repo->diff_host->setVisible(false);
                       }
                     });
    QObject::connect(pane.ignore_add, &QPushButton::clicked, pane.ignore_add,
                     [this, path, serial]() {
                       GitRepoPane* repo = FindGitRepository(path, serial);
                       if (repo == nullptr || repo->ignore_input == nullptr) {
                         return;
                       }
                       AddGitignorePattern(path, serial,
                                           repo->ignore_input->text());
                     });
    QObject::connect(
        pane.ignore_input, &QLineEdit::returnPressed, pane.ignore_input,
        [this, path, serial]() {
          GitRepoPane* repo = FindGitRepository(path, serial);
          if (repo == nullptr || repo->ignore_input == nullptr) {
            return;
          }
          AddGitignorePattern(path, serial, repo->ignore_input->text());
        });

    QObject::connect(
        header, &QPushButton::clicked, header, [this, path, serial]() {
          GitRepoPane* repo = FindGitRepository(path, serial);
          if (repo != nullptr) {
            SetGitRepoExpanded(repo, !repo->expanded);
          }
        });

    git_repositories_.push_back(pane);
    git_repositories_layout_->addWidget(card);
    const QString key =
        QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    SetGitRepoExpanded(&git_repositories_.back(),
                       !collapsed_git_repo_paths_.contains(key));
  }
  git_repositories_layout_->addStretch(1);
}

void RightRail::SetGitRepoExpanded(GitRepoPane* repository, bool expanded) {
  if (repository == nullptr) {
    return;
  }
  repository->expanded = expanded;
  const QString key =
      QDir::cleanPath(QFileInfo(repository->path).absoluteFilePath());
  if (expanded) {
    collapsed_git_repo_paths_.remove(key);
  } else {
    collapsed_git_repo_paths_.insert(key);
  }
  if (auto* header = dynamic_cast<GitRepoHeaderButton*>(repository->header)) {
    header->SetExpanded(expanded);
  }
  if (repository->body != nullptr) {
    repository->body->setVisible(expanded);
  }
}

void RightRail::RefreshGitStatuses() {
  for (const GitRepoPane& repository : git_repositories_) {
    RefreshGitStatus(repository.path, repository.serial);
  }
}

void RightRail::SyncProjectViews() {
  const QString label =
      project_label_.isEmpty() ? QStringLiteral("Home") : project_label_;
  const QString path = QDir::toNativeSeparators(project_root_);
  if (project_root_label_ != nullptr) {
    project_root_label_->setText(QStringLiteral("%1\n%2").arg(label, path));
    project_root_label_->setToolTip(project_root_);
  }
  RebuildGitRepositories(project_repositories_);
  if (git_root_label_ != nullptr) {
    QString text = QStringLiteral("%1\n%2").arg(label, path);
    if (git_repositories_.size() > 1) {
      text += QStringLiteral("\n%1 repositories").arg(git_repositories_.size());
    }
    git_root_label_->setText(text);
    git_root_label_->setToolTip(project_root_);
  }
  if (project_model_ != nullptr && project_tree_ != nullptr) {
    const QModelIndex root_index = project_model_->setRootPath(project_root_);
    project_tree_->setRootIndex(root_index);
  }
  if (project_preview_ != nullptr) {
    project_preview_->clear();
    project_preview_path_.clear();
  }
  RefreshGitStatuses();
}

RightRail::GitRepoPane* RightRail::FindGitRepository(const QString& path,
                                                     quint64 serial) {
  for (GitRepoPane& repository : git_repositories_) {
    if (repository.path == path &&
        (serial == 0 || repository.serial == serial)) {
      return &repository;
    }
  }
  return nullptr;
}

void RightRail::ShowProjectPreview(const QString& path) {
  if (project_preview_ == nullptr) {
    return;
  }
  project_preview_path_ = path;
  const QFileInfo info(path);
  if (!info.isFile()) {
    return;
  }
  if (info.size() > 512 * 1024) {
    project_preview_->document()->setBaseUrl(QUrl());
    project_preview_->setPlainText(
        QStringLiteral("Preview unavailable · file is larger than 512 KB."));
    return;
  }
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    project_preview_->document()->setBaseUrl(QUrl());
    project_preview_->setPlainText(QStringLiteral("Unable to read file."));
    return;
  }
  const QByteArray bytes = file.readAll();
  const QString suffix = info.suffix().toLower();
  const bool image =
      suffix == QStringLiteral("png") || suffix == QStringLiteral("jpg") ||
      suffix == QStringLiteral("jpeg") || suffix == QStringLiteral("gif") ||
      suffix == QStringLiteral("webp") || suffix == QStringLiteral("bmp") ||
      suffix == QStringLiteral("svg") || suffix == QStringLiteral("ico");
  if (bytes.contains('\0') && !image) {
    project_preview_->document()->setBaseUrl(QUrl());
    project_preview_->setPlainText(
        QStringLiteral("Preview unavailable · binary file."));
    return;
  }
  const QUrl base =
      QUrl::fromLocalFile(info.absolutePath() + QStringLiteral("/"));
  const QString text = QString::fromUtf8(bytes);
  const bool markdown =
      suffix == QStringLiteral("md") || suffix == QStringLiteral("markdown") ||
      suffix == QStringLiteral("mdown") || suffix == QStringLiteral("mkd");
  const bool html = suffix == QStringLiteral("html") ||
                    suffix == QStringLiteral("htm") ||
                    suffix == QStringLiteral("xhtml");
  if (image) {
    project_preview_->document()->setBaseUrl(base);
    project_preview_->document()->setDefaultFont(
        theme_.UiFont(13, QFont::Normal));
    project_preview_->setHtml(
        QStringLiteral(
            "<html><body style=\"margin:12px; background:%1;\">"
            "<img src=\"%2\" style=\"max-width:100%;\"></body></html>")
            .arg(theme_.surface.name(),
                 QUrl::fromLocalFile(info.absoluteFilePath()).toString()));
    return;
  }
  if (markdown || html) {
    project_preview_->document()->setBaseUrl(base);
    if (markdown) {
      project_preview_->setMarkdown(text);
    } else {
      project_preview_->setHtml(text);
    }
    StyleMarkdownPreview(project_preview_, theme_);
    project_preview_->moveCursor(QTextCursor::Start);
    return;
  }
  const bool monospace =
      suffix == QStringLiteral("cc") || suffix == QStringLiteral("cpp") ||
      suffix == QStringLiteral("h") || suffix == QStringLiteral("hpp") ||
      suffix == QStringLiteral("py") || suffix == QStringLiteral("js") ||
      suffix == QStringLiteral("ts") || suffix == QStringLiteral("tsx") ||
      suffix == QStringLiteral("jsx") || suffix == QStringLiteral("json") ||
      suffix == QStringLiteral("yaml") || suffix == QStringLiteral("yml") ||
      suffix == QStringLiteral("toml") || suffix == QStringLiteral("sh") ||
      suffix == QStringLiteral("zsh") || suffix == QStringLiteral("bash") ||
      suffix == QStringLiteral("css") || suffix == QStringLiteral("scss") ||
      suffix == QStringLiteral("xml") || suffix == QStringLiteral("cmake") ||
      suffix == QStringLiteral("txt") || suffix == QStringLiteral("log");
  project_preview_->document()->setBaseUrl(base);
  project_preview_->document()->setDefaultFont(
      monospace ? theme_.MonoFont(12, QFont::Normal)
                : theme_.UiFont(13, QFont::Normal));
  project_preview_->document()->setDocumentMargin(12);
  project_preview_->setPlainText(text);
  project_preview_->moveCursor(QTextCursor::Start);
}

void RightRail::RefreshGitStatus(const QString& path, quint64 serial) {
  GitRepoPane* repository = FindGitRepository(path, serial);
  if (repository == nullptr || repository->status == nullptr) {
    return;
  }
  repository->status->setText(QStringLiteral("Reading changes…"));
  RefreshGitignore(repository);
  RunGit(
      path,
      {QStringLiteral("status"), QStringLiteral("--porcelain=v1"),
       QStringLiteral("-z"), QStringLiteral("--branch"),
       QStringLiteral("--untracked-files=all")},
      [this, path, serial](bool ok, const QString& output) {
        GitRepoPane* current = FindGitRepository(path, serial);
        if (current == nullptr || current->status == nullptr) {
          return;
        }
        if (!ok) {
          if (current->branch != nullptr) {
            current->branch->setText(QStringLiteral("Not a git repository"));
          }
          if (auto* header =
                  dynamic_cast<GitRepoHeaderButton*>(current->header)) {
            header->SetChangeCount(0);
          }
          current->status->setText(
              output.isEmpty() ? QStringLiteral("Git status is unavailable.")
                               : output);
          if (current->changes_layout != nullptr) {
            ClearLayout(current->changes_layout);
          }
          if (current->commit != nullptr) {
            current->commit->setEnabled(false);
          }
          if (current->push != nullptr) {
            current->push->setEnabled(false);
          }
          if (current->generate != nullptr) {
            current->generate->setEnabled(false);
          }
          return;
        }
        RebuildGitChanges(current, output);
      });
}

void RightRail::RebuildGitChanges(GitRepoPane* repository,
                                  const QString& status_output) {
  if (repository == nullptr || repository->changes_layout == nullptr) {
    return;
  }
  ClearLayout(repository->changes_layout);
  const GitStatusSnapshot snapshot = ParseGitStatus(status_output);
  int staged_count = 0;
  int unstaged_count = 0;
  QSet<QString> changed_paths;
  for (const GitFileChange& change : snapshot.changes) {
    changed_paths.insert(change.path);
    if (change.staged) {
      ++staged_count;
    } else {
      ++unstaged_count;
    }
  }
  if (auto* header = dynamic_cast<GitRepoHeaderButton*>(repository->header)) {
    header->SetChangeCount(changed_paths.size());
  }

  if (repository->branch != nullptr) {
    repository->branch->setText(
        snapshot.branch.isEmpty() ? QStringLiteral("Detached HEAD")
                                  : snapshot.branch);
    repository->branch->setToolTip(
        snapshot.branch.isEmpty()
            ? QStringLiteral("No current branch")
            : QStringLiteral("Current branch: %1").arg(snapshot.branch));
  }
  if (repository->status != nullptr) {
    QStringList summary;
    if (staged_count > 0) {
      summary << QStringLiteral("%1 staged").arg(staged_count);
    }
    if (unstaged_count > 0) {
      summary << QStringLiteral("%1 changed").arg(unstaged_count);
    }
    repository->status->setText(
        summary.isEmpty() ? QStringLiteral("Working tree clean")
                          : summary.join(QStringLiteral(" · ")));
  }

  const QString path = repository->path;
  const quint64 serial = repository->serial;
  int row_index = 0;
  auto add_group_header = [this, repository](const QString& title, int count,
                                             bool staged) {
    auto* header = new QWidget(repository->changes_host);
    header->setObjectName(GitPaneObjectName(
        staged ? QStringLiteral("rightRailGitStagedGroup")
               : QStringLiteral("rightRailGitChangesGroup"),
        repository->suffix));
    auto* layout = new QHBoxLayout(header);
    layout->setContentsMargins(0, 4, 0, 2);
    layout->setSpacing(4);
    auto* label = RailLabel(
        header, theme_, QStringLiteral("%1 · %2").arg(title).arg(count), 10,
        QFont::DemiBold, theme_.text_secondary);
    label->setWordWrap(false);
    label->setProperty("gitGroupTitle", true);
    layout->addWidget(label, 1);
    if (count > 0) {
      auto* action = PreviewAction(
          header, staged ? QStringLiteral("Unstage all")
                         : QStringLiteral("Stage all"),
          GitPaneObjectName(staged ? QStringLiteral("rightRailGitUnstageAll")
                                   : QStringLiteral("rightRailGitStageAll"),
                            repository->suffix));
      action->setProperty("gitStageAll", !staged);
      action->setToolTip(staged ? QStringLiteral("Unstage every file")
                                : QStringLiteral("Stage every change"));
      layout->addWidget(action);
      const QString root = repository->path;
      const quint64 repo_serial = repository->serial;
      QObject::connect(action, &QPushButton::clicked, action,
                       [this, root, repo_serial, staged]() {
                         StageAllGit(root, repo_serial, !staged);
                       });
    }
    repository->changes_layout->addWidget(header);
  };
  auto add_empty_group = [this, repository](const QString& text) {
    auto* label = RailLabel(repository->changes_host, theme_, text, 10,
                            QFont::Normal, theme_.text_tertiary);
    label->setWordWrap(false);
    label->setProperty("gitEmptyGroup", true);
    repository->changes_layout->addWidget(label);
  };
  auto add_change_row = [this, repository, path, serial, &row_index](
                            const GitFileChange& change, bool staged) {
    auto* row = new QWidget(repository->changes_host);
    row->setObjectName(GitPaneObjectName(QStringLiteral("rightRailGitChange"),
                                         repository->suffix, row_index));
    row->setProperty("gitChangeRow", true);
    row->setAttribute(Qt::WA_StyledBackground, true);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 1, 0, 1);
    layout->setSpacing(4);

    auto* badge = new QLabel(change.status.left(1), row);
    badge->setObjectName(
        GitPaneObjectName(QStringLiteral("rightRailGitBadge"),
                          repository->suffix, row_index));
    badge->setAlignment(Qt::AlignCenter);
    badge->setFixedSize(18, 18);
    badge->setProperty("gitBadge", true);
    badge->setProperty("gitChangeKind", GitChangeKind(change.status));
    badge->setToolTip(GitChangeDescription(change.status));
    StyleGitBadge(badge, theme_);
    layout->addWidget(badge);

    const QString display_path = GitChangeDisplayPath(change);
    const QStringList change_paths = GitChangePaths(change);
    const bool untracked = change.status == QStringLiteral("?");
    QString path_button_text = display_path;
    path_button_text.replace(QStringLiteral("&"), QStringLiteral("&&"));
    auto* name = new QPushButton(path_button_text, row);
    name->setObjectName(
        GitPaneObjectName(QStringLiteral("rightRailGitChangePath"),
                          repository->suffix, row_index));
    name->setFlat(true);
    name->setCursor(Qt::PointingHandCursor);
    name->setFocusPolicy(Qt::StrongFocus);
    name->setAutoDefault(false);
    name->setFont(theme_.UiFont(10, QFont::Normal));
    name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    name->setMinimumWidth(0);
    name->setProperty("gitChangePath", true);
    name->setProperty("gitPath", change.path);
    name->setToolTip(
        QStringLiteral("%1\n%2\nClick to view the diff.")
            .arg(GitChangeDescription(change.status), display_path));
    layout->addWidget(name, 1);
    const auto show_diff = [this, path, serial, change_paths, display_path,
                            staged, untracked]() {
      ShowGitDiff(path, serial, change_paths, display_path, staged, untracked);
    };
    QObject::connect(name, &QPushButton::clicked, name, show_diff);

    auto* action = PreviewAction(
        row, staged ? QStringLiteral("Unstage") : QStringLiteral("Stage"),
        GitPaneObjectName(staged ? QStringLiteral("rightRailGitUnstage")
                                 : QStringLiteral("rightRailGitStage"),
                          repository->suffix, row_index));
    action->setProperty("gitPath", change.path);
    action->setProperty("gitOriginalPath", change.original_path);
    action->setMinimumHeight(22);
    action->setToolTip(staged ? QStringLiteral("Unstage %1").arg(change.path)
                              : QStringLiteral("Stage %1").arg(change.path));
    layout->addWidget(action);
    QObject::connect(action, &QPushButton::clicked, action,
                     [this, path, serial, change_paths, staged]() {
                       StageGitPaths(path, serial, change_paths, !staged);
                     });

    auto* menu = new QMenu(row);
    auto* view_diff = menu->addAction(QStringLiteral("View diff"));
    QObject::connect(view_diff, &QAction::triggered, row, show_diff);
    auto* preview = menu->addAction(QStringLiteral("Preview file"));
    QObject::connect(preview, &QAction::triggered, row,
                     [this, path, change]() {
                       ShowProjectPreview(
                           QDir(path).filePath(change.path));
                     });
    auto* copy = menu->addAction(QStringLiteral("Copy path"));
    QObject::connect(copy, &QAction::triggered, row, [change]() {
      QGuiApplication::clipboard()->setText(change.path);
    });
    if (!staged) {
      menu->addSeparator();
      auto* ignore_file = menu->addAction(QStringLiteral("Ignore this file"));
      QObject::connect(
          ignore_file, &QAction::triggered, row,
          [this, path, serial, pattern = GitignorePathPattern(change.path)]() {
            AddGitignorePattern(path, serial, pattern);
          });
      const QString directory = GitignoreDirectoryPattern(change.path);
      if (!directory.isEmpty()) {
        auto* ignore_directory =
            menu->addAction(QStringLiteral("Ignore parent folder"));
        QObject::connect(
            ignore_directory, &QAction::triggered, row,
            [this, path, serial, directory]() {
              AddGitignorePattern(path, serial, directory);
            });
      }
      const QString extension = GitignoreExtensionPattern(change.path);
      if (!extension.isEmpty()) {
        auto* ignore_extension =
            menu->addAction(QStringLiteral("Ignore %1 files").arg(extension));
        QObject::connect(
            ignore_extension, &QAction::triggered, row,
            [this, path, serial, extension]() {
              AddGitignorePattern(path, serial, extension);
            });
      }
    }
    auto* more = PreviewAction(
        row, QStringLiteral("⋯"),
        GitPaneObjectName(QStringLiteral("rightRailGitChangeMenu"),
                          repository->suffix, row_index));
    more->setFixedSize(20, 22);
    more->setMenu(menu);
    more->setToolTip(QStringLiteral("More file actions"));
    layout->addWidget(more);

    repository->changes_layout->addWidget(row);
    ++row_index;
  };

  if (!snapshot.changes.empty()) {
    add_group_header(QStringLiteral("Staged"), staged_count, true);
    if (staged_count == 0) {
      add_empty_group(QStringLiteral("No staged changes"));
    }
    for (const GitFileChange& change : snapshot.changes) {
      if (change.staged) {
        add_change_row(change, true);
      }
    }
    add_group_header(QStringLiteral("Changes"), unstaged_count, false);
    if (unstaged_count == 0) {
      add_empty_group(QStringLiteral("No unstaged changes"));
    }
    for (const GitFileChange& change : snapshot.changes) {
      if (!change.staged) {
        add_change_row(change, false);
      }
    }
  }

  if (repository->commit != nullptr) {
    repository->commit->setEnabled(staged_count > 0);
  }
  if (repository->push != nullptr) {
    repository->push->setEnabled(true);
  }
  if (repository->generate != nullptr && !repository->ai_generating) {
    repository->generate->setEnabled(true);
  }
}

void RightRail::RefreshGitignore(GitRepoPane* repository) {
  if (repository == nullptr || repository->ignore_layout == nullptr) {
    return;
  }
  ClearLayout(repository->ignore_layout);
  const QStringList entries = ReadGitignoreEntries(repository->path);
  const QString suffix = repository->suffix;
  if (entries.isEmpty()) {
    auto* empty = RailLabel(repository->ignore_host, theme_,
                            QStringLiteral("No ignore rules"), 10,
                            QFont::Normal, theme_.text_tertiary);
    empty->setObjectName(
        QStringLiteral("rightRailGitIgnoreEmpty%1").arg(suffix));
    empty->setWordWrap(false);
    repository->ignore_layout->addWidget(empty);
    return;
  }
  const QString path = repository->path;
  const quint64 serial = repository->serial;
  for (int i = 0; i < entries.size(); ++i) {
    const QString pattern = entries[i];
    auto* row = new QWidget(repository->ignore_host);
    row->setObjectName(
        GitPaneObjectName(QStringLiteral("rightRailGitIgnoreRow"), suffix, i));
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    auto* label = RailLabel(row, theme_, pattern, 10, QFont::Normal,
                            theme_.text_secondary);
    label->setObjectName(
        GitPaneObjectName(QStringLiteral("rightRailGitIgnoreEntry"), suffix,
                          i));
    label->setWordWrap(false);
    label->setProperty("gitIgnoreEntry", true);
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    label->setMinimumWidth(0);
    label->setToolTip(pattern);
    layout->addWidget(label, 1);
    auto* remove = PreviewAction(
        row, QStringLiteral("×"),
        GitPaneObjectName(QStringLiteral("rightRailGitIgnoreRemove"), suffix,
                          i));
    remove->setFixedSize(18, 20);
    remove->setProperty("gitIgnorePattern", pattern);
    remove->setToolTip(
        QStringLiteral("Remove %1 from .gitignore").arg(pattern));
    layout->addWidget(remove);
    QObject::connect(remove, &QPushButton::clicked, remove,
                     [this, path, serial, pattern]() {
                       RemoveGitignorePattern(path, serial, pattern);
                     });
    repository->ignore_layout->addWidget(row);
  }
}

void RightRail::StageGitPaths(const QString& path, quint64 serial,
                              const QStringList& paths, bool stage) {
  if (paths.isEmpty()) {
    return;
  }
  GitRepoPane* repository = FindGitRepository(path, serial);
  if (repository == nullptr || repository->message_status == nullptr) {
    return;
  }
  repository->message_status->setText(
      stage ? QStringLiteral("Staging…") : QStringLiteral("Unstaging…"));
  if (!stage) {
    UnstageGitPaths(path, serial, paths, false);
    return;
  }
  QStringList stage_arguments{QStringLiteral("add"), QStringLiteral("-A"),
                              QStringLiteral("--")};
  stage_arguments << paths;
  RunGit(path, stage_arguments,
         [this, path, serial](bool ok, const QString& output) {
           GitRepoPane* current = FindGitRepository(path, serial);
           if (current == nullptr || current->message_status == nullptr) {
             return;
           }
           if (!ok) {
             current->message_status->setText(output);
             return;
           }
           current->message_status->setText(QStringLiteral("Staged."));
           RefreshGitStatus(path, serial);
         });
}

void RightRail::UnstageGitPaths(const QString& path, quint64 serial,
                                const QStringList& paths, bool all) {
  if (!all && paths.isEmpty()) {
    return;
  }
  GitRepoPane* repository = FindGitRepository(path, serial);
  if (repository == nullptr || repository->message_status == nullptr) {
    return;
  }
  repository->message_status->setText(QStringLiteral("Unstaging…"));
  const auto finish = [this, path, serial, all](bool ok,
                                                const QString& output) {
    GitRepoPane* current = FindGitRepository(path, serial);
    if (current == nullptr || current->message_status == nullptr) {
      return;
    }
    if (!ok) {
      current->message_status->setText(output);
      return;
    }
    current->message_status->setText(
        all ? QStringLiteral("Unstaged all changes.")
            : QStringLiteral("Unstaged."));
    RefreshGitStatus(path, serial);
  };
  QStringList restore{QStringLiteral("restore"), QStringLiteral("--staged"),
                      QStringLiteral("--")};
  if (all) {
    restore << QStringLiteral(".");
  } else {
    restore << paths;
  }
  RunGit(path, restore,
         [this, path, paths, all, finish](bool ok, const QString& output) {
           if (ok) {
             finish(true, output);
             return;
           }
           // A repository with no commits has no HEAD to restore from. Remove
           // the paths from the index instead, leaving the worktree untouched.
           RunGit(
               path,
               {QStringLiteral("rev-parse"), QStringLiteral("--verify"),
                QStringLiteral("HEAD")},
               [this, path, paths, all, finish, output](bool has_head,
                                                        const QString&) {
                 if (has_head) {
                   finish(false, output);
                   return;
                 }
                 QStringList cached{QStringLiteral("rm"),
                                    QStringLiteral("--cached"),
                                    QStringLiteral("-r"),
                                    QStringLiteral("--quiet"),
                                    QStringLiteral("--")};
                 if (all) {
                   cached << QStringLiteral(".");
                 } else {
                   cached << paths;
                 }
                 RunGit(path, cached, finish);
               });
         });
}

void RightRail::StageAllGit(const QString& path, quint64 serial, bool stage) {
  GitRepoPane* repository = FindGitRepository(path, serial);
  if (repository == nullptr || repository->message_status == nullptr) {
    return;
  }
  if (!stage) {
    UnstageGitPaths(path, serial, {}, true);
    return;
  }
  repository->message_status->setText(QStringLiteral("Staging all…"));
  RunGit(
      path, {QStringLiteral("add"), QStringLiteral("-A")},
      [this, path, serial](bool ok, const QString& output) {
        GitRepoPane* current = FindGitRepository(path, serial);
        if (current == nullptr || current->message_status == nullptr) {
          return;
        }
        if (!ok) {
          current->message_status->setText(output);
          return;
        }
        current->message_status->setText(QStringLiteral("Staged all changes."));
        RefreshGitStatus(path, serial);
      });
}

void RightRail::ShowGitDiff(const QString& path, quint64 serial,
                            const QStringList& paths,
                            const QString& display_path, bool staged,
                            bool untracked) {
  GitRepoPane* repository = FindGitRepository(path, serial);
  if (repository == nullptr || repository->diff_host == nullptr ||
      repository->diff_title == nullptr || repository->diff_view == nullptr ||
      paths.isEmpty()) {
    return;
  }
  ++repository->diff_generation;
  const quint64 generation = repository->diff_generation;
  repository->diff_host->setVisible(true);
  QString title_prefix = QStringLiteral("Unstaged diff");
  if (staged) {
    title_prefix = QStringLiteral("Staged diff");
  } else if (untracked) {
    title_prefix = QStringLiteral("New file");
  }
  repository->diff_title->setText(
      QStringLiteral("%1 · %2").arg(title_prefix, display_path));
  repository->diff_title->setToolTip(display_path);
  repository->diff_view->setPlainText(QStringLiteral("Loading diff…"));
  QStringList arguments;
  if (untracked) {
    arguments << QStringLiteral("diff") << QStringLiteral("--no-index")
              << QStringLiteral("--") << QStringLiteral("/dev/null")
              << paths.first();
  } else {
    arguments << QStringLiteral("diff");
    if (staged) {
      arguments << QStringLiteral("--cached");
    }
    arguments << QStringLiteral("--no-ext-diff") << QStringLiteral("--")
              << paths;
  }
  RunGit(path, arguments,
         [this, path, serial, generation](bool ok, const QString& output) {
           GitRepoPane* current = FindGitRepository(path, serial);
           if (current == nullptr || current->diff_generation != generation ||
               current->diff_view == nullptr) {
             return;
           }
           // git diff exits 1 when it finds differences. The output is still
           // the diff we want, so accept anything that looks like one.
           const bool looks_like_diff =
               output.startsWith(QStringLiteral("diff --git")) ||
               output.contains(QStringLiteral("\n@@ "));
           if (!ok && !looks_like_diff) {
             current->diff_view->setPlainText(
                 output.isEmpty() ? QStringLiteral("Unable to read the diff.")
                                  : output);
             return;
           }
           current->diff_view->setPlainText(
               output.isEmpty()
                   ? QStringLiteral("No textual diff in this file.")
                   : output);
           current->diff_view->moveCursor(QTextCursor::Start);
         });
}

void RightRail::AddGitignorePattern(const QString& path, quint64 serial,
                                    const QString& pattern) {
  GitRepoPane* repository = FindGitRepository(path, serial);
  if (repository == nullptr || repository->message_status == nullptr) {
    return;
  }
  const QString normalized = pattern.trimmed();
  if (normalized.isEmpty()) {
    repository->message_status->setText(
        QStringLiteral("Enter a file, directory, or pattern."));
    return;
  }
  QString error;
  if (!AppendGitignoreEntry(path, normalized, &error)) {
    repository->message_status->setText(error);
    return;
  }
  if (repository->ignore_input != nullptr) {
    repository->ignore_input->clear();
  }
  repository->message_status->setText(
      QStringLiteral("Added %1 to .gitignore.").arg(normalized));
  RefreshGitStatus(path, serial);
}

void RightRail::RemoveGitignorePattern(const QString& path, quint64 serial,
                                       const QString& pattern) {
  GitRepoPane* repository = FindGitRepository(path, serial);
  if (repository == nullptr || repository->message_status == nullptr) {
    return;
  }
  QString error;
  if (!RemoveGitignoreEntry(path, pattern, &error)) {
    repository->message_status->setText(error);
    return;
  }
  repository->message_status->setText(
      QStringLiteral("Removed %1 from .gitignore.").arg(pattern));
  RefreshGitStatus(path, serial);
}

void RightRail::RunGit(
    const QString& root, const QStringList& arguments,
    std::function<void(bool ok, const QString& output)> done) {
  if (root.isEmpty() || !QFileInfo(root).isDir()) {
    done(false, QStringLiteral("No project selected."));
    return;
  }
  auto* process = new QProcess(this);
  process->setWorkingDirectory(root);
  process->setProcessChannelMode(QProcess::MergedChannels);
  auto completed = std::make_shared<bool>(false);
  auto finish = [process, done, completed](bool ok, const QString& output) {
    if (*completed) {
      return;
    }
    *completed = true;
    done(ok, output);
    process->deleteLater();
  };
  QObject::connect(
      process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
      process, [process, finish](int code, QProcess::ExitStatus status) {
        finish(
            status == QProcess::NormalExit && code == 0,
            QString::fromLocal8Bit(process->readAllStandardOutput()).trimmed());
      });
  QObject::connect(process, &QProcess::errorOccurred, process,
                   [process, finish](QProcess::ProcessError error) {
                     if (error != QProcess::FailedToStart) {
                       return;
                     }
                     finish(false, QStringLiteral("Unable to run git: %1")
                                       .arg(process->errorString()));
                   });
  process->start(QStringLiteral("git"), arguments);
}

void RightRail::CommitStagedGit(
    const QString& path, quint64 serial, const QString& message,
    std::function<void(bool ok, const QString& output)> done) {
  GitRepoPane* repository = FindGitRepository(path, serial);
  if (repository == nullptr || repository->message_status == nullptr) {
    done(false, QStringLiteral("No git repository."));
    return;
  }
  repository->message_status->setText(
      QStringLiteral("Checking staged changes…"));
  RunGit(
      path,
      {QStringLiteral("diff"), QStringLiteral("--cached"),
       QStringLiteral("--name-only")},
      [this, path, serial, message, done](bool ok, const QString& output) {
        GitRepoPane* current = FindGitRepository(path, serial);
        if (current == nullptr || current->message_status == nullptr) {
          done(false, QStringLiteral("No git repository."));
          return;
        }
        if (!ok) {
          done(false, output);
          return;
        }
        if (output.trimmed().isEmpty()) {
          done(false, QStringLiteral("Stage files before committing."));
          return;
        }
        current->message_status->setText(QStringLiteral("Committing…"));
        RunGit(
            path, {QStringLiteral("commit"), QStringLiteral("-m"), message},
            [done](bool committed, const QString& commit_output) {
              done(committed, commit_output);
            });
      });
}

void RightRail::CommitGit(const QString& path, quint64 serial) {
  GitRepoPane* repository = FindGitRepository(path, serial);
  if (repository == nullptr || repository->message == nullptr ||
      repository->message_status == nullptr) {
    return;
  }
  const QString message = repository->message->toPlainText().trimmed();
  if (message.isEmpty()) {
    repository->message_status->setText(
        QStringLiteral("Enter a commit message."));
    return;
  }
  CommitStagedGit(
      path, serial, message,
      [this, path, serial](bool committed, const QString& output) {
        GitRepoPane* current = FindGitRepository(path, serial);
        if (current == nullptr || current->message_status == nullptr) {
          return;
        }
        if (!committed) {
          current->message_status->setText(output);
          return;
        }
        if (current->message != nullptr) {
          current->message->clear();
        }
        current->message_status->setText(QStringLiteral("Committed."));
        RefreshGitStatus(path, serial);
      });
}

void RightRail::GenerateCommitMessage(const QString& path, quint64 serial) {
  GitRepoPane* repository = FindGitRepository(path, serial);
  if (repository == nullptr || repository->message_status == nullptr ||
      repository->generate == nullptr) {
    return;
  }
  if (repository->ai_generating) {
    return;
  }
  if (!on_generate_commit_message) {
    repository->message_status->setText(
        QStringLiteral("AI generation is unavailable."));
    return;
  }
  if (repository->path.isEmpty()) {
    repository->message_status->setText(
        QStringLiteral("No repository selected."));
    return;
  }
  const QString avoid = repository->message == nullptr
                            ? QString()
                            : repository->message->toPlainText().trimmed();
  const quint64 generation = ++repository->ai_generation;
  repository->ai_generating = true;
  repository->generate->setEnabled(false);
  repository->message_status->setText(
      QStringLiteral("Reading staged changes…"));
  GatherCommitContext(
      path, [this, path, serial, generation, avoid](
                bool ok, const QString& diff, const QString& recent_commits,
                const QString& error) {
        GitRepoPane* current = FindGitRepository(path, serial);
        if (current == nullptr || current->ai_generation != generation) {
          return;
        }
        if (!ok) {
          FinishCommitGeneration(path, serial);
          current = FindGitRepository(path, serial);
          if (current != nullptr && current->message_status != nullptr) {
            current->message_status->setText(
                error.isEmpty() ? QStringLiteral("Unable to read git diff.")
                                : error);
          }
          return;
        }
        if (diff.trimmed().isEmpty()) {
          FinishCommitGeneration(path, serial);
          current = FindGitRepository(path, serial);
          if (current != nullptr && current->message_status != nullptr) {
            current->message_status->setText(
                QStringLiteral("No changes to describe."));
          }
          return;
        }
        if (current->message_status != nullptr) {
          current->message_status->setText(QStringLiteral("Generating…"));
        }
        on_generate_commit_message(
            diff, recent_commits, avoid,
            [this, path, serial, generation](const QString& text_in,
                                             const QString& error_in) {
              GitRepoPane* done = FindGitRepository(path, serial);
              if (done == nullptr || done->ai_generation != generation) {
                return;
              }
              FinishCommitGeneration(path, serial);
              done = FindGitRepository(path, serial);
              if (done == nullptr || done->message_status == nullptr) {
                return;
              }
              const QString text = text_in.trimmed();
              const QString error = error_in.trimmed();
              if (!error.isEmpty()) {
                done->message_status->setText(
                    QStringLiteral("AI generation failed: %1").arg(error));
                return;
              }
              if (text.isEmpty()) {
                done->message_status->setText(
                    QStringLiteral("No commit message returned."));
                return;
              }
              if (done->message != nullptr) {
                done->message->setPlainText(text);
              }
              done->message_status->setText(
                  QStringLiteral("Generated · review and commit."));
            });
      });
}

void RightRail::GatherCommitContext(
    const QString& root,
    std::function<void(bool ok, const QString& diff,
                       const QString& recent_commits, const QString& error)>
        done) {
  auto after_diff = [this, root, done](bool ok, const QString& diff) {
    if (!ok) {
      done(false, QString(), QString(),
           diff.isEmpty() ? QStringLiteral("Unable to read git diff.") : diff);
      return;
    }
    QString context = diff;
    RunGit(root,
           {QStringLiteral("ls-files"), QStringLiteral("--others"),
            QStringLiteral("--exclude-standard")},
           [this, root, done, context](bool, const QString& untracked) mutable {
             if (!untracked.trimmed().isEmpty()) {
               context += QStringLiteral("\n# Untracked files:\n");
               context += untracked;
               context += QChar('\n');
             }
             RunGit(
                 root,
                 {QStringLiteral("log"), QStringLiteral("-n"),
                  QStringLiteral("10"), QStringLiteral("--pretty=format:%s")},
                 [done, context](bool, const QString& recent) mutable {
                   constexpr int kDiffMaxChars = 120000;
                   if (context.size() > kDiffMaxChars) {
                     const int omitted =
                         static_cast<int>(context.size()) - kDiffMaxChars;
                     context = context.left(kDiffMaxChars);
                     context += QStringLiteral(
                                    "\n# diff truncated (%1 chars omitted)\n")
                                    .arg(omitted);
                   }
                   done(true, context, recent.trimmed(), QString());
                 });
           });
  };
  RunGit(root,
         {QStringLiteral("diff"), QStringLiteral("--cached"),
          QStringLiteral("--name-only")},
         [this, root, done, after_diff](bool listed,
                                        const QString& staged) mutable {
           if (listed && !staged.trimmed().isEmpty()) {
             RunGit(root, {QStringLiteral("diff"), QStringLiteral("--cached")},
                    after_diff);
             return;
           }
           RunGit(root, {QStringLiteral("diff"), QStringLiteral("HEAD")},
                  [this, root, done, after_diff](bool ok,
                                                 const QString& diff) mutable {
                    if (ok) {
                      after_diff(true, diff);
                      return;
                    }
                    RunGit(root, {QStringLiteral("diff")}, after_diff);
                  });
         });
}

void RightRail::FinishCommitGeneration(const QString& path, quint64 serial) {
  GitRepoPane* repository = FindGitRepository(path, serial);
  if (repository == nullptr) {
    return;
  }
  repository->ai_generating = false;
  if (repository->generate != nullptr) {
    repository->generate->setEnabled(true);
  }
}

void RightRail::PushGit(const QString& path, quint64 serial) {
  GitRepoPane* repository = FindGitRepository(path, serial);
  if (repository == nullptr || repository->message_status == nullptr) {
    return;
  }
  const QString message = repository->message == nullptr
                              ? QString()
                              : repository->message->toPlainText().trimmed();
  const auto push = [this, path, serial]() {
    GitRepoPane* current = FindGitRepository(path, serial);
    if (current == nullptr || current->message_status == nullptr) {
      return;
    }
    current->message_status->setText(QStringLiteral("Pushing…"));
    RunGit(path, {QStringLiteral("push")},
           [this, path, serial](bool ok, const QString& output) {
             GitRepoPane* done = FindGitRepository(path, serial);
             if (done == nullptr || done->message_status == nullptr) {
               return;
             }
             done->message_status->setText(ok ? QStringLiteral("Pushed.")
                                              : output);
             if (ok) {
               RefreshGitStatus(path, serial);
             }
           });
  };
  if (message.isEmpty()) {
    push();
    return;
  }
  CommitStagedGit(
      path, serial, message,
      [this, path, serial, push](bool committed, const QString& output) {
        GitRepoPane* current = FindGitRepository(path, serial);
        if (current == nullptr || current->message_status == nullptr) {
          return;
        }
        if (!committed) {
          current->message_status->setText(output);
          return;
        }
        if (current->message != nullptr) {
          current->message->clear();
        }
        push();
      });
}

void RightRail::EnsureTerminal() {
  if (terminal_ == nullptr) {
    return;
  }
  const QString cwd =
      QFileInfo(project_root_).isDir() ? project_root_ : QDir::homePath();
  terminal_->SetWorkingDirectory(cwd);
  if (!terminal_->IsRunning()) {
    terminal_->Start();
  }
}

void RightRail::RestartTerminal() {
  if (terminal_ == nullptr) {
    return;
  }
  const QString cwd =
      QFileInfo(project_root_).isDir() ? project_root_ : QDir::homePath();
  terminal_->SetWorkingDirectory(cwd);
  terminal_->Restart();
}

void RightRail::RunTerminalCommandForTest(const QString& command) {
  EnsureTerminal();
  if (terminal_ == nullptr) {
    return;
  }
  terminal_->RunCommandForTest(command);
}

void RightRail::RebuildContext(const ProfileSnapshot& snapshot) {
  const SessionUsage& usage = snapshot.usage;
  context_session_->setText(
      snapshot.selected_session_id.isEmpty()
          ? QStringLiteral("No session selected")
          : QStringLiteral("Session · %1").arg(snapshot.selected_session_id));
  int total_messages = 0;
  for (const ChatMessage& message : snapshot.messages) {
    if (!message.blocks.empty() || !message.reasoning.isEmpty()) {
      ++total_messages;
    }
  }
  if (usage.context_max > 0) {
    const int percent =
        usage.context_percent > 0
            ? usage.context_percent
            : static_cast<int>(std::min<qint64>(
                  100, (usage.context_used * 100 + usage.context_max / 2) /
                           usage.context_max));
    context_summary_->setText(QStringLiteral("%1 / %2 tokens · %3%")
                                  .arg(CompactUsageTokens(usage.context_used),
                                       CompactUsageTokens(usage.context_max))
                                  .arg(percent));
    context_bar_->setValue(std::max(0, std::min(100, percent)));
    context_bar_->setVisible(true);
  } else {
    context_summary_->setText(QStringLiteral("Context usage has not arrived."));
    context_bar_->setVisible(false);
  }
  QString detail = QStringLiteral("%1 messages").arg(total_messages);
  if (!snapshot.model.isEmpty()) {
    detail += QStringLiteral(" · %1").arg(snapshot.model);
  }
  if (!snapshot.reasoning_effort.isEmpty()) {
    detail += QStringLiteral(" · %1 reasoning").arg(snapshot.reasoning_effort);
  }
  if (usage.total > 0) {
    detail += QStringLiteral(" · %1 total tokens")
                  .arg(CompactUsageTokens(usage.total));
  }
  context_detail_->setText(detail);
  context_control_->setText(snapshot.session_control_summary.trimmed());
  context_control_->setVisible(
      !snapshot.session_control_summary.trimmed().isEmpty());
  const GatewayNotice* latest = nullptr;
  for (const GatewayNotice& notice : snapshot.gateway_notices) {
    if (latest == nullptr || notice.serial >= latest->serial) {
      latest = &notice;
    }
  }
  if (latest == nullptr) {
    context_notice_->clear();
    context_notice_->setVisible(false);
  } else {
    context_notice_->setText(QStringLiteral("%1 · %2").arg(
        GatewayNoticeTitle(latest->level), GatewayNoticeLine(*latest)));
    context_notice_->setVisible(true);
  }
}

RightRail::AgentCard* RightRail::FindAgentCard(const QString& id) {
  auto it = agent_cards_.find(id);
  return it == agent_cards_.end() ? nullptr : &it->second;
}

void RightRail::CreateAgentCard(const QString& id) {
  AgentCard card;
  card.id = id;
  card.card = new QWidget(agents_list_);
  card.card->setObjectName(QStringLiteral("rightRailAgentCard_") + id);
  card.card->setProperty("railAgentCard", true);
  card.card->setAttribute(Qt::WA_StyledBackground, true);
  auto* card_layout = new QVBoxLayout(card.card);
  card_layout->setContentsMargins(0, 0, 0, 0);
  card_layout->setSpacing(0);

  card.header = new AgentHeaderButton(id, theme_, card.card);
  card_layout->addWidget(card.header);
  QObject::connect(
      card.header, &QPushButton::clicked, card.header,
      [this, id]() { SetAgentExpanded(id, !expanded_agents_.contains(id)); });

  card.details = new QWidget(card.card);
  card.details->setObjectName(QStringLiteral("rightRailAgentDetails_") + id);
  auto* details_layout = new QVBoxLayout(card.details);
  details_layout->setContentsMargins(10, 0, 10, 8);
  details_layout->setSpacing(5);

  card.meta = RailLabel(card.details, theme_, QString(), 10, QFont::Normal,
                        theme_.text_secondary);
  card.meta->setObjectName(QStringLiteral("rightRailAgentMeta_") + id);
  details_layout->addWidget(card.meta);

  card.stream_host = new QWidget(card.details);
  card.stream_host->setObjectName(QStringLiteral("rightRailAgentStream_") + id);
  card.stream_layout = new QVBoxLayout(card.stream_host);
  card.stream_layout->setContentsMargins(0, 0, 0, 0);
  card.stream_layout->setSpacing(2);
  details_layout->addWidget(card.stream_host);

  card.controls = new QWidget(card.details);
  card.controls->setObjectName(QStringLiteral("rightRailAgentControls_") + id);
  auto* control_layout = new QHBoxLayout(card.controls);
  control_layout->setContentsMargins(0, 0, 0, 0);
  control_layout->setSpacing(4);
  card.steer_input = new QLineEdit(card.controls);
  card.steer_input->setObjectName(QStringLiteral("rightRailAgentSteerInput_") +
                                  id);
  card.steer_input->setProperty("railAgentSteer", true);
  card.steer_input->setPlaceholderText(QStringLiteral("Steer this agent…"));
  card.steer_input->setClearButtonEnabled(true);
  control_layout->addWidget(card.steer_input, 1);
  card.steer = PreviewAction(card.controls, QStringLiteral("Steer"),
                             QStringLiteral("rightRailAgentSteer_") + id);
  card.steer->setEnabled(false);
  control_layout->addWidget(card.steer);
  card.stop = PreviewAction(card.controls, QStringLiteral("Stop"),
                            QStringLiteral("rightRailAgentStop_") + id);
  control_layout->addWidget(card.stop);
  details_layout->addWidget(card.controls);

  card.feedback = RailLabel(card.details, theme_, QString(), 10, QFont::Normal,
                            theme_.text_secondary);
  card.feedback->setObjectName(QStringLiteral("rightRailAgentFeedback_") + id);
  card.feedback->hide();
  details_layout->addWidget(card.feedback);

  QObject::connect(card.steer_input, &QLineEdit::textChanged, card.steer,
                   [steer = card.steer](const QString& text) {
                     steer->setEnabled(!text.trimmed().isEmpty());
                   });
  QObject::connect(card.steer_input, &QLineEdit::returnPressed, card.steer,
                   [this, id]() { SteerAgent(id); });
  QObject::connect(card.steer, &QPushButton::clicked, card.steer,
                   [this, id]() { SteerAgent(id); });
  QObject::connect(card.stop, &QPushButton::clicked, card.stop,
                   [this, id]() { InterruptAgent(id); });

  card.details->hide();
  card.header->SetExpanded(false);
  card_layout->addWidget(card.details);
  agents_list_->layout()->addWidget(card.card);
  agent_cards_[id] = card;
}

void RightRail::UpdateAgentCard(const SubagentProgress& agent, int depth,
                                const ProfileSnapshot& snapshot,
                                qint64 now_ms) {
  AgentCard* card = FindAgentCard(agent.id);
  if (card == nullptr) {
    return;
  }
  card->owner_session_id =
      !agent.owner_session_id.isEmpty()
          ? agent.owner_session_id
          : (!agent.session_id.isEmpty() ? agent.session_id
                                         : snapshot.selected_session_id);
  const QString agent_label = agent.goal.isEmpty() ? agent.id : agent.goal;
  card->steer_input->setAccessibleName(
      QStringLiteral("Steer %1").arg(agent_label));
  card->stop->setAccessibleName(QStringLiteral("Stop %1").arg(agent_label));
  card->header->SetContent(agent, depth, now_ms);
  const QString meta = AgentMeta(agent, now_ms);
  card->meta->setText(meta);
  card->meta->setVisible(!meta.isEmpty());

  const QString signature = AgentStreamSignature(agent);
  if (signature != card->stream_signature) {
    ClearLayout(card->stream_layout);
    const std::size_t start =
        agent.stream.size() > 8 ? agent.stream.size() - 8 : 0;
    for (std::size_t i = start; i < agent.stream.size(); ++i) {
      const SubagentStreamEntry& entry = agent.stream[i];
      const QColor color = entry.error
                               ? theme_.danger
                               : (entry.kind == QStringLiteral("thinking")
                                      ? theme_.text_tertiary
                                      : theme_.text_secondary);
      const QString stamp = entry.at_ms > 0
                                ? QDateTime::fromMSecsSinceEpoch(entry.at_ms)
                                      .toString(QStringLiteral("HH:mm:ss"))
                                : QString();
      const QString line = stamp.isEmpty()
                               ? entry.text
                               : stamp + QStringLiteral("  ") + entry.text;
      auto* label =
          RailLabel(card->stream_host, theme_, line, 10, QFont::Normal, color);
      label->setObjectName(QStringLiteral("rightRailAgentStreamLine_") +
                           agent.id + QStringLiteral("_") +
                           QString::number(i - start));
      card->stream_layout->addWidget(label);
    }
    if (agent.stream.empty()) {
      auto* waiting = RailLabel(card->stream_host, theme_,
                                QStringLiteral("Waiting for the first update."),
                                10, QFont::Normal, theme_.text_tertiary);
      waiting->setObjectName(QStringLiteral("rightRailAgentStreamEmpty_") +
                             agent.id);
      card->stream_layout->addWidget(waiting);
    }
    card->stream_signature = signature;
  }

  const bool live = !SubagentIsTerminal(agent.status);
  const bool can_steer = live && on_steer_subagent != nullptr;
  const bool can_stop = live && on_interrupt_subagent != nullptr;
  card->steer_input->setVisible(can_steer);
  card->steer->setVisible(can_steer);
  card->stop->setVisible(can_stop);
  card->controls->setVisible(can_steer || can_stop);
  card->feedback->setVisible((can_steer || can_stop) &&
                             !card->feedback->text().isEmpty());
  if (!live) {
    card->feedback->clear();
    card->steer_input->clear();
  }

  const bool expanded = expanded_agents_.contains(agent.id);
  card->expanded = expanded;
  card->details->setVisible(expanded);
  card->header->SetExpanded(expanded);
}

void RightRail::SetAgentExpanded(const QString& id, bool expanded) {
  AgentCard* card = FindAgentCard(id);
  if (card == nullptr) {
    return;
  }
  if (expanded) {
    expanded_agents_.insert(id);
  } else {
    expanded_agents_.remove(id);
  }
  card->expanded = expanded;
  card->header->SetExpanded(expanded);
  card->details->setVisible(expanded);
  if (expanded && sections_scroll_ != nullptr) {
    QTimer::singleShot(0, this, [this, id]() {
      AgentCard* current = FindAgentCard(id);
      if (current != nullptr) {
        sections_scroll_->ensureWidgetVisible(current->card, 0, 12);
      }
    });
  }
}

void RightRail::SteerAgent(const QString& id) {
  AgentCard* card = FindAgentCard(id);
  if (card == nullptr || card->steer_input == nullptr ||
      card->feedback == nullptr || on_steer_subagent == nullptr) {
    return;
  }
  const QString text = card->steer_input->text().trimmed();
  if (text.isEmpty()) {
    return;
  }
  card->steer->setEnabled(false);
  card->feedback->setText(QStringLiteral("Steering…"));
  card->feedback->setProperty("railColorRole",
                              RailColorRole(theme_, theme_.text_secondary));
  StyleRailLabel(card->feedback, theme_);
  card->feedback->show();

  const QPointer<QLineEdit> input_guard(card->steer_input);
  const QPointer<QPushButton> steer_guard(card->steer);
  const QPointer<QLabel> feedback_guard(card->feedback);
  const Theme theme = theme_;
  on_steer_subagent(
      card->owner_session_id, id, text,
      [input_guard, steer_guard, feedback_guard, theme, text](
          const QJsonObject& result, const QString& error) {
        if (!feedback_guard) {
          return;
        }
        const auto set_feedback = [&feedback_guard, &theme](
                                      const QString& message,
                                      const QColor& color) {
          feedback_guard->setText(message);
          feedback_guard->setProperty("railColorRole",
                                      RailColorRole(theme, color));
          StyleRailLabel(feedback_guard, theme);
          feedback_guard->show();
        };
        if (!error.isEmpty()) {
          set_feedback(error, theme.danger);
        } else if (result.value(QStringLiteral("status")).toString() ==
                   QStringLiteral("queued")) {
          if (input_guard && input_guard->text().trimmed() == text) {
            input_guard->clear();
          }
          set_feedback(QStringLiteral("Steering queued for this agent."),
                       theme.success);
        } else {
          set_feedback(QStringLiteral("Steering was rejected."), theme.warning);
        }
        if (steer_guard && input_guard) {
          steer_guard->setEnabled(!input_guard->text().trimmed().isEmpty());
        }
      });
}

void RightRail::InterruptAgent(const QString& id) {
  AgentCard* card = FindAgentCard(id);
  if (card == nullptr || card->feedback == nullptr ||
      on_interrupt_subagent == nullptr) {
    return;
  }
  card->feedback->setText(QStringLiteral("Stopping…"));
  card->feedback->setProperty("railColorRole",
                              RailColorRole(theme_, theme_.text_secondary));
  StyleRailLabel(card->feedback, theme_);
  card->feedback->show();

  const QPointer<QLabel> feedback_guard(card->feedback);
  const Theme theme = theme_;
  on_interrupt_subagent(
      card->owner_session_id, id,
      [feedback_guard, theme](const QJsonObject& result, const QString& error) {
        if (!feedback_guard) {
          return;
        }
        QString message;
        QColor color = theme.text_secondary;
        if (!error.isEmpty()) {
          message = error;
          color = theme.danger;
        } else if (result.value(QStringLiteral("found")).toBool()) {
          message = QStringLiteral("Stop requested.");
          color = theme.warning;
        } else {
          message = QStringLiteral("Stop request was rejected.");
          color = theme.warning;
        }
        feedback_guard->setText(message);
        feedback_guard->setProperty("railColorRole",
                                    RailColorRole(theme, color));
        StyleRailLabel(feedback_guard, theme);
        feedback_guard->show();
      });
}

void RightRail::RebuildAgents(const ProfileSnapshot& snapshot) {
  auto* layout = qobject_cast<QVBoxLayout*>(agents_list_->layout());
  if (layout == nullptr) {
    return;
  }
  const auto ordered = BuildSubagentTree(snapshot.subagents);
  const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
  QSet<QString> seen;
  int live_count = 0;
  int index = 0;
  for (const SubagentProgress* agent : ordered) {
    seen.insert(agent->id);
    if (FindAgentCard(agent->id) == nullptr) {
      CreateAgentCard(agent->id);
    }
    UpdateAgentCard(*agent, SubagentDepth(agent->id, snapshot.subagents),
                    snapshot, now_ms);
    AgentCard* card = FindAgentCard(agent->id);
    if (card != nullptr && layout->indexOf(card->card) != index) {
      layout->removeWidget(card->card);
      layout->insertWidget(index, card->card);
    }
    ++index;
    if (!SubagentIsTerminal(agent->status)) {
      ++live_count;
      if (!announced_agents_.contains(agent->id) && isVisible() &&
          !collapsed_) {
        announced_agents_.insert(agent->id);
        Section* section = FindSection(QStringLiteral("agents"));
        if (section != nullptr && section->header != nullptr &&
            !section->header->property("expanded").toBool()) {
          SetSectionExpanded(QStringLiteral("agents"), true);
        }
      }
    }
  }

  for (auto it = agent_cards_.begin(); it != agent_cards_.end();) {
    if (seen.contains(it->first)) {
      ++it;
      continue;
    }
    const QString id = it->first;
    if (it->second.card != nullptr) {
      layout->removeWidget(it->second.card);
      it->second.card->deleteLater();
    }
    expanded_agents_.remove(id);
    announced_agents_.remove(id);
    it = agent_cards_.erase(it);
  }

  agents_empty_->setVisible(ordered.empty());
  SetSectionTitle(QStringLiteral("agents"),
                  live_count > 0
                      ? QStringLiteral("Agents · %1 live").arg(live_count)
                      : QStringLiteral("Agents"));
}

void RightRail::RebuildTodos(const ProfileSnapshot& snapshot) {
  auto* layout = qobject_cast<QVBoxLayout*>(todos_list_->layout());
  if (layout == nullptr) {
    return;
  }
  const QString signature = TodosSignature(snapshot.todos);
  if (signature == todos_signature_) {
    return;
  }
  todos_signature_ = signature;
  ClearLayout(layout);
  todos_empty_->setVisible(snapshot.todos.empty());
  todos_summary_->setVisible(!snapshot.todos.empty());
  todos_summary_->setText(TodoSummaryText(snapshot.todos));
  for (std::size_t index = 0; index < snapshot.todos.size(); ++index) {
    const TodoItem& item = snapshot.todos[index];
    QString text = QStringLiteral("  ").repeated(
                       std::min(16, TodoDepth(item, snapshot.todos))) +
                   TodoStatusMarker(item.status) + QStringLiteral(" ") +
                   item.content;
    auto* row = RailLabel(todos_list_, theme_, text, 11, QFont::Normal,
                          item.status == QStringLiteral("completed") ||
                                  item.status == QStringLiteral("cancelled")
                              ? theme_.text_secondary
                              : theme_.foreground);
    row->setObjectName(QStringLiteral("rightRailTodo_") +
                       (item.id.isEmpty() ? QString::number(index) : item.id));
    row->setToolTip(item.status);
    layout->addWidget(row);
  }
}

void RightRail::RebuildTimeline(const ProfileSnapshot& snapshot) {
  timeline_entries_ = CollectTimelineEntries(snapshot.messages);
  RebuildTimelineRows();
}

void RightRail::RebuildTimelineRows() {
  auto* layout = qobject_cast<QVBoxLayout*>(timeline_list_->layout());
  if (layout == nullptr) {
    return;
  }
  const QString query = timeline_search_ == nullptr
                            ? QString()
                            : timeline_search_->text().trimmed();
  const QString signature = TimelineSignature(timeline_entries_, query);
  if (signature == timeline_render_signature_) {
    return;
  }
  timeline_render_signature_ = signature;
  ClearLayout(layout);
  int shown = 0;
  for (const TimelineEntry& entry : timeline_entries_) {
    if (!query.isEmpty() &&
        !entry.preview.contains(query, Qt::CaseInsensitive)) {
      continue;
    }
    auto* button = new TimelineRowButton(entry, shown, timeline_list_);
    button->SetTheme(theme_);
    const qint64 row_id = entry.row_id;
    QObject::connect(button, &QPushButton::clicked, button, [this, row_id]() {
      if (on_timeline_open) {
        on_timeline_open(row_id);
      }
    });
    layout->addWidget(button);
    ++shown;
  }
  timeline_empty_->setVisible(shown == 0);
  timeline_empty_->setText(
      timeline_entries_.empty()
          ? QStringLiteral("Prompts from this chat appear here.")
          : QStringLiteral("No prompts match this search."));
}

void RightRail::RestoreArtifactScroll(int pane_value, int document_value,
                                      quint64 generation) {
  const auto apply = [this, pane_value, document_value](bool only_if_zero) {
    if (sections_scroll_ != nullptr) {
      QScrollBar* bar = sections_scroll_->verticalScrollBar();
      if (!only_if_zero || (pane_value > 0 && bar->value() == 0)) {
        bar->setValue(pane_value);
      }
    }
    if (artifact_preview_document_ != nullptr &&
        artifact_preview_document_->isVisible()) {
      QScrollBar* bar = artifact_preview_document_->verticalScrollBar();
      if (!only_if_zero || (document_value > 0 && bar->value() == 0)) {
        bar->setValue(document_value);
      }
    }
  };
  QTimer::singleShot(0, this, [this, generation, apply]() {
    if (generation == artifact_scroll_generation_) {
      apply(false);
    }
  });
  for (int delay : {16, 100, 300}) {
    QTimer::singleShot(delay, this, [this, generation, apply]() {
      if (generation == artifact_scroll_generation_) {
        apply(true);
      }
    });
  }
}

void RightRail::RebuildArtifacts(const ProfileSnapshot& snapshot) {
  auto* layout = qobject_cast<QVBoxLayout*>(artifacts_list_->layout());
  if (layout == nullptr) {
    return;
  }
  const int pane_scroll = sections_scroll_ != nullptr
                              ? sections_scroll_->verticalScrollBar()->value()
                              : 0;
  const int document_scroll =
      artifact_preview_document_ != nullptr
          ? artifact_preview_document_->verticalScrollBar()->value()
          : 0;
  const std::vector<SessionArtifact> artifacts =
      CollectSessionArtifacts(snapshot.messages);
  const QString render_signature = ArtifactRenderSignature(artifacts, snapshot);
  if (artifacts_rendered_ && render_signature == artifact_render_signature_) {
    return;
  }
  artifacts_rendered_ = true;
  artifact_render_signature_ = render_signature;
  const quint64 scroll_generation = ++artifact_scroll_generation_;
  ClearLayout(layout);
  artifacts_empty_->setVisible(artifacts.empty());
  artifact_home_ = snapshot.home;
  const SessionArtifact* preview_match = nullptr;
  for (const SessionArtifact& artifact : artifacts) {
    if (preview_artifact_.generated) {
      if (artifact.generated && artifact.id == preview_artifact_.id) {
        preview_match = &artifact;
      }
    } else if (!artifact.generated &&
               artifact.value == preview_artifact_.value) {
      preview_match = &artifact;
    }
  }
  if (preview_match == nullptr) {
    CloseArtifactPreview();
  } else if (preview_artifact_.generated) {
    const int index = preview_artifact_.version_index;
    preview_artifact_ = *preview_match;
    preview_artifact_.version_index =
        preview_artifact_.versions.empty()
            ? 0
            : std::max(0,
                       std::min(index, static_cast<int>(
                                           preview_artifact_.versions.size()) -
                                           1));
    artifact_preview_title_->setText(preview_artifact_.label);
    RenderArtifactPreview();
  }
  for (std::size_t index = 0; index < artifacts.size(); ++index) {
    const SessionArtifact artifact = artifacts[index];
    const QString detail = artifact.generated
                               ? QStringLiteral("%1 · %2 version(s)")
                                     .arg(artifact.language)
                                     .arg(artifact.versions.size())
                               : (QFileInfo(artifact.value).fileName().isEmpty()
                                      ? artifact.value
                                      : QFileInfo(artifact.value).fileName());
    const QString label = QStringLiteral("%1\n%2").arg(artifact.label, detail);
    auto* button = new QPushButton(label, artifacts_list_);
    button->setObjectName(
        QStringLiteral("rightRailArtifact_%1").arg(static_cast<int>(index)));
    button->setProperty("railArtifactCard", true);
    button->setFlat(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setFont(theme_.UiFont(11, QFont::Normal));
    button->setToolTip(
        artifact.generated
            ? QStringLiteral("%1 artifact: %2")
                  .arg(artifact.kind, artifact.label)
            : QStringLiteral("%1: %2").arg(artifact.kind, artifact.value));
    const auto handler = [this, artifact]() {
      if (artifact.generated) {
        ShowArtifactPreview(artifact);
        return;
      }
      if (on_artifact_open) {
        on_artifact_open(artifact);
      }
    };
    QObject::connect(button, &QPushButton::clicked, button, handler);
    auto* row = new QWidget(artifacts_list_);
    auto* row_layout = new QHBoxLayout(row);
    row_layout->setContentsMargins(0, 0, 0, 0);
    row_layout->setSpacing(4);
    row_layout->addWidget(button, 1);
    auto* preview = new QPushButton(QStringLiteral("Preview"), row);
    preview->setObjectName(QStringLiteral("rightRailArtifactPreview_%1")
                               .arg(static_cast<int>(index)));
    preview->setProperty("railAction", true);
    preview->setFlat(true);
    preview->setCursor(Qt::PointingHandCursor);
    QObject::connect(preview, &QPushButton::clicked, preview,
                     [this, artifact]() { ShowArtifactPreview(artifact); });
    row_layout->addWidget(preview);
    layout->addWidget(row);
  }
  RestoreArtifactScroll(pane_scroll, document_scroll, scroll_generation);
}

void RightRail::CloseArtifactPreview() {
  preview_artifact_ = SessionArtifact{};
  artifact_show_source_ = false;
  if (artifact_preview_ == nullptr) {
    return;
  }
  artifact_preview_->hide();
  if (artifact_preview_body_ != nullptr) {
    artifact_preview_body_->setPixmap(QPixmap());
    artifact_preview_body_->setTextFormat(Qt::AutoText);
    artifact_preview_body_->clear();
  }
  if (artifact_preview_document_ != nullptr) {
    artifact_preview_document_->hide();
  }
  if (artifact_preview_svg_ != nullptr) {
    artifact_preview_svg_->hide();
  }
}

void RightRail::ShowArtifactPreview(const SessionArtifact& artifact) {
  preview_artifact_ = artifact;
  artifact_show_source_ = false;
  artifact_preview_->show();
  artifact_preview_body_->show();
  artifact_preview_document_->hide();
  artifact_preview_svg_->hide();
  artifact_preview_title_->setText(
      artifact.label.isEmpty() ? QStringLiteral("Artifact") : artifact.label);

  const bool has_versions = artifact.versions.size() > 1;
  artifact_version_prev_->setVisible(has_versions);
  artifact_version_label_->setVisible(has_versions);
  artifact_version_next_->setVisible(has_versions);
  artifact_preview_copy_->setVisible(artifact.generated);
  artifact_preview_save_->setVisible(artifact.generated);
  const bool renderable =
      artifact.generated && (artifact.kind == QStringLiteral("html") ||
                             artifact.kind == QStringLiteral("svg"));
  artifact_preview_source_->setVisible(renderable);
  artifact_preview_browser_->setVisible(
      artifact.generated && artifact.kind == QStringLiteral("html"));
  artifact_preview_open_->setVisible(!artifact.generated);

  if (artifact.generated) {
    RenderArtifactPreview();
    return;
  }

  QString path = artifact.value;
  if (QFileInfo(path).isRelative() && !artifact_home_.isEmpty()) {
    path = QDir(artifact_home_).filePath(path);
  }
  QFile file(path);
  if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
    artifact_preview_body_->setTextFormat(Qt::PlainText);
    artifact_preview_body_->setPixmap(QPixmap());
    artifact_preview_body_->setText(
        QStringLiteral("%1\n%2\n\nNot a local file; open it in the "
                       "workspace to view.")
            .arg(artifact.kind, artifact.value));
    return;
  }
  const QByteArray bytes = file.read(64 * 1024);
  file.close();
  artifact_preview_body_->setTextFormat(Qt::PlainText);
  if (artifact.kind == QStringLiteral("image")) {
    QPixmap pixmap;
    if (pixmap.loadFromData(bytes)) {
      artifact_preview_body_->setPixmap(
          pixmap.scaledToWidth(250, Qt::SmoothTransformation));
      artifact_preview_body_->setText(QString());
      return;
    }
  }
  QString text = QString::fromUtf8(bytes);
  if (bytes.size() == 64 * 1024) {
    text += QStringLiteral("\n\n… preview truncated at 64 KB");
  }
  artifact_preview_body_->setPixmap(QPixmap());
  artifact_preview_body_->setText(text);
}

void RightRail::ShowArtifactVersion(int index) {
  if (!preview_artifact_.generated || preview_artifact_.versions.empty()) {
    return;
  }
  preview_artifact_.version_index = std::max(
      0,
      std::min(index, static_cast<int>(preview_artifact_.versions.size()) - 1));
  RenderArtifactPreview();
}

void RightRail::RenderArtifactPreview() {
  if (!preview_artifact_.generated || preview_artifact_.versions.empty()) {
    return;
  }
  const int total = static_cast<int>(preview_artifact_.versions.size());
  const int index =
      std::max(0, std::min(preview_artifact_.version_index, total - 1));
  preview_artifact_.version_index = index;
  const QString content = CurrentArtifactContent(preview_artifact_);
  artifact_version_label_->setText(
      QStringLiteral("v%1 of %2").arg(index + 1).arg(total));
  artifact_version_prev_->setEnabled(index > 0);
  artifact_version_next_->setEnabled(index + 1 < total);

  artifact_preview_body_->setPixmap(QPixmap());
  artifact_preview_document_->hide();
  artifact_preview_svg_->hide();
  const bool renderable = preview_artifact_.kind == QStringLiteral("html") ||
                          preview_artifact_.kind == QStringLiteral("svg");
  artifact_preview_source_->setText(artifact_show_source_
                                        ? QStringLiteral("Rendered")
                                        : QStringLiteral("Source"));

  if (preview_artifact_.kind == QStringLiteral("html") &&
      !artifact_show_source_) {
    artifact_preview_body_->hide();
    artifact_preview_document_->setHtml(ComposeArtifactHtml(content));
    artifact_preview_document_->show();
    return;
  }
  if (preview_artifact_.kind == QStringLiteral("svg") &&
      !artifact_show_source_) {
    artifact_preview_svg_->load(content.toUtf8());
    if (artifact_preview_svg_->renderer() != nullptr &&
        artifact_preview_svg_->renderer()->isValid()) {
      artifact_preview_body_->hide();
      artifact_preview_svg_->show();
      return;
    }
  }

  QString source = content;
  constexpr int kPreviewLimit = 128 * 1024;
  if (source.size() > kPreviewLimit) {
    source = source.left(kPreviewLimit) +
             QStringLiteral("\n\n… source preview truncated at 128 KB");
  }
  artifact_preview_body_->setTextFormat(Qt::PlainText);
  artifact_preview_body_->setText(source);
  artifact_preview_body_->show();
  if (!renderable) {
    artifact_preview_source_->hide();
  } else {
    artifact_preview_source_->show();
  }
}

void RightRail::SetSnapshot(const ProfileSnapshot& snapshot) {
  RebuildContext(snapshot);
  RebuildAgents(snapshot);
  RebuildTodos(snapshot);
  RebuildTimeline(snapshot);
  RebuildArtifacts(snapshot);
}

}  // namespace hermes::ui
