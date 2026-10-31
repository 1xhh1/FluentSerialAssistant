#include "app/core/script_runner.h"
#include "app/serial/virtual_serial_pair.h"
#include "app/view/settings_page.h"
#include "app/view/workbench/workbench_page_internal.h"
#include "app/view/workbench_sessions_page.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QLibrary>
#include <QtCore/QScopeGuard>
#include <QtCore/QTemporaryDir>
#include <QtCore/QThread>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <QtTest/QTest>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#endif

using namespace WorkbenchPagePrivate;

class WorkbenchReliabilityTest : public QObject
{
    Q_OBJECT

  private:
    // Working set in KiB (Windows); -1 when unavailable.
    static qint64 processRssKb()
    {
#ifdef Q_OS_WIN
        typedef BOOL(WINAPI * GetProcessMemoryInfoFn)(HANDLE, void *, DWORD);
        struct MemoryCounters
        {
            DWORD cb;
            DWORD PageFaultCount;
            SIZE_T PeakWorkingSetSize;
            SIZE_T WorkingSetSize;
            SIZE_T QuotaPeakPagedPoolUsage;
            SIZE_T QuotaPagedPoolUsage;
            SIZE_T QuotaPeakNonPagedPoolUsage;
            SIZE_T QuotaNonPagedPoolUsage;
            SIZE_T PagefileUsage;
            SIZE_T PeakPagefileUsage;
        };
        static GetProcessMemoryInfoFn fn = nullptr;
        if (!fn) {
            QLibrary kernel(QStringLiteral("kernel32"));
            fn = reinterpret_cast<GetProcessMemoryInfoFn>(kernel.resolve("K32GetProcessMemoryInfo"));
        }
        if (!fn) {
            return -1;
        }
        MemoryCounters counters{};
        counters.cb = sizeof(counters);
        if (!fn(GetCurrentProcess(), &counters, sizeof(counters))) {
            return -1;
        }
        return static_cast<qint64>(counters.WorkingSetSize / 1024);
#else
        return -1;
#endif
    }

    static void connectVirtualPort(WorkbenchPage &page, const QString &port = VirtualSerialPair::portAName())
    {
        VirtualSerialPair::instance()->setEnabled(true);
        page.refreshPorts();
        page.m_portCombo->setCurrentIndex(page.m_portCombo->findData(port));
        page.m_connectButton->click();
        QVERIFY(page.m_link.isOpen());
    }

    static void savePacket(WorkbenchPage &page, const QString &name)
    {
        page.m_packetNameEdit->setText(name);
        page.m_packetPayloadEdit->setPlainText(name + QStringLiteral(" payload"));
        page.saveCurrentPacket();
    }

    static void saveMacroStep(WorkbenchPage &page, const QString &name)
    {
        page.m_macroList->setCurrentRow(-1);
        page.m_macroNameEdit->setText(name);
        page.m_macroPayloadEdit->setPlainText(name + QStringLiteral(" payload"));
        page.saveCurrentMacroStep();
    }

    static void saveAutoReplyRule(WorkbenchPage &page, const QString &name)
    {
        page.startNewAutoReplyRule();
        page.m_autoReplyNameEdit->setText(name);
        page.m_autoReplyPatternEdit->setText(name + QStringLiteral(" request"));
        page.m_autoReplyPayloadEdit->setPlainText(name + QStringLiteral(" response"));
        page.saveCurrentAutoReplyRule();
    }

    static QStringList packetNames(const WorkbenchPage &page)
    {
        QStringList names;
        for (const WorkbenchPage::SendPacket &packet : page.m_sendPackets) {
            names.append(packet.name);
        }
        return names;
    }

    static QStringList macroNames(const WorkbenchPage &page)
    {
        QStringList names;
        for (const WorkbenchPage::MacroStep &step : page.m_macroSteps) {
            names.append(step.name);
        }
        return names;
    }

    static QStringList autoReplyNames(const WorkbenchPage &page)
    {
        QStringList names;
        for (const WorkbenchPage::AutoReplyRule &rule : page.m_autoReplyRules) {
            names.append(rule.name);
        }
        return names;
    }

    static QString receivedText(const WorkbenchPage &page)
    {
        QString text;
        for (const WorkbenchPage::SessionRecord &record : page.m_records) {
            if (record.direction == WorkbenchPage::RecordDirection::Rx) {
                text += record.terminalText;
            }
        }
        return text;
    }

  private slots:
    void initTestCase()
    {
        Q_INIT_RESOURCE(app);
        Q_INIT_RESOURCE(fluentqtwidgets);
        FluentConfig::instance()->setFileName(
            QDir(AppSettings::directoryPath()).filePath(QStringLiteral("fluent.json")));
    }

