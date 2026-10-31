#include "app/view/workbench/workbench_dashboard.h"

#include "app/core/app_i18n.h"
#include "app/core/app_settings.h"
#include "app/core/hex_utils.h"
#include "app/debug/jlink_commander.h"
#include "app/view/workbench_page.h"
#include "app/view/workbench/workbench_page_internal.h"

#include <FluentQtWidgets/FluentQtWidgets.h>
#include <FluentQtWidgets/Widgets/InfoBar.h>
#include <FluentQtWidgets/Widgets/RealtimePlotWidget.h>

#include <QtCore/QDateTime>
#include <QtCore/QFile>
#include <QtCore/QRegularExpression>
#include <QtCore/QTimer>
#include <QtGui/QTextCursor>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QApplication>
#include <QtGui/QWheelEvent>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QScrollArea>

using namespace FluentQt;

namespace {

const QRegularExpression kKeyValuePattern(QStringLiteral("([A-Za-z_][A-Za-z0-9_]*)\\s*[=:]\\s*(-?\\d+(?:\\.\\d+)?)"));

} // namespace

WorkbenchDashboard::WorkbenchDashboard(WorkbenchPage *page, QWidget *parent)
    : QWidget(parent)
    , m_page(page)
{
    setObjectName(QStringLiteral("workbenchDashboard"));

    m_jlink = new JLinkCommander(this);
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(50);
    connect(m_pollTimer, &QTimer::timeout, this, &WorkbenchDashboard::pollSource);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("dashboardScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);

    auto *canvas = new QWidget(scroll);
    canvas->setObjectName(QStringLiteral("dashboardCanvas"));
    scroll->setWidget(canvas);
    auto *root = new QVBoxLayout(canvas);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    // Source bar: serial RX or J-Link RTT, Synex-style transport selector.
    auto *sourceBar = new QWidget(this);
    sourceBar->setObjectName(QStringLiteral("dashboardSourceBar"));
    auto *sourceLayout = new QHBoxLayout(sourceBar);
    sourceLayout->setContentsMargins(10, 6, 10, 6);
    sourceLayout->setSpacing(8);

    sourceLayout->addWidget(new BodyLabel(AppI18n::text("数据源"), sourceBar));
    m_sourceCombo = new ComboBox(sourceBar);
    m_sourceCombo->setObjectName(QStringLiteral("dashboardSourceCombo"));
    m_sourceCombo->addItem(AppI18n::text("串口接收"), QIcon(), QStringLiteral("serial"));
    m_sourceCombo->addItem(QStringLiteral("J-Link RTT"), QIcon(), QStringLiteral("rtt"));
    WorkbenchPagePrivate::makeCompactControl(m_sourceCombo);
    sourceLayout->addWidget(m_sourceCombo);

    m_deviceEdit = new LineEdit(sourceBar);
    m_deviceEdit->setObjectName(QStringLiteral("dashboardDeviceEdit"));
    m_deviceEdit->setText(AppSettings().value(QStringLiteral("debugger/device"), QStringLiteral("S32K144")).toString());
    m_deviceEdit->setFixedWidth(140);
    sourceLayout->addWidget(m_deviceEdit);
    m_speedEdit = new LineEdit(sourceBar);
    m_speedEdit->setObjectName(QStringLiteral("dashboardSpeedEdit"));
    m_speedEdit->setText(QStringLiteral("4000"));
    m_speedEdit->setFixedWidth(64);
    sourceLayout->addWidget(m_speedEdit);
    m_channelCombo = new ComboBox(sourceBar);
    m_channelCombo->setObjectName(QStringLiteral("dashboardRttChannelCombo"));
    for (int channel = 0; channel <= 2; ++channel) {
        m_channelCombo->addItem(QStringLiteral("CH%1").arg(channel), QIcon(), channel);
    }
    m_channelCombo->setFixedWidth(72);
    sourceLayout->addWidget(m_channelCombo);

    m_startButton = new PushButton(icon(FluentIcon::Play), AppI18n::text("启动"), sourceBar);
    m_startButton->setObjectName(QStringLiteral("dashboardStartButton"));
    m_stopButton = new PushButton(icon(FluentIcon::Pause), AppI18n::text("停止"), sourceBar);
    m_stopButton->setObjectName(QStringLiteral("dashboardStopButton"));
    m_stopButton->setEnabled(false);
    sourceLayout->addWidget(m_startButton);
    sourceLayout->addWidget(m_stopButton);

    auto *backButton = new TransparentToolButton(icon(FluentIcon::Return), sourceBar);
    backButton->setObjectName(QStringLiteral("dashboardBackButton"));
    AppUi::setFluentToolTip(backButton, AppI18n::text("返回终端"));
    sourceLayout->addWidget(backButton);
    sourceLayout->addStretch(1);

    m_statusLabel = new CaptionLabel(AppI18n::text("未启动"), sourceBar);
    m_statusLabel->setTextColor(QColor(96, 96, 96), QColor(180, 180, 180));
    sourceLayout->addWidget(m_statusLabel);

    root->addWidget(sourceBar);

    // Tile: live plot.
    auto *plotTitle = new SubtitleLabel(AppI18n::text("实时绘图"), this);
    root->addWidget(plotTitle);
    m_plot = new RealtimePlotWidget(this);
    m_plot->setObjectName(QStringLiteral("dashboardPlot"));
    m_plot->setCapacity(20000);
    m_plot->setMinimumHeight(220);
    root->addWidget(m_plot, 3);

    // Tile: RTT / data console.
    auto *consoleTitle = new SubtitleLabel(AppI18n::text("数据控制台"), this);
    root->addWidget(consoleTitle);
    m_consoleView = new PlainTextEdit(this);
    m_consoleView->setObjectName(QStringLiteral("dashboardConsole"));
    m_consoleView->setReadOnly(true);
    m_consoleView->setFont(QFont(QStringLiteral("Consolas"), 9));
    m_consoleView->setFixedHeight(140);
    root->addWidget(m_consoleView);

    // Tile: SWD memory R/W.
    auto *memoryTitle = new SubtitleLabel(AppI18n::text("内存读写 (SWD)"), this);
    root->addWidget(memoryTitle);
    auto *memoryRow = new QHBoxLayout;
    memoryRow->setSpacing(8);
    m_addressEdit = new LineEdit(this);
    m_addressEdit->setObjectName(QStringLiteral("dashboardAddressEdit"));
    m_addressEdit->setPlaceholderText(QStringLiteral("0x20000000"));
    m_addressEdit->setFixedWidth(140);
    m_sizeEdit = new LineEdit(this);
    m_sizeEdit->setObjectName(QStringLiteral("dashboardMemorySizeEdit"));
    m_sizeEdit->setText(QStringLiteral("64"));
    m_sizeEdit->setFixedWidth(64);
    m_readButton = new PushButton(icon(FluentIcon::Download), AppI18n::text("读取"), this);
    m_readButton->setObjectName(QStringLiteral("dashboardReadButton"));
    m_writeButton = new PushButton(icon(FluentIcon::SaveAs), AppI18n::text("写入并校验"), this);
    m_writeButton->setObjectName(QStringLiteral("dashboardWriteButton"));
    memoryRow->addWidget(new BodyLabel(AppI18n::text("地址"), this));
    memoryRow->addWidget(m_addressEdit);
    memoryRow->addWidget(new BodyLabel(AppI18n::text("字节数"), this));
    memoryRow->addWidget(m_sizeEdit);
    memoryRow->addWidget(m_readButton);
    memoryRow->addWidget(m_writeButton);
    memoryRow->addStretch(1);
    root->addLayout(memoryRow);

    auto *writeRow = new QHBoxLayout;
    writeRow->setSpacing(8);
    m_writeEdit = new LineEdit(this);
    m_writeEdit->setObjectName(QStringLiteral("dashboardWriteEdit"));
    m_writeEdit->setPlaceholderText(AppI18n::text("写入字节，HEX 格式，如：00 10 20 30"));
    writeRow->addWidget(m_writeEdit, 1);
    root->addLayout(writeRow);

    m_memoryLabel = new CaptionLabel(AppI18n::text("尚未读取"), this);
    m_memoryLabel->setTextColor(QColor(96, 96, 96), QColor(180, 180, 180));
    root->addWidget(m_memoryLabel);

    m_memoryView = new PlainTextEdit(this);
    m_memoryView->setObjectName(QStringLiteral("dashboardMemoryView"));
    m_memoryView->setReadOnly(true);
    m_memoryView->setFont(QFont(QStringLiteral("Consolas"), 9));
    m_memoryView->setFixedHeight(120);
    root->addWidget(m_memoryView);

    // Wiring.
    connect(m_sourceCombo, &ComboBox::currentIndexChanged, this, [this](int) { updateUi(); });
    connect(m_startButton, &PushButton::clicked, this, &WorkbenchDashboard::startSource);
    connect(m_stopButton, &PushButton::clicked, this, &WorkbenchDashboard::stopSource);
    connect(backButton, &TransparentToolButton::clicked, this, &WorkbenchDashboard::backRequested);
    connect(m_readButton, &PushButton::clicked, this, &WorkbenchDashboard::readMemory);
    connect(m_writeButton, &PushButton::clicked, this, &WorkbenchDashboard::writeMemory);
    if (m_page) {
        // Serial RX lines feed the same pipeline as RTT lines.
        connect(m_page, &WorkbenchPage::recordCaptured, this,
                [this](const QByteArray &bytes, bool transmit) {
                    if (transmit || m_sourceCombo->currentData().toString() != QStringLiteral("serial")
                        || !m_pollTimer->isActive()) {
                        return;
                    }
                    appendConsoleText(QString::fromUtf8(bytes));
                    feedLines(QString::fromUtf8(bytes));
                });
    }

    updateUi();
}

WorkbenchDashboard::~WorkbenchDashboard()
{
    m_pollTimer->stop();
    m_jlink->rttLoggerStop();
}

void WorkbenchDashboard::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
}

