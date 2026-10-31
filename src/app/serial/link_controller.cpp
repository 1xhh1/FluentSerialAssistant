#include "app/serial/link_controller.h"

#include <QtNetwork/QHostAddress>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <QtCore/QDateTime>
#include <QtCore/QFile>
#include <QtCore/QStandardPaths>
#include <QtCore/QProcess>
#include <QtCore/QTimer>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <QtNetwork/QUdpSocket>

namespace {

QString peerName(QTcpSocket *socket)
{
    return QStringLiteral("%1:%2").arg(socket->peerAddress().toString()).arg(socket->peerPort());
}

} // namespace

QString linkTypeKey(LinkType type)
{
    switch (type) {
    case LinkType::TcpServer:
        return QStringLiteral("tcp-server");
    case LinkType::TcpClient:
        return QStringLiteral("tcp-client");
    case LinkType::Udp:
        return QStringLiteral("udp");
    case LinkType::Serial:
        break;
    }
    return QStringLiteral("serial");
}

LinkType linkTypeFromKey(const QString &key)
{
    const QString normalized = key.trimmed().toLower();
    if (normalized == QStringLiteral("tcp-server")) {
        return LinkType::TcpServer;
    }
    if (normalized == QStringLiteral("tcp-client")) {
        return LinkType::TcpClient;
    }
    if (normalized == QStringLiteral("udp")) {
        return LinkType::Udp;
    }
    if (normalized == QStringLiteral("rtt")) {
        return LinkType::Rtt;
    }
    return LinkType::Serial;
}

QString LinkConfig::key() const { return linkTypeKey(type); }

LinkController::LinkController(QObject *parent) : QObject(parent)
{
    m_rttTimer = new QTimer(this);
    m_rttTimer->setInterval(50);
    connect(m_rttTimer, &QTimer::timeout, this, &LinkController::pollRttLog);

    const auto forwardOpened = [this](const QString &name) {
        emit opened(displayName().isEmpty() ? name : displayName());
    };
    connect(&m_serial, &SerialController::opened, this, forwardOpened);
    connect(&m_serial, &SerialController::closed, this, &LinkController::closed);
    connect(&m_serial, &SerialController::dataReceived, this, &LinkController::dataReceived);
    connect(&m_serial, &SerialController::writeQueued, this, &LinkController::writeQueued);
    connect(&m_serial, &SerialController::errorOccurred, this, &LinkController::errorOccurred);
}

LinkController::~LinkController() { closeLink(); }

bool LinkController::isOpen() const
{
    switch (m_config.type) {
    case LinkType::Serial:
        return m_serial.isOpen();
    case LinkType::TcpServer:
        return m_server && m_server->isListening();
    case LinkType::TcpClient:
        return m_clientSocket && m_clientSocket->state() == QAbstractSocket::ConnectedState;
    case LinkType::Udp:
        return m_udp && m_udp->state() == QAbstractSocket::BoundState;
    case LinkType::Rtt:
        return m_rttLogger && m_rttLogger->state() == QProcess::Running;
    }
    return false;
}

LinkType LinkController::linkType() const { return m_config.type; }

QString LinkController::displayName() const
{
    switch (m_config.type) {
    case LinkType::Serial:
        return m_serial.isOpen() ? m_serial.portName() : m_config.serial.portName;
    case LinkType::TcpServer:
        return QStringLiteral(":%1").arg(m_config.port);
    case LinkType::TcpClient:
        return QStringLiteral("%1:%2").arg(m_config.host, QString::number(m_config.port));
    case LinkType::Udp:
        return QStringLiteral("%1:%2").arg(m_config.host, QString::number(m_config.port));
    case LinkType::Rtt:
        return QStringLiteral("%1 (RTT)").arg(m_config.host);
    }
    return {};
}

QString LinkController::errorString() const
{
    if (!m_error.isEmpty()) {
        return m_error;
    }
    if (m_config.type == LinkType::Serial) {
        return m_serial.errorString();
    }
    return {};
}

int LinkController::peerCount() const { return m_config.type == LinkType::TcpServer ? m_clients.size() : 0; }

