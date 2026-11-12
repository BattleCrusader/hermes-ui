// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_SLASH_COMMANDS_H_
#define HERMES_UI_SLASH_COMMANDS_H_

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

}  // namespace hermes::ui

#endif  // HERMES_UI_SLASH_COMMANDS_H_
