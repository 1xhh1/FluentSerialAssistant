#include "app/core/app_i18n.h"
#include "app/core/checksum_utils.h"
#include "app/core/hex_utils.h"
#include "app/core/modbus_utils.h"
#include "app/core/protocol_template.h"

#include <QtCore/QHash>
#include <QtCore/QTranslator>
#include <QtTest/QTest>

#include <functional>

class FrameCodecsTest : public QObject
{
    Q_OBJECT

  private slots:
    void hexPayloadsParse_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("spaced") << QStringLiteral("AA 55 10") << QByteArray::fromHex("AA5510");
        QTest::newRow("separators") << QStringLiteral("aa,55;10-ff\t01\n02") << QByteArray::fromHex("AA5510FF0102");
        QTest::newRow("prefixed") << QStringLiteral("0xAA 0X5 0x0a") << QByteArray::fromHex("AA050A");
        QTest::newRow("packed") << QStringLiteral("AA55FF") << QByteArray::fromHex("AA55FF");
        QTest::newRow("prefixed packed") << QStringLiteral("0xAA55") << QByteArray::fromHex("AA55");
        QTest::newRow("single digits") << QStringLiteral("A B") << QByteArray::fromHex("0A0B");
        QTest::newRow("empty") << QString() << QByteArray();
        QTest::newRow("blank") << QStringLiteral(" \n\t ") << QByteArray();
    }

    void hexPayloadsParse()
    {
        QFETCH(QString, input);
        QFETCH(QByteArray, bytes);
        const HexParseResult result = parseHexPayload(input);
        QVERIFY2(result.ok, qPrintable(result.errorMessage));
        QCOMPARE(result.bytes, bytes);
        QCOMPARE(result.errorOffset, qsizetype(-1));
    }

    void malformedHexReportsWhere_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<int>("offset");
        QTest::addColumn<QByteArray>("message");
        QTest::newRow("letter") << QStringLiteral("AA ZZ") << 3 << QByteArray("包含非 HEX 字符");
        QTest::newRow("full width") << QStringLiteral("ＡＡ") << 0 << QByteArray("包含非 HEX 字符");
        QTest::newRow("prefix inside") << QStringLiteral("AA0x55") << 3 << QByteArray("包含非 HEX 字符");
        QTest::newRow("bare prefix") << QStringLiteral("AA 0x") << 3 << QByteArray("0x 后缺少字节");
        QTest::newRow("odd packed") << QStringLiteral("ABC") << 0 << QByteArray("连续 HEX 字符数量必须为偶数");
        QTest::newRow("odd prefixed") << QStringLiteral("11 0xABC") << 5 << QByteArray("连续 HEX 字符数量必须为偶数");
        QTest::newRow("only separators") << QStringLiteral(", ;-") << 0 << QByteArray("没有解析到有效字节");
    }

    void malformedHexReportsWhere()
    {
        QFETCH(QString, input);
        QFETCH(int, offset);
        QFETCH(QByteArray, message);
        const HexParseResult result = parseHexPayload(input);
        QVERIFY(!result.ok);
        QVERIFY(result.bytes.isEmpty());
        QCOMPARE(result.errorOffset, qsizetype(offset));
        QCOMPARE(result.errorMessage, AppI18n::text(message.constData()));
    }

    void hexRoundTripsEveryByte()
    {
        QByteArray bytes;
        for (int value = 0; value < 256; ++value) {
            bytes.append(static_cast<char>(value));
        }
        const QString hex = bytesToHex(bytes);
        QVERIFY(hex.startsWith(QStringLiteral("00 01 02")));
        QVERIFY(hex.endsWith(QStringLiteral("FE FF")));
        QCOMPARE(parseHexPayload(hex).bytes, bytes);
        QCOMPARE(bytesToHex(QByteArray()), QString());
    }

    void escapedTextPayloadsDecode_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("literal") << QStringLiteral("hello") << QByteArray("hello");
        QTest::newRow("crlf") << QStringLiteral("A\r\nB") << QByteArray("A\r\nB");
        QTest::newRow("all short escapes") << QStringLiteral("\\a\\b\\f\\v\\t\\0") << QByteArray("\a\b\f\v\t\0", 6);
        QTest::newRow("hex mixed case") << QStringLiteral("\\x41\\x5a \\x1b[0m") << QByteArray("AZ \x1B[0m");
        QTest::newRow("single hex digit") << QStringLiteral("\\xA") << QByteArray("\x0A", 1);
        QTest::newRow("escaped backslash") << QStringLiteral("C:\\\\temp") << QByteArray("C:\\temp");
        QTest::newRow("literal text with hex bytes") << QStringLiteral("AT\\x0d") << QByteArray("AT\r");
        QTest::newRow("chinese gbk") << QStringLiteral("串口\\x41")
                                     << QByteArray("\xB4\xAE\xBF\xDA"
                                                   "A");
        QTest::newRow("empty") << QString() << QByteArray();
    }

    void escapedTextPayloadsDecode()
    {
        QFETCH(QString, input);
        QFETCH(QByteArray, bytes);
        const EscapeParseResult result = parseEscapedTextPayload(input, QStringLiteral("gbk"));
        QVERIFY2(result.ok, qPrintable(result.errorMessage));
        QCOMPARE(result.bytes, bytes);
        QCOMPARE(result.errorOffset, qsizetype(-1));
    }

    void malformedEscapesReportWhere_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<int>("offset");
        QTest::addColumn<QByteArray>("message");
        QTest::newRow("trailing backslash") << QStringLiteral("AB\\") << 2 << QByteArray("转义符后缺少字符");
        QTest::newRow("hex without digits")
            << QStringLiteral("\\xZZ") << 0 << QByteArray("\\x 后缺少 1-2 位十六进制数字");
        QTest::newRow("unknown escape") << QStringLiteral("A\\q") << 1 << QByteArray("不支持的转义序列");
    }

    void malformedEscapesReportWhere()
    {
        QFETCH(QString, input);
        QFETCH(int, offset);
        QFETCH(QByteArray, message);
        const EscapeParseResult result = parseEscapedTextPayload(input, QStringLiteral("gbk"));
        QVERIFY(!result.ok);
        QVERIFY(result.bytes.isEmpty());
        QCOMPARE(result.errorOffset, qsizetype(offset));
        QCOMPARE(result.errorMessage, AppI18n::text(message.constData()));
    }

    void escapesDecodeUtf8RunsWithCodec()
    {
        const EscapeParseResult result = parseEscapedTextPayload(QStringLiteral("温度\\x41"), QStringLiteral("utf-8"));
        QVERIFY2(result.ok, qPrintable(result.errorMessage));
        QCOMPARE(result.bytes, QString::fromUtf8("温度").toUtf8() + QByteArrayLiteral("A"));
    }

    void checksumsMatchStandardCheckValues_data()
    {
        QTest::addColumn<QString>("algorithm");
        QTest::addColumn<quint32>("value");
        // Check values of each algorithm for the ASCII input "123456789".
        QTest::newRow("crc16-modbus") << QStringLiteral("crc16-modbus") << quint32(0x4B37);
        QTest::newRow("crc16-ccitt-false") << QStringLiteral("crc16-ccitt") << quint32(0x29B1);
        QTest::newRow("crc32") << QStringLiteral("crc32") << quint32(0xCBF43926);
        QTest::newRow("modbus-alias") << QStringLiteral(" Modbus ") << quint32(0x4B37);
    }

    void checksumsMatchStandardCheckValues()
    {
        QFETCH(QString, algorithm);
        QFETCH(quint32, value);
        const auto result =
            AppChecksum::calculate(QByteArrayLiteral("123456789"), algorithm, AppChecksum::ByteOrder::BigEndian);
        QVERIFY(result.ok);
        QCOMPARE(result.value, value);
    }

    // Check values over "123456789" for every algorithm in options(). PEC and
    // ZMODEM carry the values the parameterizations actually produce (see the
    // notes in checksum_utils.cpp); all others match published check values.
    void everyAlgorithmMatchesDocumentedCheckValue()
    {
        struct Expected
        {
            const char *key;
            quint32 value;
        };
        static const QVector<Expected> expected = {
            {"crc16-modbus", 0x4B37},
            {"crc16-ccitt", 0x29B1},
            {"crc32", 0xCBF43926},
            {"lrc", 0x23},
            {"xor", 0x31},
            {"sum8", 0xDD},
            {"crc8", 0xF4},
            {"crc8-cdma2000", 0xDA},
            {"crc8-itu", 0xA1},
            {"crc8-maxim", 0xA1},
            {"crc8-rohc", 0xD0},
            {"crc8-wcdma", 0x25},
            {"crc16-ccitt-false", 0x29B1},
            {"crc16-xmodem", 0x31C3},
            {"crc16-x25", 0x906E},
            {"crc16-kermit", 0x2189},
            {"crc16-ibm", 0xBB3D},
            {"crc16-maxim", 0x44C2},
            {"crc16-usb", 0xB4C8},
            {"crc16-dnp", 0xEA82},
            {"crc16-pec", 0x31C3},
            {"crc32-bzip2", 0xFC891918},
            {"crc32-jamcrc", 0x340BC6D9},
            {"crc32-mpeg2", 0x0376E6E7},
            {"crc32-posix", 0x765E7680},
            {"crc32-zmodem", 0x89A1897F},
            {"checksum8", 0xDD},
            {"checksum8r", 0x22},
            {"checksum16", 0x01DD},
            {"checksum16-internet", 0xF62A},
            {"checksum16-mitsubishi", 0xFE23},
        };
        QHash<QString, quint32> values;
        for (const Expected &item : expected) {
            values.insert(QString::fromLatin1(item.key), item.value);
        }

        const QByteArray check = QByteArrayLiteral("123456789");
        const QList<AppChecksum::AlgorithmOption> algorithms = AppChecksum::options();
        QCOMPARE(algorithms.size(), expected.size());
        for (const AppChecksum::AlgorithmOption &option : algorithms) {
            QVERIFY2(values.contains(option.key),
                     qPrintable(QStringLiteral("missing check value for %1").arg(option.key)));
            const auto result = AppChecksum::calculate(check, option.key, AppChecksum::ByteOrder::BigEndian);
            QVERIFY2(result.ok, qPrintable(QStringLiteral("%1 failed to calculate").arg(option.key)));
            QCOMPARE(result.value, values.value(option.key));
            // The serialized bytes must carry the value in the algorithm's width.
            const QString hex = bytesToHex(result.bytes).remove(QLatin1Char(' '));
            QString wanted = QString::number(values.value(option.key), 16).toUpper();
            while (wanted.size() < option.widthBytes * 2) {
                wanted.prepend(QLatin1Char('0'));
            }
            QCOMPARE(hex, wanted);
        }

        // Normalized aliases keep their existing targets and gain the new ones.
        QCOMPARE(AppChecksum::normalizedAlgorithmKey(QStringLiteral("crc16")), QStringLiteral("crc16-ccitt"));
        QCOMPARE(AppChecksum::normalizedAlgorithmKey(QStringLiteral("crc")), QStringLiteral("crc32"));
        QCOMPARE(AppChecksum::normalizedAlgorithmKey(QStringLiteral("xmodem")), QStringLiteral("crc16-xmodem"));
        QCOMPARE(AppChecksum::normalizedAlgorithmKey(QStringLiteral("kermit")), QStringLiteral("crc16-kermit"));
    }

    void unsupportedChecksumAlgorithmIsReportedInsteadOfReplaced()
    {
        QVERIFY(AppChecksum::isSupportedAlgorithm(QStringLiteral("CRC32")));
        QVERIFY(AppChecksum::isSupportedAlgorithm(QStringLiteral("crc8")));
        QVERIFY(!AppChecksum::isSupportedAlgorithm(QStringLiteral("no-such-algorithm")));
        const auto unknown = AppChecksum::calculate(QByteArrayLiteral("123456789"), QStringLiteral("no-such-algorithm"),
                                                    AppChecksum::ByteOrder::LittleEndian);
        QVERIFY(!unknown.ok);
        QVERIFY(!unknown.errorMessage.isEmpty());

        AppProtocol::ProtocolTemplate protocol = AppProtocol::defaultTemplate();
        QByteArray checked = QByteArray::fromHex("AA550310010203");
        checked += AppChecksum::calculate(checked, protocol.checksumAlgorithm, protocol.checksumByteOrder).bytes;
        QVERIFY(AppProtocol::parseFrame(checked, protocol).checksumValid);
        // A template naming an unsupported algorithm must not be checked as CRC16-Modbus.
        protocol.checksumAlgorithm = QStringLiteral("no-such-algorithm");
        const auto parsed = AppProtocol::parseFrame(checked, protocol);
        QVERIFY(!parsed.ok);
        QVERIFY(!parsed.checksumChecked);
        QCOMPARE(parsed.errorMessage, unknown.errorMessage);
    }

    void checksumRangeSkipsLeadingAndTrailingBytes()
    {
        const auto order = AppChecksum::ByteOrder::LittleEndian;
        const QString crc = QStringLiteral("crc16-modbus");
        const QByteArray data = QByteArray::fromHex("AA55010203040D0A");
        const auto ranged = AppChecksum::calculate(data, crc, order, 2, 2);
        QVERIFY(ranged.ok);
        QCOMPARE(ranged.bytes, AppChecksum::calculate(QByteArray::fromHex("01020304"), crc, order).bytes);
        QCOMPARE(AppChecksum::calculate(data, crc, order, 0, 0).bytes, AppChecksum::calculate(data, crc, order).bytes);
        // Skipping every byte, or a negative count, leaves nothing to check.
        for (const auto &skip :
             {qMakePair(8, 0), qMakePair(0, 8), qMakePair(4, 4), qMakePair(6, 3), qMakePair(-1, 0)}) {
            const auto empty = AppChecksum::calculate(data, crc, order, skip.first, skip.second);
            QVERIFY(!empty.ok);
            QVERIFY(!empty.errorMessage.isEmpty());
        }
    }

    void checksumStartAndTrailerShapeTheFrame()
    {
        AppProtocol::ProtocolTemplate protocol = AppProtocol::defaultTemplate();
        protocol.checksumStart = 2;
        protocol.trailer = QByteArray::fromHex("0D0A");
        // AA 55 | LEN 03 | CMD 10 | 01 02 03 | CRC over LEN..payload | 0D 0A
        const QByteArray body = QByteArray::fromHex("AA550310010203");
        const QByteArray crc =
            AppChecksum::calculate(body.mid(2), protocol.checksumAlgorithm, protocol.checksumByteOrder).bytes;
        const QByteArray frame = body + crc + protocol.trailer;
        const auto parsed = AppProtocol::parseFrame(frame, protocol);
        QVERIFY2(parsed.ok && parsed.checksumValid, qPrintable(parsed.errorMessage));
        QCOMPARE(parsed.frameLength, int(frame.size()));
        QCOMPARE(parsed.payload, QByteArray::fromHex("010203"));
        QCOMPARE(parsed.checksum, crc);
        const auto layout = AppProtocol::frameLayout(frame, protocol);
        QCOMPARE(layout.checksumOffset, qint64(7));
        QCOMPARE(layout.trailerOffset, qint64(9));
        QCOMPARE(layout.frameLength, qint64(11));

        AppProtocol::ProtocolTemplate wholeFrame = protocol;
        wholeFrame.checksumStart = 0;
        QVERIFY(!AppProtocol::parseFrame(frame, wholeFrame).checksumValid);

        QByteArray wrongTrailer = frame;
        wrongTrailer[wrongTrailer.size() - 1] = '\x0B';
        const auto mismatch = AppProtocol::parseFrame(wrongTrailer, protocol);
        QVERIFY(!mismatch.ok);
        QCOMPARE(mismatch.errorMessage, AppI18n::text("帧尾不匹配"));

        AppProtocol::ProtocolTemplate frameLength = protocol;
        frameLength.lengthMode = AppProtocol::LengthMode::FrameLength;
        const QByteArray example = AppProtocol::exampleFrame(frameLength);
        QVERIFY(example.endsWith(protocol.trailer));
        QCOMPARE(int(static_cast<unsigned char>(example.at(2))), int(example.size()));
        const auto parsedExample = AppProtocol::parseFrame(example, frameLength);
        QVERIFY2(parsedExample.ok && parsedExample.checksumValid, qPrintable(parsedExample.errorMessage));

        AppProtocol::ProtocolTemplate pastChecksum = protocol;
        pastChecksum.checksumStart = 7;
        const auto late = AppProtocol::parseFrame(frame, pastChecksum);
        QVERIFY(!late.ok);
        QCOMPARE(late.errorMessage, AppI18n::text("校验起始偏移应小于校验位置 %1").arg(7));
        // The example frame has a longer payload, so it needs a later start to fail.
        QVERIFY(!AppProtocol::exampleFrame(pastChecksum).isEmpty());
        pastChecksum.checksumStart = 100;
        QVERIFY(AppProtocol::exampleFrame(pastChecksum).isEmpty());

        QVERIFY(AppProtocol::fromJson(AppProtocol::toJson(protocol)) == protocol);
        // Templates saved before these fields existed check the whole frame and have no trailer.
        QJsonObject saved = AppProtocol::toJson(AppProtocol::defaultTemplate());
        saved.remove(QStringLiteral("checksumStart"));
        saved.remove(QStringLiteral("trailer"));
        QVERIFY(AppProtocol::fromJson(saved) == AppProtocol::defaultTemplate());
    }

    void exampleFramesParseWithTheirTemplate()
    {
        AppProtocol::ProtocolTemplate frameLength = AppProtocol::defaultTemplate();
        frameLength.lengthMode = AppProtocol::LengthMode::FrameLength;
        frameLength.lengthSize = 2;
        frameLength.lengthByteOrder = AppChecksum::ByteOrder::LittleEndian;
        frameLength.commandOffset = 4;
        frameLength.payloadOffset = 5;
        frameLength.checksumAlgorithm = QStringLiteral("crc32");
        for (const AppProtocol::ProtocolTemplate &protocol : {AppProtocol::defaultTemplate(), frameLength}) {
            const auto parsed = AppProtocol::parseFrame(AppProtocol::exampleFrame(protocol), protocol);
            QVERIFY2(parsed.ok && parsed.checksumValid, qPrintable(parsed.errorMessage));
        }
        // A header longer than the frame its fields describe cannot form a frame.
        AppProtocol::ProtocolTemplate impossible = AppProtocol::defaultTemplate();
        impossible.header = QByteArray(16, '\xAA');
        QVERIFY(AppProtocol::exampleFrame(impossible).isEmpty());
    }

    void frameLayoutMatchesTheParser()
    {
        AppProtocol::ProtocolTemplate frameLength = AppProtocol::defaultTemplate();
        frameLength.lengthMode = AppProtocol::LengthMode::FrameLength;
        AppProtocol::ProtocolTemplate fixedPayload = AppProtocol::defaultTemplate();
        fixedPayload.lengthSize = 0;
        fixedPayload.payloadLength = 2;
        AppProtocol::ProtocolTemplate noLength = AppProtocol::defaultTemplate();
        noLength.lengthSize = 0;
        AppProtocol::ProtocolTemplate trailer = AppProtocol::defaultTemplate();
        trailer.trailer = QByteArray::fromHex("0D0A");
        AppProtocol::ProtocolTemplate trailerNoLength = noLength;
        trailerNoLength.trailer = QByteArray::fromHex("03");
        for (const AppProtocol::ProtocolTemplate &protocol :
             {AppProtocol::defaultTemplate(), frameLength, fixedPayload, noLength, trailer, trailerNoLength}) {
            const QByteArray frame = AppProtocol::exampleFrame(protocol);
            const auto parsed = AppProtocol::parseFrame(frame, protocol);
            QVERIFY2(parsed.ok && parsed.checksumValid, qPrintable(parsed.errorMessage));
            const auto layout = AppProtocol::frameLayout(frame, protocol);
            QCOMPARE(layout.lengthValue, parsed.lengthValue);
            QCOMPARE(layout.frameLength, qint64(parsed.frameLength));
            QCOMPARE(layout.payloadLength, qint64(parsed.payload.size()));
            QCOMPARE(layout.trailerOffset, qint64(parsed.frameLength - protocol.trailer.size()));
            QCOMPARE(layout.checksumOffset, layout.trailerOffset - parsed.checksum.size());
        }
        // A length field past the end of the frame reads as 0.
        const auto truncated = AppProtocol::frameLayout(QByteArray::fromHex("AA55"), AppProtocol::defaultTemplate());
        QCOMPARE(truncated.lengthValue, 0u);
        QCOMPARE(truncated.frameLength, qint64(6));
    }

    void templatesCompareEveryField()
    {
        const AppProtocol::ProtocolTemplate base = AppProtocol::defaultTemplate();
        QVERIFY(base == AppProtocol::defaultTemplate());
        const QList<std::function<void(AppProtocol::ProtocolTemplate &)>> changes = {
            [](auto &item) { item.name = QStringLiteral("other"); },
            [](auto &item) { item.header = QByteArray::fromHex("BB66"); },
            [](auto &item) { item.lengthOffset = 1; },
            [](auto &item) { item.lengthSize = 2; },
            [](auto &item) { item.lengthMode = AppProtocol::LengthMode::FrameLength; },
            [](auto &item) { item.lengthByteOrder = AppChecksum::ByteOrder::LittleEndian; },
            [](auto &item) { item.commandOffset = 4; },
            [](auto &item) { item.commandSize = 2; },
            [](auto &item) { item.payloadOffset = 5; },
            [](auto &item) { item.payloadLength = 3; },
            [](auto &item) { item.checksumAlgorithm = QStringLiteral("crc32"); },
            [](auto &item) { item.checksumByteOrder = AppChecksum::ByteOrder::BigEndian; },
            [](auto &item) { item.checksumStart = 2; },
            [](auto &item) { item.trailer = QByteArray::fromHex("0D0A"); },
        };
        for (const auto &change : changes) {
            AppProtocol::ProtocolTemplate changed = base;
            change(changed);
            QVERIFY(!(changed == base));
        }
    }

    void englishModbusSummariesKeepTheirCase()
    {
        Q_INIT_RESOURCE(app);
        QTranslator translator;
        QVERIFY(translator.load(QStringLiteral(":/app/i18n/fluentserialassistant.en_US.qm")));
        QVERIFY(QCoreApplication::installTranslator(&translator));
        const auto withCrc = [](QByteArray frame) {
            return frame +
                   AppChecksum::calculate(frame, QStringLiteral("crc16-modbus"), AppChecksum::ByteOrder::LittleEndian)
                       .bytes;
        };
        const auto response = AppModbus::parseResponse(withCrc(QByteArray::fromHex("010F0000000A")));
        const auto exception = AppModbus::parseResponse(withCrc(QByteArray::fromHex("018F02")));
        QCoreApplication::removeTranslator(&translator);
        QVERIFY(response.recognized && exception.recognized);
        // Only the function code is upper case.
        QVERIFY2(response.summary.startsWith(QStringLiteral("Modbus response · Slave ID 1 · Function 0x0F")),
                 qPrintable(response.summary));
        QVERIFY2(exception.summary.startsWith(QStringLiteral("Modbus exception · Slave ID 1 · Function 0x8F")),
                 qPrintable(exception.summary));
    }

    void modbusAsciiRequestBuildsExactFrame()
    {
        AppModbus::RequestConfig config;
        config.slaveId = 1;
        config.functionKey = QStringLiteral("read-holding-registers");
        config.address = 0x0000;
        config.quantity = 2;
        config.mode = AppModbus::TransportMode::Ascii;
        const auto result = AppModbus::buildRequest(config);
        QVERIFY2(result.ok, qPrintable(result.errorMessage));
        // ':' + hex ASCII of the PDU (01 03 00 00 00 02) + LRC 0xFA + CRLF.
        QCOMPARE(result.frame, QByteArrayLiteral(":010300000002FA\r\n"));
    }

    void modbusAsciiResponseParse()
    {
        // Read holding registers response: slave 1, function 3, byte count 4, registers 10 and 20.
        // PDU sum is 0x26, so the LRC (two's complement) is 0xDA.
        const auto response = AppModbus::parseResponse(QByteArrayLiteral(":010304000A0014DA\r\n"));
        QVERIFY(response.recognized);
        QVERIFY(response.crcValid);
        QCOMPARE(response.mode, AppModbus::TransportMode::Ascii);
        QVERIFY2(response.summary.contains(QStringLiteral("10, 20")), qPrintable(response.summary));

        // Exception response: slave 1, function 0x83, code 0x02; LRC = 0x7A.
        const auto exception = AppModbus::parseResponse(QByteArrayLiteral(":0183027A\r\n"));
        QVERIFY(exception.recognized);
        QVERIFY(exception.crcValid);
        QCOMPARE(exception.mode, AppModbus::TransportMode::Ascii);
        QVERIFY2(exception.summary.contains(QStringLiteral("0x83")), qPrintable(exception.summary));

        // A wrong LRC still parses the frame but must be reported as invalid.
        const auto broken = AppModbus::parseResponse(QByteArrayLiteral(":010304000A0014FF\r\n"));
        QVERIFY(broken.recognized);
        QVERIFY(!broken.crcValid);
        QCOMPARE(broken.mode, AppModbus::TransportMode::Ascii);
    }

    void modbusRtuRegression()
    {
        AppModbus::RequestConfig config;
        config.slaveId = 1;
        config.functionKey = QStringLiteral("read-holding-registers");
        config.address = 0x0000;
        config.quantity = 2;
        const auto result = AppModbus::buildRequest(config);
        QVERIFY2(result.ok, qPrintable(result.errorMessage));
        QCOMPARE(result.frame.size(), 8);
        QVERIFY(result.frame.startsWith(QByteArray::fromHex("010300000002")));

        const auto parsed = AppModbus::parseResponse(result.frame);
        QVERIFY(parsed.recognized);
        QVERIFY(parsed.crcValid);
        QCOMPARE(parsed.mode, AppModbus::TransportMode::Rtu);
    }

    void modbusQuantitiesFitOneFrame_data()
    {
        QTest::addColumn<QString>("function");
        QTest::addColumn<int>("maximum");
        QTest::newRow("read-coils") << QStringLiteral("read-coils") << 2000;
        QTest::newRow("read-registers") << QStringLiteral("read-holding-registers") << 125;
        QTest::newRow("write-coils") << QStringLiteral("write-multiple-coils") << 1968;
        QTest::newRow("write-registers") << QStringLiteral("write-multiple-registers") << 123;
        QTest::newRow("write-single-register") << QStringLiteral("write-single-register") << 1;
    }

    void modbusQuantitiesFitOneFrame()
    {
        QFETCH(QString, function);
        QFETCH(int, maximum);
        const AppModbus::FunctionOption option = AppModbus::functionForKey(function);
        QCOMPARE(int(AppModbus::maximumQuantity(option)), maximum);
        const auto values = [&option](int count) {
            return QStringList(count, option.coil ? QStringLiteral("1") : QStringLiteral("7")).join(QLatin1Char(' '));
        };

        AppModbus::RequestConfig config;
        config.functionKey = function;
        config.quantity = static_cast<quint16>(maximum);
        config.valuesText = option.write ? values(maximum) : QString();
        const auto accepted = AppModbus::buildRequest(config);
        QVERIFY2(accepted.ok, qPrintable(accepted.errorMessage));
        // An RTU frame (address, PDU and CRC) holds at most 256 bytes.
        QVERIFY(accepted.frame.size() <= 256);

        if (option.write && !option.multiple) {
            return;
        }
        config.quantity = static_cast<quint16>(maximum + 1);
        config.valuesText = option.write ? values(maximum + 1) : QString();
        const auto rejected = AppModbus::buildRequest(config);
        QVERIFY(!rejected.ok);
        QVERIFY(rejected.errorMessage.contains(QString::number(maximum)));
    }
};

QTEST_GUILESS_MAIN(FrameCodecsTest)
#include "tst_frame_codecs.moc"
