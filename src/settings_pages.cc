// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/settings_pages.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextTable>
#include <QTextTableCell>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QVector>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "src/appearance_prefs.h"
#include "src/markdown_preview_style.h"
#include "src/notification_prefs.h"
#include "src/qr_code.h"
#include "src/theme_presets.h"

namespace hermes::ui {
namespace {

constexpr int kLabelWidth = 220;

QString Text(const QJsonValue& value) {
  if (value.isString()) {
    return value.toString();
  }
  if (value.isBool()) {
    return value.toBool() ? QStringLiteral("yes") : QStringLiteral("no");
  }
  if (value.isDouble()) {
    return QString::number(value.toDouble(), 'g', 12);
  }
  if (value.isArray()) {
    QStringList parts;
    for (const QJsonValue& entry : value.toArray()) {
      parts.append(Text(entry));
    }
    return parts.join(QStringLiteral(", "));
  }
  return QString();
}

// Gateway routes disagree on the array key (`sessions`, `jobs`, `servers`).
// Try the names the route documents, then a bare array payload.
QJsonArray ArrayField(const QJsonObject& object,
                      std::initializer_list<const char*> keys) {
  for (const char* key : keys) {
    const QJsonValue value = object.value(QLatin1String(key));
    if (value.isArray()) {
      return value.toArray();
    }
  }
  return QJsonArray();
}

QJsonObject ObjectField(const QJsonObject& object, const char* key) {
  return object.value(QLatin1String(key)).toObject();
}

// Connector operation frames nest connect_url/op_id under targets. The page
// does not need the whole card: it only opens the first authorization link and
// polls the first operation id it can find.
QString FindStringRecursive(const QJsonValue& value, const QString& key) {
  if (value.isObject()) {
    const QJsonObject object = value.toObject();
    const QJsonValue direct = object.value(key);
    if (direct.isString() && !direct.toString().isEmpty()) {
      return direct.toString();
    }
    for (const QJsonValue& child : object) {
      const QString found = FindStringRecursive(child, key);
      if (!found.isEmpty()) {
        return found;
      }
    }
  } else if (value.isArray()) {
    for (const QJsonValue& child : value.toArray()) {
      const QString found = FindStringRecursive(child, key);
      if (!found.isEmpty()) {
        return found;
      }
    }
  }
  return QString();
}

QString ConnectorOperationSummary(const QJsonObject& result) {
  QStringList parts;
  for (const QJsonValue& value :
       result.value(QStringLiteral("targets")).toArray()) {
    const QJsonObject target = value.toObject();
    const QString name = target.value(QStringLiteral("name")).toString();
    QString state = target.value(QStringLiteral("state")).toString();
    const QString detail = target.value(QStringLiteral("detail")).toString();
    if (!detail.isEmpty()) {
      state += QStringLiteral(" — ") + detail;
    }
    parts.append(name.isEmpty() ? state : name + QStringLiteral(": ") + state);
  }
  if (!parts.isEmpty()) {
    return parts.join(QStringLiteral(" · "));
  }
  return result.value(QStringLiteral("note")).toString();
}

// Device-code authorization rides in target text, not a dedicated field: the
// gateway fills `instructions`/`hint` with the code and verification URL, and
// vendors sometimes add `user_code`/`verification_uri`. Render the human steps
// next to the operation state so the user never has to open the browser first.
QString ConnectorPromptSummary(const QJsonObject& result) {
  QStringList parts;
  const QString code = FindStringRecursive(result, QStringLiteral("user_code"));
  if (!code.isEmpty()) {
    parts.append(QStringLiteral("Code: %1").arg(code));
  }
  for (const QJsonValue& value :
       result.value(QStringLiteral("targets")).toArray()) {
    const QJsonObject target = value.toObject();
    const QString instructions =
        target.value(QStringLiteral("instructions")).toString();
    if (!instructions.isEmpty()) {
      parts.append(instructions);
    }
    const QString hint = target.value(QStringLiteral("hint")).toString();
    if (!hint.isEmpty()) {
      parts.append(hint);
    }
    const QJsonArray tools = target.value(QStringLiteral("tools")).toArray();
    if (!tools.isEmpty()) {
      QStringList names;
      for (const QJsonValue& tool : tools) {
        const QString name = tool.toString();
        if (!name.isEmpty()) {
          names.append(name);
        }
      }
      if (!names.isEmpty()) {
        parts.append(
            QStringLiteral("Tools: %1").arg(names.join(QStringLiteral(", "))));
      }
    }
    const QJsonArray required_env =
        target.value(QStringLiteral("required_env")).toArray();
    if (!required_env.isEmpty()) {
      QStringList names;
      for (const QJsonValue& field : required_env) {
        const QString name =
            field.toObject().value(QStringLiteral("name")).toString();
        if (!name.isEmpty()) {
          names.append(name);
        }
      }
      if (!names.isEmpty()) {
        parts.append(QStringLiteral("Needs credentials: %1")
                         .arg(names.join(QStringLiteral(", "))));
      }
    }
  }
  return parts.join(QStringLiteral("\n"));
}

// Some routes answer with a bare array, others wrap it. Accept both.
QJsonArray PayloadArray(const QJsonObject& object, const char* key,
                        std::initializer_list<const char*> nested) {
  const QJsonValue value = object.value(QLatin1String(key));
  if (value.isArray()) {
    return value.toArray();
  }
  return ArrayField(ObjectField(object, key), nested);
}

QLabel* Label(QWidget* parent, const QString& text, const char* object_name) {
  auto* label = new QLabel(text, parent);
  label->setObjectName(QLatin1String(object_name));
  return label;
}

QLabel* Description(QWidget* parent, const QString& text) {
  auto* label = Label(parent, text, "settingDescription");
  label->setWordWrap(true);
  return label;
}

QPushButton* Button(QWidget* parent, const QString& text) {
  return new QPushButton(text, parent);
}

void ClearLayout(QLayout* layout) {
  while (QLayoutItem* item = layout->takeAt(0)) {
    if (QWidget* widget = item->widget()) {
      widget->deleteLater();
    }
    delete item;
  }
}

// The connector catalog row is the shell. Accounts and tools are connector-
// owned subpages: both are scoped reads plus account mutations, so they live in
// dialogs instead of growing the capabilities page vertically.
void ShowConnectorAccounts(QWidget* page, const PageContext& context,
                           const QString& slug,
                           const std::function<void()>& changed) {
  const auto rpc = context.connector_rpc;
  if (!rpc) {
    return;
  }
  auto* dialog = new QDialog(page);
  dialog->setObjectName(QStringLiteral("connectorAccountsDialog_") + slug);
  dialog->setWindowTitle(QStringLiteral("Accounts · %1").arg(slug));
  dialog->resize(560, 380);
  auto* layout = new QVBoxLayout(dialog);
  auto* status = new QLabel(QStringLiteral("Loading accounts…"), dialog);
  status->setObjectName(QStringLiteral("connectorAccountsStatus_") + slug);
  status->setWordWrap(true);
  layout->addWidget(status);
  auto* list = new QWidget(dialog);
  list->setObjectName(QStringLiteral("connectorAccounts_") + slug);
  auto* list_layout = new QVBoxLayout(list);
  list_layout->setContentsMargins(0, 0, 0, 0);
  list_layout->setSpacing(6);
  layout->addWidget(list);
  layout->addStretch(1);
  auto* close = new QPushButton(QStringLiteral("Close"), dialog);
  close->setObjectName(QStringLiteral("connectorAccountsClose_") + slug);
  QObject::connect(close, &QPushButton::clicked, dialog, &QDialog::accept);
  auto* footer = new QHBoxLayout();
  footer->addStretch(1);
  footer->addWidget(close);
  layout->addLayout(footer);

  auto reload = std::make_shared<std::function<void()>>();
  *reload = [=]() {
    status->setText(QStringLiteral("Loading accounts…"));
    rpc(QStringLiteral("connectors.accounts"),
        QJsonObject{{QStringLiteral("connector"), slug}},
        [=](const QJsonObject& data, const QString& error) {
          ClearLayout(list_layout);
          if (!error.isEmpty()) {
            status->setText(error);
            return;
          }
          const QJsonArray accounts =
              ArrayField(data, {"accounts", "items", "results"});
          if (accounts.isEmpty()) {
            status->setText(QStringLiteral("No accounts for this connector."));
            return;
          }
          status->setText(QStringLiteral("%1 account(s)").arg(accounts.size()));
          for (const QJsonValue& value : accounts) {
            const QJsonObject account = value.toObject();
            const QString connection_id =
                account.value(QStringLiteral("connection_id")).toString();
            if (connection_id.isEmpty()) {
              continue;
            }
            const QString account_status =
                account.value(QStringLiteral("status")).toString();
            const bool is_active =
                account.value(QStringLiteral("active")).toBool();
            const bool disabled =
                account.value(QStringLiteral("disabled")).toBool();
            const QString reason =
                account.value(QStringLiteral("status_reason")).toString();
            QStringList detail;
            detail.append(account_status.isEmpty() ? QStringLiteral("unknown")
                                                   : account_status);
            detail.append(is_active ? QStringLiteral("active")
                                    : QStringLiteral("inactive"));
            if (disabled) {
              detail.append(QStringLiteral("retired"));
            }
            if (!reason.isEmpty()) {
              detail.append(reason);
            }
            auto* row = new QWidget(list);
            row->setObjectName(QStringLiteral("connectorAccount_") +
                               connection_id);
            auto* row_layout = new QHBoxLayout(row);
            row_layout->setContentsMargins(0, 0, 0, 0);
            row_layout->setSpacing(6);
            auto* alias = new QLineEdit(
                account.value(QStringLiteral("alias"))
                    .toString(
                        account.value(QStringLiteral("label")).toString()),
                row);
            alias->setObjectName(QStringLiteral("connectorAlias_") +
                                 connection_id);
            row_layout->addWidget(alias, 1);
            row_layout->addWidget(
                new QLabel(detail.join(QStringLiteral(" · ")), row));
            auto* rename = new QPushButton(QStringLiteral("Rename"), row);
            rename->setObjectName(QStringLiteral("connectorRename_") +
                                  connection_id);
            row_layout->addWidget(rename);
            auto* remove = new QPushButton(QStringLiteral("Remove"), row);
            remove->setObjectName(QStringLiteral("connectorRemove_") +
                                  connection_id);
            row_layout->addWidget(remove);
            list_layout->addWidget(row);
            QObject::connect(rename, &QPushButton::clicked, row, [=]() {
              const QString next = alias->text().trimmed();
              if (next.isEmpty()) {
                status->setText(QStringLiteral("Alias cannot be empty."));
                return;
              }
              rpc(QStringLiteral("connectors.accounts.rename"),
                  QJsonObject{{QStringLiteral("connection_id"), connection_id},
                              {QStringLiteral("alias"), next}},
                  [=](const QJsonObject&, const QString& rename_error) {
                    if (!rename_error.isEmpty()) {
                      status->setText(rename_error);
                      return;
                    }
                    status->setText(QStringLiteral("Account renamed."));
                    (*reload)();
                    changed();
                  });
            });
            QObject::connect(remove, &QPushButton::clicked, row, [=]() {
              if (QMessageBox::question(
                      dialog, QStringLiteral("Remove account"),
                      QStringLiteral(
                          "Remove this connector account? Tools using it "
                          "will stop working."),
                      QMessageBox::Yes | QMessageBox::No,
                      QMessageBox::No) != QMessageBox::Yes) {
                return;
              }
              rpc(QStringLiteral("connectors.accounts.remove"),
                  QJsonObject{{QStringLiteral("connection_id"), connection_id}},
                  [=](const QJsonObject&, const QString& remove_error) {
                    if (!remove_error.isEmpty()) {
                      status->setText(remove_error);
                      return;
                    }
                    status->setText(QStringLiteral("Account removed."));
                    (*reload)();
                    changed();
                  });
            });
          }
        });
  };
  (*reload)();
  dialog->open();
}

void ShowConnectorCatalog(QWidget* page, const PageContext& context) {
  const auto rpc = context.connector_rpc;
  if (!rpc) {
    return;
  }
  auto* dialog = new QDialog(page);
  dialog->setObjectName(QStringLiteral("connectorCatalogDialog"));
  dialog->setWindowTitle(QStringLiteral("Connector catalog"));
  dialog->resize(620, 520);
  auto* layout = new QVBoxLayout(dialog);
  auto* status = new QLabel(QStringLiteral("Loading catalog…"), dialog);
  status->setObjectName(QStringLiteral("connectorCatalogStatus"));
  status->setWordWrap(true);
  layout->addWidget(status);
  auto* text = new QTextBrowser(dialog);
  text->setObjectName(QStringLiteral("connectorCatalogText"));
  layout->addWidget(text, 1);
  auto* footer = new QHBoxLayout();
  footer->addStretch(1);
  auto* close = new QPushButton(QStringLiteral("Close"), dialog);
  close->setObjectName(QStringLiteral("connectorCatalogClose"));
  QObject::connect(close, &QPushButton::clicked, dialog, &QDialog::accept);
  footer->addWidget(close);
  layout->addLayout(footer);

  rpc(QStringLiteral("connectors.catalog"), QJsonObject(),
      [=](const QJsonObject& data, const QString& error) {
        if (!error.isEmpty()) {
          status->setText(error);
          return;
        }
        const QJsonArray connectors =
            ArrayField(data, {"connectors", "items", "results"});
        status->setText(
            QStringLiteral("%1 catalog connector(s)").arg(connectors.size()));
        QStringList lines;
        for (const QJsonValue& value : connectors) {
          const QJsonObject entry = value.toObject();
          const QString name =
              entry.value(QStringLiteral("name"))
                  .toString(entry.value(QStringLiteral("slug")).toString());
          const QString slug = entry.value(QStringLiteral("slug")).toString();
          const QString category =
              entry.value(QStringLiteral("category")).toString();
          const QString description =
              entry.value(QStringLiteral("description")).toString();
          lines.append(QStringLiteral("• %1 (%2)%3")
                           .arg(name, slug,
                                category.isEmpty()
                                    ? QString()
                                    : QStringLiteral(" · %1").arg(category)));
          if (!description.isEmpty()) {
            lines.append(QStringLiteral("   ") + description);
          }
        }
        text->setPlainText(lines.isEmpty()
                               ? QStringLiteral("No catalog connectors.")
                               : lines.join(QStringLiteral("\n")));
      });
  dialog->open();
}

void ShowConnectorTools(QWidget* page, const PageContext& context,
                        const QString& slug) {
  const auto rpc = context.connector_rpc;
  if (!rpc) {
    return;
  }
  auto* dialog = new QDialog(page);
  dialog->setObjectName(QStringLiteral("connectorToolsDialog_") + slug);
  dialog->setWindowTitle(QStringLiteral("Tools · %1").arg(slug));
  dialog->resize(620, 480);
  auto* layout = new QVBoxLayout(dialog);
  auto* status = new QLabel(dialog);
  status->setObjectName(QStringLiteral("connectorToolsStatus_") + slug);
  status->setWordWrap(true);
  layout->addWidget(status);
  auto* text = new QTextBrowser(dialog);
  text->setObjectName(QStringLiteral("connectorToolsText_") + slug);
  layout->addWidget(text, 1);
  auto* footer = new QHBoxLayout();
  footer->addStretch(1);
  auto* refresh = new QPushButton(QStringLiteral("Refresh"), dialog);
  refresh->setObjectName(QStringLiteral("connectorToolsRefresh_") + slug);
  footer->addWidget(refresh);
  auto* close = new QPushButton(QStringLiteral("Close"), dialog);
  close->setObjectName(QStringLiteral("connectorToolsClose_") + slug);
  QObject::connect(close, &QPushButton::clicked, dialog, &QDialog::accept);
  footer->addWidget(close);
  layout->addLayout(footer);

  auto load = std::make_shared<std::function<void(bool)>>();
  *load = [=](bool force) {
    status->setText(QStringLiteral("Loading tools…"));
    rpc(QStringLiteral("connectors.tools"),
        QJsonObject{{QStringLiteral("slug"), slug},
                    {QStringLiteral("refresh"), force}},
        [=](const QJsonObject& data, const QString& error) {
          if (!error.isEmpty()) {
            status->setText(error);
            return;
          }
          const QJsonArray tools =
              ArrayField(data, {"tools", "items", "results"});
          QStringList lines;
          for (const QJsonValue& value : tools) {
            const QJsonObject tool = value.toObject();
            const QString name = tool.value(QStringLiteral("name")).toString();
            const QString facet =
                tool.value(QStringLiteral("facet")).toString();
            const QString description =
                tool.value(QStringLiteral("description")).toString();
            lines.append(QStringLiteral("• %1%2").arg(
                name, facet.isEmpty() ? QString()
                                      : QStringLiteral(" [%1]").arg(facet)));
            if (!description.isEmpty()) {
              lines.append(QStringLiteral("   ") + description);
            }
          }
          const QString source =
              data.value(QStringLiteral("source")).toString();
          QString summary = QStringLiteral("%1 tool(s)").arg(tools.size());
          if (!source.isEmpty()) {
            summary += QStringLiteral(" · source: %1").arg(source);
          }
          if (data.value(QStringLiteral("stale")).toBool()) {
            summary += QStringLiteral(" · stale");
          }
          status->setText(summary);
          text->setPlainText(lines.isEmpty()
                                 ? QStringLiteral("No tools advertised.")
                                 : lines.join(QStringLiteral("\n")));
        });
  };
  QObject::connect(refresh, &QPushButton::clicked, dialog,
                   [=]() { (*load)(true); });
  (*load)(false);
  dialog->open();
}

const PageState* PageStateFor(const PageContext& context, const QString& id) {
  if (context.snapshot == nullptr) {
    return nullptr;
  }
  const auto it = context.snapshot->pages.constFind(id);
  return it == context.snapshot->pages.constEnd() ? nullptr : &(*it);
}

// Title row + loading/error/notice banner, shared by every data page.
QVBoxLayout* PageLayout(QWidget* page) {
  auto* layout = new QVBoxLayout(page);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);
  layout->setAlignment(Qt::AlignTop);
  return layout;
}

void AddHeading(QWidget* page, QVBoxLayout* layout, const PageContext& context,
                const QString& id, const QString& title) {
  if (context.suppress_page_heading) {
    return;
  }
  auto* header = new QWidget(page);
  auto* row = new QHBoxLayout(header);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(12);
  auto* heading = Label(header, title, "settingsTitle");
  row->addWidget(heading);
  row->addStretch(1);

  const PageState* state = PageStateFor(context, id);
  if (state != nullptr && !state->notice.isEmpty()) {
    row->addWidget(Description(header, state->notice));
  }
  auto* refresh = Button(header, QStringLiteral("Refresh"));
  refresh->setObjectName(QStringLiteral("pageRefresh_") + id);
  const auto refresh_fn = context.refresh;
  refresh->setEnabled(static_cast<bool>(refresh_fn));
  QObject::connect(refresh, &QPushButton::clicked, refresh, [refresh_fn, id]() {
    if (refresh_fn) {
      refresh_fn(id);
    }
  });
  row->addWidget(refresh);
  layout->addWidget(header);

  if (state == nullptr) {
    layout->addWidget(
        Description(page, QStringLiteral("Not loaded yet — press Refresh.")));
    return;
  }
  if (state->loading) {
    layout->addWidget(
        Description(page, QStringLiteral("Loading from gateway…")));
  }
  if (!state->error.isEmpty()) {
    auto* error = Description(page, state->error);
    error->setStyleSheet(QStringLiteral("color: #c0392b;"));
    layout->addWidget(error);
  }
}

void AddLocalHeading(QWidget* page, QVBoxLayout* layout, const QString& title) {
  auto* heading = Label(page, title, "settingsTitle");
  layout->addWidget(heading);
}

struct Row {
  QWidget* widget = nullptr;
  QHBoxLayout* layout = nullptr;
};

Row AddRow(QWidget* page, QVBoxLayout* layout, const QString& title,
           const QString& description) {
  Row row;
  row.widget = new QWidget(page);
  row.layout = new QHBoxLayout(row.widget);
  row.layout->setContentsMargins(0, 0, 0, 0);
  row.layout->setSpacing(12);

  auto* labels = new QVBoxLayout();
  labels->setSpacing(2);
  auto* name = Label(row.widget, title, "settingTitle");
  name->setMinimumWidth(kLabelWidth);
  // Long connector/job titles must wrap instead of forcing the settings form
  // wider than its viewport (the scroll area hides horizontal overflow).
  name->setWordWrap(true);
  labels->addWidget(name);
  if (!description.isEmpty()) {
    labels->addWidget(Description(row.widget, description));
  }
  row.layout->addLayout(labels, 1);
  layout->addWidget(row.widget);
  return row;
}

// Every boolean in the app reads as a checkbox. Interactive ones are real
// toggles bound to a mutation; informational ones are inert on purpose
// (transparent to the mouse, no focus) so they can never look clickable but
// dead.
QCheckBox* BoolCheck(QWidget* parent, const QString& object_name, bool value,
                     std::function<void(bool)> on_change, QLayout* layout) {
  auto* box = new QCheckBox(parent);
  box->setObjectName(object_name);
  box->setChecked(value);
  if (on_change) {
    box->setCursor(Qt::PointingHandCursor);
    QObject::connect(box, &QCheckBox::toggled, box, std::move(on_change));
  } else {
    box->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    box->setFocusPolicy(Qt::NoFocus);
    box->setToolTip(QStringLiteral("Read-only value"));
  }
  if (layout != nullptr) {
    layout->addWidget(box);
  }
  return box;
}

void AddRowIfPresent(QWidget* page, QVBoxLayout* layout, const QString& title,
                     const QString& value) {
  if (value.isEmpty() || value == QStringLiteral("null")) {
    return;
  }
  AddRow(page, layout, title, value);
}

QString ProviderLabel(const QJsonObject& provider) {
  const QString label = provider.value(QStringLiteral("label")).toString();
  if (!label.isEmpty()) {
    return label;
  }
  const QString name = provider.value(QStringLiteral("name")).toString();
  if (!name.isEmpty()) {
    return name;
  }
  return provider.value(QStringLiteral("slug")).toString();
}

// The env row that owns a provider card's primary credential.
QString PrimaryEnvKey(const QJsonObject& env, const QString& slug) {
  for (const QString& key : env.keys()) {
    const QJsonObject row = env.value(key).toObject();
    if (row.value(QStringLiteral("provider")).toString() != slug) {
      continue;
    }
    if (row.value(QStringLiteral("provider_primary")).toBool()) {
      return key;
    }
  }
  return QString();
}

QWidget* ProvidersPage(const PageContext& context, QWidget* parent) {
  auto* page = new QWidget(parent);
  page->setObjectName(QStringLiteral("settingsPage:providers"));
  auto* layout = PageLayout(page);
  AddHeading(page, layout, context, QStringLiteral("providers"),
             QStringLiteral("Providers"));

  const PageState* state = PageStateFor(context, QStringLiteral("providers"));
  const QJsonObject data = state == nullptr ? QJsonObject() : state->data;
  const QJsonObject env = ObjectField(data, "env");
  const QJsonArray providers =
      ArrayField(ObjectField(data, "options"), {"providers"});
  const auto act = context.action;

  layout->addWidget(Description(
      page, QStringLiteral("%1 providers · /api/model/options + /api/env")
                .arg(providers.size())));

  for (const QJsonValue& value : providers) {
    const QJsonObject provider = value.toObject();
    const QString slug = provider.value(QStringLiteral("slug")).toString();
    const QString label = ProviderLabel(provider);
    const QJsonArray models =
        provider.value(QStringLiteral("models")).toArray();
    const QString env_key = PrimaryEnvKey(env, slug);
    const bool is_set =
        !env_key.isEmpty() &&
        env.value(env_key).toObject().value(QStringLiteral("is_set")).toBool();

    Row row = AddRow(
        page, layout, label.isEmpty() ? slug : label,
        QStringLiteral("%1 models · %2")
            .arg(models.size())
            .arg(env_key.isEmpty()
                     ? QStringLiteral("no credential declared")
                     : (is_set
                            ? QStringLiteral("%1 is set").arg(env_key)
                            : QStringLiteral("%1 is not set").arg(env_key))));
    row.widget->setObjectName(QStringLiteral("providerRow_") + slug);

    auto* set_key = Button(row.widget, is_set ? QStringLiteral("Replace key")
                                              : QStringLiteral("Set key"));
    set_key->setObjectName(QStringLiteral("providerSetKey_") + slug);
    set_key->setEnabled(!env_key.isEmpty() && static_cast<bool>(act));
    QObject::connect(set_key, &QPushButton::clicked, set_key, [=]() {
      bool ok = false;
      const QString secret =
          QInputDialog::getText(page, QStringLiteral("Set %1").arg(env_key),
                                QStringLiteral("%1 for %2").arg(env_key, label),
                                QLineEdit::Password, QString(), &ok);
      if (!ok || secret.trimmed().isEmpty()) {
        return;
      }
      act(QStringLiteral("providers"), QStringLiteral("PUT"),
          QStringLiteral("/api/env"),
          QJsonObject{{QStringLiteral("key"), env_key},
                      {QStringLiteral("value"), secret.trimmed()}});
    });
    row.layout->addWidget(set_key);

    auto* clear_key = Button(row.widget, QStringLiteral("Clear"));
    clear_key->setObjectName(QStringLiteral("providerClearKey_") + slug);
    clear_key->setEnabled(is_set && static_cast<bool>(act));
    QObject::connect(clear_key, &QPushButton::clicked, clear_key, [=]() {
      act(QStringLiteral("providers"), QStringLiteral("DELETE"),
          QStringLiteral("/api/env"),
          QJsonObject{{QStringLiteral("key"), env_key}});
    });
    row.layout->addWidget(clear_key);
  }

  const QString profile =
      context.snapshot == nullptr ? QString() : context.snapshot->name;
  const QString suffix =
      profile.isEmpty()
          ? QString()
          : QStringLiteral("?profile=%1")
                .arg(QString::fromLatin1(QUrl::toPercentEncoding(profile)));
  const auto open_endpoint = [page, act, suffix](const QJsonObject& endpoint) {
    auto* dialog = new QDialog(page);
    dialog->setObjectName(QStringLiteral("endpointDialog"));
    auto* form = new QVBoxLayout(dialog);
    form->addWidget(new QLabel(QStringLiteral("Name"), dialog));
    auto* name = new QLineEdit(
        endpoint.value(QStringLiteral("name")).toString(), dialog);
    name->setObjectName(QStringLiteral("endpointName"));
    form->addWidget(name);
    form->addWidget(new QLabel(QStringLiteral("Base URL"), dialog));
    auto* base_url = new QLineEdit(
        endpoint.value(QStringLiteral("base_url")).toString(), dialog);
    base_url->setObjectName(QStringLiteral("endpointBaseUrl"));
    base_url->setPlaceholderText(QStringLiteral("https://host/v1"));
    form->addWidget(base_url);
    form->addWidget(new QLabel(QStringLiteral("Model"), dialog));
    auto* model = new QLineEdit(
        endpoint.value(QStringLiteral("model")).toString(), dialog);
    model->setObjectName(QStringLiteral("endpointModel"));
    form->addWidget(model);
    form->addWidget(new QLabel(
        QStringLiteral("API key (blank keeps the stored key)"), dialog));
    auto* api_key = new QLineEdit(dialog);
    api_key->setObjectName(QStringLiteral("endpointApiKey"));
    api_key->setEchoMode(QLineEdit::Password);
    form->addWidget(api_key);
    form->addWidget(
        new QLabel(QStringLiteral("Context length (0 = auto)"), dialog));
    auto* context_length = new QSpinBox(dialog);
    context_length->setObjectName(QStringLiteral("endpointContext"));
    context_length->setRange(0, 100000000);
    context_length->setValue(
        endpoint.value(QStringLiteral("context_length")).toInt());
    form->addWidget(context_length);
    auto* discover = new QCheckBox(QStringLiteral("Discover models"), dialog);
    discover->setObjectName(QStringLiteral("endpointDiscover"));
    discover->setChecked(
        endpoint.value(QStringLiteral("discover_models")).toBool(true));
    form->addWidget(discover);
    auto* make_default =
        new QCheckBox(QStringLiteral("Make this the default model"), dialog);
    make_default->setObjectName(QStringLiteral("endpointMakeDefault"));
    const bool is_current =
        endpoint.value(QStringLiteral("is_current")).toBool();
    make_default->setChecked(is_current);
    form->addWidget(make_default);

    auto* validate = Button(dialog, QStringLiteral("Validate"));
    validate->setObjectName(QStringLiteral("endpointValidate"));
    auto* validate_status = Description(dialog, QString());
    validate_status->setObjectName(QStringLiteral("endpointValidateStatus"));
    validate_status->setWordWrap(true);
    form->addWidget(validate, 0, Qt::AlignLeft);
    form->addWidget(validate_status);
    QObject::connect(
        validate, &QPushButton::clicked, dialog,
        [act, base_url, model, api_key, validate_status]() {
          const QString url = base_url->text().trimmed();
          if (url.isEmpty()) {
            validate_status->setText(
                QStringLiteral("Enter an endpoint URL first."));
            return;
          }
          validate_status->setText(QStringLiteral("Probing %1 ...").arg(url));
          act(QStringLiteral("providers"), QStringLiteral("POST"),
              QStringLiteral("/api/providers/custom-endpoints/validate"),
              QJsonObject{
                  {QStringLiteral("name"), QStringLiteral("probe")},
                  {QStringLiteral("base_url"), url},
                  {QStringLiteral("model"), model->text().trimmed()},
                  {QStringLiteral("api_key"), api_key->text().trimmed()}});
        });

    auto* buttons = new QWidget(dialog);
    auto* button_row = new QHBoxLayout(buttons);
    button_row->setContentsMargins(0, 0, 0, 0);
    auto* save = Button(buttons, QStringLiteral("Save"));
    save->setObjectName(QStringLiteral("endpointSubmit"));
    QObject::connect(save, &QPushButton::clicked, dialog, [=]() {
      const QString endpoint_name = name->text().trimmed();
      const QString url = base_url->text().trimmed();
      const QString selected_model = model->text().trimmed();
      if (endpoint_name.isEmpty() || url.isEmpty() ||
          selected_model.isEmpty()) {
        validate_status->setText(
            QStringLiteral("Name, base URL, and model are required."));
        return;
      }
      QJsonObject body{
          {QStringLiteral("name"), endpoint_name},
          {QStringLiteral("base_url"), url},
          {QStringLiteral("model"), selected_model},
          {QStringLiteral("discover_models"), discover->isChecked()},
          {QStringLiteral("make_default"), make_default->isChecked()}};
      const QString id = endpoint.value(QStringLiteral("id")).toString();
      if (!id.isEmpty()) {
        body.insert(QStringLiteral("id"), id);
      }
      if (!api_key->text().trimmed().isEmpty()) {
        body.insert(QStringLiteral("api_key"), api_key->text().trimmed());
      }
      if (context_length->value() > 0) {
        body.insert(QStringLiteral("context_length"), context_length->value());
      }
      act(QStringLiteral("providers"), QStringLiteral("POST"),
          QStringLiteral("/api/providers/custom-endpoints") + suffix, body);
      dialog->accept();
    });
    auto* cancel = Button(buttons, QStringLiteral("Cancel"));
    QObject::connect(cancel, &QPushButton::clicked, dialog,
                     [dialog]() { dialog->reject(); });
    button_row->addWidget(save);
    button_row->addWidget(cancel);
    button_row->addStretch(1);
    form->addWidget(buttons);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->resize(460, 500);
    dialog->open();
  };

  const QJsonArray endpoints =
      ArrayField(ObjectField(data, "endpoints"), {"endpoints"});
  auto* endpoint_heading = new QWidget(page);
  auto* endpoint_heading_row = new QHBoxLayout(endpoint_heading);
  endpoint_heading_row->setContentsMargins(0, 0, 0, 0);
  endpoint_heading_row->addWidget(Label(
      endpoint_heading, QStringLiteral("Custom endpoints"), "settingTitle"));
  endpoint_heading_row->addStretch(1);
  auto* add_endpoint = Button(endpoint_heading, QStringLiteral("Add"));
  add_endpoint->setObjectName(QStringLiteral("endpointAdd"));
  add_endpoint->setEnabled(static_cast<bool>(act));
  QObject::connect(add_endpoint, &QPushButton::clicked, add_endpoint,
                   [open_endpoint]() { open_endpoint(QJsonObject()); });
  endpoint_heading_row->addWidget(add_endpoint);
  layout->addWidget(endpoint_heading);
  if (endpoints.isEmpty()) {
    layout->addWidget(
        Description(page, QStringLiteral("No custom OpenAI-compatible "
                                         "endpoints configured.")));
  }
  for (const QJsonValue& value : endpoints) {
    const QJsonObject endpoint = value.toObject();
    const QString id = endpoint.value(QStringLiteral("id")).toString();
    const QString name = endpoint.value(QStringLiteral("name")).toString();
    const QString base = endpoint.value(QStringLiteral("base_url")).toString();
    Row row = AddRow(page, layout, name.isEmpty() ? id : name, base);
    row.widget->setObjectName(QStringLiteral("endpointRow_") + id);

    auto* edit = Button(row.widget, QStringLiteral("Edit"));
    edit->setObjectName(QStringLiteral("endpointEdit_") + id);
    edit->setEnabled(static_cast<bool>(act));
    QObject::connect(edit, &QPushButton::clicked, edit,
                     [open_endpoint, endpoint]() { open_endpoint(endpoint); });
    row.layout->addWidget(edit);

    auto* activate = Button(row.widget, QStringLiteral("Activate"));
    activate->setObjectName(QStringLiteral("endpointActivate_") + id);
    activate->setEnabled(static_cast<bool>(act));
    QObject::connect(activate, &QPushButton::clicked, activate, [=]() {
      act(QStringLiteral("providers"), QStringLiteral("POST"),
          QStringLiteral("/api/providers/custom-endpoints/%1/activate").arg(id),
          QJsonObject());
    });
    row.layout->addWidget(activate);

    auto* remove = Button(row.widget, QStringLiteral("Delete"));
    remove->setObjectName(QStringLiteral("endpointDelete_") + id);
    remove->setEnabled(static_cast<bool>(act));
    QObject::connect(remove, &QPushButton::clicked, remove, [=]() {
      act(QStringLiteral("providers"), QStringLiteral("DELETE"),
          QStringLiteral("/api/providers/custom-endpoints/%1").arg(id),
          QJsonObject());
    });
    row.layout->addWidget(remove);
  }
  return page;
}

QWidget* KeysPage(const PageContext& context, QWidget* parent) {
  auto* page = new QWidget(parent);
  page->setObjectName(QStringLiteral("settingsPage:keys"));
  auto* layout = PageLayout(page);
  AddHeading(page, layout, context, QStringLiteral("keys"),
             QStringLiteral("API keys"));
  layout->addWidget(Description(
      page, QStringLiteral("Credentials in this profile's .env. Values stay "
                           "redacted; the gateway never returns them here.")));

  const PageState* state = PageStateFor(context, QStringLiteral("keys"));
  const QJsonObject env =
      state == nullptr ? QJsonObject() : ObjectField(state->data, "env");
  const auto act = context.action;

  QStringList names = env.keys();
  names.sort();
  int shown = 0;
  for (const QString& name : names) {
    const QJsonObject row = env.value(name).toObject();
    if (row.value(QStringLiteral("channel_managed")).toBool()) {
      continue;
    }
    ++shown;
    const bool is_set = row.value(QStringLiteral("is_set")).toBool();
    const QString category = row.value(QStringLiteral("category")).toString();
    const QString preview =
        row.value(QStringLiteral("redacted_value")).toString();
    const QString description =
        row.value(QStringLiteral("description")).toString();
    QString detail = category;
    if (!preview.isEmpty()) {
      detail += QStringLiteral(" · ") + preview;
    }
    if (!description.isEmpty()) {
      detail += QStringLiteral(" · ") + description;
    }

    Row line = AddRow(page, layout, name,
                      detail.isEmpty() ? QStringLiteral("not set") : detail);
    line.widget->setObjectName(QStringLiteral("envRow_") + name);

    auto* edit = Button(line.widget, is_set ? QStringLiteral("Replace")
                                            : QStringLiteral("Set"));
    edit->setObjectName(QStringLiteral("envSet_") + name);
    edit->setEnabled(static_cast<bool>(act));
    QObject::connect(edit, &QPushButton::clicked, edit, [=]() {
      bool ok = false;
      const QString secret = QInputDialog::getText(
          page, QStringLiteral("Set %1").arg(name), name,
          row.value(QStringLiteral("is_password")).toBool()
              ? QLineEdit::Password
              : QLineEdit::Normal,
          QString(), &ok);
      if (!ok || secret.trimmed().isEmpty()) {
        return;
      }
      act(QStringLiteral("keys"), QStringLiteral("PUT"),
          QStringLiteral("/api/env"),
          QJsonObject{{QStringLiteral("key"), name},
                      {QStringLiteral("value"), secret.trimmed()}});
    });
    line.layout->addWidget(edit);

    auto* clear = Button(line.widget, QStringLiteral("Clear"));
    clear->setObjectName(QStringLiteral("envClear_") + name);
    clear->setEnabled(is_set && static_cast<bool>(act));
    QObject::connect(clear, &QPushButton::clicked, clear, [=]() {
      act(QStringLiteral("keys"), QStringLiteral("DELETE"),
          QStringLiteral("/api/env"),
          QJsonObject{{QStringLiteral("key"), name}});
    });
    line.layout->addWidget(clear);
  }

  if (shown == 0 && state != nullptr && state->loaded) {
    layout->addWidget(
        Description(page, QStringLiteral("No credentials stored in "
                                         "this profile's .env.")));
  }

  auto* add = Button(page, QStringLiteral("Add custom key"));
  add->setObjectName(QStringLiteral("envAddCustom"));
  add->setEnabled(static_cast<bool>(act));
  QObject::connect(add, &QPushButton::clicked, add, [=]() {
    bool ok = false;
    const QString name = QInputDialog::getText(
        page, QStringLiteral("Add key"), QStringLiteral("Key name"),
        QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) {
      return;
    }
    const QString secret =
        QInputDialog::getText(page, QStringLiteral("Add key"), name.trimmed(),
                              QLineEdit::Password, QString(), &ok);
    if (!ok) {
      return;
    }
    act(QStringLiteral("keys"), QStringLiteral("PUT"),
        QStringLiteral("/api/env"),
        QJsonObject{{QStringLiteral("key"), name.trimmed().toUpper()},
                    {QStringLiteral("value"), secret.trimmed()}});
  });
  layout->addWidget(add, 0, Qt::AlignLeft);
  return page;
}

QWidget* VaultPage(const PageContext& context, QWidget* parent) {
  auto* page = new QWidget(parent);
  page->setObjectName(QStringLiteral("settingsPage:vault"));
  auto* layout = PageLayout(page);
  AddHeading(page, layout, context, QStringLiteral("vault"),
             QStringLiteral("Credential vault"));
  layout->addWidget(Description(
      page, QStringLiteral("Pooled credentials per provider "
                           "(/api/credentials/pool). Tokens are redacted.")));

  const PageState* state = PageStateFor(context, QStringLiteral("vault"));
  const QJsonArray providers =
      state == nullptr
          ? QJsonArray()
          : ArrayField(ObjectField(state->data, "pool"), {"providers"});
  const auto act = context.action;
  if (providers.isEmpty() && state != nullptr && state->loaded) {
    layout->addWidget(
        Description(page, QStringLiteral("No pooled credentials.")));
  }

  for (const QJsonValue& value : providers) {
    const QJsonObject provider = value.toObject();
    const QString name = provider.value(QStringLiteral("provider")).toString();
    layout->addWidget(Label(page, name, "settingTitle"));
    for (const QJsonValue& entry_value :
         provider.value(QStringLiteral("entries")).toArray()) {
      const QJsonObject entry = entry_value.toObject();
      const int index = entry.value(QStringLiteral("index")).toInt();
      const QString label = entry.value(QStringLiteral("label")).toString();
      const QString source = entry.value(QStringLiteral("source")).toString();
      const QString preview =
          entry.value(QStringLiteral("token_preview")).toString();
      const QString status =
          entry.value(QStringLiteral("last_status")).toString();
      QStringList detail;
      for (const QString& part : {source, preview, status}) {
        if (!part.isEmpty()) {
          detail.append(part);
        }
      }
      Row row =
          AddRow(page, layout,
                 label.isEmpty() ? QStringLiteral("#%1").arg(index) : label,
                 detail.join(QStringLiteral(" · ")));
      row.widget->setObjectName(
          QStringLiteral("vaultRow_%1_%2").arg(name).arg(index));

      auto* remove = Button(row.widget, QStringLiteral("Remove"));
      remove->setObjectName(
          QStringLiteral("vaultRemove_%1_%2").arg(name).arg(index));
      remove->setEnabled(static_cast<bool>(act));
      QObject::connect(remove, &QPushButton::clicked, remove, [=]() {
        act(QStringLiteral("vault"), QStringLiteral("DELETE"),
            QStringLiteral("/api/credentials/pool/%1/%2").arg(name).arg(index),
            QJsonObject());
      });
      row.layout->addWidget(remove);
    }
  }
  return page;
}

QWidget* GatewayPage(const PageContext& context, QWidget* parent) {
  auto* page = new QWidget(parent);
  page->setObjectName(QStringLiteral("settingsPage:gateway"));
  auto* layout = PageLayout(page);
  AddHeading(page, layout, context, QStringLiteral("gateway"),
             QStringLiteral("Gateway & web access"));
  layout->addWidget(Description(
      page,
      QStringLiteral("The gateway process is separate from the optional "
                     "web app listener. Restarting the gateway drops live "
                     "socket sessions briefly.")));

  const PageState* state = PageStateFor(context, QStringLiteral("gateway"));
  const QJsonObject access =
      state == nullptr ? QJsonObject() : ObjectField(state->data, "access");
  const QJsonObject status =
      state == nullptr ? QJsonObject() : ObjectField(state->data, "status");
  const auto act = context.action;

  layout->addWidget(
      Label(page, QStringLiteral("Gateway process"), "settingTitle"));
  const QString gateway_state =
      status.value(QStringLiteral("gateway_state")).toString();
  AddRow(page, layout, QStringLiteral("State"),
         gateway_state.isEmpty() ? QStringLiteral("unknown") : gateway_state);
  AddRowIfPresent(page, layout, QStringLiteral("Version"),
                  Text(status.value(QStringLiteral("version"))));
  AddRowIfPresent(page, layout, QStringLiteral("Process ID"),
                  Text(status.value(QStringLiteral("gateway_pid"))));
  AddRowIfPresent(page, layout, QStringLiteral("Active agents"),
                  Text(status.value(QStringLiteral("active_agents"))));

  layout->addWidget(
      Label(page, QStringLiteral("Web app listener"), "settingTitle"));
  if (!access.isEmpty()) {
    if (access.contains(QStringLiteral("enabled"))) {
      Row enabled_row =
          AddRow(page, layout, QStringLiteral("Web app enabled"), QString());
      BoolCheck(enabled_row.widget, QStringLiteral("gatewayEnabled"),
                access.value(QStringLiteral("enabled")).toBool(),
                std::function<void(bool)>(), enabled_row.layout);
    }
    AddRowIfPresent(page, layout, QStringLiteral("Mode"),
                    Text(access.value(QStringLiteral("mode"))));
    AddRowIfPresent(page, layout, QStringLiteral("Port"),
                    Text(access.value(QStringLiteral("port"))));
    AddRowIfPresent(page, layout, QStringLiteral("Public URL"),
                    Text(access.value(QStringLiteral("public_url"))));
    AddRowIfPresent(page, layout, QStringLiteral("LAN address"),
                    Text(access.value(QStringLiteral("lan_address"))));
    const QJsonValue webapp_state = access.value(QStringLiteral("gateway"));
    AddRow(page, layout, QStringLiteral("Web app state"),
           webapp_state.isObject()
               ? Text(webapp_state.toObject().value(QStringLiteral("state")))
               : Text(webapp_state));
    if (access.contains(QStringLiteral("nous_signed_in"))) {
      Row nous_row =
          AddRow(page, layout, QStringLiteral("Signed in to Nous"), QString());
      BoolCheck(nous_row.widget, QStringLiteral("nousSignedIn"),
                access.value(QStringLiteral("nous_signed_in")).toBool(),
                std::function<void(bool)>(), nous_row.layout);
    }
  }

  auto* controls = new QWidget(page);
  auto* row = new QHBoxLayout(controls);
  row->setContentsMargins(0, 0, 0, 0);
  const QString mode = access.value(QStringLiteral("mode")).toString();
  const bool enabled = access.value(QStringLiteral("enabled")).toBool();

  auto* start = Button(controls, QStringLiteral("Start"));
  start->setObjectName(QStringLiteral("gatewayStart"));
  start->setEnabled(static_cast<bool>(act) && !enabled);
  QObject::connect(start, &QPushButton::clicked, start, [=]() {
    act(QStringLiteral("gateway"), QStringLiteral("POST"),
        QStringLiteral("/api/webapp-access/start"),
        QJsonObject{{QStringLiteral("mode"),
                     mode.isEmpty() || mode == QStringLiteral("off")
                         ? QStringLiteral("local")
                         : mode}});
  });
  row->addWidget(start);

  auto* stop = Button(controls, QStringLiteral("Stop"));
  stop->setObjectName(QStringLiteral("gatewayStop"));
  stop->setEnabled(static_cast<bool>(act) && enabled);
  QObject::connect(stop, &QPushButton::clicked, stop, [=]() {
    act(QStringLiteral("gateway"), QStringLiteral("POST"),
        QStringLiteral("/api/webapp-access/stop"), QJsonObject());
  });
  row->addWidget(stop);

  auto* restart = Button(controls, QStringLiteral("Restart gateway"));
  restart->setObjectName(QStringLiteral("gatewayRestart"));
  restart->setEnabled(static_cast<bool>(act));
  QObject::connect(restart, &QPushButton::clicked, restart, [=]() {
    act(QStringLiteral("gateway"), QStringLiteral("POST"),
        QStringLiteral("/api/webapp-access/restart"), QJsonObject());
  });
  row->addWidget(restart);
  row->addStretch(1);
  layout->addWidget(controls);
  return page;
}

QWidget* NotificationsPage(const PageContext& context, QWidget* parent) {
  auto* page = new QWidget(parent);
  page->setObjectName(QStringLiteral("settingsPage:notifications"));
  auto* layout = PageLayout(page);
  AddLocalHeading(page, layout, QStringLiteral("Notifications"));
  layout->addWidget(Description(
      page, QStringLiteral(
                "System alerts are delivered by the OS. Turn-ended alerts only "
                "appear while Hermes is in the background; approval and input "
                "prompts also appear for another chat. macOS asks for "
                "notification permission the first time one is sent.")));

  const auto pref_on = [&context](const QString& key, bool fallback) {
    if (!context.get_pref) {
      return fallback;
    }
    const QString value = context.get_pref(key);
    if (value.isEmpty()) {
      return fallback;
    }
    return value == QStringLiteral("1") || value == QStringLiteral("true");
  };
  const auto set_pref = context.set_pref;
  const bool prefs_writable = static_cast<bool>(set_pref);

  Row master =
      AddRow(page, layout, QStringLiteral("Enable system notifications"),
             QStringLiteral("notifications.enabled"));
  master.widget->setObjectName(
      QStringLiteral("notifyRow_notifications.enabled"));
  auto* master_box = new QCheckBox(master.widget);
  master_box->setObjectName(
      QStringLiteral("notifyToggle_notifications.enabled"));
  master_box->setChecked(
      pref_on(QStringLiteral("notifications.enabled"), true));
  master_box->setEnabled(prefs_writable);
  master.layout->addWidget(master_box);

  QVector<QCheckBox*> kind_boxes;
  for (NotificationKind kind : kNotificationKinds) {
    const QString key = NotificationKindPrefKey(kind);
    Row row = AddRow(page, layout, NotificationKindLabel(kind),
                     NotificationKindDescription(kind));
    row.widget->setObjectName(QStringLiteral("notifyRow_") + key);
    auto* box = new QCheckBox(row.widget);
    box->setObjectName(QStringLiteral("notifyToggle_") + key);
    box->setChecked(pref_on(key, true));
    box->setEnabled(prefs_writable && master_box->isChecked());
    row.layout->addWidget(box);
    kind_boxes.push_back(box);
    QObject::connect(box, &QCheckBox::toggled, box, [=](bool checked) {
      if (set_pref) {
        set_pref(key, checked ? QStringLiteral("1") : QStringLiteral("0"));
      }
    });
  }

  QObject::connect(
      master_box, &QCheckBox::toggled, master_box, [=](bool checked) {
        if (set_pref) {
          set_pref(QStringLiteral("notifications.enabled"),
                   checked ? QStringLiteral("1") : QStringLiteral("0"));
        }
        for (QCheckBox* box : kind_boxes) {
          box->setEnabled(checked);
        }
      });

  Row mute = AddRow(page, layout,
                    QStringLiteral("Mute notification and completion sounds"),
                    QStringLiteral("notifications.mute_sounds"));
  mute.widget->setObjectName(
      QStringLiteral("notifyRow_notifications.mute_sounds"));
  auto* mute_box = new QCheckBox(mute.widget);
  mute_box->setObjectName(
      QStringLiteral("notifyToggle_notifications.mute_sounds"));
  mute_box->setChecked(
      pref_on(QStringLiteral("notifications.mute_sounds"), false));
  mute_box->setEnabled(prefs_writable);
  mute.layout->addWidget(mute_box);
  QObject::connect(mute_box, &QCheckBox::toggled, mute_box, [=](bool checked) {
    if (set_pref) {
      set_pref(QStringLiteral("notifications.mute_sounds"),
               checked ? QStringLiteral("1") : QStringLiteral("0"));
    }
  });

  auto* test_row = new QWidget(page);
  auto* test_layout = new QHBoxLayout(test_row);
  test_layout->setContentsMargins(0, 8, 0, 0);
  test_layout->setSpacing(10);
  auto* test = Button(test_row, QStringLiteral("Send test notification"));
  test->setObjectName(QStringLiteral("notifyTest"));
  test->setEnabled(static_cast<bool>(context.send_notification));
  test_layout->addWidget(test);
  auto* status = Description(test_row, QString());
  status->setObjectName(QStringLiteral("notifyTestStatus"));
  status->setWordWrap(true);
  test_layout->addWidget(status, 1);
  layout->addWidget(test_row);

  const auto send_notification = context.send_notification;
  QObject::connect(test, &QPushButton::clicked, test, [=]() {
    if (!send_notification) {
      return;
    }
    QString error;
    if (send_notification(QStringLiteral("Hermes"),
                          QStringLiteral("Test notification from Hermes."),
                          mute_box->isChecked(), &error)) {
      status->setText(QStringLiteral(
          "Test notification sent. If nothing appeared, allow Hermes in "
          "System Settings → Notifications."));
      status->setProperty("hermesError", false);
    } else {
      status->setText(error.isEmpty()
                          ? QStringLiteral("Could not send the notification.")
                          : error);
      status->setProperty("hermesError", true);
    }
    status->style()->unpolish(status);
    status->style()->polish(status);
  });
  return page;
}

QWidget* PluginsPage(const PageContext& context, QWidget* parent) {
  auto* page = new QWidget(parent);
  page->setObjectName(QStringLiteral("settingsPage:plugins"));
  auto* layout = PageLayout(page);
  AddHeading(page, layout, context, QStringLiteral("plugins"),
             QStringLiteral("Plugins"));

  const PageState* state = PageStateFor(context, QStringLiteral("plugins"));
  QJsonArray plugins;
  QJsonArray hub;
  if (state != nullptr) {
    const QJsonValue value = state->data.value(QStringLiteral("plugins"));
    plugins = value.isArray() ? value.toArray()
                              : ArrayField(ObjectField(state->data, "plugins"),
                                           {"plugins"});
    hub = ArrayField(ObjectField(state->data, "hub"), {"plugins"});
  }
  const auto act = context.action;
  QString profile_query;
  if (context.snapshot != nullptr && !context.snapshot->name.isEmpty()) {
    profile_query =
        QStringLiteral("?profile=") +
        QString::fromLatin1(QUrl::toPercentEncoding(context.snapshot->name));
  }
  layout->addWidget(Description(
      page, QStringLiteral("%1 dashboard plugins · %2 agent plugins discovered "
                           "by this gateway.")
                .arg(plugins.size())
                .arg(hub.size())));
  if (state != nullptr && !state->error.isEmpty()) {
    auto* error = Description(page, state->error);
    error->setObjectName(QStringLiteral("pluginsError"));
    error->setWordWrap(true);
    layout->addWidget(error);
  } else if (state != nullptr && state->loading && !state->loaded) {
    layout->addWidget(Description(
        page,
        QStringLiteral("Loading plugins… slow agent hubs can take up to a "
                       "minute on first scan.")));
  }

  // Agent plugins carry the enable/disable state; dashboard plugins are tabs
  // contributed by some of the same packages.
  for (const QJsonValue& value : hub) {
    const QJsonObject plugin = value.toObject();
    const QString name = plugin.value(QStringLiteral("name")).toString();
    const QString description =
        plugin.value(QStringLiteral("description")).toString();
    const QString version = plugin.value(QStringLiteral("version")).toString();
    const QString status =
        plugin.value(QStringLiteral("runtime_status")).toString();
    const bool enabled = status == QStringLiteral("enabled");
    QStringList detail;
    if (!version.isEmpty()) {
      detail.append(version);
    }
    detail.append(status.isEmpty() ? QStringLiteral("unknown") : status);
    Row row = AddRow(
        page, layout, name,
        description +
            (detail.isEmpty()
                 ? QString()
                 : QStringLiteral("\n") + detail.join(QStringLiteral(" · "))));
    row.widget->setObjectName(QStringLiteral("pluginRow_") + name);
    row.widget->setMaximumWidth(860);
    if (!status.isEmpty()) {
      BoolCheck(row.widget, QStringLiteral("pluginToggle_") + name, enabled,
                static_cast<bool>(act)
                    ? std::function<void(bool)>([=](bool checked) {
                        act(QStringLiteral("plugins"), QStringLiteral("POST"),
                            QStringLiteral("/api/dashboard/agent-plugins/%1/%2")
                                .arg(name, checked ? QStringLiteral("enable")
                                                   : QStringLiteral("disable")),
                            QJsonObject());
                      })
                    : std::function<void(bool)>(),
                row.layout);
    }
    if (plugin.value(QStringLiteral("can_update_git")).toBool()) {
      auto* update = Button(row.widget, QStringLiteral("Update"));
      update->setObjectName(QStringLiteral("pluginUpdate_") + name);
      update->setEnabled(static_cast<bool>(act));
      QObject::connect(update, &QPushButton::clicked, update, [=]() {
        act(QStringLiteral("plugins"), QStringLiteral("POST"),
            QStringLiteral("/api/dashboard/agent-plugins/%1/update").arg(name),
            QJsonObject());
      });
      row.layout->addWidget(update);
    }
    if (plugin.value(QStringLiteral("can_remove")).toBool()) {
      auto* remove = Button(row.widget, QStringLiteral("Remove"));
      remove->setObjectName(QStringLiteral("pluginRemove_") + name);
      remove->setEnabled(static_cast<bool>(act));
      QObject::connect(remove, &QPushButton::clicked, remove, [=]() {
        act(QStringLiteral("plugins"), QStringLiteral("DELETE"),
            QStringLiteral("/api/dashboard/agent-plugins/%1").arg(name),
            QJsonObject());
      });
      row.layout->addWidget(remove);
    }
    const QJsonArray schema =
        plugin.value(QStringLiteral("settings_schema")).toArray();
    const auto save_plugin = context.save_plugin_settings;
    const auto refresh_page = context.refresh;
    if (!schema.isEmpty() && save_plugin && act) {
      auto* settings = Button(row.widget, QStringLiteral("Settings"));
      settings->setObjectName(QStringLiteral("pluginSettings_") + name);
      row.layout->addWidget(settings);
      QObject::connect(settings, &QPushButton::clicked, settings, [=]() {
        auto* dialog = new QDialog(page);
        dialog->setObjectName(QStringLiteral("pluginSettingsDialog_") + name);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowTitle(QStringLiteral("Settings · ") + name);
        auto* dialog_layout = new QVBoxLayout(dialog);
        auto* form = new QFormLayout();
        dialog_layout->addLayout(form);

        struct Field {
          QJsonObject spec;
          QCheckBox* check = nullptr;
          QComboBox* pick = nullptr;
          QLineEdit* edit = nullptr;
        };
        auto fields = std::make_shared<std::vector<Field>>();
        for (const QJsonValue& value : schema) {
          const QJsonObject spec = value.toObject();
          const QString key = spec.value(QStringLiteral("key")).toString();
          if (key.isEmpty()) {
            continue;
          }
          QString label = spec.value(QStringLiteral("label")).toString();
          if (label.isEmpty()) {
            label = key;
          }
          if (spec.value(QStringLiteral("required")).toBool()) {
            label += QStringLiteral(" *");
          }
          const QString type = spec.value(QStringLiteral("type")).toString();
          Field field;
          field.spec = spec;
          if (type == QStringLiteral("boolean")) {
            field.check = new QCheckBox(dialog);
            field.check->setObjectName(QStringLiteral("pluginSetting_") + key);
            field.check->setChecked(
                spec.value(QStringLiteral("value")).toBool());
            form->addRow(label, field.check);
          } else if (type == QStringLiteral("enum")) {
            field.pick = new QComboBox(dialog);
            field.pick->setObjectName(QStringLiteral("pluginSetting_") + key);
            for (const QJsonValue& choice :
                 spec.value(QStringLiteral("choices")).toArray()) {
              field.pick->addItem(choice.toString());
            }
            const QString current =
                spec.value(QStringLiteral("value")).toString();
            const int index = field.pick->findText(current);
            if (index >= 0) {
              field.pick->setCurrentIndex(index);
            }
            form->addRow(label, field.pick);
          } else {
            field.edit = new QLineEdit(dialog);
            const QString field_key =
                type == QStringLiteral("secret")
                    ? spec.value(QStringLiteral("env")).toString()
                    : key;
            field.edit->setObjectName(
                (type == QStringLiteral("secret")
                     ? QStringLiteral("pluginSettingSecret_")
                     : QStringLiteral("pluginSetting_")) +
                field_key);
            if (type == QStringLiteral("secret")) {
              field.edit->setEchoMode(QLineEdit::Password);
              if (spec.value(QStringLiteral("has_value")).toBool()) {
                field.edit->setPlaceholderText(
                    QStringLiteral("already set — blank keeps it"));
              }
            } else {
              const QJsonValue current = spec.value(QStringLiteral("value"));
              if (current.isString()) {
                field.edit->setText(current.toString());
              } else if (current.isDouble()) {
                field.edit->setText(QString::number(current.toDouble()));
              } else if (current.isBool()) {
                field.edit->setText(current.toBool() ? QStringLiteral("true")
                                                     : QStringLiteral("false"));
              } else if (current.isArray() || current.isObject()) {
                field.edit->setText(QString::fromUtf8(
                    QJsonDocument(current.isArray()
                                      ? QJsonDocument(current.toArray())
                                      : QJsonDocument(current.toObject()))
                        .toJson(QJsonDocument::Compact)));
              }
            }
            if (!spec.value(QStringLiteral("description"))
                     .toString()
                     .isEmpty()) {
              field.edit->setToolTip(
                  spec.value(QStringLiteral("description")).toString());
            }
            form->addRow(label, field.edit);
          }
          fields->push_back(field);
        }
        auto* status = new QLabel(dialog);
        status->setObjectName(QStringLiteral("pluginSettingsStatus"));
        status->setWordWrap(true);
        dialog_layout->addWidget(status);
        auto* submit = Button(dialog, QStringLiteral("Save"));
        submit->setObjectName(QStringLiteral("pluginSettingsSubmit"));
        dialog_layout->addWidget(submit);
        QObject::connect(submit, &QPushButton::clicked, dialog, [=]() {
          QJsonObject values;
          QJsonObject secrets;
          for (const Field& field : *fields) {
            const QString key =
                field.spec.value(QStringLiteral("key")).toString();
            const QString type =
                field.spec.value(QStringLiteral("type")).toString();
            QString label =
                field.spec.value(QStringLiteral("label")).toString();
            if (label.isEmpty()) {
              label = key;
            }
            if (field.check != nullptr) {
              values.insert(key, field.check->isChecked());
            } else if (field.pick != nullptr) {
              values.insert(key, field.pick->currentText());
            } else if (field.edit != nullptr &&
                       type == QStringLiteral("secret")) {
              const QString secret = field.edit->text();
              if (!secret.isEmpty()) {
                secrets.insert(
                    field.spec.value(QStringLiteral("env")).toString(), secret);
              }
            } else if (field.edit != nullptr) {
              const QString raw = field.edit->text().trimmed();
              if (type == QStringLiteral("number")) {
                bool ok = false;
                const double number = raw.toDouble(&ok);
                if (!ok) {
                  status->setText(label +
                                  QStringLiteral(": expected a number"));
                  return;
                }
                values.insert(key, number);
              } else if (type == QStringLiteral("json")) {
                QJsonParseError parse_error;
                const QJsonDocument document =
                    QJsonDocument::fromJson(raw.toUtf8(), &parse_error);
                if (parse_error.error != QJsonParseError::NoError ||
                    document.isNull()) {
                  status->setText(label +
                                  QStringLiteral(": expected valid JSON"));
                  return;
                }
                values.insert(key, document.isArray()
                                       ? QJsonValue(document.array())
                                       : QJsonValue(document.object()));
              } else {
                values.insert(key, raw);
              }
            }
          }
          for (auto it = secrets.constBegin(); it != secrets.constEnd(); ++it) {
            act(QStringLiteral("plugins"), QStringLiteral("PUT"),
                QStringLiteral("/api/env") + profile_query,
                QJsonObject{{QStringLiteral("key"), it.key()},
                            {QStringLiteral("value"), it.value().toString()}});
          }
          status->setText(QStringLiteral("Saving…"));
          const QPointer<QDialog> guard(dialog);
          const QPointer<QLabel> status_guard(status);
          save_plugin(name, values,
                      [guard, status_guard, refresh_page, name](
                          const QJsonObject& result, const QString& error) {
                        if (guard == nullptr || status_guard == nullptr) {
                          return;
                        }
                        if (!error.isEmpty() ||
                            (result.contains(QStringLiteral("ok")) &&
                             !result.value(QStringLiteral("ok")).toBool())) {
                          status_guard->setText(
                              error.isEmpty()
                                  ? QStringLiteral("Could not save plugin "
                                                   "settings.")
                                  : error);
                          return;
                        }
                        if (refresh_page) {
                          refresh_page(QStringLiteral("plugins"));
                        }
                        guard->accept();
                      });
        });
        dialog->open();
      });
    }
  }

  const PageState* catalog_state =
      PageStateFor(context, QStringLiteral("plugincatalog"));
  auto* browse = new QWidget(page);
  auto* browse_row = new QHBoxLayout(browse);
  browse_row->setContentsMargins(0, 0, 0, 0);
  auto* browse_button = Button(browse, QStringLiteral("Browse catalog"));
  browse_button->setObjectName(QStringLiteral("pluginsCatalogBrowse"));
  const auto refresh = context.refresh;
  browse_button->setEnabled(static_cast<bool>(refresh));
  QObject::connect(browse_button, &QPushButton::clicked, browse_button,
                   [refresh]() {
                     if (refresh) {
                       refresh(QStringLiteral("plugincatalog"));
                     }
                   });
  browse_row->addWidget(browse_button);
  browse_row->addStretch(1);
  layout->addWidget(browse);
  if (catalog_state != nullptr && catalog_state->loading) {
    layout->addWidget(Description(page, QStringLiteral("Loading catalog…")));
  }
  const QJsonArray catalog_entries =
      catalog_state == nullptr
          ? QJsonArray()
          : ArrayField(ObjectField(catalog_state->data, "catalog"),
                       {"entries"});
  if (!catalog_entries.isEmpty()) {
    layout->addWidget(Label(page, QStringLiteral("Catalog"), "settingTitle"));
    int shown = 0;
    for (const QJsonValue& value : catalog_entries) {
      if (shown++ >= 10) {
        break;
      }
      const QJsonObject entry = value.toObject();
      const QString name = entry.value(QStringLiteral("name")).toString();
      const bool installed = entry.value(QStringLiteral("installed")).toBool();
      Row row = AddRow(page, layout, name,
                       entry.value(QStringLiteral("description")).toString());
      row.widget->setObjectName(QStringLiteral("catalogRow_") + name);
      if (!installed) {
        auto* install = Button(row.widget, QStringLiteral("Install"));
        install->setObjectName(QStringLiteral("catalogInstall_") + name);
        install->setEnabled(static_cast<bool>(act));
        QObject::connect(install, &QPushButton::clicked, install, [=]() {
          act(QStringLiteral("plugins"), QStringLiteral("POST"),
              QStringLiteral("/api/dashboard/agent-plugins/install"),
              QJsonObject{{QStringLiteral("catalog_name"), name},
                          {QStringLiteral("enable"), true}});
        });
        row.layout->addWidget(install);
      } else {
        row.layout->addWidget(
            Description(row.widget, QStringLiteral("installed")));
      }
    }
  }

  auto* rescan = Button(page, QStringLiteral("Rescan plugins"));
  rescan->setObjectName(QStringLiteral("pluginsRescan"));
  rescan->setEnabled(static_cast<bool>(act));
  QObject::connect(rescan, &QPushButton::clicked, rescan, [=]() {
    act(QStringLiteral("plugins"), QStringLiteral("GET"),
        QStringLiteral("/api/dashboard/plugins/rescan"), QJsonObject());
  });
  layout->addWidget(rescan, 0, Qt::AlignLeft);
  return page;
}

QWidget* BillingPage(const PageContext& context, QWidget* parent) {
  auto* page = new QWidget(parent);
  page->setObjectName(QStringLiteral("settingsPage:billing"));
  auto* layout = PageLayout(page);
  AddHeading(page, layout, context, QStringLiteral("billing"),
             QStringLiteral("Billing & plans"));

  const PageState* state = PageStateFor(context, QStringLiteral("billing"));
  const QJsonObject portal =
      state == nullptr ? QJsonObject() : ObjectField(state->data, "portal");
  if (portal.isEmpty()) {
    layout->addWidget(Description(
        page, QStringLiteral("No Portal status from this gateway.")));
    return page;
  }

  AddRow(page, layout, QStringLiteral("Signed in"),
         Text(portal.value(QStringLiteral("logged_in"))));
  QJsonValue plan = portal.value(QStringLiteral("plan"));
  if (Text(plan).isEmpty()) {
    plan = portal.value(QStringLiteral("account_tier"));
  }
  AddRowIfPresent(page, layout, QStringLiteral("Plan"), Text(plan));
  AddRowIfPresent(page, layout, QStringLiteral("Free tier"),
                  Text(portal.value(QStringLiteral("free_tier"))));
  AddRowIfPresent(page, layout, QStringLiteral("Subscription"),
                  Text(portal.value(QStringLiteral("subscription_status"))));
  AddRowIfPresent(page, layout, QStringLiteral("Portal"),
                  Text(portal.value(QStringLiteral("portal_url"))));

  for (const QJsonValue& value :
       ArrayField(portal, {"features", "feature_rows"})) {
    const QJsonObject feature = value.toObject();
    AddRow(page, layout, feature.value(QStringLiteral("label")).toString(),
           feature.value(QStringLiteral("state")).toString());
  }

  const QString url = portal.value(QStringLiteral("portal_url")).toString();
  if (!url.isEmpty()) {
    auto* open = Button(page, QStringLiteral("Open Portal"));
    open->setObjectName(QStringLiteral("portalOpen"));
    QObject::connect(open, &QPushButton::clicked, open,
                     [url]() { QDesktopServices::openUrl(QUrl(url)); });
    layout->addWidget(open, 0, Qt::AlignLeft);
  }
  return page;
}

QWidget* SessionsPage(const PageContext& context, QWidget* parent) {
  auto* page = new QWidget(parent);
  page->setObjectName(QStringLiteral("settingsPage:sessions"));
  auto* layout = PageLayout(page);
  AddHeading(page, layout, context, QStringLiteral("sessions"),
             QStringLiteral("Sessions"));

  const PageState* state = PageStateFor(context, QStringLiteral("sessions"));
  const QJsonObject stats =
      state == nullptr ? QJsonObject() : ObjectField(state->data, "stats");
  const auto act = context.action;

  if (!stats.isEmpty()) {
    AddRow(page, layout, QStringLiteral("Stored sessions"),
           Text(stats.value(QStringLiteral("total"))));
    AddRow(page, layout, QStringLiteral("Active"),
           Text(stats.value(QStringLiteral("active_store"))));
    AddRow(page, layout, QStringLiteral("Archived"),
           Text(stats.value(QStringLiteral("archived"))));
    AddRow(page, layout, QStringLiteral("Messages"),
           Text(stats.value(QStringLiteral("messages"))));
  }

  auto* count =
      Description(page, QStringLiteral("%1 sessions in this profile")
                            .arg(context.snapshot == nullptr
                                     ? 0
                                     : context.snapshot->sessions.size()));
  layout->addWidget(count);

  auto* list = new QListWidget(page);
  list->setObjectName(QStringLiteral("settingsSessions"));
  list->setSelectionMode(QAbstractItemView::ExtendedSelection);
  if (context.snapshot != nullptr) {
    int shown = 0;
    for (const SessionSummary& session : context.snapshot->sessions) {
      if (shown++ >= 80) {
        break;
      }
      const QString title = session.title.isEmpty()
                                ? QStringLiteral("Untitled chat")
                                : session.title;
      const QString lineage_kind =
          !session.branched_from.isEmpty() ? QStringLiteral("branch")
          : !session.reset_from.isEmpty()  ? QStringLiteral("reset")
          : session.continuation_kind == QStringLiteral("compression")
              ? QStringLiteral("compression")
              : QString();
      const QString parent_id =
          !session.branched_from.isEmpty() ? session.branched_from
          : !session.reset_from.isEmpty()  ? session.reset_from
                                           : session.parent_session_id;
      // The desktop nests genuine fork children under the parent; at list
      // density a branch marker plus parent action covers the same navigation
      // without another recursive widget.
      const QString label = lineage_kind == QStringLiteral("branch")
                                ? QStringLiteral("↳ %1").arg(title)
                            : lineage_kind == QStringLiteral("reset")
                                ? QStringLiteral("↻ %1").arg(title)
                            : lineage_kind == QStringLiteral("compression")
                                ? QStringLiteral("⟳ %1").arg(title)
                                : title;
      auto* item = new QListWidgetItem(label, list);
      item->setData(Qt::UserRole, session.id);
      item->setData(Qt::UserRole + 1, session.archived);
      item->setData(Qt::UserRole + 2, parent_id);
      item->setData(Qt::UserRole + 3, lineage_kind);
      QStringList tooltip;
      if (!session.model.isEmpty()) {
        tooltip.append(session.model);
      }
      if (!session.source.isEmpty()) {
        tooltip.append(session.source);
      }
      if (!lineage_kind.isEmpty()) {
        tooltip.append(
            lineage_kind == QStringLiteral("branch")
                ? QStringLiteral("Branched from %1").arg(parent_id)
            : lineage_kind == QStringLiteral("reset")
                ? QStringLiteral("Continued from %1").arg(parent_id)
                : QStringLiteral("Compressed from %1").arg(parent_id));
      }
      item->setToolTip(tooltip.join(QStringLiteral(" · ")));
    }
  }
  if (list->count() == 0) {
    list->addItem(QStringLiteral("No sessions"));
    list->setEnabled(false);
  }
  list->setMinimumHeight(100);
  list->setMaximumHeight(qBound(100, 38 * list->count() + 8, 360));
  const auto open_session = context.open_session;
  QObject::connect(list, &QListWidget::itemClicked, list,
                   [open_session](QListWidgetItem* item) {
                     if (open_session) {
                       open_session(item->data(Qt::UserRole).toString());
                     }
                   });
  layout->addWidget(list);

  auto* parent_button = Button(page, QStringLiteral("Open parent"));
  parent_button->setObjectName(QStringLiteral("sessionOpenParent"));
  parent_button->setEnabled(false);
  const auto sync_parent = [list, parent_button, open_session]() {
    QListWidgetItem* item = list->currentItem();
    parent_button->setEnabled(
        open_session && item != nullptr &&
        !item->data(Qt::UserRole + 2).toString().isEmpty());
  };
  QObject::connect(
      list, &QListWidget::currentItemChanged, parent_button,
      [sync_parent](QListWidgetItem*, QListWidgetItem*) { sync_parent(); });
  QObject::connect(parent_button, &QPushButton::clicked, parent_button,
                   [list, open_session]() {
                     QListWidgetItem* item = list->currentItem();
                     const QString parent_id =
                         item == nullptr
                             ? QString()
                             : item->data(Qt::UserRole + 2).toString();
                     if (open_session && !parent_id.isEmpty()) {
                       open_session(parent_id);
                     }
                   });
  sync_parent();
  layout->addWidget(parent_button);

  const auto selected_ids = [list]() {
    QStringList ids;
    for (QListWidgetItem* item : list->selectedItems()) {
      const QString id = item->data(Qt::UserRole).toString();
      if (!id.isEmpty()) {
        ids.push_back(id);
      }
    }
    if (ids.isEmpty() && list->currentItem() != nullptr) {
      const QString id = list->currentItem()->data(Qt::UserRole).toString();
      if (!id.isEmpty()) {
        ids.push_back(id);
      }
    }
    return ids;
  };

  auto* actions = new QWidget(page);
  auto* row = new QHBoxLayout(actions);
  row->setContentsMargins(0, 0, 0, 0);
  auto* remove = Button(actions, QStringLiteral("Delete selected"));
  remove->setObjectName(QStringLiteral("sessionDelete"));
  remove->setEnabled(static_cast<bool>(act));
  QObject::connect(remove, &QPushButton::clicked, remove, [=]() {
    const QStringList ids = selected_ids();
    if (ids.isEmpty()) {
      return;
    }
    QJsonArray array;
    for (const QString& id : ids) {
      array.append(id);
    }
    act(QStringLiteral("sessions"), QStringLiteral("POST"),
        QStringLiteral("/api/sessions/bulk-delete"),
        QJsonObject{{QStringLiteral("ids"), array}});
  });
  row->addWidget(remove);

  auto* archive = Button(actions, QStringLiteral("Archive selected"));
  archive->setObjectName(QStringLiteral("sessionArchive"));
  archive->setEnabled(static_cast<bool>(act));
  QObject::connect(archive, &QPushButton::clicked, archive, [=]() {
    for (const QString& id : selected_ids()) {
      act(QStringLiteral("sessions"), QStringLiteral("PATCH"),
          QStringLiteral("/api/sessions/%1").arg(id),
          QJsonObject{{QStringLiteral("archived"), true}});
    }
  });
  row->addWidget(archive);

  auto* restore = Button(actions, QStringLiteral("Restore selected"));
  restore->setObjectName(QStringLiteral("sessionRestore"));
  restore->setEnabled(static_cast<bool>(act));
  QObject::connect(restore, &QPushButton::clicked, restore, [=]() {
    for (const QString& id : selected_ids()) {
      act(QStringLiteral("sessions"), QStringLiteral("PATCH"),
          QStringLiteral("/api/sessions/%1").arg(id),
          QJsonObject{{QStringLiteral("archived"), false}});
    }
  });
  row->addWidget(restore);

  const auto export_session = context.export_session;
  const auto pick_save_file = context.pick_save_file;
  auto* export_button = Button(actions, QStringLiteral("Export…"));
  export_button->setObjectName(QStringLiteral("sessionExport"));
  export_button->setEnabled(static_cast<bool>(export_session));
  QObject::connect(
      export_button, &QPushButton::clicked, export_button,
      [page, list, export_session, pick_save_file]() {
        QListWidgetItem* item = list->currentItem();
        if (item == nullptr) {
          return;
        }
        const QString title = item->text();
        QString suggested = title;
        for (const QChar character : QStringLiteral("/\\:*?\"<>|")) {
          suggested.replace(character, QLatin1Char('-'));
        }
        suggested += QStringLiteral(".json");
        const QString local =
            pick_save_file
                ? pick_save_file(QStringLiteral("Export session"), suggested)
                : QFileDialog::getSaveFileName(
                      page, QStringLiteral("Export session"), suggested,
                      QStringLiteral("Session JSON (*.json)"));
        if (local.isEmpty()) {
          return;
        }
        QPointer<QWidget> page_guard(page);
        export_session(item->data(Qt::UserRole).toString(), local,
                       [page_guard](const QJsonObject&, const QString& error) {
                         if (!error.isEmpty() && page_guard) {
                           QMessageBox::warning(page_guard,
                                                QStringLiteral("Export failed"),
                                                error);
                         }
                       });
      });
  row->addWidget(export_button);

  const auto import_sessions = context.import_sessions;
  const auto pick_open_file = context.pick_open_file;
  auto* import_button = Button(actions, QStringLiteral("Import…"));
  import_button->setObjectName(QStringLiteral("sessionImport"));
  import_button->setEnabled(static_cast<bool>(import_sessions));
  QObject::connect(
      import_button, &QPushButton::clicked, import_button,
      [page, import_sessions, pick_open_file]() {
        const QString local =
            pick_open_file
                ? pick_open_file(QStringLiteral("Import sessions"), QString())
                : QFileDialog::getOpenFileName(
                      page, QStringLiteral("Import sessions"), QString(),
                      QStringLiteral("Session JSON (*.json)"));
        if (local.isEmpty()) {
          return;
        }
        QPointer<QWidget> page_guard(page);
        import_sessions(
            local, [page_guard](const QJsonObject&, const QString& error) {
              if (!error.isEmpty() && page_guard) {
                QMessageBox::warning(page_guard,
                                     QStringLiteral("Import failed"), error);
              }
            });
      });
  row->addWidget(import_button);
  row->addStretch(1);
  layout->addWidget(actions);

  auto* maintenance = new QWidget(page);
  auto* maintenance_layout = new QHBoxLayout(maintenance);
  maintenance_layout->setContentsMargins(0, 0, 0, 0);
  auto* empty = Button(maintenance, QStringLiteral("Delete empty sessions"));
  empty->setObjectName(QStringLiteral("sessionDeleteEmpty"));
  empty->setEnabled(static_cast<bool>(act));
  QObject::connect(empty, &QPushButton::clicked, empty, [act]() {
    act(QStringLiteral("sessions"), QStringLiteral("DELETE"),
        QStringLiteral("/api/sessions/empty"), QJsonObject());
  });
  maintenance_layout->addWidget(empty);

  maintenance_layout->addWidget(
      new QLabel(QStringLiteral("Keep sessions newer than"), maintenance));
  auto* days = new QSpinBox(maintenance);
  days->setObjectName(QStringLiteral("sessionPruneDays"));
  days->setRange(1, 3650);
  days->setValue(90);
  days->setSuffix(QStringLiteral(" days"));
  maintenance_layout->addWidget(days);
  auto* include_archived =
      new QCheckBox(QStringLiteral("Include archived"), maintenance);
  include_archived->setObjectName(QStringLiteral("sessionPruneArchived"));
  maintenance_layout->addWidget(include_archived);
  auto* prune = Button(maintenance, QStringLiteral("Prune"));
  prune->setObjectName(QStringLiteral("sessionPrune"));
  prune->setEnabled(static_cast<bool>(act));
  QObject::connect(
      prune, &QPushButton::clicked, prune, [act, days, include_archived]() {
        act(QStringLiteral("sessions"), QStringLiteral("POST"),
            QStringLiteral("/api/sessions/prune"),
            QJsonObject{{QStringLiteral("older_than_days"), days->value()},
                        {QStringLiteral("include_archived"),
                         include_archived->isChecked()}});
      });
  maintenance_layout->addWidget(prune);
  maintenance_layout->addStretch(1);
  layout->addWidget(maintenance);
  return page;
}

QWidget* KeybindsPage(const PageContext& context, QWidget* parent) {
  auto* page = new QWidget(parent);
  page->setObjectName(QStringLiteral("settingsPage:keybinds"));
  auto* layout = PageLayout(page);
  AddLocalHeading(page, layout, QStringLiteral("Keybinds"));
  layout->addWidget(Description(
      page, QStringLiteral("Overrides persist in this window's preferences "
                           "and apply immediately.")));

  struct Binding {
    const char* action;
    const char* label;
    const char* default_key;
  };
  // Mirrors the actions MainWindow registers (and their defaults). Desktop
  // bindings that only make sense with panes/tiles this shell does not have
  // are deliberately absent rather than shown as dead rows.
  const Binding bindings[] = {
      {"command_palette", "Command palette", "Ctrl+K"},
      {"command_palette_alt", "Command palette (alternate)", "Ctrl+P"},
      {"settings", "Settings", "Ctrl+,"},
      {"keybinds_panel", "Keybinds", "Ctrl+/"},
      {"new_chat", "New chat", "Ctrl+N"},
      {"interrupt", "Stop the turn", "Ctrl+."},
      {"theme", "Toggle theme", "Ctrl+Shift+D"},
      {"find_sessions", "Find sessions", "Ctrl+F"},
      {"toggle_sidebar", "Toggle sidebar", "Ctrl+B"},
      {"toggle_right_rail", "Toggle right rail", "Ctrl+Shift+B"},
      {"detach_chat", "Detach chat window", "Ctrl+Shift+N"},
      {"pet_overlay", "Desktop pet", "Ctrl+Shift+O"},
      {"toggle_statusbar", "Toggle status bar", "Ctrl+Shift+S"},
      {"model_picker", "Model picker", "Ctrl+Shift+M"},
      {"scroll_page_up", "Scroll page up", "PgUp"},
      {"scroll_page_down", "Scroll page down", "PgDown"},
      {"session_next", "Next session", "Ctrl+Shift+]"},
      {"session_prev", "Previous session", "Ctrl+Shift+["},
      {"profile_1", "Profile 1", "Ctrl+1"},
      {"profile_2", "Profile 2", "Ctrl+2"},
      {"profile_3", "Profile 3", "Ctrl+3"},
      {"profile_4", "Profile 4", "Ctrl+4"},
  };
  for (const Binding& binding : bindings) {
    const QString action = QLatin1String(binding.action);
    const QString key = QStringLiteral("keybind.") + action;
    const QString stored = context.get_pref ? context.get_pref(key) : QString();
    const QString current =
        stored.isEmpty() ? QLatin1String(binding.default_key) : stored;
    Row row = AddRow(page, layout, QLatin1String(binding.label), QString());
    row.widget->setObjectName(QStringLiteral("keybindRow_") + action);
    auto* value = Label(row.widget, current, "settingTitle");
    value->setObjectName(QStringLiteral("keybindValue_") + action);
    row.layout->addWidget(value);

    auto* edit = Button(row.widget, QStringLiteral("Change"));
    edit->setObjectName(QStringLiteral("keybindEdit_") + action);
    edit->setEnabled(static_cast<bool>(context.set_pref));
    const auto set_pref = context.set_pref;
    QObject::connect(edit, &QPushButton::clicked, edit, [=]() {
      bool ok = false;
      const QString sequence = QInputDialog::getText(
          page, QStringLiteral("Rebind %1").arg(QLatin1String(binding.label)),
          QStringLiteral("Key sequence (e.g. Ctrl+K)"), QLineEdit::Normal,
          value->text(), &ok);
      if (!ok || sequence.trimmed().isEmpty() ||
          QKeySequence(sequence.trimmed()).isEmpty()) {
        return;
      }
      const QString normalized =
          QKeySequence(sequence.trimmed()).toString(QKeySequence::PortableText);
      value->setText(normalized);
      set_pref(key, normalized);
    });
    row.layout->addWidget(edit);

    auto* reset = Button(row.widget, QStringLiteral("Reset"));
    reset->setObjectName(QStringLiteral("keybindReset_") + action);
    reset->setEnabled(static_cast<bool>(context.set_pref) && !stored.isEmpty());
    QObject::connect(reset, &QPushButton::clicked, reset, [=]() {
      value->setText(QLatin1String(binding.default_key));
      reset->setEnabled(false);
      set_pref(key, QString());
    });
    row.layout->addWidget(reset);
  }
  return page;
}

QWidget* AboutPage(const PageContext& context, QWidget* parent) {
  auto* page = new QWidget(parent);
  page->setObjectName(QStringLiteral("settingsPage:about"));
  auto* layout = PageLayout(page);
  AddHeading(page, layout, context, QStringLiteral("about"),
             QStringLiteral("About"));

  const PageState* state = PageStateFor(context, QStringLiteral("about"));
  const QJsonObject status =
      state == nullptr ? QJsonObject() : ObjectField(state->data, "status");
  // The update probe shells out to git and can take longer than a page load;
  // it is its own fetch, triggered by the button below.
  const PageState* update_state =
      PageStateFor(context, QStringLiteral("update"));
  const QJsonObject update = update_state == nullptr
                                 ? QJsonObject()
                                 : ObjectField(update_state->data, "update");
  const auto act = context.action;
  const auto refresh = context.refresh;

  AddRow(page, layout, QStringLiteral("Native client"),
         QStringLiteral("hermes-ui (Qt 6)"));
  AddRow(
      page, layout, QStringLiteral("Gateway"),
      context.snapshot == nullptr || context.snapshot->gateway_version.isEmpty()
          ? QStringLiteral("unknown")
          : context.snapshot->gateway_version);
  AddRow(page, layout, QStringLiteral("Profile"),
         context.snapshot == nullptr
             ? QString()
             : (context.snapshot->label.isEmpty() ? context.snapshot->name
                                                  : context.snapshot->label));
  AddRow(page, layout, QStringLiteral("Profile home"),
         context.snapshot == nullptr ? QString() : context.snapshot->home);
  if (!status.isEmpty()) {
    const QJsonObject gateway = status.value(QStringLiteral("components"))
                                    .toObject()
                                    .value(QStringLiteral("gateway"))
                                    .toObject();
    AddRow(page, layout, QStringLiteral("Gateway version"),
           Text(status.value(QStringLiteral("version"))));
    AddRow(page, layout, QStringLiteral("Gateway status"),
           Text(gateway.value(QStringLiteral("status"))));
    AddRow(page, layout, QStringLiteral("Active sessions"),
           Text(status.value(QStringLiteral("active_sessions"))));
    AddRowIfPresent(page, layout, QStringLiteral("Uptime"),
                    Text(status.value(QStringLiteral("uptime"))));
  }

  layout->addWidget(Label(page, QStringLiteral("Updates"), "settingTitle"));
  if (!update.isEmpty()) {
    AddRow(page, layout, QStringLiteral("Install method"),
           Text(update.value(QStringLiteral("install_method"))));
    AddRow(page, layout, QStringLiteral("Current version"),
           Text(update.value(QStringLiteral("current_version"))));
    AddRow(page, layout, QStringLiteral("Update available"),
           Text(update.value(QStringLiteral("update_available"))));
    AddRow(page, layout, QStringLiteral("Commits behind"),
           Text(update.value(QStringLiteral("behind"))));
    AddRow(page, layout, QStringLiteral("Message"),
           Text(update.value(QStringLiteral("message"))));
    AddRowIfPresent(page, layout, QStringLiteral("Action ID"),
                    Text(update.value(QStringLiteral("action_id"))));
    AddRowIfPresent(page, layout, QStringLiteral("Process ID"),
                    Text(update.value(QStringLiteral("pid"))));
  }
  if (update_state != nullptr && !update_state->error.isEmpty()) {
    auto* error = Label(page, update_state->error, "settingDescription");
    error->setObjectName(QStringLiteral("updateError"));
    error->setWordWrap(true);
    layout->addWidget(error);
  } else if (update_state != nullptr && !update_state->notice.isEmpty()) {
    auto* notice = Label(page, update_state->notice, "settingDescription");
    notice->setObjectName(QStringLiteral("updateStatus"));
    notice->setWordWrap(true);
    layout->addWidget(notice);
  }

  auto* row = new QWidget(page);
  auto* buttons = new QHBoxLayout(row);
  buttons->setContentsMargins(0, 0, 0, 0);
  auto* check = Button(row, QStringLiteral("Check for updates"));
  check->setObjectName(QStringLiteral("updateCheck"));
  check->setEnabled(static_cast<bool>(refresh));
  const bool checking = update_state != nullptr && update_state->loading;
  check->setText(checking ? QStringLiteral("Checking…")
                          : QStringLiteral("Check for updates"));
  QObject::connect(check, &QPushButton::clicked, check, [=]() {
    if (refresh) {
      refresh(QStringLiteral("update"));
    }
  });
  buttons->addWidget(check);

  auto* apply = Button(row, QStringLiteral("Install update"));
  apply->setObjectName(QStringLiteral("updateApply"));
  apply->setEnabled(static_cast<bool>(act) &&
                    update.value(QStringLiteral("can_apply")).toBool());
  QObject::connect(apply, &QPushButton::clicked, apply, [=]() {
    act(QStringLiteral("update"), QStringLiteral("POST"),
        QStringLiteral("/api/hermes/update"), QJsonObject());
  });
  buttons->addWidget(apply);
  buttons->addStretch(1);
  layout->addWidget(row);
  return page;
}

// Scheduled jobs read as cards: the text column is capped at 80% of the card
// so long names, schedules and run summaries wrap instead of being clipped by
// the settings scroll area. The controls live in a separate icon row below the
// text, mirroring the desktop's task footer actions.
struct CronCard {
  QWidget* widget = nullptr;
  QVBoxLayout* labels = nullptr;
  QHBoxLayout* actions = nullptr;
};

CronCard AddCronCard(QWidget* page, QVBoxLayout* layout, const QString& id,
                     const QString& title, const QString& detail) {
  CronCard card;
  card.widget = new QWidget(page);
  card.widget->setObjectName(QStringLiteral("cronRow_") + id);
  auto* outer = new QVBoxLayout(card.widget);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(5);

  auto* text = new QWidget(card.widget);
  text->setObjectName(QStringLiteral("cronText_") + id);
  text->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  card.labels = new QVBoxLayout(text);
  card.labels->setContentsMargins(0, 0, 0, 0);
  card.labels->setSpacing(2);

  auto* name = Label(text, title, "settingTitle");
  name->setObjectName(QStringLiteral("cronName_") + id);
  name->setWordWrap(true);
  name->setMinimumWidth(0);
  card.labels->addWidget(name);
  if (!detail.isEmpty()) {
    auto* description = Description(text, detail);
    description->setObjectName(QStringLiteral("cronDetail_") + id);
    card.labels->addWidget(description);
  }
  auto* content_row = new QHBoxLayout();
  content_row->setContentsMargins(0, 0, 0, 0);
  content_row->setSpacing(0);
  content_row->addWidget(text, 4);
  content_row->addStretch(1);
  outer->addLayout(content_row);

  auto* action_host = new QWidget(card.widget);
  action_host->setObjectName(QStringLiteral("cronActions_") + id);
  card.actions = new QHBoxLayout(action_host);
  card.actions->setContentsMargins(0, 0, 0, 0);
  card.actions->setSpacing(4);
  card.actions->setAlignment(Qt::AlignLeft);
  outer->addWidget(action_host);

  layout->addWidget(card.widget);
  return card;
}

QPushButton* CronActionButton(QWidget* parent, const QString& object_name,
                              ushort codepoint, const QString& tooltip,
                              const Theme& theme) {
  auto* button = new QPushButton(QString(QChar(codepoint)), parent);
  button->setObjectName(object_name);
  button->setToolTip(tooltip);
  button->setAccessibleName(tooltip);
  button->setFont(theme.IconFont(16));
  button->setFixedSize(28, 28);
  button->setCursor(Qt::PointingHandCursor);
  button->setStyleSheet(
      QStringLiteral(
          "QPushButton { background: transparent; color: %1; border: none; "
          "border-radius: 6px; padding: 0; font-family: 'codicon'; "
          "font-size: 16px; }"
          "QPushButton:hover { background: %2; color: %3; }"
          "QPushButton:pressed { background: %4; color: %3; }"
          "QPushButton:disabled { color: %5; }")
          .arg(theme.text_secondary.name(), theme.hover.name(),
               theme.foreground.name(), theme.selected.name(),
               theme.text_tertiary.name()));
  return button;
}

QWidget* CronPage(const PageContext& context, QWidget* parent) {
  auto* page = new QWidget(parent);
  page->setObjectName(QStringLiteral("settingsPage:cron"));
  auto* layout = PageLayout(page);
  AddHeading(page, layout, context, QStringLiteral("cron"),
             QStringLiteral("Scheduled tasks"));

  const PageState* state = PageStateFor(context, QStringLiteral("cron"));
  QJsonArray jobs;
  if (state != nullptr) {
    const QJsonValue value = state->data.value(QStringLiteral("jobs"));
    jobs = value.isArray()
               ? value.toArray()
               : ArrayField(ObjectField(state->data, "jobs"), {"jobs", "rows"});
  }
  const auto act = context.action;
  const QString profile =
      context.snapshot == nullptr ? QString() : context.snapshot->name;
  const QString profile_query =
      profile.isEmpty()
          ? QString()
          : QStringLiteral("?profile=%1")
                .arg(QString::fromLatin1(QUrl::toPercentEncoding(profile)));

  // Delivery targets feed both the job summary and the create form.
  QStringList delivery_names;
  QStringList delivery_ids;
  const QJsonArray targets = ArrayField(
      state == nullptr ? QJsonObject() : ObjectField(state->data, "delivery"),
      {"targets"});
  for (const QJsonValue& value : targets) {
    const QJsonObject target = value.toObject();
    delivery_ids.append(target.value(QStringLiteral("id")).toString());
    delivery_names.append(
        target.value(QStringLiteral("name"))
            .toString(target.value(QStringLiteral("id")).toString()));
  }
  if (!delivery_names.isEmpty()) {
    AddRow(page, layout, QStringLiteral("Delivery targets"),
           delivery_names.join(QStringLiteral(", ")));
  }

  auto* new_job = Button(page, QStringLiteral("New task"));
  new_job->setObjectName(QStringLiteral("cronCreate"));
  new_job->setEnabled(static_cast<bool>(act));
  QObject::connect(
      new_job, &QPushButton::clicked, new_job,
      [page, act, delivery_ids, delivery_names, profile_query]() {
        auto* dialog = new QDialog(page);
        dialog->setObjectName(QStringLiteral("cronCreateDialog"));
        auto* form = new QVBoxLayout(dialog);
        auto* name = new QLineEdit(dialog);
        name->setObjectName(QStringLiteral("cronCreateName"));
        name->setPlaceholderText(QStringLiteral("Task name"));
        form->addWidget(name);
        auto* schedule = new QLineEdit(dialog);
        schedule->setObjectName(QStringLiteral("cronCreateSchedule"));
        schedule->setPlaceholderText(
            QStringLiteral("Schedule (cron expression or 'every 2h')"));
        form->addWidget(schedule);
        auto* prompt = new QPlainTextEdit(dialog);
        prompt->setObjectName(QStringLiteral("cronCreatePrompt"));
        prompt->setPlaceholderText(QStringLiteral("What should the agent do?"));
        form->addWidget(prompt);
        auto* deliver = new QComboBox(dialog);
        deliver->setObjectName(QStringLiteral("cronCreateDeliver"));
        for (int i = 0; i < delivery_ids.size(); ++i) {
          deliver->addItem(delivery_names.at(i), delivery_ids.at(i));
        }
        if (deliver->count() == 0) {
          deliver->addItem(QStringLiteral("Local (save only)"),
                           QStringLiteral("local"));
        }
        form->addWidget(deliver);
        auto* buttons = new QWidget(dialog);
        auto* row = new QHBoxLayout(buttons);
        row->setContentsMargins(0, 0, 0, 0);
        auto* create = Button(buttons, QStringLiteral("Create"));
        create->setObjectName(QStringLiteral("cronCreateSubmit"));
        QObject::connect(create, &QPushButton::clicked, dialog, [=]() {
          if (schedule->text().trimmed().isEmpty()) {
            return;
          }
          act(QStringLiteral("cron"), QStringLiteral("POST"),
              QStringLiteral("/api/cron/jobs") + profile_query,
              QJsonObject{
                  {QStringLiteral("name"), name->text().trimmed()},
                  {QStringLiteral("schedule"), schedule->text().trimmed()},
                  {QStringLiteral("prompt"), prompt->toPlainText()},
                  {QStringLiteral("deliver"),
                   deliver->currentData().toString()}});
          dialog->accept();
        });
        auto* cancel = Button(buttons, QStringLiteral("Cancel"));
        QObject::connect(cancel, &QPushButton::clicked, dialog,
                         [dialog]() { dialog->reject(); });
        row->addWidget(create);
        row->addWidget(cancel);
        row->addStretch(1);
        form->addWidget(buttons);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->resize(460, 320);
        dialog->open();
      });
  layout->addWidget(new_job, 0, Qt::AlignLeft);

  if (jobs.isEmpty() && state != nullptr && state->loaded) {
    layout->addWidget(Description(page, QStringLiteral("No cron jobs.")));
  }
  const QJsonObject runs =
      state == nullptr ? QJsonObject() : ObjectField(state->data, "runs");
  for (const QJsonValue& value : jobs) {
    const QJsonObject job = value.toObject();
    const QString id = job.value(QStringLiteral("id")).toString();
    const QString name = job.value(QStringLiteral("name")).toString();
    // Jobs store a parsed schedule plus its display string; next_run_at is ISO.
    QString schedule = job.value(QStringLiteral("schedule_display")).toString();
    if (schedule.isEmpty()) {
      const QJsonValue raw = job.value(QStringLiteral("schedule"));
      schedule = raw.isObject()
                     ? Text(raw.toObject().value(QStringLiteral("expr")))
                     : Text(raw);
    }
    const QString next = job.value(QStringLiteral("next_run_at")).toString();
    const bool enabled = job.value(QStringLiteral("enabled")).toBool();
    const QString deliver = job.value(QStringLiteral("deliver")).toString();
    QStringList detail;
    if (!schedule.isEmpty()) {
      detail.append(schedule);
    }
    if (!next.isEmpty()) {
      detail.append(QStringLiteral("next ") + next);
    }
    detail.append(enabled ? QStringLiteral("enabled")
                          : QStringLiteral("paused"));
    if (!deliver.isEmpty()) {
      detail.append(QStringLiteral("→ ") + deliver);
    }
    CronCard card = AddCronCard(page, layout, id, name.isEmpty() ? id : name,
                                detail.join(QStringLiteral(" · ")));

    auto* pause = CronActionButton(
        card.widget, QStringLiteral("cronToggle_") + id, 0xEAD1,
        enabled ? QStringLiteral("Pause task") : QStringLiteral("Resume task"),
        context.theme);
    if (!enabled) {
      pause->setText(QString(QChar(0xEB2C)));
    }
    pause->setEnabled(static_cast<bool>(act));
    QObject::connect(pause, &QPushButton::clicked, pause, [=]() {
      act(QStringLiteral("cron"), QStringLiteral("POST"),
          QStringLiteral("/api/cron/jobs/%1/%2")
                  .arg(id, enabled ? QStringLiteral("pause")
                                   : QStringLiteral("resume")) +
              profile_query,
          QJsonObject());
    });
    card.actions->addWidget(pause);

    auto* edit =
        CronActionButton(card.widget, QStringLiteral("cronEdit_") + id, 0xEA73,
                         QStringLiteral("Edit task"), context.theme);
    edit->setEnabled(static_cast<bool>(act));
    QObject::connect(
        edit, &QPushButton::clicked, edit,
        [page, act, job, id, delivery_ids, delivery_names, profile_query]() {
          auto* dialog = new QDialog(page);
          dialog->setObjectName(QStringLiteral("cronEditDialog_") + id);
          auto* form = new QVBoxLayout(dialog);
          auto field = [dialog, form](const QString& label,
                                      const QString& value,
                                      const QString& object_name) {
            form->addWidget(new QLabel(label, dialog));
            auto* input = new QLineEdit(value, dialog);
            input->setObjectName(object_name);
            form->addWidget(input);
            return input;
          };
          const QString initial_name =
              job.value(QStringLiteral("name")).toString();
          QString initial_schedule =
              job.value(QStringLiteral("schedule_display")).toString();
          if (initial_schedule.isEmpty()) {
            const QJsonValue raw = job.value(QStringLiteral("schedule"));
            initial_schedule =
                raw.isObject()
                    ? Text(raw.toObject().value(QStringLiteral("expr")))
                    : Text(raw);
          }
          const QString initial_prompt =
              job.value(QStringLiteral("prompt")).toString();
          const QString initial_deliver =
              job.value(QStringLiteral("deliver")).toString();
          const QString initial_model =
              job.value(QStringLiteral("model")).toString();
          const QString initial_provider =
              job.value(QStringLiteral("provider")).toString();

          auto* name = field(QStringLiteral("Name"), initial_name,
                             QStringLiteral("cronEditName"));
          auto* schedule = field(QStringLiteral("Schedule"), initial_schedule,
                                 QStringLiteral("cronEditSchedule"));
          form->addWidget(new QLabel(QStringLiteral("Prompt"), dialog));
          auto* prompt = new QPlainTextEdit(dialog);
          prompt->setObjectName(QStringLiteral("cronEditPrompt"));
          prompt->setPlainText(initial_prompt);
          form->addWidget(prompt, 1);
          form->addWidget(new QLabel(QStringLiteral("Delivery"), dialog));
          auto* deliver = new QComboBox(dialog);
          deliver->setObjectName(QStringLiteral("cronEditDeliver"));
          for (int i = 0; i < delivery_ids.size(); ++i) {
            deliver->addItem(delivery_names.at(i), delivery_ids.at(i));
          }
          int deliver_index = deliver->findData(initial_deliver);
          if (deliver_index < 0 && !initial_deliver.isEmpty()) {
            deliver->addItem(initial_deliver, initial_deliver);
            deliver_index = deliver->count() - 1;
          }
          if (deliver_index >= 0) {
            deliver->setCurrentIndex(deliver_index);
          }
          form->addWidget(deliver);
          auto* model = field(QStringLiteral("Model"), initial_model,
                              QStringLiteral("cronEditModel"));
          auto* provider = field(QStringLiteral("Provider"), initial_provider,
                                 QStringLiteral("cronEditProvider"));

          auto* buttons = new QWidget(dialog);
          auto* button_row = new QHBoxLayout(buttons);
          button_row->setContentsMargins(0, 0, 0, 0);
          auto* save = Button(buttons, QStringLiteral("Save"));
          save->setObjectName(QStringLiteral("cronEditSubmit"));
          QObject::connect(save, &QPushButton::clicked, dialog, [=]() {
            QJsonObject updates;
            if (name->text().trimmed() != initial_name) {
              updates.insert(QStringLiteral("name"), name->text().trimmed());
            }
            if (schedule->text().trimmed() != initial_schedule) {
              updates.insert(QStringLiteral("schedule"),
                             schedule->text().trimmed());
            }
            if (prompt->toPlainText() != initial_prompt) {
              updates.insert(QStringLiteral("prompt"), prompt->toPlainText());
            }
            if (deliver->currentData().toString() != initial_deliver) {
              updates.insert(QStringLiteral("deliver"),
                             deliver->currentData().toString());
            }
            if (model->text().trimmed() != initial_model) {
              updates.insert(QStringLiteral("model"), model->text().trimmed());
            }
            if (provider->text().trimmed() != initial_provider) {
              updates.insert(QStringLiteral("provider"),
                             provider->text().trimmed());
            }
            if (updates.isEmpty()) {
              dialog->accept();
              return;
            }
            act(QStringLiteral("cron"), QStringLiteral("PUT"),
                QStringLiteral("/api/cron/jobs/%1").arg(id) + profile_query,
                QJsonObject{{QStringLiteral("updates"), updates}});
            dialog->accept();
          });
          auto* cancel = Button(buttons, QStringLiteral("Cancel"));
          QObject::connect(cancel, &QPushButton::clicked, dialog,
                           [dialog]() { dialog->reject(); });
          button_row->addWidget(save);
          button_row->addWidget(cancel);
          button_row->addStretch(1);
          form->addWidget(buttons);
          dialog->setAttribute(Qt::WA_DeleteOnClose);
          dialog->resize(460, 420);
          dialog->open();
        });
    card.actions->addWidget(edit);

    auto* trigger =
        CronActionButton(card.widget, QStringLiteral("cronTrigger_") + id,
                         0xEBA6, QStringLiteral("Run task now"), context.theme);
    trigger->setEnabled(static_cast<bool>(act));
    QObject::connect(trigger, &QPushButton::clicked, trigger, [=]() {
      act(QStringLiteral("cron"), QStringLiteral("POST"),
          QStringLiteral("/api/cron/jobs/%1/trigger").arg(id) + profile_query,
          QJsonObject());
    });
    card.actions->addWidget(trigger);

    auto* remove =
        CronActionButton(card.widget, QStringLiteral("cronDelete_") + id,
                         0xEA81, QStringLiteral("Delete task"), context.theme);
    remove->setEnabled(static_cast<bool>(act));
    QObject::connect(remove, &QPushButton::clicked, remove, [=]() {
      act(QStringLiteral("cron"), QStringLiteral("DELETE"),
          QStringLiteral("/api/cron/jobs/%1").arg(id) + profile_query,
          QJsonObject());
    });
    card.actions->addWidget(remove);

    const QJsonArray job_runs = runs.value(id).toArray();
    if (!job_runs.isEmpty()) {
      auto* history = Label(
          card.widget,
          QStringLiteral("Recent runs: ") +
              [job_runs]() {
                QStringList run_parts;
                int shown = 0;
                for (const QJsonValue& run_value : job_runs) {
                  if (shown++ >= 5) {
                    break;
                  }
                  const QJsonObject run = run_value.toObject();
                  QString status =
                      run.value(QStringLiteral("status"))
                          .toString(
                              run.value(QStringLiteral("state")).toString());
                  if (status.isEmpty()) {
                    status = run.value(QStringLiteral("end_reason")).toString();
                  }
                  const QJsonValue started_value =
                      run.value(QStringLiteral("started_at"));
                  QString started;
                  if (started_value.isDouble()) {
                    started = QDateTime::fromSecsSinceEpoch(
                                  static_cast<qint64>(started_value.toDouble()))
                                  .toString(QStringLiteral("MMM d HH:mm"));
                  } else {
                    started = started_value.toString();
                  }
                  QString title = run.value(QStringLiteral("title")).toString();
                  if (title.isEmpty()) {
                    title = run.value(QStringLiteral("preview")).toString();
                  }
                  QStringList parts;
                  if (!started.isEmpty()) {
                    parts.append(started);
                  }
                  if (!title.isEmpty()) {
                    parts.append(title);
                  }
                  if (!status.isEmpty()) {
                    parts.append(status);
                  }
                  const QString model =
                      run.value(QStringLiteral("model")).toString();
                  if (!model.isEmpty()) {
                    parts.append(model);
                  }
                  if (run.contains(QStringLiteral("tool_call_count"))) {
                    parts.append(
                        QStringLiteral("%1 tools")
                            .arg(run.value(QStringLiteral("tool_call_count"))
                                     .toInt()));
                  }
                  if (run.contains(QStringLiteral("message_count"))) {
                    parts.append(QStringLiteral("%1 msgs").arg(
                        run.value(QStringLiteral("message_count")).toInt()));
                  }
                  if (run.contains(QStringLiteral("estimated_cost_usd"))) {
                    parts.append(QStringLiteral("$%1").arg(
                        run.value(QStringLiteral("estimated_cost_usd"))
                            .toDouble(),
                        0, 'f', 4));
                  }
                  parts.removeAll(QString());
                  if (parts.isEmpty()) {
                    parts.append(QStringLiteral("run"));
                  }
                  run_parts.append(parts.join(QStringLiteral(" · ")));
                }
                return run_parts.join(QStringLiteral(" | "));
              }(),
          "settingDescription");
      history->setObjectName(QStringLiteral("cronRuns_") + id);
      history->setWordWrap(true);
      card.labels->addWidget(history);
    }
  }

  const QJsonArray blueprints = ArrayField(
      state == nullptr ? QJsonObject() : ObjectField(state->data, "blueprints"),
      {"blueprints"});
  if (!blueprints.isEmpty()) {
    layout->addWidget(
        Label(page, QStringLiteral("Blueprints"), "settingTitle"));
    int shown = 0;
    for (const QJsonValue& value : blueprints) {
      if (shown++ >= 12) {
        break;
      }
      const QJsonObject blueprint = value.toObject();
      const QString key = blueprint.value(QStringLiteral("key")).toString();
      const QString title = blueprint.value(QStringLiteral("title")).toString();
      Row row =
          AddRow(page, layout, title.isEmpty() ? key : title,
                 blueprint.value(QStringLiteral("description")).toString());
      row.widget->setObjectName(QStringLiteral("cronBlueprint_") + key);
      auto* use = Button(row.widget, QStringLiteral("Use"));
      use->setObjectName(QStringLiteral("cronBlueprintUse_") + key);
      use->setEnabled(static_cast<bool>(act));
      const QJsonArray fields =
          blueprint.value(QStringLiteral("fields")).toArray();
      // The catalog describes each slot's type, default and options; the form
      // is built from that schema instead of assuming a fixed blueprint shape.
      QObject::connect(use, &QPushButton::clicked, use, [=]() {
        auto* dialog = new QDialog(page);
        auto* form = new QVBoxLayout(dialog);
        QHash<QString, QLineEdit*> edits;
        QHash<QString, QComboBox*> combos;
        for (const QJsonValue& field_value : fields) {
          const QJsonObject field = field_value.toObject();
          const QString field_name =
              field.value(QStringLiteral("name")).toString();
          if (field_name.isEmpty()) {
            continue;
          }
          form->addWidget(new QLabel(
              field.value(QStringLiteral("label")).toString(field_name),
              dialog));
          const QJsonArray options =
              field.value(QStringLiteral("options")).toArray();
          if (field.value(QStringLiteral("type")).toString() ==
                  QStringLiteral("enum") &&
              !options.isEmpty()) {
            auto* combo = new QComboBox(dialog);
            for (const QJsonValue& option : options) {
              combo->addItem(Text(option), Text(option));
            }
            const QString def = Text(field.value(QStringLiteral("default")));
            const int index = combo->findData(def);
            if (index >= 0) {
              combo->setCurrentIndex(index);
            }
            combos.insert(field_name, combo);
            form->addWidget(combo);
          } else {
            auto* edit = new QLineEdit(dialog);
            edit->setText(Text(field.value(QStringLiteral("default"))));
            edits.insert(field_name, edit);
            form->addWidget(edit);
          }
        }
        auto* buttons = new QWidget(dialog);
        auto* row_buttons = new QHBoxLayout(buttons);
        row_buttons->setContentsMargins(0, 0, 0, 0);
        auto* create = Button(buttons, QStringLiteral("Create"));
        QObject::connect(create, &QPushButton::clicked, dialog, [=]() {
          QJsonObject values;
          for (auto it = edits.constBegin(); it != edits.constEnd(); ++it) {
            values.insert(it.key(), it.value()->text());
          }
          for (auto it = combos.constBegin(); it != combos.constEnd(); ++it) {
            values.insert(it.key(), it.value()->currentData().toString());
          }
          act(QStringLiteral("cron"), QStringLiteral("POST"),
              QStringLiteral("/api/cron/blueprints/instantiate") +
                  profile_query,
              QJsonObject{{QStringLiteral("blueprint"), key},
                          {QStringLiteral("values"), values}});
          dialog->accept();
        });
        auto* cancel = Button(buttons, QStringLiteral("Cancel"));
        QObject::connect(cancel, &QPushButton::clicked, dialog,
                         [dialog]() { dialog->reject(); });
        row_buttons->addWidget(create);
        row_buttons->addWidget(cancel);
        row_buttons->addStretch(1);
        form->addWidget(buttons);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->resize(420, 260);
        dialog->open();
      });
      row.layout->addWidget(use);
    }
  }
  return page;
}

QString Bytes(int64_t bytes) {
  if (bytes <= 0) {
    return QStringLiteral("0 GB");
  }
  return QStringLiteral("%1 GB").arg(
      static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0), 0, 'f', 1);
}

QWidget* LocalModelsPage(const PageContext& context, QWidget* parent) {
  auto* page = new QWidget(parent);
  page->setObjectName(QStringLiteral("settingsPage:localmodels"));
  auto* layout = PageLayout(page);
  AddHeading(page, layout, context, QStringLiteral("localmodels"),
             QStringLiteral("Local models"));

  const PageState* state = PageStateFor(context, QStringLiteral("localmodels"));
  const QJsonObject data = state == nullptr ? QJsonObject() : state->data;
  const QJsonObject status = ObjectField(data, "status");
  const QJsonObject hardware = ObjectField(data, "hardware");
  const QJsonArray models =
      ArrayField(ObjectField(data, "catalog"), {"models"});
  const auto act = context.action;
  const QString profile =
      context.snapshot == nullptr ? QString() : context.snapshot->name;
  const QString profile_query =
      profile.isEmpty()
          ? QString()
          : QStringLiteral("?profile=%1")
                .arg(QString::fromLatin1(QUrl::toPercentEncoding(profile)));

  layout->addWidget(Label(page, QStringLiteral("Runtime"), "settingTitle"));
  AddRowIfPresent(page, layout, QStringLiteral("Enabled"),
                  Text(status.value(QStringLiteral("enabled"))));
  AddRowIfPresent(page, layout, QStringLiteral("Runtime installed"),
                  Text(status.value(QStringLiteral("runtime_installed"))));
  AddRowIfPresent(page, layout, QStringLiteral("Backend"),
                  Text(status.value(QStringLiteral("runtime_backend"))));
  AddRowIfPresent(page, layout, QStringLiteral("Server"),
                  Text(status.value(QStringLiteral("server_running"))));
  AddRowIfPresent(page, layout, QStringLiteral("Base URL"),
                  status.value(QStringLiteral("server_base_url")).toString());
  AddRowIfPresent(page, layout, QStringLiteral("Active model"),
                  status.value(QStringLiteral("active_model_id")).toString());
  AddRowIfPresent(page, layout, QStringLiteral("Models directory"),
                  status.value(QStringLiteral("models_dir")).toString());

  if (!hardware.isEmpty()) {
    layout->addWidget(Label(page, QStringLiteral("Hardware"), "settingTitle"));
    if (hardware.value(QStringLiteral("uma")).toBool()) {
      AddRow(page, layout, QStringLiteral("Memory"),
             QStringLiteral("unified (Apple silicon)"));
    }
    AddRowIfPresent(
        page, layout, QStringLiteral("GPU memory"),
        hardware.value(QStringLiteral("vram_label")).toString().isEmpty()
            ? Bytes(static_cast<int64_t>(
                  hardware.value(QStringLiteral("vram_total_bytes"))
                      .toDouble()))
            : hardware.value(QStringLiteral("vram_label")).toString());
    AddRow(
        page, layout, QStringLiteral("RAM available"),
        Bytes(static_cast<int64_t>(
            hardware.value(QStringLiteral("ram_available_bytes")).toDouble())));
  }

  auto* server_row = new QWidget(page);
  auto* server_layout = new QHBoxLayout(server_row);
  server_layout->setContentsMargins(0, 0, 0, 0);
  const bool running = status.value(QStringLiteral("server_running")).toBool();
  auto* server = Button(server_row, running ? QStringLiteral("Stop server")
                                            : QStringLiteral("Start server"));
  server->setObjectName(QStringLiteral("localModelsServer"));
  server->setEnabled(static_cast<bool>(act));
  QObject::connect(
      server, &QPushButton::clicked, server, [act, running, profile_query]() {
        act(QStringLiteral("localmodels"), QStringLiteral("POST"),
            QStringLiteral("/api/local-models/server") + profile_query,
            QJsonObject{
                {QStringLiteral("action"),
                 running ? QStringLiteral("stop") : QStringLiteral("start")}});
      });
  server_layout->addWidget(server);
  server_layout->addStretch(1);
  layout->addWidget(server_row);

  layout->addWidget(Label(
      page, QStringLiteral("Catalog (%1)").arg(models.size()), "settingTitle"));
  int shown = 0;
  for (const QJsonValue& value : models) {
    if (shown++ >= 20) {
      break;
    }
    const QJsonObject model = value.toObject();
    const QString id = model.value(QStringLiteral("id")).toString();
    const QString name =
        model.value(QStringLiteral("display_name")).toString(id);
    const bool downloaded = model.value(QStringLiteral("downloaded")).toBool();
    const bool fits = model.value(QStringLiteral("fits")).toBool();
    QStringList detail;
    if (!model.value(QStringLiteral("size_label")).toString().isEmpty()) {
      detail.append(model.value(QStringLiteral("size_label")).toString());
    }
    detail.append(downloaded ? QStringLiteral("downloaded")
                             : (fits ? QStringLiteral("fits this machine")
                                     : QStringLiteral("needs more memory")));
    if (!model.value(QStringLiteral("fit_summary")).toString().isEmpty() &&
        !fits) {
      detail.append(model.value(QStringLiteral("fit_summary")).toString());
    }
    Row row = AddRow(page, layout, name, detail.join(QStringLiteral(" · ")));
    row.widget->setObjectName(QStringLiteral("localModelRow_") + id);
    row.widget->setMaximumWidth(900);
    if (!downloaded) {
      auto* download = Button(row.widget, QStringLiteral("Download"));
      download->setObjectName(QStringLiteral("localModelDownload_") + id);
      download->setEnabled(static_cast<bool>(act));
      QObject::connect(download, &QPushButton::clicked, download, [=]() {
        act(QStringLiteral("localmodels"), QStringLiteral("POST"),
            QStringLiteral("/api/local-models/download") + profile_query,
            QJsonObject{{QStringLiteral("model_id"), id}});
      });
      row.layout->addWidget(download);
    } else {
      auto* activate = Button(row.widget, QStringLiteral("Use"));
      activate->setObjectName(QStringLiteral("localModelActivate_") + id);
      activate->setEnabled(static_cast<bool>(act));
      QObject::connect(activate, &QPushButton::clicked, activate, [=]() {
        act(QStringLiteral("localmodels"), QStringLiteral("POST"),
            QStringLiteral("/api/local-models/activate") + profile_query,
            QJsonObject{{QStringLiteral("model_id"), id}});
      });
      row.layout->addWidget(activate);
      auto* remove = Button(row.widget, QStringLiteral("Delete"));
      remove->setObjectName(QStringLiteral("localModelDelete_") + id);
      remove->setEnabled(static_cast<bool>(act));
      QObject::connect(remove, &QPushButton::clicked, remove, [=]() {
        act(QStringLiteral("localmodels"), QStringLiteral("DELETE"),
            QStringLiteral("/api/local-models/models/%1").arg(id) +
                profile_query,
            QJsonObject());
      });
      row.layout->addWidget(remove);
    }
  }
  return page;
}

QWidget* DelegationPage(const PageContext& context, QWidget* parent) {
  auto* page = new QWidget(parent);
  page->setObjectName(QStringLiteral("settingsPage:delegation"));
  auto* layout = PageLayout(page);
  AddHeading(page, layout, context, QStringLiteral("delegation"),
             QStringLiteral("Delegation"));

  const PageState* state = PageStateFor(context, QStringLiteral("delegation"));
  const QJsonObject routing =
      state == nullptr ? QJsonObject() : ObjectField(state->data, "routing");
  const auto act = context.action;
  const QString profile =
      context.snapshot == nullptr ? QString() : context.snapshot->name;
  const QString profile_query =
      profile.isEmpty()
          ? QString()
          : QStringLiteral("?profile=%1")
                .arg(QString::fromLatin1(QUrl::toPercentEncoding(profile)));

  const QString configured =
      routing.value(QStringLiteral("configured")).toString();
  const QString blocked =
      routing.value(QStringLiteral("blocked_reason")).toString();
  AddRow(page, layout, QStringLiteral("Backend"),
         configured.isEmpty() ? QStringLiteral("default") : configured);
  if (!blocked.isEmpty()) {
    auto* error = Description(page, QStringLiteral("Blocked: ") + blocked);
    layout->addWidget(error);
  }

  const QJsonArray backends = ArrayField(routing, {"backends"});
  auto* backend_row = new QWidget(page);
  auto* backend_layout = new QHBoxLayout(backend_row);
  backend_layout->setContentsMargins(0, 0, 0, 0);
  auto* backend = new QComboBox(backend_row);
  backend->setObjectName(QStringLiteral("delegationBackend"));
  for (const QJsonValue& value : backends) {
    const QJsonObject entry = value.toObject();
    const QString name = entry.value(QStringLiteral("name")).toString();
    const QString label = entry.value(QStringLiteral("label")).toString(name);
    const bool available = entry.value(QStringLiteral("available")).toBool();
    backend->addItem(available
                         ? label
                         : label + QStringLiteral(" — ") +
                               entry.value(QStringLiteral("reason")).toString(),
                     name);
    backend->setItemData(backend->count() - 1, available, Qt::UserRole + 1);
  }
  const int current = backend->findData(configured);
  if (current >= 0) {
    backend->setCurrentIndex(current);
  }
  backend_layout->addWidget(backend, 1);
  auto* apply = Button(backend_row, QStringLiteral("Apply"));
  apply->setObjectName(QStringLiteral("delegationApply"));
  apply->setEnabled(static_cast<bool>(act));
  QObject::connect(
      apply, &QPushButton::clicked, apply, [act, backend, profile_query]() {
        act(QStringLiteral("delegation"), QStringLiteral("PUT"),
            QStringLiteral("/api/delegation/backend") + profile_query,
            QJsonObject{{QStringLiteral("backend"),
                         backend->currentData().toString()}});
      });
  backend_layout->addWidget(apply);
  layout->addWidget(backend_row);

  const QJsonObject model_map = ObjectField(routing, "model_map");
  const QJsonArray model_options = ArrayField(model_map, {"options"});
  const QString model_default =
      model_map.value(QStringLiteral("default")).toString();
  layout->addWidget(
      Label(page, QStringLiteral("Model routes"), "settingTitle"));
  layout->addWidget(Description(
      page, QStringLiteral("Named routes the agent reads when choosing a "
                           "delegation model. Fallbacks are tried in order.")));
  if (model_options.isEmpty()) {
    layout->addWidget(Description(
        page, QStringLiteral("No model routes. Subagents use the default "
                             "delegation model.")));
  }

  auto fallback_text = [](const QJsonArray& entries) {
    QStringList lines;
    for (const QJsonValue& value : entries) {
      if (value.isObject()) {
        const QJsonObject entry = value.toObject();
        const QString provider =
            entry.value(QStringLiteral("provider")).toString();
        const QString model = entry.value(QStringLiteral("model")).toString();
        if (!provider.isEmpty() || !model.isEmpty()) {
          lines.append(provider + QStringLiteral("/") + model);
        }
      } else if (value.isString()) {
        lines.append(value.toString());
      }
    }
    return lines.join(QStringLiteral("\n"));
  };
  auto parse_fallbacks = [](const QString& text, const QJsonArray& previous) {
    QJsonArray entries;
    for (const QString& raw :
         text.split(QRegularExpression(QStringLiteral("[\\n,]")),
                    Qt::SkipEmptyParts)) {
      const QString line = raw.trimmed();
      if (line.isEmpty()) {
        continue;
      }
      const int slash = line.indexOf(QLatin1Char('/'));
      const QString provider =
          slash > 0 ? line.left(slash).trimmed() : QString();
      const QString model = slash > 0 ? line.mid(slash + 1).trimmed() : line;
      if (model.isEmpty()) {
        continue;
      }
      QJsonObject entry;
      for (const QJsonValue& value : previous) {
        const QJsonObject old = value.toObject();
        if (old.value(QStringLiteral("provider")).toString() == provider &&
            old.value(QStringLiteral("model")).toString() == model) {
          entry = old;
          break;
        }
      }
      entry.insert(QStringLiteral("provider"), provider);
      entry.insert(QStringLiteral("model"), model);
      entries.append(entry);
    }
    return entries;
  };
  auto save_model_map = [act, profile_query](const QJsonArray& options,
                                             const QString& default_route) {
    QJsonObject payload{{QStringLiteral("options"), options}};
    if (!default_route.isEmpty()) {
      payload.insert(QStringLiteral("default"), default_route);
    }
    act(QStringLiteral("delegation"), QStringLiteral("PUT"),
        QStringLiteral("/api/delegation/backends/model_map") + profile_query,
        payload);
  };
  auto open_model_editor = [page, context, model_options, model_default,
                            save_model_map, fallback_text,
                            parse_fallbacks](const QJsonObject& initial,
                                             const QString& original_name) {
    auto* dialog = new QDialog(page);
    dialog->setObjectName(QStringLiteral("delegationModelDialog"));
    dialog->setWindowTitle(original_name.isEmpty()
                               ? QStringLiteral("Add model route")
                               : QStringLiteral("Edit model route"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    auto* dialog_layout = new QVBoxLayout(dialog);
    auto* form = new QFormLayout();
    auto add_field = [&](const QString& label, QLineEdit* field) {
      field->setMinimumWidth(320);
      form->addRow(label, field);
    };
    auto* name =
        new QLineEdit(initial.value(QStringLiteral("name")).toString(), dialog);
    name->setObjectName(QStringLiteral("delegationRouteName"));
    auto* description = new QLineEdit(
        initial.value(QStringLiteral("description")).toString(), dialog);
    description->setObjectName(QStringLiteral("delegationRouteDescription"));
    auto* provider = new QLineEdit(
        initial.value(QStringLiteral("provider")).toString(), dialog);
    provider->setObjectName(QStringLiteral("delegationRouteProvider"));
    auto* model = new QLineEdit(
        initial.value(QStringLiteral("model")).toString(), dialog);
    model->setObjectName(QStringLiteral("delegationRouteModel"));
    auto* base_url = new QLineEdit(
        initial.value(QStringLiteral("base_url")).toString(), dialog);
    base_url->setObjectName(QStringLiteral("delegationRouteBaseUrl"));
    auto* api_key = new QLineEdit(dialog);
    api_key->setObjectName(QStringLiteral("delegationRouteApiKey"));
    const QString saved_key =
        initial.value(QStringLiteral("api_key")).toString();
    api_key->setPlaceholderText(
        saved_key.isEmpty()
            ? QStringLiteral("Optional API key")
            : QStringLiteral("Saved key — leave blank to keep"));
    if (!saved_key.isEmpty()) {
      api_key->setEchoMode(QLineEdit::Password);
    }
    auto* reasoning = new QLineEdit(
        initial.value(QStringLiteral("reasoning_effort")).toString(), dialog);
    reasoning->setObjectName(QStringLiteral("delegationRouteReasoning"));
    auto* service_tier = new QLineEdit(
        initial.value(QStringLiteral("service_tier")).toString(), dialog);
    service_tier->setObjectName(QStringLiteral("delegationRouteServiceTier"));
    auto* fallbacks = new QPlainTextEdit(
        fallback_text(
            initial.value(QStringLiteral("fallback_providers")).toArray()),
        dialog);
    fallbacks->setObjectName(QStringLiteral("delegationRouteFallbacks"));
    fallbacks->setPlaceholderText(
        QStringLiteral("provider/model — one per line"));
    fallbacks->setFixedHeight(84);
    add_field(QStringLiteral("Name"), name);
    add_field(QStringLiteral("Description"), description);
    add_field(QStringLiteral("Provider"), provider);
    add_field(QStringLiteral("Model"), model);
    add_field(QStringLiteral("Base URL"), base_url);
    add_field(QStringLiteral("API key"), api_key);
    add_field(QStringLiteral("Reasoning effort"), reasoning);
    add_field(QStringLiteral("Service tier"), service_tier);
    form->addRow(QStringLiteral("Fallbacks"), fallbacks);
    dialog_layout->addLayout(form);
    auto* status = new QLabel(dialog);
    status->setObjectName(QStringLiteral("delegationRouteStatus"));
    status->setWordWrap(true);
    status->setStyleSheet(
        QStringLiteral("color: %1;").arg(context.theme.danger.name()));
    dialog_layout->addWidget(status);
    auto* actions = new QHBoxLayout();
    actions->addStretch(1);
    auto* cancel = Button(dialog, QStringLiteral("Cancel"));
    auto* save = Button(dialog, QStringLiteral("Save"));
    save->setObjectName(QStringLiteral("delegationRouteSave"));
    actions->addWidget(cancel);
    actions->addWidget(save);
    dialog_layout->addLayout(actions);
    QObject::connect(cancel, &QPushButton::clicked, dialog, &QDialog::reject);
    QObject::connect(save, &QPushButton::clicked, dialog, [=]() {
      const QString route_name = name->text().trimmed();
      if (route_name.isEmpty() || description->text().trimmed().isEmpty() ||
          model->text().trimmed().isEmpty() ||
          (provider->text().trimmed().isEmpty() &&
           base_url->text().trimmed().isEmpty())) {
        status->setText(
            QStringLiteral("Name, description, model and provider/base URL "
                           "are required."));
        return;
      }
      QJsonObject updated = initial;
      updated.insert(QStringLiteral("name"), route_name);
      updated.insert(QStringLiteral("description"),
                     description->text().trimmed());
      for (const auto& pair :
           std::initializer_list<std::pair<const char*, QLineEdit*>>{
               {"provider", provider},
               {"model", model},
               {"base_url", base_url},
               {"reasoning_effort", reasoning},
               {"service_tier", service_tier}}) {
        const QString value = pair.second->text().trimmed();
        if (value.isEmpty()) {
          updated.remove(QLatin1String(pair.first));
        } else {
          updated.insert(QLatin1String(pair.first), value);
        }
      }
      if (!api_key->text().isEmpty()) {
        updated.insert(QStringLiteral("api_key"), api_key->text());
      }
      const QJsonArray parsed = parse_fallbacks(
          fallbacks->toPlainText(),
          updated.value(QStringLiteral("fallback_providers")).toArray());
      if (parsed.isEmpty()) {
        updated.remove(QStringLiteral("fallback_providers"));
      } else {
        updated.insert(QStringLiteral("fallback_providers"), parsed);
      }
      QJsonArray options;
      bool replaced = false;
      for (const QJsonValue& value : model_options) {
        const QJsonObject option = value.toObject();
        if (!original_name.isEmpty() &&
            option.value(QStringLiteral("name")).toString() == original_name) {
          options.append(updated);
          replaced = true;
        } else {
          options.append(option);
        }
      }
      if (!replaced) {
        options.append(updated);
      }
      QString default_route = model_default;
      if (!original_name.isEmpty() && default_route == original_name) {
        default_route = route_name;
      }
      save_model_map(options, default_route);
      dialog->accept();
    });
    dialog->open();
    return dialog;
  };

  for (const QJsonValue& value : model_options) {
    const QJsonObject option = value.toObject();
    const QString name = option.value(QStringLiteral("name")).toString();
    QStringList detail;
    for (const char* key : {"provider", "model", "description"}) {
      const QString text = Text(option.value(QLatin1String(key)));
      if (!text.isEmpty()) {
        detail.append(text);
      }
    }
    const QString fallback = fallback_text(
        option.value(QStringLiteral("fallback_providers")).toArray());
    if (!fallback.isEmpty()) {
      detail.append(
          QStringLiteral("fallbacks: ") +
          fallback.split(QLatin1Char('\n')).join(QStringLiteral(", ")));
    }
    Row row = AddRow(page, layout, name, detail.join(QStringLiteral(" · ")));
    row.widget->setObjectName(QStringLiteral("delegationModel_") + name);
    auto* edit = Button(row.widget, QStringLiteral("Edit"));
    edit->setObjectName(QStringLiteral("delegationModelEdit_") + name);
    edit->setEnabled(static_cast<bool>(act));
    QObject::connect(edit, &QPushButton::clicked, edit,
                     [open_model_editor, option, name]() {
                       open_model_editor(option, name);
                     });
    row.layout->addWidget(edit);
    auto* remove = Button(row.widget, QStringLiteral("Remove"));
    remove->setObjectName(QStringLiteral("delegationModelRemove_") + name);
    remove->setEnabled(static_cast<bool>(act));
    QObject::connect(remove, &QPushButton::clicked, remove, [=]() {
      QJsonArray kept;
      for (const QJsonValue& other : model_options) {
        const QJsonObject object = other.toObject();
        if (object.value(QStringLiteral("name")).toString() != name) {
          kept.append(object);
        }
      }
      save_model_map(kept, model_default == name ? QString() : model_default);
    });
    row.layout->addWidget(remove);
  }
  auto* model_actions = new QWidget(page);
  auto* model_actions_layout = new QHBoxLayout(model_actions);
  model_actions_layout->setContentsMargins(0, 0, 0, 0);
  auto* add_model = Button(model_actions, QStringLiteral("Add route"));
  add_model->setObjectName(QStringLiteral("delegationModelAdd"));
  add_model->setEnabled(static_cast<bool>(act));
  QObject::connect(
      add_model, &QPushButton::clicked, add_model,
      [open_model_editor]() { open_model_editor(QJsonObject(), QString()); });
  model_actions_layout->addWidget(add_model);
  model_actions_layout->addSpacing(12);
  model_actions_layout->addWidget(
      Label(model_actions, QStringLiteral("Default"), "settingBody"));
  auto* model_default_combo = new QComboBox(model_actions);
  model_default_combo->setObjectName(QStringLiteral("delegationModelDefault"));
  model_default_combo->addItem(QStringLiteral("None"), QString());
  for (const QJsonValue& value : model_options) {
    const QString name =
        value.toObject().value(QStringLiteral("name")).toString();
    if (!name.isEmpty()) {
      model_default_combo->addItem(name, name);
    }
  }
  const int model_default_index = model_default_combo->findData(model_default);
  if (model_default_index >= 0) {
    model_default_combo->setCurrentIndex(model_default_index);
  }
  model_actions_layout->addWidget(model_default_combo);
  auto* apply_model_default =
      Button(model_actions, QStringLiteral("Apply default"));
  apply_model_default->setObjectName(
      QStringLiteral("delegationModelDefaultApply"));
  apply_model_default->setEnabled(static_cast<bool>(act));
  QObject::connect(
      apply_model_default, &QPushButton::clicked, apply_model_default, [=]() {
        save_model_map(model_options,
                       model_default_combo->currentData().toString());
      });
  model_actions_layout->addWidget(apply_model_default);
  layout->addWidget(model_actions);
  AddRowIfPresent(page, layout, QStringLiteral("Default route"), model_default);

  // Profile routes: a list of {name, description} plus an optional default.
  const QJsonObject routes = ObjectField(routing, "profile_routes");
  const QJsonArray route_rows = ArrayField(routes, {"profiles"});
  const QString route_default =
      routes.value(QStringLiteral("default")).toString();
  const QJsonArray profiles =
      routing.value(QStringLiteral("profiles")).toArray();
  auto save_profile_routes = [act, profile_query](
                                 const QJsonArray& rows,
                                 const QString& default_route) {
    QJsonObject payload{{QStringLiteral("profiles"), rows}};
    if (!default_route.isEmpty()) {
      payload.insert(QStringLiteral("default"), default_route);
    }
    act(QStringLiteral("delegation"), QStringLiteral("PUT"),
        QStringLiteral("/api/delegation/backends/profile_routes") +
            profile_query,
        payload);
  };
  layout->addWidget(
      Label(page, QStringLiteral("Profile routes"), "settingTitle"));
  for (const QJsonValue& value : route_rows) {
    const QJsonObject entry = value.toObject();
    const QString name = entry.value(QStringLiteral("name")).toString();
    Row row = AddRow(page, layout, name,
                     entry.value(QStringLiteral("description")).toString());
    row.widget->setObjectName(QStringLiteral("delegationRoute_") + name);
    auto* edit = Button(row.widget, QStringLiteral("Edit"));
    edit->setObjectName(QStringLiteral("delegationRouteEdit_") + name);
    edit->setEnabled(static_cast<bool>(act));
    QObject::connect(edit, &QPushButton::clicked, edit, [=]() {
      auto* dialog = new QDialog(page);
      dialog->setObjectName(QStringLiteral("delegationProfileDialog"));
      dialog->setAttribute(Qt::WA_DeleteOnClose);
      dialog->setWindowTitle(QStringLiteral("Edit profile route"));
      auto* dialog_layout = new QVBoxLayout(dialog);
      auto* description = new QLineEdit(
          entry.value(QStringLiteral("description")).toString(), dialog);
      description->setObjectName(
          QStringLiteral("delegationProfileDescription"));
      dialog_layout->addWidget(description);
      auto* actions = new QHBoxLayout();
      actions->addStretch(1);
      auto* cancel = Button(dialog, QStringLiteral("Cancel"));
      auto* save = Button(dialog, QStringLiteral("Save"));
      save->setObjectName(QStringLiteral("delegationProfileSave"));
      actions->addWidget(cancel);
      actions->addWidget(save);
      dialog_layout->addLayout(actions);
      QObject::connect(cancel, &QPushButton::clicked, dialog, &QDialog::reject);
      QObject::connect(save, &QPushButton::clicked, dialog, [=]() {
        QJsonArray updated;
        for (const QJsonValue& other : route_rows) {
          QJsonObject object = other.toObject();
          if (object.value(QStringLiteral("name")).toString() == name) {
            const QString text = description->text().trimmed();
            if (text.isEmpty()) {
              object.remove(QStringLiteral("description"));
            } else {
              object.insert(QStringLiteral("description"), text);
            }
          }
          updated.append(object);
        }
        save_profile_routes(updated, route_default);
        dialog->accept();
      });
      dialog->open();
    });
    row.layout->addWidget(edit);
    auto* remove = Button(row.widget, QStringLiteral("Remove"));
    remove->setObjectName(QStringLiteral("delegationRouteRemove_") + name);
    remove->setEnabled(static_cast<bool>(act));
    QObject::connect(remove, &QPushButton::clicked, remove, [=]() {
      QJsonArray kept;
      for (const QJsonValue& other : route_rows) {
        const QJsonObject object = other.toObject();
        if (object.value(QStringLiteral("name")).toString() != name) {
          kept.append(object);
        }
      }
      save_profile_routes(kept,
                          route_default == name ? QString() : route_default);
    });
    row.layout->addWidget(remove);
  }

  auto* add_row = new QWidget(page);
  auto* add_layout = new QHBoxLayout(add_row);
  add_layout->setContentsMargins(0, 0, 0, 0);
  auto* add_profile = new QComboBox(add_row);
  add_profile->setObjectName(QStringLiteral("delegationRouteProfile"));
  for (const QJsonValue& value : profiles) {
    add_profile->addItem(value.toString(), value.toString());
  }
  add_layout->addWidget(add_profile, 1);
  auto* add = Button(add_row, QStringLiteral("Add route"));
  add->setObjectName(QStringLiteral("delegationRouteAdd"));
  add->setEnabled(static_cast<bool>(act) && add_profile->count() > 0);
  QObject::connect(
      add, &QPushButton::clicked, add,
      [save_profile_routes, add_profile, route_rows, route_default]() {
        QJsonArray updated = route_rows;
        const QString name = add_profile->currentData().toString();
        bool present = false;
        for (const QJsonValue& value : route_rows) {
          if (value.toObject().value(QStringLiteral("name")).toString() ==
              name) {
            present = true;
            break;
          }
        }
        if (!present && !name.isEmpty()) {
          updated.append(QJsonObject{{QStringLiteral("name"), name}});
        }
        save_profile_routes(updated, route_default);
      });
  add_layout->addWidget(add);
  layout->addWidget(add_row);

  auto* profile_default_row = new QWidget(page);
  auto* profile_default_layout = new QHBoxLayout(profile_default_row);
  profile_default_layout->setContentsMargins(0, 0, 0, 0);
  profile_default_layout->addWidget(
      Label(profile_default_row, QStringLiteral("Default"), "settingBody"));
  auto* profile_default_combo = new QComboBox(profile_default_row);
  profile_default_combo->setObjectName(
      QStringLiteral("delegationRouteDefault"));
  profile_default_combo->addItem(QStringLiteral("None"), QString());
  for (const QJsonValue& value : route_rows) {
    const QString name =
        value.toObject().value(QStringLiteral("name")).toString();
    if (!name.isEmpty()) {
      profile_default_combo->addItem(name, name);
    }
  }
  const int route_default_index =
      profile_default_combo->findData(route_default);
  if (route_default_index >= 0) {
    profile_default_combo->setCurrentIndex(route_default_index);
  }
  profile_default_layout->addWidget(profile_default_combo, 1);
  auto* apply_profile_default =
      Button(profile_default_row, QStringLiteral("Apply default"));
  apply_profile_default->setObjectName(
      QStringLiteral("delegationRouteDefaultApply"));
  apply_profile_default->setEnabled(static_cast<bool>(act));
  QObject::connect(apply_profile_default, &QPushButton::clicked,
                   apply_profile_default, [=]() {
                     save_profile_routes(
                         route_rows,
                         profile_default_combo->currentData().toString());
                   });
  profile_default_layout->addWidget(apply_profile_default);
  layout->addWidget(profile_default_row);
  return page;
}

QWidget* StarmapPage(const PageContext& context, QWidget* parent) {
  auto* page = new QWidget(parent);
  page->setObjectName(QStringLiteral("settingsPage:starmap"));
  auto* layout = PageLayout(page);
  AddHeading(page, layout, context, QStringLiteral("starmap"),
             QStringLiteral("Star map"));

  const PageState* state = PageStateFor(context, QStringLiteral("starmap"));
  const QJsonObject graph =
      state == nullptr ? QJsonObject()
                       : state->data.value(QStringLiteral("graph")).toObject();
  const QJsonArray nodes = graph.value(QStringLiteral("nodes")).toArray();
  const QJsonArray edges = graph.value(QStringLiteral("edges")).toArray();
  const QJsonArray clusters = graph.value(QStringLiteral("clusters")).toArray();
  const QJsonArray memories = graph.value(QStringLiteral("memory")).toArray();
  const QJsonObject stats = graph.value(QStringLiteral("stats")).toObject();

  layout->addWidget(
      Label(page,
            QStringLiteral("%1 node%2 · %3 memory card%4 · %5 link%6")
                .arg(nodes.size())
                .arg(nodes.size() == 1 ? QString() : QStringLiteral("s"))
                .arg(memories.size())
                .arg(memories.size() == 1 ? QString() : QStringLiteral("s"))
                .arg(edges.size())
                .arg(edges.size() == 1 ? QString() : QStringLiteral("s")),
            "settingTitle"));
  layout->addWidget(Description(
      page,
      QStringLiteral("Learned skills and memory chunks from the profile's "
                     "learning graph. Skills are archived on delete; "
                     "memory nodes are removed.")));

  if (state != nullptr && !state->error.isEmpty()) {
    auto* error = new QLabel(state->error, page);
    error->setObjectName(QStringLiteral("starmapError"));
    error->setWordWrap(true);
    error->setStyleSheet(
        QStringLiteral("color: %1;").arg(context.theme.danger.name()));
    layout->addWidget(error);
  }

  QStringList stat_bits;
  if (stats.value(QStringLiteral("learned_skills")).toInt() > 0) {
    stat_bits.append(
        QStringLiteral("%1 learned skills")
            .arg(stats.value(QStringLiteral("learned_skills")).toInt()));
  }
  if (stats.value(QStringLiteral("memory_skill_edges")).toInt() > 0) {
    stat_bits.append(
        QStringLiteral("%1 memory links")
            .arg(stats.value(QStringLiteral("memory_skill_edges")).toInt()));
  }
  if (!stat_bits.isEmpty()) {
    auto* stat_label = new QLabel(stat_bits.join(QStringLiteral(" · ")), page);
    stat_label->setObjectName(QStringLiteral("starmapStats"));
    stat_label->setWordWrap(true);
    layout->addWidget(stat_label);
  }

  if (!clusters.isEmpty()) {
    QStringList bits;
    for (const QJsonValue& value : clusters) {
      const QJsonObject cluster = value.toObject();
      bits.append(QStringLiteral("%1 (%2)").arg(
          cluster.value(QStringLiteral("category")).toString(),
          QString::number(cluster.value(QStringLiteral("count")).toInt())));
    }
    auto* cluster_label = new QLabel(
        QStringLiteral("Clusters: %1").arg(bits.join(QStringLiteral(", "))),
        page);
    cluster_label->setObjectName(QStringLiteral("starmapClusters"));
    cluster_label->setWordWrap(true);
    layout->addWidget(cluster_label);
  }

  if (nodes.isEmpty() && memories.isEmpty()) {
    auto* empty = new QLabel(
        QStringLiteral("The star map is empty. Learned skills and saved "
                       "memories appear here as the profile grows."),
        page);
    empty->setObjectName(QStringLiteral("starmapEmpty"));
    empty->setWordWrap(true);
    layout->addWidget(empty);
  }

  const QString profile =
      context.snapshot == nullptr ? QString() : context.snapshot->name;
  const QString profile_query =
      profile.isEmpty()
          ? QString()
          : QStringLiteral("?profile=%1")
                .arg(QString::fromLatin1(QUrl::toPercentEncoding(profile)));
  const auto action = context.action;
  const auto get_detail = context.get_learning_node;
  const Theme detail_theme = context.theme;
  auto show_detail = [page, detail_theme, get_detail](const QString& id) {
    if (id.isEmpty() || !get_detail) {
      return;
    }
    QPointer<QWidget> page_guard(page);
    get_detail(id, [page_guard, detail_theme](const QJsonObject& detail,
                                              const QString& error) {
      if (!page_guard) {
        return;
      }
      QWidget* page = page_guard.data();
      if (!error.isEmpty()) {
        auto* status =
            page->findChild<QLabel*>(QStringLiteral("starmapStatus"));
        if (status == nullptr) {
          status = new QLabel(page);
          status->setObjectName(QStringLiteral("starmapStatus"));
          status->setWordWrap(true);
          if (auto* box = page->findChild<QVBoxLayout*>()) {
            box->addWidget(status);
          }
        }
        status->setStyleSheet(
            QStringLiteral("color: %1;").arg(detail_theme.danger.name()));
        status->setText(error);
        return;
      }
      auto* dialog = new QDialog(page);
      dialog->setObjectName(QStringLiteral("starmapDetailDialog"));
      dialog->setAttribute(Qt::WA_DeleteOnClose, true);
      dialog->setWindowTitle(
          detail.value(QStringLiteral("label")).toString().isEmpty()
              ? QStringLiteral("Star map node")
              : detail.value(QStringLiteral("label")).toString());
      auto* dialog_layout = new QVBoxLayout(dialog);
      auto* label =
          new QLabel(QStringLiteral("%1 · %2").arg(
                         detail.value(QStringLiteral("kind")).toString(),
                         detail.value(QStringLiteral("label")).toString()),
                     dialog);
      label->setObjectName(QStringLiteral("starmapDetailLabel"));
      dialog_layout->addWidget(label);
      auto* content = new QPlainTextEdit(dialog);
      content->setObjectName(QStringLiteral("starmapDetailContent"));
      content->setReadOnly(true);
      content->setPlainText(detail.value(QStringLiteral("content")).toString());
      content->setMinimumSize(620, 380);
      dialog_layout->addWidget(content, 1);
      auto* close = new QPushButton(QStringLiteral("Close"), dialog);
      close->setObjectName(QStringLiteral("starmapDetailClose"));
      QObject::connect(close, &QPushButton::clicked, dialog, &QDialog::close);
      dialog_layout->addWidget(close, 0, Qt::AlignRight);
      dialog->show();
    });
  };

  for (int index = 0; index < nodes.size(); ++index) {
    const QJsonObject node = nodes.at(index).toObject();
    const QString id = node.value(QStringLiteral("id")).toString();
    auto* row = new QWidget(page);
    row->setObjectName(QStringLiteral("starmapRow_%1").arg(index));
    row->setMaximumWidth(820);
    auto* row_layout = new QHBoxLayout(row);
    row_layout->setContentsMargins(12, 8, 12, 8);
    row_layout->setSpacing(8);
    auto* text = new QWidget(row);
    auto* text_layout = new QVBoxLayout(text);
    text_layout->setContentsMargins(0, 0, 0, 0);
    text_layout->setSpacing(2);
    const QString kind = node.value(QStringLiteral("kind")).toString();
    auto* title = new QLabel(
        QStringLiteral("%1 · %2").arg(
            kind == QStringLiteral("skill") ? QStringLiteral("Skill")
                                            : QStringLiteral("Memory"),
            node.value(QStringLiteral("label")).toString()),
        text);
    title->setObjectName(QStringLiteral("starmapTitle_%1").arg(index));
    title->setWordWrap(true);
    title->setStyleSheet(QStringLiteral("color: %1; font-weight: 600;")
                             .arg(context.theme.foreground.name()));
    text_layout->addWidget(title);
    QStringList meta;
    const QString category = node.value(QStringLiteral("category")).toString();
    if (!category.isEmpty()) {
      meta.append(category);
    }
    const QString state_text = node.value(QStringLiteral("state")).toString();
    if (!state_text.isEmpty()) {
      meta.append(state_text);
    }
    if (node.value(QStringLiteral("useCount")).toInt() > 0) {
      meta.append(QStringLiteral("%1 uses").arg(
          node.value(QStringLiteral("useCount")).toInt()));
    }
    if (node.value(QStringLiteral("pinned")).toBool()) {
      meta.append(QStringLiteral("pinned"));
    }
    const QString memory_source =
        node.value(QStringLiteral("memorySource")).toString();
    if (!memory_source.isEmpty()) {
      meta.append(memory_source);
    }
    auto* detail_label = new QLabel(meta.join(QStringLiteral(" · ")), text);
    detail_label->setObjectName(QStringLiteral("starmapMeta_%1").arg(index));
    detail_label->setWordWrap(true);
    detail_label->setStyleSheet(
        QStringLiteral("color: %1;").arg(context.theme.text_secondary.name()));
    text_layout->addWidget(detail_label);
    row_layout->addWidget(text, 1);
    auto* view = new QPushButton(QStringLiteral("View"), row);
    view->setObjectName(QStringLiteral("starmapView_%1").arg(index));
    view->setEnabled(static_cast<bool>(get_detail) && !id.isEmpty());
    QObject::connect(view, &QPushButton::clicked, view,
                     [show_detail, id]() { show_detail(id); });
    row_layout->addWidget(view);
    auto* remove = new QPushButton(QStringLiteral("Delete"), row);
    remove->setObjectName(QStringLiteral("starmapDelete_%1").arg(index));
    remove->setEnabled(static_cast<bool>(action) && !id.isEmpty());
    QObject::connect(
        remove, &QPushButton::clicked, remove, [action, id, profile_query]() {
          if (action) {
            action(QStringLiteral("starmap"), QStringLiteral("DELETE"),
                   QStringLiteral("/api/learning/node") + profile_query,
                   QJsonObject{{QStringLiteral("id"), id}});
          }
        });
    row_layout->addWidget(remove);
    layout->addWidget(row);
  }

  if (!edges.isEmpty()) {
    layout->addWidget(Label(page, QStringLiteral("Links"), "settingTitle"));
    for (int index = 0; index < std::min(40, static_cast<int>(edges.size()));
         ++index) {
      const QJsonObject edge = edges.at(index).toObject();
      auto* label =
          new QLabel(QStringLiteral("%1 → %2").arg(
                         edge.value(QStringLiteral("source")).toString(),
                         edge.value(QStringLiteral("target")).toString()),
                     page);
      label->setObjectName(QStringLiteral("starmapEdge_%1").arg(index));
      label->setWordWrap(true);
      layout->addWidget(label);
    }
  }

  if (!memories.isEmpty()) {
    layout->addWidget(
        Label(page, QStringLiteral("Memory cards"), "settingTitle"));
    for (int index = 0; index < memories.size(); ++index) {
      const QJsonObject card = memories.at(index).toObject();
      auto* label =
          new QLabel(QStringLiteral("%1\n%2").arg(
                         card.value(QStringLiteral("title")).toString(),
                         card.value(QStringLiteral("body")).toString()),
                     page);
      label->setObjectName(QStringLiteral("starmapMemory_%1").arg(index));
      label->setWordWrap(true);
      label->setMaximumWidth(820);
      label->setStyleSheet(
          QStringLiteral("border: 1px solid %1; border-radius: 6px; "
                         "padding: 8px;")
              .arg(context.theme.stroke.name()));
      layout->addWidget(label);
    }
  }

  return page;
}

QString SubagentStatusStyle(SubagentStatus status, const Theme& theme) {
  QString color = theme.text_secondary.name();
  switch (status) {
    case SubagentStatus::kRunning:
      color = QStringLiteral("#4f9cf9");
      break;
    case SubagentStatus::kCompleted:
      color = QStringLiteral("#3fa66a");
      break;
    case SubagentStatus::kFailed:
      color = theme.danger.name();
      break;
    case SubagentStatus::kInterrupted:
      color = QStringLiteral("#c58b2f");
      break;
    case SubagentStatus::kQueued:
      break;
  }
  return color;
}

QWidget* AgentsPage(const PageContext& context, QWidget* parent) {
  auto* page = new QWidget(parent);
  page->setObjectName(QStringLiteral("settingsPage:agents"));
  auto* layout = PageLayout(page);
  AddHeading(page, layout, context, QStringLiteral("agents"),
             QStringLiteral("Agents"));

  const std::vector<SubagentProgress>& rows =
      context.snapshot == nullptr ? std::vector<SubagentProgress>()
                                  : context.snapshot->subagents;
  const std::vector<const SubagentProgress*> ordered = BuildSubagentTree(rows);
  const int live = static_cast<int>(
      std::count_if(rows.begin(), rows.end(), [](const SubagentProgress& row) {
        return !SubagentIsTerminal(row.status);
      }));
  layout->addWidget(
      Label(page,
            QStringLiteral("%1 agent%2 · %3 live")
                .arg(rows.size())
                .arg(rows.size() == 1 ? QString() : QStringLiteral("s"))
                .arg(live),
            "settingTitle"));
  layout->addWidget(Description(
      page, QStringLiteral("Delegated children reported by the gateway's "
                           "subagent stream. A finished agent keeps its "
                           "summary until the profile reconnects.")));

  if (ordered.empty()) {
    auto* empty = new QLabel(
        QStringLiteral("No subagents yet. Delegated work appears here while it "
                       "runs."),
        page);
    empty->setObjectName(QStringLiteral("agentEmpty"));
    empty->setWordWrap(true);
    layout->addWidget(empty);
    return page;
  }

  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  QString current_session;
  for (const SubagentProgress* row : ordered) {
    if (row->session_id != current_session) {
      current_session = row->session_id;
      QString title = current_session;
      for (const SessionSummary& session : context.snapshot == nullptr
                                               ? std::vector<SessionSummary>()
                                               : context.snapshot->sessions) {
        if (session.id == current_session) {
          title = session.title.isEmpty() ? session.id : session.title;
          break;
        }
      }
      if (title.isEmpty()) {
        title = QStringLiteral("Current session");
      }
      layout->addWidget(Label(page, title, "settingTitle"));
    }

    const int depth = SubagentDepth(row->id, rows);
    auto* card = new QWidget(page);
    card->setObjectName(QStringLiteral("agentRow_") + row->id);
    card->setMaximumWidth(820);
    auto* card_layout = new QVBoxLayout(card);
    card_layout->setContentsMargins(12 + depth * 18, 10, 12, 10);
    card_layout->setSpacing(4);
    card_layout->setAlignment(Qt::AlignTop);

    auto* head = new QWidget(card);
    auto* head_layout = new QHBoxLayout(head);
    head_layout->setContentsMargins(0, 0, 0, 0);
    head_layout->setSpacing(8);
    auto* goal = new QLabel(row->goal, head);
    goal->setObjectName(QStringLiteral("agentTitle_") + row->id);
    goal->setWordWrap(true);
    goal->setStyleSheet(QStringLiteral("color: %1; font-weight: 600;")
                            .arg(context.theme.foreground.name()));
    head_layout->addWidget(goal, 1);
    auto* status = new QLabel(SubagentStatusText(row->status), head);
    status->setObjectName(QStringLiteral("agentStatus_") + row->id);
    status->setStyleSheet(
        QStringLiteral("color: %1; border: 1px solid %1; border-radius: 8px; "
                       "padding: 1px 7px; font-size: 11px;")
            .arg(SubagentStatusStyle(row->status, context.theme)));
    head_layout->addWidget(status);
    card_layout->addWidget(head);

    const auto steer_child = context.steer_subagent;
    const auto interrupt_child = context.interrupt_subagent;
    if (!SubagentIsTerminal(row->status) && (steer_child || interrupt_child)) {
      auto* controls = new QWidget(card);
      auto* control_layout = new QVBoxLayout(controls);
      control_layout->setContentsMargins(0, 2, 0, 0);
      control_layout->setSpacing(2);
      auto* control_row = new QWidget(controls);
      auto* control_row_layout = new QHBoxLayout(control_row);
      control_row_layout->setContentsMargins(0, 0, 0, 0);
      control_row_layout->setSpacing(6);
      auto* steer_input = new QLineEdit(control_row);
      steer_input->setObjectName(QStringLiteral("agentSteerInput_") + row->id);
      steer_input->setPlaceholderText(QStringLiteral("Steer this agent…"));
      control_row_layout->addWidget(steer_input, 1);
      auto* control_status = new QLabel(controls);
      control_status->setObjectName(QStringLiteral("agentControlStatus_") +
                                    row->id);
      control_status->setWordWrap(true);
      control_status->setStyleSheet(
          QStringLiteral("color: %1; font-size: 11px;")
              .arg(context.theme.text_secondary.name()));
      if (steer_child) {
        auto* steer = Button(control_row, QStringLiteral("Steer"));
        steer->setObjectName(QStringLiteral("agentSteer_") + row->id);
        steer->setEnabled(false);
        QObject::connect(steer_input, &QLineEdit::textChanged, steer,
                         [steer](const QString& text) {
                           steer->setEnabled(!text.trimmed().isEmpty());
                         });
        const QString session_id = row->owner_session_id.isEmpty()
                                       ? row->session_id
                                       : row->owner_session_id;
        const QString subagent_id = row->id;
        const auto send_steer = [steer, steer_input, control_status, session_id,
                                 subagent_id, steer_child]() {
          if (!steer->isEnabled()) {
            return;
          }
          const QString text = steer_input->text().trimmed();
          control_status->setText(QStringLiteral("Steering…"));
          const QPointer<QLineEdit> input_guard(steer_input);
          const QPointer<QLabel> status_guard(control_status);
          steer_child(session_id, subagent_id, text,
                      [input_guard, status_guard](const QJsonObject& result,
                                                  const QString& error) {
                        if (!status_guard) {
                          return;
                        }
                        if (!error.isEmpty()) {
                          status_guard->setText(error);
                          return;
                        }
                        if (result.value(QStringLiteral("status")).toString() !=
                            QStringLiteral("queued")) {
                          status_guard->setText(
                              QStringLiteral("Steering was rejected."));
                          return;
                        }
                        if (input_guard) {
                          input_guard->clear();
                        }
                        status_guard->setText(
                            QStringLiteral("Steering queued for this agent."));
                      });
        };
        QObject::connect(steer_input, &QLineEdit::returnPressed, steer,
                         [send_steer]() { send_steer(); });
        QObject::connect(steer, &QPushButton::clicked, steer,
                         [send_steer]() { send_steer(); });
        control_row_layout->addWidget(steer);
      }
      if (interrupt_child) {
        auto* stop = Button(control_row, QStringLiteral("Stop"));
        stop->setObjectName(QStringLiteral("agentStop_") + row->id);
        const QString session_id = row->owner_session_id.isEmpty()
                                       ? row->session_id
                                       : row->owner_session_id;
        const QString subagent_id = row->id;
        QObject::connect(
            stop, &QPushButton::clicked, stop,
            [control_status, session_id, subagent_id, interrupt_child]() {
              control_status->setText(QStringLiteral("Stopping…"));
              const QPointer<QLabel> status_guard(control_status);
              interrupt_child(
                  session_id, subagent_id,
                  [status_guard](const QJsonObject& result,
                                 const QString& error) {
                    if (!status_guard) {
                      return;
                    }
                    if (!error.isEmpty()) {
                      status_guard->setText(error);
                      return;
                    }
                    status_guard->setText(
                        result.value(QStringLiteral("found")).toBool()
                            ? QStringLiteral("Stop requested.")
                            : QStringLiteral("Stop request was rejected."));
                  });
            });
        control_row_layout->addWidget(stop);
      }
      control_layout->addWidget(control_row);
      control_layout->addWidget(control_status);
      card_layout->addWidget(controls);
    }

    QStringList meta;
    if (row->task_count > 1) {
      meta.append(QStringLiteral("Task %1/%2")
                      .arg(row->task_index + 1)
                      .arg(row->task_count));
    }
    if (!row->model.isEmpty()) {
      meta.append(row->model);
    }
    if (!row->current_tool.isEmpty()) {
      meta.append(row->current_tool);
    }
    const QString duration = FormatSubagentDuration(row->duration_seconds);
    if (!duration.isEmpty()) {
      meta.append(duration);
    }
    if (row->tool_count > 0) {
      meta.append(
          QStringLiteral("%1 tool%2")
              .arg(row->tool_count)
              .arg(row->tool_count == 1 ? QString() : QStringLiteral("s")));
    }
    const qint64 tokens = row->input_tokens + row->output_tokens;
    const QString token_text = FormatSubagentTokens(tokens);
    if (!token_text.isEmpty()) {
      meta.append(token_text);
    }
    if (row->cost_usd > 0.0) {
      meta.append(
          QStringLiteral("$%1").arg(QString::number(row->cost_usd, 'f', 2)));
    }
    meta.append(FormatSubagentAge(row->updated_at_ms, now));
    auto* meta_label = new QLabel(meta.join(QStringLiteral(" · ")), card);
    meta_label->setObjectName(QStringLiteral("agentMeta_") + row->id);
    meta_label->setWordWrap(true);
    meta_label->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;")
                                  .arg(context.theme.text_secondary.name()));
    card_layout->addWidget(meta_label);

    const int shown = static_cast<int>(row->stream.size());
    for (int i = std::max(0, shown - 6); i < shown; ++i) {
      const SubagentStreamEntry& entry = row->stream[i];
      QString prefix;
      if (entry.kind == QStringLiteral("tool")) {
        prefix = QStringLiteral("▸ ");
      } else if (entry.kind == QStringLiteral("thinking")) {
        prefix = QStringLiteral("… ");
      } else if (entry.kind == QStringLiteral("summary")) {
        prefix = QStringLiteral("✔ ");
      }
      auto* line = new QLabel(prefix + entry.text, card);
      line->setObjectName(
          QStringLiteral("agentStream_%1_%2").arg(row->id).arg(i));
      line->setWordWrap(true);
      line->setTextFormat(Qt::PlainText);
      line->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;")
                              .arg(entry.error
                                       ? context.theme.danger.name()
                                       : context.theme.text_secondary.name()));
      card_layout->addWidget(line);
    }
    if (!row->summary.isEmpty() &&
        (row->stream.empty() ||
         row->stream.back().kind != QStringLiteral("summary"))) {
      auto* summary = new QLabel(row->summary, card);
      summary->setObjectName(QStringLiteral("agentSummary_") + row->id);
      summary->setWordWrap(true);
      summary->setTextFormat(Qt::PlainText);
      summary->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;")
                                 .arg(context.theme.foreground.name()));
      card_layout->addWidget(summary);
    }

    if (!row->files_written.isEmpty() || !row->files_read.isEmpty()) {
      QStringList files;
      for (const QString& path : row->files_written) {
        files.append(QStringLiteral("wrote %1").arg(path));
      }
      for (const QString& path : row->files_read) {
        files.append(QStringLiteral("read %1").arg(path));
      }
      auto* file_label = new QLabel(files.join(QStringLiteral(" · ")), card);
      file_label->setObjectName(QStringLiteral("agentFiles_") + row->id);
      file_label->setWordWrap(true);
      file_label->setStyleSheet(QStringLiteral("color: %1; font-size: 10px;")
                                    .arg(context.theme.text_secondary.name()));
      card_layout->addWidget(file_label);
    }
    layout->addWidget(card);
  }
  return page;
}

QWidget* CapabilitiesPage(const PageContext& context, QWidget* parent) {
  auto* page = new QWidget(parent);
  page->setObjectName(QStringLiteral("settingsPage:capabilities"));
  auto* layout = PageLayout(page);
  AddHeading(page, layout, context, QStringLiteral("capabilities"),
             QStringLiteral("Capabilities"));

  const PageState* state =
      PageStateFor(context, QStringLiteral("capabilities"));
  const QJsonObject data = state == nullptr ? QJsonObject() : state->data;
  const auto act = context.action;
  // Config writes (toolsets, skills, MCP) land in this profile. The page read
  // is already profile-scoped by the worker; repeat it on mutations so a
  // stale active profile cannot redirect the write.
  const QString profile =
      context.snapshot == nullptr ? QString() : context.snapshot->name;
  const QString profile_query =
      profile.isEmpty()
          ? QString()
          : QStringLiteral("?profile=%1")
                .arg(QString::fromLatin1(QUrl::toPercentEncoding(profile)));

  const QJsonArray toolsets =
      PayloadArray(data, "toolsets", {"toolsets", "rows"});
  layout->addWidget(Label(page,
                          QStringLiteral("Toolsets (%1)").arg(toolsets.size()),
                          "settingTitle"));
  for (const QJsonValue& value : toolsets) {
    const QJsonObject toolset = value.toObject();
    const QString name = toolset.value(QStringLiteral("name")).toString();
    const QString label = toolset.value(QStringLiteral("label")).toString();
    const bool enabled = toolset.value(QStringLiteral("enabled")).toBool();
    const QJsonArray tool_names =
        toolset.value(QStringLiteral("tools")).toArray();
    QStringList detail;
    if (tool_names.size() > 0) {
      detail.append(
          QStringLiteral("%1 tool%2")
              .arg(tool_names.size())
              .arg(tool_names.size() == 1 ? QString() : QStringLiteral("s")));
    }
    if (!toolset.value(QStringLiteral("available")).toBool() &&
        !toolset.value(QStringLiteral("configured")).toBool()) {
      detail.append(QStringLiteral("not configured"));
    }
    Row row = AddRow(page, layout, label.isEmpty() ? name : label,
                     detail.join(QStringLiteral(" · ")));
    row.widget->setObjectName(QStringLiteral("toolsetRow_") + name);
    // Keep the toggle near the content column instead of pinned to the window
    // edge (the page scroll area is much wider than the form).
    row.widget->setMaximumWidth(760);
    if (!tool_names.isEmpty()) {
      QStringList names;
      for (const QJsonValue& tool : tool_names) {
        names.append(tool.toString());
      }
      auto* tools_button = Button(
          row.widget, QStringLiteral("Tools (%1)").arg(tool_names.size()));
      tools_button->setObjectName(QStringLiteral("toolsetToolsButton_") + name);
      tools_button->setCheckable(true);
      row.layout->addWidget(tools_button);
      auto* tools_list = new QLabel(names.join(QStringLiteral(", ")), page);
      tools_list->setObjectName(QStringLiteral("toolsetTools_") + name);
      tools_list->setWordWrap(true);
      tools_list->setTextInteractionFlags(Qt::TextSelectableByMouse);
      tools_list->setStyleSheet(QStringLiteral("color: %1; padding-left: 12px;")
                                    .arg(context.theme.text_secondary.name()));
      tools_list->hide();
      layout->addWidget(tools_list);
      QObject::connect(
          tools_button, &QPushButton::toggled, tools_button,
          [tools_button, tools_list, count = tool_names.size()](bool expanded) {
            tools_list->setVisible(expanded);
            tools_button->setText(
                expanded ? QStringLiteral("Hide tools")
                         : QStringLiteral("Tools (%1)").arg(count));
          });
    }
    // PUT /api/tools/toolsets/{name} is the route the CLI's own picker uses:
    // a capability really can be switched off.
    BoolCheck(row.widget, QStringLiteral("toolsetToggle_") + name, enabled,
              static_cast<bool>(act)
                  ? std::function<void(bool)>([=](bool checked_on) {
                      act(QStringLiteral("capabilities"), QStringLiteral("PUT"),
                          QStringLiteral("/api/tools/toolsets/%1").arg(name) +
                              profile_query,
                          QJsonObject{{QStringLiteral("enabled"), checked_on}});
                    })
                  : std::function<void(bool)>(),
              row.layout);
    const auto get_config = context.get_toolset_config;
    const auto save_config = context.save_toolset_config;
    const auto get_models = context.get_toolset_models;
    const auto save_model = context.save_toolset_model;
    const auto refresh_page = context.refresh;
    if (get_config && save_config) {
      auto* configure = Button(row.widget, QStringLiteral("Configure"));
      configure->setObjectName(QStringLiteral("toolsetConfig_") + name);
      row.layout->addWidget(configure);
      QObject::connect(configure, &QPushButton::clicked, configure, [=]() {
        QPointer<QWidget> page_guard(page);
        get_config(name, [=](const QJsonObject& config, const QString& error) {
          if (page_guard == nullptr) {
            return;
          }
          QWidget* page = page_guard.data();
          if (!error.isEmpty()) {
            QMessageBox::warning(page, QStringLiteral("Toolset"),
                                 error.isEmpty()
                                     ? QStringLiteral("Could not load this "
                                                      "toolset's settings.")
                                     : error);
            return;
          }
          auto* dialog = new QDialog(page);
          dialog->setObjectName(QStringLiteral("toolsetConfigDialog_") + name);
          dialog->setAttribute(Qt::WA_DeleteOnClose);
          dialog->setWindowTitle(QStringLiteral("Configure ") + name);
          auto* dialog_layout = new QVBoxLayout(dialog);
          const QJsonArray providers =
              config.value(QStringLiteral("providers")).toArray();
          const QString active_provider =
              config.value(QStringLiteral("active_provider")).toString();
          auto* provider_label = new QLabel(QStringLiteral("Provider"), dialog);
          dialog_layout->addWidget(provider_label);
          auto* provider_pick = new QComboBox(dialog);
          provider_pick->setObjectName(QStringLiteral("toolsetProvider"));
          for (const QJsonValue& value : providers) {
            const QJsonObject provider = value.toObject();
            const QString provider_name =
                provider.value(QStringLiteral("name")).toString();
            QString label_text =
                provider.value(QStringLiteral("badge")).toString();
            if (label_text.isEmpty()) {
              label_text = provider_name;
            }
            const QString status =
                provider.value(QStringLiteral("status")).toString();
            if (!status.isEmpty()) {
              label_text += QStringLiteral(" · ") + status;
            }
            provider_pick->addItem(label_text, provider_name);
            if (provider_name == active_provider) {
              provider_pick->setCurrentIndex(provider_pick->count() - 1);
            }
          }
          provider_pick->setEnabled(provider_pick->count() > 1);
          dialog_layout->addWidget(provider_pick);
          auto* model_label = new QLabel(QStringLiteral("Model"), dialog);
          dialog_layout->addWidget(model_label);
          auto* model_pick = new QComboBox(dialog);
          model_pick->setObjectName(QStringLiteral("toolsetModel"));
          dialog_layout->addWidget(model_pick);
          model_label->hide();
          model_pick->hide();
          auto model_token = std::make_shared<int>(0);
          auto model_label_ptr = QPointer<QLabel>(model_label);
          auto model_pick_ptr = QPointer<QComboBox>(model_pick);
          auto* env_host = new QWidget(dialog);
          auto* env_form = new QFormLayout(env_host);
          env_form->setContentsMargins(0, 0, 0, 0);
          dialog_layout->addWidget(env_host);
          auto* status = new QLabel(dialog);
          status->setObjectName(QStringLiteral("toolsetConfigStatus"));
          status->setWordWrap(true);
          dialog_layout->addWidget(status);
          auto* submit = Button(dialog, QStringLiteral("Save"));
          submit->setObjectName(QStringLiteral("toolsetConfigSubmit"));
          dialog_layout->addWidget(submit);

          struct EnvField {
            QString key;
            QLineEdit* edit = nullptr;
          };
          auto fields = std::make_shared<std::vector<EnvField>>();
          const auto rebuild = [=]() {
            while (env_form->rowCount() > 0) {
              env_form->removeRow(0);
            }
            fields->clear();
            const QString provider_name =
                provider_pick->currentData().toString();
            // A provider switch invalidates any in-flight model catalog.
            const int model_generation = ++*model_token;
            if (model_label_ptr) {
              model_label_ptr->hide();
            }
            if (model_pick_ptr) {
              model_pick_ptr->clear();
              model_pick_ptr->hide();
            }
            if (get_models) {
              get_models(
                  name, provider_name,
                  [model_token, model_generation, model_label_ptr,
                   model_pick_ptr, status, name, save_model](
                      const QJsonObject& catalog, const QString& load_error) {
                    if (!model_pick_ptr || *model_token != model_generation) {
                      return;
                    }
                    if (!load_error.isEmpty()) {
                      return;
                    }
                    const QJsonArray models =
                        catalog.value(QStringLiteral("models")).toArray();
                    if (!catalog.value(QStringLiteral("has_models")).toBool() ||
                        models.isEmpty()) {
                      return;
                    }
                    const QString provider =
                        catalog.value(QStringLiteral("provider")).toString();
                    const QString selected =
                        catalog.value(QStringLiteral("current"))
                            .toString(catalog.value(QStringLiteral("default"))
                                          .toString());
                    {
                      const QSignalBlocker block(model_pick_ptr);
                      for (const QJsonValue& value : models) {
                        const QJsonObject model = value.toObject();
                        QString label_text =
                            model.value(QStringLiteral("display")).toString();
                        if (label_text.isEmpty()) {
                          label_text =
                              model.value(QStringLiteral("id")).toString();
                        }
                        const QString price =
                            model.value(QStringLiteral("price")).toString();
                        if (!price.isEmpty()) {
                          label_text += QStringLiteral(" · ") + price;
                        }
                        const QString model_id =
                            model.value(QStringLiteral("id")).toString();
                        model_pick_ptr->addItem(label_text, model_id);
                        if (model_id == selected) {
                          model_pick_ptr->setCurrentIndex(
                              model_pick_ptr->count() - 1);
                        }
                      }
                    }
                    if (model_label_ptr) {
                      model_label_ptr->show();
                    }
                    model_pick_ptr->show();
                    if (save_model) {
                      QObject::connect(
                          model_pick_ptr, &QComboBox::currentIndexChanged,
                          model_pick_ptr,
                          [model_pick_ptr, status, name, provider,
                           save_model](int) {
                            const QString model =
                                model_pick_ptr->currentData().toString();
                            if (model.isEmpty()) {
                              return;
                            }
                            status->setText(QStringLiteral("Saving model…"));
                            save_model(
                                name, model, provider,
                                [status, model](const QJsonObject&,
                                                const QString& save_error) {
                                  if (!status) {
                                    return;
                                  }
                                  status->setText(
                                      save_error.isEmpty()
                                          ? QStringLiteral("Model set to "
                                                           "%1.")
                                                .arg(model)
                                          : save_error);
                                });
                          });
                    }
                  });
            }
            for (const QJsonValue& value : providers) {
              const QJsonObject provider = value.toObject();
              if (provider.value(QStringLiteral("name")).toString() !=
                  provider_name) {
                continue;
              }
              for (const QJsonValue& env_value :
                   provider.value(QStringLiteral("env_vars")).toArray()) {
                const QJsonObject env = env_value.toObject();
                const QString key = env.value(QStringLiteral("key")).toString();
                if (key.isEmpty()) {
                  continue;
                }
                auto* edit = new QLineEdit(env_host);
                edit->setObjectName(QStringLiteral("toolsetEnv_") + key);
                edit->setEchoMode(QLineEdit::Password);
                const bool is_set =
                    env.value(QStringLiteral("is_set")).toBool();
                edit->setPlaceholderText(
                    is_set ? QStringLiteral("already set — blank keeps it")
                           : env.value(QStringLiteral("prompt")).toString(key));
                QString label_text =
                    env.value(QStringLiteral("prompt")).toString();
                if (label_text.isEmpty()) {
                  label_text = key;
                }
                if (is_set) {
                  label_text += QStringLiteral(" · set");
                }
                env_form->addRow(label_text, edit);
                fields->push_back({key, edit});
              }
              break;
            }
            if (fields->empty()) {
              env_form->addRow(new QLabel(
                  QStringLiteral("No credentials needed."), env_host));
            }
          };
          QObject::connect(provider_pick, &QComboBox::currentIndexChanged,
                           dialog, [rebuild](int) { rebuild(); });
          rebuild();
          QObject::connect(submit, &QPushButton::clicked, dialog, [=]() {
            const QString provider = provider_pick->currentData().toString();
            QJsonObject env;
            for (const EnvField& field : *fields) {
              const QString value = field.edit->text().trimmed();
              if (!value.isEmpty()) {
                env.insert(field.key, value);
              }
            }
            status->setText(QStringLiteral("Saving…"));
            QPointer<QLabel> status_guard(status);
            QPointer<QDialog> dialog_guard(dialog);
            save_config(name, provider, env,
                        [status_guard, dialog_guard, refresh_page](
                            const QJsonObject&, const QString& save_error) {
                          if (status_guard == nullptr) {
                            return;
                          }
                          if (!save_error.isEmpty()) {
                            status_guard->setText(save_error);
                            return;
                          }
                          if (refresh_page) {
                            refresh_page(QStringLiteral("capabilities"));
                          }
                          if (dialog_guard != nullptr) {
                            dialog_guard->accept();
                          }
                        });
          });
          dialog->open();
        });
      });
    }
  }

  const auto list_connectors = context.list_connectors;
  const auto connect_connector = context.connect_connector;
  const auto connector_status = context.connector_operation_status;
  const auto connector_rpc = context.connector_rpc;
  const auto connector_open_url = context.open_url;
  if (list_connectors) {
    layout->addWidget(
        Label(page, QStringLiteral("Connectors"), "settingTitle"));
    auto* panel = new QWidget(page);
    panel->setObjectName(QStringLiteral("connectorsPanel"));
    panel->setMaximumWidth(760);
    auto* panel_layout = new QVBoxLayout(panel);
    panel_layout->setContentsMargins(0, 0, 0, 0);
    panel_layout->setSpacing(6);
    auto* panel_status =
        new QLabel(QStringLiteral("Loading connectors…"), panel);
    panel_status->setObjectName(QStringLiteral("connectorsStatus"));
    panel_status->setWordWrap(true);
    panel_layout->addWidget(panel_status);
    if (connector_rpc) {
      auto* catalog = Button(panel, QStringLiteral("Browse catalog"));
      catalog->setObjectName(QStringLiteral("connectorsCatalogButton"));
      panel_layout->addWidget(catalog, 0, Qt::AlignLeft);
      QObject::connect(catalog, &QPushButton::clicked, panel,
                       [=]() { ShowConnectorCatalog(page, context); });
    }
    auto* rows = new QWidget(panel);
    rows->setObjectName(QStringLiteral("connectorRows"));
    auto* rows_layout = new QVBoxLayout(rows);
    rows_layout->setContentsMargins(0, 0, 0, 0);
    rows_layout->setSpacing(4);
    panel_layout->addWidget(rows);
    layout->addWidget(panel);

    auto reload = std::make_shared<std::function<void()>>();
    // The page can be rebuilt while the connector list is in flight; never
    // touch the old panel/layout after Qt has destroyed it.
    QPointer<QWidget> page_guard(page);
    QPointer<QWidget> panel_guard(panel);
    QPointer<QLabel> panel_status_guard(panel_status);
    QPointer<QWidget> rows_guard(rows);
    QPointer<QVBoxLayout> rows_layout_guard(rows_layout);
    *reload = [=]() {
      if (page_guard == nullptr || panel_guard == nullptr ||
          panel_status_guard == nullptr || rows_guard == nullptr ||
          rows_layout_guard == nullptr) {
        return;
      }
      panel_status->setText(QStringLiteral("Loading connectors…"));
      list_connectors([=](const QJsonObject& data, const QString& load_error) {
        if (page_guard == nullptr || panel_guard == nullptr ||
            panel_status_guard == nullptr || rows_guard == nullptr ||
            rows_layout_guard == nullptr) {
          return;
        }
        while (QLayoutItem* item = rows_layout->takeAt(0)) {
          if (QWidget* widget = item->widget()) {
            widget->deleteLater();
          }
          delete item;
        }
        if (!load_error.isEmpty()) {
          panel_status->setText(load_error);
          return;
        }
        if (!data.value(QStringLiteral("available")).toBool(true)) {
          panel_status->setText(
              QStringLiteral("Connectors are not available on this gateway."));
          return;
        }
        panel_status->setText(QStringLiteral("Hosted connectors"));
        const QJsonArray connectors =
            data.value(QStringLiteral("connectors")).toArray();
        if (connectors.isEmpty()) {
          panel_status->setText(QStringLiteral("No connectors configured."));
          return;
        }
        for (const QJsonValue& value : connectors) {
          const QJsonObject entry = value.toObject();
          const QString slug =
              entry.value(QStringLiteral("connector")).toString();
          if (slug.isEmpty()) {
            continue;
          }
          const bool connected =
              entry.value(QStringLiteral("connected")).toBool();
          const QString connection_state =
              entry.value(QStringLiteral("connection_status")).toString();
          const QString reason =
              entry.value(QStringLiteral("status_reason")).toString();
          QStringList detail;
          detail.append(connected ? QStringLiteral("Connected")
                                  : QStringLiteral("Not connected"));
          if (!connection_state.isEmpty()) {
            detail.append(connection_state);
          }
          if (!reason.isEmpty()) {
            detail.append(reason);
          }
          const hermes::ui::ConnectionOperation* operation =
              context.snapshot == nullptr
                  ? nullptr
                  : hermes::ui::AccountOperationFor(
                        context.snapshot->account_operations, slug);
          // Only an open operation is live progress. A settled one is kept so
          // a retry can be addressed, but the connector list has already been
          // refetched and owns the final connected/disconnected state.
          if (operation != nullptr && !operation->settled) {
            QStringList states;
            for (const hermes::ui::ConnectionTarget& target :
                 operation->targets) {
              if (target.name == slug && !target.state.isEmpty()) {
                states.append(target.state);
              }
            }
            if (!states.isEmpty()) {
              detail.append(QStringLiteral("operation %1")
                                .arg(states.join(QStringLiteral("/"))));
            }
          }
          auto* row = new QWidget(rows);
          row->setObjectName(QStringLiteral("connectorRow_") + slug);
          auto* row_layout = new QHBoxLayout(row);
          row_layout->setContentsMargins(0, 0, 0, 0);
          row_layout->setSpacing(8);
          auto* label =
              new QLabel(QStringLiteral("%1 · %2").arg(
                             slug, detail.join(QStringLiteral(" · "))),
                         row);
          label->setWordWrap(true);
          row_layout->addWidget(label, 1);
          auto* row_status = new QLabel(row);
          row_status->setObjectName(QStringLiteral("connectorStatus_") + slug);
          row_status->setWordWrap(true);
          if (operation != nullptr && !operation->settled) {
            QStringList states;
            for (const hermes::ui::ConnectionTarget& target :
                 operation->targets) {
              if (target.name == slug && !target.state.isEmpty()) {
                states.append(target.state);
              }
            }
            if (!states.isEmpty()) {
              row_status->setText(QStringLiteral("Connector operation: %1")
                                      .arg(states.join(QStringLiteral(", "))));
            }
          }
          row_layout->addWidget(row_status);
          QPointer<QLabel> row_status_guard(row_status);
          auto* action = Button(row, connected ? QStringLiteral("Reconnect")
                                               : QStringLiteral("Connect"));
          action->setObjectName(QStringLiteral("connectorConnect_") + slug);
          if (connector_rpc) {
            auto* accounts = Button(row, QStringLiteral("Accounts"));
            accounts->setObjectName(QStringLiteral("connectorAccountsButton_") +
                                    slug);
            row_layout->addWidget(accounts);
            auto* tools = Button(row, QStringLiteral("Tools"));
            tools->setObjectName(QStringLiteral("connectorToolsButton_") +
                                 slug);
            row_layout->addWidget(tools);
            QObject::connect(accounts, &QPushButton::clicked, row, [=]() {
              ShowConnectorAccounts(page, context, slug, *reload);
            });
            QObject::connect(tools, &QPushButton::clicked, row, [=]() {
              ShowConnectorTools(page, context, slug);
            });
          }
          row_layout->addWidget(action);
          rows_layout->addWidget(row);
          QObject::connect(action, &QPushButton::clicked, row, [=]() {
            row_status->setText(QStringLiteral("Starting…"));
            connect_connector(
                slug, connected,
                [=](const QJsonObject& result, const QString& connect_error) {
                  if (row_status_guard == nullptr) {
                    return;
                  }
                  if (!connect_error.isEmpty()) {
                    row_status_guard->setText(connect_error);
                    return;
                  }
                  QString url = FindStringRecursive(
                      result, QStringLiteral("connect_url"));
                  if (url.isEmpty()) {
                    url = FindStringRecursive(
                        result, QStringLiteral("verification_uri"));
                  }
                  if (!url.isEmpty() && connector_open_url) {
                    connector_open_url(url);
                  }
                  const QString summary = ConnectorOperationSummary(result);
                  const QString prompt = ConnectorPromptSummary(result);
                  QString text = summary.isEmpty()
                                     ? QStringLiteral("Authorization started.")
                                     : summary;
                  if (!prompt.isEmpty()) {
                    text += QStringLiteral("\n") + prompt;
                  }
                  row_status_guard->setText(text);
                  const QString op_id =
                      FindStringRecursive(result, QStringLiteral("op_id"));
                  if (op_id.isEmpty() || !connector_status ||
                      page_guard == nullptr) {
                    return;
                  }
                  auto* timer = new QTimer(page);
                  QPointer<QTimer> timer_guard(timer);
                  timer->setInterval(250);
                  QObject::connect(timer, &QTimer::timeout, page, [=]() {
                    connector_status(
                        op_id, [=](const QJsonObject& status_result,
                                   const QString& poll_error) {
                          if (row_status_guard == nullptr ||
                              timer_guard == nullptr) {
                            return;
                          }
                          if (!poll_error.isEmpty()) {
                            row_status_guard->setText(poll_error);
                            timer_guard->stop();
                            return;
                          }
                          const QString poll_summary =
                              ConnectorOperationSummary(status_result);
                          const QString poll_prompt =
                              ConnectorPromptSummary(status_result);
                          QString text = poll_summary;
                          if (!poll_prompt.isEmpty()) {
                            text += (text.isEmpty() ? QString()
                                                    : QStringLiteral("\n")) +
                                    poll_prompt;
                          }
                          if (!text.isEmpty()) {
                            row_status_guard->setText(text);
                          }
                          if (status_result.value(QStringLiteral("settled"))
                                  .toBool()) {
                            timer_guard->stop();
                            (*reload)();
                          }
                        });
                  });
                  timer->start();
                });
          });
        }
      });
    };
    (*reload)();
  }

  const auto get_computer_use = context.get_computer_use_status;
  const auto grant_computer_use = context.grant_computer_use_permissions;
  const auto get_action_status = context.get_action_status;
  if (get_computer_use) {
    layout->addWidget(
        Label(page, QStringLiteral("Computer Use"), "settingTitle"));
    auto* panel = new QWidget(page);
    panel->setObjectName(QStringLiteral("computerUsePanel"));
    panel->setMaximumWidth(760);
    auto* panel_layout = new QVBoxLayout(panel);
    panel_layout->setContentsMargins(0, 0, 0, 0);
    panel_layout->setSpacing(4);
    auto* status_label =
        new QLabel(QStringLiteral("Checking Computer Use status…"), panel);
    status_label->setObjectName(QStringLiteral("computerUseStatus"));
    status_label->setWordWrap(true);
    panel_layout->addWidget(status_label);
    auto* detail_label = new QLabel(panel);
    detail_label->setObjectName(QStringLiteral("computerUseDetail"));
    detail_label->setWordWrap(true);
    detail_label->setStyleSheet(
        QStringLiteral("color: %1;").arg(context.theme.text_secondary.name()));
    panel_layout->addWidget(detail_label);
    auto* accessibility = new QLabel(panel);
    accessibility->setObjectName(QStringLiteral("computerUseAccessibility"));
    accessibility->setWordWrap(true);
    panel_layout->addWidget(accessibility);
    auto* screen_recording = new QLabel(panel);
    screen_recording->setObjectName(
        QStringLiteral("computerUseScreenRecording"));
    screen_recording->setWordWrap(true);
    panel_layout->addWidget(screen_recording);
    auto* checks_label = new QLabel(panel);
    checks_label->setObjectName(QStringLiteral("computerUseChecks"));
    checks_label->setWordWrap(true);
    checks_label->setStyleSheet(
        QStringLiteral("color: %1;").arg(context.theme.danger.name()));
    panel_layout->addWidget(checks_label);
    auto* actions = new QWidget(panel);
    auto* action_row = new QHBoxLayout(actions);
    action_row->setContentsMargins(0, 0, 0, 0);
    action_row->setSpacing(8);
    auto* refresh = Button(actions, QStringLiteral("Recheck"));
    refresh->setObjectName(QStringLiteral("computerUseRefresh"));
    auto* grant = Button(actions, QStringLiteral("Grant permissions"));
    grant->setObjectName(QStringLiteral("computerUseGrant"));
    grant->setVisible(false);
    action_row->addWidget(refresh);
    action_row->addWidget(grant);
    action_row->addStretch(1);
    panel_layout->addWidget(actions);
    layout->addWidget(panel);

    auto grant_text = [](const QJsonValue& value) {
      if (!value.isBool()) {
        return QStringLiteral("Unknown");
      }
      return value.toBool() ? QStringLiteral("Granted")
                            : QStringLiteral("Not granted");
    };
    QPointer<QWidget> page_guard(page);
    QPointer<QLabel> status_guard(status_label);
    QPointer<QLabel> detail_guard(detail_label);
    QPointer<QLabel> accessibility_guard(accessibility);
    QPointer<QLabel> screen_guard(screen_recording);
    QPointer<QLabel> checks_guard(checks_label);
    QPointer<QPushButton> grant_guard(grant);
    const auto load_status = [page_guard, status_guard, detail_guard,
                              accessibility_guard, screen_guard, checks_guard,
                              grant_guard, grant_text, get_computer_use]() {
      if (page_guard == nullptr || status_guard == nullptr) {
        return;
      }
      status_guard->setText(QStringLiteral("Checking Computer Use status…"));
      get_computer_use([status_guard, detail_guard, accessibility_guard,
                        screen_guard, checks_guard, grant_guard, grant_text](
                           const QJsonObject& status, const QString& error) {
        if (status_guard == nullptr) {
          return;
        }
        if (!error.isEmpty()) {
          status_guard->setText(error);
          return;
        }
        const QString platform =
            status.value(QStringLiteral("platform")).toString();
        if (!status.value(QStringLiteral("platform_supported")).toBool()) {
          status_guard->setText(
              QStringLiteral("Computer Use isn't supported on this platform "
                             "(%1).")
                  .arg(platform.isEmpty() ? QStringLiteral("unknown")
                                          : platform));
          if (grant_guard != nullptr) {
            grant_guard->setVisible(false);
          }
          return;
        }
        if (!status.value(QStringLiteral("installed")).toBool()) {
          status_guard->setText(
              QStringLiteral("cua-driver is not installed. Install the "
                             "Computer Use backend to drive this machine."));
          if (grant_guard != nullptr) {
            grant_guard->setVisible(false);
          }
          return;
        }
        const bool ready = status.value(QStringLiteral("ready")).toBool();
        status_guard->setText(
            ready ? QStringLiteral("Computer Use is ready.")
                  : QStringLiteral("Computer Use is not ready."));
        QStringList details;
        const QString version =
            status.value(QStringLiteral("version")).toString();
        if (!version.isEmpty()) {
          details.append(version);
        }
        details.append(QStringLiteral("platform: %1").arg(platform));
        if (detail_guard != nullptr) {
          detail_guard->setText(details.join(QStringLiteral(" · ")));
        }
        const bool can_grant =
            status.value(QStringLiteral("can_grant")).toBool();
        if (accessibility_guard != nullptr) {
          accessibility_guard->setText(
              can_grant ? QStringLiteral("Accessibility · %1")
                              .arg(grant_text(status.value(
                                  QStringLiteral("accessibility"))))
                        : QString());
          accessibility_guard->setVisible(can_grant);
        }
        if (screen_guard != nullptr) {
          screen_guard->setText(
              can_grant ? QStringLiteral("Screen Recording · %1")
                              .arg(grant_text(status.value(
                                  QStringLiteral("screen_recording"))))
                        : QString());
          screen_guard->setVisible(can_grant);
        }
        QStringList failures;
        for (const QJsonValue& value :
             status.value(QStringLiteral("checks")).toArray()) {
          const QJsonObject check = value.toObject();
          if (check.value(QStringLiteral("status")).toString() ==
              QStringLiteral("ok")) {
            continue;
          }
          failures.append(QStringLiteral("%1: %2").arg(
              check.value(QStringLiteral("label")).toString(),
              check.value(QStringLiteral("message")).toString()));
        }
        const QString status_error =
            status.value(QStringLiteral("error")).toString();
        if (!status_error.isEmpty()) {
          failures.append(status_error);
        }
        if (checks_guard != nullptr) {
          checks_guard->setText(failures.join(QStringLiteral("\n")));
        }
        if (grant_guard != nullptr) {
          grant_guard->setVisible(can_grant && !ready);
          grant_guard->setEnabled(can_grant && !ready);
          grant_guard->setText(QStringLiteral("Grant permissions"));
        }
      });
    };
    QObject::connect(refresh, &QPushButton::clicked, refresh,
                     [load_status]() { load_status(); });

    if (grant_computer_use && get_action_status) {
      QPointer<QTimer> poll_guard(nullptr);
      auto* poll = new QTimer(page);
      poll_guard = poll;
      poll->setInterval(1500);
      QPointer<QLabel> poll_status_guard(status_label);
      QPointer<QPushButton> poll_grant_guard(grant);
      auto attempts = std::make_shared<int>(0);
      QObject::connect(
          poll, &QTimer::timeout, poll,
          [poll_guard, poll_status_guard, poll_grant_guard, attempts,
           get_action_status, load_status]() {
            if (poll_guard == nullptr) {
              return;
            }
            if (++*attempts > 150) {
              poll_guard->stop();
              if (poll_status_guard != nullptr) {
                poll_status_guard->setText(
                    QStringLiteral("Permission grant did not finish. Recheck "
                                   "after approving in System Settings."));
              }
              if (poll_grant_guard != nullptr) {
                poll_grant_guard->setEnabled(true);
                poll_grant_guard->setText(QStringLiteral("Grant permissions"));
              }
              return;
            }
            get_action_status(
                QStringLiteral("computer-use-grant"),
                [poll_guard, poll_status_guard, poll_grant_guard, load_status](
                    const QJsonObject& result, const QString& error) {
                  if (poll_guard == nullptr) {
                    return;
                  }
                  if (!error.isEmpty()) {
                    poll_guard->stop();
                    if (poll_status_guard != nullptr) {
                      poll_status_guard->setText(error);
                    }
                    return;
                  }
                  if (result.value(QStringLiteral("running")).toBool()) {
                    return;
                  }
                  poll_guard->stop();
                  if (poll_grant_guard != nullptr) {
                    poll_grant_guard->setEnabled(true);
                  }
                  load_status();
                });
          });
      QObject::connect(
          grant, &QPushButton::clicked, grant,
          [grant, status_label, poll_guard, attempts, grant_computer_use]() {
            if (poll_guard == nullptr) {
              return;
            }
            grant->setEnabled(false);
            grant->setText(QStringLiteral("Waiting for approval…"));
            status_label->setText(
                QStringLiteral("Approve the CuaDriver prompt in System "
                               "Settings, then return here."));
            *attempts = 0;
            grant_computer_use(
                [grant, status_label, poll_guard](const QJsonObject& result,
                                                  const QString& error) {
                  if (poll_guard == nullptr) {
                    return;
                  }
                  if (!error.isEmpty() ||
                      !result.value(QStringLiteral("ok")).toBool()) {
                    status_label->setText(
                        error.isEmpty()
                            ? QStringLiteral("Could not request permissions.")
                            : error);
                    grant->setEnabled(true);
                    grant->setText(QStringLiteral("Grant permissions"));
                    return;
                  }
                  poll_guard->start();
                });
          });
    } else {
      grant->setEnabled(false);
    }
    load_status();
  }

  const auto list_backends_hook = context.list_terminal_backends;
  const auto select_backend_hook = context.select_terminal_backend;
  if (list_backends_hook && select_backend_hook) {
    layout->addWidget(
        Label(page, QStringLiteral("Terminal backend"), "settingTitle"));
    auto* panel = new QWidget(page);
    panel->setObjectName(QStringLiteral("terminalBackendPanel"));
    panel->setMaximumWidth(760);
    auto* panel_layout = new QVBoxLayout(panel);
    panel_layout->setContentsMargins(0, 0, 0, 0);
    panel_layout->setSpacing(4);
    auto* combo = new QComboBox(panel);
    combo->setObjectName(QStringLiteral("terminalBackendSelect"));
    combo->setProperty("loading", true);
    combo->addItem(QStringLiteral("Loading backends…"));
    panel_layout->addWidget(combo);
    auto* detail = new QLabel(panel);
    detail->setObjectName(QStringLiteral("terminalBackendDetail"));
    detail->setWordWrap(true);
    detail->setStyleSheet(
        QStringLiteral("color: %1;").arg(context.theme.text_secondary.name()));
    panel_layout->addWidget(detail);
    auto* status = new QLabel(panel);
    status->setObjectName(QStringLiteral("terminalBackendStatus"));
    status->setWordWrap(true);
    status->setStyleSheet(
        QStringLiteral("color: %1;").arg(context.theme.text_secondary.name()));
    panel_layout->addWidget(status);
    layout->addWidget(panel);

    QPointer<QComboBox> combo_guard(combo);
    QPointer<QLabel> detail_guard(detail);
    QPointer<QLabel> status_guard(status);
    auto load_backends = std::make_shared<std::function<void()>>();
    *load_backends = [combo_guard, detail_guard, status_guard,
                      list_backends_hook, load_backends]() {
      if (combo_guard == nullptr || detail_guard == nullptr) {
        return;
      }
      combo_guard->setProperty("loading", true);
      list_backends_hook([combo_guard, detail_guard, status_guard,
                          load_backends](const QJsonObject& result,
                                         const QString& error) {
        if (combo_guard == nullptr || detail_guard == nullptr) {
          return;
        }
        if (!error.isEmpty()) {
          combo_guard->setProperty("loading", false);
          if (status_guard != nullptr) {
            status_guard->setText(error);
          }
          return;
        }
        const QJsonArray rows =
            result.value(QStringLiteral("backends")).toArray();
        QString active = result.value(QStringLiteral("active")).toString();
        {
          const QSignalBlocker blocker(combo_guard.data());
          combo_guard->clear();
          for (const QJsonValue& value : rows) {
            const QJsonObject row = value.toObject();
            const QString name = row.value(QStringLiteral("name")).toString();
            if (name.isEmpty()) {
              continue;
            }
            QString label = row.value(QStringLiteral("label")).toString(name);
            const QString row_status =
                row.value(QStringLiteral("status")).toString();
            if (!row_status.isEmpty()) {
              label += QStringLiteral(" · ") + row_status;
            }
            combo_guard->addItem(label, name);
            const int index = combo_guard->count() - 1;
            combo_guard->setItemData(
                index,
                QStringLiteral("%1\n%2").arg(
                    row.value(QStringLiteral("description")).toString(),
                    row.value(QStringLiteral("detail")).toString()),
                Qt::UserRole + 1);
          }
          if (active.isEmpty()) {
            active = result.value(QStringLiteral("active")).toString();
          }
          const int active_index = combo_guard->findData(active);
          if (active_index >= 0) {
            combo_guard->setCurrentIndex(active_index);
          }
        }
        combo_guard->setProperty("loading", false);
        detail_guard->setText(
            combo_guard->currentData(Qt::UserRole + 1).toString());
      });
    };
    QObject::connect(
        combo, &QComboBox::currentIndexChanged, combo,
        [combo, detail, status, combo_guard, status_guard, detail_guard,
         select_backend_hook, load_backends](int index) {
          if (combo->property("loading").toBool() || index < 0) {
            return;
          }
          detail->setText(combo->itemData(index, Qt::UserRole + 1).toString());
          const QString backend = combo->itemData(index).toString();
          if (backend.isEmpty()) {
            return;
          }
          status->setText(QStringLiteral("Saving terminal backend…"));
          select_backend_hook(
              backend, [combo_guard, status_guard, backend, load_backends](
                           const QJsonObject& result, const QString& error) {
                if (combo_guard == nullptr) {
                  return;
                }
                if (!error.isEmpty() ||
                    !result.value(QStringLiteral("ok")).toBool()) {
                  if (status_guard != nullptr) {
                    status_guard->setText(
                        error.isEmpty()
                            ? QStringLiteral("Could not save the terminal "
                                             "backend.")
                            : error);
                  }
                  return;
                }
                if (status_guard != nullptr) {
                  status_guard->setText(
                      QStringLiteral("Terminal backend set to %1.")
                          .arg(result.value(QStringLiteral("backend"))
                                   .toString(backend)));
                }
                (*load_backends)();
              });
        });
    load_backends->operator()();
  }

  const QJsonArray skills = PayloadArray(data, "skills", {"skills", "rows"});
  layout->addWidget(Label(
      page, QStringLiteral("Skills (%1)").arg(skills.size()), "settingTitle"));
  auto* skill_actions = new QWidget(page);
  skill_actions->setMaximumWidth(760);
  auto* skill_action_row = new QHBoxLayout(skill_actions);
  skill_action_row->setContentsMargins(0, 0, 0, 0);
  auto* new_skill = Button(skill_actions, QStringLiteral("New skill"));
  new_skill->setObjectName(QStringLiteral("skillNew"));
  new_skill->setEnabled(static_cast<bool>(act));
  QObject::connect(
      new_skill, &QPushButton::clicked, new_skill,
      [page, act, profile_query]() {
        auto* dialog = new QDialog(page);
        dialog->setObjectName(QStringLiteral("skillNewDialog"));
        auto* form = new QVBoxLayout(dialog);
        form->addWidget(new QLabel(QStringLiteral("Name"), dialog));
        auto* name = new QLineEdit(dialog);
        name->setObjectName(QStringLiteral("skillNewName"));
        name->setPlaceholderText(QStringLiteral("lowercase-skill-name"));
        form->addWidget(name);
        form->addWidget(new QLabel(QStringLiteral("Category"), dialog));
        auto* category = new QLineEdit(dialog);
        category->setObjectName(QStringLiteral("skillNewCategory"));
        category->setPlaceholderText(QStringLiteral("optional, e.g. research"));
        form->addWidget(category);
        form->addWidget(new QLabel(QStringLiteral("SKILL.md"), dialog));
        auto* content = new QPlainTextEdit(dialog);
        content->setObjectName(QStringLiteral("skillNewContent"));
        content->setPlaceholderText(QStringLiteral(
            "---\nname: my-skill\ndescription: What it does\n---\n"));
        form->addWidget(content, 1);
        auto* buttons = new QWidget(dialog);
        auto* button_row = new QHBoxLayout(buttons);
        button_row->setContentsMargins(0, 0, 0, 0);
        auto* submit = Button(buttons, QStringLiteral("Create"));
        submit->setObjectName(QStringLiteral("skillNewSubmit"));
        QObject::connect(submit, &QPushButton::clicked, dialog, [=]() {
          const QString skill_name = name->text().trimmed();
          if (skill_name.isEmpty()) {
            return;
          }
          QJsonObject body{{QStringLiteral("name"), skill_name},
                           {QStringLiteral("content"), content->toPlainText()}};
          const QString skill_category = category->text().trimmed();
          if (!skill_category.isEmpty()) {
            body.insert(QStringLiteral("category"), skill_category);
          }
          act(QStringLiteral("capabilities"), QStringLiteral("POST"),
              QStringLiteral("/api/skills") + profile_query, body);
          dialog->accept();
        });
        auto* cancel = Button(buttons, QStringLiteral("Cancel"));
        QObject::connect(cancel, &QPushButton::clicked, dialog,
                         [dialog]() { dialog->reject(); });
        button_row->addWidget(submit);
        button_row->addWidget(cancel);
        button_row->addStretch(1);
        form->addWidget(buttons);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->resize(520, 460);
        dialog->open();
      });
  skill_action_row->addWidget(new_skill);
  auto* browse_hub = Button(skill_actions, QStringLiteral("Browse skill hub"));
  browse_hub->setObjectName(QStringLiteral("skillHubBrowse"));
  browse_hub->setEnabled(static_cast<bool>(context.list_official_skills) ||
                         static_cast<bool>(context.list_skill_hub_sources) ||
                         static_cast<bool>(context.search_skill_hub));
  QObject::connect(
      browse_hub, &QPushButton::clicked, browse_hub,
      [page, act, profile_query, list_official = context.list_official_skills,
       list_sources = context.list_skill_hub_sources,
       hub_search = context.search_skill_hub,
       hub_preview = context.preview_skill_hub,
       hub_scan = context.scan_skill_hub]() {
        auto* dialog = new QDialog(page);
        dialog->setObjectName(QStringLiteral("skillHubDialog"));
        auto* form = new QVBoxLayout(dialog);
        auto* status =
            Label(dialog, QStringLiteral("Loading official catalog…"),
                  "fieldDescription");
        status->setObjectName(QStringLiteral("skillHubStatus"));
        form->addWidget(status);

        auto* search_row = new QWidget(dialog);
        auto* search_layout = new QHBoxLayout(search_row);
        search_layout->setContentsMargins(0, 0, 0, 0);
        auto* query = new QLineEdit(search_row);
        query->setObjectName(QStringLiteral("skillHubQuery"));
        query->setPlaceholderText(
            QStringLiteral("Search every hub source (GitHub, index, …)"));
        auto* source = new QComboBox(search_row);
        source->setObjectName(QStringLiteral("skillHubSource"));
        source->addItem(QStringLiteral("All sources"), QStringLiteral("all"));
        auto* search = Button(search_row, QStringLiteral("Search"));
        search->setObjectName(QStringLiteral("skillHubSearch"));
        search->setEnabled(static_cast<bool>(hub_search));
        search_layout->addWidget(query, 1);
        search_layout->addWidget(source);
        search_layout->addWidget(search);
        form->addWidget(search_row);

        auto* list = new QListWidget(dialog);
        list->setObjectName(QStringLiteral("skillHubList"));
        form->addWidget(list, 1);
        auto* detail = new QPlainTextEdit(dialog);
        detail->setObjectName(QStringLiteral("skillHubDetail"));
        detail->setReadOnly(true);
        detail->setMinimumHeight(120);
        detail->setMaximumHeight(220);
        form->addWidget(detail);
        auto* buttons = new QWidget(dialog);
        auto* button_row = new QHBoxLayout(buttons);
        button_row->setContentsMargins(0, 0, 0, 0);
        auto* preview = Button(buttons, QStringLiteral("Preview"));
        preview->setObjectName(QStringLiteral("skillHubPreview"));
        preview->setEnabled(false);
        auto* scan = Button(buttons, QStringLiteral("Scan"));
        scan->setObjectName(QStringLiteral("skillHubScan"));
        scan->setEnabled(false);
        auto* install = Button(buttons, QStringLiteral("Install"));
        install->setObjectName(QStringLiteral("skillHubInstall"));
        install->setEnabled(false);
        auto* close = Button(buttons, QStringLiteral("Close"));
        QObject::connect(close, &QPushButton::clicked, dialog,
                         [dialog]() { dialog->reject(); });
        button_row->addWidget(preview);
        button_row->addWidget(scan);
        button_row->addWidget(install);
        button_row->addWidget(close);
        button_row->addStretch(1);
        form->addWidget(buttons);

        const auto identifier_at = [list]() -> QString {
          if (list->currentItem() == nullptr) {
            return QString();
          }
          return list->currentItem()->data(Qt::UserRole).toString();
        };
        const auto add_result = [list](const QJsonObject& entry,
                                       bool installed) {
          const QString name = entry.value(QStringLiteral("name")).toString();
          const QString identifier =
              entry.value(QStringLiteral("identifier")).toString();
          if (identifier.isEmpty()) {
            return;
          }
          QString description =
              entry.value(QStringLiteral("description")).toString();
          const QString source =
              entry.value(QStringLiteral("source")).toString();
          const QString trust =
              entry.value(QStringLiteral("trust_level")).toString();
          if (!source.isEmpty()) {
            description += description.isEmpty()
                               ? source
                               : QStringLiteral(" · %1").arg(source);
          }
          if (!trust.isEmpty() && trust != source) {
            description += QStringLiteral(" · %1").arg(trust);
          }
          if (installed) {
            description += QStringLiteral(" · installed");
          }
          auto* item = new QListWidgetItem(
              description.isEmpty()
                  ? name
                  : QStringLiteral("%1 — %2").arg(name, description),
              list);
          item->setData(Qt::UserRole, identifier);
          item->setData(Qt::UserRole + 1, installed);
        };

        QPointer<QListWidget> official_list_guard(list);
        QPointer<QLabel> official_status_guard(status);
        if (list_official) {
          list_official(
              [official_list_guard, official_status_guard, add_result](
                  const QJsonObject& result, const QString& error) {
                if (official_list_guard == nullptr ||
                    official_status_guard == nullptr) {
                  return;
                }
                if (!error.isEmpty()) {
                  official_status_guard->setText(error);
                  return;
                }
                const QJsonArray catalog =
                    result.value(QStringLiteral("skills")).toArray();
                for (const QJsonValue& value : catalog) {
                  const QJsonObject entry = value.toObject();
                  QString description =
                      entry.value(QStringLiteral("description")).toString();
                  const QString category =
                      entry.value(QStringLiteral("category")).toString();
                  if (!category.isEmpty()) {
                    description += description.isEmpty()
                                       ? QStringLiteral("(%1)").arg(category)
                                       : QStringLiteral(" · %1").arg(category);
                  }
                  QJsonObject display = entry;
                  display.insert(QStringLiteral("description"), description);
                  add_result(display,
                             entry.value(QStringLiteral("installed")).toBool());
                }
                official_status_guard->setText(
                    QStringLiteral("%1 official skill%2")
                        .arg(official_list_guard->count())
                        .arg(official_list_guard->count() == 1
                                 ? QString()
                                 : QStringLiteral("s")));
              });
        } else {
          status->setText(
              QStringLiteral("Search hub sources or choose a source below."));
        }

        if (list_sources) {
          QPointer<QComboBox> source_guard(source);
          QPointer<QWidget> dialog_guard(dialog);
          list_sources([source_guard, dialog_guard](
                           const QJsonObject& result,
                           const QString& source_error) {
            if (source_guard == nullptr || dialog_guard == nullptr ||
                !source_error.isEmpty()) {
              return;
            }
            const QJsonArray sources =
                result.value(QStringLiteral("sources")).toArray();
            for (const QJsonValue& value : sources) {
              const QJsonObject entry = value.toObject();
              const QString id = entry.value(QStringLiteral("id")).toString();
              if (id.isEmpty()) {
                continue;
              }
              QString label = entry.value(QStringLiteral("label")).toString(id);
              if (entry.value(QStringLiteral("rate_limited")).toBool()) {
                label += QStringLiteral(" (rate limited)");
              } else if (entry.contains(QStringLiteral("available")) &&
                         !entry.value(QStringLiteral("available")).toBool()) {
                label += QStringLiteral(" (unavailable)");
              }
              source_guard->addItem(label, id);
            }
          });
        }

        QObject::connect(
            list, &QListWidget::currentRowChanged, dialog,
            [list, preview, scan, install, hub_preview, hub_scan](int row) {
              if (row < 0) {
                preview->setEnabled(false);
                scan->setEnabled(false);
                install->setEnabled(false);
                return;
              }
              preview->setEnabled(static_cast<bool>(hub_preview));
              scan->setEnabled(static_cast<bool>(hub_scan));
              install->setEnabled(
                  !list->item(row)->data(Qt::UserRole + 1).toBool() &&
                  !list->item(row)->data(Qt::UserRole + 2).toBool());
            });

        QObject::connect(
            search, &QPushButton::clicked, dialog,
            [query, source, search, list, status, detail, add_result,
             hub_search]() {
              if (!hub_search) {
                return;
              }
              const QString text = query->text().trimmed();
              if (text.isEmpty()) {
                status->setText(QStringLiteral("Type a search query."));
                return;
              }
              list->clear();
              detail->clear();
              status->setText(QStringLiteral("Searching hub sources…"));
              search->setEnabled(false);
              QPointer<QListWidget> list_guard(list);
              QPointer<QPushButton> search_guard(search);
              QPointer<QLabel> status_guard(status);
              hub_search(
                  text, source->currentData().toString(), 20,
                  [list_guard, search_guard, status_guard, add_result](
                      const QJsonObject& result, const QString& error) {
                    if (list_guard == nullptr || search_guard == nullptr ||
                        status_guard == nullptr) {
                      return;
                    }
                    search_guard->setEnabled(true);
                    if (!error.isEmpty()) {
                      status_guard->setText(error);
                      return;
                    }
                    const QJsonArray results =
                        result.value(QStringLiteral("results")).toArray();
                    const QJsonObject installed =
                        result.value(QStringLiteral("installed")).toObject();
                    for (const QJsonValue& value : results) {
                      const QJsonObject entry = value.toObject();
                      const QString identifier =
                          entry.value(QStringLiteral("identifier")).toString();
                      add_result(entry, installed.contains(identifier));
                    }
                    QString summary = QStringLiteral("%1 result%2")
                                          .arg(list_guard->count())
                                          .arg(list_guard->count() == 1
                                                   ? QString()
                                                   : QStringLiteral("s"));
                    const QJsonArray timed_out =
                        result.value(QStringLiteral("timed_out")).toArray();
                    if (!timed_out.isEmpty()) {
                      QStringList names;
                      for (const QJsonValue& value : timed_out) {
                        names.append(value.toString());
                      }
                      summary += QStringLiteral(" · timed out: %1")
                                     .arg(names.join(QStringLiteral(", ")));
                    }
                    status_guard->setText(summary);
                  });
            });

        QObject::connect(
            preview, &QPushButton::clicked, dialog,
            [identifier_at, detail, status, hub_preview]() {
              if (!hub_preview) {
                return;
              }
              const QString identifier = identifier_at();
              if (identifier.isEmpty()) {
                return;
              }
              status->setText(QStringLiteral("Loading preview…"));
              QPointer<QPlainTextEdit> detail_guard(detail);
              QPointer<QLabel> status_guard(status);
              hub_preview(identifier, [detail_guard, status_guard, identifier](
                                          const QJsonObject& result,
                                          const QString& error) {
                if (detail_guard == nullptr || status_guard == nullptr) {
                  return;
                }
                if (!error.isEmpty()) {
                  status_guard->setText(error);
                  return;
                }
                QString text = result.value(QStringLiteral("name")).toString();
                const QString source =
                    result.value(QStringLiteral("source")).toString();
                const QString trust =
                    result.value(QStringLiteral("trust_level")).toString();
                text += QStringLiteral("\n%1").arg(
                    result.value(QStringLiteral("identifier")).toString());
                if (!source.isEmpty() || !trust.isEmpty()) {
                  text += QStringLiteral("\n%1 · %2").arg(source, trust);
                }
                const QString description =
                    result.value(QStringLiteral("description")).toString();
                if (!description.isEmpty()) {
                  text += QStringLiteral("\n\n%1").arg(description);
                }
                text += QStringLiteral("\n\n%1").arg(
                    result.value(QStringLiteral("skill_md")).toString());
                const QJsonArray files =
                    result.value(QStringLiteral("files")).toArray();
                if (!files.isEmpty()) {
                  QStringList names;
                  for (const QJsonValue& value : files) {
                    names.append(value.toString());
                  }
                  text += QStringLiteral("\n\nFiles:\n - %1")
                              .arg(names.join(QStringLiteral("\n - ")));
                }
                detail_guard->setPlainText(text);
                status_guard->setText(
                    QStringLiteral("Preview: %1")
                        .arg(result.value(QStringLiteral("name"))
                                 .toString(identifier)));
              });
            });

        QObject::connect(
            scan, &QPushButton::clicked, dialog,
            [identifier_at, detail, status, install, list, hub_scan]() {
              if (!hub_scan) {
                return;
              }
              const QString identifier = identifier_at();
              if (identifier.isEmpty()) {
                return;
              }
              status->setText(QStringLiteral("Scanning skill…"));
              QPointer<QPlainTextEdit> detail_guard(detail);
              QPointer<QLabel> status_guard(status);
              QPointer<QPushButton> install_guard(install);
              QPointer<QListWidget> list_guard(list);
              hub_scan(identifier, [detail_guard, status_guard, install_guard,
                                    list_guard,
                                    identifier](const QJsonObject& result,
                                                const QString& error) {
                if (detail_guard == nullptr || status_guard == nullptr) {
                  return;
                }
                if (!error.isEmpty()) {
                  status_guard->setText(error);
                  return;
                }
                const QString verdict =
                    result.value(QStringLiteral("verdict")).toString();
                const QString policy =
                    result.value(QStringLiteral("policy")).toString();
                QString text =
                    QStringLiteral("%1 (%2)\n%3")
                        .arg(
                            result.value(QStringLiteral("name")).toString(),
                            result.value(QStringLiteral("identifier"))
                                .toString(),
                            result.value(QStringLiteral("summary")).toString());
                text += QStringLiteral("\nVerdict: %1 · policy: %2")
                            .arg(verdict, policy);
                const QJsonArray findings =
                    result.value(QStringLiteral("findings")).toArray();
                for (const QJsonValue& value : findings) {
                  const QJsonObject finding = value.toObject();
                  text += QStringLiteral("\n- [%1] %2")
                              .arg(finding.value(QStringLiteral("severity"))
                                       .toString(),
                                   finding.value(QStringLiteral("description"))
                                       .toString());
                }
                detail_guard->setPlainText(text);
                if (policy == QStringLiteral("block")) {
                  if (install_guard != nullptr) {
                    install_guard->setEnabled(false);
                  }
                  if (list_guard != nullptr) {
                    for (int row = 0; row < list_guard->count(); ++row) {
                      QListWidgetItem* item = list_guard->item(row);
                      if (item->data(Qt::UserRole).toString() == identifier) {
                        item->setData(Qt::UserRole + 2, true);
                        break;
                      }
                    }
                  }
                }
                status_guard->setText(
                    QStringLiteral("Scan: %1 · %2").arg(verdict, policy));
              });
            });

        QObject::connect(
            install, &QPushButton::clicked, dialog,
            [act, list, status, profile_query]() {
              QListWidgetItem* item = list->currentItem();
              if (item == nullptr) {
                return;
              }
              const QString identifier = item->data(Qt::UserRole).toString();
              act(QStringLiteral("capabilities"), QStringLiteral("POST"),
                  QStringLiteral("/api/skills/hub/install") + profile_query,
                  QJsonObject{{QStringLiteral("identifier"), identifier}});
              status->setText(
                  QStringLiteral("Install started: %1").arg(identifier));
              item->setData(Qt::UserRole + 1, true);
            });
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->resize(760, 680);
        dialog->open();
      });
  skill_action_row->addWidget(browse_hub);
  auto* update_hub = Button(skill_actions, QStringLiteral("Update installed"));
  update_hub->setObjectName(QStringLiteral("skillHubUpdate"));
  update_hub->setEnabled(static_cast<bool>(act));
  QObject::connect(
      update_hub, &QPushButton::clicked, update_hub, [act, profile_query]() {
        act(QStringLiteral("capabilities"), QStringLiteral("POST"),
            QStringLiteral("/api/skills/hub/update") + profile_query,
            QJsonObject());
      });
  skill_action_row->addWidget(update_hub);
  skill_action_row->addStretch(1);
  layout->addWidget(skill_actions);
  const auto get_skill_content = context.get_skill_content;
  int shown = 0;
  for (const QJsonValue& value : skills) {
    if (shown++ >= 40) {
      break;
    }
    const QJsonObject skill = value.toObject();
    const QString name = skill.value(QStringLiteral("name")).toString();
    const bool enabled = skill.value(QStringLiteral("enabled")).toBool(true);
    const bool locked = skill.value(QStringLiteral("locked")).toBool();
    QString detail = skill.value(QStringLiteral("description")).toString();
    const QString provenance =
        skill.value(QStringLiteral("provenance")).toString();
    if (!provenance.isEmpty()) {
      detail +=
          detail.isEmpty() ? provenance : QStringLiteral(" · ") + provenance;
    }
    const int usage = skill.value(QStringLiteral("usage")).toInt();
    if (usage > 0) {
      detail += QStringLiteral(" · %1 use%2")
                    .arg(usage)
                    .arg(usage == 1 ? QString() : QStringLiteral("s"));
    }
    Row row = AddRow(page, layout, name, detail);
    row.widget->setObjectName(QStringLiteral("skillRow_") + name);
    row.widget->setMaximumWidth(760);

    auto* status = Label(row.widget, QString(), "fieldDescription");
    status->setObjectName(QStringLiteral("skillStatus_") + name);
    status->setMinimumWidth(60);
    row.layout->addWidget(status);

    auto* edit = Button(row.widget, QStringLiteral("Edit"));
    edit->setObjectName(QStringLiteral("skillEdit_") + name);
    edit->setEnabled(static_cast<bool>(get_skill_content));
    QObject::connect(
        edit, &QPushButton::clicked, edit,
        [page, act, name, status, get_skill_content, profile_query]() {
          if (!get_skill_content) {
            return;
          }
          QPointer<QWidget> page_guard(page);
          QPointer<QLabel> status_guard(status);
          get_skill_content(name, [page_guard, status_guard, act, name,
                                   profile_query](const QJsonObject& result,
                                                  const QString& error) {
            if (page_guard == nullptr || status_guard == nullptr) {
              return;
            }
            if (!error.isEmpty()) {
              status_guard->setText(error);
              return;
            }
            status_guard->clear();
            QWidget* page = page_guard.data();
            const QString content =
                result.value(QStringLiteral("content")).toString();
            const QString path =
                result.value(QStringLiteral("path")).toString();
            auto* dialog = new QDialog(page);
            dialog->setObjectName(QStringLiteral("skillEditDialog_") + name);
            auto* form = new QVBoxLayout(dialog);
            form->addWidget(new QLabel(QStringLiteral("SKILL.md"), dialog));
            auto* editor = new QPlainTextEdit(dialog);
            editor->setObjectName(QStringLiteral("skillEditContent"));
            editor->setPlainText(content);
            form->addWidget(editor, 1);
            if (!path.isEmpty()) {
              auto* source = Label(dialog, path, "fieldDescription");
              source->setObjectName(QStringLiteral("skillEditPath"));
              source->setTextInteractionFlags(Qt::TextSelectableByMouse);
              form->addWidget(source);
            }
            auto* buttons = new QWidget(dialog);
            auto* button_row = new QHBoxLayout(buttons);
            button_row->setContentsMargins(0, 0, 0, 0);
            auto* save = Button(buttons, QStringLiteral("Save"));
            save->setObjectName(QStringLiteral("skillEditSubmit"));
            QObject::connect(save, &QPushButton::clicked, dialog, [=]() {
              if (editor->toPlainText() != content) {
                act(QStringLiteral("capabilities"), QStringLiteral("PUT"),
                    QStringLiteral("/api/skills/content") + profile_query,
                    QJsonObject{
                        {QStringLiteral("name"), name},
                        {QStringLiteral("content"), editor->toPlainText()}});
              }
              dialog->accept();
            });
            auto* cancel = Button(buttons, QStringLiteral("Cancel"));
            QObject::connect(cancel, &QPushButton::clicked, dialog,
                             [dialog]() { dialog->reject(); });
            button_row->addWidget(save);
            button_row->addWidget(cancel);
            button_row->addStretch(1);
            form->addWidget(buttons);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->resize(620, 520);
            dialog->open();
          });
        });
    row.layout->addWidget(edit);

    if (provenance == QStringLiteral("hub")) {
      auto* uninstall = Button(row.widget, QStringLiteral("Uninstall"));
      uninstall->setObjectName(QStringLiteral("skillUninstall_") + name);
      uninstall->setEnabled(static_cast<bool>(act));
      QObject::connect(
          uninstall, &QPushButton::clicked, uninstall,
          [act, name, profile_query]() {
            act(QStringLiteral("capabilities"), QStringLiteral("POST"),
                QStringLiteral("/api/skills/hub/uninstall") + profile_query,
                QJsonObject{{QStringLiteral("name"), name}});
          });
      row.layout->addWidget(uninstall);
    }

    auto* lock = Button(
        row.widget, locked ? QStringLiteral("Unlock") : QStringLiteral("Lock"));
    lock->setObjectName(QStringLiteral("skillLock_") + name);
    lock->setToolTip(
        QStringLiteral("Protect against agent edits (your edits still work)"));
    lock->setEnabled(static_cast<bool>(act));
    QObject::connect(lock, &QPushButton::clicked, lock,
                     [act, name, locked, profile_query]() {
                       act(QStringLiteral("capabilities"),
                           QStringLiteral("PUT"),
                           QStringLiteral("/api/skills/lock") + profile_query,
                           QJsonObject{{QStringLiteral("name"), name},
                                       {QStringLiteral("locked"), !locked}});
                     });
    row.layout->addWidget(lock);

    BoolCheck(row.widget, QStringLiteral("skillToggle_") + name, enabled,
              static_cast<bool>(act)
                  ? std::function<void(bool)>([=](bool checked) {
                      act(QStringLiteral("capabilities"), QStringLiteral("PUT"),
                          QStringLiteral("/api/skills/toggle") + profile_query,
                          QJsonObject{{QStringLiteral("name"), name},
                                      {QStringLiteral("enabled"), checked}});
                    })
                  : std::function<void(bool)>(),
              row.layout);
  }

  const QJsonArray servers = PayloadArray(data, "mcp", {"servers", "rows"});
  layout->addWidget(
      Label(page, QStringLiteral("MCP servers (%1)").arg(servers.size()),
            "settingTitle"));

  // Inline add form: unlike a modal, every field stays on screen and can be
  // driven directly by the native test.
  auto* add_row = new QWidget(page);
  add_row->setMaximumWidth(760);
  auto* add_layout = new QHBoxLayout(add_row);
  add_layout->setContentsMargins(0, 0, 0, 0);
  auto* add_name = new QLineEdit(add_row);
  add_name->setObjectName(QStringLiteral("mcpNewName"));
  add_name->setPlaceholderText(QStringLiteral("Name"));
  add_name->setFixedWidth(120);
  auto* add_url = new QLineEdit(add_row);
  add_url->setObjectName(QStringLiteral("mcpNewUrl"));
  add_url->setPlaceholderText(QStringLiteral("https://server/mcp"));
  auto* add_command = new QLineEdit(add_row);
  add_command->setObjectName(QStringLiteral("mcpNewCommand"));
  add_command->setPlaceholderText(QStringLiteral("or command"));
  add_command->setFixedWidth(140);
  auto* add_args = new QLineEdit(add_row);
  add_args->setObjectName(QStringLiteral("mcpNewArgs"));
  add_args->setPlaceholderText(QStringLiteral("args"));
  add_args->setFixedWidth(110);
  auto* add = Button(add_row, QStringLiteral("Add server"));
  add->setObjectName(QStringLiteral("mcpAdd"));
  add->setEnabled(static_cast<bool>(act));
  QObject::connect(add, &QPushButton::clicked, add, [=]() {
    const QString name = add_name->text().trimmed();
    const QString url = add_url->text().trimmed();
    const QString command = add_command->text().trimmed();
    if (name.isEmpty() || (url.isEmpty() && command.isEmpty())) {
      return;
    }
    QJsonObject body{{QStringLiteral("name"), name}};
    if (!url.isEmpty()) {
      body.insert(QStringLiteral("url"), url);
    } else {
      body.insert(QStringLiteral("command"), command);
      QStringList args;
      for (const QString& arg :
           add_args->text().split(QRegularExpression(QStringLiteral("\\s+")),
                                  Qt::SkipEmptyParts)) {
        args.append(arg);
      }
      if (!args.isEmpty()) {
        body.insert(QStringLiteral("args"), QJsonArray::fromStringList(args));
      }
    }
    act(QStringLiteral("capabilities"), QStringLiteral("POST"),
        QStringLiteral("/api/mcp/servers") + profile_query, body);
    add_name->clear();
    add_url->clear();
    add_command->clear();
    add_args->clear();
  });
  add_layout->addWidget(add_name);
  add_layout->addWidget(add_url, 1);
  add_layout->addWidget(add_command);
  add_layout->addWidget(add_args);
  add_layout->addWidget(add);
  layout->addWidget(add_row);

  const auto list_catalog = context.list_mcp_catalog;
  const auto get_mcp_config = context.get_mcp_config;
  const auto replace_mcp_config = context.replace_mcp_config;
  auto* catalog_row = new QWidget(page);
  auto* catalog_layout = new QHBoxLayout(catalog_row);
  catalog_layout->setContentsMargins(0, 0, 0, 0);
  auto* catalog = Button(catalog_row, QStringLiteral("Browse catalog"));
  catalog->setObjectName(QStringLiteral("mcpCatalogBrowse"));
  catalog->setEnabled(static_cast<bool>(list_catalog) &&
                      static_cast<bool>(act));
  QObject::connect(
      catalog, &QPushButton::clicked, catalog,
      [page, act, list_catalog, profile_query]() {
        QPointer<QWidget> page_guard(page);
        list_catalog([page_guard, act, profile_query](const QJsonObject& result,
                                                      const QString& error) {
          if (page_guard == nullptr || !error.isEmpty()) {
            return;
          }
          QWidget* page = page_guard.data();
          const QJsonArray entries =
              result.value(QStringLiteral("entries")).toArray();
          auto* dialog = new QDialog(page);
          dialog->setObjectName(QStringLiteral("mcpCatalogDialog"));
          auto* form = new QVBoxLayout(dialog);
          auto* heading = new QLabel(
              QStringLiteral("Approved MCP catalog (%1)").arg(entries.size()),
              dialog);
          form->addWidget(heading);
          auto* status = Label(dialog, QString(), "fieldDescription");
          status->setObjectName(QStringLiteral("mcpCatalogStatus"));
          form->addWidget(status);
          auto* scroll = new QScrollArea(dialog);
          scroll->setWidgetResizable(true);
          auto* body = new QWidget(scroll);
          auto* list = new QVBoxLayout(body);
          list->setContentsMargins(0, 0, 0, 0);
          int shown = 0;
          for (const QJsonValue& value : entries) {
            if (shown++ >= 80) {
              break;
            }
            const QJsonObject entry = value.toObject();
            const QString name = entry.value(QStringLiteral("name")).toString();
            if (name.isEmpty()) {
              continue;
            }
            const bool installed =
                entry.value(QStringLiteral("installed")).toBool();
            QString detail =
                entry.value(QStringLiteral("description")).toString();
            const QString transport =
                entry.value(QStringLiteral("transport")).toString();
            if (!transport.isEmpty()) {
              detail += detail.isEmpty() ? transport
                                         : QStringLiteral(" · ") + transport;
            }
            if (installed) {
              detail += detail.isEmpty() ? QStringLiteral("installed")
                                         : QStringLiteral(" · installed");
            }
            Row row = AddRow(body, list, name, detail);
            row.widget->setObjectName(QStringLiteral("mcpCatalogRow_") + name);
            QJsonArray required =
                entry.value(QStringLiteral("required_env")).toArray();
            for (const QJsonValue& required_value : required) {
              const QJsonObject env = required_value.toObject();
              const QString key = env.value(QStringLiteral("name")).toString();
              if (key.isEmpty()) {
                continue;
              }
              auto* field = new QLineEdit(row.widget);
              field->setObjectName(QStringLiteral("mcpCatalogEnv_") + name +
                                   QStringLiteral("_") + key);
              field->setPlaceholderText(
                  env.value(QStringLiteral("prompt")).toString(key));
              field->setEchoMode(QLineEdit::Password);
              field->setFixedWidth(140);
              row.layout->addWidget(field);
            }
            auto* install = Button(row.widget, QStringLiteral("Install"));
            install->setObjectName(QStringLiteral("mcpCatalogInstall_") + name);
            install->setEnabled(!installed);
            QObject::connect(
                install, &QPushButton::clicked, dialog,
                [act, entry, install, name, profile_query, row, status]() {
                  QJsonObject env;
                  for (QLineEdit* field :
                       row.widget->findChildren<QLineEdit*>()) {
                    const QString object_name = field->objectName();
                    const QString prefix = QStringLiteral("mcpCatalogEnv_") +
                                           name + QStringLiteral("_");
                    if (field->text().isEmpty() ||
                        !object_name.startsWith(prefix)) {
                      continue;
                    }
                    env.insert(object_name.mid(prefix.size()), field->text());
                  }
                  QJsonObject body{{QStringLiteral("name"), name},
                                   {QStringLiteral("enable"), true}};
                  if (!env.isEmpty()) {
                    body.insert(QStringLiteral("env"), env);
                  }
                  act(QStringLiteral("capabilities"), QStringLiteral("POST"),
                      QStringLiteral("/api/mcp/catalog/install") +
                          profile_query,
                      body);
                  const bool background =
                      entry.value(QStringLiteral("needs_install")).toBool();
                  status->setText(
                      background
                          ? QStringLiteral("Installing %1 in the background…")
                                .arg(name)
                          : QStringLiteral("Installed %1.").arg(name));
                  install->setEnabled(false);
                });
            row.layout->addWidget(install);
          }
          scroll->setWidget(body);
          form->addWidget(scroll, 1);
          auto* buttons = new QWidget(dialog);
          auto* button_row = new QHBoxLayout(buttons);
          button_row->setContentsMargins(0, 0, 0, 0);
          auto* close = Button(buttons, QStringLiteral("Close"));
          QObject::connect(close, &QPushButton::clicked, dialog,
                           [dialog]() { dialog->reject(); });
          button_row->addWidget(close);
          button_row->addStretch(1);
          form->addWidget(buttons);
          dialog->setAttribute(Qt::WA_DeleteOnClose);
          dialog->resize(720, 620);
          dialog->open();
        });
      });
  catalog_layout->addWidget(catalog);
  catalog_layout->addStretch(1);
  layout->addWidget(catalog_row);

  const auto test_server = context.test_mcp_server;
  const auto start_oauth = context.start_mcp_oauth;
  const auto poll_oauth = context.poll_mcp_oauth;
  const auto cancel_flow = context.cancel_mcp_oauth;
  const auto open_url = context.open_url;
  const auto refresh_page = context.refresh;
  for (const QJsonValue& value : servers) {
    const QJsonObject server = value.toObject();
    const QString name = server.value(QStringLiteral("name")).toString();
    const bool enabled = server.value(QStringLiteral("enabled")).toBool();
    const QString transport =
        server.value(QStringLiteral("transport")).toString();
    QString detail =
        enabled ? QStringLiteral("enabled") : QStringLiteral("disabled");
    if (!transport.isEmpty()) {
      detail += QStringLiteral(" · ") + transport;
    }
    Row row = AddRow(page, layout, name, detail);
    row.widget->setObjectName(QStringLiteral("mcpRow_") + name);
    row.widget->setMaximumWidth(760);

    auto* status = Label(row.widget, QString(), "fieldDescription");
    status->setObjectName(QStringLiteral("mcpStatus_") + name);
    status->setMinimumWidth(80);
    row.layout->addWidget(status);

    const QString auth = server.value(QStringLiteral("auth")).toString();
    const QString source = server.value(QStringLiteral("source")).toString();
    const bool can_oauth = transport == QStringLiteral("http") &&
                           auth != QStringLiteral("header") &&
                           source != QStringLiteral("plugin");
    if (can_oauth) {
      auto* oauth = Button(row.widget, QStringLiteral("OAuth"));
      oauth->setObjectName(QStringLiteral("mcpOAuth_") + name);
      oauth->setEnabled(static_cast<bool>(start_oauth) &&
                        static_cast<bool>(poll_oauth));
      auto* cancel = Button(row.widget, QStringLiteral("Cancel"));
      cancel->setObjectName(QStringLiteral("mcpOAuthCancel_") + name);
      cancel->setVisible(false);
      cancel->setEnabled(static_cast<bool>(cancel_flow));
      QObject::connect(
          oauth, &QPushButton::clicked, oauth,
          [page, name, status, start_oauth, poll_oauth, test_server,
           refresh_page, open_url, cancel, cancel_flow]() {
            QPointer<QWidget> page_guard(page);
            QPointer<QLabel> status_guard(status);
            QPointer<QPushButton> cancel_guard(cancel);
            start_oauth(name, [page_guard, status_guard, cancel_guard, name,
                               poll_oauth, test_server, refresh_page, open_url,
                               cancel_flow](const QJsonObject& flow,
                                            const QString& error) {
              if (page_guard == nullptr || status_guard == nullptr) {
                return;
              }
              if (!error.isEmpty()) {
                status_guard->setText(error);
                return;
              }
              const QString flow_id =
                  flow.value(QStringLiteral("flow_id")).toString();
              const QString url =
                  flow.value(QStringLiteral("authorization_url")).toString();
              if (flow_id.isEmpty()) {
                status_guard->setText(
                    flow.value(QStringLiteral("error"))
                        .toString(QStringLiteral("OAuth did not start.")));
                return;
              }
              if (!url.isEmpty()) {
                if (open_url) {
                  open_url(url);
                } else {
                  QDesktopServices::openUrl(QUrl(url));
                }
              }
              status_guard->setText(
                  QStringLiteral("Waiting for authorization…"));
              if (cancel_guard != nullptr) {
                cancel_guard->setVisible(true);
              }
              auto* timer = new QTimer(page_guard);
              auto polling = std::make_shared<bool>(false);
              QObject::connect(
                  timer, &QTimer::timeout, page_guard,
                  [timer, polling, flow_id, name, status_guard, cancel_guard,
                   poll_oauth, test_server, refresh_page, cancel_flow]() {
                    if (*polling) {
                      return;
                    }
                    *polling = true;
                    poll_oauth(flow_id, [timer, polling, name, status_guard,
                                         cancel_guard, test_server,
                                         refresh_page](
                                            const QJsonObject& current,
                                            const QString& error) {
                      *polling = false;
                      if (timer == nullptr || status_guard == nullptr) {
                        return;
                      }
                      if (!error.isEmpty()) {
                        timer->stop();
                        if (cancel_guard != nullptr) {
                          cancel_guard->setVisible(false);
                        }
                        status_guard->setText(error);
                        return;
                      }
                      const QString state =
                          current.value(QStringLiteral("status")).toString();
                      if (state == QStringLiteral("approved")) {
                        timer->stop();
                        if (cancel_guard != nullptr) {
                          cancel_guard->setVisible(false);
                        }
                        if (test_server) {
                          QPointer<QLabel> label_guard(status_guard);
                          test_server(name, [label_guard, refresh_page](
                                                const QJsonObject& result,
                                                const QString&) {
                            if (label_guard == nullptr) {
                              return;
                            }
                            if (result.value(QStringLiteral("ok")).toBool()) {
                              const int tools =
                                  result.value(QStringLiteral("tools"))
                                      .toArray()
                                      .size();
                              label_guard->setText(
                                  QStringLiteral("OAuth complete · %1 tool%2")
                                      .arg(tools)
                                      .arg(tools == 1 ? QString()
                                                      : QStringLiteral("s")));
                            } else {
                              label_guard->setText(
                                  QStringLiteral("OAuth complete"));
                            }
                            if (refresh_page) {
                              refresh_page(QStringLiteral("capabilities"));
                            }
                          });
                        } else {
                          status_guard->setText(
                              QStringLiteral("OAuth complete"));
                          if (refresh_page) {
                            refresh_page(QStringLiteral("capabilities"));
                          }
                        }
                      } else if (state == QStringLiteral("error")) {
                        timer->stop();
                        if (cancel_guard != nullptr) {
                          cancel_guard->setVisible(false);
                        }
                        QString message =
                            current.value(QStringLiteral("error")).toString();
                        if (message.isEmpty()) {
                          message = QStringLiteral("OAuth failed.");
                        }
                        status_guard->setText(message);
                      }
                    });
                  });
              if (cancel_guard != nullptr) {
                QObject::connect(
                    cancel_guard, &QPushButton::clicked, cancel_guard,
                    [cancel_flow, flow_id, status_guard, timer,
                     cancel_guard]() {
                      if (!cancel_flow) {
                        return;
                      }
                      cancel_flow(flow_id, [status_guard, timer, cancel_guard](
                                               const QJsonObject&,
                                               const QString& error) {
                        if (!error.isEmpty()) {
                          if (status_guard != nullptr) {
                            status_guard->setText(error);
                          }
                          return;
                        }
                        if (timer != nullptr) {
                          timer->stop();
                        }
                        if (cancel_guard != nullptr) {
                          cancel_guard->setVisible(false);
                        }
                        if (status_guard != nullptr) {
                          status_guard->setText(
                              QStringLiteral("OAuth cancelled."));
                        }
                      });
                    });
              }
              timer->setInterval(250);
              timer->start();
            });
          });
      row.layout->addWidget(oauth);
      row.layout->addWidget(cancel);
    }

    auto* edit = Button(row.widget, QStringLiteral("Edit config"));
    edit->setObjectName(QStringLiteral("mcpEdit_") + name);
    edit->setEnabled(static_cast<bool>(get_mcp_config) &&
                     static_cast<bool>(replace_mcp_config));
    QObject::connect(
        edit, &QPushButton::clicked, edit,
        [page, name, status, get_mcp_config, replace_mcp_config,
         refresh_page]() {
          QPointer<QWidget> page_guard(page);
          QPointer<QLabel> status_guard(status);
          get_mcp_config([page_guard, status_guard, name, replace_mcp_config,
                          refresh_page](const QJsonObject& result,
                                        const QString& error) {
            if (page_guard == nullptr || status_guard == nullptr) {
              return;
            }
            if (!error.isEmpty()) {
              status_guard->setText(error);
              return;
            }
            QWidget* page = page_guard.data();
            const QJsonObject servers =
                result.value(QStringLiteral("servers")).toObject();
            const QJsonObject current = servers.value(name).toObject();
            auto* dialog = new QDialog(page);
            dialog->setObjectName(QStringLiteral("mcpEditDialog_") + name);
            auto* form = new QVBoxLayout(dialog);
            auto* intro =
                Label(dialog,
                      QStringLiteral(
                          "Raw mcp_servers entry. Saving replaces the whole "
                          "server map, so existing env/headers stay intact. "
                          "Use \"tools\" to limit the advertised tools."),
                      "fieldDescription");
            intro->setWordWrap(true);
            form->addWidget(intro);
            auto* editor = new QPlainTextEdit(dialog);
            editor->setObjectName(QStringLiteral("mcpEditJson"));
            editor->setPlainText(QString::fromUtf8(
                QJsonDocument(current).toJson(QJsonDocument::Indented)));
            form->addWidget(editor, 1);
            auto* edit_status = Label(dialog, QString(), "fieldDescription");
            edit_status->setObjectName(QStringLiteral("mcpEditStatus"));
            edit_status->setWordWrap(true);
            form->addWidget(edit_status);
            auto* buttons = new QWidget(dialog);
            auto* button_row = new QHBoxLayout(buttons);
            button_row->setContentsMargins(0, 0, 0, 0);
            auto* save = Button(buttons, QStringLiteral("Save"));
            save->setObjectName(QStringLiteral("mcpEditSubmit"));
            QObject::connect(
                save, &QPushButton::clicked, dialog,
                [dialog, editor, edit_status, name, servers, replace_mcp_config,
                 refresh_page]() {
                  QJsonParseError parse_error;
                  const QJsonDocument document = QJsonDocument::fromJson(
                      editor->toPlainText().toUtf8(), &parse_error);
                  if (parse_error.error != QJsonParseError::NoError ||
                      !document.isObject()) {
                    edit_status->setText(
                        QStringLiteral("Enter a JSON object."));
                    return;
                  }
                  QJsonObject updated = servers;
                  updated.insert(name, document.object());
                  QPointer<QLabel> edit_status_guard(edit_status);
                  QPointer<QDialog> dialog_guard(dialog);
                  replace_mcp_config(
                      updated, [edit_status_guard, dialog_guard, refresh_page](
                                   const QJsonObject&, const QString& error) {
                        if (!error.isEmpty()) {
                          if (edit_status_guard != nullptr) {
                            edit_status_guard->setText(error);
                          }
                          return;
                        }
                        if (refresh_page) {
                          refresh_page(QStringLiteral("capabilities"));
                        }
                        if (dialog_guard != nullptr) {
                          dialog_guard->accept();
                        }
                      });
                });
            auto* cancel = Button(buttons, QStringLiteral("Cancel"));
            QObject::connect(cancel, &QPushButton::clicked, dialog,
                             [dialog]() { dialog->reject(); });
            button_row->addWidget(save);
            button_row->addWidget(cancel);
            button_row->addStretch(1);
            form->addWidget(buttons);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->resize(620, 520);
            dialog->open();
          });
        });
    row.layout->addWidget(edit);

    auto* test = Button(row.widget, QStringLiteral("Test"));
    test->setObjectName(QStringLiteral("mcpTest_") + name);
    test->setEnabled(static_cast<bool>(test_server));
    QObject::connect(
        test, &QPushButton::clicked, test, [test_server, name, status]() {
          QPointer<QLabel> status_guard(status);
          test_server(name, [status_guard](const QJsonObject& result,
                                           const QString& error) {
            if (status_guard == nullptr) {
              return;
            }
            if (!error.isEmpty()) {
              status_guard->setText(error);
              return;
            }
            if (!result.value(QStringLiteral("ok")).toBool()) {
              status_guard->setText(
                  result.value(QStringLiteral("error")).toString());
              return;
            }
            const int tools =
                result.value(QStringLiteral("tools")).toArray().size();
            status_guard->setText(
                QStringLiteral("%1 tool%2")
                    .arg(tools)
                    .arg(tools == 1 ? QString() : QStringLiteral("s")));
          });
        });
    row.layout->addWidget(test);

    auto* toggle = new QCheckBox(row.widget);
    toggle->setObjectName(QStringLiteral("mcpToggle_") + name);
    toggle->setChecked(enabled);
    toggle->setEnabled(static_cast<bool>(act));
    QObject::connect(toggle, &QCheckBox::toggled, toggle, [=](bool checked) {
      act(QStringLiteral("capabilities"), QStringLiteral("PUT"),
          QStringLiteral("/api/mcp/servers/%1/enabled")
                  .arg(QString::fromLatin1(QUrl::toPercentEncoding(name))) +
              profile_query,
          QJsonObject{{QStringLiteral("enabled"), checked}});
    });
    row.layout->addWidget(toggle);

    auto* remove = Button(row.widget, QStringLiteral("Remove"));
    remove->setObjectName(QStringLiteral("mcpRemove_") + name);
    remove->setEnabled(static_cast<bool>(act));
    QObject::connect(
        remove, &QPushButton::clicked, remove, [act, name, profile_query]() {
          act(QStringLiteral("capabilities"), QStringLiteral("DELETE"),
              QStringLiteral("/api/mcp/servers/%1")
                      .arg(QString::fromLatin1(QUrl::toPercentEncoding(name))) +
                  profile_query,
              QJsonObject());
        });
    row.layout->addWidget(remove);
  }
  return page;
}

// WhatsApp pairing is a linked-device flow rather than a bot token: the
// gateway spawns a pairing process, publishes a QR payload, then flips to
// connected when the phone scans it. Keep the dialog alive across all three
// phases and stop polling as soon as it is cancelled or applied.
void ShowWhatsAppSetup(
    QWidget* page, QString mode, QString allowed_users,
    std::function<void(const QString&, const QString&, bool, PageJsonCallback)>
        start_pairing,
    std::function<void(const QString&, PageJsonCallback)> poll_pairing,
    std::function<void(const QString&, const QString&, const QString&,
                       PageJsonCallback)>
        apply_pairing,
    std::function<void(const QString&, PageJsonCallback)> cancel_pairing,
    std::function<void(const QString&)> refresh_page) {
  page->setCursor(Qt::WaitCursor);
  const QString initial_mode =
      mode.trimmed().isEmpty() ? QStringLiteral("bot") : mode.trimmed();
  const QString initial_allowed = allowed_users;
  start_pairing(
      initial_mode, allowed_users, false,
      [page = QPointer<QWidget>(page), initial_mode, initial_allowed,
       poll_pairing, apply_pairing, cancel_pairing, refresh_page,
       start_pairing](const QJsonObject& pairing, const QString& error) {
        if (page == nullptr) {
          return;
        }
        page->unsetCursor();
        QWidget* host = page.data();
        if (!error.isEmpty()) {
          QMessageBox::warning(host, QStringLiteral("WhatsApp setup"), error);
          return;
        }
        const QString pairing_id =
            pairing.value(QStringLiteral("pairing_id")).toString();
        if (pairing_id.isEmpty()) {
          QMessageBox::warning(
              host, QStringLiteral("WhatsApp setup"),
              QStringLiteral("The gateway returned an incomplete WhatsApp "
                             "setup response."));
          return;
        }

        auto* dialog = new QDialog(host);
        dialog->setObjectName(
            QStringLiteral("messagingOnboardDialog_whatsapp"));
        dialog->setWindowTitle(QStringLiteral("Set up WhatsApp"));
        auto* form = new QVBoxLayout(dialog);
        auto* intro = Label(
            dialog,
            QStringLiteral("Scan this QR with WhatsApp on your phone: Settings "
                           "→ Linked devices → Link a device. Hermes polls the "
                           "gateway until the link completes; nothing is saved "
                           "until you apply the setup."),
            "fieldDescription");
        intro->setWordWrap(true);
        intro->setMaximumWidth(520);
        form->addWidget(intro);

        auto* qr = new QLabel(dialog);
        qr->setObjectName(QStringLiteral("messagingOnboardQr_whatsapp"));
        qr->setAlignment(Qt::AlignCenter);
        qr->setMinimumSize(300, 300);
        qr->setText(QStringLiteral("Waiting for the gateway to publish a QR…"));
        form->addWidget(qr, 1, Qt::AlignHCenter);

        auto* status =
            Label(dialog, QStringLiteral("Starting WhatsApp pairing…"),
                  "fieldDescription");
        status->setObjectName(
            QStringLiteral("messagingOnboardStatus_whatsapp"));
        status->setWordWrap(true);
        status->setMaximumWidth(520);
        form->addWidget(status);

        auto* account = new QLabel(dialog);
        account->setObjectName(
            QStringLiteral("messagingOnboardAccount_whatsapp"));
        account->setProperty("role", QStringLiteral("fieldDescription"));
        account->setWordWrap(true);
        account->setMaximumWidth(520);
        form->addWidget(account);

        auto* mode_label =
            Label(dialog, QStringLiteral("WhatsApp mode"), "settingLabel");
        form->addWidget(mode_label);
        auto* mode_box = new QComboBox(dialog);
        mode_box->setObjectName(QStringLiteral("messagingOnboardMode"));
        mode_box->addItem(QStringLiteral("Bot"), QStringLiteral("bot"));
        mode_box->addItem(QStringLiteral("Self-chat (Message yourself)"),
                          QStringLiteral("self-chat"));
        const int mode_index = mode_box->findData(
            pairing.value(QStringLiteral("mode")).toString(initial_mode));
        mode_box->setCurrentIndex(mode_index < 0 ? 0 : mode_index);
        form->addWidget(mode_box);

        form->addWidget(Label(dialog, QStringLiteral("Allowed WhatsApp users"),
                              "settingLabel"));
        auto* allowed = new QLineEdit(dialog);
        allowed->setObjectName(QStringLiteral("messagingOnboardAllowedUsers"));
        allowed->setPlaceholderText(
            QStringLiteral("Comma-separated phone numbers or IDs"));
        allowed->setText(pairing.value(QStringLiteral("allowed_users"))
                             .toString(initial_allowed));
        form->addWidget(allowed);

        auto* buttons = new QWidget(dialog);
        auto* button_row = new QHBoxLayout(buttons);
        button_row->setContentsMargins(0, 0, 0, 0);
        auto* apply = Button(buttons, QStringLiteral("Apply setup"));
        apply->setObjectName(QStringLiteral("messagingOnboardApply"));
        apply->setEnabled(pairing.value(QStringLiteral("status")).toString() ==
                          QStringLiteral("connected"));
        auto* cancel = Button(buttons, QStringLiteral("Cancel"));
        cancel->setObjectName(QStringLiteral("messagingOnboardCancel"));
        button_row->addWidget(apply);
        button_row->addWidget(cancel);
        button_row->addStretch(1);
        form->addWidget(buttons);

        auto applied = std::make_shared<bool>(false);
        auto* timer = new QTimer(dialog);
        timer->setInterval(1500);
        auto polling = std::make_shared<bool>(false);
        auto qr_payload = std::make_shared<QString>();
        auto apply_state = [qr, status, account, apply, timer, mode_box,
                            allowed, applied,
                            qr_payload](const QJsonObject& state) {
          if (*applied) {
            return;
          }
          const QString state_name =
              state.value(QStringLiteral("status")).toString();
          const QString error = state.value(QStringLiteral("error")).toString();
          const QString payload =
              state.value(QStringLiteral("qr_payload")).toString();
          if (!payload.isEmpty() && payload != *qr_payload) {
            *qr_payload = payload;
            const QImage image = RenderQrImage(payload, 300);
            if (image.isNull()) {
              qr->setText(
                  QStringLiteral("Could not render the WhatsApp "
                                 "QR payload."));
            } else {
              qr->setPixmap(QPixmap::fromImage(image));
            }
          }
          if (state_name == QStringLiteral("connected")) {
            timer->stop();
            qr->setText(QStringLiteral("✓ Device linked"));
            const QString name =
                state.value(QStringLiteral("account_name")).toString();
            const QString phone =
                state.value(QStringLiteral("account_phone")).toString();
            QString who = name;
            if (!phone.isEmpty()) {
              who = who.isEmpty() ? phone
                                  : QStringLiteral("%1 (%2)").arg(name, phone);
            }
            account->setText(
                who.isEmpty()
                    ? QStringLiteral("WhatsApp is linked. Choose a mode, "
                                     "then apply the setup.")
                    : QStringLiteral("Linked as %1.").arg(who));
            if (allowed->text().trimmed().isEmpty() &&
                !state.value(QStringLiteral("allowed_users"))
                     .toString()
                     .isEmpty()) {
              allowed->setText(
                  state.value(QStringLiteral("allowed_users")).toString());
            }
            if (allowed->text().trimmed().isEmpty() &&
                state.value(QStringLiteral("mode")).toString() ==
                    QStringLiteral("self-chat")) {
              const QString self =
                  state.value(QStringLiteral("account_phone")).toString();
              if (!self.isEmpty()) {
                allowed->setText(self);
              }
            }
            if (mode_box->findData(
                    state.value(QStringLiteral("mode")).toString()) >= 0) {
              mode_box->setCurrentIndex(mode_box->findData(
                  state.value(QStringLiteral("mode")).toString()));
            }
            status->setText(QStringLiteral(
                "WhatsApp linked. Apply to save the mode and allowlist."));
            apply->setEnabled(true);
            return;
          }
          if (state_name == QStringLiteral("error") ||
              state_name == QStringLiteral("expired") ||
              state_name == QStringLiteral("cancelled")) {
            timer->stop();
            status->setText(error.isEmpty() ? QStringLiteral("WhatsApp pairing "
                                                             "stopped.")
                                            : error);
            apply->setEnabled(false);
            return;
          }
          const QString expires =
              state.value(QStringLiteral("expires_at")).toString();
          if (state_name == QStringLiteral("installing")) {
            status->setText(
                QStringLiteral("Installing WhatsApp pairing support…"));
          } else if (state_name == QStringLiteral("starting")) {
            status->setText(QStringLiteral("Starting WhatsApp pairing…"));
          } else {
            status->setText(
                expires.isEmpty()
                    ? QStringLiteral("Scan the QR with WhatsApp. Waiting…")
                    : QStringLiteral("Scan the QR with WhatsApp. Expires "
                                     "%1.")
                          .arg(expires));
          }
          apply->setEnabled(false);
        };

        const auto poll = [pairing_id, status, timer, polling, poll_pairing,
                           apply_state]() {
          if (*polling || !timer->isActive()) {
            return;
          }
          *polling = true;
          QPointer<QTimer> timer_guard(timer);
          QPointer<QLabel> status_guard(status);
          poll_pairing(
              pairing_id, [timer_guard, status_guard, polling, apply_state](
                              const QJsonObject& state, const QString& error) {
                *polling = false;
                if (timer_guard == nullptr || status_guard == nullptr) {
                  return;
                }
                if (!error.isEmpty()) {
                  timer_guard->stop();
                  status_guard->setText(error);
                  return;
                }
                apply_state(state);
              });
        };
        QObject::connect(timer, &QTimer::timeout, dialog, poll);
        QObject::connect(
            apply, &QPushButton::clicked, dialog,
            [pairing_id, mode_box, allowed, status, apply, timer, applied,
             apply_pairing, refresh_page]() {
              const QString selected_mode = mode_box->currentData().toString();
              QString users = allowed->text().trimmed();
              if (users.isEmpty() &&
                  selected_mode == QStringLiteral("self-chat")) {
                status->setText(QStringLiteral(
                    "Self-chat mode needs the linked account number."));
                return;
              }
              apply->setEnabled(false);
              QPointer<QLabel> status_guard(status);
              QPointer<QPushButton> apply_guard(apply);
              QPointer<QTimer> timer_guard(timer);
              apply_pairing(
                  pairing_id, selected_mode, users,
                  [status_guard, apply_guard, timer_guard, applied,
                   refresh_page](const QJsonObject& result,
                                 const QString& error) {
                    if (status_guard == nullptr) {
                      return;
                    }
                    if (!error.isEmpty()) {
                      status_guard->setText(error);
                      if (apply_guard != nullptr) {
                        apply_guard->setEnabled(true);
                      }
                      return;
                    }
                    *applied = true;
                    if (timer_guard != nullptr) {
                      timer_guard->stop();
                    }
                    QString message = QStringLiteral("WhatsApp setup applied.");
                    if (result.value(QStringLiteral("needs_restart"))
                            .toBool()) {
                      message += QStringLiteral(
                          " Restart the gateway to connect WhatsApp.");
                    } else if (result.value(QStringLiteral("restart_started"))
                                   .toBool()) {
                      message += QStringLiteral(
                          " The gateway is restarting to connect WhatsApp.");
                    }
                    status_guard->setText(message);
                    if (refresh_page) {
                      refresh_page(QStringLiteral("messaging"));
                    }
                  });
            });
        QObject::connect(cancel, &QPushButton::clicked, dialog,
                         [dialog]() { dialog->reject(); });
        QObject::connect(dialog, &QDialog::finished, dialog,
                         [pairing_id, cancel_pairing, applied]() {
                           if (!*applied && cancel_pairing) {
                             cancel_pairing(pairing_id, [](const QJsonObject&,
                                                           const QString&) {});
                           }
                         });

        apply_state(pairing);
        timer->start();
        QTimer::singleShot(0, dialog, poll);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->resize(620, 760);
        dialog->open();
      });
}

QWidget* MessagingPage(const PageContext& context, QWidget* parent) {
  auto* page = new QWidget(parent);
  page->setObjectName(QStringLiteral("settingsPage:messaging"));
  auto* layout = PageLayout(page);
  AddHeading(page, layout, context, QStringLiteral("messaging"),
             QStringLiteral("Messaging"));

  const PageState* state = PageStateFor(context, QStringLiteral("messaging"));
  const QJsonObject data = state == nullptr ? QJsonObject() : state->data;
  const auto act = context.action;
  const auto start_telegram = context.start_telegram_onboarding;
  const auto poll_telegram = context.poll_telegram_onboarding;
  const auto apply_telegram = context.apply_telegram_onboarding;
  const auto cancel_telegram = context.cancel_telegram_onboarding;
  const auto start_whatsapp = context.start_whatsapp_onboarding;
  const auto poll_whatsapp = context.poll_whatsapp_onboarding;
  const auto apply_whatsapp = context.apply_whatsapp_onboarding;
  const auto cancel_whatsapp = context.cancel_whatsapp_onboarding;
  const auto open_url = context.open_url;
  const auto refresh_page = context.refresh;
  const QString profile =
      context.snapshot == nullptr ? QString() : context.snapshot->name;
  const QString profile_query =
      profile.isEmpty()
          ? QString()
          : QStringLiteral("?profile=%1")
                .arg(QString::fromLatin1(QUrl::toPercentEncoding(profile)));

  const QJsonObject root = data.value(QStringLiteral("platforms")).toObject();
  const QJsonArray platforms =
      PayloadArray(root, "platforms", {"platforms", "rows"});
  layout->addWidget(
      Label(page, QStringLiteral("Platforms (%1)").arg(platforms.size()),
            "settingTitle"));
  for (const QJsonValue& value : platforms) {
    const QJsonObject platform = value.toObject();
    QString name = platform.value(QStringLiteral("label")).toString();
    if (name.isEmpty()) {
      name = platform.value(QStringLiteral("name")).toString();
    }
    const QString id =
        platform.value(QStringLiteral("id"))
            .toString(platform.value(QStringLiteral("platform_id")).toString());
    if (name.isEmpty()) {
      name = id;
    }
    const bool enabled =
        platform.value(QStringLiteral("enabled"))
            .toBool(platform.value(QStringLiteral("configured")).toBool());
    QString status = platform.value(QStringLiteral("status")).toString();
    if (status.isEmpty()) {
      status = enabled ? QStringLiteral("enabled") : QStringLiteral("disabled");
    }
    const QString state_name =
        platform.value(QStringLiteral("state")).toString();
    if (!state_name.isEmpty() && state_name != status) {
      status += QStringLiteral(" · ") + state_name;
    }
    const QJsonArray env_vars =
        platform.value(QStringLiteral("env_vars")).toArray();
    if (!env_vars.isEmpty()) {
      status +=
          QStringLiteral(" · %1 setting%2")
              .arg(env_vars.size())
              .arg(env_vars.size() == 1 ? QString() : QStringLiteral("s"));
    }
    Row row = AddRow(page, layout, name, status);
    row.widget->setObjectName(QStringLiteral("messagingRow_") + id);
    row.widget->setMaximumWidth(900);

    const auto test_platform = context.test_platform;
    if (test_platform) {
      auto* test = Button(row.widget, QStringLiteral("Test"));
      test->setObjectName(QStringLiteral("messagingTest_") + id);
      auto* test_status = new QLabel(row.widget);
      test_status->setObjectName(QStringLiteral("messagingTestStatus_") + id);
      test_status->setProperty("role", QStringLiteral("fieldDescription"));
      test_status->setWordWrap(true);
      test_status->setMaximumWidth(320);
      QObject::connect(
          test, &QPushButton::clicked, test,
          [test, test_status, id, test_platform]() {
            QPointer<QPushButton> test_guard(test);
            QPointer<QLabel> status_guard(test_status);
            if (test_guard != nullptr) {
              test_guard->setEnabled(false);
            }
            if (status_guard != nullptr) {
              status_guard->setText(QStringLiteral("Testing…"));
            }
            test_platform(
                id, [test_guard, status_guard](const QJsonObject& result,
                                               const QString& error) {
                  if (test_guard != nullptr) {
                    test_guard->setEnabled(true);
                  }
                  if (status_guard == nullptr) {
                    return;
                  }
                  if (!error.isEmpty()) {
                    status_guard->setText(error);
                    return;
                  }
                  QString message =
                      result.value(QStringLiteral("message")).toString();
                  if (message.isEmpty()) {
                    const QString state =
                        result.value(QStringLiteral("state")).toString();
                    message = state.isEmpty() ? QStringLiteral("No response.")
                                              : state;
                  }
                  const QString posture =
                      result.value(QStringLiteral("auth_posture")).toString();
                  if (!posture.isEmpty()) {
                    message += QStringLiteral(" · auth: ") + posture;
                  }
                  status_guard->setText(message);
                });
          });
      row.layout->addWidget(test);
      row.layout->addWidget(test_status);
    }

    if (id == QStringLiteral("whatsapp") && start_whatsapp) {
      auto* onboard = Button(row.widget, QStringLiteral("Set up"));
      onboard->setObjectName(QStringLiteral("messagingOnboard_whatsapp"));
      auto* onboard_status = new QLabel(row.widget);
      onboard_status->setObjectName(
          QStringLiteral("messagingOnboardStatus_whatsapp"));
      onboard_status->setProperty("role", QStringLiteral("fieldDescription"));
      onboard_status->setWordWrap(true);
      onboard_status->setMaximumWidth(320);
      QObject::connect(
          onboard, &QPushButton::clicked, onboard,
          [page, onboard, onboard_status, start_whatsapp, poll_whatsapp,
           apply_whatsapp, cancel_whatsapp, refresh_page, platform]() {
            QPointer<QPushButton> onboard_guard(onboard);
            QPointer<QLabel> onboard_status_guard(onboard_status);
            if (onboard_guard != nullptr) {
              onboard_guard->setEnabled(false);
            }
            if (onboard_status_guard != nullptr) {
              onboard_status_guard->setText(
                  QStringLiteral("Starting WhatsApp setup…"));
            }
            ShowWhatsAppSetup(
                page, platform.value(QStringLiteral("mode")).toString(),
                platform.value(QStringLiteral("allowed_users")).toString(),
                start_whatsapp, poll_whatsapp, apply_whatsapp, cancel_whatsapp,
                refresh_page);
            if (onboard_guard != nullptr) {
              onboard_guard->setEnabled(true);
            }
            if (onboard_status_guard != nullptr) {
              onboard_status_guard->clear();
            }
          });
      row.layout->addWidget(onboard);
      row.layout->addWidget(onboard_status);
    }

    if (id == QStringLiteral("telegram") && start_telegram) {
      auto* onboard = Button(row.widget, QStringLiteral("Set up"));
      onboard->setObjectName(QStringLiteral("messagingOnboard_telegram"));
      auto* onboard_status = new QLabel(row.widget);
      onboard_status->setObjectName(
          QStringLiteral("messagingOnboardStatus_telegram"));
      onboard_status->setProperty("role", QStringLiteral("fieldDescription"));
      onboard_status->setWordWrap(true);
      onboard_status->setMaximumWidth(320);
      QObject::connect(
          onboard, &QPushButton::clicked, onboard,
          [page, onboard, onboard_status, start_telegram, poll_telegram,
           apply_telegram, cancel_telegram, open_url, refresh_page]() {
            QPointer<QWidget> page_guard(page);
            QPointer<QPushButton> onboard_guard(onboard);
            QPointer<QLabel> onboard_status_guard(onboard_status);
            if (onboard_guard != nullptr) {
              onboard_guard->setEnabled(false);
            }
            if (onboard_status_guard != nullptr) {
              onboard_status_guard->setText(
                  QStringLiteral("Starting Telegram setup…"));
            }
            start_telegram(
                QStringLiteral("Hermes Agent"),
                [page_guard, onboard_guard, onboard_status_guard, poll_telegram,
                 apply_telegram, cancel_telegram, open_url, refresh_page](
                    const QJsonObject& pairing, const QString& error) {
                  if (page_guard == nullptr ||
                      onboard_status_guard == nullptr) {
                    return;
                  }
                  QWidget* page = page_guard.data();
                  if (onboard_guard != nullptr) {
                    onboard_guard->setEnabled(true);
                  }
                  if (!error.isEmpty()) {
                    onboard_status_guard->setText(error);
                    return;
                  }
                  const QString pairing_id =
                      pairing.value(QStringLiteral("pairing_id")).toString();
                  const QString deep_link =
                      pairing.value(QStringLiteral("deep_link")).toString();
                  if (pairing_id.isEmpty() || deep_link.isEmpty()) {
                    onboard_status_guard->setText(
                        QStringLiteral("Telegram setup returned an incomplete "
                                       "response."));
                    return;
                  }
                  onboard_status_guard->clear();

                  auto* dialog = new QDialog(page);
                  dialog->setObjectName(
                      QStringLiteral("messagingOnboardDialog_telegram"));
                  auto* form = new QVBoxLayout(dialog);
                  auto* intro = Label(
                      dialog,
                      QStringLiteral("Open this Telegram link on your phone, "
                                     "then confirm the bot. Hermes polls until "
                                     "Telegram returns the bot token; nothing "
                                     "is saved until you apply the setup."),
                      "fieldDescription");
                  intro->setWordWrap(true);
                  form->addWidget(intro);

                  const QString suggested =
                      pairing.value(QStringLiteral("suggested_username"))
                          .toString();
                  if (!suggested.isEmpty()) {
                    auto* username = Label(
                        dialog,
                        QStringLiteral("Suggested bot: @%1").arg(suggested),
                        "settingLabel");
                    username->setObjectName(
                        QStringLiteral("messagingOnboardSuggestedBot"));
                    form->addWidget(username);
                  }
                  auto* link = new QLabel(deep_link, dialog);
                  link->setObjectName(QStringLiteral("messagingOnboardLink"));
                  link->setTextInteractionFlags(Qt::TextSelectableByMouse);
                  link->setWordWrap(true);
                  form->addWidget(link);
                  auto* open = Button(dialog, QStringLiteral("Open Telegram"));
                  open->setObjectName(
                      QStringLiteral("messagingOnboardOpenLink"));
                  open->setEnabled(static_cast<bool>(open_url));
                  QObject::connect(
                      open, &QPushButton::clicked, open,
                      [open_url, deep_link]() { open_url(deep_link); });
                  form->addWidget(open);

                  auto* status =
                      Label(dialog, QStringLiteral("Waiting for Telegram…"),
                            "fieldDescription");
                  status->setObjectName(
                      QStringLiteral("messagingOnboardStatus_telegram"));
                  status->setWordWrap(true);
                  form->addWidget(status);

                  form->addWidget(
                      Label(dialog, QStringLiteral("Allowed Telegram user IDs"),
                            "settingLabel"));
                  auto* allowed = new QLineEdit(dialog);
                  allowed->setObjectName(
                      QStringLiteral("messagingOnboardAllowedUsers"));
                  allowed->setPlaceholderText(
                      QStringLiteral("Comma-separated numeric IDs"));
                  form->addWidget(allowed);

                  auto* buttons = new QWidget(dialog);
                  auto* button_row = new QHBoxLayout(buttons);
                  button_row->setContentsMargins(0, 0, 0, 0);
                  auto* apply = Button(buttons, QStringLiteral("Apply setup"));
                  apply->setObjectName(QStringLiteral("messagingOnboardApply"));
                  apply->setEnabled(false);
                  auto* cancel = Button(buttons, QStringLiteral("Cancel"));
                  QObject::connect(cancel, &QPushButton::clicked, dialog,
                                   [dialog]() { dialog->reject(); });
                  button_row->addWidget(apply);
                  button_row->addWidget(cancel);
                  button_row->addStretch(1);
                  form->addWidget(buttons);

                  auto applied = std::make_shared<bool>(false);
                  auto* timer = new QTimer(dialog);
                  timer->setInterval(500);
                  auto polling = std::make_shared<bool>(false);
                  auto poll = [pairing_id, status, apply, allowed, timer,
                               poll_telegram, applied, polling]() {
                    if (*applied || *polling) {
                      return;
                    }
                    *polling = true;
                    QPointer<QTimer> timer_guard(timer);
                    QPointer<QLabel> status_guard(status);
                    QPointer<QPushButton> apply_guard(apply);
                    QPointer<QLineEdit> allowed_guard(allowed);
                    poll_telegram(pairing_id, [timer_guard, status_guard,
                                               apply_guard, allowed_guard,
                                               polling](
                                                  const QJsonObject& state,
                                                  const QString& error) {
                      *polling = false;
                      if (timer_guard == nullptr || status_guard == nullptr) {
                        return;
                      }
                      if (!error.isEmpty()) {
                        timer_guard->stop();
                        status_guard->setText(error);
                        return;
                      }
                      const QString state_name =
                          state.value(QStringLiteral("status")).toString();
                      if (state_name == QStringLiteral("ready")) {
                        timer_guard->stop();
                        const QString bot_username =
                            state.value(QStringLiteral("bot_username"))
                                .toString();
                        status_guard->setText(
                            bot_username.isEmpty()
                                ? QStringLiteral(
                                      "Bot is ready. Add the allowed "
                                      "user IDs, then apply.")
                                : QStringLiteral(
                                      "@%1 is ready. Add the allowed "
                                      "user IDs, then apply.")
                                      .arg(bot_username));
                        const QString owner =
                            state.value(QStringLiteral("owner_user_id"))
                                .toString();
                        if (allowed_guard != nullptr &&
                            allowed_guard->text().trimmed().isEmpty() &&
                            !owner.isEmpty()) {
                          allowed_guard->setText(owner);
                        }
                        if (apply_guard != nullptr) {
                          apply_guard->setEnabled(true);
                        }
                        return;
                      }
                      if (state_name == QStringLiteral("waiting")) {
                        const QString expires_at =
                            state.value(QStringLiteral("expires_at"))
                                .toString();
                        status_guard->setText(
                            expires_at.isEmpty()
                                ? QStringLiteral("Waiting for Telegram…")
                                : QStringLiteral("Waiting for Telegram… "
                                                 "expires %1")
                                      .arg(expires_at));
                        return;
                      }
                      if (state_name == QStringLiteral("error")) {
                        timer_guard->stop();
                        status_guard->setText(
                            state.value(QStringLiteral("error"))
                                .toString(
                                    QStringLiteral("Telegram setup failed.")));
                        return;
                      }
                      status_guard->setText(
                          QStringLiteral("Waiting for Telegram…"));
                    });
                  };
                  QObject::connect(timer, &QTimer::timeout, dialog, poll);
                  QObject::connect(
                      apply, &QPushButton::clicked, dialog,
                      [pairing_id, allowed, status, apply, timer, applied,
                       apply_telegram, refresh_page]() {
                        QStringList ids;
                        for (const QString& id : allowed->text().split(
                                 QRegularExpression(QStringLiteral("[,\\s]+")),
                                 Qt::SkipEmptyParts)) {
                          ids.append(id);
                        }
                        if (ids.isEmpty()) {
                          status->setText(QStringLiteral(
                              "Add at least one allowed user ID."));
                          return;
                        }
                        apply->setEnabled(false);
                        QPointer<QLabel> status_guard(status);
                        QPointer<QPushButton> apply_guard(apply);
                        QPointer<QTimer> timer_guard(timer);
                        apply_telegram(
                            pairing_id, ids,
                            [status_guard, apply_guard, timer_guard, applied,
                             refresh_page](const QJsonObject& result,
                                           const QString& error) {
                              if (status_guard == nullptr) {
                                return;
                              }
                              if (!error.isEmpty()) {
                                status_guard->setText(error);
                                if (apply_guard != nullptr) {
                                  apply_guard->setEnabled(true);
                                }
                                return;
                              }
                              *applied = true;
                              if (timer_guard != nullptr) {
                                timer_guard->stop();
                              }
                              const QString username =
                                  result.value(QStringLiteral("bot_username"))
                                      .toString();
                              QString message =
                                  username.isEmpty()
                                      ? QStringLiteral(
                                            "Telegram setup applied.")
                                      : QStringLiteral(
                                            "Telegram setup applied for @%1.")
                                            .arg(username);
                              if (result.value(QStringLiteral("needs_restart"))
                                      .toBool()) {
                                message += QStringLiteral(
                                    " Restart the gateway to connect.");
                              }
                              status_guard->setText(message);
                              if (refresh_page) {
                                refresh_page(QStringLiteral("messaging"));
                              }
                            });
                      });
                  QObject::connect(dialog, &QDialog::finished, dialog,
                                   [pairing_id, cancel_telegram, applied]() {
                                     if (!*applied && cancel_telegram) {
                                       cancel_telegram(pairing_id,
                                                       [](const QJsonObject&,
                                                          const QString&) {});
                                     }
                                   });
                  timer->start();
                  QTimer::singleShot(0, dialog, poll);
                  dialog->setAttribute(Qt::WA_DeleteOnClose);
                  dialog->resize(620, 480);
                  dialog->open();
                });
          });
      row.layout->addWidget(onboard);
      row.layout->addWidget(onboard_status);
    }

    if (!env_vars.isEmpty()) {
      auto* configure = Button(row.widget, QStringLiteral("Configure"));
      configure->setObjectName(QStringLiteral("messagingConfigure_") + id);
      configure->setEnabled(static_cast<bool>(act));
      QObject::connect(
          configure, &QPushButton::clicked, configure,
          [page, act, id, name, env_vars, profile_query]() {
            auto* dialog = new QDialog(page);
            dialog->setObjectName(QStringLiteral("messagingConfigDialog_") +
                                  id);
            auto* form = new QVBoxLayout(dialog);
            auto* intro = Label(
                dialog,
                QStringLiteral("%1 credentials are written to this profile's "
                               ".env. Existing secrets are never echoed back; "
                               "leave a field blank to keep it.")
                    .arg(name),
                "fieldDescription");
            intro->setWordWrap(true);
            form->addWidget(intro);

            struct Field {
              QString key;
              QLineEdit* input = nullptr;
              QCheckBox* clear = nullptr;
            };
            std::vector<Field> fields;
            for (const QJsonValue& env_value : env_vars) {
              const QJsonObject env = env_value.toObject();
              const QString key = env.value(QStringLiteral("key")).toString();
              if (key.isEmpty()) {
                continue;
              }
              const QString prompt =
                  env.value(QStringLiteral("prompt")).toString(key);
              const bool required =
                  env.value(QStringLiteral("required")).toBool();
              const bool is_set = env.value(QStringLiteral("is_set")).toBool();
              auto* label = Label(
                  dialog, required ? prompt + QStringLiteral(" *") : prompt,
                  "settingLabel");
              form->addWidget(label);
              auto* input = new QLineEdit(dialog);
              input->setObjectName(
                  QStringLiteral("messagingEnv_%1_%2").arg(id, key));
              input->setEchoMode(
                  env.value(QStringLiteral("is_password")).toBool()
                      ? QLineEdit::Password
                      : QLineEdit::Normal);
              const QString redacted =
                  env.value(QStringLiteral("redacted_value")).toString();
              input->setPlaceholderText(
                  is_set ? (redacted.isEmpty() ? QStringLiteral("already set")
                                               : redacted)
                         : QStringLiteral("not set"));
              form->addWidget(input);
              const QString help = env.value(QStringLiteral("help")).toString();
              if (!help.isEmpty()) {
                auto* hint = Description(dialog, help);
                hint->setWordWrap(true);
                form->addWidget(hint);
              }
              auto* clear =
                  new QCheckBox(QStringLiteral("Clear saved value"), dialog);
              clear->setObjectName(
                  QStringLiteral("messagingClear_%1_%2").arg(id, key));
              clear->setVisible(is_set);
              QObject::connect(
                  clear, &QCheckBox::toggled, input,
                  [input](bool checked) { input->setEnabled(!checked); });
              form->addWidget(clear);
              fields.push_back(Field{key, input, clear});
            }

            auto* buttons = new QWidget(dialog);
            auto* button_row = new QHBoxLayout(buttons);
            button_row->setContentsMargins(0, 0, 0, 0);
            auto* save = Button(buttons, QStringLiteral("Save"));
            save->setObjectName(QStringLiteral("messagingConfigSubmit"));
            QObject::connect(
                save, &QPushButton::clicked, dialog,
                [act, dialog, id, fields, profile_query]() {
                  QJsonObject env;
                  QJsonArray clear_env;
                  for (const Field& field : fields) {
                    if (field.clear->isChecked()) {
                      clear_env.append(field.key);
                    } else {
                      const QString value = field.input->text().trimmed();
                      if (!value.isEmpty()) {
                        env.insert(field.key, value);
                      }
                    }
                  }
                  if (env.isEmpty() && clear_env.isEmpty()) {
                    dialog->accept();
                    return;
                  }
                  act(QStringLiteral("messaging"), QStringLiteral("PUT"),
                      QStringLiteral("/api/messaging/platforms/%1").arg(id) +
                          profile_query,
                      QJsonObject{{QStringLiteral("env"), env},
                                  {QStringLiteral("clear_env"), clear_env}});
                  dialog->accept();
                });
            auto* cancel = Button(buttons, QStringLiteral("Cancel"));
            QObject::connect(cancel, &QPushButton::clicked, dialog,
                             [dialog]() { dialog->reject(); });
            button_row->addWidget(save);
            button_row->addWidget(cancel);
            button_row->addStretch(1);
            form->addWidget(buttons);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->resize(560, 620);
            dialog->open();
          });
      row.layout->addWidget(configure);
    }

    auto* toggle = new QCheckBox(row.widget);
    toggle->setObjectName(QStringLiteral("messagingToggle_") + id);
    toggle->setChecked(enabled);
    toggle->setEnabled(static_cast<bool>(act) && !id.isEmpty());
    QObject::connect(toggle, &QCheckBox::toggled, toggle, [=](bool checked) {
      act(QStringLiteral("messaging"), QStringLiteral("PUT"),
          QStringLiteral("/api/messaging/platforms/%1").arg(id) + profile_query,
          QJsonObject{{QStringLiteral("enabled"), checked}});
    });
    row.layout->addWidget(toggle);
  }
  if (platforms.isEmpty()) {
    layout->addWidget(Description(
        page, QStringLiteral("The gateway returned no messaging platforms.")));
  }
  return page;
}

QWidget* ProfilesPage(const PageContext& context, QWidget* parent) {
  auto* page = new QWidget(parent);
  page->setObjectName(QStringLiteral("settingsPage:profiles"));
  auto* layout = PageLayout(page);
  AddHeading(page, layout, context, QStringLiteral("profiles"),
             QStringLiteral("Bots"));

  const PageState* state = PageStateFor(context, QStringLiteral("profiles"));
  const QJsonObject data = state == nullptr ? QJsonObject() : state->data;
  const QJsonArray profiles =
      PayloadArray(data, "profiles", {"profiles", "rows"});

  auto* actions = new QWidget(page);
  auto* action_row = new QHBoxLayout(actions);
  action_row->setContentsMargins(0, 0, 0, 0);
  auto* add = Button(actions, QStringLiteral("New bot"));
  add->setObjectName(QStringLiteral("profilesAdd"));
  const auto add_profile = context.add_profile;
  QObject::connect(add, &QPushButton::clicked, add, [parent, add_profile]() {
    if (!add_profile) {
      return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(
        parent, QStringLiteral("New bot"),
        QStringLiteral("Profile name (letters, digits, dashes):"),
        QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) {
      return;
    }
    add_profile(name.trimmed(), [](const QJsonObject&, const QString& error) {
      if (!error.isEmpty()) {
        qWarning("create profile failed: %s", qUtf8Printable(error));
      }
    });
  });
  action_row->addWidget(add);

  auto* import = Button(actions, QStringLiteral("Import bot"));
  import->setObjectName(QStringLiteral("profilesImport"));
  const auto import_profile = context.import_profile;
  import->setEnabled(static_cast<bool>(import_profile));
  QObject::connect(
      import, &QPushButton::clicked, import, [parent, import_profile]() {
        if (!import_profile) {
          return;
        }
        const QString archive = QFileDialog::getOpenFileName(
            parent, QStringLiteral("Import bot"), QString(),
            QStringLiteral("Profile archives (*.tar.gz *.tgz)"));
        if (archive.isEmpty()) {
          return;
        }
        QPointer<QWidget> parent_guard(parent);
        import_profile(
            archive, [parent_guard](const QJsonObject&, const QString& error) {
              if (!error.isEmpty() && parent_guard) {
                QMessageBox::warning(parent_guard,
                                     QStringLiteral("Import failed"), error);
              }
            });
      });
  action_row->addWidget(import);
  action_row->addStretch(1);
  layout->addWidget(actions);

  const QString active =
      context.snapshot == nullptr ? QString() : context.snapshot->name;
  const std::vector<ModelProvider> catalog =
      context.snapshot == nullptr ? std::vector<ModelProvider>()
                                  : context.snapshot->model_providers;
  for (const QJsonValue& value : profiles) {
    const QJsonObject profile = value.toObject();
    QString name = profile.value(QStringLiteral("name")).toString();
    if (name.isEmpty()) {
      name = profile.value(QStringLiteral("label")).toString();
    }
    const QString encoded_name =
        QString::fromLatin1(QUrl::toPercentEncoding(name));
    const QString description =
        profile.value(QStringLiteral("description")).toString();
    const QString model = profile.value(QStringLiteral("model")).toString();
    const QString provider =
        profile.value(QStringLiteral("provider")).toString();
    const bool gateway_running =
        profile.value(QStringLiteral("gateway_running")).toBool();
    QStringList detail_parts;
    if (!description.isEmpty()) {
      detail_parts.append(description);
    }
    if (!model.isEmpty()) {
      detail_parts.append(provider.isEmpty()
                              ? model
                              : QStringLiteral("%1 · %2").arg(provider, model));
    }
    detail_parts.append(gateway_running ? QStringLiteral("gateway running")
                                        : QStringLiteral("gateway stopped"));
    Row row =
        AddRow(page, layout, name, detail_parts.join(QStringLiteral(" · ")));
    row.widget->setObjectName(QStringLiteral("profileRow_") + name);

    const auto activate = context.activate_profile;
    auto* open = Button(row.widget, name == active ? QStringLiteral("Active")
                                                   : QStringLiteral("Open"));
    open->setObjectName(QStringLiteral("profileOpen_") + name);
    open->setEnabled(name != active && static_cast<bool>(activate));
    QObject::connect(open, &QPushButton::clicked, open, [activate, name]() {
      if (activate) {
        activate(name);
      }
    });
    row.layout->addWidget(open);

    const auto act = context.action;
    auto* edit_model = Button(row.widget, QStringLiteral("Model"));
    edit_model->setObjectName(QStringLiteral("profileModel_") + name);
    edit_model->setEnabled(static_cast<bool>(act));
    QObject::connect(
        edit_model, &QPushButton::clicked, edit_model,
        [page, act, name, encoded_name, provider, model, catalog]() {
          auto* dialog = new QDialog(page);
          dialog->setObjectName(QStringLiteral("botModelDialog_") + name);
          auto* form = new QVBoxLayout(dialog);
          form->addWidget(new QLabel(QStringLiteral("Provider"), dialog));
          auto* provider_box = new QComboBox(dialog);
          provider_box->setObjectName(QStringLiteral("botModelProvider"));
          provider_box->setEditable(true);
          for (const ModelProvider& entry : catalog) {
            provider_box->addItem(entry.slug, entry.slug);
          }
          int provider_index = provider_box->findData(provider);
          if (provider_index < 0 && !provider.isEmpty()) {
            provider_box->addItem(provider, provider);
            provider_index = provider_box->count() - 1;
          }
          if (provider_index >= 0) {
            provider_box->setCurrentIndex(provider_index);
          } else {
            provider_box->setCurrentText(provider);
          }
          form->addWidget(provider_box);

          form->addWidget(new QLabel(QStringLiteral("Model"), dialog));
          auto* model_box = new QComboBox(dialog);
          model_box->setObjectName(QStringLiteral("botModelValue"));
          model_box->setEditable(true);
          const auto fill_models = [model_box, catalog](const QString& slug) {
            const QString current = model_box->currentText();
            model_box->clear();
            for (const ModelProvider& entry : catalog) {
              if (entry.slug == slug) {
                model_box->addItems(entry.models);
                break;
              }
            }
            if (!current.isEmpty() && model_box->findText(current) < 0) {
              model_box->addItem(current);
            }
            model_box->setCurrentText(current);
          };
          fill_models(provider_box->currentText());
          model_box->setCurrentText(model);
          QObject::connect(provider_box, &QComboBox::currentTextChanged,
                           model_box, fill_models);
          form->addWidget(model_box);

          auto* buttons = new QWidget(dialog);
          auto* button_row = new QHBoxLayout(buttons);
          button_row->setContentsMargins(0, 0, 0, 0);
          auto* save = Button(buttons, QStringLiteral("Save"));
          save->setObjectName(QStringLiteral("botModelSubmit"));
          QObject::connect(save, &QPushButton::clicked, dialog, [=]() {
            const QString selected_provider =
                provider_box->currentText().trimmed();
            const QString selected_model = model_box->currentText().trimmed();
            if (selected_provider.isEmpty() || selected_model.isEmpty()) {
              return;
            }
            act(QStringLiteral("profiles"), QStringLiteral("PUT"),
                QStringLiteral("/api/profiles/%1/model").arg(encoded_name),
                QJsonObject{{QStringLiteral("provider"), selected_provider},
                            {QStringLiteral("model"), selected_model}});
            dialog->accept();
          });
          auto* cancel = Button(buttons, QStringLiteral("Cancel"));
          QObject::connect(cancel, &QPushButton::clicked, dialog,
                           [dialog]() { dialog->reject(); });
          button_row->addWidget(save);
          button_row->addWidget(cancel);
          button_row->addStretch(1);
          form->addWidget(buttons);
          dialog->setAttribute(Qt::WA_DeleteOnClose);
          dialog->resize(420, 220);
          dialog->open();
        });
    row.layout->addWidget(edit_model);

    auto* edit_description = Button(row.widget, QStringLiteral("Description"));
    edit_description->setObjectName(QStringLiteral("profileDescription_") +
                                    name);
    edit_description->setEnabled(static_cast<bool>(act));
    QObject::connect(
        edit_description, &QPushButton::clicked, edit_description,
        [page, act, name, encoded_name, description]() {
          auto* dialog = new QDialog(page);
          dialog->setObjectName(QStringLiteral("botDescriptionDialog_") + name);
          auto* form = new QVBoxLayout(dialog);
          auto* text = new QPlainTextEdit(description, dialog);
          text->setObjectName(QStringLiteral("botDescriptionText"));
          text->setPlaceholderText(QStringLiteral("What does this bot do?"));
          form->addWidget(text);
          auto* buttons = new QWidget(dialog);
          auto* button_row = new QHBoxLayout(buttons);
          button_row->setContentsMargins(0, 0, 0, 0);
          auto* save = Button(buttons, QStringLiteral("Save"));
          save->setObjectName(QStringLiteral("botDescriptionSubmit"));
          QObject::connect(save, &QPushButton::clicked, dialog, [=]() {
            act(QStringLiteral("profiles"), QStringLiteral("PUT"),
                QStringLiteral("/api/profiles/%1/description")
                    .arg(encoded_name),
                QJsonObject{
                    {QStringLiteral("description"), text->toPlainText()}});
            dialog->accept();
          });
          auto* cancel = Button(buttons, QStringLiteral("Cancel"));
          QObject::connect(cancel, &QPushButton::clicked, dialog,
                           [dialog]() { dialog->reject(); });
          button_row->addWidget(save);
          button_row->addWidget(cancel);
          button_row->addStretch(1);
          form->addWidget(buttons);
          dialog->setAttribute(Qt::WA_DeleteOnClose);
          dialog->resize(460, 260);
          dialog->open();
        });
    row.layout->addWidget(edit_description);

    const auto get_soul = context.get_profile_soul;
    const auto set_soul = context.set_profile_soul;
    auto* edit_soul = Button(row.widget, QStringLiteral("SOUL"));
    edit_soul->setObjectName(QStringLiteral("profileSoul_") + name);
    edit_soul->setEnabled(static_cast<bool>(get_soul) &&
                          static_cast<bool>(set_soul));
    QObject::connect(
        edit_soul, &QPushButton::clicked, edit_soul,
        [page, name, get_soul, set_soul]() {
          QPointer<QWidget> page_guard(page);
          get_soul(name, [page_guard, set_soul, name](const QJsonObject& result,
                                                      const QString& error) {
            if (page_guard == nullptr) {
              return;
            }
            QWidget* page = page_guard.data();
            if (!error.isEmpty()) {
              QMessageBox::warning(page, QStringLiteral("SOUL.md"), error);
              return;
            }
            const QString content =
                result.value(QStringLiteral("content")).toString();
            auto* dialog = new QDialog(page);
            dialog->setObjectName(QStringLiteral("botSoulDialog_") + name);
            auto* form = new QVBoxLayout(dialog);
            auto* text = new QPlainTextEdit(content, dialog);
            text->setObjectName(QStringLiteral("botSoulText"));
            text->setPlaceholderText(QStringLiteral("This bot's SOUL.md"));
            form->addWidget(text);
            auto* buttons = new QWidget(dialog);
            auto* button_row = new QHBoxLayout(buttons);
            button_row->setContentsMargins(0, 0, 0, 0);
            auto* save = Button(buttons, QStringLiteral("Save"));
            save->setObjectName(QStringLiteral("botSoulSubmit"));
            QObject::connect(save, &QPushButton::clicked, dialog, [=]() {
              QPointer<QDialog> dialog_guard(dialog);
              QPointer<QPlainTextEdit> text_guard(text);
              set_soul(name, text->toPlainText(),
                       [dialog_guard, text_guard](const QJsonObject&,
                                                  const QString& save_error) {
                         if (dialog_guard == nullptr) {
                           return;
                         }
                         if (!save_error.isEmpty()) {
                           QMessageBox::warning(dialog_guard,
                                                QStringLiteral("Save failed"),
                                                save_error);
                           return;
                         }
                         dialog_guard->accept();
                       });
            });
            auto* cancel = Button(buttons, QStringLiteral("Cancel"));
            QObject::connect(cancel, &QPushButton::clicked, dialog,
                             [dialog]() { dialog->reject(); });
            button_row->addWidget(save);
            button_row->addWidget(cancel);
            button_row->addStretch(1);
            form->addWidget(buttons);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->resize(560, 420);
            dialog->open();
          });
        });
    row.layout->addWidget(edit_soul);

    const auto export_profile = context.export_profile;
    auto* export_bot = Button(row.widget, QStringLiteral("Export"));
    export_bot->setObjectName(QStringLiteral("profileExport_") + name);
    export_bot->setEnabled(static_cast<bool>(export_profile));
    QObject::connect(
        export_bot, &QPushButton::clicked, export_bot,
        [page, name, export_profile]() {
          QPointer<QWidget> page_guard(page);
          export_profile(name, [page_guard, name](const QJsonObject& result,
                                                  const QString& error) {
            if (!error.isEmpty()) {
              if (page_guard != nullptr) {
                QMessageBox::warning(page_guard,
                                     QStringLiteral("Export failed"), error);
              }
              return;
            }
            const QString archive =
                result.value(QStringLiteral("archive")).toString();
            qInfo("profile %s exported to %s", qUtf8Printable(name),
                  qUtf8Printable(archive));
          });
        });
    row.layout->addWidget(export_bot);

    const auto remove_profile = context.remove_profile;
    auto* remove = Button(row.widget, QStringLiteral("Delete"));
    remove->setObjectName(QStringLiteral("profileDelete_") + name);
    remove->setEnabled(name != QStringLiteral("default") &&
                       static_cast<bool>(remove_profile));
    QObject::connect(
        remove, &QPushButton::clicked, remove,
        [parent, remove_profile, name]() {
          if (!remove_profile) {
            return;
          }
          const auto answer = QMessageBox::question(
              parent, QStringLiteral("Delete bot"),
              QStringLiteral("Delete profile \"%1\" and its "
                             "sessions? This cannot be undone.")
                  .arg(name));
          if (answer != QMessageBox::Yes) {
            return;
          }
          QPointer<QWidget> parent_guard(parent);
          remove_profile(
              name, [parent_guard](const QJsonObject&, const QString& error) {
                if (!error.isEmpty() && parent_guard) {
                  QMessageBox::warning(parent_guard,
                                       QStringLiteral("Delete failed"), error);
                }
              });
        });
    row.layout->addWidget(remove);
  }
  if (profiles.isEmpty()) {
    layout->addWidget(Description(
        page, QStringLiteral("No profiles reported by the gateway.")));
  }
  return page;
}

QWidget* WebhooksPage(const PageContext& context, QWidget* parent) {
  auto* page = new QWidget(parent);
  page->setObjectName(QStringLiteral("settingsPage:webhooks"));
  auto* layout = PageLayout(page);
  AddHeading(page, layout, context, QStringLiteral("webhooks"),
             QStringLiteral("Webhooks"));

  const PageState* state = PageStateFor(context, QStringLiteral("webhooks"));
  const QJsonObject data = state == nullptr ? QJsonObject() : state->data;
  const QJsonObject webhooks =
      data.value(QStringLiteral("webhooks")).toObject();
  const bool enabled = webhooks.value(QStringLiteral("enabled")).toBool();
  const QString base_url =
      webhooks.value(QStringLiteral("base_url")).toString();
  const QJsonArray subscriptions =
      PayloadArray(webhooks, "subscriptions", {"subscriptions"});
  const QString profile =
      context.snapshot == nullptr ? QString() : context.snapshot->name;
  const QString suffix =
      profile.isEmpty()
          ? QString()
          : QStringLiteral("?profile=%1")
                .arg(QString::fromLatin1(QUrl::toPercentEncoding(profile)));
  const auto act = context.action;

  if (!enabled) {
    layout->addWidget(Description(
        page, QStringLiteral("The webhook platform is disabled. Incoming "
                             "events are rejected until it is enabled.")));
    auto* enable = Button(page, QStringLiteral("Enable webhooks"));
    enable->setObjectName(QStringLiteral("webhooksEnable"));
    enable->setEnabled(static_cast<bool>(act));
    QObject::connect(enable, &QPushButton::clicked, enable, [act, suffix]() {
      act(QStringLiteral("webhooks"), QStringLiteral("POST"),
          QStringLiteral("/api/webhooks/enable") + suffix, QJsonObject());
    });
    layout->addWidget(enable, 0, Qt::AlignLeft);
  } else {
    layout->addWidget(
        Description(page, base_url.isEmpty()
                              ? QStringLiteral("Webhooks are enabled.")
                              : QStringLiteral("Base URL: %1").arg(base_url)));

    auto* create = Button(page, QStringLiteral("New subscription"));
    create->setObjectName(QStringLiteral("webhooksCreate"));
    create->setEnabled(static_cast<bool>(act));
    QObject::connect(
        create, &QPushButton::clicked, create, [page, act, suffix]() {
          auto* dialog = new QDialog(page);
          dialog->setObjectName(QStringLiteral("webhookDialog"));
          auto* form = new QVBoxLayout(dialog);
          form->addWidget(new QLabel(QStringLiteral("Name"), dialog));
          auto* name = new QLineEdit(dialog);
          name->setObjectName(QStringLiteral("webhookName"));
          name->setPlaceholderText(QStringLiteral("lowercase-name"));
          form->addWidget(name);
          form->addWidget(new QLabel(QStringLiteral("Events"), dialog));
          auto* events = new QLineEdit(QStringLiteral("message"), dialog);
          events->setObjectName(QStringLiteral("webhookEvents"));
          events->setPlaceholderText(QStringLiteral("message, notification"));
          form->addWidget(events);
          form->addWidget(new QLabel(QStringLiteral("Prompt"), dialog));
          auto* prompt = new QPlainTextEdit(dialog);
          prompt->setObjectName(QStringLiteral("webhookPrompt"));
          prompt->setPlaceholderText(
              QStringLiteral("Optional prompt the event runs."));
          prompt->setMaximumHeight(120);
          form->addWidget(prompt);
          form->addWidget(new QLabel(QStringLiteral("Deliver to"), dialog));
          auto* deliver = new QLineEdit(QStringLiteral("log"), dialog);
          deliver->setObjectName(QStringLiteral("webhookDeliver"));
          form->addWidget(deliver);
          auto* buttons = new QWidget(dialog);
          auto* button_row = new QHBoxLayout(buttons);
          button_row->setContentsMargins(0, 0, 0, 0);
          auto* save = Button(buttons, QStringLiteral("Create"));
          save->setObjectName(QStringLiteral("webhookSubmit"));
          QObject::connect(save, &QPushButton::clicked, dialog, [=]() {
            const QString webhook_name = name->text().trimmed();
            if (webhook_name.isEmpty()) {
              return;
            }
            QJsonArray event_list;
            for (const QString& event :
                 events->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
              if (!event.trimmed().isEmpty()) {
                event_list.append(event.trimmed());
              }
            }
            act(QStringLiteral("webhooks"), QStringLiteral("POST"),
                QStringLiteral("/api/webhooks") + suffix,
                QJsonObject{
                    {QStringLiteral("name"), webhook_name},
                    {QStringLiteral("events"), event_list},
                    {QStringLiteral("prompt"), prompt->toPlainText()},
                    {QStringLiteral("deliver"), deliver->text().trimmed()}});
            dialog->accept();
          });
          auto* cancel = Button(buttons, QStringLiteral("Cancel"));
          QObject::connect(cancel, &QPushButton::clicked, dialog,
                           [dialog]() { dialog->reject(); });
          button_row->addWidget(save);
          button_row->addWidget(cancel);
          button_row->addStretch(1);
          form->addWidget(buttons);
          dialog->setAttribute(Qt::WA_DeleteOnClose);
          dialog->resize(440, 360);
          dialog->open();
        });
    layout->addWidget(create, 0, Qt::AlignLeft);
  }

  for (const QJsonValue& value : subscriptions) {
    const QJsonObject subscription = value.toObject();
    const QString name = subscription.value(QStringLiteral("name")).toString();
    if (name.isEmpty()) {
      continue;
    }
    const QString encoded = QString::fromLatin1(QUrl::toPercentEncoding(name));
    const QString url = subscription.value(QStringLiteral("url")).toString();
    const QString deliver =
        subscription.value(QStringLiteral("deliver")).toString();
    const QStringList events =
        Text(subscription.value(QStringLiteral("events")))
            .split(QStringLiteral(", "), Qt::SkipEmptyParts);
    QStringList detail;
    if (!events.isEmpty()) {
      detail.append(events.join(QStringLiteral(", ")));
    }
    if (!deliver.isEmpty()) {
      detail.append(QStringLiteral("→ %1").arg(deliver));
    }
    if (!url.isEmpty()) {
      detail.append(url);
    }
    if (subscription.value(QStringLiteral("secret_set")).toBool()) {
      detail.append(QStringLiteral("secret set"));
    }
    Row row = AddRow(page, layout, name, detail.join(QStringLiteral(" · ")));
    row.widget->setObjectName(QStringLiteral("webhookRow_") + name);

    auto* toggle = new QCheckBox(QStringLiteral("Enabled"), row.widget);
    toggle->setObjectName(QStringLiteral("webhookEnabled_") + name);
    toggle->setChecked(subscription.value(QStringLiteral("enabled")).toBool());
    toggle->setEnabled(static_cast<bool>(act));
    QObject::connect(
        toggle, &QCheckBox::toggled, toggle,
        [act, encoded, suffix](bool checked) {
          act(QStringLiteral("webhooks"), QStringLiteral("PUT"),
              QStringLiteral("/api/webhooks/%1/enabled").arg(encoded) + suffix,
              QJsonObject{{QStringLiteral("enabled"), checked}});
        });
    row.layout->addWidget(toggle);

    auto* remove = Button(row.widget, QStringLiteral("Delete"));
    remove->setObjectName(QStringLiteral("webhookDelete_") + name);
    remove->setEnabled(static_cast<bool>(act));
    QObject::connect(
        remove, &QPushButton::clicked, remove,
        [page, act, encoded, suffix, name]() {
          const auto answer = QMessageBox::question(
              page, QStringLiteral("Delete webhook"),
              QStringLiteral("Delete subscription \"%1\"?").arg(name));
          if (answer != QMessageBox::Yes) {
            return;
          }
          act(QStringLiteral("webhooks"), QStringLiteral("DELETE"),
              QStringLiteral("/api/webhooks/%1").arg(encoded) + suffix,
              QJsonObject());
        });
    row.layout->addWidget(remove);
  }
  if (subscriptions.isEmpty()) {
    layout->addWidget(Description(
        page, enabled ? QStringLiteral("No webhook subscriptions yet.")
                      : QStringLiteral("Enable the platform to add one.")));
  }
  return page;
}

QWidget* ArtifactsPage(const PageContext& context, QWidget* parent) {
  auto* page = new QWidget(parent);
  page->setObjectName(QStringLiteral("settingsPage:artifacts"));
  auto* layout = PageLayout(page);
  AddHeading(page, layout, context, QStringLiteral("artifacts"),
             QStringLiteral("Artifacts"));

  const PageState* state = PageStateFor(context, QStringLiteral("artifacts"));
  const QJsonObject data = state == nullptr ? QJsonObject() : state->data;
  const QString cwd =
      context.snapshot == nullptr ? QString() : context.snapshot->artifacts_cwd;
  const QString selected = context.snapshot == nullptr
                               ? QString()
                               : context.snapshot->artifacts_preview;

  const QJsonObject git = data.value(QStringLiteral("git")).toObject();
  const QString git_root = git.value(QStringLiteral("root")).toString();
  if (!git_root.isEmpty()) {
    AddRow(page, layout, QStringLiteral("Git repository"), git_root);
  }

  auto* path_row = new QWidget(page);
  auto* path_layout = new QHBoxLayout(path_row);
  path_layout->setContentsMargins(0, 0, 0, 0);
  auto* path = new QLineEdit(cwd, path_row);
  path->setObjectName(QStringLiteral("artifactsPath"));
  path_layout->addWidget(path, 1);
  const auto open_dir = context.open_dir;
  auto* browse = Button(path_row, QStringLiteral("Browse…"));
  browse->setObjectName(QStringLiteral("artifactsBrowse"));
  QObject::connect(
      browse, &QPushButton::clicked, browse, [page, path, open_dir, cwd]() {
        // A snapshot may rebuild this page while the modal dialog is running.
        // Parent the dialog to the window (which outlives the page) and keep
        // weak guards for the widgets used after the nested event loop.
        QPointer<QWidget> page_guard(page);
        QPointer<QLineEdit> path_guard(path);
        QWidget* dialog_parent =
            page_guard == nullptr ? nullptr : page_guard->window();
        QString start = path_guard == nullptr ? cwd : path_guard->text();
        if (start.isEmpty() || !QDir(start).exists()) {
          start = cwd;
        }
        if (start.isEmpty() || !QDir(start).exists()) {
          start = QDir::homePath();
        }
        const QString chosen = QFileDialog::getExistingDirectory(
            dialog_parent, QStringLiteral("Choose folder"), start);
        if (chosen.isEmpty() || path_guard == nullptr) {
          return;
        }
        path_guard->setText(chosen);
        if (open_dir) {
          open_dir(chosen);
        }
      });
  path_layout->addWidget(browse);
  auto* go = Button(path_row, QStringLiteral("Go"));
  go->setObjectName(QStringLiteral("artifactsGo"));
  QObject::connect(go, &QPushButton::clicked, go, [path, open_dir]() {
    if (open_dir && !path->text().trimmed().isEmpty()) {
      open_dir(path->text().trimmed());
    }
  });
  path_layout->addWidget(go);
  layout->addWidget(path_row);

  auto* tools = new QWidget(page);
  auto* tools_row = new QHBoxLayout(tools);
  tools_row->setContentsMargins(0, 0, 0, 0);
  const auto act = context.action;
  auto* new_folder = Button(tools, QStringLiteral("New folder"));
  new_folder->setObjectName(QStringLiteral("artifactsNewFolder"));
  new_folder->setEnabled(static_cast<bool>(act) && !cwd.isEmpty());
  QObject::connect(
      new_folder, &QPushButton::clicked, new_folder, [page, act, cwd]() {
        QPointer<QWidget> page_guard(page);
        QWidget* dialog_parent =
            page_guard == nullptr ? nullptr : page_guard->window();
        bool ok = false;
        const QString name = QInputDialog::getText(
            dialog_parent, QStringLiteral("New folder"),
            QStringLiteral("Folder name:"), QLineEdit::Normal, QString(), &ok);
        if (!ok || name.trimmed().isEmpty() || act == nullptr) {
          return;
        }
        act(QStringLiteral("artifacts"), QStringLiteral("POST"),
            QStringLiteral("/api/files/mkdir"),
            QJsonObject{{QStringLiteral("path"),
                         cwd + QStringLiteral("/") + name.trimmed()}});
      });
  tools_row->addWidget(new_folder);

  auto* new_file = Button(tools, QStringLiteral("New file"));
  new_file->setObjectName(QStringLiteral("artifactsNewFile"));
  new_file->setEnabled(static_cast<bool>(act) && !cwd.isEmpty());
  QObject::connect(
      new_file, &QPushButton::clicked, new_file, [page, act, cwd]() {
        QPointer<QWidget> page_guard(page);
        QWidget* dialog_parent =
            page_guard == nullptr ? nullptr : page_guard->window();
        bool ok = false;
        const QString name = QInputDialog::getText(
            dialog_parent, QStringLiteral("New file"),
            QStringLiteral("File name:"), QLineEdit::Normal, QString(), &ok);
        if (!ok || name.trimmed().isEmpty() || act == nullptr) {
          return;
        }
        // write-text creates the file; the parent must exist.
        act(QStringLiteral("artifacts"), QStringLiteral("POST"),
            QStringLiteral("/api/fs/write-text"),
            QJsonObject{{QStringLiteral("path"),
                         cwd + QStringLiteral("/") + name.trimmed()},
                        {QStringLiteral("content"), QString()}});
      });
  tools_row->addWidget(new_file);

  const auto upload_file = context.upload_file;
  const auto pick_open_file = context.pick_open_file;
  const auto refresh_artifacts = context.refresh;
  auto* upload = Button(tools, QStringLiteral("Upload"));
  upload->setObjectName(QStringLiteral("artifactsUpload"));
  upload->setEnabled(static_cast<bool>(upload_file) && !cwd.isEmpty());
  QObject::connect(
      upload, &QPushButton::clicked, upload,
      [page, refresh_artifacts, upload_file, pick_open_file, cwd]() {
        // Capture the guard before the modal loop: rebuilding the page while
        // the file picker is open would otherwise leave a dangling widget
        // pointer in the completion callback.
        QPointer<QWidget> page_guard(page);
        QWidget* dialog_parent =
            page_guard == nullptr ? nullptr : page_guard->window();
        const QString local =
            pick_open_file
                ? pick_open_file(QStringLiteral("Upload file"), cwd)
                : QFileDialog::getOpenFileName(
                      dialog_parent, QStringLiteral("Upload file"), cwd);
        if (local.isEmpty()) {
          return;
        }
        const QString destination =
            cwd + QStringLiteral("/") + QFileInfo(local).fileName();
        upload_file(destination, local,
                    [page_guard, refresh_artifacts](const QJsonObject&,
                                                    const QString& error) {
                      if (!error.isEmpty()) {
                        if (page_guard != nullptr) {
                          QMessageBox::warning(page_guard,
                                               QStringLiteral("Upload failed"),
                                               error);
                        }
                        return;
                      }
                      if (refresh_artifacts) {
                        refresh_artifacts(QStringLiteral("artifacts"));
                      }
                    });
      });
  tools_row->addWidget(upload);
  tools_row->addStretch(1);
  layout->addWidget(tools);

  auto* splitter_host = new QWidget(page);
  auto* split = new QHBoxLayout(splitter_host);
  split->setContentsMargins(0, 0, 0, 0);
  split->setSpacing(12);

  auto* list = new QListWidget(splitter_host);
  list->setObjectName(QStringLiteral("artifactsList"));
  list->setMinimumWidth(260);
  const QJsonObject listing = data.value(QStringLiteral("listing")).toObject();
  const QJsonArray entries = PayloadArray(listing, "entries", {"entries"});
  if (cwd != QStringLiteral("/")) {
    auto* up = new QListWidgetItem(QStringLiteral("../"), list);
    up->setData(Qt::UserRole, true);
    up->setData(Qt::UserRole + 1, true);
    int slash = cwd.lastIndexOf('/', cwd.size() - 2);
    up->setData(Qt::UserRole + 2,
                slash <= 0 ? QStringLiteral("/") : cwd.left(slash));
  }
  for (const QJsonValue& value : entries) {
    const QJsonObject entry = value.toObject();
    const QString name = entry.value(QStringLiteral("name")).toString();
    if (name.isEmpty() || name == QStringLiteral(".") ||
        name == QStringLiteral("..")) {
      continue;
    }
    const bool is_dir =
        entry.value(QStringLiteral("isDirectory"))
            .toBool(entry.value(QStringLiteral("is_directory")).toBool());
    auto* item = new QListWidgetItem(
        (is_dir ? QStringLiteral("[dir] ") : QStringLiteral("      ")) + name,
        list);
    item->setData(Qt::UserRole, is_dir);
    item->setData(Qt::UserRole + 1, false);
    item->setData(Qt::UserRole + 2,
                  entry.value(QStringLiteral("path")).toString());
    if (entry.value(QStringLiteral("path")).toString() == selected) {
      list->setCurrentItem(item);
    }
  }
  const auto preview_file = context.preview_file;
  const auto activate_item = [open_dir, preview_file](QListWidgetItem* item) {
    const QString target = item->data(Qt::UserRole + 2).toString();
    if (item->data(Qt::UserRole).toBool()) {
      if (open_dir) {
        open_dir(target);
      }
    } else if (preview_file) {
      preview_file(target);
    }
  };
  QObject::connect(list, &QListWidget::itemClicked, list, activate_item);
  QObject::connect(list, &QListWidget::itemActivated, list, activate_item);

  const auto download_file = context.download_file;
  const auto pick_save_file = context.pick_save_file;
  auto* download = Button(splitter_host, QStringLiteral("Download"));
  download->setObjectName(QStringLiteral("artifactsDownload"));
  download->setEnabled(static_cast<bool>(download_file));
  QObject::connect(
      download, &QPushButton::clicked, download,
      [page, download_file, pick_save_file, list]() {
        QPointer<QWidget> page_guard(page);
        QListWidgetItem* item = list->currentItem();
        if (item == nullptr || item->data(Qt::UserRole + 1).toBool() ||
            item->data(Qt::UserRole).toBool()) {
          return;
        }
        const QString target = item->data(Qt::UserRole + 2).toString();
        if (target.isEmpty()) {
          return;
        }
        const QString name = QFileInfo(target).fileName();
        QWidget* dialog_parent =
            page_guard == nullptr ? nullptr : page_guard->window();
        const QString local =
            pick_save_file
                ? pick_save_file(QStringLiteral("Save artifact"), name)
                : QFileDialog::getSaveFileName(
                      dialog_parent, QStringLiteral("Save artifact"), name);
        if (local.isEmpty()) {
          return;
        }
        download_file(target, local,
                      [page_guard](const QJsonObject&, const QString& error) {
                        if (!error.isEmpty() && page_guard != nullptr) {
                          QMessageBox::warning(
                              page_guard, QStringLiteral("Download failed"),
                              error);
                        }
                      });
      });

  auto* delete_button = Button(splitter_host, QStringLiteral("Delete"));
  delete_button->setObjectName(QStringLiteral("artifactsDelete"));
  delete_button->setEnabled(static_cast<bool>(act));
  QObject::connect(
      delete_button, &QPushButton::clicked, delete_button, [page, act, list]() {
        QPointer<QWidget> page_guard(page);
        QListWidgetItem* item = list->currentItem();
        if (item == nullptr || item->data(Qt::UserRole + 1).toBool()) {
          return;
        }
        const QString target = item->data(Qt::UserRole + 2).toString();
        if (target.isEmpty()) {
          return;
        }
        QWidget* dialog_parent =
            page_guard == nullptr ? nullptr : page_guard->window();
        if (QMessageBox::question(
                dialog_parent, QStringLiteral("Delete"),
                QStringLiteral("Delete %1?").arg(item->text().trimmed()),
                QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes ||
            act == nullptr) {
          return;
        }
        act(QStringLiteral("artifacts"), QStringLiteral("DELETE"),
            QStringLiteral("/api/files"),
            QJsonObject{{QStringLiteral("path"), target},
                        {QStringLiteral("recursive"), true}});
      });
  auto* list_column = new QWidget(splitter_host);
  auto* list_layout = new QVBoxLayout(list_column);
  list_layout->setContentsMargins(0, 0, 0, 0);
  list_layout->addWidget(list, 1);
  auto* actions_row = new QWidget(splitter_host);
  auto* actions_layout = new QHBoxLayout(actions_row);
  actions_layout->setContentsMargins(0, 0, 0, 0);
  actions_layout->addWidget(download);
  actions_layout->addWidget(delete_button);
  actions_layout->addStretch(1);
  list_layout->addWidget(actions_row);
  split->addWidget(list_column, 1);

  auto* preview_column = new QWidget(splitter_host);
  auto* preview_layout = new QVBoxLayout(preview_column);
  preview_layout->setContentsMargins(0, 0, 0, 0);
  preview_layout->setSpacing(6);
  auto* preview_title = new QLineEdit(
      selected.isEmpty()
          ? QStringLiteral("Select a file to preview its contents.")
          : selected,
      preview_column);
  preview_title->setObjectName(QStringLiteral("artifactsPreviewPath"));
  preview_title->setReadOnly(true);
  preview_layout->addWidget(preview_title);

  const QJsonObject preview_doc =
      data.value(QStringLiteral("preview")).toObject();
  const QString preview_text =
      preview_doc.value(QStringLiteral("text")).toString();
  const bool binary = preview_doc.value(QStringLiteral("binary")).toBool();
  const bool truncated =
      preview_doc.value(QStringLiteral("truncated")).toBool();
  const QString mime = preview_doc.value(QStringLiteral("mimeType")).toString();

  // Render formats the desktop previews instead of dumping raw source:
  // markdown, HTML and CSV/TSV get a Preview tab plus the editable Source tab.
  const QString suffix = QFileInfo(selected).suffix().toLower();
  const bool markdown = mime.startsWith(QStringLiteral("text/markdown")) ||
                        suffix == QStringLiteral("md") ||
                        suffix == QStringLiteral("markdown");
  const bool html = mime == QStringLiteral("text/html") ||
                    suffix == QStringLiteral("html") ||
                    suffix == QStringLiteral("htm");
  const bool csv = mime == QStringLiteral("text/csv") ||
                   suffix == QStringLiteral("csv") ||
                   suffix == QStringLiteral("tsv");
  auto make_editor = [&](QWidget* parent_widget) {
    auto* source = new QPlainTextEdit(parent_widget);
    source->setObjectName(QStringLiteral("artifactsEditor"));
    source->setPlainText(preview_text);
    source->setReadOnly(binary || truncated || preview_text.isEmpty());
    source->setMinimumHeight(220);
    return source;
  };

  QPlainTextEdit* editor = nullptr;
  if (!binary && (markdown || html || csv)) {
    auto* tabs = new QTabWidget(preview_column);
    tabs->setObjectName(QStringLiteral("artifactsPreviewTabs"));
    auto* browser = new QTextBrowser(tabs);
    browser->setObjectName(QStringLiteral("artifactsPreviewBrowser"));
    browser->setStyleSheet(
        QStringLiteral(
            "QTextBrowser { background: %1; color: %2; "
            "border: 1px solid %3; border-radius: 6px; padding: 8px; }")
            .arg(context.theme.surface.name(), context.theme.foreground.name(),
                 context.theme.stroke.name()));
    browser->setOpenExternalLinks(false);
    browser->setOpenLinks(false);
    QObject::connect(browser, &QTextBrowser::anchorClicked, browser,
                     [](const QUrl& url) {
                       if (url.isValid()) {
                         QDesktopServices::openUrl(url);
                       }
                     });
    browser->document()->setBaseUrl(
        QUrl::fromLocalFile(QFileInfo(selected).absolutePath() + "/"));
    if (markdown) {
      browser->setMarkdown(preview_text);
      StyleMarkdownPreview(browser, context.theme);
    } else if (html) {
      browser->setHtml(preview_text);
    } else {
      // Light CSV/TSV table: first row as header, bounded so a huge export
      // cannot stall the UI thread.
      const QChar delimiter = suffix == QStringLiteral("tsv")
                                  ? QLatin1Char('\t')
                                  : QLatin1Char(',');
      const QStringList lines = preview_text.split('\n');
      QString table = QStringLiteral(
          "<table cellspacing='0' cellpadding='4' border='1' "
          "style='border-collapse:collapse;font-size:12px;'>");
      const int row_limit = std::min(static_cast<int>(lines.size()), 200);
      for (int row = 0; row < row_limit; ++row) {
        const QStringList cells = lines[row].split(delimiter);
        table += row == 0 ? QStringLiteral("<tr style='font-weight:600;'>")
                          : QStringLiteral("<tr>");
        const int column_limit = std::min<int>(cells.size(), 30);
        for (int column = 0; column < column_limit; ++column) {
          QString cell = cells[column];
          if (cell.size() > 200) {
            cell = cell.left(200) + QStringLiteral("…");
          }
          cell.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
          cell.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
          cell.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
          table += QStringLiteral("<td>%1</td>").arg(cell);
        }
        if (cells.size() > column_limit) {
          table += QStringLiteral("<td>…</td>");
        }
        table += QStringLiteral("</tr>");
      }
      if (lines.size() > row_limit) {
        table +=
            QStringLiteral("<tr><td colspan='1'><i>%1 more rows…</i></td></tr>")
                .arg(lines.size() - row_limit);
      }
      table += QStringLiteral("</table>");
      browser->setHtml(table);
    }
    tabs->addTab(browser, QStringLiteral("Preview"));
    editor = make_editor(tabs);
    tabs->addTab(editor, QStringLiteral("Source"));
    preview_layout->addWidget(tabs, 1);
  } else {
    editor = make_editor(preview_column);
    preview_layout->addWidget(editor, 1);
  }

  const QJsonObject media =
      data.value(QStringLiteral("previewMedia")).toObject();
  const QString data_url = media.value(QStringLiteral("dataUrl")).toString();
  if (mime.startsWith(QStringLiteral("image/")) && !data_url.isEmpty()) {
    const int marker = data_url.indexOf(QStringLiteral("base64,"));
    const QByteArray bytes =
        QByteArray::fromBase64(data_url.mid(marker + 7).toUtf8());
    QPixmap pixmap;
    if (pixmap.loadFromData(bytes)) {
      auto* image = new QLabel(preview_column);
      image->setObjectName(QStringLiteral("artifactsImage"));
      image->setPixmap(pixmap.scaledToWidth(qBound(320, pixmap.width(), 640),
                                            Qt::SmoothTransformation));
      preview_layout->addWidget(image, 0, Qt::AlignLeft);
    }
  } else if (mime == QStringLiteral("application/pdf")) {
    auto* open_pdf = Button(preview_column, QStringLiteral("Open PDF"));
    open_pdf->setObjectName(QStringLiteral("artifactsOpenPdf"));
    open_pdf->setEnabled(!data_url.isEmpty());
    QObject::connect(open_pdf, &QPushButton::clicked, open_pdf, [data_url]() {
      QDesktopServices::openUrl(QUrl(data_url));
    });
    preview_layout->addWidget(open_pdf, 0, Qt::AlignLeft);
  }
  if (binary) {
    preview_layout->addWidget(Description(
        preview_column,
        QStringLiteral("Binary file (%1 bytes) — no inline text preview.")
            .arg(preview_doc.value(QStringLiteral("byteSize")).toDouble())));
  }
  if (!selected.isEmpty()) {
    auto* open_external =
        Button(preview_column, QStringLiteral("Open in default app"));
    open_external->setObjectName(QStringLiteral("artifactsOpenExternal"));
    QObject::connect(
        open_external, &QPushButton::clicked, open_external, [selected]() {
          QDesktopServices::openUrl(
              QUrl::fromLocalFile(QFileInfo(selected).absoluteFilePath()));
        });
    preview_layout->addWidget(open_external, 0, Qt::AlignLeft);
  }

  auto* save_row = new QWidget(preview_column);
  auto* save_layout = new QHBoxLayout(save_row);
  save_layout->setContentsMargins(0, 0, 0, 0);
  auto* save = Button(save_row, QStringLiteral("Save"));
  save->setObjectName(QStringLiteral("artifactsSave"));
  const auto save_file = context.save_file;
  save->setEnabled(static_cast<bool>(save_file) && !selected.isEmpty() &&
                   !binary && !truncated && !preview_text.isEmpty());
  QObject::connect(save, &QPushButton::clicked, save,
                   [save_file, selected, editor]() {
                     if (save_file) {
                       save_file(selected, editor->toPlainText());
                     }
                   });
  save_layout->addWidget(save);
  save_layout->addStretch(1);
  preview_layout->addWidget(save_row);
  split->addWidget(preview_column, 1);
  layout->addWidget(splitter_host, 1);
  return page;
}

// Four colour bands of a preset: canvas, surface, accent, text. Enough to tell
// two schemes apart at a glance without rendering a whole window per card.
class ThemeSwatch : public QWidget {
 public:
  ThemeSwatch(const Theme& preset, const Theme& current, QWidget* parent)
      : QWidget(parent), preset_(preset), current_(current) {
    setFixedSize(196, 56);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
  }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF canvas(0.5, 0.5, width() - 1.0, height() - 1.0);
    painter.setPen(QPen(current_.stroke, 1));
    painter.setBrush(preset_.background);
    painter.drawRoundedRect(canvas, 7, 7);

    const QRectF band(9, 9, width() - 18.0, height() - 18.0);
    painter.setClipRect(band);
    const double step = band.width() / 4.0;
    const QColor colors[4] = {preset_.surface, preset_.surface_raised,
                              preset_.accent, preset_.foreground};
    for (int i = 0; i < 4; ++i) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(colors[i]);
      painter.drawRect(
          QRectF(band.left() + step * i, band.top(), step, band.height()));
    }
    painter.setClipping(false);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(current_.stroke, 1));
    painter.drawRoundedRect(QRectF(9, 9, band.width(), band.height()), 5, 5);
  }

 private:
  Theme preset_;
  Theme current_;
};

QWidget* ThemesPage(const PageContext& context, QWidget* parent) {
  auto* page = new QWidget(parent);
  page->setObjectName(QStringLiteral("settingsPage:themes"));
  auto* layout = PageLayout(page);
  AddLocalHeading(page, layout, QStringLiteral("Themes"));
  layout->addWidget(Description(
      page, QStringLiteral("Pre-made colour schemes. The palette comes from "
                           "qt-themes (MIT); Hermes derives its surfaces, "
                           "bubbles and code colours from it.")));

  const auto apply = context.set_theme_preset;
  const QString active = context.theme_preset;

  auto add_card = [&](const QString& id, const QString& name,
                      const Theme& preset, const QString& group) {
    Row row = AddRow(page, layout, name, group);
    row.widget->setObjectName(QStringLiteral("themeRow_") + id);
    row.widget->setMaximumWidth(860);
    row.layout->insertWidget(
        1, new ThemeSwatch(preset, context.theme, row.widget));
    auto* use = Button(row.widget, active == id ? QStringLiteral("Active")
                                                : QStringLiteral("Use"));
    use->setObjectName(QStringLiteral("themeUse_") + id);
    use->setEnabled(static_cast<bool>(apply) && active != id);
    if (active == id) {
      use->setStyleSheet(
          QStringLiteral("QPushButton { color: %1; border-color: %1; }")
              .arg(context.theme.accent.name(QColor::HexArgb)));
    }
    QObject::connect(use, &QPushButton::clicked, use, [apply, id]() {
      if (apply) {
        apply(id);
      }
    });
    row.layout->addWidget(use);
  };

  add_card(QStringLiteral(""), QStringLiteral("Hermes (built-in)"),
           context.theme, QStringLiteral("the current light/dark tokens"));

  for (const ThemePreset& preset : ThemePresets()) {
    add_card(preset.id, preset.name, preset.theme, preset.group);
  }
  return page;
}

QWidget* AppearancePage(const PageContext& context, QWidget* parent) {
  auto* page = new QWidget(parent);
  page->setObjectName(QStringLiteral("settingsPage:appearance"));
  auto* layout = PageLayout(page);
  AddLocalHeading(page, layout, QStringLiteral("Appearance"));
  layout->addWidget(Description(
      page, QStringLiteral("These preferences belong to this window. They are "
                           "stored locally and apply immediately to the "
                           "transcript, sidebar and terminal.")));

  const auto get_pref = context.get_pref;
  const auto set_pref = context.set_pref;
  const auto read = [get_pref](const char* key) {
    return get_pref ? get_pref(QLatin1String(key)) : QString();
  };
  const auto save = [set_pref](const char* key, const QString& value) {
    if (set_pref) {
      set_pref(QLatin1String(key), value);
    }
  };

  // ---- Theme -------------------------------------------------------------
  AddLocalHeading(page, layout, QStringLiteral("Theme"));
  Row theme_row = AddRow(page, layout, QStringLiteral("Colour mode"),
                         QStringLiteral("Light or dark. Applies to every "
                                        "native surface. Pre-made colour "
                                        "schemes live on the Themes page."));
  auto* theme = new QComboBox(theme_row.widget);
  theme->setObjectName(QStringLiteral("appearanceTheme"));
  theme->addItem(QStringLiteral("Light"), 0);
  theme->addItem(QStringLiteral("Dark"), 1);
  theme->setCurrentIndex(context.theme.background.lightness() > 128 ? 0 : 1);
  const auto set_theme = context.set_theme;
  QObject::connect(
      theme, QOverload<int>::of(&QComboBox::currentIndexChanged), theme,
      [set_theme, save](int index) {
        if (set_theme) {
          set_theme(index == 1 ? ThemeMode::kDark : ThemeMode::kLight);
        }
        save("native.theme",
             index == 1 ? QStringLiteral("dark") : QStringLiteral("light"));
      });
  theme_row.layout->addWidget(theme);

  // ---- Typography --------------------------------------------------------
  AddLocalHeading(page, layout, QStringLiteral("Typography"));

  Row scale_row =
      AddRow(page, layout, QStringLiteral("UI scale"),
             QStringLiteral("Scales every custom-painted surface: transcript, "
                            "composer, sidebar and terminal."));
  auto* preset = new QComboBox(scale_row.widget);
  preset->setObjectName(QStringLiteral("appearanceUiScalePreset"));
  const QList<double> presets{0.9, 1.0, 1.1, 1.25, 1.5};
  for (double value : presets) {
    preset->addItem(QStringLiteral("%1%").arg(qRound(value * 100)), value);
  }
  preset->addItem(QStringLiteral("Custom"), 0.0);
  auto* scale = new QDoubleSpinBox(scale_row.widget);
  scale->setObjectName(QStringLiteral("appearanceFontScale"));
  scale->setRange(0.8, 1.6);
  scale->setSingleStep(0.05);
  scale->setDecimals(2);
  scale->setSuffix(QStringLiteral("×"));
  const QString stored = read("native.font_scale");
  const double stored_scale = stored.toDouble();
  const double current_scale = stored_scale > 0 ? stored_scale : 1.0;
  scale->setValue(current_scale);
  QSignalBlocker scale_blocker(scale);
  int preset_index = presets.size();
  for (int i = 0; i < presets.size(); ++i) {
    if (qAbs(presets.at(i) - current_scale) < 0.001) {
      preset_index = i;
      break;
    }
  }
  preset->setCurrentIndex(preset_index);
  QObject::connect(preset, QOverload<int>::of(&QComboBox::currentIndexChanged),
                   preset, [preset, scale](int index) {
                     const double value = preset->itemData(index).toDouble();
                     if (value > 0.0) {
                       scale->setValue(value);
                     }
                   });
  QObject::connect(scale, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                   scale, [preset, presets, save](double value) {
                     save("native.font_scale", QString::number(value, 'f', 2));
                     const QSignalBlocker blocker(preset);
                     int match = presets.size();
                     for (int i = 0; i < presets.size(); ++i) {
                       if (qAbs(presets.at(i) - value) < 0.001) {
                         match = i;
                         break;
                       }
                     }
                     preset->setCurrentIndex(match);
                   });
  scale_row.layout->addWidget(preset);
  scale_row.layout->addWidget(scale);

  Row chat_scale_row = AddRow(
      page, layout, QStringLiteral("Chat text scale"),
      QStringLiteral("Scales transcript text separately from the rest of the "
                     "window."));
  auto* chat_scale = new QDoubleSpinBox(chat_scale_row.widget);
  chat_scale->setObjectName(QStringLiteral("appearanceChatTextScale"));
  chat_scale->setRange(0.8, 1.4);
  chat_scale->setSingleStep(0.05);
  chat_scale->setDecimals(2);
  chat_scale->setSuffix(QStringLiteral("×"));
  const double stored_chat = read("appearance.chat_text_scale").toDouble();
  chat_scale->setValue(stored_chat > 0 ? stored_chat : 1.0);
  QObject::connect(
      chat_scale, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
      chat_scale, [save](double value) {
        save("appearance.chat_text_scale", QString::number(value, 'f', 2));
      });
  chat_scale_row.layout->addWidget(chat_scale);

  const auto add_font_picker =
      [&](const QString& title, const QString& description, const char* key,
          const char* object_name, const QString& fallback) {
        Row row = AddRow(page, layout, title, description);
        auto* combo = new QComboBox(row.widget);
        combo->setObjectName(QLatin1String(object_name));
        combo->setEditable(true);
        combo->setInsertPolicy(QComboBox::NoInsert);
        combo->addItem(QStringLiteral("System default"), QString());
        for (const QString& family : QFontDatabase::families()) {
          combo->addItem(family, family);
        }
        const QString value = read(key);
        combo->setCurrentIndex(0);
        combo->setEditText(value.isEmpty() ? fallback : value);
        QObject::connect(combo, &QComboBox::currentTextChanged, combo,
                         [save, key](const QString& text) {
                           save(key, text == QStringLiteral("System default")
                                         ? QString()
                                         : text.trimmed());
                         });
        row.layout->addWidget(combo);
      };
  add_font_picker(
      QStringLiteral("Interface font"),
      QStringLiteral("Controls labels and message text. Leave blank for the "
                     "system font."),
      "appearance.ui_font", "appearanceUiFont", context.theme.ui_family);
  add_font_picker(
      QStringLiteral("Code font"),
      QStringLiteral("Controls fenced code, tool output, the editor and the "
                     "embedded terminal."),
      "appearance.mono_font", "appearanceMonoFont", context.theme.mono_family);

  // ---- Window ------------------------------------------------------------
  AddLocalHeading(page, layout, QStringLiteral("Window"));
  Row density_row = AddRow(
      page, layout, QStringLiteral("Session list density"),
      QStringLiteral("Choose how much context appears beneath session titles "
                     "in the sidebar."));
  auto* density = new QComboBox(density_row.widget);
  density->setObjectName(QStringLiteral("appearanceSessionDensity"));
  density->addItem(QStringLiteral("Compact"), QStringLiteral("compact"));
  density->addItem(QStringLiteral("Comfortable"),
                   QStringLiteral("comfortable"));
  density->addItem(QStringLiteral("Detailed"), QStringLiteral("detailed"));
  const QString density_value = read("appearance.session_density");
  density->setCurrentIndex(density->findData(
      density_value.isEmpty() ? QStringLiteral("compact") : density_value));
  QObject::connect(density, QOverload<int>::of(&QComboBox::currentIndexChanged),
                   density, [density, save](int index) {
                     save("appearance.session_density",
                          density->itemData(index).toString());
                   });
  density_row.layout->addWidget(density);

  Row opacity_row = AddRow(
      page, layout, QStringLiteral("Window translucency"),
      QStringLiteral("Lowers the opacity of the whole window so the desktop "
                     "shows through. 100% keeps it opaque."));
  auto* opacity = new QSlider(Qt::Horizontal, opacity_row.widget);
  opacity->setObjectName(QStringLiteral("appearanceWindowOpacity"));
  opacity->setRange(60, 100);
  const int stored_opacity = read("appearance.window_opacity").toInt();
  opacity->setValue(stored_opacity > 0 ? stored_opacity : 100);
  auto* opacity_value = Label(opacity_row.widget, QString(), "settingValue");
  opacity_value->setMinimumWidth(44);
  opacity_value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  opacity_value->setText(QStringLiteral("%1%").arg(opacity->value()));
  QObject::connect(opacity, &QSlider::valueChanged, opacity,
                   [save, opacity_value](int value) {
                     opacity_value->setText(QStringLiteral("%1%").arg(value));
                     save("appearance.window_opacity", QString::number(value));
                   });
  opacity_row.layout->addWidget(opacity, 1);
  opacity_row.layout->addWidget(opacity_value);

  // ---- Chat --------------------------------------------------------------
  AddLocalHeading(page, layout, QStringLiteral("Chat"));
  Row width_row = AddRow(
      page, layout, QStringLiteral("Message width"),
      QStringLiteral("Maximum width of the conversation column. Wider columns "
                     "suit code and tables; narrower ones read more easily."));
  auto* width = new QComboBox(width_row.widget);
  width->setObjectName(QStringLiteral("appearanceMessageWidth"));
  width->addItem(QStringLiteral("Narrow · 600 px"), 600);
  width->addItem(QStringLiteral("Standard · 720 px"), 720);
  width->addItem(QStringLiteral("Wide · 860 px"), 860);
  width->addItem(QStringLiteral("Full · 1040 px"), 1040);
  const int stored_width = read("appearance.message_width").toInt();
  const int width_value = stored_width > 0 ? stored_width : 720;
  int width_index = width->findData(width_value);
  if (width_index < 0) {
    width->addItem(QStringLiteral("%1 px").arg(width_value), width_value);
    width_index = width->count() - 1;
  }
  width->setCurrentIndex(width_index);
  QObject::connect(width, QOverload<int>::of(&QComboBox::currentIndexChanged),
                   width, [width, save](int index) {
                     save("appearance.message_width",
                          QString::number(width->itemData(index).toInt()));
                   });
  width_row.layout->addWidget(width);

  Row timestamps_row =
      AddRow(page, layout, QStringLiteral("Message timestamps"),
             QStringLiteral("Show the time beside assistant names."));
  BoolCheck(
      timestamps_row.widget, QStringLiteral("appearanceShowTimestamps"),
      AppearanceBool(read("appearance.show_timestamps"), true),
      [save](bool value) {
        save("appearance.show_timestamps",
             value ? QStringLiteral("1") : QStringLiteral("0"));
      },
      timestamps_row.layout);

  Row reasoning_row = AddRow(
      page, layout, QStringLiteral("Collapse reasoning by default"),
      QStringLiteral("Keep the thinking trace folded until it is opened. "
                     "Turning this off shows every trace while it streams."));
  BoolCheck(
      reasoning_row.widget, QStringLiteral("appearanceReasoningCollapsed"),
      AppearanceBool(read("appearance.reasoning_collapsed"), true),
      [save](bool value) {
        save("appearance.reasoning_collapsed",
             value ? QStringLiteral("1") : QStringLiteral("0"));
      },
      reasoning_row.layout);

  Row bubble_row = AddRow(
      page, layout, QStringLiteral("User bubble transparency"),
      QStringLiteral("Lets the transcript background show through your own "
                     "messages."));
  auto* bubble = new QSlider(Qt::Horizontal, bubble_row.widget);
  bubble->setObjectName(QStringLiteral("appearanceUserBubble"));
  bubble->setRange(0, 80);
  const int stored_bubble = read("appearance.user_bubble_transparency").toInt();
  bubble->setValue(std::clamp(stored_bubble, 0, 80));
  auto* bubble_value = Label(bubble_row.widget, QString(), "settingValue");
  bubble_value->setMinimumWidth(44);
  bubble_value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  bubble_value->setText(QStringLiteral("%1%").arg(bubble->value()));
  QObject::connect(
      bubble, &QSlider::valueChanged, bubble, [save, bubble_value](int value) {
        bubble_value->setText(QStringLiteral("%1%").arg(value));
        save("appearance.user_bubble_transparency", QString::number(value));
      });
  bubble_row.layout->addWidget(bubble, 1);
  bubble_row.layout->addWidget(bubble_value);

  Row animations_row = AddRow(
      page, layout, QStringLiteral("Animations"),
      QStringLiteral("Animate the streaming caret and activity indicators. "
                     "Disable for reduced motion."));
  BoolCheck(
      animations_row.widget, QStringLiteral("appearanceAnimations"),
      AppearanceBool(read("appearance.animations"), true),
      [save](bool value) {
        save("appearance.animations",
             value ? QStringLiteral("1") : QStringLiteral("0"));
      },
      animations_row.layout);

  // ---- Code --------------------------------------------------------------
  AddLocalHeading(page, layout, QStringLiteral("Code"));
  Row code_row =
      AddRow(page, layout, QStringLiteral("Code text scale"),
             QStringLiteral("Scales fenced code, tool output and the embedded "
                            "terminal without changing conversation text."));
  auto* code_scale = new QDoubleSpinBox(code_row.widget);
  code_scale->setObjectName(QStringLiteral("appearanceCodeScale"));
  code_scale->setRange(0.8, 1.5);
  code_scale->setSingleStep(0.05);
  code_scale->setDecimals(2);
  code_scale->setSuffix(QStringLiteral("×"));
  const double stored_code = read("appearance.code_scale").toDouble();
  code_scale->setValue(stored_code > 0 ? stored_code : 1.0);
  QObject::connect(
      code_scale, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
      code_scale, [save](double value) {
        save("appearance.code_scale", QString::number(value, 'f', 2));
      });
  code_row.layout->addWidget(code_scale);

  layout->addWidget(Description(
      page, QStringLiteral("Marketplace theme search is served by the desktop "
                           "web renderer; native colour schemes are available "
                           "on the Themes page.")));
  return page;
}
}  // namespace

bool IsDataPage(const QString& id) {
  static const char* const kPages[] = {
      "providers",  "keys",     "vault",     "gateway",  "plugins",
      "billing",    "sessions", "about",     "cron",     "capabilities",
      "messaging",  "profiles", "artifacts", "webhooks", "localmodels",
      "delegation", "agents",   "starmap"};
  for (const char* page : kPages) {
    if (id == QLatin1String(page)) {
      return true;
    }
  }
  return false;
}

bool IsLocalPage(const QString& id) {
  return id == QStringLiteral("appearance") || id == QStringLiteral("themes") ||
         id == QStringLiteral("notifications") ||
         id == QStringLiteral("keybinds");
}

QWidget* BuildDataPage(const QString& id, const PageContext& context,
                       QWidget* parent) {
  if (id == QStringLiteral("appearance")) {
    return AppearancePage(context, parent);
  }
  if (id == QStringLiteral("themes")) {
    return ThemesPage(context, parent);
  }
  if (id == QStringLiteral("providers")) {
    return ProvidersPage(context, parent);
  }
  if (id == QStringLiteral("keys")) {
    return KeysPage(context, parent);
  }
  if (id == QStringLiteral("vault")) {
    return VaultPage(context, parent);
  }
  if (id == QStringLiteral("gateway")) {
    return GatewayPage(context, parent);
  }
  if (id == QStringLiteral("notifications")) {
    return NotificationsPage(context, parent);
  }
  if (id == QStringLiteral("plugins")) {
    return PluginsPage(context, parent);
  }
  if (id == QStringLiteral("billing")) {
    return BillingPage(context, parent);
  }
  if (id == QStringLiteral("sessions")) {
    return SessionsPage(context, parent);
  }
  if (id == QStringLiteral("keybinds")) {
    return KeybindsPage(context, parent);
  }
  if (id == QStringLiteral("about")) {
    return AboutPage(context, parent);
  }
  if (id == QStringLiteral("cron")) {
    return CronPage(context, parent);
  }
  if (id == QStringLiteral("capabilities")) {
    return CapabilitiesPage(context, parent);
  }
  if (id == QStringLiteral("messaging")) {
    return MessagingPage(context, parent);
  }
  if (id == QStringLiteral("profiles")) {
    return ProfilesPage(context, parent);
  }
  if (id == QStringLiteral("artifacts")) {
    return ArtifactsPage(context, parent);
  }
  if (id == QStringLiteral("webhooks")) {
    return WebhooksPage(context, parent);
  }
  if (id == QStringLiteral("localmodels")) {
    return LocalModelsPage(context, parent);
  }
  if (id == QStringLiteral("delegation")) {
    return DelegationPage(context, parent);
  }
  if (id == QStringLiteral("agents")) {
    return AgentsPage(context, parent);
  }
  if (id == QStringLiteral("starmap")) {
    return StarmapPage(context, parent);
  }
  return nullptr;
}

}  // namespace hermes::ui
