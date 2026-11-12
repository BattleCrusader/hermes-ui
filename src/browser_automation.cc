// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/browser_automation.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFont>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <QWebSocket>
#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace hermes::ui {
namespace {

constexpr int kCdpCommandTimeoutMs = 15000;
constexpr char kDefaultLocalCdp[] = "http://127.0.0.1:9222";

QString HttpSchemeFor(const QString& scheme) {
  return scheme == QStringLiteral("wss") || scheme == QStringLiteral("https")
             ? QStringLiteral("https")
             : QStringLiteral("http");
}

QUrl BuildDiscoveryUrl(const QUrl& input) {
  QUrl discovery;
  discovery.setScheme(HttpSchemeFor(input.scheme()));
  discovery.setHost(input.host());
  if (input.port() != -1) {
    discovery.setPort(input.port());
  }
  return discovery;
}

QString FirstPageWebsocket(const QJsonArray& targets) {
  for (const QJsonValue& value : targets) {
    const QJsonObject target = value.toObject();
    const QString type = target.value(QStringLiteral("type")).toString();
    const QString websocket =
        target.value(QStringLiteral("webSocketDebuggerUrl")).toString();
    if ((type.isEmpty() || type == QStringLiteral("page")) &&
        !websocket.isEmpty()) {
      return websocket;
    }
  }
  return QString();
}

QString FirstPageTargetId(const QJsonArray& targets) {
  for (const QJsonValue& value : targets) {
    const QJsonObject target = value.toObject();
    const QString type = target.value(QStringLiteral("type")).toString();
    const QString id = target.value(QStringLiteral("targetId")).toString();
    if (type == QStringLiteral("page") && !id.isEmpty()) {
      return id;
    }
  }
  for (const QJsonValue& value : targets) {
    const QJsonObject target = value.toObject();
    const QString id = target.value(QStringLiteral("targetId")).toString();
    if (!id.isEmpty()) {
      return id;
    }
  }
  return QString();
}

int BrowserButtonMask(const QString& button) {
  if (button == QStringLiteral("right")) {
    return 2;
  }
  if (button == QStringLiteral("middle")) {
    return 4;
  }
  return 1;
}

QString BrowserButton(const QString& button) {
  return button.isEmpty() ? QStringLiteral("left") : button;
}

}  // namespace

std::optional<CdpEndpoint> ParseCdpEndpoint(const QString& input,
                                            QString* error) {
  QString text = input.trimmed();
  if (text.isEmpty()) {
    text = QString::fromLatin1(kDefaultLocalCdp);
  }
  if (!text.contains(QStringLiteral("://"))) {
    text.prepend(QStringLiteral("http://"));
  }

  const QUrl url(text);
  const QString scheme = url.scheme().toLower();
  if (!url.isValid() || url.host().isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("invalid CDP endpoint: %1").arg(text);
    }
    return std::nullopt;
  }

  CdpEndpoint endpoint;
  if (scheme == QStringLiteral("http") || scheme == QStringLiteral("https")) {
    endpoint.discovery = BuildDiscoveryUrl(url);
    return endpoint;
  }
  if (scheme != QStringLiteral("ws") && scheme != QStringLiteral("wss")) {
    if (error != nullptr) {
      *error = QStringLiteral("unsupported CDP scheme: %1").arg(scheme);
    }
    return std::nullopt;
  }

  const QString path = url.path();
  if (path.startsWith(QStringLiteral("/devtools/page/"))) {
    endpoint.websocket = url;
    endpoint.direct_page = true;
    return endpoint;
  }
  if (path.startsWith(QStringLiteral("/devtools/browser/"))) {
    endpoint.websocket = url;
    endpoint.direct_browser = true;
    return endpoint;
  }
  if (path.isEmpty() || path == QStringLiteral("/") ||
      path == QStringLiteral("/json") ||
      path == QStringLiteral("/json/version") ||
      path == QStringLiteral("/json/list")) {
    endpoint.discovery = BuildDiscoveryUrl(url);
    return endpoint;
  }

  if (error != nullptr) {
    *error = QStringLiteral("unsupported CDP websocket path: %1").arg(path);
  }
  return std::nullopt;
}

QString NormalizeCdpEndpoint(const QString& input) {
  const auto parsed = ParseCdpEndpoint(input);
  return parsed ? parsed->Display() : input.trimmed();
}

bool BrowserUseRealProfile(const QJsonObject& config) {
  QJsonValue current = config;
  for (const QString& part :
       {QStringLiteral("browser"), QStringLiteral("use_real_profile")}) {
    if (!current.isObject()) {
      return false;
    }
    current = current.toObject().value(part);
  }
  return current.toBool(false);
}

bool ShouldOfferBrowserConsent(const BrowserConsentState& state) {
  return !state.use_real_profile && !state.prompt_dismissed && !state.muted;
}

QJsonObject BuildBrowserConsentPatch(bool enabled) {
  return QJsonObject{
      {QStringLiteral("browser"),
       QJsonObject{{QStringLiteral("use_real_profile"), enabled}}}};
}

QJsonObject BuildBrowserManageParams(const QString& action, const QString& url,
                                     const QString& profile) {
  QJsonObject params{{QStringLiteral("action"),
                      action.isEmpty() ? QStringLiteral("status") : action}};
  if (!url.trimmed().isEmpty()) {
    params.insert(QStringLiteral("url"), url.trimmed());
  }
  if (!profile.isEmpty()) {
    params.insert(QStringLiteral("profile"), profile);
  }
  return params;
}

