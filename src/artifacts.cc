// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/artifacts.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace hermes::ui {
namespace {

constexpr int kHtmlDocMinChars = 160;
constexpr int kHtmlFragmentMinChars = 1200;
constexpr int kSvgMinChars = 2000;
constexpr int kCodeMinLines = 48;
constexpr int kCodeMinChars = 3000;
constexpr int kMaxArtifactsPerSession = 24;
constexpr int kMaxVersionsPerArtifact = 20;
constexpr qint64 kMaxRegistryUnits = 4 * 1024 * 1024;

const QSet<QString>& NonArtifactLanguages() {
  static const QSet<QString> languages = {QString(),
                                          QStringLiteral("console"),
                                          QStringLiteral("diff"),
                                          QStringLiteral("listing"),
                                          QStringLiteral("log"),
                                          QStringLiteral("logs"),
                                          QStringLiteral("markdown"),
                                          QStringLiteral("md"),
                                          QStringLiteral("mermaid"),
                                          QStringLiteral("output"),
                                          QStringLiteral("patch"),
                                          QStringLiteral("plain"),
                                          QStringLiteral("plaintext"),
                                          QStringLiteral("shell-session"),
                                          QStringLiteral("stdout"),
                                          QStringLiteral("text"),
                                          QStringLiteral("txt")};
  return languages;
}

int CountLines(const QString& text) {
  return text.count(QLatin1Char('\n')) + 1;
}

QString StripTags(const QString& value) {
  static const QRegularExpression tag(QStringLiteral("<[^>]*>"));
  QString stripped = value;
  stripped.replace(tag, QStringLiteral(" "));
  return stripped.simplified();
}

QString TitleFromTag(const QString& content, const QString& tag) {
  const QRegularExpression expression(
      QStringLiteral("<%1[^>]*>([\\s\\S]*?)</%1>").arg(tag),
      QRegularExpression::CaseInsensitiveOption);
  const QRegularExpressionMatch match = expression.match(content);
  if (!match.hasMatch()) {
    return QString();
  }
  return StripTags(match.captured(1)).left(80);
}

QString SanitizeLanguage(const QString& language) {
  static const QRegularExpression valid(
      QStringLiteral("^[A-Za-z0-9][A-Za-z0-9+#-]*$"));
  const QString first = language.trimmed()
                            .split(QRegularExpression(QStringLiteral("\\s+")))
                            .value(0);
  if (first.isEmpty() || first.size() > 16 || !valid.match(first).hasMatch()) {
    return QString();
  }
  return first.toLower();
}

QString CodeTitle(const QString& language, const QString& content) {
  const QString head = content.left(2000);
  static const QRegularExpression filename(
      QStringLiteral(
          "^\\s*(?://|#|--|<!--|/\\*)\\s*([\\w./-]+\\.[a-z0-9]{1,8})\\b"),
      QRegularExpression::CaseInsensitiveOption);
  const QRegularExpressionMatch file_match = filename.match(head);
  if (file_match.hasMatch()) {
    return file_match.captured(1);
  }
  static const QRegularExpression declaration(QStringLiteral(
      "(?:^|\\n)\\s*(?:export\\s+)?(?:default\\s+)?(?:async\\s+)?"
      "(?:function|class|struct|interface|enum|trait|impl|def|fn)"
      "\\s+([A-Za-z_$][\\w$]*)"));
  const QRegularExpressionMatch declaration_match = declaration.match(head);
  if (declaration_match.hasMatch()) {
    return declaration_match.captured(1);
  }
  return language;
}

QString Base36(quint32 value) {
  static const QString digits =
      QStringLiteral("0123456789abcdefghijklmnopqrstuvwxyz");
  if (value == 0) {
    return QStringLiteral("0");
  }
  QString out;
  while (value > 0) {
    out.prepend(digits.at(static_cast<int>(value % 36)));
    value /= 36;
  }
  return out;
}

QString DownloadExtension(const QString& language) {
  static const QHash<QString, QString> extensions = {
      {QStringLiteral("bash"), QStringLiteral(".sh")},
      {QStringLiteral("c"), QStringLiteral(".c")},
      {QStringLiteral("cpp"), QStringLiteral(".cpp")},
      {QStringLiteral("csharp"), QStringLiteral(".cs")},
      {QStringLiteral("css"), QStringLiteral(".css")},
      {QStringLiteral("go"), QStringLiteral(".go")},
      {QStringLiteral("htm"), QStringLiteral(".html")},
      {QStringLiteral("html"), QStringLiteral(".html")},
      {QStringLiteral("java"), QStringLiteral(".java")},
      {QStringLiteral("javascript"), QStringLiteral(".js")},
      {QStringLiteral("js"), QStringLiteral(".js")},
      {QStringLiteral("json"), QStringLiteral(".json")},
      {QStringLiteral("jsx"), QStringLiteral(".jsx")},
      {QStringLiteral("kotlin"), QStringLiteral(".kt")},
      {QStringLiteral("php"), QStringLiteral(".php")},
      {QStringLiteral("py"), QStringLiteral(".py")},
      {QStringLiteral("python"), QStringLiteral(".py")},
      {QStringLiteral("rb"), QStringLiteral(".rb")},
      {QStringLiteral("rs"), QStringLiteral(".rs")},
      {QStringLiteral("ruby"), QStringLiteral(".rb")},
      {QStringLiteral("rust"), QStringLiteral(".rs")},
      {QStringLiteral("sh"), QStringLiteral(".sh")},
      {QStringLiteral("sql"), QStringLiteral(".sql")},
      {QStringLiteral("svg"), QStringLiteral(".svg")},
      {QStringLiteral("swift"), QStringLiteral(".swift")},
      {QStringLiteral("toml"), QStringLiteral(".toml")},
      {QStringLiteral("ts"), QStringLiteral(".ts")},
      {QStringLiteral("tsx"), QStringLiteral(".tsx")},
      {QStringLiteral("typescript"), QStringLiteral(".ts")},
      {QStringLiteral("xhtml"), QStringLiteral(".html")},
      {QStringLiteral("xml"), QStringLiteral(".xml")},
      {QStringLiteral("yaml"), QStringLiteral(".yaml")},
      {QStringLiteral("yml"), QStringLiteral(".yaml")},
  };
  return extensions.value(language, QStringLiteral(".txt"));
}

qint64 ContentUnits(const std::vector<ArtifactRecord>& records) {
  qint64 total = 0;
  for (const ArtifactRecord& record : records) {
    for (const ArtifactVersion& version : record.versions) {
      total += version.content.size();
    }
  }
  return total;
}

void PruneRegistry(std::vector<ArtifactRecord>* records) {
  while (records->size() > kMaxArtifactsPerSession) {
    auto oldest = std::min_element(
        records->begin(), records->end(),
        [](const ArtifactRecord& left, const ArtifactRecord& right) {
          return left.updated_at_ms < right.updated_at_ms;
        });
    records->erase(oldest);
  }

  while (ContentUnits(*records) > kMaxRegistryUnits) {
    ArtifactRecord* oldest_record = nullptr;
    std::size_t oldest_version = 0;
    qint64 oldest_stamp = 0;
    for (ArtifactRecord& record : *records) {
      // Every record keeps its newest version; only history is evicted here.
      for (std::size_t index = 0; index + 1 < record.versions.size(); ++index) {
        const qint64 stamp = record.versions[index].created_at_ms;
        if (oldest_record == nullptr || stamp < oldest_stamp) {
          oldest_record = &record;
          oldest_version = index;
          oldest_stamp = stamp;
        }
      }
    }
    if (oldest_record != nullptr) {
      oldest_record->versions.erase(
          oldest_record->versions.begin() +
          static_cast<std::ptrdiff_t>(oldest_version));
      continue;
    }
    if (records->size() <= 1) {
      break;
    }
    auto oldest = std::min_element(
        records->begin(), records->end(),
        [](const ArtifactRecord& left, const ArtifactRecord& right) {
          return left.updated_at_ms < right.updated_at_ms;
        });
    records->erase(oldest);
  }
}

}  // namespace

