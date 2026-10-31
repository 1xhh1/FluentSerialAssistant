#include "app/view/debugger_dialog.h"

#include "app/core/app_i18n.h"
#include "app/core/app_settings.h"
#include "app/core/hex_utils.h"

#include <FluentQtWidgets/FluentQtWidgets.h>
#include <FluentQtWidgets/Widgets/InfoBar.h>

#include <QtCore/QDateTime>
#include <QtCore/QTimer>
#include <QtGui/QTextCursor>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QVBoxLayout>

using namespace FluentQt;

namespace {

// Parses "0x20000123" / "20000123" (hex) / decimal into an address.
bool parseAddress(const QString &text, quint32 *address)
{
    QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    bool ok = false;
    quint64 value = 0;
    if (trimmed.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        value = trimmed.mid(2).toULongLong(&ok, 16);
    } else if (trimmed.contains(QLatin1Char('a'), Qt::CaseInsensitive) || trimmed.contains(QLatin1Char('b'), Qt::CaseInsensitive)
               || trimmed.contains(QLatin1Char('c'), Qt::CaseInsensitive) || trimmed.contains(QLatin1Char('d'), Qt::CaseInsensitive)
               || trimmed.contains(QLatin1Char('e'), Qt::CaseInsensitive) || trimmed.contains(QLatin1Char('f'), Qt::CaseInsensitive)) {
        value = trimmed.toULongLong(&ok, 16);
    } else {
        value = trimmed.toULongLong(&ok, 10);
    }
    if (!ok || value > 0xFFFFFFFFULL) {
        return false;
    }
    *address = static_cast<quint32>(value);
    return true;
}

} // namespace

