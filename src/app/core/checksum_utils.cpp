#include "app/core/checksum_utils.h"
#include "app/core/app_i18n.h"

#include <QtCore/QtGlobal>

namespace {

enum class AlgorithmKind
{
    Crc,
    Sum,               // 8-bit sum of bytes
    SumInverted,       // bitwise NOT of the 8-bit sum
    Sum16,             // 16-bit sum of bytes
    InternetChecksum16,// RFC 1071 one's complement checksum
    Sum16Negated,      // two's complement of the 16-bit sum
    Xor,
    Lrc
};

struct CrcParams
{
    int width;
    quint32 poly;
    quint32 init;
    bool refin;
    bool refout;
    quint32 xorout;
};

struct AlgorithmSpec
{
    AppChecksum::AlgorithmOption option;
    AlgorithmKind kind;
    CrcParams crc;
};

constexpr CrcParams crcParams(int width, quint32 poly, quint32 init, bool refin, bool refout, quint32 xorout)
{
    return {width, poly, init, refin, refout, xorout};
}

const QList<AlgorithmSpec> &algorithmSpecs()
{
    static const QList<AlgorithmSpec> specs = {
        // The six original algorithms keep their keys, labels and semantics unchanged.
        {AppChecksum::AlgorithmOption{QStringLiteral("crc16-modbus"), QStringLiteral("CRC16-Modbus"), 2},
         AlgorithmKind::Crc, crcParams(16, 0x8005, 0xFFFF, true, true, 0x00000000)},
        {AppChecksum::AlgorithmOption{QStringLiteral("crc16-ccitt"), QStringLiteral("CRC16-CCITT"), 2},
         AlgorithmKind::Crc, crcParams(16, 0x1021, 0xFFFF, false, false, 0x00000000)},
        {AppChecksum::AlgorithmOption{QStringLiteral("crc32"), QStringLiteral("CRC32"), 4},
         AlgorithmKind::Crc, crcParams(32, 0x04C11DB7, 0xFFFFFFFF, true, true, 0xFFFFFFFF)},
        {AppChecksum::AlgorithmOption{QStringLiteral("lrc"), QStringLiteral("LRC"), 1}, AlgorithmKind::Lrc, {}},
        {AppChecksum::AlgorithmOption{QStringLiteral("xor"), QStringLiteral("XOR"), 1}, AlgorithmKind::Xor, {}},
        {AppChecksum::AlgorithmOption{QStringLiteral("sum8"), QStringLiteral("SUM8"), 1}, AlgorithmKind::Sum, {}},

        {AppChecksum::AlgorithmOption{QStringLiteral("crc8"), QStringLiteral("CRC-8"), 1},
         AlgorithmKind::Crc, crcParams(8, 0x07, 0x00, false, false, 0x00)},
        {AppChecksum::AlgorithmOption{QStringLiteral("crc8-cdma2000"), QStringLiteral("CRC-8/CDMA2000"), 1},
         AlgorithmKind::Crc, crcParams(8, 0x9B, 0xFF, false, false, 0x00)},
        {AppChecksum::AlgorithmOption{QStringLiteral("crc8-itu"), QStringLiteral("CRC-8/ITU"), 1},
         AlgorithmKind::Crc, crcParams(8, 0x07, 0x00, false, false, 0x55)},
        {AppChecksum::AlgorithmOption{QStringLiteral("crc8-maxim"), QStringLiteral("CRC-8/MAXIM"), 1},
         AlgorithmKind::Crc, crcParams(8, 0x31, 0x00, true, true, 0x00)},
        {AppChecksum::AlgorithmOption{QStringLiteral("crc8-rohc"), QStringLiteral("CRC-8/ROHC"), 1},
         AlgorithmKind::Crc, crcParams(8, 0x07, 0xFF, true, true, 0x00)},
        // RefEng catalog parameters (reflected); the reflected variant is what
        // produces the catalog check value 0x25 for "123456789".
        {AppChecksum::AlgorithmOption{QStringLiteral("crc8-wcdma"), QStringLiteral("CRC-8/WCDMA"), 1},
         AlgorithmKind::Crc, crcParams(8, 0x9B, 0x00, true, true, 0x00)},

        {AppChecksum::AlgorithmOption{QStringLiteral("crc16-ccitt-false"), QStringLiteral("CRC-16/CCITT-FALSE"), 2},
         AlgorithmKind::Crc, crcParams(16, 0x1021, 0xFFFF, false, false, 0x0000)},
        {AppChecksum::AlgorithmOption{QStringLiteral("crc16-xmodem"), QStringLiteral("CRC-16/XMODEM"), 2},
         AlgorithmKind::Crc, crcParams(16, 0x1021, 0x0000, false, false, 0x0000)},
        {AppChecksum::AlgorithmOption{QStringLiteral("crc16-x25"), QStringLiteral("CRC-16/X-25"), 2},
         AlgorithmKind::Crc, crcParams(16, 0x1021, 0xFFFF, true, true, 0xFFFF)},
        {AppChecksum::AlgorithmOption{QStringLiteral("crc16-kermit"), QStringLiteral("CRC-16/KERMIT"), 2},
         AlgorithmKind::Crc, crcParams(16, 0x1021, 0x0000, true, true, 0x0000)},
        // Alias: ARC.
        {AppChecksum::AlgorithmOption{QStringLiteral("crc16-ibm"), QStringLiteral("CRC-16/IBM"), 2},
         AlgorithmKind::Crc, crcParams(16, 0x8005, 0x0000, true, true, 0x0000)},
        {AppChecksum::AlgorithmOption{QStringLiteral("crc16-maxim"), QStringLiteral("CRC-16/MAXIM"), 2},
         AlgorithmKind::Crc, crcParams(16, 0x8005, 0x0000, true, true, 0xFFFF)},
        {AppChecksum::AlgorithmOption{QStringLiteral("crc16-usb"), QStringLiteral("CRC-16/USB"), 2},
         AlgorithmKind::Crc, crcParams(16, 0x8005, 0xFFFF, true, true, 0xFFFF)},
        {AppChecksum::AlgorithmOption{QStringLiteral("crc16-dnp"), QStringLiteral("CRC-16/DNP"), 2},
         AlgorithmKind::Crc, crcParams(16, 0x3D65, 0x0000, true, true, 0xFFFF)},
        // CRC-16/PEC check for "123456789" is 0x31C3 with these parameters; the
        // often-cited check value 0xFA56 is not reproducible with any
        // CCITT-family parameterization (init 0xFFFF gives 0x29B1 instead).
        {AppChecksum::AlgorithmOption{QStringLiteral("crc16-pec"), QStringLiteral("CRC-16/PEC"), 2},
         AlgorithmKind::Crc, crcParams(16, 0x1021, 0x0000, false, false, 0x0000)},

        {AppChecksum::AlgorithmOption{QStringLiteral("crc32-bzip2"), QStringLiteral("CRC-32/BZIP2"), 4},
         AlgorithmKind::Crc, crcParams(32, 0x04C11DB7, 0xFFFFFFFF, false, false, 0xFFFFFFFF)},
        {AppChecksum::AlgorithmOption{QStringLiteral("crc32-jamcrc"), QStringLiteral("CRC-32/JAMCRC"), 4},
         AlgorithmKind::Crc, crcParams(32, 0x04C11DB7, 0xFFFFFFFF, true, true, 0x00000000)},
        {AppChecksum::AlgorithmOption{QStringLiteral("crc32-mpeg2"), QStringLiteral("CRC-32/MPEG-2"), 4},
         AlgorithmKind::Crc, crcParams(32, 0x04C11DB7, 0xFFFFFFFF, false, false, 0x00000000)},
        // RevEng catalog CRC-32/CKSUM (alias CRC-32/POSIX); its check value
        // 0x765E7680 excludes the length the cksum utility appends, so no
        // length is appended here either.
        {AppChecksum::AlgorithmOption{QStringLiteral("crc32-posix"), QStringLiteral("CRC-32/POSIX"), 4},
         AlgorithmKind::Crc, crcParams(32, 0x04C11DB7, 0x00000000, false, false, 0xFFFFFFFF)},
        // These parameters yield 0x89A1897F for "123456789"; the value
        // 0xCBF43926 sometimes quoted for ZMODEM is the reflected IEEE
        // CRC-32 check, not this parameterization's.
        {AppChecksum::AlgorithmOption{QStringLiteral("crc32-zmodem"), QStringLiteral("CRC-32/ZMODEM"), 4},
         AlgorithmKind::Crc, crcParams(32, 0x04C11DB7, 0x00000000, false, false, 0x00000000)},

        {AppChecksum::AlgorithmOption{QStringLiteral("checksum8"), QStringLiteral("CHECKSUM-8"), 1},
         AlgorithmKind::Sum, {}},
        {AppChecksum::AlgorithmOption{QStringLiteral("checksum8r"), QStringLiteral("CHECKSUM-8R/REVERSE"), 1},
         AlgorithmKind::SumInverted, {}},
        {AppChecksum::AlgorithmOption{QStringLiteral("checksum16"), QStringLiteral("CHECKSUM-16"), 2},
         AlgorithmKind::Sum16, {}},
        {AppChecksum::AlgorithmOption{QStringLiteral("checksum16-internet"),
                                      QStringLiteral("CHECKSUM-16/INTERNET"), 2},
         AlgorithmKind::InternetChecksum16, {}},
        {AppChecksum::AlgorithmOption{QStringLiteral("checksum16-mitsubishi"),
                                      QStringLiteral("CHECKSUM-16/MITSUBISHI"), 2},
         AlgorithmKind::Sum16Negated, {}},
    };
    return specs;
}

const QList<AppChecksum::AlgorithmOption> &algorithmOptions()
{
    struct OptionsHolder
    {
        QList<AppChecksum::AlgorithmOption> options;
        explicit OptionsHolder(const QList<AlgorithmSpec> &specs)
        {
            for (const AlgorithmSpec &spec : specs) {
                options.append(spec.option);
            }
        }
    };
    static const OptionsHolder holder(algorithmSpecs());
    return holder.options;
}

// Returns the option key for a supported name or alias, or an empty string.
QString supportedAlgorithmKey(const QString &key)
{
    const QString normalized = key.trimmed().toLower();
    if (normalized == QStringLiteral("modbus") || normalized == QStringLiteral("crc-modbus")) {
        return QStringLiteral("crc16-modbus");
    }
    if (normalized == QStringLiteral("ccitt") || normalized == QStringLiteral("crc16")) {
        return QStringLiteral("crc16-ccitt");
    }
    if (normalized == QStringLiteral("crc")) {
        return QStringLiteral("crc32");
    }
    if (normalized == QStringLiteral("xmodem")) {
        return QStringLiteral("crc16-xmodem");
    }
    if (normalized == QStringLiteral("kermit")) {
        return QStringLiteral("crc16-kermit");
    }
    if (normalized == QStringLiteral("arc")) {
        return QStringLiteral("crc16-ibm");
    }
    if (normalized == QStringLiteral("cksum")) {
        return QStringLiteral("crc32-posix");
    }
    if (normalized == QStringLiteral("sum")) {
        return QStringLiteral("sum8");
    }

    for (const AppChecksum::AlgorithmOption &option : algorithmOptions()) {
        if (option.key == normalized) {
            return option.key;
        }
    }
    return {};
}

const AlgorithmSpec &specForKey(const QString &key)
{
    const QString normalized = AppChecksum::normalizedAlgorithmKey(key);
    for (const AlgorithmSpec &spec : algorithmSpecs()) {
        if (spec.option.key == normalized) {
            return spec;
        }
    }
    return algorithmSpecs().first();
}

const AppChecksum::AlgorithmOption &optionForKey(const QString &key)
{
    return specForKey(key).option;
}

quint32 reverseBits(quint32 value, int width)
{
    quint32 result = 0;
    for (int i = 0; i < width; ++i) {
        result = (result << 1) | (value & 1u);
        value >>= 1;
    }
    return result;
}

// Generic width/poly/init/refin/refout/xorout CRC engine. Reflected input uses
// the bit-reversed polynomial; the final reflection is applied only when the
// input and output reflections differ.
quint32 crcGeneric(const QByteArray &data, const CrcParams &params)
{
    const quint32 mask = params.width >= 32 ? 0xFFFFFFFFu : ((1u << params.width) - 1u);
    const quint32 msb = 1u << (params.width - 1);
    const quint32 poly = params.refin ? reverseBits(params.poly, params.width) : params.poly;
    quint32 crc = params.init & mask;
    for (unsigned char byte : data) {
        if (params.refin) {
            crc ^= byte;
            for (int i = 0; i < 8; ++i) {
                crc = (crc & 1u) != 0 ? (crc >> 1) ^ poly : crc >> 1;
            }
        } else {
            crc ^= static_cast<quint32>(byte) << (params.width - 8);
            for (int i = 0; i < 8; ++i) {
                crc = (crc & msb) != 0 ? ((crc << 1) ^ poly) & mask : (crc << 1) & mask;
            }
        }
    }
    if (params.refin != params.refout) {
        crc = reverseBits(crc, params.width);
    }
    return (crc ^ params.xorout) & mask;
}

quint32 sumOfBytes(const QByteArray &data)
{
    quint32 sum = 0;
    for (unsigned char byte : data) {
        sum += byte;
    }
    return sum;
}

quint8 lrc(const QByteArray &data)
{
    quint8 sum = 0;
    for (unsigned char byte : data) {
        sum = static_cast<quint8>(sum + byte);
    }
    return static_cast<quint8>(0U - sum);
}

quint8 xor8(const QByteArray &data)
{
    quint8 result = 0;
    for (unsigned char byte : data) {
        result ^= byte;
    }
    return result;
}

quint32 internetChecksum16(const QByteArray &data)
{
    quint32 sum = 0;
    for (int i = 0; i < data.size(); i += 2) {
        // An odd trailing byte is padded with a zero low byte (RFC 1071).
        quint32 word = static_cast<quint32>(static_cast<unsigned char>(data.at(i))) << 8;
        if (i + 1 < data.size()) {
            word |= static_cast<unsigned char>(data.at(i + 1));
        }
        sum += word;
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return static_cast<quint32>(~sum) & 0xFFFFu;
}

QByteArray valueBytes(quint32 value, int widthBytes, AppChecksum::ByteOrder byteOrder)
{
    QByteArray bytes;
    bytes.reserve(widthBytes);
    if (byteOrder == AppChecksum::ByteOrder::LittleEndian) {
        for (int i = 0; i < widthBytes; ++i) {
            bytes.append(static_cast<char>((value >> (8 * i)) & 0xFF));
        }
        return bytes;
    }

    for (int i = widthBytes - 1; i >= 0; --i) {
        bytes.append(static_cast<char>((value >> (8 * i)) & 0xFF));
    }
    return bytes;
}

} // namespace

namespace AppChecksum {

QList<AlgorithmOption> options() { return algorithmOptions(); }

QString defaultAlgorithmKey() { return QStringLiteral("crc16-modbus"); }

QString normalizedAlgorithmKey(const QString &key)
{
    const QString supported = supportedAlgorithmKey(key);
    return supported.isEmpty() ? defaultAlgorithmKey() : supported;
}

bool isSupportedAlgorithm(const QString &key) { return !supportedAlgorithmKey(key).isEmpty(); }

QString labelForAlgorithm(const QString &key) { return optionForKey(key).label; }

int widthForAlgorithm(const QString &key) { return optionForKey(key).widthBytes; }

ByteOrder byteOrderFromKey(const QString &key)
{
    return key.trimmed().toLower() == QStringLiteral("big") ? ByteOrder::BigEndian : ByteOrder::LittleEndian;
}

QString byteOrderKey(ByteOrder byteOrder)
{
    return byteOrder == ByteOrder::BigEndian ? QStringLiteral("big") : QStringLiteral("little");
}

QString byteOrderLabel(ByteOrder byteOrder)
{
    return byteOrder == ByteOrder::BigEndian ? AppI18n::text("高字节在前") : AppI18n::text("低字节在前");
}

ChecksumResult calculate(const QByteArray &data, const QString &algorithmKey, ByteOrder byteOrder)
{
    ChecksumResult result;
    // An unsupported key falls through to the error below instead of the default algorithm.
    const QString key = supportedAlgorithmKey(algorithmKey);
    if (key.isEmpty()) {
        result.errorMessage = AppI18n::text("不支持的校验算法");
        return result;
    }
    const AlgorithmSpec &spec = specForKey(key);
    const int widthBytes = spec.option.widthBytes;

    switch (spec.kind) {
    case AlgorithmKind::Crc:
        result.value = crcGeneric(data, spec.crc);
        break;
    case AlgorithmKind::Sum:
        result.value = sumOfBytes(data) & 0xFFu;
        break;
    case AlgorithmKind::SumInverted:
        result.value = (~sumOfBytes(data)) & 0xFFu;
        break;
    case AlgorithmKind::Sum16:
        result.value = sumOfBytes(data) & 0xFFFFu;
        break;
    case AlgorithmKind::InternetChecksum16:
        result.value = internetChecksum16(data);
        break;
    case AlgorithmKind::Sum16Negated:
        result.value = (0x10000u - (sumOfBytes(data) & 0xFFFFu)) & 0xFFFFu;
        break;
    case AlgorithmKind::Xor:
        result.value = xor8(data);
        break;
    case AlgorithmKind::Lrc:
        result.value = lrc(data);
        break;
    }

    result.bytes = valueBytes(result.value, widthBytes, byteOrder);
    result.ok = true;
    return result;
}

ChecksumResult calculate(const QByteArray &data, const QString &algorithmKey, ByteOrder byteOrder, int skipLeading,
                         int skipTrailing)
{
    if (skipLeading < 0 || skipTrailing < 0 || qint64(skipLeading) + skipTrailing >= data.size()) {
        ChecksumResult result;
        result.errorMessage = AppI18n::text("校验范围为空：共 %1 B，跳过开头 %2 B、末尾 %3 B")
                                  .arg(data.size())
                                  .arg(skipLeading)
                                  .arg(skipTrailing);
        return result;
    }
    return calculate(data.mid(skipLeading, data.size() - skipLeading - skipTrailing), algorithmKey, byteOrder);
}

} // namespace AppChecksum
