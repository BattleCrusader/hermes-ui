// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/slash_commands.h"

#include <QJsonValue>
#include <QRegularExpression>
#include <vector>

namespace hermes::ui {
namespace {

const std::vector<SlashCommand>& Catalog() {
  static const std::vector<SlashCommand> catalog = {
      {QStringLiteral("/new"),
       {QStringLiteral("/reset"), QStringLiteral("/clear")},
       QStringLiteral("Start a new chat"),
       QString(),
       false},
      {QStringLiteral("/stop"),
       {},
       QStringLiteral("Stop the running turn and this chat's processes"),
       QString(),
       false},
      {QStringLiteral("/steer"),
       {QStringLiteral("/s")},
       QStringLiteral("Redirect the running turn"),
       QStringLiteral("text"),
       true},
      {QStringLiteral("/queue"),
       {QStringLiteral("/q")},
       QStringLiteral("Send this after the running turn"),
       QStringLiteral("text"),
       true},
      {QStringLiteral("/bg"),
       {QStringLiteral("/btw")},
       QStringLiteral("Run a background side task"),
       QStringLiteral("[text]"),
       true},
      {QStringLiteral("/reasoning"),
       {},
       QStringLiteral("Set reasoning effort"),
       QStringLiteral("none|minimal|low|medium|high|xhigh|max|ultra"),
       true},
      {QStringLiteral("/model"),
       {},
       QStringLiteral("Switch model"),
       QStringLiteral("[model] [--provider name]|reset"),
       true},
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
       QStringLiteral("Switch light/dark or a theme preset"),
       QStringLiteral("[light|dark|preset]"),
       true},
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

}  // namespace

const std::vector<SlashCommand>& SlashCommandCatalog() { return Catalog(); }

std::vector<const SlashCommand*> MatchSlashCommands(const QString& prefix) {
  return MatchSlashCommands(Catalog(), prefix);
}

std::vector<const SlashCommand*> MatchSlashCommands(
    const std::vector<SlashCommand>& catalog, const QString& prefix) {
  const QString needle = prefix.trimmed().toLower();
  std::vector<const SlashCommand*> matches;
  for (const SlashCommand& command : catalog) {
    bool hit = needle.isEmpty() || command.name.toLower().startsWith(needle);
    for (const QString& alias : command.aliases) {
      hit = hit || (needle.isEmpty() || alias.toLower().startsWith(needle));
    }
    if (hit) {
      matches.push_back(&command);
    }
  }
  return matches;
}

const SlashCommand* FindSlashCommand(const QString& name) {
  const QString needle = name.trimmed().toLower();
  for (const SlashCommand& command : Catalog()) {
    if (command.name.toLower() == needle) {
      return &command;
    }
    for (const QString& alias : command.aliases) {
      if (alias.toLower() == needle) {
        return &command;
      }
    }
  }
  return nullptr;
}

SlashDispatch ParseSlashDispatch(const QJsonObject& result) {
  SlashDispatch dispatch;
  dispatch.output = result.value(QStringLiteral("output")).toString();
  if (dispatch.output.trimmed().isEmpty()) {
    dispatch.output = result.value(QStringLiteral("text")).toString();
  }
  dispatch.warning = result.value(QStringLiteral("warning")).toString();

  const QString type = result.value(QStringLiteral("type")).toString();
  if (type.isEmpty()) {
    if (!dispatch.output.trimmed().isEmpty() ||
        !dispatch.warning.trimmed().isEmpty()) {
      dispatch.kind = SlashDispatch::Kind::kOutput;
    }
    return dispatch;
  }

  dispatch.notice = result.value(QStringLiteral("notice")).toString();
  dispatch.message = result.value(QStringLiteral("message")).toString();
  dispatch.display = result.value(QStringLiteral("display")).toString();
  dispatch.target = result.value(QStringLiteral("target")).toString();
  dispatch.name = result.value(QStringLiteral("name")).toString();

  if (type == QStringLiteral("exec")) {
    dispatch.kind = SlashDispatch::Kind::kExec;
  } else if (type == QStringLiteral("plugin")) {
    dispatch.kind = SlashDispatch::Kind::kPlugin;
  } else if (type == QStringLiteral("alias") &&
             !dispatch.target.trimmed().isEmpty()) {
    dispatch.kind = SlashDispatch::Kind::kAlias;
  } else if (type == QStringLiteral("send")) {
    dispatch.kind = dispatch.message.trimmed().isEmpty()
                        ? SlashDispatch::Kind::kNone
                        : SlashDispatch::Kind::kSend;
  } else if (type == QStringLiteral("skill")) {
    dispatch.kind = SlashDispatch::Kind::kSkill;
  } else if (type == QStringLiteral("prefill")) {
    dispatch.kind = dispatch.message.trimmed().isEmpty()
                        ? SlashDispatch::Kind::kNone
                        : SlashDispatch::Kind::kPrefill;
  }
  return dispatch;
}

bool ShouldFallbackToSlashDispatch(const QString& error) {
  // These are the only refusals that mean "slash.exec does not own this
  // command". Every other 4018 came from a handler slash.exec already
  // forwarded to (retry/undo/compress/queue/bundles); retrying would run a
  // mutating command twice. A helper failure must surface as itself.
  static const QRegularExpression kNotMine(
      QStringLiteral("^skill command: use command\\.dispatch for /|use "
                     "command\\.dispatch for /snapshot restore"));
  if (!kNotMine.match(error).hasMatch()) {
    return false;
  }
  return !error.contains(QStringLiteral("slash worker")) &&
         !error.contains(QStringLiteral("timed out")) &&
         !error.contains(QStringLiteral("not connected")) &&
         !error.contains(QStringLiteral("not running"));
}

}  // namespace hermes::ui
