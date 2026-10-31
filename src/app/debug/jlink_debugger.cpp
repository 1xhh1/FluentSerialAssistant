#include "app/debug/jlink_debugger.h"

#include <QtCore/QLibrary>
#include <QtCore/QPointer>

namespace {

// J-Link DLL prototypes (SEGGER UM08001). Signatures are stable across
// releases; anything missing marks the DLL as unusable instead of crashing.
using JLINK_Open_fn = int (*)(void);
using JLINK_Close_fn = void (*)(void);
using JLINK_ExecCommand_fn = int (*)(const char *);
using JLINK_TIF_Select_fn = int (*)(int);
using JLINK_SetSpeed_fn = int (*)(int);
using JLINK_Connect_fn = int (*)(void);
using JLINK_IsConnected_fn = int (*)(void);
using JLINK_Halt_fn = int (*)(void);
using JLINK_Reset_fn = int (*)(void);
using JLINK_ReadMem_fn = int (*)(unsigned int, unsigned int, void *);
using JLINK_WriteMem_fn = int (*)(unsigned int, unsigned int, const void *);
using JLINK_RTTERMINAL_Open_fn = int (*)(const char *);
using JLINK_RTTERMINAL_Close_fn = int (*)(const char *);
using JLINK_RTTERMINAL_Read_fn = int (*)(int, char *, unsigned int);
using JLINK_RTTERMINAL_Write_fn = int (*)(int, const char *, unsigned int);

constexpr int kTifSwd = 1;

} // namespace

struct JLinkDebugger::Symbols
{
    JLINK_Open_fn open = nullptr;
    JLINK_Close_fn close = nullptr;
    JLINK_ExecCommand_fn execCommand = nullptr;
    JLINK_TIF_Select_fn tifSelect = nullptr;
    JLINK_SetSpeed_fn setSpeed = nullptr;
    JLINK_Connect_fn connect = nullptr;
    JLINK_IsConnected_fn isConnected = nullptr;
    JLINK_Halt_fn halt = nullptr;
    JLINK_Reset_fn reset = nullptr;
    JLINK_ReadMem_fn readMem = nullptr;
    JLINK_WriteMem_fn writeMem = nullptr;
    JLINK_RTTERMINAL_Open_fn rttOpen = nullptr;
    JLINK_RTTERMINAL_Close_fn rttClose = nullptr;
    JLINK_RTTERMINAL_Read_fn rttRead = nullptr;
    JLINK_RTTERMINAL_Write_fn rttWrite = nullptr;
};

JLinkDebugger::JLinkDebugger(QObject *parent)
    : QObject(parent)
    , m_symbols(new Symbols)
{
}

JLinkDebugger::~JLinkDebugger()
{
    unload();
    delete m_symbols;
}

QString JLinkDebugger::defaultDllPath()
{
    return QStringLiteral("C:/Program Files/SEGGER/JLink/JLink_x64.dll");
}

