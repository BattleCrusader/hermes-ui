// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_NOTIFICATION_SERVICE_H_
#define HERMES_UI_NOTIFICATION_SERVICE_H_

#include <QString>

class QSystemTrayIcon;

namespace hermes::ui {

// Thin platform bridge for native OS notifications. The macOS build uses
// NSUserNotification from the app bundle; the bundle identity is what lets
// macOS register Hermes and deliver alerts. Other platforms use a tray balloon.
// The shell owns the policy; this class only delivers or reports why it could
// not.
class NotificationService {
 public:
  explicit NotificationService(QObject* parent = nullptr);
  ~NotificationService();

  NotificationService(const NotificationService&) = delete;
  NotificationService& operator=(const NotificationService&) = delete;

  bool Show(const QString& title, const QString& body, bool silent,
            QString* error);

 private:
  QSystemTrayIcon* tray_ = nullptr;
};

}  // namespace hermes::ui

#endif  // HERMES_UI_NOTIFICATION_SERVICE_H_
