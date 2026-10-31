#pragma once

#include "app/serial/link_config.h"

#include <QtCore/QList>
#include <QtCore/QObject>

class QTcpServer;
class QTcpSocket;
class QUdpSocket;

// Transport-agnostic session link. Serial delegates to SerialController (so
// virtual pair logic is unchanged); TCP server/client and UDP reuse the same
// signal and writeData surface the workbench already expects.
class LinkController : public QObject
{
    Q_OBJECT

  public:
    explicit LinkController(QObject *parent = nullptr);
    ~LinkController() override;

    bool isOpen() const;
    LinkType linkType() const;
    // Port name for serial, "host:port" for client/UDP, ":port" for a server.
    QString displayName() const;
    QString errorString() const;
    int peerCount() const;

    bool openLink(const LinkConfig &config);
    void closeLink();
    bool writeData(const QByteArray &data, QString *error = nullptr);
    // Bytes accepted by writeData() not yet handed to the transport.
    qint64 bytesToWrite() const;
    bool setRequestToSend(bool enabled);
    bool setDataTerminalReady(bool enabled);

  signals:
    void opened(const QString &displayName);
    void closed();
    void dataReceived(const QByteArray &data);
    void writeQueued(qint64 bytes);
    void errorOccurred(const QString &message);
    void peerConnected(const QString &peer);
    void peerDisconnected(const QString &peer);

  private:
    void readSocket();
    void readDatagrams();

    SerialController m_serial;
    QTcpServer *m_server = nullptr;
    QUdpSocket *m_udp = nullptr;
    QList<QTcpSocket *> m_clients;
    QTcpSocket *m_clientSocket = nullptr;
    LinkConfig m_config;
    QString m_udpHost;
    QString m_error;
};