bool BrowserManageConnected(const QJsonObject& result) {
  return result.value(QStringLiteral("connected")).toBool(false);
}

QString BrowserManageUrl(const QJsonObject& result) {
  return result.value(QStringLiteral("url")).toString();
}

QString BrowserManageMessages(const QJsonObject& result) {
  QStringList lines;
  for (const QJsonValue& value :
       result.value(QStringLiteral("messages")).toArray()) {
    const QString line = value.toString().trimmed();
    if (!line.isEmpty()) {
      lines.append(line);
    }
  }
  return lines.join(QLatin1Char('\n'));
}

QJsonObject BuildBrowserMouseEvent(const QString& type, double x, double y,
                                   const QString& button, int buttons,
                                   int click_count) {
  return QJsonObject{{QStringLiteral("type"), type},
                     {QStringLiteral("x"), x},
                     {QStringLiteral("y"), y},
                     {QStringLiteral("button"),
                      button.isEmpty() ? QStringLiteral("none") : button},
                     {QStringLiteral("buttons"), buttons},
                     {QStringLiteral("clickCount"), click_count}};
}

std::vector<QJsonObject> BuildBrowserClickEvents(double x, double y,
                                                 const QString& button,
                                                 int click_count) {
  const QString resolved = BrowserButton(button);
  const int mask = BrowserButtonMask(resolved);
  const int count = std::max(1, click_count);
  return {BuildBrowserMouseEvent(QStringLiteral("mouseMoved"), x, y,
                                 QStringLiteral("none"), 0, 0),
          BuildBrowserMouseEvent(QStringLiteral("mousePressed"), x, y, resolved,
                                 mask, count),
          BuildBrowserMouseEvent(QStringLiteral("mouseReleased"), x, y,
                                 resolved, 0, count)};
}

std::vector<QJsonObject> BuildBrowserDragEvents(double x1, double y1, double x2,
                                                double y2,
                                                const QString& button) {
  const QString resolved = BrowserButton(button);
  const int mask = BrowserButtonMask(resolved);
  return {BuildBrowserMouseEvent(QStringLiteral("mouseMoved"), x1, y1,
                                 QStringLiteral("none"), 0, 0),
          BuildBrowserMouseEvent(QStringLiteral("mousePressed"), x1, y1,
                                 resolved, mask, 1),
          BuildBrowserMouseEvent(QStringLiteral("mouseMoved"), x2, y2, resolved,
                                 mask, 1),
          BuildBrowserMouseEvent(QStringLiteral("mouseReleased"), x2, y2,
                                 resolved, 0, 1)};
}

QString BrowserScreenshotName(const QDateTime& now) {
  return QStringLiteral("hermes-browser-%1.png")
      .arg(now.toString(QStringLiteral("yyyyMMdd-HHmmss")));
}

bool DecodeBrowserScreenshot(const QJsonObject& result, QByteArray* png,
                             QString* error) {
  const QString data = result.value(QStringLiteral("data")).toString();
  if (data.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("CDP screenshot returned no image data");
    }
    return false;
  }
  const QByteArray decoded = QByteArray::fromBase64(data.toUtf8());
  if (decoded.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("CDP screenshot data is not valid base64");
    }
    return false;
  }
  *png = decoded;
  return true;
}

bool SaveBrowserScreenshot(const QByteArray& png, const QString& path,
                           QString* error) {
  if (png.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("screenshot is empty");
    }
    return false;
  }
  const QFileInfo info(path);
  if (!info.absolutePath().isEmpty() && !QDir().mkpath(info.absolutePath())) {
    if (error != nullptr) {
      *error = QStringLiteral("could not create %1").arg(info.absolutePath());
    }
    return false;
  }
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    if (error != nullptr) {
      *error = file.errorString();
    }
    return false;
  }
  if (file.write(png) != png.size()) {
    if (error != nullptr) {
      *error = file.errorString();
    }
    file.cancelWriting();
    return false;
  }
  if (!file.commit()) {
    if (error != nullptr) {
      *error = file.errorString();
    }
    return false;
  }
  return true;
}

CdpClient::CdpClient(QObject* parent) : QObject(parent) {
  network_ = new QNetworkAccessManager(this);
  websocket_ =
      new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
  connect(websocket_, &QWebSocket::connected, this,
          &CdpClient::OnSocketConnected);
  connect(websocket_, &QWebSocket::disconnected, this,
          &CdpClient::OnSocketDisconnected);
  connect(websocket_, &QWebSocket::textMessageReceived, this,
          &CdpClient::OnTextMessage);
  connect(websocket_, &QWebSocket::errorOccurred, this,
          [this](QAbstractSocket::SocketError) {
            if (websocket_ != nullptr) {
              Fail(websocket_->errorString());
            }
          });
}

CdpClient::~CdpClient() { Disconnect(); }

void CdpClient::Connect(const QString& endpoint) {
  Disconnect();
  QString error;
  const auto parsed = ParseCdpEndpoint(endpoint, &error);
  if (!parsed) {
    Fail(error);
    return;
  }
  connecting_ = true;
  emit StatusChanged(QStringLiteral("Connecting to CDP browser…"));
  if (parsed->direct_page || parsed->direct_browser) {
    OpenSocket(parsed->websocket, parsed->direct_browser);
    return;
  }
  Discover(parsed->discovery);
}

