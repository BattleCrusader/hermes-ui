// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/markdown.h"

#include <QRegularExpression>
#include <QStringList>  // NOLINT(build/include_order): Qt is an "other library".
#include <algorithm>
#include <vector>

namespace hermes::ui {
namespace {

QString EscapeHtml(const QString& text) {
  QString escaped;
  escaped.reserve(text.size());
  for (const QChar character : text) {
    switch (character.unicode()) {
      case '&':
        escaped += QStringLiteral("&amp;");
        break;
      case '<':
        escaped += QStringLiteral("&lt;");
        break;
      case '>':
        escaped += QStringLiteral("&gt;");
        break;
      case '"':
        escaped += QStringLiteral("&quot;");
        break;
      default:
        escaped += character;
        break;
    }
  }
  return escaped;
}

bool IsRule(const QString& line) {
  const QString trimmed = line.trimmed();
  if (trimmed.size() < 3) {
    return false;
  }
  const QChar marker = trimmed.at(0);
  if (marker != QLatin1Char('-') && marker != QLatin1Char('*') &&
      marker != QLatin1Char('_')) {
    return false;
  }
  for (const QChar character : trimmed) {
    if (character != marker && character != QLatin1Char(' ')) {
      return false;
    }
  }
  return true;
}

QStringList TableCells(const QString& line) {
  QString trimmed = line.trimmed();
  if (trimmed.startsWith(QLatin1Char('|'))) {
    trimmed.remove(0, 1);
  }
  if (trimmed.endsWith(QLatin1Char('|'))) {
    trimmed.chop(1);
  }
  QStringList cells = trimmed.split(QLatin1Char('|'));
  for (QString& cell : cells) {
    cell = cell.trimmed();
  }
  return cells;
}

bool IsTableDelimiter(const QString& line) {
  if (!line.contains(QLatin1Char('|')) && !line.contains(QLatin1Char('-'))) {
    return false;
  }
  const QStringList cells = TableCells(line);
  if (cells.isEmpty()) {
    return false;
  }
  for (const QString& cell : cells) {
    const QString trimmed = cell.trimmed();
    if (trimmed.isEmpty()) {
      return false;
    }
    QString dashes = trimmed;
    if (dashes.startsWith(QLatin1Char(':'))) {
      dashes.remove(0, 1);
    }
    if (dashes.endsWith(QLatin1Char(':'))) {
      dashes.chop(1);
    }
    if (dashes.size() < 3) {
      return false;
    }
    for (const QChar character : dashes) {
      if (character != QLatin1Char('-')) {
        return false;
      }
    }
  }
  return true;
}

QString TableHtml(const QStringList& header, const QStringList& alignments,
                  const std::vector<QStringList>& rows) {
  QString html = QStringLiteral(
      "<table cellspacing=\"0\" cellpadding=\"0\" width=\"100%\" "
      "style=\"border-collapse:collapse;margin:2px 0 4px 0;\">");
  const auto alignment = [&alignments](int column) {
    if (column < 0 || column >= alignments.size()) {
      return QStringLiteral("left");
    }
    if (alignments.at(column) == QStringLiteral("center")) {
      return QStringLiteral("center");
    }
    if (alignments.at(column) == QStringLiteral("right")) {
      return QStringLiteral("right");
    }
    return QStringLiteral("left");
  };
  html += QStringLiteral("<tr>");
  for (int column = 0; column < header.size(); ++column) {
    html += QStringLiteral(
                "<th align=\"%1\" style=\"background-color:"
                "rgba(127,127,127,0.10);border-bottom:1px solid "
                "rgba(127,127,127,0.32);padding:6px 9px;\">%2</th>")
                .arg(alignment(column), InlineHtml(header.at(column)));
  }
  html += QStringLiteral("</tr>");
  for (const QStringList& row : rows) {
    html += QStringLiteral("<tr>");
    for (int column = 0; column < header.size(); ++column) {
      const QString cell = column < row.size() ? row.at(column) : QString();
      html += QStringLiteral(
                  "<td align=\"%1\" style=\"border-bottom:1px solid "
                  "rgba(127,127,127,0.22);padding:6px 9px;\">%2</td>")
                  .arg(alignment(column), InlineHtml(cell));
    }
    html += QStringLiteral("</tr>");
  }
  html += QStringLiteral("</table>");
  return html;
}

// A standalone image line. Returns false when the line is prose that merely
// mentions an image.
bool ParseImage(const QString& line, QString* alt, QString* source,
                QString* title) {
  const QRegularExpression markdown(QStringLiteral(
      R"(^!\[([^\]]*)\]\(\s*([^\s\)]+)(?:\s+["']([^"']*)["'])?\s*\)\s*$)"));
  const QRegularExpressionMatch match = markdown.match(line.trimmed());
  if (match.hasMatch()) {
    *alt = match.captured(1).trimmed();
    *source = match.captured(2).trimmed();
    *title = match.captured(3).trimmed();
    return !source->isEmpty();
  }
  const QRegularExpression html(QStringLiteral(R"(^<img\s+([^>]+)>\s*$)"),
                                QRegularExpression::CaseInsensitiveOption);
  const QRegularExpressionMatch html_match = html.match(line.trimmed());
  if (!html_match.hasMatch()) {
    return false;
  }
  const QRegularExpression attribute(
      QStringLiteral(R"((src|alt|title)\s*=\s*["']([^"']*)["'])"),
      QRegularExpression::CaseInsensitiveOption);
  QRegularExpressionMatchIterator attributes =
      attribute.globalMatch(html_match.captured(1));
  while (attributes.hasNext()) {
    const QRegularExpressionMatch item = attributes.next();
    const QString key = item.captured(1).toLower();
    if (key == QStringLiteral("src")) {
      *source = item.captured(2).trimmed();
    } else if (key == QStringLiteral("alt")) {
      *alt = item.captured(2).trimmed();
    } else if (key == QStringLiteral("title")) {
      *title = item.captured(2).trimmed();
    }
  }
  return !source->isEmpty();
}