void WorkbenchDashboard::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    // The probe is exclusive; stop polling while the dashboard is hidden.
    m_pollTimer->stop();
    m_jlink->rttLoggerStop();
    updateUi();
}

void WorkbenchDashboard::startSource()
{
    const QString source = m_sourceCombo->currentData().toString();
    if (source == QStringLiteral("serial")) {
        // Serial samples arrive through the recordCaptured signal.
        m_pollTimer->stop();
        m_statusLabel->setText(AppI18n::text("正在从串口接收解析"));
        m_startButton->setEnabled(false);
        m_stopButton->setEnabled(true);
        return;
    }

    const QString device = m_deviceEdit->text().trimmed();
    bool speedOk = false;
    const int speed = m_speedEdit->text().trimmed().toInt(&speedOk);
    if (device.isEmpty() || !speedOk || speed <= 0) {
        InfoBar::warning(AppI18n::text("连接参数无效"), AppI18n::text("请填写目标设备名称和正整数速率"), Qt::Horizontal,
                         true, 3500, InfoBarPosition::Top, window());
        return;
    }
    {
        AppSettings settings;
        settings.setValue(QStringLiteral("debugger/device"), device);
        settings.setValue(QStringLiteral("debugger/speed"), speed);
    }

    QString error;
    const int channel = m_channelCombo->currentData().toInt();
    if (!m_jlink->rttLoggerStart(JLinkCommander::defaultRttLoggerPath(), device, speed, channel, &error)) {
        InfoBar::error(AppI18n::text("RTT 启动失败"), error, Qt::Horizontal, true, 5000, InfoBarPosition::Top,
                       window());
        updateUi();
        return;
    }
    m_pollTimer->start();
    m_lineBuffer.clear();
    m_statusLabel->setText(QStringLiteral("RTT CH%1").arg(channel));
    updateUi();
}

