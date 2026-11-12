// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_COMPOSER_H_
#define HERMES_UI_COMPOSER_H_

#include <QHash>
#include <QJsonArray>
#include <QPointer>
#include <QString>
#include <QWidget>
#include <functional>
#include <utility>
#include <vector>

#include "src/click_target.h"
#include "src/model.h"
#include "src/slash_commands.h"
#include "src/theme.h"

class QDialog;
class QListWidget;
class QTimer;
class QPushButton;
class QTextEdit;
class QTextEdit;

namespace hermes::ui {

// Docked composer card. A real QTextEdit provides IME and selection; the card,
// model chip and send button are painted so the surface matches the app.
class Composer : public QWidget {
  Q_OBJECT

 public:
  explicit Composer(const Theme& theme, QWidget* parent = nullptr);
  ~Composer() override;

  void SetTheme(const Theme& theme);
  void SetText(const QString& text);
  void SetModelLabel(const QString& model);
  void SetModelCatalog(const std::vector<ModelProvider>& providers,
                       const QString& provider, const QString& model);
  // Local model-picker visibility. Empty means every catalog model is shown.
  void SetHiddenModels(const QSet<QString>& hidden);
  QSet<QString> HiddenModels() const { return hidden_models_; }
  // Native test seam: the dialog is returned without entering a modal loop.
  QDialog* ShowModelVisibilityDialogForTest();
  void SetReasoning(const QString& effort);
  // Context-window meter painted beside the model/reasoning chips. Zero max
  // (no session.usage tick yet) keeps it hidden.
  void SetUsage(qint64 context_used, qint64 context_max);
  QString TokenUsageText() const;
  // Live push-to-talk status from the gateway. While recording the partial
  // transcript replaces the token meter; `status` covers transcribing/no
  // speech without pretending a capture is still live.
  void SetVoiceState(bool recording, const QString& partial,
                     const QString& status);
  QString VoiceStatusText() const;
  // Number of transcript replies pinned into the next prompt. The gateway has
  // no pin RPC; the shell composes the context prefix on submit.
  void SetPinnedContextCount(int count);
  void SetRunning(bool running, qint64 started_ms = 0);
  // Next-turn queue depth painted beside the running indicator. Queueing is
  // owned by the shell; the composer only reflects it.
  void SetQueuedPrompts(int count);
  void SetSlashCommands(std::vector<SlashCommand> commands);
  // Per-session user prompt ring, newest first. ArrowUp/ArrowDown browse it
  // only when the caret is at the start/end, so multiline editing still works.
  void SetHistory(const QStringList& history);
  // `@` completion. The shell supplies a gateway-backed directory lister and
  // the profile home that relative mentions resolve against. Listing is async
  // so typing a mention never blocks the UI on a gateway round-trip.
  using PathListCallback =
      std::function<void(const QJsonArray&, const QString&)>;
  void SetPathLister(
      std::function<void(const QString&, PathListCallback)> lister);
  void SetMentionRoot(const QString& root);
  bool MentionMenuVisible() const;
  QStringList MentionMenuEntries() const;
  void Clear();
  // Inserts a reference to a staged upload so the next prompt names the file.
  void AddAttachment(const QString& name, const QString& path);
  // Native test seam for the overflow drawer (clicking the painted +N chip).
  QMenu* ShowAttachmentDrawerForTest();
  QStringList AttachmentPaths() const;
  std::vector<ClickTarget> ClickTargets() const;
  // Slash-command completion popup state (also used by the click audit).
  bool SlashMenuVisible() const;
  QStringList SlashMenuEntries() const;
  QString Text() const;
  void FocusEditor();

  std::function<void()> on_stop;
  std::function<void(const QString& provider, const QString& model)>
      on_model_selected;
  // Bust the gateway's provider model cache. The dialog closes and reopens
  // with the refreshed catalog so newly discovered models appear in place.
  std::function<void()> on_refresh_models;
  std::function<void(const QString& effort)> on_reasoning_selected;

