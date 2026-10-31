#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <functional>

// Synex-style command templates: hex bytes mixed with typed value
// placeholders and a checksum slot, e.g. "AA 55 01 [i8:7] [crc] 0D 0A".
// Placeholder forms: [u8:v] [i8:v] [u16:v] [i16:v] [u32:v] [i32:v] [f32:v]
// (v is decimal or 0x-prefixed; multi-byte values serialize in the requested
// byte order) and [crc], which injects the configured checksum over every
// byte rendered before its position.
struct CommandTemplateResult
{
    bool ok = false;
    QByteArray bytes;
    qsizetype errorOffset = -1;
    QString errorMessage;
};

// checksum: computed with the given little/big-endian flag for byte layout;
// the checksum bytes are produced by the caller through appendChecksum, which
// receives the payload rendered so far.
CommandTemplateResult renderCommandTemplate(
    const QString &input, bool bigEndian,
    const std::function<QByteArray(const QByteArray &, QString *)> &appendChecksum);
