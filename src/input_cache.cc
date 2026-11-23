// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/input_cache.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSaveFile>
#include <QSet>
#include <QTextCursor>
#include <QTextEdit>
#include <QVariant>
#include <QWidget>
#include <initializer_list>

#include "src/cache_store.h"

namespace hermes::ui {
namespace {

constexpr int kDraftFileVersion = 1;
constexpr int kCompactAfterRecords = 512;
constexpr qint64 kCompactAfterBytes = 1024 * 1024;

QString DraftsPath() {
  return QDir(UiCacheRoot()).filePath(QStringLiteral("input-drafts.json"));
}

QString JournalPath() {
  return QDir(UiCacheRoot()).filePath(QStringLiteral("input-drafts.journal"));
}

QString ClassName(const QObject* object) {
  return QString::fromLatin1(object->metaObject()->className());
}

QString WidgetSegment(QWidget* widget) {
  QString name = widget->objectName();
  QWidget* parent = widget->parentWidget();
  int count = 0;
  int ordinal = 0;
  if (parent != nullptr) {
    const QString class_name = ClassName(widget);
    for (QObject* child : parent->children()) {
      if (child->isWidgetType() && ClassName(child) == class_name) {
        ++count;
        if (child == widget) {
          ordinal = count;
        }
      }
    }
  }
  if (name.isEmpty()) {
    name = ClassName(widget);
    if (widget->isWindow() && !widget->windowTitle().trimmed().isEmpty()) {
      name += QLatin1Char(':') + widget->windowTitle().trimmed();
    }
  }
  // Dynamic forms often reuse an object name for a row control. The ordinal is
  // only added when it is needed, so ordinary names stay readable and stable.
  if (count > 1) {
    name += QStringLiteral("#%1").arg(ordinal);
  }
  return name;
}

QString ScopeFor(QWidget* widget) {
  for (QWidget* current = widget; current != nullptr;
       current = current->parentWidget()) {
    const QVariant scope = current->property("draftScope");
    if (scope.isValid() && !scope.toString().isEmpty()) {
      return scope.toString();
    }
  }
  return QString();
}

QString KeyFor(QWidget* widget) {
  QStringList parts;
  for (QWidget* current = widget; current != nullptr;
       current = current->parentWidget()) {
    parts.prepend(WidgetSegment(current));
    if (current->isWindow()) {
      break;
    }
  }
  const QString scope = ScopeFor(widget);
  if (!scope.isEmpty()) {
    parts.prepend(QStringLiteral("scope=%1").arg(scope));
  }
  return parts.join(QLatin1Char('/'));
}

bool IsSearchField(const QWidget* widget) {
  return widget->objectName().contains(QStringLiteral("search"),
                                       Qt::CaseInsensitive);
}

bool IsEligible(QWidget* widget) {
  if (widget == nullptr || widget->property("noDraftCache").toBool()) {
    return false;
  }
  if (widget->objectName().startsWith(QStringLiteral("qt_"))) {
    return false;
  }
  if (qobject_cast<QComboBox*>(widget->parentWidget()) != nullptr) {
    return false;
  }
  if (qobject_cast<QFileDialog*>(widget->window()) != nullptr) {
    return false;
  }
  if (IsSearchField(widget)) {
    return false;
  }
  if (auto* line = qobject_cast<QLineEdit*>(widget)) {
    return !line->isReadOnly() &&
           line->echoMode() == QLineEdit::Normal &&
           (line->inputMethodHints() & Qt::ImhHiddenText) == 0;
  }
  if (auto* plain = qobject_cast<QPlainTextEdit*>(widget)) {
    return !plain->isReadOnly();
  }
  if (auto* text = qobject_cast<QTextEdit*>(widget)) {
    return !text->isReadOnly();
  }
  return false;
}

QString CurrentText(QWidget* widget) {
  if (auto* line = qobject_cast<QLineEdit*>(widget)) {
    return line->text();
  }
  if (auto* plain = qobject_cast<QPlainTextEdit*>(widget)) {
    return plain->toPlainText();
  }
  if (auto* text = qobject_cast<QTextEdit*>(widget)) {
    return text->toPlainText();
  }
  return QString();
}

void SetWidgetText(QWidget* widget, const QString& text) {
  if (auto* line = qobject_cast<QLineEdit*>(widget)) {
    line->setText(text);
    line->setCursorPosition(line->text().size());
    return;
  }
  if (auto* plain = qobject_cast<QPlainTextEdit*>(widget)) {
    plain->setPlainText(text);
    QTextCursor cursor = plain->textCursor();
    cursor.movePosition(QTextCursor::End);
    plain->setTextCursor(cursor);
    return;
  }
  if (auto* text_edit = qobject_cast<QTextEdit*>(widget)) {
    text_edit->setPlainText(text);
    QTextCursor cursor = text_edit->textCursor();
    cursor.movePosition(QTextCursor::End);
    text_edit->setTextCursor(cursor);
  }
}

class InputDraftStore : public QObject {
 public:
  InputDraftStore() = default;
  ~InputDraftStore() override {
    if (journal_.isOpen()) {
      journal_.close();
    }
  }

