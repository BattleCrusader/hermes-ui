// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_MARKDOWN_PREVIEW_STYLE_H_
#define HERMES_UI_MARKDOWN_PREVIEW_STYLE_H_

#include <QColor>
#include <QFont>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFormat>
#include <QTextFrame>
#include <QTextTable>
#include <functional>

#include "src/theme.h"

namespace hermes::ui {

// QTextDocument's Markdown importer writes a 9 pt "Sans Serif" body and
// grid-style tables into the document. Override both so README/TODO/project
// previews read like the transcript instead of a tiny unstyled paste.
inline void StyleMarkdownPreview(QTextBrowser* browser, const Theme& theme) {
  if (browser == nullptr || browser->document() == nullptr) {
    return;
  }
  QTextDocument* document = browser->document();
  document->setDefaultFont(theme.UiFont(13, QFont::Normal));
  document->setDocumentMargin(12);
  QTextOption option = document->defaultTextOption();
  option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
  document->setDefaultTextOption(option);
  const QColor header_background = Mix(theme.surface, theme.foreground, 0.06);
  const QColor quote_background = Mix(theme.surface, theme.foreground, 0.055);
  for (QTextBlock block = document->begin(); block.isValid();
       block = block.next()) {
    QTextBlockFormat format = block.blockFormat();
    if (format.intProperty(QTextFormat::BlockQuoteLevel) <= 0) {
      continue;
    }
    format.setLeftMargin(14);
    format.setRightMargin(12);
    format.setTopMargin(6);
    format.setBottomMargin(6);
    format.setBackground(quote_background);
    QTextCursor cursor(block);
    cursor.setBlockFormat(format);
  }
  const std::function<void(QTextFrame*)> visit_frame = [&](QTextFrame* frame) {
    if (frame == nullptr) {
      return;
    }
    for (QTextFrame::iterator it = frame->begin(); !it.atEnd(); ++it) {
      QTextTable* table = qobject_cast<QTextTable*>(it.currentFrame());
      if (table == nullptr) {
        visit_frame(it.currentFrame());
        continue;
      }
      QTextTableFormat format = table->format();
      format.setBorder(0);
      format.setCellSpacing(0);
      format.setCellPadding(7);
      format.setWidth(QTextLength(QTextLength::PercentageLength, 100));
      table->setFormat(format);
      for (int row = 0; row < table->rows(); ++row) {
        for (int column = 0; column < table->columns(); ++column) {
          QTextTableCell cell = table->cellAt(row, column);
          QTextTableCellFormat cell_format = cell.format().toTableCellFormat();
          cell_format.setLeftBorder(0);
          cell_format.setRightBorder(0);
          cell_format.setTopBorder(0);
          cell_format.setBottomBorder(1);
          cell_format.setBottomBorderStyle(QTextFrameFormat::BorderStyle_Solid);
          cell_format.setBottomBorderBrush(row == 0 ? theme.stroke_strong
                                                    : theme.stroke);
          if (row == 0) {
            cell_format.setBackground(header_background);
          }
          cell.setFormat(cell_format);
        }
      }
    }
  };
  visit_frame(document->rootFrame());
}

}  // namespace hermes::ui

#endif  // HERMES_UI_MARKDOWN_PREVIEW_STYLE_H_
