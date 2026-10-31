#include "app/core/command_template.h"
#include "app/core/app_i18n.h"

#include <QtCore/QStringList>

#include <functional>

namespace {

struct PlaceholderSpec
{
    QString name;
    int widthBytes = 0;
    bool floating = false;
    bool signedType = false;
};

const PlaceholderSpec &placeholderSpec(const QString &name)
{
    static const QList<PlaceholderSpec> specs = {
        {QStringLiteral("u8"), 1, false, false}, {QStringLiteral("i8"), 1, false, true},
        {QStringLiteral("u16"), 2, false, false}, {QStringLiteral("i16"), 2, false, true},
        {QStringLiteral("u32"), 4, false, false}, {QStringLiteral("i32"), 4, false, true},
        {QStringLiteral("f32"), 4, true, true},
    };
    static const PlaceholderSpec invalid;
    for (const PlaceholderSpec &spec : specs) {
        if (spec.name == name) {
            return spec;
        }
    }
    return invalid;
}

bool isHexDigit(QChar ch)
{
    return (ch >= QLatin1Char('0') && ch <= QLatin1Char('9')) || (ch >= QLatin1Char('a') && ch <= QLatin1Char('f'))
           || (ch >= QLatin1Char('A') && ch <= QLatin1Char('F'));
}

int hexValue(QChar ch)
{
    if (ch <= QLatin1Char('9')) {
        return ch.unicode() - QLatin1Char('0').unicode();
    }
    if (ch >= QLatin1Char('a')) {
        return 10 + ch.unicode() - QLatin1Char('a').unicode();
    }
    return 10 + ch.unicode() - QLatin1Char('A').unicode();
}

void appendValueBytes(QByteArray &out, qint64 value, int widthBytes, bool bigEndian)
{
    if (bigEndian) {
        for (int shift = 8 * (widthBytes - 1); shift >= 0; shift -= 8) {
            out.append(static_cast<char>((value >> shift) & 0xFF));
        }
        return;
    }
    for (int shift = 0; shift < 8 * widthBytes; shift += 8) {
        out.append(static_cast<char>((value >> shift) & 0xFF));
    }
}

void appendFloatBytes(QByteArray &out, float value, bool bigEndian)
{
    const auto *raw = reinterpret_cast<const unsigned char *>(&value);
    if (bigEndian) {
        for (int i = sizeof(float) - 1; i >= 0; --i) {
            out.append(static_cast<char>(raw[i]));
        }
        return;
    }
    out.append(reinterpret_cast<const char *>(raw), sizeof(float));
}

CommandTemplateResult fail(qsizetype offset, const QString &message)
{
    CommandTemplateResult result;
    result.ok = false;
    result.errorOffset = offset;
    result.errorMessage = message;
    return result;
}

} // namespace

CommandTemplateResult renderCommandTemplate(
    const QString &input, bool bigEndian,
    const std::function<QByteArray(const QByteArray &, QString *)> &appendChecksum)
{
    CommandTemplateResult result;
    result.ok = true;

    auto renderValue = [&](const QString &specText, const QString &valueText, qsizetype offset) -> bool {
        const QString spec = specText.trimmed().toLower();
        const PlaceholderSpec &specInfo = placeholderSpec(spec);
        if (specInfo.widthBytes == 0) {
            result = fail(offset, AppI18n::text("不支持的模板占位符"));
            return false;
        }
        QString error;
        if (specInfo.floating) {
            bool numericOk = false;
            const float value = valueText.toFloat(&numericOk);
            if (!numericOk) {
                result = fail(offset, AppI18n::text("占位符数值无效"));
                return false;
            }
            appendFloatBytes(result.bytes, value, bigEndian);
            return true;
        }
        bool numericOk = false;
        const qint64 value = valueText.startsWith(QLatin1String("0x")) || valueText.startsWith(QLatin1String("0X"))
                                 ? valueText.mid(2).toLongLong(&numericOk, 16)
                                 : valueText.toLongLong(&numericOk, 10);
        if (!numericOk) {
            result = fail(offset, AppI18n::text("占位符数值无效"));
            return false;
        }
        const qint64 magnitude = 1LL << (8 * specInfo.widthBytes - (specInfo.signedType ? 1 : 0));
        const qint64 minimum = specInfo.signedType ? -magnitude : 0;
        const qint64 maximum = specInfo.signedType ? magnitude - 1 : magnitude - 1;
        if (value < minimum || value > maximum) {
            result = fail(offset, AppI18n::text("占位符数值超出 %1 范围").arg(specInfo.name.toUpper()));
            return false;
        }
        appendValueBytes(result.bytes, value, specInfo.widthBytes, bigEndian);
        return true;
    };

    qsizetype i = 0;
    const qsizetype size = input.size();
    while (i < size) {
        QChar ch = input.at(i);
        if (ch.isSpace() || ch == QLatin1Char(',') || ch == QLatin1Char(';')) {
            ++i;
            continue;
        }
        if (ch != QLatin1Char('[')) {
            // Plain hex token, one or two digit pairs like the HEX parser.
            QString hex;
            while (i < size && isHexDigit(input.at(i))) {
                hex.append(input.at(i));
                ++i;
            }
            if (hex.isEmpty()) {
                return fail(i, AppI18n::text("包含非 HEX 字符"));
            }
            if (hex.size() % 2 != 0) {
                return fail(i - hex.size(), AppI18n::text("连续 HEX 字符数量必须为偶数"));
            }
            for (qsizetype n = 0; n < hex.size(); n += 2) {
                result.bytes.append(static_cast<char>((hexValue(hex.at(n)) << 4) | hexValue(hex.at(n + 1))));
            }
            continue;
        }

        const qsizetype tokenStart = i;
        const qsizetype close = input.indexOf(QLatin1Char(']'), i);
        if (close < 0) {
            return fail(tokenStart, AppI18n::text("模板占位符缺少右括号"));
        }
        const QString body = input.mid(i + 1, close - i - 1).trimmed();
        i = close + 1;
        if (body.compare(QLatin1String("crc"), Qt::CaseInsensitive) == 0) {
            QString error;
            const QByteArray checksum = appendChecksum(result.bytes, &error);
            if (!error.isEmpty()) {
                return fail(tokenStart, error);
            }
            result.bytes.append(checksum);
            continue;
        }
        const qsizetype colon = body.indexOf(QLatin1Char(':'));
        const QString specText = colon < 0 ? body : body.left(colon);
        const QString valueText = colon < 0 ? QString() : body.mid(colon + 1).trimmed();
        if (!renderValue(specText, valueText, tokenStart)) {
            return result;
        }
    }
    return result;
}