void WorkbenchDashboard::stopSource()
{
    m_pollTimer->stop();
    m_jlink->rttLoggerStop();
    m_statusLabel->setText(AppI18n::text("未启动"));
    updateUi();
}

void WorkbenchDashboard::pollSource()
{
    if (m_sourceCombo->currentData().toString() != QStringLiteral("rtt")) {
        return;
    }
    const QByteArray data = m_jlink->rttLoggerTail();
    if (!m_jlink->rttLoggerRunning()) {
        const QString loggerError = m_jlink->rttLoggerError();
        InfoBar::error(AppI18n::text("RTT 读取失败"),
                       loggerError.isEmpty() ? AppI18n::text("RTT 记录器已退出") : loggerError, Qt::Horizontal, true,
                       4000, InfoBarPosition::Top, window());
        stopSource();
        return;
    }
    if (data.isEmpty()) {
        return;
    }
    const QString text = QString::fromUtf8(data);
    appendConsoleText(text);
    feedLines(text);
}

void WorkbenchDashboard::appendConsoleText(const QString &text)
{
    QTextCursor cursor = m_consoleView->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text);
    m_consoleView->setTextCursor(cursor);
    m_consoleView->ensureCursorVisible();
}

void WorkbenchDashboard::feedLines(const QString &text)
{
    m_lineBuffer += text;
    qsizetype newline = 0;
    while ((newline = m_lineBuffer.indexOf(QLatin1Char('\n'))) >= 0) {
        const QString line = m_lineBuffer.left(newline).trimmed();
        m_lineBuffer.remove(0, newline + 1);
        if (line.isEmpty()) {
            continue;
        }

        // Key=value style lines register named channels ("temp=25.5 ...").
        QRegularExpressionMatchIterator matches = kKeyValuePattern.globalMatch(line);
        if (matches.hasNext()) {
            while (matches.hasNext()) {
                const QRegularExpressionMatch match = matches.next();
                feedSample(match.captured(1), match.captured(2).toDouble());
            }
            continue;
        }
        // Fallback: whitespace-separated numbers become CH0..CHn.
        static const QRegularExpression numberPattern(QStringLiteral("-?\\d+(?:\\.\\d+)?"));
        QRegularExpressionMatchIterator numbers = numberPattern.globalMatch(line);
        int index = 0;
        bool any = false;
        while (numbers.hasNext()) {
            const QRegularExpressionMatch match = numbers.next();
            bool ok = false;
            const double value = match.captured(0).toDouble(&ok);
            if (ok) {
                any = true;
                feedSample(QStringLiteral("CH%1").arg(index), value);
            }
            ++index;
        }
        Q_UNUSED(any);
    }
    // Guard against a runaway buffer when the source never emits newlines.
    if (m_lineBuffer.size() > 8192) {
        m_lineBuffer.clear();
    }
}

