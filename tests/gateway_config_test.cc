// Copyright 2026 The Hermes Qt Spike Authors.

#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

#include "src/gateway_endpoint.h"
#include "src/gateway_http.h"

namespace {

QJsonValue FindKey(const QJsonObject& object, const QString& key) {
  const QStringList parts = key.split(QLatin1Char('.'));
  QJsonValue current = object;
  for (const QString& part : parts) {
    if (!current.isObject()) {
      return QJsonValue();
    }
    const QJsonObject row = current.toObject();
    if (!row.contains(part)) {
      return QJsonValue();
    }
    current = row.value(part);
  }
  return current;
}

}  // namespace

// Live contract test: GET the sparse config, write the *same* scalar back with
// PUT /api/config, then read it again. This proves the native save path is
// accepted by the gateway without changing the user's settings.
class GatewayConfigTest : public QObject {
  Q_OBJECT

 private slots:
  void putConfigRoundTrips();
  void settingsPageEndpointsRespond();
  void settingsPagePayloadShapesMatchRenderers();
  void uiPreferencesPatchRoundTrips();
};

void GatewayConfigTest::putConfigRoundTrips() {
  hermes::ui::GatewayEndpoint endpoint;
  QString error;
  if (!hermes::ui::DiscoverEndpoint(&endpoint, &error)) {
    QSKIP("gateway not running");
  }

  hermes::ui::GatewayHttp http(hermes::ui::HermesHome(),
                               QStringLiteral("default"), endpoint);
  QJsonDocument config_doc;
  QVERIFY2(http.GetJson(QStringLiteral("/api/config?include_defaults=false"),
                        &config_doc, &error),
           qUtf8Printable(error));
  const QJsonObject config = config_doc.object();

  const QString key = QStringLiteral("display.show_reasoning");
  const QJsonValue current = FindKey(config, key);
  if (!current.isBool()) {
    QSKIP("display.show_reasoning is not present in the sparse config");
  }

  const QJsonObject patch{
      {QStringLiteral("display"),
       QJsonObject{{QStringLiteral("show_reasoning"), current.toBool()}}}};
  QJsonDocument response;
  QVERIFY2(http.PutJson(QStringLiteral("/api/config"),
                        QJsonObject{{QStringLiteral("config"), patch}},
                        &response, &error),
           qUtf8Printable(error));

  QJsonDocument after_doc;
  QVERIFY2(http.GetJson(QStringLiteral("/api/config?include_defaults=false"),
                        &after_doc, &error),
           qUtf8Printable(error));
  QCOMPARE(FindKey(after_doc.object(), key).toBool(), current.toBool());
}

void GatewayConfigTest::settingsPageEndpointsRespond() {
  hermes::ui::GatewayEndpoint endpoint;
  QString error;
  if (!hermes::ui::DiscoverEndpoint(&endpoint, &error)) {
    QSKIP("gateway not running");
  }
  hermes::ui::GatewayHttp http(hermes::ui::HermesHome(),
                               QStringLiteral("default"), endpoint);

  // Every source the Settings data pages read. A page is only "ported" if the
  // route it renders from actually answers for this profile.
  const QStringList paths{
      QStringLiteral("/api/env"),
      QStringLiteral("/api/providers/custom-endpoints"),
      QStringLiteral("/api/credentials/pool"),
      QStringLiteral("/api/webapp-access/status"),
      QStringLiteral("/api/dashboard/plugins"),
      QStringLiteral("/api/portal"),
      QStringLiteral("/api/status"),
      QStringLiteral("/api/hermes/update/check"),
      QStringLiteral("/api/sessions/stats"),
      QStringLiteral("/api/cron/jobs"),
      QStringLiteral("/api/tools/toolsets"),
      QStringLiteral("/api/skills"),
      QStringLiteral("/api/mcp/servers"),
  };
  for (const QString& path : paths) {
    QJsonDocument document;
    QString route_error;
    QVERIFY2(http.GetJson(path, &document, &route_error),
             qUtf8Printable(path + QStringLiteral(": ") + route_error));
    QVERIFY2(!document.isNull(), qUtf8Printable(path));
  }
}

