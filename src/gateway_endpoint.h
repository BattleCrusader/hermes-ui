// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_GATEWAY_ENDPOINT_H_
#define HERMES_UI_GATEWAY_ENDPOINT_H_

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace hermes::ui {

// The credential-free endpoint published by `gateway ensure --json`. Tickets
// are minted on the owner-only control socket; they never appear in this
// struct.
struct GatewayEndpoint {
  QString api_origin;
  QString instance_id;
  QString
      profile_id;  // Default home path; named profiles append /profiles/<n>.
  QString code_sha;
  int runtime_protocol = 0;
  // Profile homes this gateway serves. The REST status roster can list every
  // profile on disk; only these can mint a ticket.
  QStringList served_profiles;

  bool valid() const { return !api_origin.isEmpty() && !instance_id.isEmpty(); }
};

// $HERMES_HOME or ~/.hermes.
QString HermesHome();

// Home for a profile name. "default" is the root home.
QString ProfileHome(const QString& profile_name);

// Resolves the control socket for *home* (direct path, or the pointer file
// used when the home path exceeds the AF_UNIX sun_path limit).
QString ControlSocketPath(const QString& home, QString* error);

// Sends one JSON line to the control socket and returns the `result` object.
bool QueryControl(const QString& home, const QJsonObject& request,
                  QJsonObject* result, QString* error);

// Identifies the gateway serving *home*.
bool DiscoverEndpoint(GatewayEndpoint* endpoint, QString* error);

// Whether a ticket refusal means the endpoint names a gateway instance that
// has since been replaced. A gateway restart keeps the same API origin and
// control socket but mints a new instance id, so a client must discover the
// endpoint again instead of retrying the old ticket forever.
bool IsStaleEndpointError(const QString& error);

// Mints a one-use ticket for *profile_name*. Purpose is "native-http" for REST
// calls and "interactive" for the /api/ws dial.
bool RequestTicket(const QString& home, const QString& profile_name,
                   const GatewayEndpoint& endpoint, const QString& purpose,
                   QString* ticket, QString* error);

}  // namespace hermes::ui

#endif  // HERMES_UI_GATEWAY_ENDPOINT_H_
