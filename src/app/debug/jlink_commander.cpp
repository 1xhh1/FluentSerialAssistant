#include "app/debug/jlink_commander.h"

#include <QtCore/QDateTime>
#include <QtCore/QEventLoop>
#include <QtCore/QFile>
#include <QtCore/QProcess>
#include <QtCore/QRegularExpression>
#include <QtCore/QStandardPaths>
#include <QtCore/QTimer>

namespace {

constexpr int kBatchTimeoutMs = 20000;

// mem32 output: "20000000 = 5A5A5A5A" (one or more words per line).
const QRegularExpression kMemWordPattern(QStringLiteral("([0-9A-Fa-f]{8})\\s*=\\s*((?:[0-9A-Fa-f]{8}\\s*)+)"));

QByteArray wordsToBytes(const QString &words)
{
    QByteArray out;
    const QStringList list = words.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &word : list) {
        bool ok = false;
        const quint32 value = word.toUInt(&ok, 16);
        if (!ok) {
            continue;
        }
        // Commander prints words in little-endian byte order.
        out.append(static_cast<char>(value & 0xFF));
        out.append(static_cast<char>((value >> 8) & 0xFF));
        out.append(static_cast<char>((value >> 16) & 0xFF));
        out.append(static_cast<char>((value >> 24) & 0xFF));
    }
    return out;
}

QString failError(const QString &output)
{
    static const QStringList markers = {QStringLiteral("Cannot connect"), QStringLiteral("Failed to connect"),
                                        QStringLiteral("Unknown / Could not identify"),
                                        QStringLiteral("Error while identifying target"),
                                        QStringLiteral("Could not find expected RTT")};
    for (const QString &marker : markers) {
        if (output.contains(marker, Qt::CaseInsensitive)) {
            return output.trimmed().right(400);
        }
    }
    return QStringLiteral("J-Link reported an error");
}

} // namespace

JLinkCommander::JLinkCommander(QObject *parent)
    : QObject(parent)
{
}

JLinkCommander::~JLinkCommander()
{
    rttLoggerStop();
}

QString JLinkCommander::defaultExePath()
{
    return QStringLiteral("D:/SoftWare/CodeSoftWare/JLink/InstallRoute/JLink.exe");
}

QString JLinkCommander::defaultRttLoggerPath()
{
    return QStringLiteral("D:/SoftWare/CodeSoftWare/JLink/InstallRoute/JLinkRTTLogger.exe");
}

bool JLinkCommander::runBatch(const QString &exePath, const QString &device, int speedKhz,
                              const QStringList &commands, QString *output, QString *error)
{
    if (commands.isEmpty()) {
        if (error) {
            *error = QStringLiteral("no commands");
        }
        return false;
    }

    const QString scriptPath = QStringLiteral("%1/jlink_batch_%2.jlink")
                                   .arg(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                   .arg(QDateTime::currentMSecsSinceEpoch());
    QFile script(scriptPath);
    if (!script.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("Cannot write command file: %1").arg(script.errorString());
        }
        return false;
    }
    for (const QString &command : commands) {
        script.write((command + QLatin1Char('\n')).toUtf8());
    }
    script.write("qc\n");
    script.close();

    QProcess process;
    process.setProgram(exePath);
    process.setArguments({QStringLiteral("-device"), device, QStringLiteral("-if"), QStringLiteral("SWD"),
                          QStringLiteral("-Speed"), QString::number(speedKhz > 0 ? speedKhz : 4000),
                          QStringLiteral("-autoconnect"), QStringLiteral("1"), QStringLiteral("-NoGui"),
                          QStringLiteral("1"), QStringLiteral("-CommandFile"), scriptPath});
    process.start();
    QEventLoop loop;
    QTimer::singleShot(kBatchTimeoutMs, &loop, &QEventLoop::quit);
    QObject::connect(&process, &QProcess::finished, &loop, &QEventLoop::quit);
    QObject::connect(&process, &QProcess::errorOccurred, &loop, &QEventLoop::quit);
    loop.exec();

    const QString captured = QString::fromLocal8Bit(process.readAllStandardOutput());
    if (output) {
        *output = captured;
    }
    QFile::remove(scriptPath);

    if (process.state() == QProcess::Running) {
        process.kill();
        process.waitForFinished(2000);
        if (error) {
            *error = QStringLiteral("J-Link Commander timed out");
        }
        return false;
    }
    if (process.error() != QProcess::UnknownError || process.exitCode() != 0) {
        if (error) {
            *error = QStringLiteral("J-Link Commander exited with %1").arg(process.exitCode());
        }
        return false;
    }
    if (captured.contains(QStringLiteral("Cannot connect"), Qt::CaseInsensitive)
        || captured.contains(QStringLiteral("Failed to connect"), Qt::CaseInsensitive)
        || captured.contains(QStringLiteral("Could not identify"), Qt::CaseInsensitive)) {
        if (error) {
            *error = failError(captured);
        }
        return false;
    }
    return true;
}

QByteArray JLinkCommander::readMemory(const QString &exePath, const QString &device, int speedKhz, quint32 address,
                                      int byteCount, QString *error)
{
    const int words = (byteCount + 3) / 4;
    QString output;
    if (!runBatch(exePath, device, speedKhz,
                  {QStringLiteral("mem32 %1 %2")
                       .arg(address, 8, 16, QLatin1Char('0'))
                       .arg(words)},
                  &output, error)) {
        return {};
    }
    QByteArray bytes;
    QRegularExpressionMatchIterator it = kMemWordPattern.globalMatch(output);
    while (it.hasNext()) {
        bytes += wordsToBytes(it.next().captured(2));
    }
    if (bytes.size() < byteCount) {
        if (error) {
            *error = QStringLiteral("J-Link returned fewer bytes than requested");
        }
        return {};
    }
    bytes.truncate(byteCount);
    return bytes;
}

