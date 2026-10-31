#include "app/core/app_i18n.h"
#include "app/debug/jlink_commander.h"
#include "app/view/jlink_settings_dialog.h"
#include "app/view/protocol_template_window.h"
#include "app/view/workbench/workbench_page_internal.h"

#include <QtCore/QRegularExpression>
#include <QtCore/QTimer>
#include <QtGui/QRegularExpressionValidator>

using namespace FluentQt;
using namespace WorkbenchPagePrivate;

QWidget *WorkbenchPage::createConnectionSection()
{
    auto *section = new ExpandSettingCard(FluentIcon::Connect, AppI18n::text("连接"), QString(), this);
    auto *root = cardBody(section);

    m_linkTypeCombo = new ComboBox(section);
    makeCompactControl(m_linkTypeCombo);
    m_linkTypeCombo->addItem(AppI18n::text("串口"), QIcon(), QStringLiteral("serial"));
    m_linkTypeCombo->addItem(QStringLiteral("TCP ") + AppI18n::text("服务器"), QIcon(), QStringLiteral("tcp-server"));
    m_linkTypeCombo->addItem(QStringLiteral("TCP ") + AppI18n::text("客户端"), QIcon(), QStringLiteral("tcp-client"));
    m_linkTypeCombo->addItem(QStringLiteral("UDP"), QIcon(), QStringLiteral("udp"));
    m_linkTypeCombo->addItem(QStringLiteral("J-Link RTT"), QIcon(), QStringLiteral("rtt"));
    m_linkTypeCombo->setCurrentIndex(0);
    addFormRow(root, AppI18n::text("传输"), m_linkTypeCombo);

    m_serialLinkRows = new QWidget(section);
    auto *serialLayout = new QVBoxLayout(m_serialLinkRows);
    serialLayout->setContentsMargins(0, 0, 0, 0);
    serialLayout->setSpacing(6);

    m_portCombo = new ComboBox(section);
    makeCompactControl(m_portCombo);
    m_portCombo->setPlaceholderText(AppI18n::text("选择端口"));
    m_refreshButton = new TransparentToolButton(icon(FluentIcon::Sync), section);
    AppUi::setFluentToolTip(m_refreshButton, AppI18n::text("刷新端口"));
    m_refreshButton->setIconSize(QSize(16, 16));
    m_refreshButton->setFixedSize(CompactControlHeight, CompactControlHeight);
    addFormRow(serialLayout, AppI18n::text("端口"), m_portCombo, m_refreshButton);

    m_baudCombo = new EditableComboBox(section);
    makeCompactControl(m_baudCombo);
    m_baudCombo->addItems(commonBaudRateTexts());
    m_baudCombo->setCurrentText(QStringLiteral("115200"));
    addFormRow(serialLayout, AppI18n::text("波特率"), m_baudCombo);

    m_dataBitsCombo = new ComboBox(section);
    makeCompactControl(m_dataBitsCombo);
    m_dataBitsCombo->addItems({QStringLiteral("5"), QStringLiteral("6"), QStringLiteral("7"), QStringLiteral("8")});
    m_dataBitsCombo->setCurrentText(QStringLiteral("8"));
    addFormRow(serialLayout, AppI18n::text("数据位"), m_dataBitsCombo);

    m_parityCombo = new ComboBox(section);
    makeCompactControl(m_parityCombo);
    m_parityCombo->addItem(AppI18n::text("无校验"), QIcon(), static_cast<int>(QSerialPort::NoParity));
    m_parityCombo->addItem(AppI18n::text("偶校验"), QIcon(), static_cast<int>(QSerialPort::EvenParity));
    m_parityCombo->addItem(AppI18n::text("奇校验"), QIcon(), static_cast<int>(QSerialPort::OddParity));
    m_parityCombo->addItem(AppI18n::text("空格"), QIcon(), static_cast<int>(QSerialPort::SpaceParity));
    m_parityCombo->addItem(AppI18n::text("标记"), QIcon(), static_cast<int>(QSerialPort::MarkParity));
    m_parityCombo->setCurrentIndex(0);
    addFormRow(serialLayout, AppI18n::text("校验位"), m_parityCombo);

    m_stopBitsCombo = new ComboBox(section);
    makeCompactControl(m_stopBitsCombo);
    m_stopBitsCombo->addItem(AppI18n::text("1 停止位"), QIcon(), static_cast<int>(QSerialPort::OneStop));
    m_stopBitsCombo->addItem(AppI18n::text("1.5 停止位"), QIcon(), static_cast<int>(QSerialPort::OneAndHalfStop));
    m_stopBitsCombo->addItem(AppI18n::text("2 停止位"), QIcon(), static_cast<int>(QSerialPort::TwoStop));
    m_stopBitsCombo->setCurrentIndex(0);
    addFormRow(serialLayout, AppI18n::text("停止位"), m_stopBitsCombo);

    m_flowControlCombo = new ComboBox(section);
    makeCompactControl(m_flowControlCombo);
    m_flowControlCombo->addItem(AppI18n::text("无流控"), QIcon(), static_cast<int>(QSerialPort::NoFlowControl));
    m_flowControlCombo->addItem(AppI18n::text("硬件流控"), QIcon(), static_cast<int>(QSerialPort::HardwareControl));
    m_flowControlCombo->addItem(AppI18n::text("软件流控"), QIcon(), static_cast<int>(QSerialPort::SoftwareControl));
    m_flowControlCombo->setCurrentIndex(0);
    addFormRow(serialLayout, AppI18n::text("流控"), m_flowControlCombo);

    serialLayout->addWidget(createCheckRow({QStringLiteral("RTS"), QStringLiteral("DTR"), AppI18n::text("启动连接")},
                                           {&m_rtsCheck, &m_dtrCheck, &m_autoOpenCheck}, section));
    root->addWidget(m_serialLinkRows);

    m_networkLinkRows = new QWidget(section);
    auto *networkLayout = new QVBoxLayout(m_networkLinkRows);
    networkLayout->setContentsMargins(0, 0, 0, 0);
    networkLayout->setSpacing(6);
    m_hostEdit = new LineEdit(section);
    m_hostEdit->setPlaceholderText(QStringLiteral("127.0.0.1"));
    m_hostEdit->setObjectName(QStringLiteral("linkHostEdit"));
    addFormRow(networkLayout, AppI18n::text("地址"), m_hostEdit);
    m_portEdit = new LineEdit(section);
    m_portEdit->setPlaceholderText(QStringLiteral("502"));
    m_portEdit->setObjectName(QStringLiteral("linkPortEdit"));
    m_portEdit->setValidator(
        new QRegularExpressionValidator(QRegularExpression(QStringLiteral("\\d{1,5}")), m_portEdit));
    addFormRow(networkLayout, AppI18n::text("端口"), m_portEdit);
    root->addWidget(m_networkLinkRows);

    // J-Link RTT: a summary chip with the gear dialog (Synex's 硬件配置 bar)
    // plus the SWD memory inspector. The RTT stream feeds the terminal
    // exactly like a serial port would.
    m_rttLinkRows = new QWidget(section);
    auto *rttLayout = new QVBoxLayout(m_rttLinkRows);
    rttLayout->setContentsMargins(0, 0, 0, 0);
    rttLayout->setSpacing(6);
    auto *rttSummaryRow = new QHBoxLayout;
    rttSummaryRow->setSpacing(8);
    m_rttSummaryLabel = new CaptionLabel(QString(), section);
    m_rttSummaryLabel->setObjectName(QStringLiteral("rttSummaryLabel"));
    rttSummaryRow->addWidget(m_rttSummaryLabel, 1);
    m_rttSettingsButton = new TransparentToolButton(icon(FluentIcon::Setting), section);
    m_rttSettingsButton->setObjectName(QStringLiteral("rttSettingsButton"));
    m_rttSettingsButton->setAccessibleName(AppI18n::text("J-Link RTT 参数"));
    AppUi::setFluentToolTip(m_rttSettingsButton, AppI18n::text("J-Link RTT 参数"));
    rttSummaryRow->addWidget(m_rttSettingsButton);
    m_rttMemoryButton = new TransparentToolButton(icon(FluentIcon::Edit), section);
    m_rttMemoryButton->setObjectName(QStringLiteral("rttMemoryButton"));
    m_rttMemoryButton->setAccessibleName(AppI18n::text("RTT 内存读写"));
    AppUi::setFluentToolTip(m_rttMemoryButton, AppI18n::text("RTT 内存读写"));
    rttSummaryRow->addWidget(m_rttMemoryButton);
    rttLayout->addLayout(rttSummaryRow);

    // The memory inspector lives in the toolbox window; the card only keeps
    // the entry point so the sidebar stays compact.
    m_rttMemoryPanel = new QWidget(this);
    auto *rttMemoryLayout = new QVBoxLayout(m_rttMemoryPanel);
    rttMemoryLayout->setContentsMargins(0, 0, 0, 0);
    rttMemoryLayout->setSpacing(8);

    m_rttAddressEdit = new LineEdit(m_rttMemoryPanel);
    m_rttAddressEdit->setObjectName(QStringLiteral("rttAddressEdit"));
    m_rttAddressEdit->setPlaceholderText(QStringLiteral("0x20000000"));
    addGroupHeading(rttMemoryLayout, AppI18n::text("读取"), AppI18n::text("填地址与字节数，从目标内存读出 HEX。"));
    addFormRow(rttMemoryLayout, AppI18n::text("内存地址"), m_rttAddressEdit);
    m_rttSizeEdit = new LineEdit(m_rttMemoryPanel);
    m_rttSizeEdit->setObjectName(QStringLiteral("rttSizeEdit"));
    m_rttSizeEdit->setText(QStringLiteral("64"));
    m_rttReadButton = new PushButton(icon(FluentIcon::Download), AppI18n::text("读取"), m_rttMemoryPanel);
    m_rttReadButton->setObjectName(QStringLiteral("rttReadButton"));
    setButtonRowControlPolicy(m_rttReadButton);
    addFormRow(rttMemoryLayout, AppI18n::text("字节数"), m_rttSizeEdit, m_rttReadButton);
    m_rttWriteEdit = new LineEdit(m_rttMemoryPanel);
    m_rttWriteEdit->setObjectName(QStringLiteral("rttWriteEdit"));
    m_rttWriteEdit->setPlaceholderText(AppI18n::text("写入字节，HEX 格式，如：00 10 20 30"));
    addGroupHeading(rttMemoryLayout, AppI18n::text("写入"), AppI18n::text("写入 HEX 数据，可回读校验。"));
    addFormRow(rttMemoryLayout, AppI18n::text("写入内容"), m_rttWriteEdit);
    m_rttWriteButton = new PushButton(icon(FluentIcon::SaveAs), AppI18n::text("写入并校验"), m_rttMemoryPanel);
    m_rttWriteButton->setObjectName(QStringLiteral("rttWriteButton"));
    setButtonRowControlPolicy(m_rttWriteButton);
    rttMemoryLayout->addWidget(m_rttWriteButton);
    m_rttStatusLabel = new CaptionLabel(AppI18n::text("尚未读取"), m_rttMemoryPanel);
    m_rttStatusLabel->setTextColor(QColor(96, 96, 96), QColor(180, 180, 180));
    m_rttStatusLabel->setWordWrap(true);
    rttMemoryLayout->addWidget(m_rttStatusLabel);
    m_rttMemoryView = new PlainTextEdit(m_rttMemoryPanel);
    m_rttMemoryView->setObjectName(QStringLiteral("rttMemoryView"));
    m_rttMemoryView->setReadOnly(true);
    m_rttMemoryView->setFont(QFont(QStringLiteral("Consolas"), 9));
    m_rttMemoryView->setFixedHeight(96);
    rttMemoryLayout->addWidget(m_rttMemoryView);
    rttMemoryLayout->addStretch(1);
    root->addWidget(m_rttLinkRows);

    m_connectButton = new PrimaryPushButton(icon(FluentIcon::Connect), AppI18n::text("连接"), section);
    root->addWidget(m_connectButton);

    const auto refreshRttSummary = [this]() {
        AppSettings settings;
        const QString device =
            settings.value(QStringLiteral("debugger/device"), QStringLiteral("S32K144")).toString();
        const int speed = settings.value(QStringLiteral("debugger/speed"), 4000).toInt();
        const int channel = settings.value(QStringLiteral("debugger/channel"), 0).toInt();
        if (m_rttSummaryLabel) {
            m_rttSummaryLabel->setText(
                QStringLiteral("%1 · %2 kHz · CH%3").arg(device).arg(speed).arg(channel));
            m_rttSummaryLabel->setToolTip(
                QStringLiteral("%1 | SWD %2 kHz | RTT CH%3").arg(device).arg(speed).arg(channel));
        }
    };
    const auto updateLinkTypeUi = [this, refreshRttSummary]() {
        const QString type = m_linkTypeCombo->currentData().toString();
        const bool serial = type == QStringLiteral("serial");
        const bool rtt = type == QStringLiteral("rtt");
        m_serialLinkRows->setVisible(serial);
        m_networkLinkRows->setVisible(!serial && !rtt);
        m_rttLinkRows->setVisible(rtt);
        m_rttReadButton->setEnabled(!m_link.isOpen());
        m_rttWriteButton->setEnabled(!m_link.isOpen());
        if (rtt) {
            refreshRttSummary();
        }
        if (m_settingsRestored) {
            // During construction this would clobber the saved transport
            // before restoreSettings had a chance to read it.
            AppSettings settings;
            settings.setValue(QStringLiteral("link/type"), type);
            settings.setValue(QStringLiteral("link/host"), m_hostEdit->text().trimmed());
            settings.setValue(QStringLiteral("link/port"), m_portEdit->text().trimmed().toInt());
        }
    };
    connect(m_linkTypeCombo, &ComboBox::currentIndexChanged, this, [updateLinkTypeUi](int) { updateLinkTypeUi(); });
    connect(m_hostEdit, &LineEdit::textChanged, this, [updateLinkTypeUi](const QString &) { updateLinkTypeUi(); });
    connect(m_portEdit, &LineEdit::textChanged, this, [updateLinkTypeUi](const QString &) { updateLinkTypeUi(); });
    connect(m_rttMemoryButton, &TransparentToolButton::clicked, this,
            [this]() { showToolboxWindow(QStringLiteral("rttMemory")); });
    connect(m_rttSettingsButton, &TransparentToolButton::clicked, this, [this, refreshRttSummary]() {
        AppSettings settings;
        JLinkSettingsDialog::Settings current;
        current.device = settings.value(QStringLiteral("debugger/device"), QStringLiteral("S32K144")).toString();
        current.speedKhz = settings.value(QStringLiteral("debugger/speed"), 4000).toInt();
        current.upChannel = settings.value(QStringLiteral("debugger/channel"), 0).toInt();
        current.dllPath = settings.value(QStringLiteral("debugger/dllPath"), JLinkCommander::defaultDllPath()).toString();

        JLinkSettingsDialog dialog(current, window());
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        const JLinkSettingsDialog::Settings changed = dialog.settings();
        settings.setValue(QStringLiteral("debugger/device"), changed.device);
        settings.setValue(QStringLiteral("debugger/speed"), changed.speedKhz);
        settings.setValue(QStringLiteral("debugger/channel"), changed.upChannel);
        settings.setValue(QStringLiteral("debugger/dllPath"), changed.dllPath);
        settings.setValue(QStringLiteral("debugger/probeConnection"), changed.probeConnection);
        settings.setValue(QStringLiteral("debugger/probeParameter"), changed.probeParameter);
        settings.setValue(QStringLiteral("debugger/interface"), changed.interfaceType);
        settings.setValue(QStringLiteral("debugger/scriptFile"), changed.scriptFile);
        refreshRttSummary();
    });
    updateLinkTypeUi();

    connect(m_refreshButton, &ToolButton::clicked, this, &WorkbenchPage::refreshPorts);
    connect(m_connectButton, &PrimaryPushButton::clicked, this, &WorkbenchPage::onConnectClicked);
    connect(m_rtsCheck, &CheckBox::toggled, this, [this](bool checked) {
        if (m_link.isOpen()) {
            m_link.setRequestToSend(checked);
        }
    });
    connect(m_dtrCheck, &CheckBox::toggled, this, [this](bool checked) {
        if (m_link.isOpen()) {
            m_link.setDataTerminalReady(checked);
        }
    });
    connect(m_autoOpenCheck, &CheckBox::toggled, this, [](bool checked) {
        AppSettings settings;
        settings.setValue(QStringLiteral("serial/autoOpen"), checked);
    });

    makeCollapsibleCard(section, QStringLiteral("connection"), true);
    return section;
}

