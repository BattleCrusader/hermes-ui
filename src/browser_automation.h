// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_BROWSER_AUTOMATION_H_
#define HERMES_UI_BROWSER_AUTOMATION_H_

#include <QByteArray>
#include <QDateTime>
#include <QDialog>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QPushButton;
class QDoubleSpinBox;
class QWebSocket;

namespace hermes::ui {

// Desktop parity surface for `browser.manage` and the CDP browser behind it.
// The gateway owns discovery/launch and profile scoping; this client attaches
// to the concrete CDP endpoint it returns and performs screenshots plus real
// Chromium input (mouse move / click / drag), never synthetic DOM events.
struct CdpEndpoint {
  // Discovery-style input (http/https or a bare host). Empty for a direct WS
  // target.
  QUrl discovery;
  // Concrete websocket target.
  QUrl websocket;
  // A /devtools/page/<id> websocket is driven directly; a
  // /devtools/browser/<id> websocket needs Target.attachToTarget first.
  bool direct_page = false;
  bool direct_browser = false;

  bool valid() const {
    return (!discovery.isEmpty() && discovery.isValid()) ||
           (!websocket.isEmpty() && websocket.isValid());
  }

  QString Display() const {
    return direct_page || direct_browser ? websocket.toString()
                                         : discovery.toString();
  }
};

std::optional<CdpEndpoint> ParseCdpEndpoint(const QString& input,
                                            QString* error = nullptr);
QString NormalizeCdpEndpoint(const QString& input);

struct BrowserConsentState {
  bool use_real_profile = false;
  bool prompt_dismissed = false;
  bool muted = false;
};

bool BrowserUseRealProfile(const QJsonObject& config);
bool ShouldOfferBrowserConsent(const BrowserConsentState& state);
QJsonObject BuildBrowserConsentPatch(bool enabled);

QJsonObject BuildBrowserManageParams(const QString& action, const QString& url,
                                     const QString& profile = QString());
bool BrowserManageConnected(const QJsonObject& result);
QString BrowserManageUrl(const QJsonObject& result);
QString BrowserManageMessages(const QJsonObject& result);

// Chromium's Input.dispatchMouseEvent wants one event per phase. Keeping the
// builders pure makes coordinate dispatch testable without a browser.
QJsonObject BuildBrowserMouseEvent(const QString& type, double x, double y,
                                   const QString& button, int buttons,
                                   int click_count);
std::vector<QJsonObject> BuildBrowserClickEvents(double x, double y,
                                                 const QString& button,
                                                 int click_count);
std::vector<QJsonObject> BuildBrowserDragEvents(double x1, double y1, double x2,
                                                double y2,
                                                const QString& button);

QString BrowserScreenshotName(const QDateTime& now);
bool DecodeBrowserScreenshot(const QJsonObject& result, QByteArray* png,
                             QString* error);
bool SaveBrowserScreenshot(const QByteArray& png, const QString& path,
                           QString* error);

using CdpCallback = std::function<void(const QJsonObject&, const QString&)>;
using CdpScreenshotCallback = std::function<void(
    const QByteArray&, const QString&)>;  // NOLINT(whitespace/indent_namespace)

// Minimal Chrome DevTools Protocol client: HTTP discovery, websocket framing,
// request/response ids, optional flat-session attach for browser targets, and
// the Page/Input commands needed by the automation dialog.
class CdpClient : public QObject {
  Q_OBJECT

 public:
  explicit CdpClient(QObject* parent = nullptr);
  ~CdpClient() override;

  void Connect(const QString& endpoint);
  void Disconnect();
  bool IsConnected() const { return connected_; }

  int SendCommand(const QString& method, const QJsonObject& params,
                  CdpCallback done = {});
  void Navigate(const QString& url, CdpCallback done = {});
  void CaptureScreenshot(CdpScreenshotCallback done);
  void CaptureScreenshotTo(const QString& path, CdpCallback done = {});
  void MoveMouse(double x, double y, CdpCallback done = {});
  void Click(double x, double y, const QString& button, int click_count,
             CdpCallback done = {});
  void Drag(double x1, double y1, double x2, double y2, const QString& button,
            CdpCallback done = {});
  void DispatchMouseEvents(const std::vector<QJsonObject>& events,
                           CdpCallback done = {});

