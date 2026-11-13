// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_CHAT_VIEW_H_
#define HERMES_UI_CHAT_VIEW_H_

#include <QRectF>
#include <QScrollArea>
#include <QString>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "src/click_target.h"
#include "src/model.h"
#include "src/theme.h"

namespace hermes::ui {

class RequestBar;
class TranscriptWidget;

// Scrollable transcript. The scroll area only owns geometry; the transcript
// lays out and paints message blocks so the look is not tied to a platform
// widget style.
class ChatView : public QScrollArea {
 public:
  explicit ChatView(const Theme& theme, QWidget* parent = nullptr);

  void SetTheme(const Theme& theme);
  void SetSnapshot(std::shared_ptr<const ProfileSnapshot> snapshot);
  void SetProfileName(const QString& name);
  // Clarify/approval cards render as the last transcript item, not as a
  // separate dock above the composer.
  void SetRequest(const PendingRequest& request);
  void ClearRequest();
  // Answer a connector-directory display card with the composer's words.
  // Returns false when the visible request is not such a card.
  bool HasConnectionRequest() const;
  bool AnswerConnectionDisplay(const QString& text);
  bool ContinueConnection();
  // Transcript find: matches message text case-insensitively and scrolls the
  // viewport to the active hit. The find bar lives in the shell.
  void SetSearchQuery(const QString& query);
  void FindNext();
  void FindPrevious();
  void ClearSearch();
  int SearchMatchCount() const;
  int CurrentSearchMatch() const;
  void EnsureSearchVisible();
  // Timeline jump: scroll to a durable transcript row (0 is ignored).
  void ScrollToMessageRow(qint64 row_id);
  void SetFollowBottom(bool follow_bottom);
  // Appearance preferences. These only affect the transcript; the shell keeps
  // the values in native preferences and re-applies them after a theme change.
  void SetContentMaxWidth(int pixels);
  void SetShowTimestamps(bool show);
  void SetReasoningCollapsedByDefault(bool collapsed);
  void SetAnimationsEnabled(bool enabled);
  void SetChatFontScale(double scale);
  void SetCodeFontScale(double scale);
  bool IsCaretAnimatingForTest() const;
  // Number of messages whose previous layout was reused by the last snapshot
  // publish. Streaming updates should reuse the whole transcript except the
  // changed tail.
  int LastReusedMessageCountForTest() const;
  void ScrollToTop();
  void ScrollToBottom();
  // PageUp / PageDown and any other fractional scroll action.
  void ScrollBy(int delta);
  // Message actions are context-menu entries and painted chips. The handlers
  // let the shell open the editor / truncating resend without the transcript
  // owning session or gateway state.
  using MessageActionHandler =
      std::function<void(const ChatMessage& message, const QString& text)>;
  using ReactionHandler =
      std::function<void(const ChatMessage& message, const QString& emoji)>;
  void SetEditMessageHandler(MessageActionHandler handler);
  void SetRegenerateMessageHandler(MessageActionHandler handler);
  // Read aloud: the shell synthesizes speech through /api/audio/speak and owns
  // playback, so the transcript only forwards the message text.
  void SetReadAloudHandler(MessageActionHandler handler);
  // Pin a completed assistant reply into the next prompt's context. The gateway
  // has no message-pin RPC, so the shell prefixes pinned text on submit.
  void SetPinMessageHandler(MessageActionHandler handler);
  // Session lifecycle actions. Branch copies the transcript prefix through the
  // clicked message; rewind undoes the last turn; delete drops the session.
  void SetBranchMessageHandler(MessageActionHandler handler);
  void SetRewindMessageHandler(MessageActionHandler handler);
  void SetReactMessageHandler(ReactionHandler handler);
  void SetDeleteConversationHandler(std::function<void()> handler);
  void SetRequestRespondHandler(
      std::function<void(const QString&, const QJsonObject&)> handler);
  // Test seam: invoke a painted/context action without opening a popup.
  void TriggerMessageActionForTest(const QString& action, int message_index,
                                   const QString& value = QString());
  // Test seam: the reasoning headers are custom-painted, so their hit geometry
  // and expanded state are exposed for the click audit.
  std::vector<ClickTarget> ClickTargets() const;
  // Test seam: frame and text rect for rendered blocks of one kind. Quotes
  // carry an inner text rect so tests can prove text stays inside the card.
  std::vector<std::pair<QRectF, QRectF>> BlockRectsForTest(
      BlockKind kind) const;
  QRectF MessageBodyRectForTest(int message_index) const;
  // Test seam: the actual highlight rectangles painted for the current text
  // selection. Wrapped and multi-paragraph documents must contribute one rect
  // per selected visual line.
  std::vector<QRectF> SelectionRectsForTest() const;
  // Test seam: the first code block's base foreground. Syntax spans keep their
  // own colours; this is the colour plain code text inherits.
  QColor FirstCodeForegroundForTest() const;
  bool ReasoningExpanded(int message_index) const;
  // Text selection is painted by the transcript, so expose the same seam the
  // find bar uses. Users select with the mouse or Ctrl/Cmd+A and copy with
  // Ctrl/Cmd+C.
  void SelectAllTextForTest();
  QString SelectedTextForTest() const;

 protected:
  void resizeEvent(QResizeEvent* event) override;

 private:
  Theme theme_;
  TranscriptWidget* transcript_ = nullptr;
  QString session_id_;
  bool follow_bottom_ = true;
  double chat_font_scale_ = 1.0;
};

}  // namespace hermes::ui

#endif  // HERMES_UI_CHAT_VIEW_H_