QString ArtifactAttribute(const QString& text, const QString& key) {
  const QRegularExpression pattern(
      QStringLiteral(
          R"re((?:^|[\s{])%1\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s}]+)))re")
          .arg(QRegularExpression::escape(key)),
      QRegularExpression::CaseInsensitiveOption);
  const QRegularExpressionMatch match = pattern.match(text);
  if (!match.hasMatch()) {
    return QString();
  }
  for (int group = 1; group <= 3; ++group) {
    if (!match.captured(group).isEmpty()) {
      return match.captured(group).trimmed();
    }
  }
  return QString();
}

// `:::artifact path` / `:::artifact{src="path" title="chart.png"}` opens an
// embed. The body until `:::` is a caption. Returns the parsed block and the
// number of source lines consumed (including the closing marker).
int ParseArtifact(const QStringList& lines, int start, MessageBlock* block) {
  const QString trimmed = lines.at(start).trimmed();
  if (!trimmed.startsWith(QStringLiteral(":::artifact"))) {
    return 0;
  }
  QString rest = trimmed.mid(QStringLiteral(":::artifact").size()).trimmed();
  QString source = ArtifactAttribute(rest, QStringLiteral("src"));
  if (source.isEmpty()) {
    source = ArtifactAttribute(rest, QStringLiteral("path"));
  }
  QString title = ArtifactAttribute(rest, QStringLiteral("title"));
  if (title.isEmpty()) {
    title = ArtifactAttribute(rest, QStringLiteral("name"));
  }
  if (source.isEmpty()) {
    if (rest.startsWith(QLatin1Char('{'))) {
      return 0;
    }
    source = rest;
  }
  if (source.isEmpty()) {
    return 0;
  }
  QStringList caption;
  int cursor = start + 1;
  while (cursor < lines.size() &&
         lines.at(cursor).trimmed() != QStringLiteral(":::")) {
    caption.append(lines.at(cursor));
    ++cursor;
  }
  if (cursor >= lines.size()) {
    return 0;
  }
  block->kind = BlockKind::kArtifact;
  block->detail = source;
  block->language = title;
  block->text = caption.join(QLatin1Char('\n')).trimmed();
  if (block->text.isEmpty()) {
    block->text = title.isEmpty() ? source : title;
  }
  return cursor - start + 1;
}

QString AlignmentFor(const QString& cell) {
  const QString trimmed = cell.trimmed();
  const bool left = trimmed.startsWith(QLatin1Char(':'));
  const bool right = trimmed.endsWith(QLatin1Char(':'));
  if (left && right) {
    return QStringLiteral("center");
  }
  if (right) {
    return QStringLiteral("right");
  }
  return QStringLiteral("left");
}

}  // namespace

