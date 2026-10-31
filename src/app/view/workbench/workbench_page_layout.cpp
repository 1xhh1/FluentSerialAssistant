#include "app/view/workbench/workbench_page_internal.h"

using namespace FluentQt;
using namespace WorkbenchPagePrivate;

QWidget *WorkbenchPage::createWorkbench()
{
    auto *workbench = new QWidget(this);
    auto *root = new QHBoxLayout(workbench);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(10);

    m_sideScroll = new ScrollArea(workbench);
    m_sideScroll->setFixedWidth(SidePanelWidth);
    m_sideScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    if (m_sideScroll->horizontalFluentScrollBar()) {
        m_sideScroll->horizontalFluentScrollBar()->setForceHidden(true);
    }

    m_sidePanel = new QWidget(m_sideScroll);
    auto *sideLayout = new QVBoxLayout(m_sidePanel);
    sideLayout->setContentsMargins(0, 0, 0, 0);
    sideLayout->setSpacing(10);
    // Keep the scroll content size in step with animated card heights.
    sideLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);
    sideLayout->addWidget(createConnectionSection());

    // Everything beyond the connection setup lives in the toolbox window so
    // the sidebar stays readable: the terminal and the two core cards.
    m_toolboxWindow = new ToolboxWindow(this);
    connect(m_toolboxWindow, &ToolboxWindow::toolChanged, this, [](const QString &key) {
        AppSettings settings;
        settings.setValue(QStringLiteral("workbench/currentTool"), key);
    });
    m_toolboxWindow->addTool(QStringLiteral("receive"), AppI18n::text("接收设置"), FluentIcon::Download,
                             embedToolCard(createReceiveSettingsSection()));
    m_toolboxWindow->addTool(QStringLiteral("protocol"), AppI18n::text("协议模板"), FluentIcon::Code,
                             embedToolCard(createProtocolTemplateSection()));
    m_toolboxWindow->addTool(QStringLiteral("send"), AppI18n::text("发送设置"), FluentIcon::Send,
                             embedToolCard(createSendSettingsSection()));
    m_toolboxWindow->addTool(QStringLiteral("modbus"), QStringLiteral("Modbus"), FluentIcon::Wifi,
                             embedToolCard(createModbusSection()));
    m_toolboxWindow->addTool(QStringLiteral("packets"), AppI18n::text("常用包"), FluentIcon::Library,
                             embedToolCard(createPacketSection()));
    m_toolboxWindow->addTool(QStringLiteral("macros"), AppI18n::text("宏命令"), FluentIcon::Play,
                             embedToolCard(createMacroSection()));
    m_toolboxWindow->addTool(QStringLiteral("scripts"), AppI18n::text("脚本插件"), FluentIcon::DeveloperTools,
                             embedToolCard(createScriptSection()));
    m_toolboxWindow->addTool(QStringLiteral("autoReply"), AppI18n::text("自动应答"), FluentIcon::Feedback,
                             embedToolCard(createAutoReplySection()));
    m_toolboxWindow->addTool(QStringLiteral("fileSend"), AppI18n::text("文件发送"), FluentIcon::Folder,
                             embedToolCard(createFileSendSection()));
    m_toolboxWindow->addTool(QStringLiteral("rttMemory"), AppI18n::text("RTT 内存"), FluentIcon::Edit,
                             embedToolCard(m_rttMemoryPanel));
    m_toolboxWindow->switchTool(
        AppSettings().value(QStringLiteral("workbench/currentTool"), QStringLiteral("receive")).toString());

    // The toolbox entry sits with the connection setup instead of the terminal
    // header, which is already full of view toggles.
    m_toolboxButton = new PushButton(icon(FluentIcon::DeveloperTools), AppI18n::text("工具箱"), m_sidePanel);
    m_toolboxButton->setObjectName(QStringLiteral("toolboxButton"));
    m_toolboxButton->setAccessibleName(AppI18n::text("工具箱"));
    AppUi::setFluentToolTip(m_toolboxButton,
                            AppI18n::text("工具箱：接收/发送设置、协议模板、Modbus、常用包、宏命令、脚本、自动应答、文件发送"));
    m_toolboxButton->setFixedHeight(CompactControlHeight + 8);
    connect(m_toolboxButton, &PushButton::clicked, this, [this]() { showToolboxWindow(); });
    sideLayout->addWidget(m_toolboxButton);

    sideLayout->addStretch(1);
    m_sideScroll->setWidget(m_sidePanel);
    m_sideScroll->setWidgetResizable(true);
    installSidePanelWheelFilters(m_sidePanel);

    auto *terminalPanel = new QWidget(workbench);
    auto *terminalLayout = new QVBoxLayout(terminalPanel);
    terminalLayout->setContentsMargins(0, 0, 0, 0);
    terminalLayout->setSpacing(10);
    terminalLayout->addWidget(createTerminalSection(), 1);
    terminalLayout->addWidget(createSendSection());

    root->addWidget(m_sideScroll);
    root->addWidget(terminalPanel, 1);

    return workbench;
}

void WorkbenchPage::installSidePanelWheelFilters(QWidget *root)
{
    if (!root) {
        return;
    }

    root->installEventFilter(this);
    const auto widgets = root->findChildren<QWidget *>();
    for (QWidget *widget : widgets) {
        widget->installEventFilter(this);
    }
}

QWidget *WorkbenchPage::createCheckRow(const QStringList &labels, const QList<CheckBox **> &targets, QWidget *parent)
{
    auto *rowWidget = new QWidget(parent);
    auto *row = new QHBoxLayout(rowWidget);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(10);

    const int count = qMin(labels.size(), targets.size());
    for (int i = 0; i < count; ++i) {
        auto *check = new CheckBox(labels.at(i), rowWidget);
        check->setMinimumWidth(0);
        check->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        *targets.at(i) = check;
        row->addWidget(check);
    }
    row->addStretch(1);
    return rowWidget;
}

void WorkbenchPage::showToolboxWindow(const QString &key)
{
    if (!m_toolboxWindow) {
        return;
    }
    if (!key.isEmpty()) {
        m_toolboxWindow->switchTool(key);
    }
    m_toolboxWindow->show();
    m_toolboxWindow->raise();
    m_toolboxWindow->activateWindow();
}

QWidget *WorkbenchPage::embedToolCard(QWidget *card)
{
    // Tool cards are ExpandSettingCards; strip their header and collapse
    // chrome so the panel shows pure content, permanently expanded.
    if (auto *expandCard = qobject_cast<ExpandSettingCard *>(card)) {
        if (expandCard->card()) {
            expandCard->card()->setFixedHeight(0);
            expandCard->card()->hide();
        }
        expandCard->setExpanded(true);
        // The view is already sized to the full content; adopt it right away so
        // the panel does not start out as a sliver before the animation ends.
        if (QWidget *view = expandCard->view()) {
            expandCard->setFixedHeight(view->height());
        }
    }
    return card;
}