    void init()
    {
        VirtualSerialPair::instance()->setEnabled(false);
        AppSettings settings;
        settings.clear();
        settings.sync();
    }

    void cleanup()
    {
        VirtualSerialPair::instance()->setEnabled(false);
        QCoreApplication::processEvents();
    }

    void longMacroSequenceRunsStepsFromTheEventLoop()
    {
        WorkbenchPage page(nullptr, false, false);
        connectVirtualPort(page);
        WorkbenchPage::MacroStep step;
        step.name = QStringLiteral("ping");
        step.mode = QStringLiteral("text");
        step.payload = QStringLiteral("ping");
        step.lineEnding = QStringLiteral("none");
        step.responseMode = QStringLiteral("text");
        page.m_macroSteps = {step, step};
        constexpr int Loops = 1000;
        setNumberEditValue(page.m_macroLoopCountEdit, Loops, 1, 100000);

        page.startMacroSequence();
        // Steps without a response or delay used to run recursively inside this
        // call, one stack frame chain per step.
        QVERIFY(page.m_macroRunning);
        QVERIFY(page.m_macroResults.size() < 2 * Loops);
        QTRY_VERIFY_WITH_TIMEOUT(!page.m_macroRunning, 60000);
        QCOMPARE(page.m_macroResults.size(), 2 * Loops);
        for (const WorkbenchPage::MacroRunResult &result : page.m_macroResults) {
            QCOMPARE(result.status, AppI18n::text("通过"));
        }
    }

    void closingSessionDoesNotWaitForBlockedScriptRequest()
    {
        auto *page = new WorkbenchPage(nullptr, false, false);
        page->m_scriptEdit->setPlainText(QStringLiteral("while (true) { serial.lastRxText(); serial.records(); }"));
        page->startScript();
        QVERIFY(page->m_scriptRunning);
        // Let the script make requests that wait for the page's thread.
        QTest::qWait(200);
        QVERIFY(page->m_scriptRunner);

        QElapsedTimer timer;
        timer.start();
        delete page;
        QVERIFY2(timer.elapsed() < 1500, qPrintable(QStringLiteral("Closing took %1 ms").arg(timer.elapsed())));
    }

    void stoppedScriptDoesNotSendQueuedRequest()
    {
        WorkbenchPage page(nullptr, false, false);
        connectVirtualPort(page);
        page.m_scriptEdit->setPlainText(QStringLiteral("while (true) { serial.sendText('x'); }"));
        page.startScript();
        QTRY_VERIFY(page.controlStatus().transmittedBytes > 0);
        // Without processing events, let the script queue its next send request.
        QThread::msleep(50);
        const qint64 sent = page.controlStatus().transmittedBytes;
        page.stopScript();
        QTRY_VERIFY(!page.m_scriptRunning && page.m_scriptRunner == nullptr);
        QCOMPARE(page.controlStatus().transmittedBytes, sent);
    }

    void sessionsKeepEachOthersSavedLists()
    {
        WorkbenchPage first(nullptr, true, false);
        WorkbenchPage second(nullptr, false, false);

        savePacket(first, QStringLiteral("first packet"));
        savePacket(second, QStringLiteral("second packet"));
        saveMacroStep(first, QStringLiteral("first step"));
        saveMacroStep(second, QStringLiteral("second step"));
        saveAutoReplyRule(first, QStringLiteral("first rule"));
        saveAutoReplyRule(second, QStringLiteral("second rule"));

        const QStringList packets = {QStringLiteral("first packet"), QStringLiteral("second packet")};
        const QStringList steps = {QStringLiteral("first step"), QStringLiteral("second step")};
        const QStringList rules = {QStringLiteral("first rule"), QStringLiteral("second rule")};
        QCOMPARE(packetNames(first), packets);
        QCOMPARE(macroNames(first), steps);
        QCOMPARE(autoReplyNames(first), rules);

        // Closing a session saves its settings. Neither session may drop the
        // other's items, and a blank session starts from the saved lists.
        first.saveSettings();
        second.saveSettings();
        WorkbenchPage blank(nullptr, false, false);
        blank.saveSettings();
        WorkbenchPage restored(nullptr, true, false);
        QCOMPARE(packetNames(restored), packets);
        QCOMPARE(macroNames(restored), steps);
        QCOMPARE(autoReplyNames(restored), rules);
    }

