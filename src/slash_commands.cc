// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/slash_commands.h"

#include <vector>

namespace hermes::ui {
namespace {

const std::vector<SlashCommand> &Catalog() {
  static const std::vector<SlashCommand> catalog = {
      {QStringLiteral("/new"),
       {QStringLiteral("/reset"), QStringLiteral("/clear")},
       QStringLiteral("Start a new chat"),
       QString(),
       false},
      {QStringLiteral("/stop"),
       {},
       QStringLiteral("Stop the running turn"),
       QString(),
       false},
      {QStringLiteral("/reasoning"),
       {},
       QStringLiteral("Set reasoning effort"),
       QStringLiteral("low|medium|high|off"),
       true},
      {QStringLiteral("/model"),
       {},
       QStringLiteral("Open model settings"),
       QString(),
       false},
      {QStringLiteral("/profile"),
       {},
       QStringLiteral("Switch profile"),
       QStringLiteral("[name]"),
       true},
      {QStringLiteral("/title"),
       {},
       QStringLiteral("Rename this chat"),
       QStringLiteral("text"),
       true},
      {QStringLiteral("/attach"),
       {},
       QStringLiteral("Attach a file to the next prompt"),
       QStringLiteral("[path]"),
       true},
      {QStringLiteral("/settings"),
       {},
       QStringLiteral("Open settings"),
       QString(),
       false},
      {QStringLiteral("/theme"),
       {QStringLiteral("/skin")},
       QStringLiteral("Toggle light/dark"),
       QString(),
       false},
      {QStringLiteral("/search"),
       {QStringLiteral("/find")},
       QStringLiteral("Focus session search"),
       QString(),
       false},
      {QStringLiteral("/handoff"),
       {},
       QStringLiteral("Hand this chat to a messaging platform"),
       QStringLiteral("telegram|discord|whatsapp"),
       true},
      {QStringLiteral("/help"),
       {QStringLiteral("/commands")},
       QStringLiteral("List slash commands"),
       QString(),
       false},
  };
  return catalog;
}

} // namespace

const std::vector<SlashCommand> &SlashCommandCatalog() { return Catalog(); }

std::vector<const SlashCommand *> MatchSlashCommands(const QString &prefix) {
  return MatchSlashCommands(Catalog(), prefix);
}

std::vector<const SlashCommand *>
MatchSlashCommands(const std::vector<SlashCommand> &catalog,
                   const QString &prefix) {
  const QString needle = prefix.trimmed().toLower();
  std::vector<const SlashCommand *> matches;
  for (const SlashCommand &command : catalog) {
    bool hit = needle.isEmpty() || command.name.toLower().startsWith(needle);
    for (const QString &alias : command.aliases) {
      hit = hit || (needle.isEmpty() || alias.toLower().startsWith(needle));
    }
    if (hit) {
      matches.push_back(&command);
    }
  }
  return matches;
}

const SlashCommand *FindSlashCommand(const QString &name) {
  const QString needle = name.trimmed().toLower();
  for (const SlashCommand &command : Catalog()) {
    if (command.name.toLower() == needle) {
      return &command;
    }
    for (const QString &alias : command.aliases) {
      if (alias.toLower() == needle) {
        return &command;
      }
    }
  }
  return nullptr;
}

} // namespace hermes::ui