void CdpClient::Discover(const QUrl& discovery) {
  QUrl list_url = discovery;
  list_url.setPath(QStringLiteral("/json/list"));
  QNetworkRequest request(list_url);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  QNetworkReply* reply = network_->get(request);
  connect(reply, &QNetworkReply::finished, this, [this, reply, discovery]() {
    const QByteArray body = reply->readAll();
    const bool ok = reply->error() == QNetworkReply::NoError;
    reply->deleteLater();
    if (ok) {
      const QJsonDocument document = QJsonDocument::fromJson(body);
      if (document.isArray()) {
        const QString websocket = FirstPageWebsocket(document.array());
        if (!websocket.isEmpty()) {
          OpenSocket(QUrl(websocket), false);
          return;
        }
      }
    }

    QUrl version_url = discovery;
    version_url.setPath(QStringLiteral("/json/version"));
    QNetworkRequest version_request(version_url);
    version_request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                 QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* version_reply = network_->get(version_request);
    connect(
        version_reply, &QNetworkReply::finished, this, [this, version_reply]() {
          const QByteArray version_body = version_reply->readAll();
          const bool version_ok =
              version_reply->error() == QNetworkReply::NoError;
          const QString version_error = version_reply->errorString();
          version_reply->deleteLater();
          if (!version_ok) {
            Fail(QStringLiteral("could not reach browser CDP: %1")
                     .arg(version_error));
            return;
          }
          const QJsonDocument document = QJsonDocument::fromJson(version_body);
          const QString websocket =
              document.object()
                  .value(QStringLiteral("webSocketDebuggerUrl"))
                  .toString();
          if (websocket.isEmpty()) {
            Fail(QStringLiteral("CDP discovery returned no browser websocket"));
            return;
          }
          OpenSocket(QUrl(websocket), true);
        });
  });
}

void CdpClient::OpenSocket(const QUrl& websocket, bool browser_target) {
  websocket_url_ = websocket;
  browser_target_ = browser_target;
  connecting_ = true;
  websocket_->open(websocket);
}

void CdpClient::AttachToPage() {
  emit StatusChanged(QStringLiteral("Attaching to a CDP page…"));
  SendRaw(
      QStringLiteral("Target.getTargets"), QJsonObject(),
      [this](const QJsonObject& result, const QString& error) {
        if (!error.isEmpty()) {
          Fail(error);
          return;
        }
        const QString target = FirstPageTargetId(
            result.value(QStringLiteral("targetInfos")).toArray());
        if (target.isEmpty()) {
          Fail(QStringLiteral("CDP browser reported no page target"));
          return;
        }
        SendRaw(
            QStringLiteral("Target.attachToTarget"),
            QJsonObject{{QStringLiteral("targetId"), target},
                        {QStringLiteral("flatten"), true}},
            [this](const QJsonObject& attached, const QString& attach_error) {
              if (!attach_error.isEmpty()) {
                Fail(attach_error);
                return;
              }
              session_id_ =
                  attached.value(QStringLiteral("sessionId")).toString();
              if (session_id_.isEmpty()) {
                Fail(QStringLiteral(
                    "CDP browser attached without a session id"));
                return;
              }
              FinishConnect();
            });
      });
}

void CdpClient::FinishConnect() {
  connecting_ = false;
  connected_ = true;
  emit StatusChanged(QStringLiteral("Connected to CDP browser"));
  emit Connected(websocket_url_.toString());
}

void CdpClient::Fail(const QString& error) {
  const QString message =
      error.isEmpty() ? QStringLiteral("CDP connection failed") : error;
  connecting_ = false;
  connected_ = false;
  const auto callbacks = pending_;
  pending_.clear();
  for (const auto& callback : callbacks) {
    if (callback) {
      callback(QJsonObject(), message);
    }
  }
  emit StatusChanged(message);
  emit Failed(message);
  if (websocket_ != nullptr) {
    websocket_->abort();
  }
  emit Disconnected();
}

void CdpClient::Disconnect() {
  const bool was_live = connected_ || connecting_;
  connected_ = false;
  connecting_ = false;
  browser_target_ = false;
  session_id_.clear();
  const auto callbacks = pending_;
  pending_.clear();
  for (const auto& callback : callbacks) {
    if (callback) {
      callback(QJsonObject(), QStringLiteral("CDP connection closed"));
    }
  }
  if (websocket_ != nullptr) {
    websocket_->abort();
  }
  if (was_live) {
    emit StatusChanged(QStringLiteral("CDP connection closed"));
    emit Disconnected();
  }
}

int CdpClient::SendRaw(const QString& method, const QJsonObject& params,
                       CdpCallback done) {
  if (websocket_ == nullptr ||
      websocket_->state() != QAbstractSocket::ConnectedState) {
    if (done) {
      done(QJsonObject(), QStringLiteral("CDP websocket is not connected"));
    }
    return -1;
  }
  const int id = next_id_++;
  QJsonObject frame{{QStringLiteral("id"), id},
                    {QStringLiteral("method"), method},
                    {QStringLiteral("params"), params}};
  if (!session_id_.isEmpty()) {
    frame.insert(QStringLiteral("sessionId"), session_id_);
  }
  pending_.insert(id, std::move(done));
  websocket_->sendTextMessage(
      QString::fromUtf8(QJsonDocument(frame).toJson(QJsonDocument::Compact)));
  QTimer::singleShot(kCdpCommandTimeoutMs, this, [this, id]() {
    const auto it = pending_.find(id);
    if (it == pending_.end()) {
      return;
    }
    const CdpCallback callback = it.value();
    pending_.erase(it);
    if (callback) {
      callback(QJsonObject(), QStringLiteral("CDP command timed out"));
    }
  });
  return id;
}

