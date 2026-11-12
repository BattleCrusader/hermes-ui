// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/pages_view.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QVBoxLayout>
#include <utility>

namespace hermes::ui {

PagesView::PagesView(Theme theme, QWidget *parent)
    : QWidget(parent), theme_(theme) {
  setObjectName(QStringLiteral("pagesView"));
  setAttribute(Qt::WA_StyledBackground, true);
  BuildUi();
  SetTheme(theme);
}

void PagesView::BuildUi() {
  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(28, 22, 28, 22);
  root->setSpacing(14);

  auto *header = new QWidget(this);
  auto *header_row = new QHBoxLayout(header);
  header_row->setContentsMargins(0, 0, 0, 0);
  header_row->setSpacing(12);
  auto *titles = new QVBoxLayout();
  titles->setSpacing(2);
  title_ = new QLabel(header);
  title_->setObjectName(QStringLiteral("pagesTitle"));
  titles->addWidget(title_);
  subtitle_ = new QLabel(header);
  subtitle_->setObjectName(QStringLiteral("pagesSubtitle"));
  titles->addWidget(subtitle_);
  header_row->addLayout(titles, 1);
  refresh_ = new QPushButton(QStringLiteral("Refresh"), header);
  refresh_->setObjectName(QStringLiteral("pagesRefresh"));
  connect(refresh_, &QPushButton::clicked, this, [this]() {
    if (on_page_request && !page_.isEmpty()) {
      on_page_request(page_);
    }
  });
  header_row->addWidget(refresh_);
  auto *close = new QPushButton(QStringLiteral("Close"), header);
  close->setObjectName(QStringLiteral("pagesClose"));
  connect(close, &QPushButton::clicked, this, [this]() {
    if (on_close) {
      on_close();
    }
  });
  header_row->addWidget(close);
  root->addWidget(header);

  status_ = new QLabel(this);
  status_->setObjectName(QStringLiteral("pagesStatus"));
  root->addWidget(status_);

  scroll_ = new QScrollArea(this);
  scroll_->setFrameShape(QFrame::NoFrame);
  scroll_->setWidgetResizable(true);
  body_ = new QWidget(scroll_);
  body_->setObjectName(QStringLiteral("pagesBody"));
  body_layout_ = new QVBoxLayout(body_);
  body_layout_->setContentsMargins(0, 0, 0, 0);
  body_layout_->setSpacing(12);
  body_layout_->setAlignment(Qt::AlignTop);
  scroll_->setWidget(body_);
  root->addWidget(scroll_, 1);
}

void PagesView::SetTheme(const Theme &theme) {
  theme_ = theme;
  setStyleSheet(
      QStringLiteral(
          "QWidget#pagesView { background: %1; }"
          "QWidget#pagesBody { background: transparent; }"
          "QLabel#pagesTitle { color: %2; font-size: 22px; font-weight: 600; }"
          "QLabel#pagesSubtitle { color: %3; font-size: 12px; }"
          "QLabel#pagesStatus { color: %3; font-size: 11px; }"
          "QPushButton#pagesRefresh, QPushButton#pagesClose { "
          "background: transparent; color: %3; border: 1px solid %4; "
          "border-radius: 8px; padding: 6px 12px; }"
          "QPushButton#pagesRefresh:hover, QPushButton#pagesClose:hover { "
          "color: %2; background: %5; }"
          "QLabel#settingTitle { color: %2; font-size: 14px; "
          "font-weight: 600; }"
          "QLabel#settingDescription { color: %3; font-size: 11px; }")
          .arg(theme_.background.name(), theme_.foreground.name(),
               theme_.text_secondary.name(), theme_.stroke.name(),
               theme_.hover.name()));
  scroll_->setStyleSheet(
      QStringLiteral("QScrollArea { border: none; background: transparent; }"));
  update();
}

void PagesView::SetSnapshot(const ProfileSnapshot &snapshot) {
  const bool changed = snapshot.revision != last_revision_;
  snapshot_ = snapshot;
  if (!changed || !isVisible() || page_.isEmpty()) {
    return;
  }
  Rebuild();
}

void PagesView::OpenPage(const QString &id) {
  page_ = id;
  title_->setText(TitleFor(id));
  subtitle_->setText(QStringLiteral("Workspace · %1").arg(snapshot_.label));
  last_revision_ = -1;
  if (on_page_request) {
    on_page_request(id);
  }
  Rebuild();
}

void PagesView::ClearBody() {
  while (QLayoutItem *item = body_layout_->takeAt(0)) {
    if (QWidget *widget = item->widget()) {
      widget->deleteLater();
    }
    delete item;
  }
}

void PagesView::Rebuild() {
  if (page_.isEmpty()) {
    return;
  }
  const int revision = snapshot_.revision;
  if (last_revision_ == revision && body_layout_->count() > 0) {
    return;
  }
  last_revision_ = revision;
  ClearBody();

  PageContext context;
  context.theme = theme_;
  context.snapshot = &snapshot_;
  context.suppress_page_heading = true;
  context.refresh = [this](const QString &page) {
    if (on_page_request) {
      on_page_request(page);
    }
  };
  context.action = on_page_action;
  context.open_dir = on_open_dir;
  context.preview_file = on_preview_file;
  context.save_file = on_save_file;
  context.upload_file = on_upload_file;
  context.download_file = on_download_file;
  context.test_mcp_server = on_test_mcp_server;
  context.list_mcp_catalog = on_list_mcp_catalog;
  context.get_mcp_config = on_get_mcp_config;
  context.replace_mcp_config = on_replace_mcp_config;
  context.get_learning_node = on_get_learning_node;
  context.get_computer_use_status = on_get_computer_use_status;
  context.grant_computer_use_permissions = on_grant_computer_use_permissions;
  context.get_action_status = on_get_action_status;
  context.list_terminal_backends = on_list_terminal_backends;
  context.list_connectors = on_list_connectors;
  context.connect_connector = on_connect_connector;
  context.connector_operation_status = on_connector_operation_status;
  context.connector_rpc = on_connector_rpc;
  context.select_terminal_backend = on_select_terminal_backend;
  context.get_toolset_config = on_get_toolset_config;
  context.save_toolset_config = on_save_toolset_config;
  context.get_toolset_models = on_get_toolset_models;
  context.save_toolset_model = on_save_toolset_model;
  context.steer_subagent = on_steer_subagent;
  context.interrupt_subagent = on_interrupt_subagent;
  context.save_plugin_settings = on_save_plugin_settings;
  context.start_mcp_oauth = on_start_mcp_oauth;
  context.poll_mcp_oauth = on_poll_mcp_oauth;
  context.cancel_mcp_oauth = on_cancel_mcp_oauth;
  context.open_url = on_open_url;
  context.test_platform = on_test_platform;
  context.start_telegram_onboarding = on_start_telegram_onboarding;
  context.poll_telegram_onboarding = on_poll_telegram_onboarding;
  context.apply_telegram_onboarding = on_apply_telegram_onboarding;
  context.cancel_telegram_onboarding = on_cancel_telegram_onboarding;
  context.start_whatsapp_onboarding = on_start_whatsapp_onboarding;
  context.poll_whatsapp_onboarding = on_poll_whatsapp_onboarding;
  context.apply_whatsapp_onboarding = on_apply_whatsapp_onboarding;
  context.cancel_whatsapp_onboarding = on_cancel_whatsapp_onboarding;
  context.get_skill_content = on_get_skill_content;
  context.list_official_skills = on_list_official_skills;
  context.list_skill_hub_sources = on_list_skill_hub_sources;
  context.search_skill_hub = on_search_skill_hub;
  context.preview_skill_hub = on_preview_skill_hub;
  context.scan_skill_hub = on_scan_skill_hub;
  context.export_session = on_export_session;
  context.import_sessions = on_import_sessions;
  context.add_profile = on_add_profile;
  context.remove_profile = on_remove_profile;
  context.activate_profile = on_activate_profile;
  context.get_profile_soul = on_get_profile_soul;
  context.set_profile_soul = on_set_profile_soul;
  context.export_profile = on_export_profile;
  context.import_profile = on_import_profile;
  context.open_session = on_select_session;
  context.set_theme = [](ThemeMode) {};
  context.get_pref = get_pref;
  context.set_pref = set_pref;

  QWidget *content = BuildDataPage(page_, context, body_);
  if (content == nullptr) {
    auto *label = new QLabel(
        QStringLiteral("This workspace page is not ported yet."), body_);
    label->setObjectName(QStringLiteral("settingDescription"));
    body_layout_->addWidget(label);
  } else {
    body_layout_->addWidget(content);
  }

  const auto it = snapshot_.pages.constFind(page_);
  if (it != snapshot_.pages.constEnd()) {
    if (it->loading) {
      status_->setText(QStringLiteral("Loading from gateway…"));
    } else if (!it->error.isEmpty()) {
      status_->setText(QStringLiteral("error: %1").arg(it->error));
    } else if (!it->notice.isEmpty()) {
      status_->setText(it->notice);
    } else {
      status_->clear();
    }
  } else {
    status_->setText(QStringLiteral("Not loaded yet."));
  }
}

QString PagesView::TitleFor(const QString &id) {
  if (id == QStringLiteral("capabilities")) {
    return QStringLiteral("Capabilities");
  }
  if (id == QStringLiteral("messaging")) {
    return QStringLiteral("Messaging");
  }
  if (id == QStringLiteral("artifacts")) {
    return QStringLiteral("Artifacts");
  }
  if (id == QStringLiteral("profiles")) {
    return QStringLiteral("Bots");
  }
  if (id == QStringLiteral("webhooks")) {
    return QStringLiteral("Webhooks");
  }
  if (id == QStringLiteral("cron")) {
    return QStringLiteral("Scheduled tasks");
  }
  if (id == QStringLiteral("agents")) {
    return QStringLiteral("Agents");
  }
  if (id == QStringLiteral("starmap")) {
    return QStringLiteral("Star map");
  }
  return id;
}

bool PagesView::IsOpen() const { return isVisible(); }

void PagesView::paintEvent(QPaintEvent *event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.fillRect(rect(), theme_.background);
}

void PagesView::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  scroll_->setGeometry(scroll_->geometry());
}

} // namespace hermes::ui