    void reloadedSelectionFollowsTheSameItem()
    {
        WorkbenchPage first(nullptr, false, false);
        WorkbenchPage second(nullptr, false, false);
        saveMacroStep(first, QStringLiteral("removed"));
        saveMacroStep(first, QStringLiteral("kept"));
        second.m_macroList->setCurrentRow(1);
        QCOMPARE(second.m_macroNameEdit->text(), QStringLiteral("kept"));

        first.m_macroList->setCurrentRow(0);
        first.removeSelectedMacroStep();
        QCOMPARE(macroNames(second), QStringList{QStringLiteral("kept")});
        QCOMPARE(second.m_macroList->currentRow(), 0);

        // Saving the selected step updates it instead of overwriting another one.
        second.m_macroPayloadEdit->setPlainText(QStringLiteral("edited"));
        second.saveCurrentMacroStep();
        QCOMPARE(first.m_macroSteps.size(), 1);
        QCOMPARE(first.m_macroSteps.first().payload, QStringLiteral("edited"));

        // A selection whose item another session replaced is cleared, so the
        // next save adds a step.
        first.m_macroList->setCurrentRow(0);
        first.m_macroPayloadEdit->setPlainText(QStringLiteral("changed elsewhere"));
        first.saveCurrentMacroStep();
        QCOMPARE(second.m_macroList->currentRow(), -1);
    }

    void protocolTemplatesSyncWithoutChangingSelection()
    {
        WorkbenchPage first(nullptr, false, false);
        WorkbenchPage second(nullptr, false, false);
        const QString selected = second.m_protocolNameEdit->text();

        first.m_protocolNameEdit->setText(QStringLiteral("Shared protocol"));
        first.m_protocolHeaderEdit->setText(QStringLiteral("AA BB"));
        first.saveCurrentProtocolTemplate();

        QCOMPARE(second.m_protocolTemplates.size(), first.m_protocolTemplates.size());
        QCOMPARE(second.m_protocolTemplates.last().name, QStringLiteral("Shared protocol"));
        QCOMPARE(second.m_protocolNameEdit->text(), selected);
        QCOMPARE(second.m_protocolTemplateCombo->currentIndex(), 0);

        second.saveSettings();
        WorkbenchPage restored(nullptr, true, false);
        QCOMPARE(restored.m_protocolTemplates.last().name, QStringLiteral("Shared protocol"));
    }

    void multibyteCharactersSplitAcrossReadsDecode_data()
    {
        QTest::addColumn<QString>("encoding");
        QTest::newRow("utf-8") << QStringLiteral("utf-8");
        QTest::newRow("gbk") << QStringLiteral("gbk");
    }

    void multibyteCharactersSplitAcrossReadsDecode()
    {
        QFETCH(QString, encoding);
        WorkbenchPage page(nullptr, false, false);
        selectEncodingOption(page.m_receiveEncodingCombo, encoding);
        const QString text = QStringLiteral("温度=25 湿度=60");
        const QByteArray bytes = AppTextEncoding::encode(text, encoding).bytes;
        for (int offset = 0; offset < bytes.size(); offset += 3) {
            // Odd-sized reads split two- and three-byte characters.
            page.handleReceivedData(bytes.mid(offset, 3));
        }
        QCOMPARE(receivedText(page), text);
    }

    void headerFramesEndWhenTheLineGoesQuiet()
    {
        WorkbenchPage page(nullptr, false, false);
        page.m_autoFrameBreakCheck->setChecked(true);
        page.m_frameModeCombo->setCurrentIndex(page.m_frameModeCombo->findData(QStringLiteral("header")));
        page.m_framePatternEdit->setText(QStringLiteral("AA 55"));
        setNumberEditValue(page.m_frameBreakIntervalEdit, 30, 1, 60000);
        QVERIFY(page.m_frameBreakIntervalEdit->isEnabled());

        // The next header ends the first frame; only a quiet line ends the last one.
        page.handleReceivedData(QByteArray::fromHex("AA5501AA5502"));
        QCOMPARE(page.m_records.size(), 1);
        QCOMPARE(page.m_records.first().bytes, QByteArray::fromHex("AA5501"));
        QTRY_COMPARE(page.m_records.size(), 2);
        QCOMPARE(page.m_records.last().bytes, QByteArray::fromHex("AA5502"));

        // A frame continued within the timeout stays whole.
        page.handleReceivedData(QByteArray::fromHex("AA55"));
        page.handleReceivedData(QByteArray::fromHex("0304"));
        QTRY_COMPARE(page.m_records.size(), 3);
        QCOMPARE(page.m_records.last().bytes, QByteArray::fromHex("AA550304"));
    }