  QString Value(const QString& key) {
    EnsureLoaded();
    return drafts_.value(key);
  }

  void Store(const QString& key, const QString& text) {
    if (key.isEmpty()) {
      return;
    }
    EnsureLoaded();
    const auto found = drafts_.constFind(key);
    if (text.isEmpty()) {
      if (found == drafts_.constEnd()) {
        return;
      }
      drafts_.remove(key);
    } else {
      if (found != drafts_.constEnd() && found.value() == text) {
        return;
      }
      drafts_.insert(key, text);
    }
    AppendRecord(key, text);
  }

  void Flush() {
    if (loaded_root_.isEmpty()) {
      return;
    }
    if (journal_.isOpen()) {
      journal_.close();
    }
    QDir directory(UiCacheRoot());
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
      return;
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), kDraftFileVersion);
    QJsonObject values;
    for (auto it = drafts_.constBegin(); it != drafts_.constEnd(); ++it) {
      values.insert(it.key(), it.value());
    }
    root.insert(QStringLiteral("drafts"), values);
    QSaveFile file(DraftsPath());
    if (!file.open(QIODevice::WriteOnly)) {
      return;
    }
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
      return;
    }
    QFile journal(JournalPath());
    if (journal.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      journal.close();
    }
    journal_records_ = 0;
    journal_bytes_ = 0;
  }

  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::Destroy) {
      watched_.remove(watched);
      edited_.remove(watched);
      return QObject::eventFilter(watched, event);
    }
    auto* widget = qobject_cast<QWidget*>(watched);
    if (widget == nullptr) {
      return QObject::eventFilter(watched, event);
    }
    if (event->type() == QEvent::Show) {
      Watch(widget);
    } else if (event->type() == QEvent::Hide ||
               event->type() == QEvent::FocusOut) {
      Capture(widget);
    }
    return QObject::eventFilter(watched, event);
  }

  void Restore(QWidget* widget) {
    if (!IsEligible(widget)) {
      return;
    }
    const QString cached = Value(KeyFor(widget));
    if (!cached.isEmpty() && CurrentText(widget) != cached) {
      SetWidgetText(widget, cached);
    }
  }

  void Watch(QWidget* widget) {
    if (!IsEligible(widget) || watched_.contains(widget)) {
      return;
    }
    watched_.insert(widget);
    HookDialog(widget);
    Restore(widget);
    if (auto* line = qobject_cast<QLineEdit*>(widget)) {
      QObject::connect(line, &QLineEdit::textChanged, this,
                       [this, line](const QString& text) {
                         if (line->hasFocus()) {
                           edited_.insert(line);
                           Store(KeyFor(line), text);
                         }
                       });
      return;
    }
    if (auto* plain = qobject_cast<QPlainTextEdit*>(widget)) {
      QObject::connect(plain, &QPlainTextEdit::textChanged, this,
                       [this, plain]() {
                         if (plain->hasFocus()) {
                           edited_.insert(plain);
                           Store(KeyFor(plain), plain->toPlainText());
                         }
                       });
      return;
    }
    if (auto* text = qobject_cast<QTextEdit*>(widget)) {
      QObject::connect(text, &QTextEdit::textChanged, this, [this, text]() {
        if (text->hasFocus()) {
          edited_.insert(text);
          Store(KeyFor(text), text->toPlainText());
        }
      });
    }
  }

  void ClearBelow(QWidget* root) {
    if (root == nullptr) {
      return;
    }
    EnsureLoaded();
    QList<QWidget*> widgets = root->findChildren<QWidget*>();
    widgets.prepend(root);
    bool changed = false;
    for (QWidget* widget : widgets) {
      if (!IsEligible(widget)) {
        continue;
      }
      edited_.remove(widget);
      if (drafts_.remove(KeyFor(widget)) > 0) {
        changed = true;
      }
    }
    if (changed) {
      Flush();
    }
  }

 private:
  void EnsureLoaded() {
    const QString root = UiCacheRoot();
    if (loaded_root_ == root) {
      return;
    }
    if (journal_.isOpen()) {
      journal_.close();
    }
    drafts_.clear();
    journal_records_ = 0;
    journal_bytes_ = 0;
    loaded_root_ = root;
    QDir().mkpath(root);

    QFile base(DraftsPath());
    if (base.open(QIODevice::ReadOnly)) {
      QJsonParseError error;
      const QJsonDocument document =
          QJsonDocument::fromJson(base.readAll(), &error);
      if (error.error == QJsonParseError::NoError && document.isObject()) {
        const QJsonObject object = document.object();
        if (object.value(QStringLiteral("version")).toInt() ==
            kDraftFileVersion) {
          const QJsonObject values =
              object.value(QStringLiteral("drafts")).toObject();
          for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
            if (it.value().isString() && !it.value().toString().isEmpty()) {
              drafts_.insert(it.key(), it.value().toString());
            }
          }
        }
      }
    }

    QFile journal(JournalPath());
    if (!journal.open(QIODevice::ReadOnly)) {
      return;
    }
    while (!journal.atEnd()) {
      const QByteArray line = journal.readLine();
      if (line.trimmed().isEmpty()) {
        continue;
      }
      QJsonParseError error;
      const QJsonDocument document =
          QJsonDocument::fromJson(line, &error);
      if (error.error != QJsonParseError::NoError || !document.isArray()) {
        continue;
      }
      const QJsonArray record = document.array();
      if (record.size() != 2 || !record.at(0).isString() ||
          !record.at(1).isString()) {
        continue;
      }
      const QString key = record.at(0).toString();
      const QString text = record.at(1).toString();
      if (key.isEmpty()) {
        continue;
      }
      if (text.isEmpty()) {
        drafts_.remove(key);
      } else {
        drafts_.insert(key, text);
      }
      ++journal_records_;
      journal_bytes_ += line.size();
    }
  }

  void AppendRecord(const QString& key, const QString& text) {
    QDir().mkpath(UiCacheRoot());
    if (!journal_.isOpen()) {
      journal_.setFileName(JournalPath());
      if (!journal_.open(QIODevice::WriteOnly | QIODevice::Append)) {
        Flush();
        return;
      }
    }
    QJsonArray record;
    record.append(key);
    record.append(text);
    const QByteArray line =
        QJsonDocument(record).toJson(QJsonDocument::Compact) + '\n';
    if (journal_.write(line) != line.size()) {
      Flush();
      return;
    }
    ++journal_records_;
    journal_bytes_ += line.size();
    if (journal_records_ >= kCompactAfterRecords ||
        journal_bytes_ >= kCompactAfterBytes) {
      Flush();
    }
  }

  void HookDialog(QWidget* widget) {
    auto* dialog = qobject_cast<QDialog*>(widget->window());
    if (dialog == nullptr || dialog_hooks_.contains(dialog)) {
      return;
    }
    dialog_hooks_.insert(dialog);
    // A successfully accepted form is no longer a draft. Cancelling keeps the
    // cache so reopening the dialog still restores the work.
    QObject::connect(dialog, &QDialog::accepted, this,
                     [this, dialog]() { ClearBelow(dialog); });
    QObject::connect(dialog, &QObject::destroyed, this,
                     [this, dialog]() { dialog_hooks_.remove(dialog); });
  }

  void Capture(QWidget* widget) {
    if (!watched_.contains(widget) || !edited_.contains(widget)) {
      return;
    }
    Store(KeyFor(widget), CurrentText(widget));
  }

  QString loaded_root_;
  QHash<QString, QString> drafts_;
  QSet<QObject*> watched_;
  QSet<QObject*> edited_;
  QSet<QObject*> dialog_hooks_;
  QFile journal_;
  int journal_records_ = 0;
  qint64 journal_bytes_ = 0;
};