 signals:
  void StatusChanged(const QString& status);
  void Connected(const QString& websocket_url);
  void Failed(const QString& error);
  void Disconnected();

 private:
  void Discover(const QUrl& discovery);
  void OpenSocket(const QUrl& websocket, bool browser_target);
  void AttachToPage();
  void FinishConnect();
  void Fail(const QString& error);
  int SendRaw(const QString& method, const QJsonObject& params,
              CdpCallback done);
  void DispatchSequence(const std::vector<QJsonObject>& events,
                        CdpCallback done);
  void DispatchSequenceAt(std::shared_ptr<std::vector<QJsonObject>> events,
                          std::size_t index, CdpCallback done);
  void OnTextMessage(const QString& message);
  void OnSocketConnected();
  void OnSocketDisconnected();

  QNetworkAccessManager* network_ = nullptr;
  QWebSocket* websocket_ = nullptr;
  QUrl websocket_url_;
  QString session_id_;
  QHash<int, CdpCallback> pending_;
  int next_id_ = 1;
  bool connected_ = false;
  bool connecting_ = false;
  bool browser_target_ = false;
};

class BrowserConsentDialog : public QDialog {
  Q_OBJECT

 public:
  explicit BrowserConsentDialog(QWidget* parent = nullptr);
  // True when the user asked not to see this offer again.
  bool dont_ask_again() const;
};

class BrowserAutomationDialog : public QDialog {
  Q_OBJECT

 public:
  struct Hooks {
    // Calls the gateway's `browser.manage` action. `done` receives the result
    // object and an error string (empty on success).
    std::function<void(const QString& action, const QString& url,
                       CdpCallback done)>
        manage;
    // Receives the decoded PNG. The shell saves it on a worker thread.
    std::function<void(const QByteArray& png, const QString& suggested_name)>
        save_screenshot;
  };

  explicit BrowserAutomationDialog(Hooks hooks, QWidget* parent = nullptr);
  ~BrowserAutomationDialog() override;

  QString Endpoint() const;
  void SetEndpoint(const QString& endpoint);
  void SetStatus(const QString& text, bool error = false);
  void ConnectNow();
  CdpClient* cdp() { return cdp_; }

 private:
  void BuildUi();
  void ConnectToBrowser();
  void CheckStatus();
  void DisconnectFromBrowser();
  void NavigatePage();
  void TakeScreenshot();
  void ActivateCoordinate(const QString& button, int click_count);
  void DragCoordinates();
  void MoveCoordinate();
  void UpdateConnectionUi(bool connected);
  void Manage(const QString& action, const QString& url);
  void SendCoordinateEvents(const std::vector<QJsonObject>& events);

  Hooks hooks_;
  CdpClient* cdp_ = nullptr;
  QLineEdit* endpoint_ = nullptr;
  QLineEdit* page_url_ = nullptr;
  QLabel* status_ = nullptr;
  QLabel* screenshot_hint_ = nullptr;
  QPushButton* connect_ = nullptr;
  QPushButton* check_ = nullptr;
  QPushButton* disconnect_ = nullptr;
  QPushButton* navigate_ = nullptr;
  QPushButton* screenshot_ = nullptr;
  QPushButton* click_ = nullptr;
  QPushButton* double_click_ = nullptr;
  QPushButton* right_click_ = nullptr;
  QPushButton* move_ = nullptr;
  QPushButton* drag_ = nullptr;
  QPushButton* close_ = nullptr;
  QDoubleSpinBox* x_ = nullptr;
  QDoubleSpinBox* y_ = nullptr;
  QDoubleSpinBox* x2_ = nullptr;
  QDoubleSpinBox* y2_ = nullptr;
  bool connected_ = false;
};

}  // namespace hermes::ui

#endif  // HERMES_UI_BROWSER_AUTOMATION_H_
