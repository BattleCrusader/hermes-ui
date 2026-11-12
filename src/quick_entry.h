// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_SRC_QUICK_ENTRY_H_
#define HERMES_UI_SRC_QUICK_ENTRY_H_

#include <QDialog>
#include <QString>
#include <functional>

class QLabel;
class QPlainTextEdit;
class QPushButton;

namespace hermes::ui {

// Quick Entry is the desktop's global-hotkey mini composer. The native shell
// owns one dialog and reuses the same submit path as the main composer: the
// text becomes a new chat, never a second gateway client.
class QuickEntryDialog : public QDialog {
  Q_OBJECT

 public:
  explicit QuickEntryDialog(QWidget* parent = nullptr);

  void SetSubmitHandler(std::function<void(const QString&)> handler);
  void SetSessionLabel(const QString& label);
  void FocusEditor();
  QString Text() const;
  void SetText(const QString& text);

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  void Submit();

  QPlainTextEdit* text_ = nullptr;
  QLabel* session_label_ = nullptr;
  QLabel* hint_ = nullptr;
  QPushButton* submit_ = nullptr;
  std::function<void(const QString&)> submit_handler_;
};

}  // namespace hermes::ui

#endif  // HERMES_UI_SRC_QUICK_ENTRY_H_