DebuggerDialog::DebuggerDialog(QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setWindowTitle(AppI18n::text("J-Link 调试器"));
    setMinimumSize(720, 680);
    resize(760, 760);

    m_debugger = new JLinkDebugger(this);
    m_rttTimer = new QTimer(this);
    m_rttTimer->setInterval(50);
    connect(m_rttTimer, &QTimer::timeout, this, &DebuggerDialog::pollRtt);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    auto *connectionTitle = new SubtitleLabel(AppI18n::text("调试连接"), this);
    root->addWidget(connectionTitle);

    auto *connectionForm = new QFormLayout;
    connectionForm->setHorizontalSpacing(12);
    connectionForm->setVerticalSpacing(8);

    m_dllPathEdit = new LineEdit(this);
    m_dllPathEdit->setObjectName(QStringLiteral("debuggerDllPathEdit"));
    m_dllPathEdit->setText(JLinkDebugger::defaultDllPath());
    m_dllPathEdit->setPlaceholderText(QStringLiteral("JLink_x64.dll"));
    connectionForm->addRow(AppI18n::text("J-Link DLL"), m_dllPathEdit);

    m_deviceEdit = new LineEdit(this);
    m_deviceEdit->setObjectName(QStringLiteral("debuggerDeviceEdit"));
    m_deviceEdit->setPlaceholderText(QStringLiteral("STM32G431RB"));
    m_deviceEdit->setText(AppSettings().value(QStringLiteral("debugger/device")).toString());
    connectionForm->addRow(AppI18n::text("目标设备"), m_deviceEdit);

    m_speedEdit = new LineEdit(this);
    m_speedEdit->setObjectName(QStringLiteral("debuggerSpeedEdit"));
    m_speedEdit->setText(QStringLiteral("4000"));
    connectionForm->addRow(AppI18n::text("速率 (kHz)"), m_speedEdit);

    root->addLayout(connectionForm);

    auto *connectionRow = new QHBoxLayout;
    connectionRow->setSpacing(8);
    m_connectButton = new PrimaryPushButton(icon(FluentIcon::Connect), AppI18n::text("连接"), this);
    m_connectButton->setObjectName(QStringLiteral("debuggerConnectButton"));
    connectionRow->addWidget(m_connectButton);
    m_connectionStatusLabel = new CaptionLabel(AppI18n::text("未连接"), this);
    m_connectionStatusLabel->setTextColor(QColor(96, 96, 96), QColor(180, 180, 180));
    m_connectionStatusLabel->setWordWrap(true);
    connectionRow->addWidget(m_connectionStatusLabel, 1);
    root->addLayout(connectionRow);

    auto *memoryTitle = new SubtitleLabel(AppI18n::text("内存读写"), this);
    root->addWidget(memoryTitle);

    auto *memoryForm = new QFormLayout;
    memoryForm->setHorizontalSpacing(12);
    memoryForm->setVerticalSpacing(8);

    m_addressEdit = new LineEdit(this);
    m_addressEdit->setObjectName(QStringLiteral("debuggerAddressEdit"));
    m_addressEdit->setPlaceholderText(QStringLiteral("0x20000000"));
    memoryForm->addRow(AppI18n::text("地址"), m_addressEdit);

    m_sizeEdit = new LineEdit(this);
    m_sizeEdit->setObjectName(QStringLiteral("debuggerSizeEdit"));
    m_sizeEdit->setText(QStringLiteral("64"));
    memoryForm->addRow(AppI18n::text("字节数"), m_sizeEdit);

    m_valueEdit = new LineEdit(this);
    m_valueEdit->setObjectName(QStringLiteral("debuggerValueEdit"));
    m_valueEdit->setPlaceholderText(AppI18n::text("写入字节，HEX 格式，如：00 10 20 30"));
    memoryForm->addRow(AppI18n::text("写入内容"), m_valueEdit);

    root->addLayout(memoryForm);

    auto *memoryButtons = new QHBoxLayout;
    memoryButtons->setSpacing(8);
    m_readButton = new PushButton(icon(FluentIcon::Download), AppI18n::text("读取"), this);
    m_readButton->setObjectName(QStringLiteral("debuggerReadButton"));
    m_writeButton = new PushButton(icon(FluentIcon::SaveAs), AppI18n::text("写入并校验"), this);
    m_writeButton->setObjectName(QStringLiteral("debuggerWriteButton"));
    memoryButtons->addWidget(m_readButton);
    memoryButtons->addWidget(m_writeButton);
    memoryButtons->addStretch(1);
    root->addLayout(memoryButtons);

    m_memoryStatusLabel = new CaptionLabel(AppI18n::text("尚未读取"), this);
    m_memoryStatusLabel->setTextColor(QColor(96, 96, 96), QColor(180, 180, 180));
    m_memoryStatusLabel->setWordWrap(true);
    root->addWidget(m_memoryStatusLabel);

    m_memoryView = new PlainTextEdit(this);
    m_memoryView->setObjectName(QStringLiteral("debuggerMemoryView"));
    m_memoryView->setReadOnly(true);
    m_memoryView->setFont(QFont(QStringLiteral("Consolas"), 10));
    m_memoryView->setFixedHeight(120);
    m_memoryView->setPlaceholderText(AppI18n::text("读取结果（HEX 与 ASCII）"));
    root->addWidget(m_memoryView);

    auto *rttTitle = new SubtitleLabel(AppI18n::text("RTT 终端"), this);
    root->addWidget(rttTitle);

    auto *rttRow = new QHBoxLayout;
    rttRow->setSpacing(8);
    m_rttChannelEdit = new LineEdit(this);
    m_rttChannelEdit->setObjectName(QStringLiteral("debuggerRttChannelEdit"));
    m_rttChannelEdit->setText(QStringLiteral("0"));
    m_rttChannelEdit->setFixedWidth(72);
    rttRow->addWidget(new BodyLabel(AppI18n::text("通道"), this));
    rttRow->addWidget(m_rttChannelEdit);
    m_rttStartButton = new PushButton(icon(FluentIcon::Play), AppI18n::text("启动 RTT"), this);
    m_rttStartButton->setObjectName(QStringLiteral("debuggerRttStartButton"));
    m_rttStopButton = new PushButton(icon(FluentIcon::Pause), AppI18n::text("停止"), this);
    m_rttStopButton->setObjectName(QStringLiteral("debuggerRttStopButton"));
    m_rttStopButton->setEnabled(false);
    rttRow->addWidget(m_rttStartButton);
    rttRow->addWidget(m_rttStopButton);
    rttRow->addStretch(1);
    root->addLayout(rttRow);

    m_rttView = new PlainTextEdit(this);
    m_rttView->setObjectName(QStringLiteral("debuggerRttView"));
    m_rttView->setReadOnly(true);
    m_rttView->setFont(QFont(QStringLiteral("Consolas"), 10));
    root->addWidget(m_rttView, 1);

    connect(m_connectButton, &PushButton::clicked, this, [this]() {
        if (m_debugger->isConnected()) {
            disconnectTarget();
        } else {
            connectTarget();
        }
    });
    connect(m_readButton, &PushButton::clicked, this, &DebuggerDialog::readMemory);
    connect(m_writeButton, &PushButton::clicked, this, &DebuggerDialog::writeMemory);
    connect(m_rttStartButton, &PushButton::clicked, this, [this]() {
        QString error;
        if (m_debugger->rttStart(&error)) {
            m_rttTimer->start();
            m_rttStartButton->setEnabled(false);
            m_rttStopButton->setEnabled(true);
        } else {
            InfoBar::error(AppI18n::text("RTT 启动失败"), error, Qt::Horizontal, true, 4000, InfoBarPosition::Top, window());
        }
    });
    connect(m_rttStopButton, &PushButton::clicked, this, [this]() {
        m_debugger->rttStop();
        m_rttTimer->stop();
        m_rttStartButton->setEnabled(true);
        m_rttStopButton->setEnabled(false);
    });

    updateConnectionUi();
}