    void autoReplyIsRecordedAfterItsRequest()
    {
        WorkbenchPage device(nullptr, false, false);
        WorkbenchPage responder(nullptr, false, false);
        connectVirtualPort(device);
        connectVirtualPort(responder, VirtualSerialPair::portBName());
        WorkbenchPage::AutoReplyRule rule;
        rule.matchMode = QStringLiteral("text");
        rule.pattern = QStringLiteral("PING");
        rule.responseMode = QStringLiteral("text");
        rule.responsePayload = QStringLiteral("PONG");
        rule.lineEnding = QStringLiteral("none");
        responder.m_autoReplyRules = {rule};

        QString error;
        QVERIFY2(device.controlSendBytes(QByteArrayLiteral("PING"), QString(), &error), qPrintable(error));
        QTRY_COMPARE(device.controlStatus().receivedBytes, 4);
        const auto records = responder.controlRecords(10, QStringLiteral("all"));
        QCOMPARE(records.size(), 2);
        QCOMPARE(records.at(0).direction, QStringLiteral("rx"));
        QCOMPARE(records.at(0).bytes, QByteArrayLiteral("PING"));
        QCOMPARE(records.at(1).direction, QStringLiteral("tx"));
        QCOMPARE(records.at(1).bytes, QByteArrayLiteral("PONG"));
        QVERIFY(records.at(0).timestamp <= records.at(1).timestamp);
    }

    void sendChecksumSkipsBytesAndPrecedesTheLineEnding()
    {
        WorkbenchPage device(nullptr, false, false);
        WorkbenchPage peer(nullptr, false, false);
        connectVirtualPort(device);
        connectVirtualPort(peer, VirtualSerialPair::portBName());
        device.m_checksumAlgorithmCombo->setCurrentIndex(
            device.m_checksumAlgorithmCombo->findData(QStringLiteral("sum8")));
        device.m_checksumAppendCheck->setChecked(true);
        device.m_hexSendCheck->setChecked(true);
        device.m_lineEndingCombo->setCurrentIndex(device.m_lineEndingCombo->findData(QStringLiteral("crlf")));
        const auto lastReceived = [&peer]() { return peer.controlRecords(1, QStringLiteral("rx")).first().bytes; };

        device.m_sendEdit->setPlainText(QStringLiteral("01 02 03"));
        QVERIFY(device.sendCurrentPayload());
        QTRY_COMPARE(peer.controlStatus().receivedBytes, 6);
        QCOMPARE(lastReceived(), QByteArray::fromHex("010203060D0A"));

        device.m_checksumSkipLeadingEdit->setText(QStringLiteral("2"));
        device.m_checksumSkipTrailingEdit->setText(QStringLiteral("1"));
        device.m_sendEdit->setPlainText(QStringLiteral("AA 55 01 02 03 7E"));
        QVERIFY(device.sendCurrentPayload());
        QTRY_COMPARE(peer.controlStatus().receivedBytes, 15);
        QCOMPARE(lastReceived(), QByteArray::fromHex("AA55010203067E0D0A"));
        QVERIFY(device.m_checksumResultLabel->text().contains(AppI18n::text("覆盖字节 %1–%2").arg(2).arg(4)));

        // Packets, macros and replies carry their own line ending.
        bool ok = false;
        QCOMPARE(device.payloadWithOptionalChecksum(QByteArray::fromHex("AA5504057E0A"), QStringLiteral("lf"), &ok),
                 QByteArray::fromHex("AA550405097E0A"));
        QVERIFY(ok);

        const int sentRecords = device.controlRecords(1000, QStringLiteral("tx")).size();
        device.m_sendEdit->setPlainText(QStringLiteral("AA 55 7E"));
        QVERIFY(!device.sendCurrentPayload());
        QCOMPARE(device.controlRecords(1000, QStringLiteral("tx")).size(), sentRecords);
        QString error;
        device.payloadWithOptionalChecksum(QByteArray::fromHex("AA557E"), QStringLiteral("none"), &ok, &error);
        QVERIFY(!ok);
        QCOMPARE(error, AppChecksum::calculate(QByteArray::fromHex("AA557E"), QStringLiteral("sum8"),
                                               AppChecksum::ByteOrder::LittleEndian, 2, 1)
                            .errorMessage);

        WorkbenchPage restored(nullptr, true, false);
        QCOMPARE(restored.m_checksumSkipLeadingEdit->text(), QStringLiteral("2"));
        QCOMPARE(restored.m_checksumSkipTrailingEdit->text(), QStringLiteral("1"));
    }

    void scriptLogKeepsTheLastSendError()
    {
        ScriptBridge bridge;
        bridge.setSendTextCallback([](const QString &, const QString &, QString *error) {
            *error = QStringLiteral("port closed");
            return false;
        });
        QVERIFY(!bridge.sendText(QStringLiteral("x")));
        bridge.log(QStringLiteral("send failed"));
        QCOMPARE(bridge.lastError(), QStringLiteral("port closed"));
    }