void WorkbenchDashboard::feedSample(const QString &channel, double value)
{
    if (!m_seriesIndex.contains(channel)) {
        const int index = m_seriesIndex.size();
        m_seriesIndex.insert(channel, index);
        m_plot->addSeries(channel, index == 0 ? QColor(QStringLiteral("#00d3d8")) : QColor());
    }
    m_plot->appendSample(m_seriesIndex.value(channel), value);
}

void WorkbenchDashboard::readMemory()
{
    bool ok = false;
    const quint32 address = m_addressEdit->text().toULongLong(&ok, 0) & 0xFFFFFFFFULL;
    if (!ok) {
        InfoBar::warning(AppI18n::text("地址无效"), AppI18n::text("请输入 0x20000000 样式的地址"), Qt::Horizontal,
                         true, 3500, InfoBarPosition::Top, window());
        return;
    }
    bool sizeOk = false;
    const int size = m_sizeEdit->text().trimmed().toInt(&sizeOk);
    if (!sizeOk || size <= 0 || size > 4096) {
        InfoBar::warning(AppI18n::text("字节数无效"), AppI18n::text("字节数须在 1-4096 之间"), Qt::Horizontal, true,
                         3500, InfoBarPosition::Top, window());
        return;
    }
    const QString device = m_deviceEdit->text().trimmed();
    bool speedOk = false;
    const int speed = m_speedEdit->text().trimmed().toInt(&speedOk);
    if (device.isEmpty() || !speedOk || speed <= 0) {
        InfoBar::warning(AppI18n::text("连接参数无效"), AppI18n::text("请填写目标设备名称和正整数速率"), Qt::Horizontal,
                         true, 3500, InfoBarPosition::Top, window());
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    const QByteArray data =
        m_jlink->readMemory(JLinkCommander::defaultExePath(), device, speed, address, size, &error);
    QApplication::restoreOverrideCursor();
    if (data.isEmpty()) {
        InfoBar::error(AppI18n::text("读取失败"), error, Qt::Horizontal, true, 5000, InfoBarPosition::Top, window());
        return;
    }

    QString text;
    quint32 rowAddress = address;
    for (int row = 0; row < data.size(); row += 16) {
        const QByteArray line = data.mid(row, 16);
        text += QStringLiteral("%1  ").arg(rowAddress + static_cast<quint32>(row), 8, 16, QLatin1Char('0'));
        QString ascii;
        for (unsigned char byte : line) {
            text += QStringLiteral("%1 ").arg(byte, 2, 16, QLatin1Char('0')).toUpper();
            ascii += byte >= 0x20 && byte < 0x7F ? QChar(byte) : QChar::fromLatin1('.');
        }
        text += QStringLiteral(" |%1|\n").arg(ascii);
        rowAddress += static_cast<quint32>(line.size());
    }
    m_memoryView->setPlainText(text.trimmed());
    m_memoryLabel->setText(AppI18n::text("已读取 %1 字节 @ 0x%2")
                               .arg(data.size())
                               .arg(address, 8, 16, QLatin1Char('0')));
}

void WorkbenchDashboard::writeMemory()
{
    bool ok = false;
    const quint32 address = m_addressEdit->text().toULongLong(&ok, 0) & 0xFFFFFFFFULL;
    if (!ok) {
        InfoBar::warning(AppI18n::text("地址无效"), AppI18n::text("请输入 0x20000000 样式的地址"), Qt::Horizontal,
                         true, 3500, InfoBarPosition::Top, window());
        return;
    }
    const HexParseResult parsed = parseHexPayload(m_writeEdit->text());
    if (!parsed.ok || parsed.bytes.isEmpty()) {
        InfoBar::warning(AppI18n::text("写入内容无效"), AppI18n::text("请输入 HEX 字节，如：00 10 20 30"),
                         Qt::Horizontal, true, 3500, InfoBarPosition::Top, window());
        return;
    }
    const QString device = m_deviceEdit->text().trimmed();
    bool speedOk = false;
    const int speed = m_speedEdit->text().trimmed().toInt(&speedOk);
    if (device.isEmpty() || !speedOk || speed <= 0) {
        InfoBar::warning(AppI18n::text("连接参数无效"), AppI18n::text("请填写目标设备名称和正整数速率"), Qt::Horizontal,
                         true, 3500, InfoBarPosition::Top, window());
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    const bool success = m_jlink->writeMemory(JLinkCommander::defaultExePath(), device, speed, address, parsed.bytes,
                                              &error);
    QApplication::restoreOverrideCursor();
    if (!success) {
        InfoBar::error(AppI18n::text("写入失败"), error, Qt::Horizontal, true, 5000, InfoBarPosition::Top, window());
        return;
    }
    m_memoryLabel->setText(AppI18n::text("已写入并校验 %1 字节 @ 0x%2")
                               .arg(parsed.bytes.size())
                               .arg(address, 8, 16, QLatin1Char('0')));
    readMemory();
}

void WorkbenchDashboard::clearConsole()
{
    m_consoleView->clear();
}

void WorkbenchDashboard::updateUi()
{
    const bool running = m_pollTimer->isActive();
    m_startButton->setEnabled(!running);
    m_stopButton->setEnabled(running);
    const bool rtt = m_sourceCombo->currentData().toString() == QStringLiteral("rtt");
    m_deviceEdit->setEnabled(rtt && !running);
    m_speedEdit->setEnabled(rtt && !running);
    m_channelCombo->setEnabled(rtt && !running);
    m_readButton->setEnabled(!running);
    m_writeButton->setEnabled(!running);
}