int CdpClient::SendCommand(const QString& method, const QJsonObject& params,
                           CdpCallback done) {
  if (!connected_) {
    if (done) {
      done(QJsonObject(), QStringLiteral("CDP browser is not connected"));
    }
    return -1;
  }
  return SendRaw(method, params, std::move(done));
}

void CdpClient::Navigate(const QString& url, CdpCallback done) {
  if (url.trimmed().isEmpty()) {
    if (done) {
      done(QJsonObject(), QStringLiteral("browser URL is empty"));
    }
    return;
  }
  SendCommand(QStringLiteral("Page.navigate"),
              QJsonObject{{QStringLiteral("url"), url.trimmed()}},
              std::move(done));
}

void CdpClient::CaptureScreenshot(CdpScreenshotCallback done) {
  SendCommand(QStringLiteral("Page.captureScreenshot"),
              QJsonObject{{QStringLiteral("format"), QStringLiteral("png")},
                          {QStringLiteral("fromSurface"), true}},
              [done = std::move(done)](const QJsonObject& result,
                                       const QString& error) {
                if (!error.isEmpty()) {
                  if (done) {
                    done(QByteArray(), error);
                  }
                  return;
                }
                QByteArray png;
                QString decode_error;
                if (!DecodeBrowserScreenshot(result, &png, &decode_error)) {
                  if (done) {
                    done(QByteArray(), decode_error);
                  }
                  return;
                }
                if (done) {
                  done(png, QString());
                }
              });
}

void CdpClient::CaptureScreenshotTo(const QString& path, CdpCallback done) {
  CaptureScreenshot([path, done = std::move(done)](const QByteArray& png,
                                                   const QString& error) {
    if (!error.isEmpty()) {
      if (done) {
        done(QJsonObject(), error);
      }
      return;
    }
    QString save_error;
    if (!SaveBrowserScreenshot(png, path, &save_error)) {
      if (done) {
        done(QJsonObject(), save_error);
      }
      return;
    }
    if (done) {
      done(QJsonObject{{QStringLiteral("path"), path},
                       {QStringLiteral("bytes"), png.size()}},
           QString());
    }
  });
}

void CdpClient::MoveMouse(double x, double y, CdpCallback done) {
  DispatchMouseEvents({BuildBrowserMouseEvent(QStringLiteral("mouseMoved"), x,
                                              y, QStringLiteral("none"), 0, 0)},
                      std::move(done));
}

void CdpClient::Click(double x, double y, const QString& button,
                      int click_count, CdpCallback done) {
  DispatchMouseEvents(BuildBrowserClickEvents(x, y, button, click_count),
                      std::move(done));
}

void CdpClient::Drag(double x1, double y1, double x2, double y2,
                     const QString& button, CdpCallback done) {
  DispatchMouseEvents(BuildBrowserDragEvents(x1, y1, x2, y2, button),
                      std::move(done));
}

void CdpClient::DispatchMouseEvents(const std::vector<QJsonObject>& events,
                                    CdpCallback done) {
  DispatchSequence(events, std::move(done));
}

void CdpClient::DispatchSequence(const std::vector<QJsonObject>& events,
                                 CdpCallback done) {
  if (events.empty()) {
    if (done) {
      done(QJsonObject(), QStringLiteral("no input events to dispatch"));
    }
    return;
  }
  DispatchSequenceAt(std::make_shared<std::vector<QJsonObject>>(events), 0,
                     std::move(done));
}

void CdpClient::DispatchSequenceAt(
    std::shared_ptr<std::vector<QJsonObject>> events, std::size_t index,
    CdpCallback done) {
  if (index >= events->size()) {
    if (done) {
      done(QJsonObject(), QString());
    }
    return;
  }
  SendCommand(QStringLiteral("Input.dispatchMouseEvent"), events->at(index),
              [this, events, index, done = std::move(done)](
                  const QJsonObject& result, const QString& error) {
                if (!error.isEmpty()) {
                  if (done) {
                    done(result, error);
                  }
                  return;
                }
                DispatchSequenceAt(events, index + 1, done);
              });
}

void CdpClient::OnSocketDisconnected() {
  const bool was_live = connected_ || connecting_;
  connected_ = false;
  connecting_ = false;
  const auto callbacks = pending_;
  pending_.clear();
  for (const auto& callback : callbacks) {
    if (callback) {
      callback(QJsonObject(), QStringLiteral("CDP connection closed"));
    }
  }
  if (was_live) {
    emit StatusChanged(QStringLiteral("CDP connection closed"));
    emit Disconnected();
  }
}

