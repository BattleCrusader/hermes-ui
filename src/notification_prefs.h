// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_NOTIFICATION_PREFS_H_
#define HERMES_UI_NOTIFICATION_PREFS_H_

#include <QHash>
#include <QString>
#include <array>

namespace hermes::ui {

// The desktop's seven native-notification kinds. Preferences are renderer
// local, exactly like the desktop's localStorage-backed store, so they live in
// NativePref rather than the gateway config.
enum class NotificationKind {
  kApproval,
  kInput,
  kTurnDone,
  kTurnError,
  kBackgroundDone,
  kCredits,
  kPlugin,
};

inline constexpr std::array<NotificationKind, 7> kNotificationKinds = {
    NotificationKind::kApproval,       NotificationKind::kInput,
    NotificationKind::kTurnDone,       NotificationKind::kTurnError,
    NotificationKind::kBackgroundDone, NotificationKind::kCredits,
    NotificationKind::kPlugin,
};

struct NotificationPrefs {
  bool enabled = true;
  std::array<bool, kNotificationKinds.size()> kinds = {true, true, true, true,
                                                       true, true, true};
  bool mute_sounds = false;
};

QString NotificationKindId(NotificationKind kind);
QString NotificationKindLabel(NotificationKind kind);
QString NotificationKindDescription(NotificationKind kind);
QString NotificationKindPrefKey(NotificationKind kind);
bool IsAttentionNotification(NotificationKind kind);
std::size_t NotificationKindIndex(NotificationKind kind);

// Reads the window's local notification preferences. Missing keys are the
// desktop defaults: enabled, every kind on, sounds unmuted.
NotificationPrefs LoadNotificationPrefs();

// Delivery gate from the desktop's native-notifications store. Completion
// kinds only fire for the selected session while Hermes is backgrounded;
// blocking prompts also fire for another session while the window is focused.
// Global notifications (credits/plugin work) have no session and only need the
// window to be backgrounded.
bool ShouldDeliverNotification(NotificationKind kind,
                               const NotificationPrefs &prefs,
                               bool backgrounded, bool session_is_active,
                               bool global);

// Desktop de-dupe: the same kind+session may fire at most once per second, so
// replayed gateway events cannot stack duplicate OS alerts. Entries older than
// the window are pruned on every check, keeping the map bounded.
class NotificationThrottle {
public:
  bool Allow(const QString &key, qint64 now_ms);

private:
  QHash<QString, qint64> last_fired_;
};

} // namespace hermes::ui

#endif // HERMES_UI_NOTIFICATION_PREFS_H_