  // Bare `@dir/file` tokens become typed `@file:`/`@folder:` directives, which
  // is the shape the gateway understands (mirrors desktop pathifyRefs).
  static QString PromotePathRefs(const QString& text);

 signals:
  void AttachRequested();
  void VoiceToggleRequested();
  void Submitted(const QString& text);
  // Files dropped onto the card; the shell stages them through the worker so
  // they get the same 0600 copy as the attach picker.
  void FilesDropped(const QStringList& paths);
  // The composer grows one status line while a turn runs; the shell resizes
  // its host so the line is not clipped.
  void HeightChanged(int height);
  // The "N queued" status line is clickable: the shell opens the editable
  // queue/steer list.
  void QueuedReviewRequested();
  // Enter while a turn is running steers it. Ctrl+Enter queues the draft for
  // the next turn instead of interrupting the current one.
  void SteerRequested(const QString& text);
  void QueueRequested(const QString& text);

 protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void leaveEvent(QEvent* event) override;
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dropEvent(QDropEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  QRectF CardRect() const;
  QRectF SendRect() const;
  QRectF AttachRect() const;
  QRectF VoiceRect() const;
  // Chip rects for staged attachments, laid out left-to-right between the
  // editor and the toolbar.
  QRectF AttachmentChipRect(int index) const;
  void RemoveAttachment(int index);
  void UpdateSlashMenu();
  void AcceptSlashCommand();
  void HideSlashMenu();
  void UpdateMentionMenu();
  void AcceptMention();
  void HideMentionMenu();
  QString MentionDisplayPath(const QString& absolute) const;
  QListWidget* slash_menu_ = nullptr;
  QListWidget* mention_menu_ = nullptr;
  std::function<void(const QString&, PathListCallback)> path_lister_;
  QSet<QString> path_pending_;
  int mention_generation_ = 0;
  QString mention_root_;
  QString mention_directory_;
  QString mention_prefix_;
  QString mention_token_;
  QHash<QString, QJsonArray> path_cache_;
  std::vector<SlashCommand> slash_commands_;
  QRectF ModelChipRect() const;
  QRectF ReasoningChipRect() const;
  void UpdateChipStyles();
  void ShowModelMenu();
  void ShowReasoningMenu();
  QDialog* BuildModelVisibilityDialog();
  void ShowModelVisibilityDialog();
  void Submit();
  QString SubmissionText() const;
  int VisibleAttachmentCount() const;
  QRectF AttachmentDrawerRect() const;
  void ShowAttachmentDrawer();
  QPixmap AttachmentThumbnail(const QString& path);
  void UpdateEditorStyle();

  Theme theme_;
  QTextEdit* editor_ = nullptr;
  QPushButton* model_button_ = nullptr;
  QPushButton* reasoning_button_ = nullptr;
  QString model_label_ = QStringLiteral("model");
  QString provider_;
  QString reasoning_ = QStringLiteral("medium");
  std::vector<ModelProvider> providers_;
  QSet<QString> hidden_models_;
  bool running_ = false;
  std::vector<std::pair<QString, QString>> attachments_;
  QHash<QString, QPixmap> attachment_thumbnails_;
  int queued_prompts_ = 0;
  bool voice_recording_ = false;
  QString voice_partial_;
  QString voice_status_;
  int pinned_context_count_ = 0;
  qint64 context_used_ = 0;
  qint64 context_max_ = 0;
  QStringList history_;
  int history_cursor_ = -1;
  QString history_draft_;
  bool send_hovered_ = false;
  bool editor_focused_ = false;
  QPointer<QDialog> model_visibility_dialog_;
  bool model_refresh_pending_ = false;
  bool model_refresh_completed_ = false;
};

}  // namespace hermes::ui

#endif  // HERMES_UI_COMPOSER_H_
