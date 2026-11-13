// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_PROMPT_HISTORY_DIALOG_H_
#define HERMES_UI_PROMPT_HISTORY_DIALOG_H_

#include <QDialog>
#include <QString>
#include <QStringList>
#include <functional>
#include <vector>

#include "src/theme.h"

class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;

namespace hermes::ui {

// Review surface for the gateway queue plus the two local prompt
// side-channels:
//   * server  — a pending admission owned by the gateway FIFO; replace/cancel
//               mutate the backend queue, and a delivered steer is history.
//   * queued  — legacy local pending prompts on a gateway without admissions.
//   * steered  — already delivered to the live turn; edits/deletes update the
//                local history only, so the row stays honest about what was
//                sent while still letting the prompt be loaded back.
// The dialog owns copies through caller-owned QStringList pointers; it is
// modal and never outlives MainWindow's prompt lists.
class PromptHistoryDialog : public QDialog {
 public:
  // One gateway-owned pending admission. `controls` contains the verbs the
  // owner serves for this row ("steer", "replace"); `steering` means the row is
  // in transfer to the running turn and keeps its queued rank until settled.
  struct ServerPrompt {
    QString admission_id;
    QString input_id;
    QString text;
    QString status;
    QString outcome;
    QStringList controls;
    int execution_generation = -1;
    bool steering = false;
  };
  using ActionDone = std::function<void(bool ok, const QString& message)>;

  PromptHistoryDialog(const Theme& theme, QStringList* queued,
                      QStringList* steered, QWidget* parent = nullptr);

  // Replaces the authoritative backend queue. Safe to call while the dialog is
  // open: selection follows the admission id when it survives.
  void SetServerPrompts(std::vector<ServerPrompt> prompts);
  void RemoveServerPrompt(const QString& admission_id);
  void SetStatus(const QString& text);

  std::function<void()> on_changed;
  std::function<void(const QString&)> on_load;
  // An edit cancels the old admission and admits the text at the FIFO tail.
  std::function<void(const ServerPrompt&, const QString& text, ActionDone)>
      on_replace_server;
  std::function<void(const ServerPrompt&, ActionDone)> on_cancel_server;
  // Sending removes the backend admission first, then submits its text as a
  // fresh prompt. A steered admission is already part of the live turn, so it
  // is sent without a cancel.
  std::function<void(const ServerPrompt&, ActionDone)> on_send_server;
  // Local queued/steered rows have no backend admission. Sending submits the
  // text as a fresh prompt; a successful local queue entry is removed on ACK.
  std::function<void(const QString&, ActionDone)> on_send_local;

 private:
  enum class Kind { kServer, kQueued, kSteered };

  void RebuildList();
  bool Selected(Kind* kind, int* index) const;
  bool SelectedServer(ServerPrompt* prompt) const;
  void RefreshEditor();
  void SaveSelected();
  void DeleteSelected();
  void LoadSelected();
  void SendSelected();
  QStringList* queued_ = nullptr;
  QStringList* steered_ = nullptr;
  std::vector<ServerPrompt> server_;
  QString selected_admission_;
  QListWidget* list_ = nullptr;
  QPlainTextEdit* editor_ = nullptr;
  QLabel* status_ = nullptr;
  QPushButton* save_ = nullptr;
  QPushButton* remove_ = nullptr;
  QPushButton* inject_ = nullptr;
  QPushButton* load_ = nullptr;
};

}  // namespace hermes::ui

#endif  // HERMES_UI_PROMPT_HISTORY_DIALOG_H_
