// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_MARKDOWN_H_
#define HERMES_UI_MARKDOWN_H_

#include <QString>
#include <vector>

#include "src/model.h"

namespace hermes::ui {

// Parses the markdown subset the transcript renders: headings (levels carried
// in `detail`), fenced code, bullets/numbered lists/tasks, quotes, rules,
// tables (rendered HTML in `detail`) and paragraphs. Inline spans are handled
// by InlineHtml().
std::vector<MessageBlock> ParseMarkdown(const QString& text);

// Escapes HTML and expands `code`, **bold** and *italic* spans.
QString InlineHtml(const QString& text);

}  // namespace hermes::ui

#endif  // HERMES_UI_MARKDOWN_H_