DebuggerDialog::~DebuggerDialog()
{
    AppSettings settings;
    settings.setValue(QStringLiteral("debugger/device"), m_deviceEdit->text().trimmed());
    m_debugger->disconnectTarget();
}

void DebuggerDialog::connectTarget()
{
    const QString dllPath = m_dllPathEdit->text().trimmed();
    QString error;
    if (!m_debugger->load(dllPath, &error)) {
        InfoBar::error(AppI18n::text("J-Link DLL 加载失败"), error, Qt::Horizontal, true, 4000, InfoBarPosition::Top, window());
        return;
    }
    const QString device = m_deviceEdit->text().trimmed();
    bool speedOk = false;
    const int speed = m_speedEdit->text().trimmed().toInt(&speedOk);
    if (device.isEmpty() || !speedOk || speed <= 0) {
        InfoBar::warning(AppI18n::text("连接参数无效"), AppI18n::text("请填写目标设备名称和正整数速率"), Qt::Horizontal, true, 4000, InfoBarPosition::Top, window());
        return;
    }
    if (!m_debugger->connectTarget(device, speed, &error)) {
        InfoBar::error(AppI18n::text("目标连接失败"), error, Qt::Horizontal, true, 4000, InfoBarPosition::Top, window());
        updateConnectionUi();
        return;
    }
    AppSettings settings;
    settings.setValue(QStringLiteral("debugger/device"), device);
    updateConnectionUi();
    InfoBar::success(AppI18n::text("已连接目标"), device, Qt::Horizontal, true, 2500, InfoBarPosition::Top, window());
}

void DebuggerDialog::disconnectTarget()
{
    m_rttTimer->stop();
    m_debugger->disconnectTarget();
    updateConnectionUi();
}

void DebuggerDialog::updateConnectionUi()
{
    const bool connected = m_debugger->isConnected();
    m_connectButton->setText(connected ? AppI18n::text("断开") : AppI18n::text("连接"));
    m_connectionStatusLabel->setText(connected ? AppI18n::text("已连接目标（SWD）") : statusText());
    m_readButton->setEnabled(connected);
    m_writeButton->setEnabled(connected);
    if (!connected) {
        m_rttTimer->stop();
        m_rttStartButton->setEnabled(true);
        m_rttStopButton->setEnabled(false);
    }
}

QString DebuggerDialog::statusText() const
{
    if (!m_debugger->isLoaded()) {
        return AppI18n::text("未加载 J-Link DLL");
    }
    return AppI18n::text("未连接");
}