    void invalidLoopPayloadWarnsOnceAndStops()
    {
        WorkbenchPage page(nullptr, false, false);
        connectVirtualPort(page);
        page.m_hexSendCheck->setChecked(true);
        page.m_sendEdit->setPlainText(QStringLiteral("ZZ"));
        page.m_loopIntervalEdit->setText(QStringLiteral("10"));
        const int barsBefore = page.findChildren<InfoBar *>().size();
        page.m_loopCheck->setChecked(true);
        QTRY_VERIFY(!page.m_loopCheck->isChecked());
        QVERIFY(!page.m_loopTimer.isActive());
        QTest::qWait(100);
        QCOMPARE(page.findChildren<InfoBar *>().size(), barsBefore + 1);
    }

    void hardwareFlowControlLeavesRtsToTheDriver()
    {
        WorkbenchPage page(nullptr, false, false);
        page.m_flowControlCombo->setCurrentIndex(
            page.m_flowControlCombo->findData(static_cast<int>(QSerialPort::HardwareControl)));
        connectVirtualPort(page);
        QVERIFY(!page.m_rtsCheck->isEnabled());
        QVERIFY(page.m_dtrCheck->isEnabled());
        page.m_connectButton->click();
        page.m_flowControlCombo->setCurrentIndex(
            page.m_flowControlCombo->findData(static_cast<int>(QSerialPort::NoFlowControl)));
        connectVirtualPort(page);
        QVERIFY(page.m_rtsCheck->isEnabled());
    }

    void savedReceiveCaptureWaitsForAConnection()
    {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        // The destination comes from the file dialog and is remembered; the
        // capture must not create or truncate anything before the link opens.
        const QString chosen = QDir(folder.path()).filePath(QStringLiteral("chosen_capture.bin"));
        AppSettings settings;
        settings.setValue(QStringLiteral("receive/saveToFile"), true);
        settings.setValue(QStringLiteral("receive/saveFile"), chosen);
        settings.setValue(QStringLiteral("export/folder"), folder.path());
        settings.sync();
        const auto files = [&folder]() { return QDir(folder.path()).entryList(QDir::Files); };

        WorkbenchPage page(nullptr, true, false);
        QVERIFY(page.m_saveReceiveCheck->isChecked());
        QVERIFY(files().isEmpty());

        SerialController device;
        connectVirtualPort(page, VirtualSerialPair::portBName());
        SerialPortConfig config;
        config.portName = VirtualSerialPair::portAName();
        QVERIFY(device.openPort(config));
        QVERIFY(device.writeData(QByteArrayLiteral("captured")));
        QTRY_COMPARE(page.controlStatus().receivedBytes, 8);
        page.m_connectButton->click();
        QVERIFY(!page.m_link.isOpen());
        QCOMPARE(files().size(), 1);
        QCOMPARE(files().first(), QStringLiteral("chosen_capture.bin"));
        QFile capture(chosen);
        QVERIFY(capture.open(QIODevice::ReadOnly));
        QCOMPARE(capture.readAll(), QByteArrayLiteral("captured"));
    }

    // The save dialog also picks the type, so the stored format drives the file.
    void receiveCaptureHonoursTheChosenFormat()
    {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const QString chosen = QDir(folder.path()).filePath(QStringLiteral("chosen_capture.csv"));
        AppSettings settings;
        settings.setValue(QStringLiteral("receive/saveToFile"), true);
        settings.setValue(QStringLiteral("receive/saveFile"), chosen);
        settings.setValue(QStringLiteral("receive/saveFormat"), QStringLiteral("csv"));
        settings.sync();

        WorkbenchPage page(nullptr, true, false);
        QVERIFY(page.m_saveReceiveCheck->isChecked());
        QVERIFY(!QFileInfo::exists(chosen));

        SerialController device;
        connectVirtualPort(page, VirtualSerialPair::portBName());
        SerialPortConfig config;
        config.portName = VirtualSerialPair::portAName();
        QVERIFY(device.openPort(config));
        QVERIFY(device.writeData(QByteArrayLiteral("captured")));
        QTRY_COMPARE(page.controlStatus().receivedBytes, 8);
        page.m_connectButton->click();
        QVERIFY(!page.m_link.isOpen());

        QFile capture(chosen);
        QVERIFY(capture.open(QIODevice::ReadOnly));
        const QByteArray content = capture.readAll();
        QVERIFY(content.startsWith(QByteArray("\xEF\xBB\xBFtimestamp,direction,source,length,hex,text")));
        QVERIFY(content.contains(QByteArrayLiteral("63 61 70 74 75 72 65 64")));  // hex column, space separated
        QVERIFY(content.contains(QByteArrayLiteral("captured")));
    }

