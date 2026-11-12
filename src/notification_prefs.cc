// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/notification_prefs.h"

#include "src/native_prefs.h"

namespace hermes::ui {
namespace {

bool PrefOn(const QString &key, bool fallback) {
  const QString value = NativePref(key);
  if (value.isEmpty()) {
    return fallback;
  }
  return value == QStringLiteral("1") || value == QStringLiteral("true");
}

} // namespace

QString NotificationKindId(NotificationKind kind) {
  switch (kind) {
  case NotificationKind::kApproval:
    return QStringLiteral("approval");
  case NotificationKind::kInput:
    return QStringLiteral("input");
  case NotificationKind::kTurnDone:
    return QStringLiteral("turnDone");
  case NotificationKind::kTurnError:
    return QStringLiteral("turnError");
  case NotificationKind::kBackgroundDone:
    return QStringLiteral("backgroundDone");
  case NotificationKind::kCredits:
    return QStringLiteral("credits");
  case NotificationKind::kPlugin:
    return QStringLiteral("plugin");
  }
  return QStringLiteral("turnDone");
}

QString NotificationKindLabel(NotificationKind kind) {
  switch (kind) {
  case NotificationKind::kApproval:
    return QStringLiteral("Approval requests");
  case NotificationKind::kInput:
    return QStringLiteral("Clarify and input requests");
  case NotificationKind::kTurnDone:
    return QStringLiteral("Turn finished");
  case NotificationKind::kTurnError:
    return QStringLiteral("Turn failed");
  case NotificationKind::kBackgroundDone:
    return QStringLiteral("Background work finished");
  case NotificationKind::kCredits:
    return QStringLiteral("Credits and billing");
  case NotificationKind::kPlugin:
    return QStringLiteral("Plugin events");
  }
  return QString();
}

QString NotificationKindDescription(NotificationKind kind) {
  switch (kind) {
  case NotificationKind::kApproval:
    return QStringLiteral(
        "Show the session name when an approval blocks another chat.");
  case NotificationKind::kInput:
    return QStringLiteral(
        "Clarify, sudo and secret prompts that need an answer.");
  case NotificationKind::kTurnDone:
    return QStringLiteral(
        "Only for the selected session, and only while Hermes is in the "
        "background.");
  case NotificationKind::kTurnError:
    return QStringLiteral(
        "A selected-session turn ended with an error while Hermes was in "
        "the background.");
  case NotificationKind::kBackgroundDone:
    return QStringLiteral("Background research, cron or delegated work.");
  case NotificationKind::kCredits:
    return QStringLiteral("Low balance and automatic top-up events.");
  case NotificationKind::kPlugin:
    return QStringLiteral("Plugin and external-surface events.");
  }
  return QString();
}

QString NotificationKindPrefKey(NotificationKind kind) {
  return QStringLiteral("notifications.kind.") + NotificationKindId(kind);
}

bool IsAttentionNotification(NotificationKind kind) {
  return kind == NotificationKind::kApproval ||
         kind == NotificationKind::kInput;
}

std::size_t NotificationKindIndex(NotificationKind kind) {
  for (std::size_t i = 0; i < kNotificationKinds.size(); ++i) {
    if (kNotificationKinds[i] == kind) {
      return i;
    }
  }
  return 0;
}

NotificationPrefs LoadNotificationPrefs() {
  NotificationPrefs prefs;
  prefs.enabled = PrefOn(QStringLiteral("notifications.enabled"), true);
  prefs.mute_sounds =
      PrefOn(QStringLiteral("notifications.mute_sounds"), false);
  for (NotificationKind kind : kNotificationKinds) {
    prefs.kinds[NotificationKindIndex(kind)] =
        PrefOn(NotificationKindPrefKey(kind), true);
  }
  return prefs;
}

bool NotificationThrottle::Allow(const QString &key, qint64 now_ms) {
  constexpr qint64 kWindowMs = 1000;
  for (auto it = last_fired_.begin(); it != last_fired_.end();) {
    if (now_ms - it.value() >= kWindowMs || now_ms < it.value()) {
      it = last_fired_.erase(it);
    } else {
      ++it;
    }
  }
  if (last_fired_.contains(key)) {
    return false;
  }
  last_fired_.insert(key, now_ms);
  return true;
}

bool ShouldDeliverNotification(NotificationKind kind,
                               const NotificationPrefs &prefs,
                               bool backgrounded, bool session_is_active,
                               bool global) {
  if (!prefs.enabled || !prefs.kinds[NotificationKindIndex(kind)]) {
    return false;
  }
  if (global) {
    return backgrounded;
  }
  if (IsAttentionNotification(kind)) {
    return backgrounded || !session_is_active;
  }
  return backgrounded && session_is_active;
}

} // namespace hermes::ui