std::optional<ArtifactDetection> DetectArtifact(const QString& language,
                                                const QString& code) {
  const QString trimmed = code.trimmed();
  if (trimmed.isEmpty()) {
    return std::nullopt;
  }
  const QString clean = SanitizeLanguage(language);
  static const QRegularExpression html_doc(
      QStringLiteral("<!doctype\\s+html|<html[\\s>]|<head[\\s>]|<body[\\s>]"),
      QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression html_tag(
      QStringLiteral("<[a-z][a-z0-9-]*(\\s[^>]*)?>"),
      QRegularExpression::CaseInsensitiveOption);

  if (clean == QStringLiteral("html") || clean == QStringLiteral("htm") ||
      clean == QStringLiteral("xhtml")) {
    const bool document = html_doc.match(trimmed).hasMatch();
    if ((document && trimmed.size() >= kHtmlDocMinChars) ||
        (!document && trimmed.size() >= kHtmlFragmentMinChars &&
         html_tag.match(trimmed).hasMatch())) {
      QString title = TitleFromTag(trimmed, QStringLiteral("title"));
      if (title.isEmpty()) {
        title = TitleFromTag(trimmed, QStringLiteral("h1"));
      }
      if (title.isEmpty()) {
        title = QStringLiteral("HTML");
      }
      return ArtifactDetection{ArtifactKind::kHtml, clean, title};
    }
    return std::nullopt;
  }

  if (clean == QStringLiteral("svg")) {
    static const QRegularExpression svg_tag(
        QStringLiteral("<svg[\\s>]"),
        QRegularExpression::CaseInsensitiveOption);
    if (trimmed.size() >= kSvgMinChars && svg_tag.match(trimmed).hasMatch()) {
      QString title = TitleFromTag(trimmed, QStringLiteral("title"));
      if (title.isEmpty()) {
        title = QStringLiteral("SVG");
      }
      return ArtifactDetection{ArtifactKind::kSvg, clean, title};
    }
    return std::nullopt;
  }

  if (NonArtifactLanguages().contains(clean)) {
    return std::nullopt;
  }
  if (trimmed.size() < kCodeMinChars && CountLines(trimmed) < kCodeMinLines) {
    return std::nullopt;
  }
  return ArtifactDetection{ArtifactKind::kCode, clean,
                           CodeTitle(clean, trimmed)};
}

QString ArtifactKindName(ArtifactKind kind) {
  switch (kind) {
    case ArtifactKind::kHtml:
      return QStringLiteral("html");
    case ArtifactKind::kSvg:
      return QStringLiteral("svg");
    case ArtifactKind::kCode:
      break;
  }
  return QStringLiteral("code");
}

QString ArtifactSlug(const ArtifactDetection& detection) {
  static const QRegularExpression invalid(QStringLiteral("[^\\p{L}\\p{N}]+"));
  QString title = detection.title.toLower();
  title.replace(invalid, QStringLiteral("-"));
  while (title.startsWith(QLatin1Char('-'))) {
    title.remove(0, 1);
  }
  while (title.endsWith(QLatin1Char('-'))) {
    title.chop(1);
  }
  title = title.left(48);
  if (title.isEmpty()) {
    title = QStringLiteral("untitled");
  }
  return QStringLiteral("%1:%2:%3")
      .arg(ArtifactKindName(detection.kind), detection.language, title);
}

QString ArtifactContentHash(const QString& content) {
  quint32 hash = 0x811c9dc5u;
  const ushort* units = content.utf16();
  for (int index = 0; index < content.size(); ++index) {
    hash ^= static_cast<quint32>(units[index]);
    hash *= 0x01000193u;
  }
  return Base36(hash);
}

QString ArtifactDownloadName(ArtifactKind kind, const QString& language,
                             const QString& title) {
  static const QRegularExpression invalid(
      QStringLiteral("[^\\p{L}\\p{N}._ -]+"));
  static const QRegularExpression extension(
      QStringLiteral("\\.[a-z0-9]{1,8}$"),
      QRegularExpression::CaseInsensitiveOption);
  QString base = title;
  base.replace(invalid, QString());
  base = base.trimmed();
  base.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral("-"));
  base = base.left(60);
  if (base.isEmpty()) {
    base = QStringLiteral("artifact");
  }
  if (extension.match(base).hasMatch()) {
    return base;
  }
  if (kind == ArtifactKind::kHtml) {
    return base + QStringLiteral(".html");
  }
  if (kind == ArtifactKind::kSvg) {
    return base + QStringLiteral(".svg");
  }
  return base + DownloadExtension(language);
}