    // Opt-in soak test: push FSA_STRESS_FORMAT=bin|txt worth of FSA_STRESS_FRAMES
    // frames through the virtual serial pair with the receive capture on, and
    // check that every frame reaches the file while the app stays bounded.
    void millionFrameCaptureStress()
    {
        const int frames = qEnvironmentVariableIntValue("FSA_STRESS_FRAMES");
        if (frames <= 0) {
            QSKIP("opt-in: set FSA_STRESS_FRAMES (e.g. 1000000) to run");
        }
        const QString format = qEnvironmentVariable("FSA_STRESS_FORMAT", QStringLiteral("bin"));
        const bool liveDisplay = qEnvironmentVariableIsSet("FSA_STRESS_LIVE_DISPLAY");
        const int batchFrames = qMax(1, qEnvironmentVariableIntValue("FSA_STRESS_BATCH"));
        const int perBatch = batchFrames > 0 ? batchFrames : 500;

        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const QString target = QDir(folder.path()).filePath(QStringLiteral("stress.%1").arg(format));
        AppSettings settings;
        settings.setValue(QStringLiteral("receive/saveToFile"), true);
        settings.setValue(QStringLiteral("receive/saveFile"), target);
        settings.setValue(QStringLiteral("receive/saveFormat"), format);
        // One frame per record: CRLF terminated, split on the tail.
        settings.setValue(QStringLiteral("receive/autoFrameBreak"), true);
        settings.setValue(QStringLiteral("receive/frameMode"), QStringLiteral("tail"));
        settings.setValue(QStringLiteral("receive/framePattern"), QStringLiteral("0D0A"));
        settings.sync();

        WorkbenchPage page(nullptr, true, false);
        page.resize(1120, 840);
        page.show();
        QVERIFY(page.m_saveReceiveCheck->isChecked());
        QVERIFY(page.m_autoFrameBreakCheck->isChecked());
        if (!liveDisplay) {
            page.m_pauseCheck->setChecked(true);
        }
        const int recordCap = page.maxRecordCount();

        SerialController device;
        connectVirtualPort(page, VirtualSerialPair::portBName());
        SerialPortConfig config;
        config.portName = VirtualSerialPair::portAName();
        QVERIFY(device.openPort(config));

        const auto frame = [](int index) {
            return QStringLiteral("FRAME_%1\r\n").arg(index, 8, 10, QLatin1Char('0')).toUtf8();
        };
        const qsizetype frameBytes = frame(0).size();
        const qint64 rssBefore = processRssKb();

        // Crash-proof progress trace: a failing soak run still leaves a trail.
        const QString tracePath = qEnvironmentVariable("FSA_STRESS_TRACE");
        const auto trace = [tracePath](const QString &line) {
            if (tracePath.isEmpty()) {
                return;
            }
            QFile file(tracePath);
            if (file.open(QIODevice::Append | QIODevice::Text)) {
                QTextStream(&file) << line << '\n';
            }
        };
        trace(QStringLiteral("start frames=%1 format=%2 rss=%3").arg(frames).arg(format).arg(rssBefore));

        qint64 pushed = 0;
        QElapsedTimer timer;
        timer.start();
        while (pushed < frames) {
            const int inBatch = static_cast<int>(qMin<qint64>(perBatch, frames - pushed));
            QByteArray batch;
            batch.reserve(inBatch * frameBytes);
            for (int i = 0; i < inBatch; ++i) {
                batch += frame(static_cast<int>(pushed + i));
            }
            while (!device.writeData(batch)) {
                // The virtual pair's buffer is full: let the app drain it.
                QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            }
            pushed += inBatch;
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            if (pushed % (perBatch * 20) == 0) {
                trace(QStringLiteral("pushed=%1 rx=%2 rss=%3 elapsed=%4")
                          .arg(pushed)
                          .arg(page.m_rxCount)
                          .arg(processRssKb())
                          .arg(timer.elapsed()));
            }
        }
        trace(QStringLiteral("pushed-all=%1 rx=%2 rss=%3 elapsed=%4")
                  .arg(pushed)
                  .arg(page.m_rxCount)
                  .arg(processRssKb())
                  .arg(timer.elapsed()));

        const qint64 expectedBytes = pushed * frameBytes;
        trace(QStringLiteral("wait-rx rx=%1 expected=%2").arg(page.m_rxCount).arg(expectedBytes));
        QTRY_VERIFY_WITH_TIMEOUT(page.m_rxCount >= expectedBytes, 900000);
        trace(QStringLiteral("rx-ok rx=%1 elapsed=%2").arg(page.m_rxCount).arg(timer.elapsed()));
        // Every received byte has been written by now; stopping the capture
        // flushes and closes the file so the check does not race the buffers.
        trace(QStringLiteral("stop-capture file=%1 rss=%2").arg(QFileInfo(target).size()).arg(processRssKb()));
        page.m_saveReceiveCheck->setChecked(false);
        QCoreApplication::processEvents();
        QElapsedTimer drain;
        drain.start();
        while (drain.elapsed() < 500) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        trace(QStringLiteral("file-ok file=%1 elapsed=%2").arg(QFileInfo(target).size()).arg(timer.elapsed()));
        trace(QStringLiteral("drained rx=%1 file=%2 rss=%3 elapsed=%4")
                  .arg(page.m_rxCount)
                  .arg(QFileInfo(target).size())
                  .arg(processRssKb())
                  .arg(timer.elapsed()));
        const qint64 elapsedMs = timer.elapsed();
        const qint64 rssAfter = processRssKb();
        const qint64 fileSize = QFileInfo(target).size();

        qint64 lines = 0;
        if (format != QStringLiteral("bin")) {
            QFile capture(target);
            QVERIFY(capture.open(QIODevice::ReadOnly));
            while (!capture.atEnd()) {
                lines += capture.read(1 << 20).count('\n');
            }
        }

        qInfo().noquote() << QStringLiteral("STRESS frames=%1 format=%2 display=%3 elapsed=%4 ms rate=%5 frames/s "
                                           "bytes=%6 file=%7 lines=%8 rss=%9->%10 KiB records=%11 cap=%12")
                                 .arg(frames)
                                 .arg(format)
                                 .arg(liveDisplay ? QStringLiteral("live") : QStringLiteral("paused"))
                                 .arg(elapsedMs)
                                 .arg(frames * 1000.0 / qMax<qint64>(1, elapsedMs), 0, 'f', 0)
                                 .arg(expectedBytes)
                                 .arg(fileSize)
                                 .arg(lines)
                                 .arg(rssBefore)
                                 .arg(rssAfter)
                                 .arg(page.m_records.size())
                                 .arg(recordCap);

        QVERIFY(page.m_link.isOpen());
        trace(QStringLiteral("asserting file=%1 rx=%2 rss=%3").arg(fileSize).arg(page.m_rxCount).arg(rssAfter));
        QCOMPARE(page.m_rxCount, expectedBytes);
        if (format == QStringLiteral("bin")) {
            QCOMPARE(fileSize, expectedBytes);
        } else {
            QCOMPARE(lines, static_cast<qint64>(frames));
        }
        QVERIFY(page.m_records.size() <= recordCap);
        if (rssBefore > 0 && rssAfter > 0) {
            QVERIFY2(rssAfter - rssBefore < 512 * 1024, "memory grew unbounded during the soak");
        }
        qInfo().noquote() << QStringLiteral("STRESS file: %1").arg(target);
        // keep the artifact for inspection (one per shape so runs do not clash)
        const QString keep = QDir(QDir::tempPath())
                                 .filePath(QStringLiteral("fsa_stress_%1_%2.%3").arg(frames).arg(format, format));
        QFile::remove(keep);
        QFile::copy(target, keep);
        trace(QStringLiteral("end"));
    }