QString InlineHtml(const QString& text) {
  QString html;
  html.reserve(text.size() + 24);
  bool bold = false;
  bool italic = false;
  bool strike = false;

  for (int index = 0; index < text.size();) {
    if (text.at(index) == '`') {
      const int end = text.indexOf('`', index + 1);
      if (end > index) {
        html += QStringLiteral(
            "<span style=\"font-family:'JetBrains Mono';"
            "font-size:12px;background-color:rgba(127,127,127,0.14);"
            "padding:1px 3px;\">");
        html += EscapeHtml(text.mid(index + 1, end - index - 1));
        html += QStringLiteral("</span>");
        index = end + 1;
        continue;
      }
    }
    if (text.mid(index, 2) == QStringLiteral("**")) {
      bold = !bold;
      html += bold ? QStringLiteral("<b>") : QStringLiteral("</b>");
      index += 2;
      continue;
    }
    if (text.mid(index, 2) == QStringLiteral("~~")) {
      strike = !strike;
      html += strike ? QStringLiteral("<s>") : QStringLiteral("</s>");
      index += 2;
      continue;
    }
    if (text.at(index) == '$') {
      const int end = text.indexOf(QLatin1Char('$'), index + 1);
      if (end > index + 1) {
        const QString formula = text.mid(index + 1, end - index - 1);
        if (!formula.trimmed().isEmpty() &&
            !formula.contains(QLatin1Char('\n'))) {
          html += QStringLiteral(
                      "<span style=\"font-family:'JetBrains Mono';"
                      "font-style:italic;background-color:rgba(127,127,127,"
                      "0.12);\">%1</span>")
                      .arg(EscapeHtml(formula));
          index = end + 1;
          continue;
        }
      }
    }
    if (text.at(index) == '[') {
      const int close = text.indexOf(QStringLiteral("]("), index + 1);
      if (close > index) {
        const int end = text.indexOf(QLatin1Char(')'), close + 2);
        if (end > close) {
          const QString label = text.mid(index + 1, close - index - 1);
          const QString url = text.mid(close + 2, end - close - 2).trimmed();
          if (!url.isEmpty()) {
            html += QStringLiteral(
                        "<a href=\"%1\" style=\"color:#4C8DFF;"
                        "text-decoration:none;\">%2</a>")
                        .arg(EscapeHtml(url), InlineHtml(label));
            index = end + 1;
            continue;
          }
        }
      }
    }
    if (text.at(index) == '[') {
      const int close = text.indexOf(QLatin1Char(']'), index + 1);
      if (close > index) {
        const QString citation = text.mid(index + 1, close - index - 1);
        const bool numeric = !citation.isEmpty() &&
                             std::all_of(citation.cbegin(), citation.cend(),
                                         [](QChar ch) { return ch.isDigit(); });
        if (numeric || citation.startsWith(QLatin1Char('@')) ||
            citation.startsWith(QLatin1Char('^'))) {
          QString label = citation;
          if (label.startsWith(QLatin1Char('^')) ||
              label.startsWith(QLatin1Char('@'))) {
            label.remove(0, 1);
          }
          html += QStringLiteral(
                      "<sup><a href=\"citation:%1\" style=\"color:#4C8DFF;"
                      "text-decoration:none;\">[%2]</a></sup>")
                      .arg(EscapeHtml(citation), EscapeHtml(label));
          index = close + 1;
          continue;
        }
      }
    }
    if (text.at(index) == '*') {
      italic = !italic;
      html += italic ? QStringLiteral("<i>") : QStringLiteral("</i>");
      ++index;
      continue;
    }
    html += EscapeHtml(QString(text.at(index)));
    ++index;
  }
  // Never leave unclosed emphasis behind when a stream ends mid-token.
  if (bold) {
    html += QStringLiteral("</b>");
  }
  if (italic) {
    html += QStringLiteral("</i>");
  }
  if (strike) {
    html += QStringLiteral("</s>");
  }
  return html;
}