void DebuggerDialog::readMemory()
{
    quint32 address = 0;
    if (!parseAddress(m_addressEdit->text(), &address)) {
        InfoBar::warning(AppI18n::text("地址无效"), AppI18n::text("请输入 0x20000000 样式的地址"), Qt::Horizontal, true, 3500, InfoBarPosition::Top, window());
        return;
    }
    bool sizeOk = false;
    const int size = m_sizeEdit->text().trimmed().toInt(&sizeOk);
    if (!sizeOk || size <= 0 || size > 4096) {
        InfoBar::warning(AppI18n::text("字节数无效"), AppI18n::text("字节数须在 1-4096 之间"), Qt::Horizontal, true, 3500, InfoBarPosition::Top, window());
        return;
    }

    QString error;
    const QByteArray data = m_debugger->readMemory(address, size, &error);
    if (data.isEmpty() && !error.isEmpty()) {
        InfoBar::error(AppI18n::text("读取失败"), error, Qt::Horizontal, true, 4000, InfoBarPosition::Top, window());
        return;
    }

    QString text;
    for (int row = 0; row < data.size(); row += 16) {
        const QByteArray line = data.mid(row, 16);
        text += QStringLiteral("%1  ")
                    .arg(address + static_cast<quint32>(row), 8, 16, QLatin1Char('0'))
                    .toUpper();
        QString ascii;
        for (unsigned char byte : line) {
            text += QStringLiteral("%1 ").arg(byte, 2, 16, QLatin1Char('0')).toUpper();
            ascii += byte >= 0x20 && byte < 0x7F ? QChar(byte) : QChar::fromLatin1('.');
        }
        text += QStringLiteral(" |%1|\n").arg(ascii);
    }
    m_memoryView->setPlainText(text.trimmed());
    m_memoryStatusLabel->setText(
        AppI18n::text("已读取 %1 字节 @ 0x%2").arg(data.size()).arg(address, 8, 16, QLatin1Char('0')).toUpper());
}

void DebuggerDialog::writeMemory()
{
    quint32 address = 0;
    if (!parseAddress(m_addressEdit->text(), &address)) {
        InfoBar::warning(AppI18n::text("地址无效"), AppI18n::text("请输入 0x20000000 样式的地址"), Qt::Horizontal, true, 3500, InfoBarPosition::Top, window());
        return;
    }
    const HexParseResult parsed = parseHexPayload(m_valueEdit->text());
    if (!parsed.ok || parsed.bytes.isEmpty()) {
        InfoBar::warning(AppI18n::text("写入内容无效"), AppI18n::text("请输入 HEX 字节，如：00 10 20 30"), Qt::Horizontal, true, 3500, InfoBarPosition::Top, window());
        return;
    }

    QString error;
    if (!m_debugger->writeMemory(address, parsed.bytes, true, &error)) {
        InfoBar::error(AppI18n::text("写入失败"), error, Qt::Horizontal, true, 4000, InfoBarPosition::Top, window());
        return;
    }
    m_memoryStatusLabel->setText(AppI18n::text("已写入并校验 %1 字节 @ 0x%2")
                                     .arg(parsed.bytes.size())
                                     .arg(address, 8, 16, QLatin1Char('0'))
                                     .toUpper());
    readMemory();
}

void DebuggerDialog::pollRtt()
{
    bool ok = false;
    const int channel = m_rttChannelEdit->text().trimmed().toInt(&ok);
    if (!ok || channel < 0 || channel > 15) {
        return;
    }
    QString error;
    const QByteArray data = m_debugger->rttRead(channel, 4096, &error);
    if (!error.isEmpty()) {
        InfoBar::error(AppI18n::text("RTT 读取失败"), error, Qt::Horizontal, true, 4000, InfoBarPosition::Top, window());
        m_rttTimer->stop();
        m_rttStartButton->setEnabled(true);
        m_rttStopButton->setEnabled(false);
        return;
    }
    if (!data.isEmpty()) {
        appendRttText(QString::fromUtf8(data));
    }
}

void DebuggerDialog::appendRttText(const QString &text)
{
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz "));
    QTextCursor cursor = m_rttView->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(timestamp + text);
    m_rttView->setTextCursor(cursor);
    m_rttView->ensureCursorVisible();
}