std::vector<ArtifactRecord> CollectGeneratedArtifacts(
    const std::vector<ChatMessage>& messages, const QString& session_id) {
  std::vector<ArtifactRecord> records;
  QHash<QString, int> index_by_slug;
  QString session = session_id.trimmed().isEmpty() ? QStringLiteral("session")
                                                   : session_id.trimmed();

  for (std::size_t message_index = 0; message_index < messages.size();
       ++message_index) {
    const ChatMessage& message = messages[message_index];
    if (message.role != MessageRole::kAssistant) {
      continue;
    }
    const qint64 base_stamp =
        message.timestamp.isValid()
            ? message.timestamp.toMSecsSinceEpoch()
            : static_cast<qint64>(message_index + 1) * 1000000;
    for (std::size_t block_index = 0; block_index < message.blocks.size();
         ++block_index) {
      const MessageBlock& block = message.blocks[block_index];
      if (block.kind != BlockKind::kCode) {
        continue;
      }
      const std::optional<ArtifactDetection> detection =
          DetectArtifact(block.language, block.text);
      if (!detection.has_value()) {
        continue;
      }
      const QString content = block.text.trimmed();
      const QString slug = ArtifactSlug(*detection);
      const QString hash = ArtifactContentHash(content);
      const qint64 stamp = base_stamp + static_cast<qint64>(block_index);
      const int existing = index_by_slug.value(slug, -1);
      if (existing >= 0) {
        ArtifactRecord& record = records[static_cast<std::size_t>(existing)];
        const bool known =
            std::any_of(record.versions.begin(), record.versions.end(),
                        [&hash](const ArtifactVersion& version) {
                          return version.hash == hash;
                        });
        if (known) {
          continue;
        }
        record.versions.push_back(ArtifactVersion{content, stamp, hash});
        if (record.versions.size() >
            static_cast<std::size_t>(kMaxVersionsPerArtifact)) {
          record.versions.erase(record.versions.begin());
        }
        if (!detection->title.isEmpty()) {
          record.title = detection->title;
        }
        record.updated_at_ms = stamp;
        continue;
      }

      ArtifactRecord record;
      record.id = session + QLatin1Char(':') + slug;
      record.session_id = session;
      record.slug = slug;
      record.title = detection->title;
      record.language = detection->language;
      record.kind = detection->kind;
      record.created_at_ms = stamp;
      record.updated_at_ms = stamp;
      record.versions.push_back(ArtifactVersion{content, stamp, hash});
      index_by_slug.insert(slug, static_cast<int>(records.size()));
      records.push_back(std::move(record));
    }
  }

  PruneRegistry(&records);
  std::stable_sort(records.begin(), records.end(),
                   [](const ArtifactRecord& left, const ArtifactRecord& right) {
                     return left.updated_at_ms > right.updated_at_ms;
                   });
  return records;
}

}  // namespace hermes::ui
