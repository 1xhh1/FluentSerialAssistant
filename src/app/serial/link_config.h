#pragma once

#include "app/serial/serial_controller.h"

#include <QtCore/QString>

enum class LinkType
{
    Serial,
    TcpServer,
    TcpClient,
    Udp,
    Rtt
};

// Connection parameters for a workbench session link. Serial keeps the full
// SerialPortConfig; network links need a peer address (client/UDP) or a listen
// port (server). RTT runs through JLinkRTTLogger: host carries the device
// name, port the speed in kHz, localPort the RTT channel, and rttLoggerPath
// the JLinkRTTLogger.exe location.
struct LinkConfig
{
    LinkType type = LinkType::Serial;
    SerialPortConfig serial;
    QString host;
    quint16 port = 0;
    quint16 localPort = 0;
    QString rttLoggerPath;

    LinkConfig() = default;
    LinkConfig(const SerialPortConfig &serialConfig) : type(LinkType::Serial), serial(serialConfig) {}

    QString key() const; // stable settings/IPC key: "serial", "tcp-server", ...
};

QString linkTypeKey(LinkType type);
LinkType linkTypeFromKey(const QString &key);