bool LinkController::openLink(const LinkConfig &config)
{
    closeLink();
    m_error.clear();
    m_config = config;

    switch (config.type) {
    case LinkType::Serial:
        return m_serial.openPort(config.serial);
    case LinkType::TcpServer: {
        if (!m_server) {
            m_server = new QTcpServer(this);
            connect(m_server, &QTcpServer::newConnection, this, [this]() {
                while (QTcpSocket *socket = m_server->nextPendingConnection()) {
                    m_clients.append(socket);
                    connect(socket, &QTcpSocket::readyRead, this, &LinkController::readSocket);
                    connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
                        m_clients.removeOne(socket);
                        emit peerDisconnected(peerName(socket));
                        socket->deleteLater();
                    });
                    emit peerConnected(peerName(socket));
                }
            });
        }
        if (!m_server->listen(QHostAddress::Any, config.port)) {
            m_error = m_server->errorString();
            return false;
        }
        emit opened(displayName());
        return true;
    }
    case LinkType::TcpClient: {
        if (!m_clientSocket) {
            m_clientSocket = new QTcpSocket(this);
            connect(m_clientSocket, &QTcpSocket::readyRead, this, &LinkController::readSocket);
            connect(m_clientSocket, &QTcpSocket::disconnected, this, [this]() {
                if (m_config.type == LinkType::TcpClient) {
                    emit closed();
                }
            });
            connect(m_clientSocket, &QAbstractSocket::errorOccurred, this, [this](QAbstractSocket::SocketError error) {
                if (error == QAbstractSocket::RemoteHostClosedError ||
                    error == QAbstractSocket::ConnectionRefusedError || error == QAbstractSocket::HostNotFoundError ||
                    error == QAbstractSocket::NetworkError) {
                    m_error = m_clientSocket->errorString();
                    emit errorOccurred(m_error);
                }
            });
        }
        m_clientSocket->connectToHost(config.host, config.port);
        if (!m_clientSocket->waitForConnected(3000)) {
            m_error = m_clientSocket->errorString();
            return false;
        }
        emit opened(displayName());
        return true;
    }
    case LinkType::Rtt: {
        if (!config.rttLoggerPath.isEmpty()) {
            m_rttLogger = new QProcess(this);
            m_rttLogger->setProgram(config.rttLoggerPath);
            // The log file must be the last positional argument; -NoGui would
            // swallow it (verified with V7.80).
            m_rttLogger->setArguments({QStringLiteral("-Device"), config.host,
                                       QStringLiteral("-If"), QStringLiteral("SWD"),
                                       QStringLiteral("-Speed"), QString::number(config.port),
                                       QStringLiteral("-RTTChannel"), QString::number(config.localPort),
                                       m_rttLogFile = QStringLiteral("%1/FluentRTT_%2.log")
                                                          .arg(QStandardPaths::writableLocation(
                                                                   QStandardPaths::TempLocation))
                                                          .arg(QDateTime::currentMSecsSinceEpoch())});
            m_rttLogger->start();
            if (!m_rttLogger->waitForStarted(5000)) {
                m_error = QStringLiteral("Cannot start %1: %2").arg(config.rttLoggerPath, m_rttLogger->errorString());
                delete m_rttLogger;
                m_rttLogger = nullptr;
                return false;
            }
            if (!m_rttLogger->waitForReadyRead(15000) && m_rttLogger->state() == QProcess::NotRunning) {
                m_error = QString::fromLocal8Bit(m_rttLogger->readAllStandardError());
                if (m_error.isEmpty()) {
                    m_error = QStringLiteral("RTT logger exited before connecting");
                }
                stopRttLogger();
                return false;
            }
            m_rttTailOffset = 0;
            m_rttTimer->start();
            emit opened(displayName());
            return true;
        }
        m_error = QStringLiteral("JLinkRTTLogger.exe path is empty");
        return false;
    }
    case LinkType::Udp: {
        if (!m_udp) {
            m_udp = new QUdpSocket(this);
            connect(m_udp, &QUdpSocket::readyRead, this, &LinkController::readDatagrams);
        }
        const bool bound = config.localPort != 0 ? m_udp->bind(config.localPort) : m_udp->bind();
        if (!bound) {
            m_error = m_udp->errorString();
            return false;
        }
        emit opened(displayName());
        return true;
    }
    }
    return false;
}

