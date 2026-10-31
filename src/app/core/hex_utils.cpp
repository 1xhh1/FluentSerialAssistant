#include "app/core/hex_utils.h"
#include "app/core/app_i18n.h"
#include "app/core/text_encoding.h"

#include <QtCore/QChar>
#include <QtCore/QStringList>

namespace {

bool isSeparator(QChar ch)
{
    return ch.isSpace() || ch == QLatin1Char(',') || ch == QLatin1Char(';') || ch == QLatin1Char('-');
}

bool isHexChar(QChar ch)
{
    return (ch >= QLatin1Char('0') && ch <= QLatin1Char('9')) || (ch >= QLatin1Char('a') && ch <= QLatin1Char('f')) ||
           (ch >= QLatin1Char('A') && ch <= QLatin1Char('F'));
}

int hexValue(QChar ch)
{
    if (ch >= QLatin1Char('0') && ch <= QLatin1Char('9')) {
        return ch.unicode() - QLatin1Char('0').unicode();
    }
    if (ch >= QLatin1Char('a') && ch <= QLatin1Char('f')) {
        return 10 + ch.unicode() - QLatin1Char('a').unicode();
    }
    return 10 + ch.unicode() - QLatin1Char('A').unicode();
}

HexParseResult fail(qsizetype offset, const QString &message)
{
    HexParseResult result;
    result.ok = false;
    result.errorOffset = offset;
    result.errorMessage = message;
    return result;
}

} // namespace

HexParseResult parseHexPayload(const QString &input)
{
    HexParseResult result;
    result.ok = true;

    qsizetype i = 0;
    const qsizetype size = input.size();
    while (i < size) {
        while (i < size && isSeparator(input.at(i))) {
            ++i;
        }
        if (i >= size) {
            break;
        }

        const qsizetype tokenStart = i;
        if (i + 1 < size && input.at(i) == QLatin1Char('0') &&
            (input.at(i + 1) == QLatin1Char('x') || input.at(i + 1) == QLatin1Char('X'))) {
            i += 2;
        }

        QString token;
        const qsizetype hexStart = i;
        while (i < size && !isSeparator(input.at(i))) {
            const QChar ch = input.at(i);
            if (!isHexChar(ch)) {
                return fail(i, AppI18n::text("包含非 HEX 字符"));
            }
            token.append(ch);
            ++i;
        }

        if (token.isEmpty()) {
            return fail(tokenStart, AppI18n::text("0x 后缺少字节"));
        }
        if (token.size() > 2 && token.size() % 2 != 0) {
            return fail(hexStart, AppI18n::text("连续 HEX 字符数量必须为偶数"));
        }

        if (token.size() <= 2) {
            int value = 0;
            for (const QChar ch : token) {
                value = (value << 4) | hexValue(ch);
            }
            result.bytes.append(static_cast<char>(value));
            continue;
        }

        for (qsizetype n = 0; n < token.size(); n += 2) {
            const int value = (hexValue(token.at(n)) << 4) | hexValue(token.at(n + 1));
            result.bytes.append(static_cast<char>(value));
        }
    }

    if (result.bytes.isEmpty() && !input.trimmed().isEmpty()) {
        return fail(0, AppI18n::text("没有解析到有效字节"));
    }

    return result;
}

QString bytesToHex(const QByteArray &data)
{
    QStringList bytes;
    bytes.reserve(data.size());
    for (unsigned char byte : data) {
        bytes.append(QStringLiteral("%1").arg(byte, 2, 16, QLatin1Char('0')).toUpper());
    }
    return bytes.join(QLatin1Char(' '));
}

QString bytesToPrintableText(const QByteArray &data)
{
    QString text = QString::fromUtf8(data);
    for (QChar &ch : text) {
        if (ch == QLatin1Char('\r') || ch == QLatin1Char('\n')) {
            ch = QLatin1Char(' ');
        } else if (ch.unicode() < 0x20 && ch != QLatin1Char('\t')) {
            ch = QLatin1Char('.');
        }
    }
    return text;
}

namespace {

EscapeParseResult failEscape(qsizetype offset, const QString &message)
{
    EscapeParseResult result;
    result.ok = false;
    result.errorOffset = offset;
    result.errorMessage = message;
    return result;
}

} // namespace

EscapeParseResult parseEscapedTextPayload(const QString &input, const QString &encodingKey)
{
    EscapeParseResult result;
    result.ok = true;

    QString literalRun;
    const auto flushLiteral = [&]() {
        if (literalRun.isEmpty()) {
            return true;
        }
        const AppTextEncoding::EncodeResult encoded = AppTextEncoding::encode(literalRun, encodingKey);
        if (!encoded.ok) {
            result = failEscape(-1, encoded.errorMessage);
            return false;
        }
        result.bytes.append(encoded.bytes);
        literalRun.clear();
        return true;
    };

    const qsizetype size = input.size();
    qsizetype i = 0;
    while (i < size) {
        const QChar ch = input.at(i);
        if (ch != QLatin1Char('\\')) {
            literalRun.append(ch);
            ++i;
            continue;
        }
        if (i + 1 >= size) {
            if (!flushLiteral()) {
                return result;
            }
            return failEscape(i, AppI18n::text("转义符后缺少字符"));
        }

        const QChar kind = input.at(i + 1);
        char escaped = 0;
        bool isEscaped = true;
        switch (kind.unicode()) {
        case 'r':
            escaped = '\r';
            break;
        case 'n':
            escaped = '\n';
            break;
        case 't':
            escaped = '\t';
            break;
        case 'a':
            escaped = '\a';
            break;
        case 'b':
            escaped = '\b';
            break;
        case 'f':
            escaped = '\f';
            break;
        case 'v':
            escaped = '\v';
            break;
        case '0':
            escaped = '\0';
            break;
        case '\\':
            escaped = '\\';
            break;
        case 'x':
        case 'X': {
            qsizetype digitsEnd = i + 2;
            while (digitsEnd < size && digitsEnd - (i + 2) < 2 && isHexChar(input.at(digitsEnd))) {
                ++digitsEnd;
            }
            if (digitsEnd == i + 2) {
                if (!flushLiteral()) {
                    return result;
                }
                return failEscape(i, AppI18n::text("\\x 后缺少 1-2 位十六进制数字"));
            }
            if (!flushLiteral()) {
                return result;
            }
            int value = 0;
            for (qsizetype n = i + 2; n < digitsEnd; ++n) {
                value = (value << 4) | hexValue(input.at(n));
            }
            result.bytes.append(static_cast<char>(value));
            i = digitsEnd;
            isEscaped = false;
            break;
        }
        default:
            if (!flushLiteral()) {
                return result;
            }
            return failEscape(i, AppI18n::text("不支持的转义序列"));
        }

        if (isEscaped) {
            if (!flushLiteral()) {
                return result;
            }
            result.bytes.append(escaped);
            i += 2;
        }
    }

    if (!flushLiteral()) {
        return result;
    }
    return result;
}