void CdpClient::OnTextMessage(const QString& message) {
  const QJsonDocument document = QJsonDocument::fromJson(message.toUtf8());
  if (!document.isObject()) {
    return;
  }
  const QJsonObject object = document.object();
  const QJsonValue id_value = object.value(QStringLiteral("id"));
  if (!id_value.isDouble()) {
    return;
  }
  const int id = id_value.toInt();
  const auto it = pending_.find(id);
  if (it == pending_.end()) {
    return;
  }
  const CdpCallback callback = it.value();
  pending_.erase(it);
  if (!callback) {
    return;
  }
  const QJsonValue error_value = object.value(QStringLiteral("error"));
  if (error_value.isObject()) {
    QString error =
        error_value.toObject().value(QStringLiteral("message")).toString();
    if (error.isEmpty()) {
      error = QStringLiteral("CDP command failed");
    }
    callback(QJsonObject(), error);
    return;
  }
  callback(object.value(QStringLiteral("result")).toObject(), QString());
}

void CdpClient::OnSocketConnected() {
  if (browser_target_) {
    AttachToPage();
    return;
  }
  FinishConnect();
}

BrowserConsentDialog::BrowserConsentDialog(QWidget* parent) : QDialog(parent) {
  setObjectName(QStringLiteral("browserRealProfileConsent"));
  setWindowTitle(QStringLiteral("Use your real browser profile?"));
  setModal(true);

  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(20, 18, 20, 16);
  outer->setSpacing(10);

  auto* title =
      new QLabel(QStringLiteral("Use your real browser profile?"), this);
  title->setObjectName(QStringLiteral("browserConsentTitle"));
  QFont title_font = title->font();
  title_font.setBold(true);
  title_font.setPointSizeF(title_font.pointSizeF() + 1.0);
  title->setFont(title_font);
  outer->addWidget(title);

  auto* body = new QLabel(
      QStringLiteral(
          "Browser automation attaches to a Chromium-family browser over "
          "the Chrome DevTools Protocol. With the real profile enabled, the "
          "agent can reuse the browser session you are already signed in "
          "to, including its cookies and open tabs.\n\n"
          "Only connect a browser you trust. You can disconnect at any time, "
          "and the Browser settings page keeps this choice available."),
      this);
  body->setObjectName(QStringLiteral("browserConsentBody"));
  body->setWordWrap(true);
  body->setTextInteractionFlags(Qt::TextSelectableByMouse);
  outer->addWidget(body);

  auto* dont_ask = new QCheckBox(QStringLiteral("Don't ask me again"), this);
  dont_ask->setObjectName(QStringLiteral("browserConsentDontAsk"));
  outer->addWidget(dont_ask);

  auto* buttons = new QDialogButtonBox(this);
  auto* not_now = buttons->addButton(QStringLiteral("Not now"),
                                     QDialogButtonBox::RejectRole);
  not_now->setObjectName(QStringLiteral("browserConsentNotNow"));
  auto* enable = buttons->addButton(QStringLiteral("Enable and connect"),
                                    QDialogButtonBox::AcceptRole);
  enable->setObjectName(QStringLiteral("browserConsentEnable"));
  enable->setDefault(true);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  outer->addWidget(buttons);
}

bool BrowserConsentDialog::dont_ask_again() const {
  const auto* box =
      findChild<QCheckBox*>(QStringLiteral("browserConsentDontAsk"));
  return box != nullptr && box->isChecked();
}

BrowserAutomationDialog::BrowserAutomationDialog(Hooks hooks, QWidget* parent)
    : QDialog(parent), hooks_(std::move(hooks)) {
  setObjectName(QStringLiteral("browserAutomationDialog"));
  setWindowTitle(QStringLiteral("Browser automation"));
  setModal(false);
  resize(620, 430);
  cdp_ = new CdpClient(this);
  BuildUi();

  connect(cdp_, &CdpClient::StatusChanged, this,
          [this](const QString& status) { SetStatus(status); });
  connect(cdp_, &CdpClient::Connected, this, [this](const QString& url) {
    UpdateConnectionUi(true);
    SetStatus(QStringLiteral("Connected to %1").arg(url));
  });
  connect(cdp_, &CdpClient::Failed, this, [this](const QString& error) {
    UpdateConnectionUi(false);
    SetStatus(error, true);
  });
  connect(cdp_, &CdpClient::Disconnected, this,
          [this]() { UpdateConnectionUi(false); });

  UpdateConnectionUi(false);
  SetStatus(
      QStringLiteral("Not connected. Gateway discovery runs first, then "
                     "the CDP endpoint is attached directly."));
}

BrowserAutomationDialog::~BrowserAutomationDialog() = default;