void LinkController::closeLink()
{
    const LinkType previousType = m_config.type;
    m_config = LinkConfig{};

    if (m_server) {
        m_server->close();
    }
    if (m_clientSocket) {
        m_clientSocket->abort();
    }
    if (m_udp) {
        m_udp->close();
    }
    stopRttLogger();
    for (QTcpSocket *socket : std::as_const(m_clients)) {
        socket->abort();
        socket->deleteLater();
    }
    m_clients.clear();
    // The serial controller emits closed() itself; network links report it here.
    if (previousType == LinkType::Serial) {
        m_serial.closePort();
    } else {
        emit closed();
    }
}

void LinkController::readSocket()
{
    const QList<QTcpSocket *> sources =
        m_config.type == LinkType::TcpServer ? QList<QTcpSocket *>(m_clients) : QList<QTcpSocket *>{m_clientSocket};
    for (QTcpSocket *socket : sources) {
        while (socket && socket->bytesAvailable() > 0) {
            emit dataReceived(socket->readAll());
        }
    }
}

void LinkController::readDatagrams()
{
    while (m_udp && m_udp->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(int(m_udp->pendingDatagramSize()));
        m_udp->readDatagram(datagram.data(), datagram.size());
        emit dataReceived(datagram);
    }
}

bool LinkController::writeData(const QByteArray &data, QString *error)
{
    if (!isOpen()) {
        if (error) {
            *error = QStringLiteral("link is not open");
        }
        return false;
    }

    bool ok = false;
    switch (m_config.type) {
    case LinkType::Serial:
        ok = m_serial.writeData(data, error);
        break;
    case LinkType::TcpServer: {
        for (QTcpSocket *socket : std::as_const(m_clients)) {
            ok = socket->write(data) >= 0 || ok;
        }
        break;
    }
    case LinkType::TcpClient:
        ok = m_clientSocket->write(data) >= 0;
        if (!ok) {
            m_error = m_clientSocket->errorString();
        }
        break;
    case LinkType::Udp:
        ok = m_udp->writeDatagram(data, QHostAddress(m_config.host), m_config.port) >= 0;
        if (!ok) {
            m_error = m_udp->errorString();
        }
        break;
    case LinkType::Rtt:
        if (error) {
            *error = QStringLiteral("RTT down-channel writes are not supported yet");
        }
        ok = false;
        break;
    }
    if (error && !ok && m_config.type != LinkType::Serial && !m_error.isEmpty()) {
        *error = m_error;
    }
    return ok;
}

qint64 LinkController::bytesToWrite() const
{
    switch (m_config.type) {
    case LinkType::Serial:
        return m_serial.bytesToWrite();
    case LinkType::TcpServer: {
        qint64 total = 0;
        for (QTcpSocket *socket : std::as_const(m_clients)) {
            total += socket->bytesToWrite();
        }
        return total;
    }
    case LinkType::TcpClient:
        return m_clientSocket ? m_clientSocket->bytesToWrite() : 0;
    case LinkType::Udp:
        return 0;
    case LinkType::Rtt:
        return 0;
    }
    return 0;
}

void LinkController::pollRttLog()
{
    if (!m_rttLogger || m_rttLogFile.isEmpty()) {
        return;
    }
    QFile log(m_rttLogFile);
    if (!log.open(QIODevice::ReadOnly)) {
        return;
    }
    if (log.size() > m_rttTailOffset) {
        log.seek(m_rttTailOffset);
        const QByteArray data = log.readAll();
        m_rttTailOffset += data.size();
        if (!data.isEmpty()) {
            emit dataReceived(data);
        }
    }
    if (m_rttLogger->state() != QProcess::Running) {
        m_error = QString::fromLocal8Bit(m_rttLogger->readAllStandardError());
        emit errorOccurred(m_error.isEmpty() ? QStringLiteral("RTT logger exited") : m_error);
        closeLink();
    }
}

void LinkController::stopRttLogger()
{
    if (m_rttTimer) {
        m_rttTimer->stop();
    }
    if (m_rttLogger) {
        if (m_rttLogger->state() != QProcess::NotRunning) {
            m_rttLogger->kill();
            m_rttLogger->waitForFinished(2000);
        }
        delete m_rttLogger;
        m_rttLogger = nullptr;
    }
    m_rttTailOffset = 0;
}

bool LinkController::setRequestToSend(bool enabled)
{
    return m_config.type == LinkType::Serial && m_serial.setRequestToSend(enabled);
}

bool LinkController::setDataTerminalReady(bool enabled)
{
    return m_config.type == LinkType::Serial && m_serial.setDataTerminalReady(enabled);
}
