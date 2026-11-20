// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_SLASH_COMMANDS_H_
#define HERMES_UI_SLASH_COMMANDS_H_

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <vector>

namespace hermes::ui {

// One composer slash command. Commands with `takes_args` insert a trailing
// space so the argument can be typed; the rest execute on Enter.
struct SlashCommand {
  QString name;
  QStringList aliases;
  QString description;
  QString argument_hint;
  bool takes_args = false;
};

const std::vector<SlashCommand>& SlashCommandCatalog();

// Prefix match on the command name and every alias, case-insensitive. An empty
// prefix returns the whole catalog (the menu opens on "/").
std::vector<const SlashCommand*> MatchSlashCommands(const QString& prefix);
std::vector<const SlashCommand*> MatchSlashCommands(
    const std::vector<SlashCommand>& catalog, const QString& prefix);

// Resolves "/new", "/reset" or "/clear" (without arguments) to the catalog row.
const SlashCommand* FindSlashCommand(const QString& name);

// A structured `command.dispatch` result. `kOutput` is a bare slash.exec
// payload (`output`/`text`), which every client renders as command output.
struct SlashDispatch {
  enum class Kind {
    kNone,
    kOutput,
    kExec,
    kPlugin,
    kAlias,
    kSend,
    kSkill,
    kPrefill,
  };

  Kind kind = Kind::kNone;
  QString output;
  QString warning;
  QString target;
  QString message;
  QString display;
  QString notice;
  QString name;
};

// Parses the shared slash response contract. An unknown/malformed `type`
// yields kNone; a payload without a type but with output/text yields kOutput.
SlashDispatch ParseSlashDispatch(const QJsonObject& result);

// slash.exec may deliberately refuse a skill/bundle command and point at
// `command.dispatch`. Only those exact "not mine" refusals may be retried;
// a helper timeout/crash must be shown instead of re-running the command.
bool ShouldFallbackToSlashDispatch(const QString& error);

}  // namespace hermes::ui

#endif  // HERMES_UI_SLASH_COMMANDS_H_
