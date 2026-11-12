// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_SEARCH_COMPLETION_H_
#define HERMES_UI_SEARCH_COMPLETION_H_

#include <QCompleter>
#include <QLineEdit>
#include <QStringList>
#include <QStringListModel>
#include <functional>

namespace hermes::ui {

// Search fields in the shell are context surfaces, not plain text boxes. The
// provider is evaluated on every edit so the popup reflects the current
// profile, project tree, transcript or settings catalog rather than a snapshot
// captured when the field was created.
inline void AttachContextCompleter(QLineEdit* edit,
                                   std::function<QStringList()> provider) {
  if (edit == nullptr) {
    return;
  }
  auto* completer = new QCompleter(edit);
  auto* model = new QStringListModel(completer);
  completer->setModel(model);
  completer->setCaseSensitivity(Qt::CaseInsensitive);
  completer->setFilterMode(Qt::MatchContains);
  completer->setCompletionMode(QCompleter::PopupCompletion);
  completer->setMaxVisibleItems(8);
  edit->setCompleter(completer);
  QObject::connect(
      edit, &QLineEdit::textEdited, edit,
      [provider = std::move(provider), model](const QString& text) {
        if (text.trimmed().isEmpty()) {
          model->setStringList({});
          return;
        }
        QStringList suggestions = provider ? provider() : QStringList();
        suggestions.removeAll(QString());
        suggestions.removeDuplicates();
        suggestions.sort(Qt::CaseInsensitive);
        model->setStringList(suggestions);
      });
}

}  // namespace hermes::ui

#endif  // HERMES_UI_SEARCH_COMPLETION_H_