void BrowserAutomationDialog::BuildUi() {
  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(18, 16, 18, 14);
  outer->setSpacing(10);

  auto* title = new QLabel(QStringLiteral("Browser automation"), this);
  title->setObjectName(QStringLiteral("browserAutomationTitle"));
  QFont title_font = title->font();
  title_font.setBold(true);
  title_font.setPointSizeF(title_font.pointSizeF() + 1.0);
  title->setFont(title_font);
  outer->addWidget(title);

  auto* body = new QLabel(
      QStringLiteral(
          "Connect through the gateway's browser.manage route. Once the CDP "
          "page is attached, screenshots are captured with "
          "Page.captureScreenshot and pointer actions use real Chromium "
          "Input.dispatchMouseEvent coordinates."),
      this);
  body->setObjectName(QStringLiteral("browserAutomationDescription"));
  body->setWordWrap(true);
  outer->addWidget(body);

  auto* endpoint_row = new QHBoxLayout();
  endpoint_row->setSpacing(6);
  endpoint_row->addWidget(new QLabel(QStringLiteral("CDP endpoint"), this));
  endpoint_ = new QLineEdit(this);
  endpoint_->setObjectName(QStringLiteral("browserAutomationUrl"));
  endpoint_->setText(QString::fromLatin1(kDefaultLocalCdp));
  endpoint_->setPlaceholderText(QStringLiteral("http://127.0.0.1:9222"));
  endpoint_row->addWidget(endpoint_, 1);
  check_ = new QPushButton(QStringLiteral("Status"), this);
  check_->setObjectName(QStringLiteral("browserAutomationCheck"));
  connect_ = new QPushButton(QStringLiteral("Connect"), this);
  connect_->setObjectName(QStringLiteral("browserAutomationConnect"));
  disconnect_ = new QPushButton(QStringLiteral("Disconnect"), this);
  disconnect_->setObjectName(QStringLiteral("browserAutomationDisconnect"));
  endpoint_row->addWidget(check_);
  endpoint_row->addWidget(connect_);
  endpoint_row->addWidget(disconnect_);
  outer->addLayout(endpoint_row);

  auto* page_row = new QHBoxLayout();
  page_row->setSpacing(6);
  page_row->addWidget(new QLabel(QStringLiteral("Navigate to"), this));
  page_url_ = new QLineEdit(this);
  page_url_->setObjectName(QStringLiteral("browserAutomationPageUrl"));
  page_url_->setPlaceholderText(QStringLiteral("https://example.com"));
  page_row->addWidget(page_url_, 1);
  navigate_ = new QPushButton(QStringLiteral("Open"), this);
  navigate_->setObjectName(QStringLiteral("browserAutomationNavigate"));
  page_row->addWidget(navigate_);
  outer->addLayout(page_row);

  status_ = new QLabel(this);
  status_->setObjectName(QStringLiteral("browserAutomationStatus"));
  status_->setWordWrap(true);
  status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  outer->addWidget(status_);

  auto* screenshot_row = new QHBoxLayout();
  screenshot_row->setSpacing(8);
  screenshot_ = new QPushButton(QStringLiteral("Capture screenshot"), this);
  screenshot_->setObjectName(QStringLiteral("browserAutomationScreenshot"));
  screenshot_row->addWidget(screenshot_);
  screenshot_hint_ = new QLabel(this);
  screenshot_hint_->setObjectName(
      QStringLiteral("browserAutomationScreenshotHint"));
  screenshot_hint_->setWordWrap(true);
  screenshot_row->addWidget(screenshot_hint_, 1);
  outer->addLayout(screenshot_row);

  auto* coordinates = new QGroupBox(QStringLiteral("Coordinate actions"), this);
  coordinates->setObjectName(QStringLiteral("browserAutomationCoordinates"));
  auto* grid = new QGridLayout(coordinates);
  grid->setHorizontalSpacing(6);
  grid->setVerticalSpacing(6);

  const auto make_spin = [coordinates](const QString& name, double value) {
    auto* spin = new QDoubleSpinBox(coordinates);
    spin->setObjectName(name);
    spin->setRange(0.0, 20000.0);
    spin->setDecimals(0);
    spin->setSingleStep(10.0);
    spin->setValue(value);
    return spin;
  };
  x_ = make_spin(QStringLiteral("browserAutomationX"), 0.0);
  y_ = make_spin(QStringLiteral("browserAutomationY"), 0.0);
  x2_ = make_spin(QStringLiteral("browserAutomationX2"), 0.0);
  y2_ = make_spin(QStringLiteral("browserAutomationY2"), 0.0);

  grid->addWidget(new QLabel(QStringLiteral("X"), coordinates), 0, 0);
  grid->addWidget(x_, 0, 1);
  grid->addWidget(new QLabel(QStringLiteral("Y"), coordinates), 0, 2);
  grid->addWidget(y_, 0, 3);
  move_ = new QPushButton(QStringLiteral("Move"), coordinates);
  move_->setObjectName(QStringLiteral("browserAutomationMove"));
  click_ = new QPushButton(QStringLiteral("Click"), coordinates);
  click_->setObjectName(QStringLiteral("browserAutomationClick"));
  double_click_ = new QPushButton(QStringLiteral("Double click"), coordinates);
  double_click_->setObjectName(QStringLiteral("browserAutomationDoubleClick"));
  right_click_ = new QPushButton(QStringLiteral("Right click"), coordinates);
  right_click_->setObjectName(QStringLiteral("browserAutomationRightClick"));
  grid->addWidget(move_, 0, 4);
  grid->addWidget(click_, 0, 5);
  grid->addWidget(double_click_, 0, 6);
  grid->addWidget(right_click_, 0, 7);

  grid->addWidget(new QLabel(QStringLiteral("To X"), coordinates), 1, 0);
  grid->addWidget(x2_, 1, 1);
  grid->addWidget(new QLabel(QStringLiteral("To Y"), coordinates), 1, 2);
  grid->addWidget(y2_, 1, 3);
  drag_ = new QPushButton(QStringLiteral("Drag"), coordinates);
  drag_->setObjectName(QStringLiteral("browserAutomationDrag"));
  grid->addWidget(drag_, 1, 4);
  outer->addWidget(coordinates);

  auto* footer = new QHBoxLayout();
  footer->addStretch(1);
  close_ = new QPushButton(QStringLiteral("Close"), this);
  close_->setObjectName(QStringLiteral("browserAutomationClose"));
  footer->addWidget(close_);
  outer->addLayout(footer);

  connect(check_, &QPushButton::clicked, this,
          &BrowserAutomationDialog::CheckStatus);
  connect(connect_, &QPushButton::clicked, this,
          &BrowserAutomationDialog::ConnectToBrowser);
  connect(disconnect_, &QPushButton::clicked, this,
          &BrowserAutomationDialog::DisconnectFromBrowser);
  connect(navigate_, &QPushButton::clicked, this,
          &BrowserAutomationDialog::NavigatePage);
  connect(screenshot_, &QPushButton::clicked, this,
          &BrowserAutomationDialog::TakeScreenshot);
  connect(move_, &QPushButton::clicked, this,
          &BrowserAutomationDialog::MoveCoordinate);
  connect(click_, &QPushButton::clicked, this,
          [this]() { ActivateCoordinate(QStringLiteral("left"), 1); });
  connect(double_click_, &QPushButton::clicked, this,
          [this]() { ActivateCoordinate(QStringLiteral("left"), 2); });
  connect(right_click_, &QPushButton::clicked, this,
          [this]() { ActivateCoordinate(QStringLiteral("right"), 1); });
  connect(drag_, &QPushButton::clicked, this,
          &BrowserAutomationDialog::DragCoordinates);
  connect(close_, &QPushButton::clicked, this, &QDialog::close);
}