    void fileSendWaitsForThePortToTakeEachChunk()
    {
#ifdef Q_OS_UNIX
        const int terminal = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
        QVERIFY(terminal >= 0);
        const auto closeTerminal = qScopeGuard([terminal]() { ::close(terminal); });
        QCOMPARE(grantpt(terminal), 0);
        QCOMPARE(unlockpt(terminal), 0);
        const char *portName = ptsname(terminal);
        QVERIFY(portName);

        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        QByteArray contents(256 * 1024, Qt::Uninitialized);
        for (int index = 0; index < contents.size(); ++index) {
            contents[index] = static_cast<char>(index % 251);
        }
        QFile file(folder.filePath(QStringLiteral("payload.bin")));
        QVERIFY(file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size());
        file.close();

        WorkbenchPage page(nullptr, false, false);
        SerialPortConfig config;
        config.portName = QString::fromLocal8Bit(portName);
        QVERIFY2(page.m_link.openLink(config), qPrintable(page.m_link.errorString()));
        page.m_filePathEdit->setText(file.fileName());
        constexpr int ChunkSize = 4096;
        setNumberEditValue(page.m_fileChunkSizeEdit, ChunkSize, 1, 65536);
        setNumberEditValue(page.m_fileIntervalEdit, 0, 0, 60000);
        page.startFileSend();

        // Nobody reads the other end yet, so the pseudo-terminal fills up. Sending
        // must pause with at most one chunk waiting instead of queueing the file.
        QTest::qWait(300);
        QVERIFY(page.m_fileSendFile.isOpen());
        QVERIFY(page.m_fileSendSent < page.m_fileSendTotal);
        QVERIFY(page.m_link.bytesToWrite() <= ChunkSize);

        // Drain the other end promptly; a macOS pseudo-terminal buffers only 1 KiB.
        QByteArray received;
        QElapsedTimer timer;
        timer.start();
        while ((received.size() < contents.size() || page.m_fileSendFile.isOpen()) && timer.elapsed() < 20000) {
            char buffer[16384];
            qint64 count = 0;
            while ((count = ::read(terminal, buffer, sizeof(buffer))) > 0) {
                received.append(buffer, int(count));
            }
            QTest::qWait(1);
        }
        QVERIFY(!page.m_fileSendFile.isOpen());
        QCOMPARE(received, contents);
        QCOMPARE(page.m_fileSendSent, page.m_fileSendTotal);
        page.m_link.closeLink();
#else
        QSKIP("Pseudo-terminal serial integration is available on Unix platforms");
#endif
    }