void GatewayConfigTest::uiPreferencesPatchRoundTrips() {
  // /api/ui-preferences is newer than some released gateways; the native
  // client keeps its own preferences in QSettings and only mirrors them here,
  // so a gateway without the route is a skip, not a failure.
  hermes::ui::GatewayEndpoint endpoint;
  QString error;
  if (!hermes::ui::DiscoverEndpoint(&endpoint, &error)) {
    QSKIP("gateway not running");
  }
  hermes::ui::GatewayHttp http(hermes::ui::HermesHome(),
                               QStringLiteral("default"), endpoint);

  QJsonDocument before;
  if (!http.GetJson(QStringLiteral("/api/ui-preferences"), &before, &error)) {
    QSKIP("gateway does not expose /api/ui-preferences");
  }
  const QJsonObject values =
      before.object().value(QStringLiteral("values")).toObject();
  const QJsonValue current = values.value(QStringLiteral("native.font_scale"));
  const QString value = current.isString() && !current.toString().isEmpty()
                            ? current.toString()
                            : QStringLiteral("1.00");

  QJsonDocument response;
  QVERIFY2(http.PatchJson(
               QStringLiteral("/api/ui-preferences"),
               QJsonObject{
                   {QStringLiteral("values"),
                    QJsonObject{{QStringLiteral("native.font_scale"), value}}}},
               &response, &error),
           qUtf8Printable(error));

  QJsonDocument after;
  QVERIFY2(http.GetJson(QStringLiteral("/api/ui-preferences"), &after, &error),
           qUtf8Printable(error));
  QCOMPARE(after.object()
               .value(QStringLiteral("values"))
               .toObject()
               .value(QStringLiteral("native.font_scale"))
               .toString(),
           value);
}

void GatewayConfigTest::settingsPagePayloadShapesMatchRenderers() {
  hermes::ui::GatewayEndpoint endpoint;
  QString error;
  if (!hermes::ui::DiscoverEndpoint(&endpoint, &error)) {
    QSKIP("gateway not running");
  }
  hermes::ui::GatewayHttp http(hermes::ui::HermesHome(),
                               QStringLiteral("default"), endpoint);
  auto get = [&http, &error](const QString& path) {
    QJsonDocument document;
    const bool ok = http.GetJson(path, &document, &error);
    if (!ok) {
      qWarning("%s: %s", qUtf8Printable(path), qUtf8Printable(error));
    }
    return document;
  };

  // Providers/keys read a map of env rows.
  const QJsonObject env = get(QStringLiteral("/api/env")).object();
  QVERIFY(!env.isEmpty());
  QVERIFY(
      env.constBegin().value().toObject().contains(QStringLiteral("is_set")));

  const QJsonObject endpoints =
      get(QStringLiteral("/api/providers/custom-endpoints")).object();
  QVERIFY(endpoints.contains(QStringLiteral("endpoints")));

  // Plugins, jobs, toolsets and skills render from bare arrays.
  QVERIFY(get(QStringLiteral("/api/dashboard/plugins")).isArray());
  QVERIFY(get(QStringLiteral("/api/cron/jobs")).isArray());
  QVERIFY(get(QStringLiteral("/api/tools/toolsets")).isArray());
  QVERIFY(get(QStringLiteral("/api/skills")).isArray());

  // MCP servers, credentials and web access are wrapped objects.
  QVERIFY(get(QStringLiteral("/api/mcp/servers"))
              .object()
              .value(QStringLiteral("servers"))
              .isArray());
  QVERIFY(get(QStringLiteral("/api/credentials/pool"))
              .object()
              .value(QStringLiteral("providers"))
              .isArray());
  QVERIFY(get(QStringLiteral("/api/webapp-access/status"))
              .object()
              .contains(QStringLiteral("enabled")));

  const QJsonObject stats = get(QStringLiteral("/api/sessions/stats")).object();
  QVERIFY(stats.contains(QStringLiteral("total")));
}

QTEST_MAIN(GatewayConfigTest)
#include "gateway_config_test.moc"