std::vector<MessageBlock> ParseMarkdown(const QString& text) {
  std::vector<MessageBlock> blocks;
  const QStringList lines = text.split('\n');
  QString paragraph;
  QString code;
  QString language;
  bool in_code = false;

  const auto flush_paragraph = [&blocks, &paragraph]() {
    const QString trimmed = paragraph.trimmed();
    if (!trimmed.isEmpty()) {
      MessageBlock block;
      block.kind = BlockKind::kParagraph;
      block.text = trimmed;
      blocks.push_back(block);
    }
    paragraph.clear();
  };

  for (int line_index = 0; line_index < lines.size(); ++line_index) {
    const QString& line = lines.at(line_index);
    if (!in_code) {
      MessageBlock artifact;
      const int consumed = ParseArtifact(lines, line_index, &artifact);
      if (consumed > 0) {
        flush_paragraph();
        blocks.push_back(artifact);
        line_index += consumed - 1;
        continue;
      }
    }
    if (line.startsWith(QStringLiteral("```"))) {
      if (!in_code) {
        flush_paragraph();
        in_code = true;
        language = line.mid(3).trimmed();
        code.clear();
      } else {
        MessageBlock block;
        const QString normalized = language.toLower();
        block.kind = BlockKind::kCode;
        if (normalized == QStringLiteral("diff")) {
          block.kind = BlockKind::kDiff;
        } else if (normalized == QStringLiteral("math") ||
                   normalized == QStringLiteral("latex") ||
                   normalized == QStringLiteral("tex")) {
          block.kind = BlockKind::kMath;
        } else if (normalized == QStringLiteral("mermaid")) {
          block.kind = BlockKind::kMermaid;
        }
        block.language = language.isEmpty() ? QStringLiteral("text") : language;
        block.text = code;
        blocks.push_back(block);
        in_code = false;
      }
      continue;
    }
    if (in_code) {
      if (!code.isEmpty()) {
        code += '\n';
      }
      code += line;
      continue;
    }
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty()) {
      flush_paragraph();
      continue;
    }

    if (trimmed.startsWith(QStringLiteral("$$"))) {
      flush_paragraph();
      const QString one_line = trimmed.mid(2);
      if (one_line.endsWith(QStringLiteral("$$")) && one_line.size() > 2) {
        MessageBlock block;
        block.kind = BlockKind::kMath;
        block.language = QStringLiteral("math");
        block.text = one_line.left(one_line.size() - 2).trimmed();
        blocks.push_back(block);
        continue;
      }
      QString math = one_line;
      int cursor = line_index + 1;
      while (cursor < lines.size() &&
             !lines.at(cursor).trimmed().endsWith(QStringLiteral("$$"))) {
        if (!math.isEmpty()) {
          math += '\n';
        }
        math += lines.at(cursor);
        ++cursor;
      }
      if (cursor < lines.size()) {
        const QString tail = lines.at(cursor).trimmed();
        const QString body =
            tail.left(std::max<qsizetype>(0, tail.size() - 2)).trimmed();
        if (!body.isEmpty()) {
          if (!math.isEmpty()) {
            math += '\n';
          }
          math += body;
        }
        MessageBlock block;
        block.kind = BlockKind::kMath;
        block.language = QStringLiteral("math");
        block.text = math.trimmed();
        blocks.push_back(block);
        line_index = cursor;
        continue;
      }
      // No closing fence: fall through and let the line render normally.
    }

    QString image_alt;
    QString image_source;
    QString image_title;
    if (ParseImage(line, &image_alt, &image_source, &image_title)) {
      flush_paragraph();
      MessageBlock block;
      block.kind = BlockKind::kImage;
      block.text = image_alt;
      block.detail = image_source;
      block.language = image_title;
      blocks.push_back(block);
      continue;
    }

    const QRegularExpression footnote(
        QStringLiteral(R"(^\[\^([^\]]+)\]:\s*(.*)$)"));
    const QRegularExpressionMatch footnote_match = footnote.match(trimmed);
    if (footnote_match.hasMatch()) {
      flush_paragraph();
      MessageBlock block;
      block.kind = BlockKind::kFootnote;
      block.detail = footnote_match.captured(1).trimmed();
      block.text = footnote_match.captured(2).trimmed();
      blocks.push_back(block);
      continue;
    }

    // A table is recognised by its delimiter row; without it this is just a
    // paragraph that happens to contain pipes.
    if (trimmed.contains(QLatin1Char('|')) && line_index + 1 < lines.size() &&
        IsTableDelimiter(lines.at(line_index + 1))) {
      flush_paragraph();
      const QStringList header = TableCells(trimmed);
      const QStringList delimiter = TableCells(lines.at(line_index + 1));
      QStringList alignments;
      alignments.reserve(delimiter.size());
      for (const QString& cell : delimiter) {
        alignments.append(AlignmentFor(cell));
      }
      std::vector<QStringList> rows;
      QStringList source;
      source.append(trimmed);
      source.append(lines.at(line_index + 1).trimmed());
      int cursor = line_index + 2;
      while (cursor < lines.size() &&
             lines.at(cursor).contains(QLatin1Char('|')) &&
             !lines.at(cursor).trimmed().isEmpty()) {
        source.append(lines.at(cursor).trimmed());
        rows.push_back(TableCells(lines.at(cursor)));
        ++cursor;
      }
      MessageBlock block;
      block.kind = BlockKind::kTable;
      block.text = source.join(QLatin1Char('\n'));
      block.detail = TableHtml(header, alignments, rows);
      blocks.push_back(block);
      line_index = cursor - 1;
      continue;
    }

    if (IsRule(trimmed)) {
      flush_paragraph();
      MessageBlock block;
      block.kind = BlockKind::kRule;
      blocks.push_back(block);
      continue;
    }

    if (trimmed.startsWith(QLatin1Char('>'))) {
      flush_paragraph();
      MessageBlock block;
      block.kind = BlockKind::kQuote;
      block.text = trimmed.mid(1).trimmed();
      blocks.push_back(block);
      continue;
    }

    const QRegularExpression heading(QStringLiteral("^(#{1,6})\\s+(.*)$"));
    const QRegularExpressionMatch heading_match = heading.match(trimmed);
    if (heading_match.hasMatch()) {
      flush_paragraph();
      MessageBlock block;
      block.kind = BlockKind::kHeading;
      block.detail = QString::number(heading_match.captured(1).size());
      block.text = heading_match.captured(2).trimmed();
      blocks.push_back(block);
      continue;
    }

    const QRegularExpression task(
        QStringLiteral("^[-*+]\\s+\\[([ xX])\\]\\s+(.*)$"));
    const QRegularExpressionMatch task_match = task.match(trimmed);
    if (task_match.hasMatch()) {
      flush_paragraph();
      MessageBlock block;
      block.kind = BlockKind::kBullet;
      block.detail = task_match.captured(1).trimmed().isEmpty()
                         ? QStringLiteral("task:open")
                         : QStringLiteral("task:done");
      block.text = task_match.captured(2).trimmed();
      blocks.push_back(block);
      continue;
    }

    const QRegularExpression numbered(
        QStringLiteral("^(\\d{1,3})[.)]\\s+(.*)$"));
    const QRegularExpressionMatch numbered_match = numbered.match(trimmed);
    if (numbered_match.hasMatch()) {
      flush_paragraph();
      MessageBlock block;
      block.kind = BlockKind::kBullet;
      block.detail = numbered_match.captured(1) + QStringLiteral(".");
      block.text = numbered_match.captured(2).trimmed();
      blocks.push_back(block);
      continue;
    }

    if (trimmed.startsWith(QStringLiteral("- ")) ||
        trimmed.startsWith(QStringLiteral("* ")) ||
        trimmed.startsWith(QStringLiteral("+ "))) {
      flush_paragraph();
      MessageBlock block;
      block.kind = BlockKind::kBullet;
      block.text = trimmed.mid(2).trimmed();
      blocks.push_back(block);
      continue;
    }

    if (!paragraph.isEmpty()) {
      paragraph += '\n';
    }
    paragraph += line;
  }

  flush_paragraph();
  if (in_code && !code.isEmpty()) {
    MessageBlock block;
    const QString normalized = language.toLower();
    block.kind = BlockKind::kCode;
    if (normalized == QStringLiteral("diff")) {
      block.kind = BlockKind::kDiff;
    } else if (normalized == QStringLiteral("math") ||
               normalized == QStringLiteral("latex") ||
               normalized == QStringLiteral("tex")) {
      block.kind = BlockKind::kMath;
    } else if (normalized == QStringLiteral("mermaid")) {
      block.kind = BlockKind::kMermaid;
    }
    block.language = language.isEmpty() ? QStringLiteral("text") : language;
    block.text = code;
    blocks.push_back(block);
  }
  return blocks;
}

}  // namespace hermes::ui
