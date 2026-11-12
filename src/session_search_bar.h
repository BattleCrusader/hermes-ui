// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_SESSION_SEARCH_BAR_H_
#define HERMES_UI_SESSION_SEARCH_BAR_H_

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <functional>

#include "src/theme.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QTimer;

namespace hermes::ui {

// Top-of-chat session search. The right rail owns transcript-local surfaces
// (timeline, outputs, project files); cross-session search belongs above the
// transcript where the result opens into the active chat column.
class SessionSearchBar : public QWidget {
  Q_OBJECT

 public:
  explicit SessionSearchBar(const Theme& theme, QWidget* parent = nullptr);
  ~SessionSearchBar() override;

  void SetTheme(const Theme& theme);
  void SetSearchHandler(
      std::function<void(
          const QString& query,
          std::function<void(const QJsonObject& result, const QString& error)>
              done)>
          handler);
  // Context completion is supplied by the shell: recent session titles,
  // project names and profile labels are evaluated when the user types.
  void SetSuggestionsProvider(std::function<QStringList()> provider);
  void FocusSearch();
  void ClearSearch();
  QString Query() const;
  // Probe hook: skip the debounce without changing the user-facing behavior.
  void TriggerSearchForTest();

  std::function<void(const QString& session_id)> on_session_open;

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  void ScheduleSearch();
  void RunSearch();
  void RenderResults(const QJsonObject& result);
  void ClearResults();
  void OpenResult(int row);

  Theme theme_;
  QLineEdit* input_ = nullptr;
  QLabel* status_ = nullptr;
  QListWidget* results_ = nullptr;
  QTimer* timer_ = nullptr;
  quint64 generation_ = 0;
  std::function<void(const QString&,
                     std::function<void(const QJsonObject&, const QString&)>)>
      handler_;
  std::function<QStringList()> suggestions_provider_;
};

}  // namespace hermes::ui

#endif  // HERMES_UI_SESSION_SEARCH_BAR_H_