bool JLinkCommander::writeMemory(const QString &exePath, const QString &device, int speedKhz, quint32 address,
                                 const QByteArray &data, QString *error)
{
    QByteArray padded = data;
    while (padded.size() % 4 != 0) {
        padded.append('\0');
    }
    QStringList commands;
    for (int offset = 0; offset < padded.size(); offset += 4) {
        quint32 word = 0;
        // J-Link stores memory words in little-endian byte order.
        for (int i = 0; i < 4; ++i) {
            word |= static_cast<quint32>(static_cast<quint8>(padded.at(offset + i))) << (8 * i);
        }
        commands.append(QStringLiteral("w4 %1 %2")
                            .arg(address + static_cast<quint32>(offset), 8, 16, QLatin1Char('0'))
                            .arg(word, 8, 16, QLatin1Char('0')));
    }
    // Verify by reading the written range back.
    const int words = padded.size() / 4;
    commands.append(QStringLiteral("mem32 %1 %2")
                        .arg(address, 8, 16, QLatin1Char('0'))
                        .arg(words));

    QString output;
    if (!runBatch(exePath, device, speedKhz, commands, &output, error)) {
        return false;
    }
    QByteArray readback;
    QRegularExpressionMatchIterator it = kMemWordPattern.globalMatch(output);
    while (it.hasNext()) {
        readback += wordsToBytes(it.next().captured(2));
    }
    if (readback.left(data.size()) != data) {
        if (error) {
            *error = QStringLiteral("verify readback mismatch at 0x%1").arg(address, 8, 16, QLatin1Char('0'));
        }
        return false;
    }
    return true;
}

bool JLinkCommander::halt(const QString &exePath, const QString &device, int speedKhz, QString *error)
{
    QString output;
    return runBatch(exePath, device, speedKhz, {QStringLiteral("h")}, &output, error);
}

bool JLinkCommander::run(const QString &exePath, const QString &device, int speedKhz, QString *error)
{
    QString output;
    return runBatch(exePath, device, speedKhz, {QStringLiteral("g")}, &output, error);
}

bool JLinkCommander::rttLoggerStart(const QString &exePath, const QString &device, int speedKhz, int channel,
                                    QString *error)
{
    rttLoggerStop();
    if (exePath.isEmpty()) {
        if (error) {
            *error = QStringLiteral("JLinkRTTLogger.exe path is empty");
        }
        return false;
    }
    m_rttLogFile = QStringLiteral("%1/FluentRTT_%2.log")
                       .arg(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                       .arg(QDateTime::currentMSecsSinceEpoch());
    m_rttLoggerProcess = new QProcess(this);
    m_rttLoggerProcess->setProgram(exePath);
    m_rttLoggerProcess->setArguments({QStringLiteral("-Device"), device, QStringLiteral("-If"), QStringLiteral("SWD"),
                                      QStringLiteral("-Speed"), QString::number(speedKhz > 0 ? speedKhz : 4000),
                                      QStringLiteral("-RTTChannel"), QString::number(channel),
                                      QStringLiteral("-NoGui"), QStringLiteral("1"), m_rttLogFile});
    m_rttLoggerProcess->start();
    if (!m_rttLoggerProcess->waitForStarted(5000)) {
        if (error) {
            *error = QStringLiteral("Cannot start %1: %2").arg(exePath, m_rttLoggerProcess->errorString());
        }
        delete m_rttLoggerProcess;
        m_rttLoggerProcess = nullptr;
        return false;
    }
    if (!m_rttLoggerProcess->waitForReadyRead(15000) && m_rttLoggerProcess->state() == QProcess::NotRunning) {
        if (error) {
            *error = QStringLiteral("RTT logger exited before connecting: %1").arg(rttLoggerError());
        }
        rttLoggerStop();
        return false;
    }
    return true;
}

void JLinkCommander::rttLoggerStop()
{
    if (m_rttLoggerProcess) {
        if (m_rttLoggerProcess->state() != QProcess::NotRunning) {
            m_rttLoggerProcess->kill();
            m_rttLoggerProcess->waitForFinished(2000);
        }
        delete m_rttLoggerProcess;
        m_rttLoggerProcess = nullptr;
    }
    m_rttTailOffset = 0;
    m_rttLoggerError.clear();
}

bool JLinkCommander::rttLoggerRunning() const
{
    return m_rttLoggerProcess && m_rttLoggerProcess->state() == QProcess::Running;
}

QByteArray JLinkCommander::rttLoggerTail()
{
    if (!m_rttLoggerProcess || m_rttLogFile.isEmpty()) {
        return {};
    }
    QFile log(m_rttLogFile);
    if (!log.open(QIODevice::ReadOnly) || log.size() <= m_rttTailOffset) {
        return {};
    }
    log.seek(m_rttTailOffset);
    const QByteArray data = log.readAll();
    m_rttTailOffset += data.size();
    return data;
}

QString JLinkCommander::rttLoggerError()
{
    if (m_rttLoggerProcess) {
        m_rttLoggerError = m_rttLoggerProcess->readAllStandardError();
    }
    return m_rttLoggerError;
}
