// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_REQUEST_BAR_H_
#define HERMES_UI_REQUEST_BAR_H_

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <functional>
#include <vector>

#include "src/model.h"
#include "src/theme.h"

class QLineEdit;
class QPushButton;
class QResizeEvent;
class QVBoxLayout;

namespace hermes::ui {

// Docked card for backend -> client requests above the composer. The agent
// blocks until the matching id is answered, so this is the one surface that
// must cover approval, batch clarify, sudo and secret prompts: an unanswered
// request stalls the turn for the gateway's deadline (300 s for clarify).
class RequestBar : public QWidget {
 public:
  explicit RequestBar(const Theme& theme, QWidget* parent = nullptr);

  void SetTheme(const Theme& theme);
  void SetRequest(const PendingRequest& request);
  void Clear();
  // A `manage_connections` display card is answered by the composer's words
  // (`said`) instead of a target or Continue answer.
  bool HasConnectionRequest() const;
  bool HasConnectionDisplay() const;
  bool AnswerConnectionDisplay(const QString& text);
  // Typing a message while a connection card is open ends the operation
  // first; otherwise the prompt waits behind the blocked tool batch.
  bool ContinueConnection();

  // The callback receives the request id and the exact JSON-RPC result. The
  // worker removes the request and sends the frame; this widget never touches
  // the socket. Secret values exist only in the child QLineEdit.
  std::function<void(const QString&, const QJsonObject&)> on_respond;

 protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

 private:
  void Rebuild();
  void UpdateHeight();
  void ClearContent();
  void AddHeader(const QString& title, const QString& detail);
  void AddApproval();
  void AddClarify();
  void AddConnection();
  void AddValueInput();
  QPushButton* MakeButton(const QString& label, bool primary, bool danger);
  QLineEdit* MakeInput(const QString& placeholder, bool secret);
  void UpdateChoiceStyles();
  void UpdateSendEnabled();
  QString AnswerFor(int index) const;
  void OnChoice(int question_index, const QString& choice);
  void SubmitClarify();
  void SubmitValue();
  void SendConnectionAnswer(const QString& name, const QString& status);
  void SendConnectionContinue();
  void Skip();
  void Send(const QJsonObject& result);
  void FocusNextInput(int index);

  Theme theme_;
  PendingRequest request_;
  bool has_request_ = false;
  QVBoxLayout* layout_ = nullptr;
  std::vector<QLineEdit*> inputs_;
  std::vector<std::vector<QPushButton*>> choice_buttons_;
  std::vector<QStringList> selected_;
  struct ConnectionInput {
    QString target;
    QString name;
    QLineEdit* edit = nullptr;
  };
  std::vector<ConnectionInput> connection_inputs_;
  QLineEdit* value_input_ = nullptr;
  QPushButton* send_button_ = nullptr;
  QPushButton* skip_button_ = nullptr;
};

}  // namespace hermes::ui

#endif  // HERMES_UI_REQUEST_BAR_H_
