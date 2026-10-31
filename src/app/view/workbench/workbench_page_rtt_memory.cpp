#include "app/view/workbench_page.h"

#include "app/core/app_i18n.h"
#include "app/core/app_settings.h"
#include "app/core/hex_utils.h"
#include "app/debug/jlink_commander.h"
#include "app/view/workbench/workbench_page_internal.h"

#include <FluentQtWidgets/Widgets/InfoBar.h>

#include <QtWidgets/QApplication>

using namespace FluentQt;
using namespace WorkbenchPagePrivate;

namespace {

bool parseRttAddress(const QString &text, qint64 *address)
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

void WorkbenchPage::readRttMemory()
{
    qint64 address = 0;
    if (!parseRttAddress(m_rttAddressEdit->text(), &address)) {
        InfoBar::warning(AppI18n::text("地址无效"), AppI18n::text("请输入 0x20000000 样式的地址"), Qt::Horizontal,
                         true, 3500, InfoBarPosition::Top, window());
        return;
    }
    bool sizeOk = false;
    const int size = m_rttSizeEdit->text().trimmed().toInt(&sizeOk);
    if (!sizeOk || size <= 0 || size > 4096) {
        InfoBar::warning(AppI18n::text("字节数无效"), AppI18n::text("字节数须在 1-4096 之间"), Qt::Horizontal, true,
                         3500, InfoBarPosition::Top, window());
        return;
    }
    AppSettings settings;
    const QString device = settings.value(QStringLiteral("debugger/device"), QStringLiteral("S32K144")).toString();
    const int speed = settings.value(QStringLiteral("debugger/speed"), 4000).toInt();
    if (device.isEmpty() || speed <= 0) {
        InfoBar::warning(AppI18n::text("连接参数无效"), AppI18n::text("请在连接卡中填写目标设备名称和速率"),
                         Qt::Horizontal, true, 3500, InfoBarPosition::Top, window());
        return;
    }

    // The RTT logger owns the probe while the session is open; close it for
    // the duration of the memory access (Synex does the same for HSS).
    const bool rttWasRunning = m_link.isOpen() && m_link.linkType() == LinkType::Rtt;
    if (rttWasRunning) {
        m_link.closeLink();
    }

    m_rttStatusLabel->setText(AppI18n::text("正在读取…"));
    QApplication::processEvents();
    QString error;
    const QByteArray data = m_jlinkMemory->readMemory(JLinkCommander::defaultExePath(), device, speed,
                                                      static_cast<quint32>(address), size, &error);
    if (data.isEmpty()) {
        InfoBar::error(AppI18n::text("读取失败"), error, Qt::Horizontal, true, 5000, InfoBarPosition::Top, window());
        m_rttStatusLabel->setText(AppI18n::text("读取失败"));
        if (rttWasRunning) {
            m_link.openLink(m_lastConfig);
        }
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
    m_rttMemoryView->setPlainText(text.trimmed());
    m_rttStatusLabel->setText(AppI18n::text("已读取 %1 字节 @ 0x%2")
                                  .arg(data.size())
                                  .arg(static_cast<quint32>(address), 8, 16, QLatin1Char('0')));
    if (rttWasRunning) {
        m_link.openLink(m_lastConfig);
    }
}

void WorkbenchPage::writeRttMemory()
{
    qint64 address = 0;
    if (!parseRttAddress(m_rttAddressEdit->text(), &address)) {
        InfoBar::warning(AppI18n::text("地址无效"), AppI18n::text("请输入 0x20000000 样式的地址"), Qt::Horizontal,
                         true, 3500, InfoBarPosition::Top, window());
        return;
    }
    const HexParseResult parsed = parseHexPayload(m_rttWriteEdit->text());
    if (!parsed.ok || parsed.bytes.isEmpty()) {
        InfoBar::warning(AppI18n::text("写入内容无效"), AppI18n::text("请输入 HEX 字节，如：00 10 20 30"),
                         Qt::Horizontal, true, 3500, InfoBarPosition::Top, window());
        return;
    }
    AppSettings settings;
    const QString device = settings.value(QStringLiteral("debugger/device"), QStringLiteral("S32K144")).toString();
    const int speed = settings.value(QStringLiteral("debugger/speed"), 4000).toInt();
    if (device.isEmpty() || speed <= 0) {
        InfoBar::warning(AppI18n::text("连接参数无效"), AppI18n::text("请在连接卡中填写目标设备名称和速率"),
                         Qt::Horizontal, true, 3500, InfoBarPosition::Top, window());
        return;
    }

    const bool rttWasRunning = m_link.isOpen() && m_link.linkType() == LinkType::Rtt;
    if (rttWasRunning) {
        m_link.closeLink();
    }

    m_rttStatusLabel->setText(AppI18n::text("正在写入…"));
    QApplication::processEvents();
    QString error;
    const bool success = m_jlinkMemory->writeMemory(JLinkCommander::defaultExePath(), device, speed,
                                                    static_cast<quint32>(address), parsed.bytes, &error);
    if (!success) {
        InfoBar::error(AppI18n::text("写入失败"), error, Qt::Horizontal, true, 5000, InfoBarPosition::Top, window());
        m_rttStatusLabel->setText(AppI18n::text("写入失败"));
        if (rttWasRunning) {
            m_link.openLink(m_lastConfig);
        }
        return;
    }
    m_rttStatusLabel->setText(AppI18n::text("已写入并校验 %1 字节 @ 0x%2")
                                  .arg(parsed.bytes.size())
                                  .arg(static_cast<quint32>(address), 8, 16, QLatin1Char('0')));
    if (rttWasRunning) {
        m_link.openLink(m_lastConfig);
    }
}