bool JLinkDebugger::load(const QString &dllPath, QString *error)
{
    if (m_library) {
        return true;
    }
    m_dllPath = dllPath;
    m_library = new QLibrary(m_dllPath, this);
    if (!m_library->load()) {
        if (error) {
            *error = m_library->errorString();
        }
        emit errorOccurred(m_library->errorString());
        delete m_library;
        m_library = nullptr;
        return false;
    }

    auto resolve = [this](const char *symbol) { return m_library->resolve(symbol); };
    m_symbols->open = reinterpret_cast<JLINK_Open_fn>(resolve("JLINK_Open"));
    m_symbols->close = reinterpret_cast<JLINK_Close_fn>(resolve("JLINK_Close"));
    m_symbols->execCommand = reinterpret_cast<JLINK_ExecCommand_fn>(resolve("JLINK_ExecCommand"));
    m_symbols->tifSelect = reinterpret_cast<JLINK_TIF_Select_fn>(resolve("JLINK_TIF_Select"));
    m_symbols->setSpeed = reinterpret_cast<JLINK_SetSpeed_fn>(resolve("JLINK_SetSpeed"));
    m_symbols->connect = reinterpret_cast<JLINK_Connect_fn>(resolve("JLINK_Connect"));
    m_symbols->isConnected = reinterpret_cast<JLINK_IsConnected_fn>(resolve("JLINK_IsConnected"));
    m_symbols->halt = reinterpret_cast<JLINK_Halt_fn>(resolve("JLINK_Halt"));
    m_symbols->reset = reinterpret_cast<JLINK_Reset_fn>(resolve("JLINK_Reset"));
    m_symbols->readMem = reinterpret_cast<JLINK_ReadMem_fn>(resolve("JLINK_ReadMem"));
    m_symbols->writeMem = reinterpret_cast<JLINK_WriteMem_fn>(resolve("JLINK_WriteMem"));
    m_symbols->rttOpen = reinterpret_cast<JLINK_RTTERMINAL_Open_fn>(resolve("JLINK_RTTERMINAL_Open"));
    m_symbols->rttClose = reinterpret_cast<JLINK_RTTERMINAL_Close_fn>(resolve("JLINK_RTTERMINAL_Close"));
    m_symbols->rttRead = reinterpret_cast<JLINK_RTTERMINAL_Read_fn>(resolve("JLINK_RTTERMINAL_Read"));
    m_symbols->rttWrite = reinterpret_cast<JLINK_RTTERMINAL_Write_fn>(resolve("JLINK_RTTERMINAL_Write"));

    if (!m_symbols->open || !m_symbols->connect || !m_symbols->readMem || !m_symbols->writeMem) {
        if (error) {
            *error = QStringLiteral("J-Link DLL missing required exports: %1").arg(m_dllPath);
        }
        m_library->unload();
        delete m_library;
        m_library = nullptr;
        return false;
    }
    return true;
}

bool JLinkDebugger::isLoaded() const { return m_library != nullptr; }

void JLinkDebugger::unload()
{
    if (!m_library) {
        return;
    }
    disconnectTarget();
    m_library->unload();
    delete m_library;
    m_library = nullptr;
}

bool JLinkDebugger::connectTarget(const QString &device, int speedKhz, QString *error)
{
    if (!load(m_dllPath, error)) {
        return false;
    }
    
    const QString command = QStringLiteral("device = %1").arg(device);
    if (m_symbols->execCommand && m_symbols->execCommand(command.toUtf8().constData()) < 0) {
        if (error) {
            *error = QStringLiteral("JLINK_ExecCommand(device=%1) failed").arg(device);
        }
        return false;
    }
    if (m_symbols->tifSelect && m_symbols->tifSelect(kTifSwd) < 0) {
        if (error) {
            *error = QStringLiteral("JLINK_TIF_Select(SWD) failed");
        }
        return false;
    }
    if (m_symbols->setSpeed) {
        m_symbols->setSpeed(speedKhz > 0 ? speedKhz : 4000);
    }
    if (m_symbols->connect() < 0) {
        if (error) {
            *error = QStringLiteral("JLINK_Connect failed: no probe attached or target not responding");
        }
        return false;
    }
    m_targetConnected = true;
    return true;
}

void JLinkDebugger::disconnectTarget()
{
    if (!m_library) {
        return;
    }
    if (m_rttOpen && m_symbols->rttClose) {
        m_symbols->rttClose("");
        m_rttOpen = false;
    }
    if (m_targetConnected) {
        if (m_symbols->isConnected && m_symbols->isConnected() > 0 && m_symbols->close) {
            m_symbols->close();
        }
        m_targetConnected = false;
    }
}

bool JLinkDebugger::isConnected() const
{
    if (!m_library || !m_targetConnected) {
        return false;
    }
    return m_symbols->isConnected ? m_symbols->isConnected() > 0 : false;
}