QString BrowserAutomationDialog::Endpoint() const {
  return endpoint_ != nullptr ? endpoint_->text().trimmed() : QString();
}

void BrowserAutomationDialog::SetEndpoint(const QString& endpoint) {
  if (endpoint_ != nullptr) {
    endpoint_->setText(NormalizeCdpEndpoint(endpoint));
  }
}

void BrowserAutomationDialog::SetStatus(const QString& text, bool error) {
  if (status_ == nullptr) {
    return;
  }
  status_->setText(text);
  status_->setStyleSheet(error ? QStringLiteral("color: #c0392b;") : QString());
}

void BrowserAutomationDialog::ConnectNow() { ConnectToBrowser(); }

void BrowserAutomationDialog::ConnectToBrowser() {
  if (!hooks_.manage) {
    SetStatus(QStringLiteral("Browser automation is not available."), true);
    return;
  }
  connect_->setEnabled(false);
  SetStatus(QStringLiteral("Connecting through the gateway…"));
  Manage(QStringLiteral("connect"), Endpoint());
}

void BrowserAutomationDialog::CheckStatus() {
  if (!hooks_.manage) {
    SetStatus(QStringLiteral("Browser automation is not available."), true);
    return;
  }
  SetStatus(QStringLiteral("Checking the gateway's CDP browser…"));
  Manage(QStringLiteral("status"), Endpoint());
}

void BrowserAutomationDialog::DisconnectFromBrowser() {
  if (cdp_ != nullptr) {
    cdp_->Disconnect();
  }
  UpdateConnectionUi(false);
  if (hooks_.manage) {
    SetStatus(QStringLiteral("Disconnecting…"));
    Manage(QStringLiteral("disconnect"), Endpoint());
  } else {
    SetStatus(QStringLiteral("Browser disconnected."));
  }
}

void BrowserAutomationDialog::NavigatePage() {
  if (cdp_ == nullptr || !cdp_->IsConnected()) {
    SetStatus(QStringLiteral("Connect a browser before navigating."), true);
    return;
  }
  const QString url = page_url_->text().trimmed();
  if (url.isEmpty()) {
    SetStatus(QStringLiteral("Enter a URL to navigate to."), true);
    return;
  }
  SetStatus(QStringLiteral("Navigating to %1…").arg(url));
  cdp_->Navigate(url, [this](const QJsonObject& result, const QString& error) {
    if (!error.isEmpty()) {
      SetStatus(error, true);
      return;
    }
    const QString frame_url =
        result.value(QStringLiteral("frameUrl")).toString();
    SetStatus(frame_url.isEmpty()
                  ? QStringLiteral("Navigation started.")
                  : QStringLiteral("Navigating to %1").arg(frame_url));
  });
}

void BrowserAutomationDialog::TakeScreenshot() {
  if (cdp_ == nullptr || !cdp_->IsConnected()) {
    SetStatus(QStringLiteral("Connect a browser before taking a screenshot."),
              true);
    return;
  }
  SetStatus(QStringLiteral("Capturing a CDP screenshot…"));
  const QPointer<BrowserAutomationDialog> guard(this);
  cdp_->CaptureScreenshot([guard](const QByteArray& png, const QString& error) {
    if (!guard) {
      return;
    }
    if (!error.isEmpty()) {
      guard->SetStatus(error, true);
      return;
    }
    if (guard->screenshot_hint_ != nullptr) {
      guard->screenshot_hint_->setText(
          QStringLiteral("%1 KB captured").arg(png.size() / 1024));
    }
    guard->SetStatus(
        QStringLiteral("Screenshot captured — choose where to "
                       "save it."));
    if (guard->hooks_.save_screenshot) {
      guard->hooks_.save_screenshot(
          png, BrowserScreenshotName(QDateTime::currentDateTime()));
    }
  });
}