InputDraftStore*& StorePointer() {
  static InputDraftStore* store = nullptr;
  return store;
}

bool& InstalledFlag() {
  static bool installed = false;
  return installed;
}

InputDraftStore* Store() {
  InputDraftStore*& store = StorePointer();
  if (store == nullptr) {
    store = new InputDraftStore();
  }
  return store;
}

}  // namespace

void InstallInputDraftCache() {
  InputDraftStore* store = Store();
  if (InstalledFlag()) {
    return;
  }
  InstalledFlag() = true;
  if (qApp == nullptr) {
    return;
  }
  qApp->installEventFilter(store);
  QObject::connect(qApp, &QCoreApplication::aboutToQuit, store,
                   [store]() { store->Flush(); });
  // Install can follow widgets that were already shown (a detached window or
  // a lazily-created page); attach those immediately.
  for (QWidget* widget : QApplication::allWidgets()) {
    if (widget->isVisible()) {
      store->Watch(widget);
    }
  }
}

void UninstallInputDraftCacheForTest() {
  InputDraftStore* store = StorePointer();
  if (store == nullptr || !InstalledFlag()) {
    return;
  }
  if (qApp != nullptr) {
    qApp->removeEventFilter(store);
  }
  InstalledFlag() = false;
}

void ResetInputDraftCacheForTest() {
  UninstallInputDraftCacheForTest();
  InputDraftStore*& store = StorePointer();
  delete store;
  store = nullptr;
}

QString CachedInputDraft(const QString& key) {
  return Store()->Value(key);
}

void CacheInputDraft(const QString& key, const QString& text) {
  Store()->Store(key, text);
}

void RestoreInputDraft(QWidget* widget) {
  if (widget == nullptr) {
    return;
  }
  Store()->Watch(widget);
  Store()->Restore(widget);
}

void FlushInputDraftCache() { Store()->Flush(); }

void ClearInputDrafts(QWidget* root) { Store()->ClearBelow(root); }

}  // namespace hermes::ui