QByteArray JLinkDebugger::readMemory(quint32 address, int size, QString *error)
{
    if (!isConnected()) {
        if (error) {
            *error = QStringLiteral("J-Link target is not connected");
        }
        return {};
    }
    QByteArray buffer(size, Qt::Uninitialized);
    if (m_symbols->readMem(address, static_cast<unsigned int>(size), buffer.data()) < 0) {
        if (error) {
            *error = QStringLiteral("JLINK_ReadMem(0x%1) failed").arg(address, 8, 16, QLatin1Char('0'));
        }
        emit errorOccurred(*error);
        return {};
    }
    return buffer;
}

bool JLinkDebugger::writeMemory(quint32 address, const QByteArray &data, bool verify, QString *error)
{
    if (!isConnected()) {
        if (error) {
            *error = QStringLiteral("J-Link target is not connected");
        }
        return false;
    }
    if (m_symbols->writeMem(address, static_cast<unsigned int>(data.size()), data.constData()) < 0) {
        if (error) {
            *error = QStringLiteral("JLINK_WriteMem(0x%1) failed").arg(address, 8, 16, QLatin1Char('0'));
        }
        emit errorOccurred(*error);
        return false;
    }
    if (verify) {
        const QByteArray readback = readMemory(address, data.size(), error);
        if (readback != data) {
            if (error && error->isEmpty()) {
                *error = QStringLiteral("verify readback mismatch at 0x%1").arg(address, 8, 16, QLatin1Char('0'));
            }
            return false;
        }
    }
    return true;
}

bool JLinkDebugger::haltTarget(QString *error)
{
    if (!isConnected()) {
        if (error) {
            *error = QStringLiteral("J-Link target is not connected");
        }
        return false;
    }
    if (m_symbols->halt && m_symbols->halt() < 0) {
        if (error) {
            *error = QStringLiteral("JLINK_Halt failed");
        }
        return false;
    }
    return true;
}

bool JLinkDebugger::runTarget(QString *error)
{
    if (!isConnected()) {
        if (error) {
            *error = QStringLiteral("J-Link target is not connected");
        }
        return false;
    }
    if (m_symbols->reset && m_symbols->reset() < 0) {
        if (error) {
            *error = QStringLiteral("JLINK_Reset failed");
        }
        return false;
    }
    return true;
}

bool JLinkDebugger::rttStart(QString *error)
{
    if (!isConnected()) {
        if (error) {
            *error = QStringLiteral("J-Link target is not connected");
        }
        return false;
    }
    if (!m_symbols->rttOpen) {
        if (error) {
            *error = QStringLiteral("J-Link DLL has no RTT support");
        }
        return false;
    }
    if (m_symbols->rttOpen("") < 0) {
        if (error) {
            *error = QStringLiteral("JLINK_RTTERMINAL_Open failed (RTT control block not found?)");
        }
        return false;
    }
    m_rttOpen = true;
    return true;
}

void JLinkDebugger::rttStop()
{
    if (!m_rttOpen || !m_library) {
        return;
    }
    if (m_symbols->rttClose) {
        m_symbols->rttClose("");
    }
    m_rttOpen = false;
}

bool JLinkDebugger::rttRunning() const { return m_rttOpen; }

QByteArray JLinkDebugger::rttRead(int channel, int maxBytes, QString *error)
{
    if (!m_rttOpen) {
        return {};
    }
    QByteArray buffer(maxBytes, Qt::Uninitialized);
    const int received = m_symbols->rttRead(channel, buffer.data(), static_cast<unsigned int>(maxBytes));
    if (received < 0) {
        if (error) {
            *error = QStringLiteral("JLINK_RTTERMINAL_Read failed");
        }
        return {};
    }
    buffer.truncate(received);
    return buffer;
}

bool JLinkDebugger::rttWrite(int channel, const QByteArray &data, QString *error)
{
    if (!m_rttOpen) {
        if (error) {
            *error = QStringLiteral("RTT is not open");
        }
        return false;
    }
    if (m_symbols->rttWrite(channel, data.constData(), static_cast<unsigned int>(data.size())) < 0) {
        if (error) {
            *error = QStringLiteral("JLINK_RTTERMINAL_Write failed");
        }
        return false;
    }
    return true;
}
