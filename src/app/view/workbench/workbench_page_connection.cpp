#include "app/core/app_i18n.h"
#include "app/serial/virtual_serial_pair.h"
#include "app/view/workbench/workbench_page_internal.h"

using namespace FluentQt;
using namespace WorkbenchPagePrivate;

void WorkbenchPage::scheduleReconnect()
{
    if (m_lastConfig.type == LinkType::Serial && VirtualSerialPair::isVirtualPort(m_lastConfig.serial.portName) &&
        !VirtualSerialPair::instance()->isEnabled()) {
        m_reconnectTimer.stop();
        return;
    }
    if (!hasLastLinkConfig() || m_manualDisconnect || !m_autoReconnectCheck->isChecked()) {
        return;
    }
    if (!m_reconnectTimer.isActive()) {
        m_reconnectTimer.start();
    }
    showWarning(AppI18n::text("连接中断"), AppI18n::text("正在尝试自动重连"));
}

void WorkbenchPage::attemptReconnect()
{
    if (m_lastConfig.type == LinkType::Serial && VirtualSerialPair::isVirtualPort(m_lastConfig.serial.portName) &&
        !VirtualSerialPair::instance()->isEnabled()) {
        m_reconnectTimer.stop();
        return;
    }
    if (m_link.isConnecting() || m_link.isOpen()) {
        m_reconnectTimer.stop();
        return;
    }
    if (!hasLastLinkConfig() || !m_autoReconnectCheck->isChecked()) {
        m_reconnectTimer.stop();
        return;
    }

    if (m_lastConfig.type == LinkType::Serial) {
        refreshPorts();
    }
    m_link.openLink(m_lastConfig);
}

void WorkbenchPage::onConnectClicked()
{
    if (m_link.isOpen()) {
        m_manualDisconnect = true;
        m_reconnectTimer.stop();
        m_link.closeLink();
        return;
    }

    const LinkConfig config = currentLinkConfig();
    if (m_linkTypeCombo && m_linkTypeCombo->currentData().toString() == QStringLiteral("serial") &&
        config.serial.portName.isEmpty()) {
        showWarning(AppI18n::text("未选择端口"), AppI18n::text("请先刷新并选择可用串口"));
        return;
    }
    if (m_linkTypeCombo && m_linkTypeCombo->currentData().toString() == QStringLiteral("serial")) {
        bool baudOk = false;
        const int baud = m_baudCombo->currentText().trimmed().toInt(&baudOk);
        if (!baudOk || baud <= 0) {
            showWarning(AppI18n::text("波特率无效"), AppI18n::text("请输入大于 0 的波特率"));
            return;
        }
    }
    if (m_linkTypeCombo && m_linkTypeCombo->currentData().toString() == QStringLiteral("rtt") &&
        (config.host.trimmed().isEmpty() || config.port == 0)) {
        showWarning(AppI18n::text("连接参数无效"), AppI18n::text("请填写目标设备名称和正整数速率"));
        return;
    }
    if (m_linkTypeCombo && m_linkTypeCombo->currentData().toString() == QStringLiteral("rtt")) {
        // The probe is exclusive: another session holding RTT would make this
        // connect fail with no logs, so take the probe over Synex-style.
        for (WorkbenchPage *other : openWorkbenchPages()) {
            if (other != this && other->m_link.linkType() == LinkType::Rtt && other->m_link.isOpen()) {
                other->m_manualDisconnect = true;
                other->m_reconnectTimer.stop();
                other->m_link.closeLink();
            }
        }
    }
    if (m_linkTypeCombo && m_linkTypeCombo->currentData().toString() != QStringLiteral("serial") &&
        m_linkTypeCombo->currentData().toString() != QStringLiteral("rtt") &&
        (config.port == 0 || (config.type == LinkType::TcpClient && config.host.trimmed().isEmpty()))) {
        showWarning(AppI18n::text("网络参数无效"), AppI18n::text("请填写有效的主机地址和端口"));
        return;
    }

    m_manualDisconnect = false;
    m_lastConfig = config;
    m_link.openLink(config);
}

void WorkbenchPage::onLoopChanged(bool checked)
{
    m_loopIntervalEdit->setEnabled(checked);
    if (!checked) {
        m_loopTimer.stop();
        return;
    }
    if (!m_link.isOpen()) {
        m_loopCheck->setChecked(false);
        showWarning(AppI18n::text("无法循环发送"), AppI18n::text("请先连接串口"));
        return;
    }
    m_loopTimer.start(numberEditValue(m_loopIntervalEdit, 1000, 10, 600000));
}

void WorkbenchPage::setControlsEnabledForConnection(bool connected)
{
    if (!connected && m_macroRunning) {
        stopMacroSequence(AppI18n::text("串口已断开"), false);
    }

    m_sendButton->setEnabled(connected);
    m_sendEdit->setEnabled(connected);
    m_loopCheck->setEnabled(connected);
    m_loopIntervalEdit->setEnabled(connected && m_loopCheck->isChecked());
    updatePacketActionState();
    if (m_fileSendButton) {
        m_fileSendButton->setEnabled(connected && !m_fileSendFile.isOpen());
    }
    if (m_fileCancelButton) {
        m_fileCancelButton->setEnabled(m_fileSendFile.isOpen());
    }
    updateMacroActionState();
    m_rtsCheck->setEnabled(connected &&
                           flowControlFromData(m_flowControlCombo->currentData()) != QSerialPort::HardwareControl);
    m_dtrCheck->setEnabled(connected);

    m_portCombo->setEnabled(!connected);
    m_baudCombo->setEnabled(!connected);
    m_dataBitsCombo->setEnabled(!connected);
    m_parityCombo->setEnabled(!connected);
    m_stopBitsCombo->setEnabled(!connected);
    m_flowControlCombo->setEnabled(!connected);
    m_refreshButton->setEnabled(!connected);
    if (m_linkTypeCombo) {
        m_linkTypeCombo->setEnabled(!connected);
    }
    if (m_hostEdit) {
        m_hostEdit->setEnabled(!connected);
    }
    if (m_portEdit) {
        m_portEdit->setEnabled(!connected);
    }

    if (!connected) {
        m_loopCheck->setChecked(false);
        m_loopTimer.stop();
    }
}

int WorkbenchPage::maxRecordCount() const { return m_maxRecordCount; }

int WorkbenchPage::savedMaxRecordCount()
{
    AppSettings settings;
    return qBound(1000, settings.value(QStringLiteral("terminal/maxRecords"), DefaultMaxTerminalRecords).toInt(),
                  50000);
}

void WorkbenchPage::setTerminalRecordLimit(int count)
{
    m_maxRecordCount = qBound(1000, count, 50000);
    trimRecords();
    if (m_terminalView) {
        trimTerminalDocument();
    }
}