QWidget *WorkbenchPage::createQuickReceiveSection()
{
    auto *section = new ExpandSettingCard(FluentIcon::Save, AppI18n::text("接收与保存"), QString(), this);
    auto *root = cardBody(section);

    // Display switches: the toggles used while watching the stream.
    addGroupHeading(root, AppI18n::text("显示"));
    auto *receiveOptions = new QWidget(section);
    auto *receiveOptionsGrid = new QGridLayout(receiveOptions);
    receiveOptionsGrid->setContentsMargins(0, 0, 0, 0);
    receiveOptionsGrid->setHorizontalSpacing(12);
    receiveOptionsGrid->setVerticalSpacing(6);
    receiveOptionsGrid->setColumnStretch(0, 1);
    receiveOptionsGrid->setColumnStretch(1, 1);
    receiveOptionsGrid->setColumnStretch(2, 1);
    m_autoScrollCheck = new CheckBox(AppI18n::text("自动滚动"), receiveOptions);
    m_timestampCheck = new CheckBox(AppI18n::text("时间戳"), receiveOptions);
    m_pauseCheck = new CheckBox(AppI18n::text("暂停显示"), receiveOptions);
    for (CheckBox *check : {m_autoScrollCheck, m_timestampCheck, m_pauseCheck}) {
        check->setMinimumWidth(0);
        check->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    receiveOptionsGrid->addWidget(m_autoScrollCheck, 0, 0);
    receiveOptionsGrid->addWidget(m_timestampCheck, 0, 1);
    receiveOptionsGrid->addWidget(m_pauseCheck, 0, 2);
    root->addWidget(receiveOptions);

    m_timestampFormatCombo = new ComboBox(section);
    m_timestampFormatCombo->setObjectName(QStringLiteral("timestampFormatCombo"));
    m_timestampFormatCombo->addItem(AppI18n::text("时:分:秒"), QIcon(), QStringLiteral("HH:mm:ss"));
    m_timestampFormatCombo->addItem(AppI18n::text("时:分:秒.毫秒"), QIcon(), QStringLiteral("HH:mm:ss.zzz"));
    m_timestampFormatCombo->addItem(AppI18n::text("年/月/日 时:分:秒"), QIcon(), QStringLiteral("yyyy/MM/dd HH:mm:ss"));
    m_timestampFormatCombo->addItem(AppI18n::text("年/月/日 时:分:秒.毫秒"), QIcon(),
                                    QStringLiteral("yyyy/MM/dd HH:mm:ss.zzz"));
    makeCompactControl(m_timestampFormatCombo);
    m_timestampFormatCombo->setEnabled(false);
    addFormRow(root, AppI18n::text("时间戳格式"), m_timestampFormatCombo);

    // Saving: capture the raw stream to a file, or export the receive area.
    addGroupHeading(root, AppI18n::text("保存与导出"),
                    AppI18n::text("把收到的原始字节持续写入文件，或把接收区记录导出为 TXT / CSV / BIN。"));

    auto *saveReceiveRow = new QWidget(section);
    auto *saveReceiveLayout = new QHBoxLayout(saveReceiveRow);
    saveReceiveLayout->setContentsMargins(0, 0, 0, 0);
    saveReceiveLayout->setSpacing(8);
    m_saveReceiveCheck = new CheckBox(AppI18n::text("保存接收"), saveReceiveRow);
    m_saveReceiveCheck->setMinimumWidth(0);
    m_saveReceiveCheck->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    AppUi::setFluentToolTip(m_saveReceiveCheck,
                            AppI18n::text("连接后把收到的原始字节持续写入导出目录下的 serial_rx_时间.bin"));
    saveReceiveLayout->addWidget(m_saveReceiveCheck);
    saveReceiveLayout->addStretch(1);
    root->addWidget(saveReceiveRow);

    m_receiveCaptureLabel = new CaptionLabel(AppI18n::text("接收保存未启用"), section);
    m_receiveCaptureLabel->setTextColor(QColor(96, 96, 96), QColor(180, 180, 180));
    m_receiveCaptureLabel->setWordWrap(true);
    root->addWidget(m_receiveCaptureLabel);

    auto *exportRow = new QHBoxLayout;
    exportRow->setSpacing(8);
    m_exportTxtButton = new PushButton(icon(FluentIcon::Document), QStringLiteral("TXT"), section);
    m_exportCsvButton = new PushButton(icon(FluentIcon::SaveAs), QStringLiteral("CSV"), section);
    m_exportBinButton = new PushButton(icon(FluentIcon::Save), QStringLiteral("BIN"), section);
    AppUi::setFluentToolTip(m_exportTxtButton, AppI18n::text("把接收区记录导出为 TXT 文本"));
    AppUi::setFluentToolTip(m_exportCsvButton, AppI18n::text("把接收区记录导出为 CSV（时间/方向/数据列）"));
    AppUi::setFluentToolTip(m_exportBinButton, AppI18n::text("把接收区原始字节导出为 BIN"));
    setButtonRowControlPolicy(m_exportTxtButton);
    setButtonRowControlPolicy(m_exportCsvButton);
    setButtonRowControlPolicy(m_exportBinButton);
    exportRow->addWidget(m_exportTxtButton);
    exportRow->addWidget(m_exportCsvButton);
    exportRow->addWidget(m_exportBinButton);
    root->addLayout(exportRow);

    auto *counterRow = new QHBoxLayout;
    counterRow->setSpacing(8);
    m_resetCountersButton = new PushButton(icon(FluentIcon::Cancel), AppI18n::text("计数清零"), section);
    m_clearButton = new PushButton(icon(FluentIcon::Broom), AppI18n::text("清空接收区"), section);
    AppUi::setFluentToolTip(m_resetCountersButton, AppI18n::text("把收发字节与帧计数归零"));
    AppUi::setFluentToolTip(m_clearButton, AppI18n::text("清空接收区显示与计数，不影响已保存的文件"));
    setButtonRowControlPolicy(m_resetCountersButton);
    setButtonRowControlPolicy(m_clearButton);
    counterRow->addWidget(m_resetCountersButton);
    counterRow->addWidget(m_clearButton);
    root->addLayout(counterRow);

    connect(m_autoScrollCheck, &CheckBox::toggled, this, [this](bool checked) {
        AppSettings settings;
        settings.setValue(QStringLiteral("receive/autoScroll"), checked);
        if (checked) {
            renderTerminal();
        }
    });
    connect(m_timestampCheck, &CheckBox::toggled, this, [this](bool checked) {
        AppSettings settings;
        settings.setValue(QStringLiteral("receive/timestamp"), checked);
        if (m_timestampFormatCombo) {
            m_timestampFormatCombo->setEnabled(checked);
        }
        if (m_timestampButton) {
            m_timestampButton->setChecked(checked);
        }
        renderTerminal();
    });
    connect(m_timestampFormatCombo, &ComboBox::currentIndexChanged, this, [this](int) {
        AppSettings settings;
        settings.setValue(QStringLiteral("receive/timestampFormat"), terminalTimestampFormat());
        renderTerminal();
    });
    connect(m_pauseCheck, &CheckBox::toggled, this, [this](bool checked) {
        if (!checked) {
            flushPendingLines();
        }
    });
    connect(m_saveReceiveCheck, &CheckBox::toggled, this, &WorkbenchPage::updateReceiveCapture);
    connect(m_resetCountersButton, &PushButton::clicked, this, &WorkbenchPage::resetCounters);
    connect(m_clearButton, &PushButton::clicked, this, &WorkbenchPage::clearTerminal);
    connect(m_exportTxtButton, &PushButton::clicked, this, [this]() { exportRecords(ExportFormat::Txt); });
    connect(m_exportCsvButton, &PushButton::clicked, this, [this]() { exportRecords(ExportFormat::Csv); });
    connect(m_exportBinButton, &PushButton::clicked, this, [this]() { exportRecords(ExportFormat::Bin); });

    makeCollapsibleCard(section, QStringLiteral("quickReceive"), true);
    return section;
}

QWidget *WorkbenchPage::createReceiveSettingsSection()
{
    auto *section = new ExpandSettingCard(FluentIcon::Download, AppI18n::text("接收设置"), QString(), this);
    auto *root = cardBody(section);

    addGroupHeading(root, AppI18n::text("显示"), AppI18n::text("接收内容在终端里的解析方式；显示开关与保存导出在左侧栏。"));
    m_displayModeSegment = new SegmentedWidget(section);
    m_displayModeSegment->addItem(QStringLiteral("text"), AppI18n::text("文本"));
    m_displayModeSegment->addItem(QStringLiteral("hex"), QStringLiteral("HEX"));
    m_displayModeSegment->addItem(QStringLiteral("mixed"), AppI18n::text("混合"));
    m_displayModeSegment->setCurrentItem(QStringLiteral("text"));
    root->addWidget(m_displayModeSegment);

    m_receiveEncodingCombo = new ComboBox(section);
    addEncodingOptions(m_receiveEncodingCombo);
    makeCompactControl(m_receiveEncodingCombo);
    addFormRow(root, AppI18n::text("编码"), m_receiveEncodingCombo);

    addGroupHeading(root, AppI18n::text("断帧"), AppI18n::text("把连续的字节流切成一条条记录：按帧尾等边界、固定长度或静默超时。"));
    m_frameModeCombo = new ComboBox(section);
    addFrameModeOptions(m_frameModeCombo);
    makeCompactControl(m_frameModeCombo);
    m_autoFrameBreakCheck = new CheckBox(AppI18n::text("启用"), section);
    m_autoFrameBreakCheck->setMinimumWidth(0);
    m_autoFrameBreakCheck->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    addFormRow(root, AppI18n::text("切分方式"), m_frameModeCombo, m_autoFrameBreakCheck);

    m_framePatternEdit = new LineEdit(section);
    m_framePatternEdit->setPlaceholderText(AppI18n::text("HEX 边界，如 AA 55 或 0D 0A"));
    makeCompactControl(m_framePatternEdit);
    addFormRow(root, AppI18n::text("帧尾/边界"), m_framePatternEdit);

    m_frameFixedLengthEdit = createNumberEdit(section, 8, 1, 65536);
    addFormRow(root, AppI18n::text("固定长度"), m_frameFixedLengthEdit, unitLabel(QStringLiteral("B"), section));

    m_frameBreakIntervalEdit = createNumberEdit(section, 20, 1, 60000);
    addFormRow(root, AppI18n::text("静默超时"), m_frameBreakIntervalEdit, unitLabel(QStringLiteral("ms"), section));

    addGroupHeading(root, AppI18n::text("日志"), AppI18n::text("把接收记录持续写入文件；超过滚动上限自动换新文件。"));
    m_autoLogFormatCombo = new ComboBox(section);
    m_autoLogFormatCombo->addItem(QStringLiteral("TXT"), QIcon(), QStringLiteral("txt"));
    m_autoLogFormatCombo->addItem(QStringLiteral("CSV"), QIcon(), QStringLiteral("csv"));
    m_autoLogFormatCombo->addItem(QStringLiteral("BIN"), QIcon(), QStringLiteral("bin"));
    makeCompactControl(m_autoLogFormatCombo);
    m_autoLogCheck = new CheckBox(AppI18n::text("自动日志"), section);
    m_autoLogCheck->setMinimumWidth(0);
    m_autoLogCheck->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    addFormRow(root, AppI18n::text("日志格式"), m_autoLogFormatCombo, m_autoLogCheck);

    m_autoLogMaxSizeEdit = createNumberEdit(section, 16, 1, 4096);
    addFormRow(root, AppI18n::text("日志滚动"), m_autoLogMaxSizeEdit, unitLabel(QStringLiteral("MB"), section));

    m_autoLogStatusLabel = new CaptionLabel(AppI18n::text("自动日志未启用"), section);
    m_autoLogStatusLabel->setTextColor(QColor(96, 96, 96), QColor(180, 180, 180));
    m_autoLogStatusLabel->setWordWrap(true);
    root->addWidget(m_autoLogStatusLabel);

    connect(m_autoLogCheck, &CheckBox::toggled, this, &WorkbenchPage::updateAutoLog);
    connect(m_autoLogFormatCombo, &ComboBox::currentIndexChanged, this, [this](int) {
        AppSettings settings;
        settings.setValue(QStringLiteral("log/format"), autoLogFormatKey());
        resetAutoLogSession();
        updateAutoLogStatus();
    });
    connect(m_autoLogMaxSizeEdit, &LineEdit::textChanged, this, [this](const QString &) {
        AppSettings settings;
        settings.setValue(QStringLiteral("log/maxFileSizeMb"), numberEditValue(m_autoLogMaxSizeEdit, 16, 1, 4096));
        if (m_autoLogFile.isOpen() && m_autoLogCurrentSize >= autoLogMaxFileBytes()) {
            resetAutoLogSession();
        }
        updateAutoLogStatus();
    });

    connect(m_displayModeSegment, &SegmentedWidget::currentItemChanged, this, [this](const QString &routeKey) {
        AppSettings settings;
        settings.setValue(QStringLiteral("terminal/displayMode"), routeKey);
        updateReceiveModeButton();
        renderTerminal();
    });
    connect(m_receiveEncodingCombo, &ComboBox::currentIndexChanged, this, [this](int) {
        flushRxFrameBuffer();
        AppSettings settings;
        settings.setValue(QStringLiteral("receive/encoding"), receiveEncodingKey());
    });
    connect(m_frameModeCombo, &ComboBox::currentIndexChanged, this, [this](int) {
        flushRxFrameBuffer();
        AppSettings settings;
        settings.setValue(QStringLiteral("receive/frameMode"), frameBreakModeKey());
        updateFrameControlState();
    });
    connect(m_framePatternEdit, &LineEdit::textChanged, this, [this](const QString &text) {
        flushRxFrameBuffer();
        AppSettings settings;
        settings.setValue(QStringLiteral("receive/framePattern"), text);
    });
    connect(m_frameFixedLengthEdit, &LineEdit::textChanged, this, [this](const QString &) {
        flushRxFrameBuffer();
        AppSettings settings;
        settings.setValue(QStringLiteral("receive/frameFixedLength"),
                          numberEditValue(m_frameFixedLengthEdit, 8, 1, 65536));
    });
    connect(m_frameBreakIntervalEdit, &LineEdit::textChanged, this, [this](const QString &) {
        AppSettings settings;
        settings.setValue(QStringLiteral("receive/frameBreakMs"),
                          numberEditValue(m_frameBreakIntervalEdit, 20, 1, 60000));
    });
    connect(m_autoFrameBreakCheck, &CheckBox::toggled, this, [this](bool checked) {
        flushRxFrameBuffer();
        AppSettings settings;
        settings.setValue(QStringLiteral("receive/autoFrameBreak"), checked);
        if (!checked) {
            m_lastRxTimestamp = QDateTime();
        }
        updateFrameControlState();
    });

    makeCollapsibleCard(section, QStringLiteral("receive"), true);
    return section;
}

QWidget *WorkbenchPage::createProtocolTemplateSection()
{
    auto *section = new ExpandSettingCard(FluentIcon::Code, AppI18n::text("协议模板"), QString(), this);
    auto *root = cardBody(section);

    m_protocolTemplateCombo = new ComboBox(section);
    m_protocolTemplateCombo->setObjectName(QStringLiteral("protocolTemplateCombo"));
    makeCompactControl(m_protocolTemplateCombo);
    m_protocolEnabledCheck = new CheckBox(AppI18n::text("启用"), section);
    m_protocolEnabledCheck->setObjectName(QStringLiteral("protocolTemplateEnabledCheck"));
    m_protocolEnabledCheck->setMinimumWidth(0);
    m_protocolEnabledCheck->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    addGroupHeading(root, AppI18n::text("选择与启用"), AppI18n::text("启用后收发按模板拆分与校验；点“编辑协议”调整帧结构。"));
    addFormRow(root, AppI18n::text("模板"), m_protocolTemplateCombo, m_protocolEnabledCheck);

    auto *editProtocolButton = new PushButton(icon(FluentIcon::Edit), AppI18n::text("编辑协议"), section);
    editProtocolButton->setObjectName(QStringLiteral("editProtocolButton"));
    root->addWidget(editProtocolButton);
    connect(editProtocolButton, &PushButton::clicked, this, &WorkbenchPage::showProtocolTemplateWindow);

    m_protocolStatusLabel = new CaptionLabel(AppI18n::text("协议模板未启用"), section);
    m_protocolStatusLabel->setTextColor(QColor(96, 96, 96), QColor(180, 180, 180));
    m_protocolStatusLabel->setWordWrap(true);
    root->addWidget(m_protocolStatusLabel);

    auto *editor = new QWidget;
    editor->setObjectName(QStringLiteral("protocolTemplateEditor"));
    auto *editorRoot = new QVBoxLayout(editor);
    editorRoot->setContentsMargins(0, 0, 12, 0);
    editorRoot->setSpacing(10);
    const auto group = [editor, editorRoot](const QString &title) {
        auto *box = new SimpleCardWidget(editor);
        box->setBorderRadius(8);
        box->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
        auto *layout = qobject_cast<QVBoxLayout *>(box->layout());
        layout->setContentsMargins(12, 12, 12, 12);
        layout->setSpacing(8);
        auto *heading = new BodyLabel(title, box);
        QFont font = heading->font();
        font.setBold(true);
        heading->setFont(font);
        layout->addWidget(heading);
        editorRoot->addWidget(box);
        return layout;
    };
    const auto pairedFields = [](QVBoxLayout *layout, const QString &firstLabel, QWidget *first,
                                 const QString &secondLabel, QWidget *second) {
        auto *fields = new QGridLayout;
        fields->setHorizontalSpacing(12);
        fields->setVerticalSpacing(4);
        fields->setColumnStretch(0, 1);
        fields->setColumnStretch(1, 1);
        fields->addWidget(new CaptionLabel(firstLabel, layout->parentWidget()), 0, 0);
        fields->addWidget(new CaptionLabel(secondLabel, layout->parentWidget()), 0, 1);
        fields->addWidget(first, 1, 0);
        fields->addWidget(second, 1, 1);
        layout->addLayout(fields);
    };

    auto *basicGroup = group(AppI18n::text("名称、帧头与帧尾"));
    auto *editorTemplateCombo = new ComboBox(editor);
    editorTemplateCombo->setObjectName(QStringLiteral("protocolEditorTemplateCombo"));
    editorTemplateCombo->setPlaceholderText(AppI18n::text("未保存的协议"));
    makeCompactControl(editorTemplateCombo);
    connect(editorTemplateCombo, &ComboBox::currentIndexChanged, this, [this](int index) {
        if (index < 0) {
            return;
        }
        const int previousIndex = m_protocolTemplateCombo->currentIndex();
        m_protocolTemplateCombo->setCurrentIndex(index);
        if (previousIndex == index) {
            applyProtocolTemplate(index);
        }
    });
    auto *newTemplateButton = new PushButton(AppI18n::text("新建"), editor);
    newTemplateButton->setObjectName(QStringLiteral("protocolTemplateNewButton"));
    newTemplateButton->setFixedSize(64, CompactControlHeight);
    addFormRow(basicGroup, AppI18n::text("模板"), editorTemplateCombo, newTemplateButton);
    m_protocolNameEdit = new LineEdit(editor);
    m_protocolNameEdit->setObjectName(QStringLiteral("protocolTemplateNameEdit"));
    m_protocolNameEdit->setPlaceholderText(AppI18n::text("模板名称"));
    makeCompactControl(m_protocolNameEdit);
    addFormRow(basicGroup, AppI18n::text("名称"), m_protocolNameEdit);

    m_protocolHeaderEdit = new LineEdit(editor);
    m_protocolHeaderEdit->setObjectName(QStringLiteral("protocolTemplateHeaderEdit"));
    m_protocolHeaderEdit->setPlaceholderText(AppI18n::text("帧头 HEX，如 AA 55"));
    makeCompactControl(m_protocolHeaderEdit);
    addFormRow(basicGroup, AppI18n::text("帧头"), m_protocolHeaderEdit);

    m_protocolTrailerEdit = new LineEdit(editor);
    m_protocolTrailerEdit->setObjectName(QStringLiteral("protocolTemplateTrailerEdit"));
    m_protocolTrailerEdit->setPlaceholderText(AppI18n::text("帧尾 HEX，如 0D 0A，可留空"));
    makeCompactControl(m_protocolTrailerEdit);
    addFormRow(basicGroup, AppI18n::text("帧尾"), m_protocolTrailerEdit);

    auto *lengthGroup = group(AppI18n::text("长度字段"));
    m_protocolLengthOffsetEdit = createNumberEdit(editor, 2, 0, 65535);
    m_protocolLengthOffsetEdit->setObjectName(QStringLiteral("protocolTemplateLengthOffsetEdit"));
    m_protocolLengthSizeCombo = new ComboBox(editor);
    m_protocolLengthSizeCombo->setObjectName(QStringLiteral("protocolTemplateLengthSizeCombo"));
    for (int size : {0, 1, 2, 4}) {
        m_protocolLengthSizeCombo->addItem(QStringLiteral("%1 B").arg(size), QIcon(), size);
    }
    m_protocolLengthSizeCombo->setFixedSize(76, CompactControlHeight);
    addFormRow(lengthGroup, AppI18n::text("字节偏移"), m_protocolLengthOffsetEdit, m_protocolLengthSizeCombo);

    m_protocolLengthModeCombo = new ComboBox(editor);
    m_protocolLengthModeCombo->setObjectName(QStringLiteral("protocolTemplateLengthModeCombo"));
    m_protocolLengthModeCombo->addItem(AppProtocol::lengthModeLabel(AppProtocol::LengthMode::PayloadLength), QIcon(),
                                       AppProtocol::lengthModeKey(AppProtocol::LengthMode::PayloadLength));
    m_protocolLengthModeCombo->addItem(AppProtocol::lengthModeLabel(AppProtocol::LengthMode::FrameLength), QIcon(),
                                       AppProtocol::lengthModeKey(AppProtocol::LengthMode::FrameLength));
    makeCompactControl(m_protocolLengthModeCombo);
    addFormRow(lengthGroup, AppI18n::text("含义"), m_protocolLengthModeCombo);

    m_protocolLengthByteOrderCombo = new ComboBox(editor);
    m_protocolLengthByteOrderCombo->setObjectName(QStringLiteral("protocolTemplateLengthByteOrderCombo"));
    addChecksumByteOrderOptions(m_protocolLengthByteOrderCombo);
    makeCompactControl(m_protocolLengthByteOrderCombo);
    addFormRow(lengthGroup, AppI18n::text("字节序"), m_protocolLengthByteOrderCombo);
    auto *lengthHint = new CaptionLabel(AppI18n::text("没有长度字段时选择 0 B。"), editor);
    lengthHint->setWordWrap(true);
    lengthGroup->addWidget(lengthHint);

    auto *commandGroup = group(AppI18n::text("命令字段"));
    m_protocolCommandOffsetEdit = createNumberEdit(editor, 3, 0, 65535);
    m_protocolCommandOffsetEdit->setObjectName(QStringLiteral("protocolTemplateCommandOffsetEdit"));
    m_protocolCommandSizeEdit = createNumberEdit(editor, 1, 0, 256);
    m_protocolCommandSizeEdit->setObjectName(QStringLiteral("protocolTemplateCommandSizeEdit"));
    pairedFields(commandGroup, AppI18n::text("字节偏移"), m_protocolCommandOffsetEdit, AppI18n::text("字节数"),
                 m_protocolCommandSizeEdit);

    auto *payloadGroup = group(AppI18n::text("载荷字段"));
    m_protocolPayloadOffsetEdit = createNumberEdit(editor, 4, 0, 65535);
    m_protocolPayloadOffsetEdit->setObjectName(QStringLiteral("protocolTemplatePayloadOffsetEdit"));
    m_protocolPayloadLengthEdit = createNumberEdit(editor, 0, 0, 65535);
    m_protocolPayloadLengthEdit->setObjectName(QStringLiteral("protocolTemplatePayloadLengthEdit"));
    m_protocolPayloadLengthEdit->setAccessibleDescription(AppI18n::text("0 表示按长度字段自动计算"));
    pairedFields(payloadGroup, AppI18n::text("字节偏移"), m_protocolPayloadOffsetEdit, AppI18n::text("固定长度"),
                 m_protocolPayloadLengthEdit);
    auto *payloadHint = new CaptionLabel(AppI18n::text("固定长度填 0，按长度字段或剩余字节自动计算。"), editor);
    payloadHint->setWordWrap(true);
    payloadGroup->addWidget(payloadHint);

    auto *checksumGroup = group(AppI18n::text("校验字段"));
    m_protocolChecksumAlgorithmCombo = new ComboBox(editor);
    m_protocolChecksumAlgorithmCombo->setObjectName(QStringLiteral("protocolTemplateChecksumAlgorithmCombo"));
    m_protocolChecksumAlgorithmCombo->addItem(AppI18n::text("无校验"), QIcon(), AppProtocol::checksumNoneKey());
    addChecksumAlgorithmOptions(m_protocolChecksumAlgorithmCombo);
    makeCompactControl(m_protocolChecksumAlgorithmCombo);
    addFormRow(checksumGroup, AppI18n::text("算法"), m_protocolChecksumAlgorithmCombo);

    m_protocolChecksumByteOrderCombo = new ComboBox(editor);
    m_protocolChecksumByteOrderCombo->setObjectName(QStringLiteral("protocolTemplateChecksumByteOrderCombo"));
    addChecksumByteOrderOptions(m_protocolChecksumByteOrderCombo);
    makeCompactControl(m_protocolChecksumByteOrderCombo);
    addFormRow(checksumGroup, AppI18n::text("字节序"), m_protocolChecksumByteOrderCombo);

    m_protocolChecksumStartEdit = createNumberEdit(editor, 0, 0, 65535);
    m_protocolChecksumStartEdit->setObjectName(QStringLiteral("protocolTemplateChecksumStartEdit"));
    addFormRow(checksumGroup, AppI18n::text("起始偏移"), m_protocolChecksumStartEdit);
    auto *checksumHint =
        new CaptionLabel(AppI18n::text("校验位于载荷之后、帧尾之前，计算范围从起始偏移到校验前一字节。"), editor);
    checksumHint->setWordWrap(true);
    checksumGroup->addWidget(checksumHint);

    auto *actionBar = new QWidget(editor);
    actionBar->setObjectName(QStringLiteral("protocolTemplateActionBar"));
    auto *buttonRow = new QHBoxLayout(actionBar);
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->setSpacing(8);
    m_protocolSaveButton = new PrimaryPushButton(icon(FluentIcon::Save), AppI18n::text("保存"), actionBar);
    m_protocolSaveButton->setObjectName(QStringLiteral("protocolTemplateSaveButton"));
    m_protocolDeleteButton = new PushButton(icon(FluentIcon::Delete), AppI18n::text("删除"), actionBar);
    m_protocolDeleteButton->setObjectName(QStringLiteral("protocolTemplateDeleteButton"));
    m_protocolExampleButton = new PushButton(icon(FluentIcon::Info), AppI18n::text("添加示例"), actionBar);
    m_protocolExampleButton->setObjectName(QStringLiteral("protocolTemplateExampleButton"));
    m_protocolSaveButton->setFixedHeight(CompactControlHeight);
    m_protocolDeleteButton->setFixedHeight(CompactControlHeight);
    m_protocolExampleButton->setFixedHeight(CompactControlHeight);
    buttonRow->addWidget(m_protocolDeleteButton);
    buttonRow->addWidget(m_protocolExampleButton);
    buttonRow->addStretch();
    buttonRow->addWidget(m_protocolSaveButton);
    editorRoot->addWidget(actionBar);
    editorRoot->addStretch();
    m_protocolTemplateWindow = new ProtocolTemplateWindow(editor, this);

    connect(newTemplateButton, &PushButton::clicked, this, [this]() {
        QString name = AppI18n::text("新协议");
        for (int suffix = 1; suffix <= m_protocolTemplates.size() + 1; ++suffix) {
            name = AppI18n::text("新协议 %1").arg(suffix);
            bool exists = false;
            for (const AppProtocol::ProtocolTemplate &item : m_protocolTemplates) {
                exists = exists || item.name == name;
            }
            if (!exists) {
                break;
            }
        }
        // Start from a template's defaults so no option carries over from the
        // previously selected template.
        AppProtocol::ProtocolTemplate blank;
        blank.name = name;
        blank.lengthOffset = 0;
        blank.lengthSize = 0;
        blank.commandOffset = 0;
        blank.commandSize = 0;
        blank.payloadOffset = 0;
        fillProtocolTemplateEditor(blank);
        m_protocolTemplateWindow->setSampleHex(QString());
        updateProtocolTemplateActionState();
        m_protocolNameEdit->setFocus();
        m_protocolNameEdit->selectAll();
    });

    const auto markDirty = [this]() { updateProtocolTemplateActionState(); };
    connect(m_protocolTemplateCombo, &ComboBox::currentIndexChanged, this, &WorkbenchPage::applyProtocolTemplate);
    connect(m_protocolEnabledCheck, &CheckBox::toggled, this, [this](bool) {
        AppSettings settings;
        settings.setValue(QStringLiteral("protocolTemplate/enabled"), m_protocolEnabledCheck->isChecked());
        updateProtocolTemplateActionState();
    });
    connect(m_protocolNameEdit, &LineEdit::textChanged, this, markDirty);
    connect(m_protocolHeaderEdit, &LineEdit::textChanged, this, markDirty);
    connect(m_protocolTrailerEdit, &LineEdit::textChanged, this, markDirty);
    connect(m_protocolLengthOffsetEdit, &LineEdit::textChanged, this, markDirty);
    connect(m_protocolLengthSizeCombo, &ComboBox::currentIndexChanged, this, markDirty);
    connect(m_protocolLengthModeCombo, &ComboBox::currentIndexChanged, this, markDirty);
    connect(m_protocolLengthByteOrderCombo, &ComboBox::currentIndexChanged, this, markDirty);
    connect(m_protocolCommandOffsetEdit, &LineEdit::textChanged, this, markDirty);
    connect(m_protocolCommandSizeEdit, &LineEdit::textChanged, this, markDirty);
    connect(m_protocolPayloadOffsetEdit, &LineEdit::textChanged, this, markDirty);
    connect(m_protocolPayloadLengthEdit, &LineEdit::textChanged, this, markDirty);
    connect(m_protocolChecksumAlgorithmCombo, &ComboBox::currentIndexChanged, this, markDirty);
    connect(m_protocolChecksumByteOrderCombo, &ComboBox::currentIndexChanged, this, markDirty);
    connect(m_protocolChecksumStartEdit, &LineEdit::textChanged, this, markDirty);
    connect(m_protocolSaveButton, &PushButton::clicked, this, &WorkbenchPage::saveCurrentProtocolTemplate);
    connect(m_protocolDeleteButton, &PushButton::clicked, this, &WorkbenchPage::deleteCurrentProtocolTemplate);
    connect(m_protocolExampleButton, &PushButton::clicked, this, &WorkbenchPage::insertProtocolTemplateExample);

    updateProtocolTemplateActionState();
    makeCollapsibleCard(section, QStringLiteral("protocolTemplates"));
    return section;
}

QWidget *WorkbenchPage::createSendSettingsSection()
{
    auto *section = new ExpandSettingCard(FluentIcon::Send, AppI18n::text("发送设置"), QString(), this);
    auto *root = cardBody(section);

    m_lineEndingCombo = new ComboBox(section);
    makeCompactControl(m_lineEndingCombo);
    m_lineEndingCombo->addItem(QStringLiteral("None"), QIcon(), QStringLiteral("none"));
    m_lineEndingCombo->addItem(QStringLiteral("CR"), QIcon(), QStringLiteral("cr"));
    m_lineEndingCombo->addItem(QStringLiteral("LF"), QIcon(), QStringLiteral("lf"));
    m_lineEndingCombo->addItem(QStringLiteral("CRLF"), QIcon(), QStringLiteral("crlf"));
    m_lineEndingCombo->setCurrentIndex(0);
    addGroupHeading(root, AppI18n::text("文本"), AppI18n::text("换行与编码决定发送内容如何变成字节。"));
    addFormRow(root, AppI18n::text("换行"), m_lineEndingCombo);

    m_sendEncodingCombo = new ComboBox(section);
    addEncodingOptions(m_sendEncodingCombo);
    makeCompactControl(m_sendEncodingCombo);
    addFormRow(root, AppI18n::text("编码"), m_sendEncodingCombo);

    m_checksumAlgorithmCombo = new ComboBox(section);
    addChecksumAlgorithmOptions(m_checksumAlgorithmCombo);
    makeCompactControl(m_checksumAlgorithmCombo);
    m_checksumCalcButton = new PushButton(icon(FluentIcon::Asterisk), AppI18n::text("计算"), section);
    m_checksumCalcButton->setFixedHeight(CompactControlHeight);
    addGroupHeading(root, AppI18n::text("校验"), AppI18n::text("在末尾自动追加校验码；跳过字节用于帧头/尾不参与校验。"));
    addFormRow(root, AppI18n::text("校验"), m_checksumAlgorithmCombo, m_checksumCalcButton);

    m_checksumByteOrderCombo = new ComboBox(section);
    addChecksumByteOrderOptions(m_checksumByteOrderCombo);
    makeCompactControl(m_checksumByteOrderCombo);
    m_checksumAppendCheck = new CheckBox(AppI18n::text("自动追加"), section);
    m_checksumAppendCheck->setMinimumWidth(0);
    m_checksumAppendCheck->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    addFormRow(root, AppI18n::text("字节序"), m_checksumByteOrderCombo, m_checksumAppendCheck);

    // Bytes skipped at the end follow the checksum, and the line ending
    // follows them.
    auto *checksumRange = new QWidget(section);
    auto *checksumRangeLayout = new QHBoxLayout(checksumRange);
    checksumRangeLayout->setContentsMargins(0, 0, 0, 0);
    checksumRangeLayout->setSpacing(6);
    m_checksumSkipLeadingEdit = createNumberEdit(checksumRange, 0, 0, MaxChecksumSkipBytes);
    m_checksumSkipLeadingEdit->setObjectName(QStringLiteral("checksumSkipLeadingEdit"));
    m_checksumSkipTrailingEdit = createNumberEdit(checksumRange, 0, 0, MaxChecksumSkipBytes);
    m_checksumSkipTrailingEdit->setObjectName(QStringLiteral("checksumSkipTrailingEdit"));
    const QString checksumRangeTip =
        AppI18n::text("开头和末尾跳过的字节不参与校验，校验码插在末尾跳过的字节之前；换行符始终在最后。");
    const auto checksumRangeLabel = [checksumRange](const QString &text) {
        auto *label = new CaptionLabel(text, checksumRange);
        label->setFixedHeight(CompactControlHeight);
        return label;
    };
    checksumRangeLayout->addWidget(checksumRangeLabel(AppI18n::text("开头")));
    checksumRangeLayout->addWidget(m_checksumSkipLeadingEdit, 1);
    checksumRangeLayout->addWidget(checksumRangeLabel(AppI18n::text("末尾")));
    checksumRangeLayout->addWidget(m_checksumSkipTrailingEdit, 1);
    checksumRange->setToolTip(checksumRangeTip);
    m_checksumSkipLeadingEdit->setToolTip(checksumRangeTip);
    m_checksumSkipTrailingEdit->setToolTip(checksumRangeTip);
    addFormRow(root, AppI18n::text("跳过字节"), checksumRange);

    m_checksumResultLabel = new CaptionLabel(AppI18n::text("校验未计算"), section);
    m_checksumResultLabel->setTextColor(QColor(96, 96, 96), QColor(180, 180, 180));
    m_checksumResultLabel->setWordWrap(true);
    root->addWidget(m_checksumResultLabel);

    addGroupHeading(root, AppI18n::text("发送行为"), AppI18n::text("HEX 发送、显示发送内容、回车解析与收发配色。"));
    auto *sendOptions = new QWidget(section);
    auto *sendOptionsGrid = new QGridLayout(sendOptions);
    sendOptionsGrid->setContentsMargins(0, 0, 0, 0);
    sendOptionsGrid->setHorizontalSpacing(12);
    sendOptionsGrid->setVerticalSpacing(6);
    sendOptionsGrid->setColumnStretch(0, 1);
    sendOptionsGrid->setColumnStretch(1, 1);
    m_hexSendCheck = new CheckBox(AppI18n::text("HEX 发送"), sendOptions);
    m_escapeParseCheck = new CheckBox(AppI18n::text("转义符解析"), sendOptions);
    m_escapeParseCheck->setToolTip(AppI18n::text("文本发送时解析 \\r \\n \\t \\0 \\\\ \\xHH 转义序列"));
    m_showTxCheck = new CheckBox(AppI18n::text("显示发送字符串"), sendOptions);
    m_loopCheck = new CheckBox(AppI18n::text("定时发送"), sendOptions);
    m_autoReconnectCheck = new CheckBox(AppI18n::text("自动重连"), sendOptions);
    for (CheckBox *check : {m_hexSendCheck, m_escapeParseCheck, m_showTxCheck, m_loopCheck, m_autoReconnectCheck}) {
        check->setMinimumWidth(0);
        check->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    m_txColorButton = new ColorPickerButton(defaultTxColor(), AppI18n::text("TX 颜色"), sendOptions);
    m_txColorButton->setFixedSize(32, CompactControlHeight);
    m_rxColorButton = new ColorPickerButton(defaultRxColor(), AppI18n::text("RX 颜色"), sendOptions);
    m_rxColorButton->setFixedSize(32, CompactControlHeight);
    sendOptionsGrid->addWidget(m_hexSendCheck, 0, 0);
    sendOptionsGrid->addWidget(m_showTxCheck, 0, 1);
    sendOptionsGrid->addWidget(m_txColorButton, 0, 2);
    sendOptionsGrid->addWidget(m_rxColorButton, 0, 3);
    sendOptionsGrid->addWidget(m_loopCheck, 1, 0);
    sendOptionsGrid->addWidget(m_autoReconnectCheck, 1, 1);
    sendOptionsGrid->addWidget(m_escapeParseCheck, 2, 0);
    root->addWidget(sendOptions);

    m_historyCombo = new ComboBox(section);
    makeCompactControl(m_historyCombo);
    addGroupHeading(root, AppI18n::text("定时与历史"), AppI18n::text("定时循环发送与发送历史回填。"));
    addFormRow(root, AppI18n::text("历史"), m_historyCombo);

    m_loopIntervalEdit = createNumberEdit(section, 1000, 10, 600000);
    m_loopIntervalEdit->setEnabled(false);
    addFormRow(root, AppI18n::text("间隔"), m_loopIntervalEdit, unitLabel(QStringLiteral("ms"), section));

    connect(m_historyCombo, &ComboBox::currentIndexChanged, this, &WorkbenchPage::applyHistoryItem);
    connect(m_sendEncodingCombo, &ComboBox::currentIndexChanged, this, [this](int) {
        AppSettings settings;
        settings.setValue(QStringLiteral("send/encoding"), sendEncodingKey());
    });
    connect(m_checksumAlgorithmCombo, &ComboBox::currentIndexChanged, this, [this](int) {
        AppSettings settings;
        settings.setValue(QStringLiteral("checksum/algorithm"), checksumAlgorithmKey());
    });
    connect(m_checksumByteOrderCombo, &ComboBox::currentIndexChanged, this, [this](int) {
        AppSettings settings;
        settings.setValue(QStringLiteral("checksum/byteOrder"), AppChecksum::byteOrderKey(checksumByteOrder()));
    });
    connect(m_checksumAppendCheck, &CheckBox::toggled, this, [](bool checked) {
        AppSettings settings;
        settings.setValue(QStringLiteral("checksum/autoAppend"), checked);
    });
    connect(m_checksumSkipLeadingEdit, &LineEdit::textChanged, this, [this](const QString &) {
        AppSettings settings;
        settings.setValue(QStringLiteral("checksum/skipLeading"),
                          numberEditValue(m_checksumSkipLeadingEdit, 0, 0, MaxChecksumSkipBytes));
    });
    connect(m_checksumSkipTrailingEdit, &LineEdit::textChanged, this, [this](const QString &) {
        AppSettings settings;
        settings.setValue(QStringLiteral("checksum/skipTrailing"),
                          numberEditValue(m_checksumSkipTrailingEdit, 0, 0, MaxChecksumSkipBytes));
    });
    connect(m_checksumCalcButton, &PushButton::clicked, this, &WorkbenchPage::calculateChecksumForCurrentPayload);
    connect(m_hexSendCheck, &CheckBox::toggled, this, [this](bool checked) {
        AppSettings settings;
        settings.setValue(QStringLiteral("send/mode"), checked ? QStringLiteral("hex") : QStringLiteral("text"));
        updateSendModeButton();
    });
    connect(m_escapeParseCheck, &CheckBox::toggled, this, [](bool checked) {
        AppSettings settings;
        settings.setValue(QStringLiteral("send/escapeParse"), checked);
    });
    connect(m_showTxCheck, &CheckBox::toggled, this, [this](bool checked) {
        AppSettings settings;
        settings.setValue(QStringLiteral("send/showTx"), checked);
        m_txColorButton->setEnabled(checked);
        renderTerminal();
    });
    connect(m_txColorButton, &ColorPickerButton::colorChanged, this, [this](const QColor &color) {
        AppSettings settings;
        settings.setValue(QStringLiteral("send/txColor"), color.name(QColor::HexRgb));
        renderTerminal();
    });
    connect(m_rxColorButton, &ColorPickerButton::colorChanged, this, [this](const QColor &color) {
        AppSettings settings;
        settings.setValue(QStringLiteral("send/rxColor"), color.name(QColor::HexRgb));
        renderTerminal();
    });
    connect(m_loopCheck, &CheckBox::toggled, this, &WorkbenchPage::onLoopChanged);
    connect(m_autoReconnectCheck, &CheckBox::toggled, this, [](bool checked) {
        AppSettings settings;
        settings.setValue(QStringLiteral("serial/autoReconnect"), checked);
    });
    connect(m_loopIntervalEdit, &LineEdit::textChanged, this, [this](const QString &) {
        if (m_loopTimer.isActive()) {
            m_loopTimer.start(numberEditValue(m_loopIntervalEdit, 1000, 10, 600000));
        }
    });

    makeCollapsibleCard(section, QStringLiteral("send"));
    return section;
}

QWidget *WorkbenchPage::createPacketSection()
{
    auto *section = new ExpandSettingCard(FluentIcon::Library, AppI18n::text("常用包"), QString(), this);
    auto *root = cardBody(section);

    m_packetNameEdit = new LineEdit(section);
    m_packetNameEdit->setPlaceholderText(AppI18n::text("包名称"));
    makeCompactControl(m_packetNameEdit);
    addGroupHeading(root, AppI18n::text("基本信息"), AppI18n::text("名称/分组/备注用于在列表中识别这条报文。"));
    addFormRow(root, AppI18n::text("名称"), m_packetNameEdit);

    m_packetGroupEdit = new LineEdit(section);
    m_packetGroupEdit->setPlaceholderText(AppI18n::text("分组"));
    makeCompactControl(m_packetGroupEdit);
    m_packetEnabledCheck = new CheckBox(AppI18n::text("启用"), section);
    m_packetEnabledCheck->setChecked(true);
    m_packetEnabledCheck->setFixedHeight(CompactControlHeight);
    addFormRow(root, AppI18n::text("分组"), m_packetGroupEdit, m_packetEnabledCheck);

    m_packetNoteEdit = new LineEdit(section);
    m_packetNoteEdit->setPlaceholderText(AppI18n::text("备注"));
    makeCompactControl(m_packetNoteEdit);
    addFormRow(root, AppI18n::text("备注"), m_packetNoteEdit);    addGroupHeading(root, AppI18n::text("发送内容"), AppI18n::text("模式、换行与报文内容；快捷键可在任意界面直接发送。"));


    auto *modeRow = new QHBoxLayout;
    modeRow->setSpacing(8);
    m_packetModeCombo = new ComboBox(section);
    m_packetModeCombo->addItem(AppI18n::text("文本"), QIcon(), QStringLiteral("text"));
    m_packetModeCombo->addItem(QStringLiteral("HEX"), QIcon(), QStringLiteral("hex"));
    m_packetModeCombo->addItem(AppI18n::text("模板"), QIcon(), QStringLiteral("template"));
    m_packetModeCombo->setCurrentIndex(0);
    makeCompactControl(m_packetModeCombo);
    m_packetLineEndingCombo = new ComboBox(section);
    m_packetLineEndingCombo->addItem(QStringLiteral("None"), QIcon(), QStringLiteral("none"));
    m_packetLineEndingCombo->addItem(QStringLiteral("CR"), QIcon(), QStringLiteral("cr"));
    m_packetLineEndingCombo->addItem(QStringLiteral("LF"), QIcon(), QStringLiteral("lf"));
    m_packetLineEndingCombo->addItem(QStringLiteral("CRLF"), QIcon(), QStringLiteral("crlf"));
    m_packetLineEndingCombo->setCurrentIndex(0);
    makeCompactControl(m_packetLineEndingCombo);
    modeRow->addWidget(m_packetModeCombo);
    modeRow->addWidget(m_packetLineEndingCombo);
    root->addLayout(modeRow);

    m_packetPayloadEdit = new PlainTextEdit(section);
    m_packetPayloadEdit->setPlaceholderText(AppI18n::text("发送内容"));
    m_packetPayloadEdit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_packetPayloadEdit->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    m_packetPayloadEdit->setFixedHeight(72);
    m_packetPayloadEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    root->addWidget(m_packetPayloadEdit);

    m_packetHotkeyEdit = new QKeySequenceEdit(section);
    m_packetHotkeyEdit->setObjectName(QStringLiteral("packetHotkeyEdit"));
    m_packetHotkeyEdit->setClearButtonEnabled(true);
    m_packetHotkeyEdit->setFixedHeight(CompactControlHeight);
    m_packetHotkeyEdit->setToolTip(AppI18n::text("按下快捷键立即发送该常用包"));
    addFormRow(root, AppI18n::text("快捷键"), m_packetHotkeyEdit);

    addGroupHeading(root, AppI18n::text("报文列表"), AppI18n::text("选中后可用下方按钮编辑、排序或发送。"));
    m_packetList = new ListWidget(section);
    m_packetList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_packetList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_packetList->setMinimumHeight(132);
    m_packetList->setMaximumHeight(220);
    m_packetList->setBorderRadius(8);
    root->addWidget(m_packetList);

    auto *moveRow = new QHBoxLayout;
    moveRow->setSpacing(8);
    m_packetUpButton = new TransparentToolButton(icon(FluentIcon::Up), section);
    AppUi::setFluentToolTip(m_packetUpButton, AppI18n::text("上移"));
    m_packetDownButton = new TransparentToolButton(icon(FluentIcon::Download), section);
    AppUi::setFluentToolTip(m_packetDownButton, AppI18n::text("下移"));
    for (ToolButton *button : {m_packetUpButton, m_packetDownButton}) {
        button->setFixedSize(CompactControlHeight, CompactControlHeight);
        button->setIconSize(QSize(16, 16));
    }
    m_packetSaveButton = new PushButton(icon(FluentIcon::Save), AppI18n::text("保存"), section);
    m_packetLoadButton = new PushButton(icon(FluentIcon::Edit), AppI18n::text("填入发送"), section);
    setButtonRowControlPolicy(m_packetSaveButton);
    setButtonRowControlPolicy(m_packetLoadButton);
    moveRow->addWidget(m_packetUpButton);
    moveRow->addWidget(m_packetDownButton);
    moveRow->addWidget(m_packetSaveButton);
    moveRow->addWidget(m_packetLoadButton);
    root->addLayout(moveRow);

    addGroupHeading(root, AppI18n::text("导入导出"), AppI18n::text("JSON 导入/导出整包，或把发送区内容存为快捷包。"));
    auto *ioRow = new QHBoxLayout;
    ioRow->setSpacing(8);
    m_packetImportButton = new PushButton(icon(FluentIcon::Download), AppI18n::text("导入 JSON"), section);
    m_packetExportButton = new PushButton(icon(FluentIcon::SaveAs), AppI18n::text("导出 JSON"), section);
    m_packetFromSendButton = new PushButton(icon(FluentIcon::Add), AppI18n::text("存为快捷包"), section);
    setButtonRowControlPolicy(m_packetImportButton);
    setButtonRowControlPolicy(m_packetExportButton);
    setButtonRowControlPolicy(m_packetFromSendButton);
    ioRow->addWidget(m_packetImportButton);
    ioRow->addWidget(m_packetExportButton);
    ioRow->addWidget(m_packetFromSendButton);
    root->addLayout(ioRow);

    addGroupHeading(root, AppI18n::text("发送"), AppI18n::text("发送选中报文、批量发送或删除。"));
    auto *actionRow = new QHBoxLayout;
    actionRow->setSpacing(8);
    m_packetSendButton = new PrimaryPushButton(icon(FluentIcon::Send), AppI18n::text("发送"), section);
    m_packetBatchSendButton = new PushButton(icon(FluentIcon::SendFill), AppI18n::text("批量发送"), section);
    m_packetDeleteButton = new PushButton(icon(FluentIcon::Delete), AppI18n::text("删除"), section);
    setButtonRowControlPolicy(m_packetSendButton);
    setButtonRowControlPolicy(m_packetBatchSendButton);
    setButtonRowControlPolicy(m_packetDeleteButton);
    actionRow->addWidget(m_packetSendButton);
    actionRow->addWidget(m_packetBatchSendButton);
    actionRow->addWidget(m_packetDeleteButton);
    root->addLayout(actionRow);

    connect(m_packetList, &ListWidget::currentRowChanged, this, [this](int row) { applyPacket(row); });
    connect(m_packetList, &ListWidget::itemSelectionChanged, this, &WorkbenchPage::updatePacketActionState);
    connect(m_packetSaveButton, &PushButton::clicked, this, &WorkbenchPage::saveCurrentPacket);
    connect(m_packetLoadButton, &PushButton::clicked, this, [this]() { applyPacket(m_packetList->currentRow()); });
    connect(m_packetDeleteButton, &PushButton::clicked, this, &WorkbenchPage::removeSelectedPacket);
    connect(m_packetSendButton, &PrimaryPushButton::clicked, this, &WorkbenchPage::sendSelectedPacket);
    connect(m_packetBatchSendButton, &PushButton::clicked, this, &WorkbenchPage::sendSelectedPackets);
    connect(m_packetImportButton, &PushButton::clicked, this, &WorkbenchPage::importSendPackets);
    connect(m_packetExportButton, &PushButton::clicked, this, &WorkbenchPage::exportSendPackets);
    connect(m_packetFromSendButton, &PushButton::clicked, this, &WorkbenchPage::saveSendAreaAsPacket);
    connect(m_packetUpButton, &ToolButton::clicked, this, [this]() { moveSelectedPacket(-1); });
    connect(m_packetDownButton, &ToolButton::clicked, this, [this]() { moveSelectedPacket(1); });

    updatePacketTable();
    makeCollapsibleCard(section, QStringLiteral("packets"));
    return section;
}

QWidget *WorkbenchPage::createMacroSection()
{
    auto *section = new ExpandSettingCard(FluentIcon::Play, AppI18n::text("宏命令"), QString(), this);
    auto *root = cardBody(section);

    m_macroNameEdit = new LineEdit(section);
    m_macroNameEdit->setPlaceholderText(AppI18n::text("步骤名称"));
    makeCompactControl(m_macroNameEdit);
    addFormRow(root, AppI18n::text("步骤"), m_macroNameEdit);

    addGroupHeading(root, AppI18n::text("步骤内容"), AppI18n::text("每一步要发送什么：模式与换行。"));
    auto *modeRow = new QHBoxLayout;
    modeRow->setSpacing(8);
    m_macroModeCombo = new ComboBox(section);
    m_macroModeCombo->addItem(AppI18n::text("文本"), QIcon(), QStringLiteral("text"));
    m_macroModeCombo->addItem(QStringLiteral("HEX"), QIcon(), QStringLiteral("hex"));
    makeCompactControl(m_macroModeCombo);
    m_macroLineEndingCombo = new ComboBox(section);
    m_macroLineEndingCombo->addItem(QStringLiteral("None"), QIcon(), QStringLiteral("none"));
    m_macroLineEndingCombo->addItem(QStringLiteral("CR"), QIcon(), QStringLiteral("cr"));
    m_macroLineEndingCombo->addItem(QStringLiteral("LF"), QIcon(), QStringLiteral("lf"));
    m_macroLineEndingCombo->addItem(QStringLiteral("CRLF"), QIcon(), QStringLiteral("crlf"));
    makeCompactControl(m_macroLineEndingCombo);
    modeRow->addWidget(m_macroModeCombo);
    modeRow->addWidget(m_macroLineEndingCombo);
    root->addLayout(modeRow);

    m_macroPayloadEdit = new PlainTextEdit(section);
    m_macroPayloadEdit->setPlaceholderText(AppI18n::text("发送内容"));
    m_macroPayloadEdit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_macroPayloadEdit->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    m_macroPayloadEdit->setFixedHeight(64);
    m_macroPayloadEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    root->addWidget(m_macroPayloadEdit);

    m_macroExpectedEdit = new LineEdit(section);
    m_macroExpectedEdit->setPlaceholderText(AppI18n::text("响应包含，留空不等待"));
    makeCompactControl(m_macroExpectedEdit);
    m_macroResponseModeCombo = new ComboBox(section);
    m_macroResponseModeCombo->addItem(AppI18n::text("文"), QIcon(), QStringLiteral("text"));
    m_macroResponseModeCombo->addItem(QStringLiteral("HEX"), QIcon(), QStringLiteral("hex"));
    m_macroResponseModeCombo->setFixedHeight(CompactControlHeight);
    m_macroResponseModeCombo->setFixedWidth(78);
    addGroupHeading(root, AppI18n::text("响应判定"), AppI18n::text("按预期响应判断该步成败；超时与延时单位为毫秒。"));
    addFormRow(root, AppI18n::text("响应"), m_macroExpectedEdit, m_macroResponseModeCombo);

    m_macroTimeoutEdit = createNumberEdit(section, 1000, 1, 600000);
    addFormRow(root, AppI18n::text("超时"), m_macroTimeoutEdit, unitLabel(QStringLiteral("ms"), section));

    m_macroDelayEdit = createNumberEdit(section, 0, 0, 600000);
    addFormRow(root, AppI18n::text("延时"), m_macroDelayEdit, unitLabel(QStringLiteral("ms"), section));

    addGroupHeading(root, AppI18n::text("步骤列表"), AppI18n::text("上/下调整顺序，保存或填入选中步骤。"));
    m_macroList = new ListWidget(section);
    m_macroList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_macroList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_macroList->setMinimumHeight(132);
    m_macroList->setMaximumHeight(220);
    m_macroList->setBorderRadius(8);
    root->addWidget(m_macroList);

    auto *stepRow = new QHBoxLayout;
    stepRow->setSpacing(8);
    m_macroUpButton = new TransparentToolButton(icon(FluentIcon::Up), section);
    AppUi::setFluentToolTip(m_macroUpButton, AppI18n::text("上移"));
    m_macroDownButton = new TransparentToolButton(icon(FluentIcon::Download), section);
    AppUi::setFluentToolTip(m_macroDownButton, AppI18n::text("下移"));
    for (ToolButton *button : {m_macroUpButton, m_macroDownButton}) {
        button->setFixedSize(CompactControlHeight, CompactControlHeight);
        button->setIconSize(QSize(16, 16));
    }
    m_macroSaveButton = new PushButton(icon(FluentIcon::Save), AppI18n::text("保存"), section);
    m_macroLoadButton = new PushButton(icon(FluentIcon::Edit), AppI18n::text("填入"), section);
    m_macroDeleteButton = new PushButton(icon(FluentIcon::Delete), AppI18n::text("删除"), section);
    setButtonRowControlPolicy(m_macroSaveButton);
    stepRow->addWidget(m_macroUpButton);
    stepRow->addWidget(m_macroDownButton);
    stepRow->addWidget(m_macroSaveButton);
    root->addLayout(stepRow);

    auto *editRow = new QHBoxLayout;
    editRow->setSpacing(8);
    setButtonRowControlPolicy(m_macroLoadButton);
    setButtonRowControlPolicy(m_macroDeleteButton);
    editRow->addWidget(m_macroLoadButton);
    editRow->addWidget(m_macroDeleteButton);
    root->addLayout(editRow);

    m_macroLoopCountEdit = createNumberEdit(section, 1, 1, 100000);
    m_macroAbortOnFailureCheck = new CheckBox(AppI18n::text("失败中止"), section);
    m_macroAbortOnFailureCheck->setChecked(true);
    m_macroAbortOnFailureCheck->setFixedHeight(CompactControlHeight);
    addGroupHeading(root, AppI18n::text("流程与运行"), AppI18n::text("循环次数、失败是否中止，以及运行/停止/导出结果。"));
    addFormRow(root, AppI18n::text("循环"), m_macroLoopCountEdit, m_macroAbortOnFailureCheck);

    auto *actionRow = new QHBoxLayout;
    actionRow->setSpacing(8);
    m_macroRunButton = new PrimaryPushButton(icon(FluentIcon::Play), AppI18n::text("运行"), section);
    m_macroStopButton = new PushButton(icon(FluentIcon::Cancel), AppI18n::text("停止"), section);
    m_macroExportButton = new PushButton(icon(FluentIcon::SaveAs), AppI18n::text("导出结果"), section);
    setButtonRowControlPolicy(m_macroRunButton);
    setButtonRowControlPolicy(m_macroStopButton);
    setButtonRowControlPolicy(m_macroExportButton);
    actionRow->addWidget(m_macroRunButton);
    actionRow->addWidget(m_macroStopButton);
    actionRow->addWidget(m_macroExportButton);
    root->addLayout(actionRow);

    m_macroStatusLabel = new CaptionLabel(AppI18n::text("未运行"), section);
    m_macroStatusLabel->setTextColor(QColor(96, 96, 96), QColor(180, 180, 180));
    m_macroStatusLabel->setWordWrap(true);
    root->addWidget(m_macroStatusLabel);

    connect(m_macroList, &ListWidget::currentRowChanged, this, [this](int row) {
        applyMacroStep(row);
        updateMacroActionState();
    });
    connect(m_macroSaveButton, &PushButton::clicked, this, &WorkbenchPage::saveCurrentMacroStep);
    connect(m_macroLoadButton, &PushButton::clicked, this, [this]() { applyMacroStep(m_macroList->currentRow()); });
    connect(m_macroDeleteButton, &PushButton::clicked, this, &WorkbenchPage::removeSelectedMacroStep);
    connect(m_macroUpButton, &ToolButton::clicked, this, [this]() { moveSelectedMacroStep(-1); });
    connect(m_macroDownButton, &ToolButton::clicked, this, [this]() { moveSelectedMacroStep(1); });
    connect(m_macroRunButton, &PrimaryPushButton::clicked, this, &WorkbenchPage::startMacroSequence);
    connect(m_macroStopButton, &PushButton::clicked, this, [this]() { stopMacroSequence(AppI18n::text("用户停止")); });
    connect(m_macroExportButton, &PushButton::clicked, this, &WorkbenchPage::exportMacroResults);

    updateMacroTable();
    makeCollapsibleCard(section, QStringLiteral("macros"));
    return section;
}

QWidget *WorkbenchPage::createScriptSection()
{
    auto *section = new ExpandSettingCard(FluentIcon::DeveloperTools, AppI18n::text("脚本插件"), QString(), this);
    auto *root = cardBody(section);

    addGroupHeading(root, AppI18n::text("脚本内容"), AppI18n::text("可在编辑器里直接改，或载入/保存/插入示例。"));
    m_scriptEdit = new PlainTextEdit(section);
    m_scriptEdit->setPlaceholderText(AppI18n::text("输入 JavaScript 脚本，使用 serial 对象发送、读取和记录日志"));
    m_scriptEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_scriptEdit->setFixedHeight(150);
    m_scriptEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    root->addWidget(m_scriptEdit);

    auto *fileRow = new QHBoxLayout;
    fileRow->setSpacing(8);
    m_scriptLoadButton = new PushButton(icon(FluentIcon::Folder), AppI18n::text("载入"), section);
    m_scriptSaveButton = new PushButton(icon(FluentIcon::Save), AppI18n::text("保存"), section);
    m_scriptExampleButton = new PushButton(icon(FluentIcon::Info), AppI18n::text("示例"), section);
    setButtonRowControlPolicy(m_scriptLoadButton);
    setButtonRowControlPolicy(m_scriptSaveButton);
    setButtonRowControlPolicy(m_scriptExampleButton);
    fileRow->addWidget(m_scriptLoadButton);
    fileRow->addWidget(m_scriptSaveButton);
    fileRow->addWidget(m_scriptExampleButton);
    root->addLayout(fileRow);

    addGroupHeading(root, AppI18n::text("运行"), AppI18n::text("运行或停止脚本。"));
    auto *actionRow = new QHBoxLayout;
    actionRow->setSpacing(8);
    m_scriptRunButton = new PrimaryPushButton(icon(FluentIcon::Play), AppI18n::text("运行脚本"), section);
    m_scriptStopButton = new PushButton(icon(FluentIcon::Cancel), AppI18n::text("停止"), section);
    m_scriptClearLogButton = new PushButton(icon(FluentIcon::Broom), AppI18n::text("清日志"), section);
    setButtonRowControlPolicy(m_scriptRunButton);
    setButtonRowControlPolicy(m_scriptStopButton);
    setButtonRowControlPolicy(m_scriptClearLogButton);
    actionRow->addWidget(m_scriptRunButton);
    actionRow->addWidget(m_scriptStopButton);
    actionRow->addWidget(m_scriptClearLogButton);
    root->addLayout(actionRow);

    addGroupHeading(root, AppI18n::text("日志"), AppI18n::text("脚本输出与错误显示在这里。"));
    m_scriptLogEdit = new PlainTextEdit(section);
    m_scriptLogEdit->setReadOnly(true);
    m_scriptLogEdit->setPlaceholderText(AppI18n::text("脚本日志"));
    m_scriptLogEdit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_scriptLogEdit->setFixedHeight(96);
    m_scriptLogEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    root->addWidget(m_scriptLogEdit);

    m_scriptStatusLabel = new CaptionLabel(AppI18n::text("未运行"), section);
    m_scriptStatusLabel->setTextColor(QColor(96, 96, 96), QColor(180, 180, 180));
    m_scriptStatusLabel->setWordWrap(true);
    root->addWidget(m_scriptStatusLabel);

    connect(m_scriptLoadButton, &PushButton::clicked, this, &WorkbenchPage::loadScriptFile);
    connect(m_scriptSaveButton, &PushButton::clicked, this, &WorkbenchPage::saveScriptFile);
    connect(m_scriptExampleButton, &PushButton::clicked, this, &WorkbenchPage::insertScriptExample);
    connect(m_scriptRunButton, &PrimaryPushButton::clicked, this, &WorkbenchPage::startScript);
    connect(m_scriptStopButton, &PushButton::clicked, this, &WorkbenchPage::stopScript);
    connect(m_scriptClearLogButton, &PushButton::clicked, this, &WorkbenchPage::clearScriptLog);
    connect(m_scriptEdit, &PlainTextEdit::textChanged, this, &WorkbenchPage::updateScriptActionState);

    updateScriptActionState();
    makeCollapsibleCard(section, QStringLiteral("scripts"));
    return section;
}

QWidget *WorkbenchPage::createAutoReplySection()
{
    auto *section = new ExpandSettingCard(FluentIcon::Feedback, AppI18n::text("自动应答"), QString(), this);
    auto *root = cardBody(section);

    m_autoReplyNameEdit = new LineEdit(section);
    m_autoReplyNameEdit->setPlaceholderText(AppI18n::text("规则名称"));
    makeCompactControl(m_autoReplyNameEdit);
    m_autoReplyEnabledCheck = new CheckBox(AppI18n::text("启用"), section);
    m_autoReplyEnabledCheck->setChecked(true);
    m_autoReplyEnabledCheck->setFixedHeight(CompactControlHeight);
    addGroupHeading(root, AppI18n::text("规则"), AppI18n::text("名称用于列表识别；只有启用后才会参与匹配。"));
    addFormRow(root, AppI18n::text("名称"), m_autoReplyNameEdit, m_autoReplyEnabledCheck);

    m_autoReplyPatternEdit = new LineEdit(section);
    m_autoReplyPatternEdit->setPlaceholderText(AppI18n::text("匹配内容"));
    makeCompactControl(m_autoReplyPatternEdit);
    m_autoReplyMatchModeCombo = new ComboBox(section);
    m_autoReplyMatchModeCombo->addItem(AppI18n::text("文本"), QIcon(), QStringLiteral("text"));
    m_autoReplyMatchModeCombo->addItem(QStringLiteral("HEX"), QIcon(), QStringLiteral("hex"));
    m_autoReplyMatchModeCombo->addItem(AppI18n::text("正则"), QIcon(), QStringLiteral("regex"));
    m_autoReplyMatchModeCombo->setFixedHeight(CompactControlHeight);
    m_autoReplyMatchModeCombo->setFixedWidth(78);
    addGroupHeading(root, AppI18n::text("匹配条件"), AppI18n::text("收到什么内容时触发这条规则。"));
    addFormRow(root, AppI18n::text("匹配"), m_autoReplyPatternEdit, m_autoReplyMatchModeCombo);

    addGroupHeading(root, AppI18n::text("应答内容"), AppI18n::text("回复什么、以什么模式与换行发送，以及延时多久回复。"));
    auto *responseModeRow = new QHBoxLayout;
    responseModeRow->setSpacing(8);
    m_autoReplyResponseModeCombo = new ComboBox(section);
    m_autoReplyResponseModeCombo->addItem(AppI18n::text("文本"), QIcon(), QStringLiteral("text"));
    m_autoReplyResponseModeCombo->addItem(QStringLiteral("HEX"), QIcon(), QStringLiteral("hex"));
    makeCompactControl(m_autoReplyResponseModeCombo);
    m_autoReplyLineEndingCombo = new ComboBox(section);
    m_autoReplyLineEndingCombo->addItem(QStringLiteral("None"), QIcon(), QStringLiteral("none"));
    m_autoReplyLineEndingCombo->addItem(QStringLiteral("CR"), QIcon(), QStringLiteral("cr"));
    m_autoReplyLineEndingCombo->addItem(QStringLiteral("LF"), QIcon(), QStringLiteral("lf"));
    m_autoReplyLineEndingCombo->addItem(QStringLiteral("CRLF"), QIcon(), QStringLiteral("crlf"));
    makeCompactControl(m_autoReplyLineEndingCombo);
    responseModeRow->addWidget(m_autoReplyResponseModeCombo);
    responseModeRow->addWidget(m_autoReplyLineEndingCombo);
    root->addLayout(responseModeRow);

    m_autoReplyPayloadEdit = new PlainTextEdit(section);
    m_autoReplyPayloadEdit->setPlaceholderText(AppI18n::text("应答内容"));
    m_autoReplyPayloadEdit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_autoReplyPayloadEdit->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    m_autoReplyPayloadEdit->setFixedHeight(64);
    m_autoReplyPayloadEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    root->addWidget(m_autoReplyPayloadEdit);

    m_autoReplyDelayEdit = createNumberEdit(section, 0, 0, 600000);
    addFormRow(root, AppI18n::text("延时"), m_autoReplyDelayEdit, unitLabel(QStringLiteral("ms"), section));

    addGroupHeading(root, AppI18n::text("规则列表"), AppI18n::text("上/下调整优先级，前面的规则先匹配。"));
    m_autoReplyList = new ListWidget(section);
    m_autoReplyList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_autoReplyList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_autoReplyList->setMinimumHeight(132);
    m_autoReplyList->setMaximumHeight(220);
    m_autoReplyList->setBorderRadius(8);
    root->addWidget(m_autoReplyList);

    auto *saveRow = new QHBoxLayout;
    saveRow->setSpacing(8);
    m_autoReplyUpButton = new TransparentToolButton(icon(FluentIcon::Up), section);
    AppUi::setFluentToolTip(m_autoReplyUpButton, AppI18n::text("上移"));
    m_autoReplyDownButton = new TransparentToolButton(icon(FluentIcon::Download), section);
    AppUi::setFluentToolTip(m_autoReplyDownButton, AppI18n::text("下移"));
    for (ToolButton *button : {m_autoReplyUpButton, m_autoReplyDownButton}) {
        button->setFixedSize(CompactControlHeight, CompactControlHeight);
        button->setIconSize(QSize(16, 16));
    }
    m_autoReplySaveButton = new PushButton(icon(FluentIcon::Save), AppI18n::text("保存"), section);
    setButtonRowControlPolicy(m_autoReplySaveButton);
    saveRow->addWidget(m_autoReplyUpButton);
    saveRow->addWidget(m_autoReplyDownButton);
    saveRow->addWidget(m_autoReplySaveButton);
    root->addLayout(saveRow);

    addGroupHeading(root, AppI18n::text("规则操作"), AppI18n::text("新建或删除规则。"));
    auto *editRow = new QHBoxLayout;
    editRow->setSpacing(8);
    m_autoReplyNewButton = new PushButton(icon(FluentIcon::Add), AppI18n::text("新建"), section);
    m_autoReplyDeleteButton = new PushButton(icon(FluentIcon::Delete), AppI18n::text("删除"), section);
    setButtonRowControlPolicy(m_autoReplyNewButton);
    setButtonRowControlPolicy(m_autoReplyDeleteButton);
    editRow->addWidget(m_autoReplyNewButton);
    editRow->addWidget(m_autoReplyDeleteButton);
    root->addLayout(editRow);

    m_autoReplyStatusLabel = new CaptionLabel(AppI18n::text("未触发"), section);
    m_autoReplyStatusLabel->setTextColor(QColor(96, 96, 96), QColor(180, 180, 180));
    m_autoReplyStatusLabel->setWordWrap(true);
    root->addWidget(m_autoReplyStatusLabel);

    connect(m_autoReplyList, &ListWidget::currentRowChanged, this, [this](int row) {
        applyAutoReplyRule(row);
        updateAutoReplyActionState();
    });
    connect(m_autoReplySaveButton, &PushButton::clicked, this, &WorkbenchPage::saveCurrentAutoReplyRule);
    connect(m_autoReplyNewButton, &PushButton::clicked, this, &WorkbenchPage::startNewAutoReplyRule);
    connect(m_autoReplyDeleteButton, &PushButton::clicked, this, &WorkbenchPage::removeSelectedAutoReplyRule);
    connect(m_autoReplyUpButton, &ToolButton::clicked, this, [this]() { moveSelectedAutoReplyRule(-1); });
    connect(m_autoReplyDownButton, &ToolButton::clicked, this, [this]() { moveSelectedAutoReplyRule(1); });

    updateAutoReplyTable();
    makeCollapsibleCard(section, QStringLiteral("autoReply"));
    return section;
}

QWidget *WorkbenchPage::createFileSendSection()
{
    auto *section = new ExpandSettingCard(FluentIcon::Folder, AppI18n::text("文件发送"), QString(), this);
    auto *root = cardBody(section);

    m_filePathEdit = new LineEdit(section);
    m_filePathEdit->setPlaceholderText(AppI18n::text("选择待发送文件"));
    m_filePathEdit->setReadOnly(true);
    makeCompactControl(m_filePathEdit);
    m_fileBrowseButton = new PushButton(icon(FluentIcon::Folder), AppI18n::text("浏览"), section);
    m_fileBrowseButton->setFixedHeight(CompactControlHeight);
    addGroupHeading(root, AppI18n::text("文件"), AppI18n::text("选择要发送的文件。"));
    addFormRow(root, AppI18n::text("文件"), m_filePathEdit, m_fileBrowseButton);

    m_fileChunkSizeEdit = createNumberEdit(section, DefaultFileChunkSize, 1, 65536);
    addGroupHeading(root, AppI18n::text("分块与节奏"), AppI18n::text("按块长拆包、按间隔发送，适合固件升级等大文件传输。"));
    addFormRow(root, AppI18n::text("块长"), m_fileChunkSizeEdit, unitLabel(QStringLiteral("B"), section));

    m_fileIntervalEdit = createNumberEdit(section, DefaultFileChunkIntervalMs, 0, 60000);
    addFormRow(root, AppI18n::text("间隔"), m_fileIntervalEdit, unitLabel(QStringLiteral("ms"), section));

    m_fileProgressBar = new ProgressBar(section);
    m_fileProgressBar->setRange(0, 100);
    m_fileProgressBar->setValue(0);
    m_fileProgressBar->setFixedHeight(18);
    root->addWidget(m_fileProgressBar);

    auto *actionRow = new QHBoxLayout;
    actionRow->setSpacing(8);
    m_fileSendButton = new PrimaryPushButton(icon(FluentIcon::Send), AppI18n::text("发送文件"), section);
    m_fileCancelButton = new PushButton(icon(FluentIcon::Cancel), AppI18n::text("取消"), section);
    setButtonRowControlPolicy(m_fileSendButton);
    setButtonRowControlPolicy(m_fileCancelButton);
    actionRow->addWidget(m_fileSendButton);
    actionRow->addWidget(m_fileCancelButton);
    root->addLayout(actionRow);

    m_fileStatusLabel = new CaptionLabel(AppI18n::text("未选择文件"), section);
    m_fileStatusLabel->setTextColor(QColor(96, 96, 96), QColor(180, 180, 180));
    m_fileStatusLabel->setWordWrap(true);
    root->addWidget(m_fileStatusLabel);

    connect(m_fileBrowseButton, &PushButton::clicked, this, &WorkbenchPage::browseSendFile);
    connect(m_fileSendButton, &PrimaryPushButton::clicked, this, &WorkbenchPage::startFileSend);
    connect(m_fileCancelButton, &PushButton::clicked, this, &WorkbenchPage::cancelFileSend);

    updateFileSendUi(false);
    makeCollapsibleCard(section, QStringLiteral("fileSend"));
    return section;
}
