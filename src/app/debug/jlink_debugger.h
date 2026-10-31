#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QObject>
#include <QtCore/QString>

class QLibrary;

// J-Link DLL wrapper (loaded through QLibrary, so no SDK import lib is
// needed). Covers the Synex-style debugging surfaces: SWD target connect,
// memory read/write with verify, and RTT terminal polling. All calls are
// synchronous; the DLL must live on the same thread as this object.
class JLinkDebugger : public QObject
{
    Q_OBJECT

  public:
    explicit JLinkDebugger(QObject *parent = nullptr);
    ~JLinkDebugger() override;

    static QString defaultDllPath();

    // Loads the DLL and resolves the symbols. Returns false with error set
    // when the DLL is missing or a required export is absent.
    bool load(const QString &dllPath, QString *error);
    bool isLoaded() const;

    // Opens the probe, selects device/speed/interface and connects over SWD.
    bool connectTarget(const QString &device, int speedKhz, QString *error);
    void disconnectTarget();
    bool isConnected() const;

    // Raw memory access; addresses are absolute target addresses.
    QByteArray readMemory(quint32 address, int size, QString *error);
    bool writeMemory(quint32 address, const QByteArray &data, bool verify, QString *error);
    bool haltTarget(QString *error);
    bool runTarget(QString *error);

    // RTT: open the terminal channels, then poll readRtt() from a timer.
    bool rttStart(QString *error);
    void rttStop();
    bool rttRunning() const;
    QByteArray rttRead(int channel, int maxBytes, QString *error);
    bool rttWrite(int channel, const QByteArray &data, QString *error);

  signals:
    void errorOccurred(const QString &message);

  private:
    struct Symbols;
    void unload();

    QLibrary *m_library = nullptr;
    Symbols *m_symbols = nullptr;
    QString m_dllPath;
    bool m_targetConnected = false;
    bool m_rttOpen = false;
};
