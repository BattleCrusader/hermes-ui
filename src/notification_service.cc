// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/notification_service.h"

#include <QSystemTrayIcon>

#if defined(__APPLE__)
bool MacShowNotification(const QString &title, const QString &body, bool silent,
                         QString *error);
#else
#include <QApplication>
#include <QIcon>
#endif

namespace hermes::ui {

NotificationService::NotificationService(QObject *parent) { Q_UNUSED(parent); }

NotificationService::~NotificationService() { delete tray_; }

bool NotificationService::Show(const QString &title, const QString &body,
                               bool silent, QString *error) {
#if defined(__APPLE__)
  return MacShowNotification(title, body, silent, error);
#else
  if (!QSystemTrayIcon::isSystemTrayAvailable()) {
    if (error != nullptr) {
      *error = QStringLiteral(
          "This desktop has no system tray, so notifications are "
          "unavailable.");
    }
    return false;
  }
  if (tray_ == nullptr) {
    tray_ = new QSystemTrayIcon(QApplication::windowIcon());
    tray_->setToolTip(QStringLiteral("Hermes"));
    tray_->show();
  }
  tray_->showMessage(title, body, QSystemTrayIcon::Information, 6000);
  return true;
#endif
}

} // namespace hermes::ui
