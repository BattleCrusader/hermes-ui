// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/pet_overlay.h"

#include <QBuffer>
#include <QByteArray>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QtTest>

#include "src/pet_sprite.h"

using hermes::ui::DecodePetSprite;
using hermes::ui::PetOverlay;
using hermes::ui::PetSpriteInfo;
using hermes::ui::Theme;

namespace {

QString TinySheetBase64() {
  QImage image(4, 3, QImage::Format_ARGB32);
  image.fill(QColor(210, 90, 40));
  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  image.save(&buffer, "PNG");
  return QString::fromLatin1(bytes.toBase64());
}

QJsonObject Payload() {
  return QJsonObject{
      {QStringLiteral("enabled"), true},
      {QStringLiteral("slug"), QStringLiteral("mochi")},
      {QStringLiteral("displayName"), QStringLiteral("Mochi")},
      {QStringLiteral("mime"), QStringLiteral("image/png")},
      {QStringLiteral("spritesheetRevision"), QStringLiteral("rev-1")},
      {QStringLiteral("spritesheetBase64"), TinySheetBase64()},
      {QStringLiteral("frameW"), 2},
      {QStringLiteral("frameH"), 3},
      {QStringLiteral("framesPerState"), 6},
      {QStringLiteral("framesByState"),
       QJsonObject{{QStringLiteral("idle"), 2},
                   {QStringLiteral("running"), 1},
                   {QStringLiteral("waving"), 2}}},
      {QStringLiteral("framesByRow"),
       QJsonObject{{QStringLiteral("running"), 1}}},
      {QStringLiteral("loopMs"), 200},
      {QStringLiteral("scale"), 2.0},
      {QStringLiteral("stateRows"),
       QJsonArray{QStringLiteral("idle"), QStringLiteral("running-right"),
                  QStringLiteral("running-left"), QStringLiteral("waving"),
                  QStringLiteral("jumping"), QStringLiteral("failed"),
                  QStringLiteral("waiting"), QStringLiteral("running"),
                  QStringLiteral("review")}},
  };
}

}  // namespace

class PetOverlayTest : public QObject {
  Q_OBJECT

 private slots:
  void disabledPayloadIsClean();
  void decodesSpriteGeometryAndRows();
  void rejectsMalformedSprites();
  void overlayResolvesRunRowAndAnimates();
};

void PetOverlayTest::disabledPayloadIsClean() {
  PetSpriteInfo info;
  QString error;
  QVERIFY(DecodePetSprite(QJsonObject{{QStringLiteral("enabled"), false}},
                          &info, &error));
  QVERIFY(error.isEmpty());
  QVERIFY(!info.enabled);
  QVERIFY(!info.HasSheet());
}

void PetOverlayTest::decodesSpriteGeometryAndRows() {
  PetSpriteInfo info;
  QString error;
  QVERIFY2(DecodePetSprite(Payload(), &info, &error), qUtf8Printable(error));
  QVERIFY(info.enabled);
  QVERIFY(info.HasSheet());
  QCOMPARE(info.slug, QStringLiteral("mochi"));
  QCOMPARE(info.display_name, QStringLiteral("Mochi"));
  QCOMPARE(info.revision, QStringLiteral("rev-1"));
  QCOMPARE(info.sheet.size(), QSize(4, 3));
  QCOMPARE(info.DrawSize(), QSize(4, 6));
  QCOMPARE(info.WindowSize(), QSize(220, 260));
  QCOMPARE(info.RowNameForState(QStringLiteral("run")),
           QStringLiteral("running"));
  QCOMPARE(info.RowForState(QStringLiteral("run")), 7);
  QCOMPARE(info.FrameCountForRow(7), 1);
  QCOMPARE(info.FrameCountForState(QStringLiteral("wave")), 2);
  // A state row with no real frames falls back to idle instead of flashing
  // transparent padding (same rule as the desktop canvas renderer).
  QCOMPARE(info.FrameCountForState(QStringLiteral("review")), 2);
  QCOMPARE(info.RowForState(QStringLiteral("unknown")), 0);
}

void PetOverlayTest::rejectsMalformedSprites() {
  PetSpriteInfo info;
  QString error;
  QJsonObject payload = Payload();
  payload[QStringLiteral("spritesheetBase64")] =
      QStringLiteral("not-a-real-sheet");
  QVERIFY(!DecodePetSprite(payload, &info, &error));
  QVERIFY(!error.isEmpty());

  payload = Payload();
  payload.remove(QStringLiteral("spritesheetBase64"));
  QVERIFY(!DecodePetSprite(payload, &info, &error));
  QVERIFY(error.contains(QStringLiteral("spritesheet")));
}

void PetOverlayTest::overlayResolvesRunRowAndAnimates() {
  PetSpriteInfo info;
  QString error;
  QVERIFY(DecodePetSprite(Payload(), &info, &error));
  PetOverlay overlay(Theme::Light());
  overlay.SetSprite(info);
  QCOMPARE(overlay.PreferredSize(), info.WindowSize());
  QVERIFY(overlay.HasSprite());
  QCOMPARE(overlay.Revision(), QStringLiteral("rev-1"));
  QCOMPARE(overlay.StateForTest(), QStringLiteral("idle"));
  QCOMPARE(overlay.RowForTest(), 0);

  overlay.SetState(QStringLiteral("run"));
  QCOMPARE(overlay.StateForTest(), QStringLiteral("running"));
  QCOMPARE(overlay.RowForTest(), 7);
  QCOMPARE(overlay.FrameForTest(), 0);

  overlay.SetState(QStringLiteral("waiting"));
  QCOMPARE(overlay.RowForTest(), 6);
  QCOMPARE(overlay.FrameForTest(), 0);
  overlay.SetStatus(QStringLiteral("Waiting for you"));
  QCOMPARE(overlay.PreferredSize(), info.WindowSize());
}

QTEST_MAIN(PetOverlayTest)
#include "pet_overlay_test.moc"
