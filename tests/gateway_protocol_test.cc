// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/gateway_protocol.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QtTest>
#include <vector>

#include "src/gateway_events.h"
#include "src/gateway_replay.h"
#include "src/pending_prompt_tracker.h"

namespace hermes::ui {
namespace {

QJsonObject Notice(const QString& key, const QString& text,
                   const QString& level = QStringLiteral("info")) {
  QJsonObject payload;
  payload.insert(QStringLiteral("key"), key);
  payload.insert(QStringLiteral("text"), text);
  payload.insert(QStringLiteral("level"), level);
  return payload;
}

QJsonObject Reaction(const QString& emoji) {
  QJsonObject reaction;
  reaction.insert(QStringLiteral("emoji"), emoji);
  reaction.insert(QStringLiteral("author"), QStringLiteral("you"));
  return reaction;
}

}  // namespace

class GatewayProtocolTest : public QObject {
  Q_OBJECT

 private slots:
  void replayCursorDropsDuplicatesAndIsolatesSessions();
  void replayCursorResetsOnEpochChange();
  void noticesUpsertByKeyAndClear();
  void sessionControlSummaryFormatsSnapshots();
  void moaTextMatchesDesktopLabels();
  void reactionSummaryGroupsEmoji();
  void reviewAndAgentPrintText();
  void toolProgressLabels();
  void sessionCreateCarriesRequestIdAndCwd();
  void sessionMutateCarriesFencingFields();
  void sidebarPatchMapsOntoClosedOperations();
  void replayAndReceiptParamsCarryWatermarks();
  void pendingAdmissionsParseAndBuildQueueActions();
  void pendingPromptTrackerAnnouncesOnlyProcessedQueueRows();
  void connectionOperationParsesTargetsAndBuildsAnswer();
  void handoffPlatformAndStatusText();
};

void GatewayProtocolTest::replayCursorDropsDuplicatesAndIsolatesSessions() {
  ReplayCursor cursor;
  QVERIFY(!cursor.ObserveEpoch(QStringLiteral("epoch-a")));
  QCOMPARE(cursor.Epoch(), QStringLiteral("epoch-a"));
  QVERIFY(cursor.AcceptEvent(QStringLiteral("s1"), 1));
  QVERIFY(cursor.AcceptEvent(QStringLiteral("s1"), 3));
  QVERIFY(!cursor.AcceptEvent(QStringLiteral("s1"), 3));
  QVERIFY(!cursor.AcceptEvent(QStringLiteral("s1"), 2));
  QVERIFY(cursor.AcceptEvent(QStringLiteral("s2"), 1));
  QCOMPARE(cursor.LastSeq(QStringLiteral("s1")), 3);
  QCOMPARE(cursor.LastSeq(QStringLiteral("s2")), 1);
  // Session-less events are not stamped by the gateway, so they always pass.
  QVERIFY(cursor.AcceptEvent(QString(), 9));
  QCOMPARE(cursor.Sessions(),
           QStringList({QStringLiteral("s1"), QStringLiteral("s2")}));
  cursor.SetLastSeq(QStringLiteral("s1"), 2);
  QCOMPARE(cursor.LastSeq(QStringLiteral("s1")), 3);
  cursor.ForgetSession(QStringLiteral("s2"));
  QCOMPARE(cursor.LastSeq(QStringLiteral("s2")), 0);
}

void GatewayProtocolTest::replayCursorResetsOnEpochChange() {
  ReplayCursor cursor;
  cursor.ObserveEpoch(QStringLiteral("epoch-a"));
  cursor.AcceptEvent(QStringLiteral("s1"), 7);
  QVERIFY(cursor.ObserveEpoch(QStringLiteral("epoch-b")));
  QCOMPARE(cursor.LastSeq(QStringLiteral("s1")), 0);
  QVERIFY(!cursor.ObserveEpoch(QStringLiteral("epoch-b")));
  cursor.AcceptEvent(QStringLiteral("s1"), 1);
  QCOMPARE(cursor.LastSeq(QStringLiteral("s1")), 1);
  cursor.Reset();
  QVERIFY(cursor.Epoch().isEmpty());
  QVERIFY(cursor.Sessions().isEmpty());
}

void GatewayProtocolTest::noticesUpsertByKeyAndClear() {
  std::vector<GatewayNotice> notices;
  UpsertGatewayNotice(
      &notices, Notice(QStringLiteral("credits"), QStringLiteral("2 left")), 1);
  UpsertGatewayNotice(&notices,
                      Notice(QStringLiteral("credits"),
                             QStringLiteral("1 left"), QStringLiteral("warn")),
                      2);
  QCOMPARE(notices.size(), std::size_t{1});
  QCOMPARE(notices.front().text, QStringLiteral("1 left"));
  QCOMPARE(notices.front().level, QStringLiteral("warn"));
  QCOMPARE(notices.front().serial, quint64{2});
  QCOMPARE(GatewayNoticeTitle(notices.front().level),
           QStringLiteral("Warning"));
  QCOMPARE(GatewayNoticeLine(notices.front()), QStringLiteral("1 left"));

  UpsertGatewayNotice(
      &notices, Notice(QStringLiteral("plugin"), QStringLiteral("Ready")), 3);
  QCOMPARE(notices.size(), std::size_t{2});
  QVERIFY(ClearGatewayNotice(&notices, QStringLiteral("credits")));
  QCOMPARE(notices.size(), std::size_t{1});
  QCOMPARE(notices.front().key, QStringLiteral("plugin"));
  QVERIFY(!ClearGatewayNotice(&notices, QStringLiteral("missing")));
}

void GatewayProtocolTest::sessionControlSummaryFormatsSnapshots() {
  QJsonObject goal;
  goal.insert(QStringLiteral("status"), QStringLiteral("active"));
  goal.insert(QStringLiteral("title"), QStringLiteral("Ship it"));
  QJsonObject loop;
  loop.insert(QStringLiteral("status"), QStringLiteral("paused"));
  loop.insert(QStringLiteral("interval_seconds"), 300);
  QJsonObject heartbeat;
  heartbeat.insert(QStringLiteral("status"), QStringLiteral("active"));
  heartbeat.insert(QStringLiteral("interval_seconds"), 30);
  QJsonObject control;
  control.insert(QStringLiteral("goal"), goal);
  control.insert(QStringLiteral("loop"), loop);
  control.insert(QStringLiteral("heartbeat"), heartbeat);

  QCOMPARE(SessionControlSummary(control),
           QStringLiteral("Goal: active — Ship it · Loop: paused every 5m · "
                          "Heartbeat: active every 30s"));
  QJsonObject wrapped;
  wrapped.insert(QStringLiteral("control"), control);
  QCOMPARE(SessionControlSummary(wrapped), SessionControlSummary(control));
  QVERIFY(SessionControlSummary(QJsonObject()).isEmpty());
}

void GatewayProtocolTest::moaTextMatchesDesktopLabels() {
  QJsonObject reference;
  reference.insert(QStringLiteral("index"), 2);
  reference.insert(QStringLiteral("count"), 3);
  reference.insert(QStringLiteral("label"), QStringLiteral("docs"));
  reference.insert(QStringLiteral("text"), QStringLiteral("body"));
  QCOMPARE(MoaReferenceText(reference),
           QStringLiteral("◇ Reference 2/3 — docs\nbody\n\n"));

  QJsonObject first;
  first.insert(QStringLiteral("label"), QStringLiteral("docs"));
  first.insert(QStringLiteral("text"), QStringLiteral("body"));
  QCOMPARE(MoaReferenceText(first),
           QStringLiteral("◇ Reference — docs\nbody\n\n"));

  QJsonObject progress;
  progress.insert(QStringLiteral("refs_done"), 1);
  progress.insert(QStringLiteral("refs_total"), 3);
  progress.insert(QStringLiteral("label"), QStringLiteral("docs"));
  QCOMPARE(MoaProgressText(progress),
           QStringLiteral("◇ MoA refs 1/3 — docs\n"));
  progress.remove(QStringLiteral("label"));
  QCOMPARE(MoaProgressText(progress), QStringLiteral("◇ MoA refs 1/3\n"));

  QJsonObject phase;
  phase.insert(QStringLiteral("phase"), QStringLiteral("aggregator"));
  QCOMPARE(MoaPhaseText(phase), QStringLiteral("◇ MoA aggregating…\n"));
  phase.insert(QStringLiteral("phase"), QStringLiteral("references"));
  QVERIFY(MoaPhaseText(phase).isEmpty());
}

void GatewayProtocolTest::reactionSummaryGroupsEmoji() {
  QJsonArray reactions;
  reactions.append(Reaction(QStringLiteral("👍")));
  reactions.append(Reaction(QStringLiteral("🔥")));
  reactions.append(Reaction(QStringLiteral("👍")));
  QCOMPARE(ReactionSummary(reactions), QStringLiteral("👍 2 · 🔥 1"));
  const auto parsed = ParseChatReactions(reactions);
  QCOMPARE(parsed.size(), std::size_t{3});
  QCOMPARE(parsed.front().emoji, QStringLiteral("👍"));
  QCOMPARE(parsed.front().author, QStringLiteral("you"));
}

void GatewayProtocolTest::reviewAndAgentPrintText() {
  QJsonObject review;
  review.insert(QStringLiteral("text"),
                QStringLiteral("💾 Tightened the loop"));
  QCOMPARE(ReviewSummaryText(review), QStringLiteral("Tightened the loop"));
  QJsonObject agent;
  agent.insert(QStringLiteral("text"), QStringLiteral("  build finished  "));
  QCOMPARE(AgentPrintText(agent), QStringLiteral("build finished"));
}

void GatewayProtocolTest::toolProgressLabels() {
  QJsonObject generating;
  generating.insert(QStringLiteral("name"), QStringLiteral("terminal"));
  QCOMPARE(ToolGeneratingText(generating),
           QStringLiteral("Preparing terminal…"));
  QVERIFY(!ToolGeneratingText(QJsonObject()).isEmpty());

  QJsonObject progress;
  progress.insert(QStringLiteral("name"), QStringLiteral("search"));
  progress.insert(QStringLiteral("message"), QStringLiteral("indexing"));
  progress.insert(QStringLiteral("percent"), 45);
  QCOMPARE(ToolProgressText(progress),
           QStringLiteral("search · indexing · 45%"));
  QJsonObject ratio;
  ratio.insert(QStringLiteral("progress"), 1.0);
  ratio.insert(QStringLiteral("total"), 4.0);
  QCOMPARE(ToolProgressText(ratio), QStringLiteral("25%"));
  QVERIFY(ToolProgressText(QJsonObject()).isEmpty());
}

void GatewayProtocolTest::sessionCreateCarriesRequestIdAndCwd() {
  const QJsonObject params =
      BuildSessionCreateParams(QStringLiteral("work"), QStringLiteral("key-1"));
  QCOMPARE(params.value(QStringLiteral("request_id")).toString(),
           QStringLiteral("key-1"));
  QCOMPARE(params.value(QStringLiteral("source")).toString(),
           QStringLiteral("gui"));
  QVERIFY(!params.contains(QStringLiteral("idempotency_key")));
  QCOMPARE(params.value(QStringLiteral("profile")).toString(),
           QStringLiteral("work"));
  const QJsonObject anonymous =
      BuildSessionCreateParams(QString(), QStringLiteral("key-2"));
  QVERIFY(!anonymous.contains(QStringLiteral("profile")));
  const QJsonObject scoped =
      BuildSessionCreateParams(QStringLiteral("work"), QStringLiteral("key-3"),
                               QStringLiteral("/Users/me/code/project"));
  QCOMPARE(scoped.value(QStringLiteral("cwd")).toString(),
           QStringLiteral("/Users/me/code/project"));
  QCOMPARE(scoped.value(QStringLiteral("cwd_explicit")).toBool(), true);
  QVERIFY(!BuildSessionCreateParams(QStringLiteral("work"),
                                    QStringLiteral("key-4"),
                                    QStringLiteral("  "))
               .contains(QStringLiteral("cwd")));
}

void GatewayProtocolTest::sessionMutateCarriesFencingFields() {
  QJsonObject payload;
  payload.insert(QStringLiteral("archived"), true);
  const QJsonObject params =
      BuildSessionMutateParams(QStringLiteral("s1"), QStringLiteral("r1"), 12,
                               QStringLiteral("archive"), payload, 4);
  QCOMPARE(params.value(QStringLiteral("session_id")).toString(),
           QStringLiteral("s1"));
  QCOMPARE(params.value(QStringLiteral("request_id")).toString(),
           QStringLiteral("r1"));
  QCOMPARE(params.value(QStringLiteral("expected_revision")).toInt(), 12);
  QCOMPARE(params.value(QStringLiteral("expected_generation")).toInt(), 4);
  QCOMPARE(params.value(QStringLiteral("operation")).toString(),
           QStringLiteral("archive"));
  QVERIFY(!params.contains(QStringLiteral("profile")));
  const QJsonObject unfenced =
      BuildSessionMutateParams(QStringLiteral("s1"), QStringLiteral("r2"), 12,
                               QStringLiteral("sidebar"), QJsonObject(), -1);
  QVERIFY(!unfenced.contains(QStringLiteral("expected_generation")));
}

void GatewayProtocolTest::replayAndReceiptParamsCarryWatermarks() {
  const QJsonObject replay = BuildSessionEventsSinceParams(
      QStringLiteral("s1"), QStringLiteral("work"), 42);
  QCOMPARE(replay.value(QStringLiteral("session_id")).toString(),
           QStringLiteral("s1"));
  QCOMPARE(replay.value(QStringLiteral("profile")).toString(),
           QStringLiteral("work"));
  QCOMPARE(replay.value(QStringLiteral("last_seen")).toInt(), 42);

  const QJsonObject receipt = BuildPromptReceiptParams(
      QStringLiteral("s1"), QStringLiteral("admission-7"));
  QCOMPARE(receipt.value(QStringLiteral("admission_id")).toString(),
           QStringLiteral("admission-7"));
  QCOMPARE(receipt.value(QStringLiteral("include_result")).toBool(), true);
}

void GatewayProtocolTest::pendingAdmissionsParseAndBuildQueueActions() {
  QJsonObject first{
      {QStringLiteral("admission_id"), QStringLiteral("a1")},
      {QStringLiteral("input_id"), QStringLiteral("input-1")},
      {QStringLiteral("text"), QStringLiteral("first queued")},
      {QStringLiteral("status"), QStringLiteral("queued")},
      {QStringLiteral("controls"),
       QJsonArray{QStringLiteral("steer"), QStringLiteral("replace")}},
      {QStringLiteral("steering"), false},
      {QStringLiteral("sequence"), 4},
      {QStringLiteral("authority_epoch"), 9},
      {QStringLiteral("execution_generation"), 12},
  };
  QJsonObject second{
      {QStringLiteral("id"), QStringLiteral("a2")},
      {QStringLiteral("input_id"), QStringLiteral("input-2")},
      {QStringLiteral("text"), QStringLiteral("steering now")},
      {QStringLiteral("status"), QStringLiteral("started")},
      {QStringLiteral("outcome"), QStringLiteral("steering")},
      {QStringLiteral("steering"), true},
      {QStringLiteral("sequence"), 5},
      {QStringLiteral("authority_epoch"), 9},
  };
  const std::vector<PendingAdmission> pending =
      ParsePendingAdmissions(QJsonArray{first, second});
  QCOMPARE(pending.size(), static_cast<std::size_t>(2));
  QCOMPARE(pending[0].admission_id, QStringLiteral("a1"));
  QCOMPARE(pending[0].input_id, QStringLiteral("input-1"));
  QCOMPARE(pending[0].text, QStringLiteral("first queued"));
  QCOMPARE(pending[0].controls.size(), 2);
  QCOMPARE(pending[0].sequence, 4);
  QCOMPARE(pending[0].authority_epoch, 9);
  QCOMPARE(pending[0].execution_generation, 12);
  QVERIFY(!pending[0].steering);
  QCOMPARE(pending[1].admission_id, QStringLiteral("a2"));
  QCOMPARE(pending[1].status, QStringLiteral("started"));
  QCOMPARE(pending[1].outcome, QStringLiteral("steering"));
  QCOMPARE(pending[1].execution_generation, -1);
  QVERIFY(pending[1].steering);

  // First snapshot always wins. Within one epoch an older revision cannot
  // prove an admission left the FIFO; a new owner epoch is authoritative.
  QVERIFY(PendingSnapshotWins(9, 4, 9, -1));
  QVERIFY(!PendingSnapshotWins(9, 3, 9, 4));
  QVERIFY(PendingSnapshotWins(9, 4, 9, 4));
  QVERIFY(PendingSnapshotWins(10, 1, 9, 40));

  const QJsonObject cancel = BuildPromptCancelParams(
      QStringLiteral("s1"), QStringLiteral("a1"), QStringLiteral("ceo"));
  QCOMPARE(cancel.value(QStringLiteral("session_id")).toString(),
           QStringLiteral("s1"));
  QCOMPARE(cancel.value(QStringLiteral("admission_id")).toString(),
           QStringLiteral("a1"));
  QCOMPARE(cancel.value(QStringLiteral("profile")).toString(),
           QStringLiteral("ceo"));

  const QJsonObject steer =
      BuildPromptSteerQueuedParams(QStringLiteral("s1"), QStringLiteral("a1"));
  QCOMPARE(steer.value(QStringLiteral("admission_id")).toString(),
           QStringLiteral("a1"));

  const QJsonObject replace = BuildPromptReplaceQueuedParams(
      QStringLiteral("s1"), QStringLiteral("a1"),
      QStringLiteral("submission-2"), QStringLiteral("edited queue"),
      QStringLiteral("work"));
  QCOMPARE(replace.value(QStringLiteral("submission_id")).toString(),
           QStringLiteral("submission-2"));
  QCOMPARE(replace.value(QStringLiteral("text")).toString(),
           QStringLiteral("edited queue"));

  const QJsonObject resolve = BuildPromptResolveUnknownParams(
      QStringLiteral("s1"), QStringLiteral("a1"), 12, QStringLiteral("work"));
  QCOMPARE(resolve.value(QStringLiteral("execution_generation")).toInt(), 12);
}

void GatewayProtocolTest::
    pendingPromptTrackerAnnouncesOnlyProcessedQueueRows() {
  auto row = [](const QString& id, const QString& text, const QString& status,
                bool steering = false) {
    PendingAdmission admission;
    admission.admission_id = id;
    admission.text = text;
    admission.status = status;
    admission.steering = steering;
    return admission;
  };

  PendingPromptTracker tracker;
  tracker.Reset({row(QStringLiteral("a"), QStringLiteral("first"),
                     QStringLiteral("queued")),
                 row(QStringLiteral("b"), QStringLiteral("second"),
                     QStringLiteral("queued"))});
  QVERIFY(tracker
              .Observe({row(QStringLiteral("a"), QStringLiteral("first"),
                            QStringLiteral("queued")),
                        row(QStringLiteral("b"), QStringLiteral("second"),
                            QStringLiteral("queued"))},
                       1000)
              .empty());

  std::vector<PendingAdmission> entered =
      tracker.Observe({row(QStringLiteral("a"), QStringLiteral("first"),
                           QStringLiteral("started")),
                       row(QStringLiteral("b"), QStringLiteral("second"),
                           QStringLiteral("queued"))},
                      1100);
  QCOMPARE(entered.size(), static_cast<std::size_t>(1));
  QCOMPARE(entered.front().admission_id, QStringLiteral("a"));
  QCOMPARE(entered.front().text, QStringLiteral("first"));
  QVERIFY(tracker
              .Observe({row(QStringLiteral("a"), QStringLiteral("first"),
                            QStringLiteral("started")),
                        row(QStringLiteral("b"), QStringLiteral("second"),
                            QStringLiteral("queued"))},
                       1200)
              .empty());

  // The snapshot can drop the row just before message.start reports the turn.
  QVERIFY(tracker
              .Observe({row(QStringLiteral("a"), QStringLiteral("first"),
                            QStringLiteral("started"))},
                       1300)
              .empty());
  const std::optional<PendingAdmission> awaiting =
      tracker.TakeNextAwaiting(1400);
  QVERIFY(awaiting.has_value());
  QCOMPARE(awaiting->admission_id, QStringLiteral("b"));
  QVERIFY(!tracker.TakeNextAwaiting(1500).has_value());

  // A row that disappeared without a turn is a cancellation; stale rows expire.
  tracker.Reset({row(QStringLiteral("c"), QStringLiteral("cancelled"),
                     QStringLiteral("queued"))});
  tracker.Observe({}, 2000);
  QVERIFY(!tracker.TakeNextAwaiting(9000).has_value());

  // An explicit cancel/replace marks the row retired so a late snapshot cannot
  // resurrect it in the transcript.
  tracker.Reset({row(QStringLiteral("d"), QStringLiteral("replaced"),
                     QStringLiteral("queued"))});
  tracker.Observe({}, 3000);
  tracker.Forget(QStringLiteral("d"));
  QVERIFY(!tracker.TakeNextAwaiting(3100).has_value());
  QVERIFY(tracker
              .Observe({row(QStringLiteral("d"), QStringLiteral("replaced"),
                            QStringLiteral("started"))},
                       3200)
              .empty());

  // A baseline row already in a turn belongs to history, not this turn.
  tracker.Reset({row(QStringLiteral("e"), QStringLiteral("baseline"),
                     QStringLiteral("started"))});
  QVERIFY(tracker
              .Observe({row(QStringLiteral("e"), QStringLiteral("baseline"),
                            QStringLiteral("started"))},
                       4000)
              .empty());

  // Steering transfers and accepted direct injections are announced once.
  tracker.Reset({row(QStringLiteral("f"), QStringLiteral("steer"),
                     QStringLiteral("queued"))});
  QCOMPARE(tracker
               .Observe({row(QStringLiteral("f"), QStringLiteral("steer"),
                             QStringLiteral("started"), true)},
                        5000)
               .size(),
           static_cast<std::size_t>(1));
  tracker.Reset({row(QStringLiteral("g"), QStringLiteral("injected"),
                     QStringLiteral("queued"))});
  QVERIFY(tracker.ClaimAnnouncement(QStringLiteral("g")));
  QVERIFY(tracker
              .Observe({row(QStringLiteral("g"), QStringLiteral("injected"),
                            QStringLiteral("started"))},
                       6000)
              .empty());
}

void GatewayProtocolTest::connectionOperationParsesTargetsAndBuildsAnswer() {
  QJsonObject token;
  token.insert(QStringLiteral("name"), QStringLiteral("GITHUB_TOKEN"));
  token.insert(QStringLiteral("prompt"), QStringLiteral("Token"));
  token.insert(QStringLiteral("required"), true);
  token.insert(QStringLiteral("secret"), true);
  QJsonObject github;
  github.insert(QStringLiteral("name"), QStringLiteral("github"));
  github.insert(QStringLiteral("kind"), QStringLiteral("mcp"));
  github.insert(QStringLiteral("action"), QStringLiteral("install"));
  github.insert(QStringLiteral("state"), QStringLiteral("pending"));
  github.insert(QStringLiteral("detail"), QStringLiteral("GitHub MCP"));
  github.insert(QStringLiteral("connect_url"),
                QStringLiteral("https://example.test/oauth"));
  github.insert(QStringLiteral("instructions"),
                QStringLiteral("Install the connector"));
  github.insert(QStringLiteral("tools"),
                QJsonArray{QStringLiteral("issues"), QStringLiteral("pulls")});
  github.insert(QStringLiteral("required_env"), QJsonArray{token});

  QJsonObject slack;
  slack.insert(QStringLiteral("name"), QStringLiteral("slack"));
  slack.insert(QStringLiteral("kind"), QStringLiteral("connector"));
  slack.insert(QStringLiteral("action"), QStringLiteral("connect"));
  slack.insert(QStringLiteral("state"), QStringLiteral("connected"));
  slack.insert(QStringLiteral("resolved"), true);

  QJsonObject display;
  display.insert(QStringLiteral("suggested"),
                 QJsonArray{QStringLiteral("github"), QStringLiteral("slack")});

  QJsonObject payload;
  payload.insert(QStringLiteral("op_id"), QStringLiteral("op-1"));
  payload.insert(QStringLiteral("tool_call_id"), QStringLiteral("call-9"));
  payload.insert(QStringLiteral("seq"), 7);
  payload.insert(QStringLiteral("deadline_at"), 1700000000.5);
  payload.insert(QStringLiteral("settled"), false);
  payload.insert(QStringLiteral("display"), display);
  payload.insert(QStringLiteral("targets"), QJsonArray{github, slack});

  const ConnectionOperation operation = ParseConnectionOperation(payload);
  QCOMPARE(operation.op_id, QStringLiteral("op-1"));
  QCOMPARE(operation.tool_call_id, QStringLiteral("call-9"));
  QCOMPARE(operation.seq, 7);
  QCOMPARE(operation.suggested.size(), 2);
  QCOMPARE(operation.targets.size(), static_cast<std::size_t>(2));
  const ConnectionTarget& parsed = operation.targets[0];
  QCOMPARE(parsed.name, QStringLiteral("github"));
  QCOMPARE(parsed.kind, QStringLiteral("mcp"));
  QCOMPARE(parsed.action, QStringLiteral("install"));
  QCOMPARE(parsed.connect_url, QStringLiteral("https://example.test/oauth"));
  QCOMPARE(parsed.tools.size(), 2);
  QCOMPARE(parsed.required_env.size(), static_cast<std::size_t>(1));
  QCOMPARE(parsed.required_env[0].name, QStringLiteral("GITHUB_TOKEN"));
  QVERIFY(parsed.required_env[0].required);
  QVERIFY(parsed.required_env[0].secret);
  QVERIFY(!parsed.resolved);
  QCOMPARE(operation.targets[1].state, QStringLiteral("connected"));
  QVERIFY(operation.targets[1].resolved);

  QJsonObject answered;
  answered.insert(QStringLiteral("name"), QStringLiteral("github"));
  answered.insert(QStringLiteral("status"), QStringLiteral("approved"));
  answered.insert(
      QStringLiteral("env"),
      QJsonObject{{QStringLiteral("GITHUB_TOKEN"), QStringLiteral("ghp_x")}});
  QJsonObject answer;
  answer.insert(QStringLiteral("targets"), QJsonArray{answered});
  const QJsonObject params =
      BuildConnectionRespondParams(QStringLiteral("op-1"), QStringLiteral("s1"),
                                   answer, QStringLiteral("ceo"));
  QCOMPARE(params.value(QStringLiteral("op_id")).toString(),
           QStringLiteral("op-1"));
  QCOMPARE(params.value(QStringLiteral("profile")).toString(),
           QStringLiteral("ceo"));
  const QJsonObject owner = params.value(QStringLiteral("owner")).toObject();
  QCOMPARE(owner.value(QStringLiteral("session_id")).toString(),
           QStringLiteral("s1"));
  QCOMPARE(owner.value(QStringLiteral("type")).toString(),
           QStringLiteral("session"));
  QCOMPARE(params.value(QStringLiteral("result")).toObject(), answer);

  payload.insert(QStringLiteral("settled"), true);
  payload.insert(QStringLiteral("settled_by"), QStringLiteral("continue"));
  const ConnectionOperation settled = ParseConnectionOperation(payload);
  QVERIFY(settled.settled);
  QCOMPARE(settled.settled_by, QStringLiteral("continue"));

  // Account-owned operations have no transcript card; the Capabilities page
  // resolves the connector's open operation, else its newest settled one.
  QJsonObject account_payload = payload;
  account_payload.insert(QStringLiteral("op_id"), QStringLiteral("op-account"));
  account_payload.insert(
      QStringLiteral("owner"),
      QJsonObject{{QStringLiteral("type"), QStringLiteral("account")}});
  account_payload.insert(QStringLiteral("seq"), 9);
  account_payload.insert(QStringLiteral("settled"), false);
  ConnectionOperation account = ParseConnectionOperation(account_payload);
  QCOMPARE(account.owner_type, QStringLiteral("account"));
  account.targets = {account.targets.front()};
  ConnectionOperation newer_settled = account;
  newer_settled.op_id = QStringLiteral("op-newer");
  newer_settled.seq = 10;
  newer_settled.settled = true;
  newer_settled.targets = {ConnectionTarget{.name = QStringLiteral("slack")}};
  newer_settled.targets[0].state = QStringLiteral("connected");
  const std::vector<ConnectionOperation> operations{newer_settled, account};
  const ConnectionOperation* open = AccountOperationFor(operations, "github");
  QVERIFY(open != nullptr);
  QCOMPARE(open->op_id, QStringLiteral("op-account"));
  const ConnectionOperation* settled_slack =
      AccountOperationFor(operations, "slack");
  QVERIFY(settled_slack != nullptr);
  QCOMPARE(settled_slack->op_id, QStringLiteral("op-newer"));
  QVERIFY(AccountOperationFor(operations, "missing") == nullptr);
}

void GatewayProtocolTest::sidebarPatchMapsOntoClosedOperations() {
  QJsonObject title;
  title.insert(QStringLiteral("title"), QStringLiteral("Renamed"));
  const SessionMutationSpec rename = SessionMutationForPatch(title);
  QCOMPARE(rename.operation, QStringLiteral("rename"));
  QCOMPARE(rename.payload.value(QStringLiteral("title")).toString(),
           QStringLiteral("Renamed"));

  QJsonObject archived;
  archived.insert(QStringLiteral("archived"), true);
  const SessionMutationSpec archive = SessionMutationForPatch(archived);
  QCOMPARE(archive.operation, QStringLiteral("archive"));
  QCOMPARE(archive.payload.value(QStringLiteral("archived")).toBool(), true);

  QJsonObject sidebar;
  sidebar.insert(QStringLiteral("pinned"), true);
  sidebar.insert(QStringLiteral("unread"), false);
  const SessionMutationSpec patch = SessionMutationForPatch(sidebar);
  QCOMPARE(patch.operation, QStringLiteral("sidebar"));
  QCOMPARE(patch.payload.value(QStringLiteral("pinned")).toBool(), true);
  QCOMPARE(patch.payload.value(QStringLiteral("unread")).toBool(), false);
}

}  // namespace hermes::ui

namespace hermes::ui {
void GatewayProtocolTest::handoffPlatformAndStatusText() {
  QCOMPARE(NormalizeHandoffPlatform(QStringLiteral(" Telegram  extra")),
           QStringLiteral("telegram"));
  QCOMPARE(NormalizeHandoffPlatform(QStringLiteral("   ")), QString());
  QCOMPARE(HandoffStatusText(QStringLiteral("pending"),
                             QStringLiteral("telegram"), QString()),
           QStringLiteral("Handoff to telegram queued…"));
  QCOMPARE(HandoffStatusText(QStringLiteral("completed"),
                             QStringLiteral("discord"), QString()),
           QStringLiteral("Handoff to discord complete"));
  QCOMPARE(
      HandoffStatusText(QStringLiteral("failed"), QStringLiteral("whatsapp"),
                        QStringLiteral("no home channel")),
      QStringLiteral("Handoff to whatsapp failed: no home channel"));
  QCOMPARE(HandoffStatusText(QString(), QStringLiteral("telegram"), QString()),
           QStringLiteral("No handoff pending"));
}
}  // namespace hermes::ui

QTEST_MAIN(hermes::ui::GatewayProtocolTest)
#include "gateway_protocol_test.moc"
