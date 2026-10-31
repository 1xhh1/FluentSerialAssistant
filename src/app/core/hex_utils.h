#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>

struct HexParseResult
{
    bool ok = false;
    QByteArray bytes;
    qsizetype errorOffset = -1;
    QString errorMessage;
};

HexParseResult parseHexPayload(const QString &input);
QString bytesToHex(const QByteArray &data);
QString bytesToPrintableText(const QByteArray &data);

struct EscapeParseResult
{
    bool ok = false;
    QByteArray bytes;
    qsizetype errorOffset = -1;
    QString errorMessage;
};

// Decodes a send text with escape sequences: \r \n \t \a \b \f \v \0 \\ and
// \xHH (1-2 hex digits). Literal runs are encoded with the given
// AppTextEncoding key, so escape bytes stay raw regardless of the codec.
EscapeParseResult parseEscapedTextPayload(const QString &input, const QString &encodingKey);
