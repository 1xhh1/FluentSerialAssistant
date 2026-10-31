#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QObject>
#include <QtCore/QString>

class QProcess;

// Drives SEGGER's J-Link Commander (JLink.exe) in batch mode: every memory
// operation spawns a short-lived JLink.exe with a generated command file and
// parses the output. The interactive DLL/stdin paths crash or block-buffer
// with foreign runtimes (verified with V7.80), while batch mode is the
// supported robust surface. RTT streaming goes through JLinkRTTLogger.exe,
// which owns the probe exclusively while it runs.
class JLinkCommander : public QObject
{
    Q_OBJECT

  public:
    explicit JLinkCommander(QObject *parent = nullptr);
    ~JLinkCommander() override;

    static QString defaultExePath();
    static QString defaultRttLoggerPath();
    static QString defaultDllPath();

    // Enumerates the J-Link device database through JLINK_DEVICE_GetInfo and
    // writes "vendor|device|flashBytes|ramBytes" lines. Returns the number of
    // devices, or -1 with error set.
    static int exportDeviceList(const QString &dllPath, const QString &outputFile, QString *error);

    // Synchronous: runs one JLink.exe per call (UI keeps pumping events
    // through QEventLoop). device like "STM32G431RB" or "Cortex-M4".
    QByteArray readMemory(const QString &exePath, const QString &device, int speedKhz, quint32 address,
                          int byteCount, QString *error);
    // Writes bytes (padded to words) and verifies by reading back.
    bool writeMemory(const QString &exePath, const QString &device, int speedKhz, quint32 address,
                     const QByteArray &data, QString *error);
    bool halt(const QString &exePath, const QString &device, int speedKhz, QString *error);
    bool run(const QString &exePath, const QString &device, int speedKhz, QString *error);

    // RTT runs through JLinkRTTLogger.exe, which needs the probe for itself.
    bool rttLoggerStart(const QString &exePath, const QString &device, int speedKhz, int channel, QString *error);
    void rttLoggerStop();
    bool rttLoggerRunning() const;
    QByteArray rttLoggerTail();
    QString rttLoggerError();

  private:
    bool runBatch(const QString &exePath, const QString &device, int speedKhz, const QStringList &commands,
                  QString *output, QString *error);

    QProcess *m_rttLoggerProcess = nullptr;
    QString m_rttLogFile;
    qint64 m_rttTailOffset = 0;
    QString m_rttLoggerError;
};