void BrowserAutomationDialog::ActivateCoordinate(const QString& button,
                                                 int click_count) {
  if (cdp_ == nullptr || !cdp_->IsConnected()) {
    SetStatus(QStringLiteral("Connect a browser before sending pointer input."),
              true);
    return;
  }
  const double x = x_->value();
  const double y = y_->value();
  SetStatus(QStringLiteral("%1 at (%2, %3)…")
                .arg(click_count > 1 ? QStringLiteral("Double click")
                                     : QStringLiteral("Click"))
                .arg(x)
                .arg(y));
  SendCoordinateEvents(BuildBrowserClickEvents(x, y, button, click_count));
}

void BrowserAutomationDialog::DragCoordinates() {
  if (cdp_ == nullptr || !cdp_->IsConnected()) {
    SetStatus(QStringLiteral("Connect a browser before sending pointer input."),
              true);
    return;
  }
  SetStatus(QStringLiteral("Dragging from (%1, %2) to (%3, %4)…")
                .arg(x_->value())
                .arg(y_->value())
                .arg(x2_->value())
                .arg(y2_->value()));
  SendCoordinateEvents(BuildBrowserDragEvents(x_->value(), y_->value(),
                                              x2_->value(), y2_->value(),
                                              QStringLiteral("left")));
}

void BrowserAutomationDialog::MoveCoordinate() {
  if (cdp_ == nullptr || !cdp_->IsConnected()) {
    SetStatus(QStringLiteral("Connect a browser before sending pointer input."),
              true);
    return;
  }
  SetStatus(
      QStringLiteral("Moving to (%1, %2)…").arg(x_->value()).arg(y_->value()));
  SendCoordinateEvents(
      {BuildBrowserMouseEvent(QStringLiteral("mouseMoved"), x_->value(),
                              y_->value(), QStringLiteral("none"), 0, 0)});
}

void BrowserAutomationDialog::SendCoordinateEvents(
    const std::vector<QJsonObject>& events) {
  const QPointer<BrowserAutomationDialog> guard(this);
  cdp_->DispatchMouseEvents(
      events, [guard](const QJsonObject&, const QString& error) {
        if (!guard) {
          return;
        }
        if (!error.isEmpty()) {
          guard->SetStatus(error, true);
          return;
        }
        guard->SetStatus(QStringLiteral("Coordinate action sent."));
      });
}

void BrowserAutomationDialog::UpdateConnectionUi(bool connected) {
  connected_ = connected;
  if (connect_ != nullptr) {
    connect_->setEnabled(!connected);
  }
  if (disconnect_ != nullptr) {
    disconnect_->setEnabled(true);
  }
  for (QWidget* control :
       {static_cast<QWidget*>(navigate_), static_cast<QWidget*>(screenshot_),
        static_cast<QWidget*>(click_), static_cast<QWidget*>(double_click_),
        static_cast<QWidget*>(right_click_), static_cast<QWidget*>(move_),
        static_cast<QWidget*>(drag_), static_cast<QWidget*>(x_),
        static_cast<QWidget*>(y_), static_cast<QWidget*>(x2_),
        static_cast<QWidget*>(y2_)}) {
    if (control != nullptr) {
      control->setEnabled(connected);
    }
  }
}

void BrowserAutomationDialog::Manage(const QString& action,
                                     const QString& url) {
  const QPointer<BrowserAutomationDialog> guard(this);
  hooks_.manage(
      action, url,
      [guard, action](const QJsonObject& result, const QString& error) {
        if (!guard) {
          return;
        }
        if (!error.isEmpty()) {
          guard->UpdateConnectionUi(false);
          guard->SetStatus(error, true);
          return;
        }
        const QString messages = BrowserManageMessages(result);
        if (action == QStringLiteral("status")) {
          if (BrowserManageConnected(result)) {
            const QString result_url = BrowserManageUrl(result);
            if (!result_url.isEmpty()) {
              guard->endpoint_->setText(result_url);
            }
            guard->SetStatus(
                result_url.isEmpty()
                    ? QStringLiteral("A CDP browser is configured.")
                    : QStringLiteral("CDP browser configured: %1")
                          .arg(result_url));
          } else {
            guard->UpdateConnectionUi(false);
            guard->SetStatus(QStringLiteral("No CDP browser connected."));
          }
          return;
        }
        if (action == QStringLiteral("disconnect")) {
          guard->UpdateConnectionUi(false);
          guard->SetStatus(QStringLiteral("Browser disconnected."));
          return;
        }
        if (!BrowserManageConnected(result)) {
          guard->UpdateConnectionUi(false);
          guard->connect_->setEnabled(true);
          guard->SetStatus(messages.isEmpty()
                               ? QStringLiteral("Browser not connected.")
                               : messages,
                           true);
          return;
        }
        const QString cdp_url = BrowserManageUrl(result);
        if (!cdp_url.isEmpty()) {
          guard->endpoint_->setText(cdp_url);
        }
        guard->SetStatus(
            messages.isEmpty()
                ? QStringLiteral("Gateway connected — attaching to the CDP "
                                 "page…")
                : messages);
        guard->cdp_->Connect(cdp_url.isEmpty() ? guard->Endpoint() : cdp_url);
      });
}

}  // namespace hermes::ui
