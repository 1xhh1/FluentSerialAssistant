#include "app/view/workbench_page.h"

#include "app/core/app_i18n.h"
#include "app/core/app_settings.h"
#include "app/core/hex_utils.h"
#include "app/debug/jlink_commander.h"
#include "app/view/workbench/workbench_page_internal.h"

#include <FluentQtWidgets/Widgets/InfoBar.h>

#include <QtCore/QDateTime>
#include <QtGui/QTextCursor>
#include <QtWidgets/QApplication>

using namespace FluentQt;
using namespace WorkbenchPagePrivate;

namespace {

bool parseDebuggerAddress(const QString &text, qint64 *address)
{
    QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    bool ok = false;
    qint64 value = 0;
    if (trimmed.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        value = trimmed.mid(2).toLongLong(&ok, 16);
    } else if (trimmed.contains(QRegularExpression(QStringLiteral("[a-fA-F]")))) {
        value = trimmed.toLongLong(&ok, 16);
    } else {
        value = trimmed.toLongLong(&ok, 10);
    }
    if (!ok || value < 0 || value > 0xFFFFFFFFLL) {
        return false;
    }
    *address = value;
    return true;
}

} // namespace

void WorkbenchPage::readDebuggerMemory()
{
    qint64 address = 0;
    if (!parseDebuggerAddress(m_debuggerAddressEdit->text(), &address)) {
        InfoBar::warning(AppI18n::text("地址无效"), AppI18n::text("请输入 0x20000000 样式的地址"), Qt::Horizontal,
                         true, 3500, InfoBarPosition::Top, window());
        return;
    }
    bool sizeOk = false;
    const int size = m_debuggerSizeEdit->text().trimmed().toInt(&sizeOk);
    if (!sizeOk || size <= 0 || size > 4096) {
        InfoBar::warning(AppI18n::text("字节数无效"), AppI18n::text("字节数须在 1-4096 之间"), Qt::Horizontal, true,
                         3500, InfoBarPosition::Top, window());
        return;
    }

    const QString device = m_debuggerDeviceEdit->text().trimmed();
    bool speedOk = false;
    const int speed = m_debuggerSpeedEdit->text().trimmed().toInt(&speedOk);
    if (device.isEmpty() || !speedOk || speed <= 0) {
        InfoBar::warning(AppI18n::text("连接参数无效"), AppI18n::text("请填写目标设备名称和正整数速率"),
                         Qt::Horizontal, true, 3500, InfoBarPosition::Top, window());
        return;
    }
    {
        AppSettings settings;
        settings.setValue(QStringLiteral("debugger/device"), device);
        settings.setValue(QStringLiteral("debugger/speed"), speed);
    }

    m_debuggerStatusLabel->setText(AppI18n::text("正在读取…"));
    QApplication::processEvents();
    QString error;
    const QByteArray data =
        m_jlink->readMemory(JLinkCommander::defaultExePath(), device, speed, static_cast<quint32>(address), size,
                            &error);
    if (data.isEmpty()) {
        InfoBar::error(AppI18n::text("读取失败"), error, Qt::Horizontal, true, 5000, InfoBarPosition::Top, window());
        m_debuggerStatusLabel->setText(AppI18n::text("读取失败"));
        return;
    }

    QString text;
    quint32 rowAddress = static_cast<quint32>(address);
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
    m_debuggerMemoryView->setPlainText(text.trimmed());
    m_debuggerStatusLabel->setText(AppI18n::text("已读取 %1 字节 @ 0x%2")
                                       .arg(data.size())
                                       .arg(static_cast<quint32>(address), 8, 16, QLatin1Char('0')));
}

