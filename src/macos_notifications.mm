// Copyright 2026 The Hermes Qt Spike Authors.

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <UserNotifications/UserNotifications.h>

#include <QByteArray>
#include <QString>

namespace {

NSString* ToNSString(const QString& value) {
  const QByteArray utf8 = value.toUtf8();
  return [[NSString alloc] initWithBytes:utf8.constData()
                                  length:static_cast<NSUInteger>(utf8.size())
                                encoding:NSUTF8StringEncoding];
}

}  // namespace

@interface HermesNotificationDelegate : NSObject <NSUserNotificationCenterDelegate>
@end

@implementation HermesNotificationDelegate

- (BOOL)userNotificationCenter:(NSUserNotificationCenter*)center
     shouldPresentNotification:(NSUserNotification*)notification {
  // Hermes suppresses its own focused-window alerts in ShouldDeliver; anything
  // that reaches the OS bridge should actually appear, including foreground
  // approvals for another session.
  return YES;
}

- (void)userNotificationCenter:(NSUserNotificationCenter*)center
       didActivateNotification:(NSUserNotification*)notification {
  [NSApp activateIgnoringOtherApps:YES];
}

@end

bool MacShowNotification(const QString& title, const QString& body,
                         bool silent, QString* error) {
  @autoreleasepool {
    @try {
      NSUserNotificationCenter* center =
          [NSUserNotificationCenter defaultUserNotificationCenter];
      if (center == nil) {
        if (error != nullptr) {
          *error =
              QStringLiteral("macOS did not provide a notification center.");
        }
        return false;
      }

      static HermesNotificationDelegate* delegate =
          [[HermesNotificationDelegate alloc] init];
      [center setDelegate:delegate];

      NSUserNotification* notification = [[NSUserNotification alloc] init];
      notification.title = ToNSString(title);
      notification.informativeText = ToNSString(body);
      if (!silent) {
        notification.soundName = NSUserNotificationDefaultSoundName;
      }
      [center deliverNotification:notification];
      return true;
    } @catch (NSException* exception) {
      if (error != nullptr) {
        *error = QString::fromUtf8([exception.reason UTF8String]);
      }
      return false;
    }
  }
}