    void recordLimitChangesReachOpenSessions()
    {
        AppSettings settings;
        settings.setValue(QStringLiteral("terminal/maxRecords"), 1000);
        settings.sync();
        WorkbenchSessionsPage sessions;
        auto *tabs = sessions.findChild<FluentQt::TabWidget *>();
        QVERIFY(tabs);
        tabs->tabAddRequested();
        const auto pages = sessions.findChildren<WorkbenchPage *>();
        QCOMPARE(pages.size(), 2);
        for (auto *page : pages) {
            page->m_pauseCheck->setChecked(true);
            for (int index = 0; index < 1500; ++index) {
                page->appendRecord(WorkbenchPage::RecordDirection::Rx, QByteArray::number(index));
            }
            QCOMPARE(page->m_records.size(), 1000);
        }

        // Sessions read the limit once; the settings page pushes later changes.
        SettingsPage settingsPage;
        connect(&settingsPage, &SettingsPage::terminalRecordLimitChanged, &sessions,
                &WorkbenchSessionsPage::setTerminalRecordLimit);
        auto *card = settingsPage.findChild<FluentQt::RangeSettingCard *>(QStringLiteral("terminalMaxRecordsCard"));
        QVERIFY(card);
        card->setValue(1200);
        QCOMPARE(settings.value(QStringLiteral("terminal/maxRecords")).toInt(), 1200);
        for (auto *page : pages) {
            QCOMPARE(page->maxRecordCount(), 1200);
            for (int index = 0; index < 500; ++index) {
                page->appendRecord(WorkbenchPage::RecordDirection::Rx, QByteArray::number(index));
            }
            QCOMPARE(page->m_records.size(), 1200);
        }
        card->setValue(1000);
        for (auto *page : pages) {
            QCOMPARE(page->m_records.size(), 1000);
        }
    }
    void millionFrameReceiveStressLosesNothing()
    {
        WorkbenchPage page(nullptr, false, false);
        page.m_flushTimer.stop();
        page.m_statsTimer.stop();
        page.m_timestampCheck->setChecked(false);
        page.m_autoFrameBreakCheck->setChecked(true);
        page.m_frameModeCombo->setCurrentIndex(page.m_frameModeCombo->findData(QStringLiteral("tail")));
        page.m_framePatternEdit->setText(QStringLiteral("0A"));

        constexpr int totalFrames = 1000000;
        QElapsedTimer timer;
        timer.start();
        QByteArray chunk;
        qint64 totalBytes = 0;
        for (int frame = 1; frame <= totalFrames; ++frame) {
            chunk.append(QString::fromLatin1("%1\n").arg(frame, 7, 10, QLatin1Char('0')).toUtf8());
            if (chunk.size() >= 65536) {
                totalBytes += chunk.size();
                page.handleReceivedData(chunk);
                chunk.clear();
            }
        }
        if (!chunk.isEmpty()) {
            totalBytes += chunk.size();
            page.handleReceivedData(chunk);
        }
        const qint64 ingestMs = timer.elapsed();

        // One render of the retained tail proves the display pipeline survives
        // the flood with the document cap enforced.
        page.flushPendingLines();
        QCoreApplication::processEvents();
        const qint64 totalMs = timer.elapsed();

        // Byte counters must account for every byte fed to the port.
        QCOMPARE(page.m_rxCount, totalBytes);
        // Per-frame integrity: every appended record is either still retained
        // or was trimmed by the bounded history, never dropped silently.
        QCOMPARE(page.m_firstRecordIndex + page.m_records.size(), qint64(totalFrames));
        QVERIFY(page.m_records.size() <= page.maxRecordCount());
        // The newest frame survives trimming with its full payload.
        const QString lastText = page.m_records.last().terminalText;
        QVERIFY(lastText.contains(QStringLiteral("1000000")));
        QVERIFY(page.m_terminalView->toPlainText().endsWith(QStringLiteral("1000000")));
        qInfo("million-frame stress: %lld frames / %lld bytes in %lld ms (ingest %lld ms)", qint64(totalFrames),
              totalBytes, totalMs, ingestMs);
        QVERIFY2(totalMs < 120000, qPrintable(QStringLiteral("stress run took %1 ms").arg(totalMs)));
    }
};

QTEST_MAIN(WorkbenchReliabilityTest)
#include "tst_workbench_reliability.moc"