void WorkbenchPage::writeDebuggerMemory()
{
    qint64 address = 0;
    if (!parseDebuggerAddress(m_debuggerAddressEdit->text(), &address)) {
        InfoBar::warning(AppI18n::text("地址无效"), AppI18n::text("请输入 0x20000000 样式的地址"), Qt::Horizontal,
                         true, 3500, InfoBarPosition::Top, window());
        return;
    }
    const HexParseResult parsed = parseHexPayload(m_debuggerWriteEdit->text());
    if (!parsed.ok || parsed.bytes.isEmpty()) {
        InfoBar::warning(AppI18n::text("写入内容无效"), AppI18n::text("请输入 HEX 字节，如：00 10 20 30"),
                         Qt::Horizontal, true, 3500, InfoBarPosition::Top, window());
        return;
    }
    const QString device = m_debuggerDeviceEdit->text().trimmed();
    bool speedOk = false;
    const int speed = m_debuggerSpeedEdit->text().trimmed().toInt(&speedOk);
    if (device.isEmpty() || !speedOk || speed <= 0) {
        InfoBar::warning(AppI18n::text("连接参数无效"), AppI18n::text("请填写目标设备名称和正整数速率"),
                         Qt::Horizontal, true, 3500, InfoBarPosition::Top, window());
        return;
    }

    m_debuggerStatusLabel->setText(AppI18n::text("正在写入…"));
    QApplication::processEvents();
    QString error;
    if (!m_jlink->writeMemory(JLinkCommander::defaultExePath(), device, speed, static_cast<quint32>(address),
                              parsed.bytes, &error)) {
        InfoBar::error(AppI18n::text("写入失败"), error, Qt::Horizontal, true, 5000, InfoBarPosition::Top, window());
        m_debuggerStatusLabel->setText(AppI18n::text("写入失败"));
        return;
    }
    m_debuggerStatusLabel->setText(AppI18n::text("已写入并校验 %1 字节 @ 0x%2")
                                       .arg(parsed.bytes.size())
                                       .arg(static_cast<quint32>(address), 8, 16, QLatin1Char('0')));
}

void WorkbenchPage::startDebuggerRtt()
{
    const QString device = m_debuggerDeviceEdit->text().trimmed();
    bool speedOk = false;
    const int speed = m_debuggerSpeedEdit->text().trimmed().toInt(&speedOk);
    bool channelOk = false;
    const int channel = m_debuggerRttChannelEdit->text().trimmed().toInt(&channelOk);
    if (device.isEmpty() || !speedOk || speed <= 0 || !channelOk || channel < 0 || channel > 15) {
        InfoBar::warning(AppI18n::text("连接参数无效"), AppI18n::text("请填写目标设备名称、速率和 0-15 的 RTT 通道"),
                         Qt::Horizontal, true, 3500, InfoBarPosition::Top, window());
        return;
    }
    QString error;
    if (!m_jlink->rttLoggerStart(JLinkCommander::defaultRttLoggerPath(), device, speed, channel, &error)) {
        InfoBar::error(AppI18n::text("RTT 启动失败"), error, Qt::Horizontal, true, 5000, InfoBarPosition::Top,
                       window());
        updateDebuggerUi();
        return;
    }
    m_debuggerRttTimer->start();
    updateDebuggerUi();
    InfoBar::success(AppI18n::text("已连接目标（SWD）"), QStringLiteral("RTT CH%1").arg(channel), Qt::Horizontal, true,
                     2500, InfoBarPosition::Top, window());
}

void WorkbenchPage::stopDebuggerRtt()
{
    m_debuggerRttTimer->stop();
    m_jlink->rttLoggerStop();
    updateDebuggerUi();
}

void WorkbenchPage::pollRttOutput()
{
    const QByteArray data = m_jlink->rttLoggerTail();
    const QString loggerError = m_jlink->rttLoggerError();
    if (!m_jlink->rttLoggerRunning()) {
        InfoBar::error(AppI18n::text("RTT 读取失败"),
                       loggerError.isEmpty() ? AppI18n::text("RTT 记录器已退出") : loggerError, Qt::Horizontal, true,
                       4000, InfoBarPosition::Top, window());
        m_debuggerRttTimer->stop();
        updateDebuggerUi();
        return;
    }
    if (!data.isEmpty()) {
        const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz "));
        QTextCursor cursor = m_debuggerRttView->textCursor();
        cursor.movePosition(QTextCursor::End);
        cursor.insertText(timestamp + QString::fromUtf8(data));
        m_debuggerRttView->setTextCursor(cursor);
        m_debuggerRttView->ensureCursorVisible();
    }
}

void WorkbenchPage::updateDebuggerUi()
{
    const bool rttRunning = m_jlink && m_jlink->rttLoggerRunning();
    m_debuggerRttStartButton->setEnabled(!rttRunning);
    m_debuggerRttStopButton->setEnabled(rttRunning);
    m_debuggerStatusLabel->setText(rttRunning ? QStringLiteral("RTT running") : m_debuggerStatusLabel->text());
}
