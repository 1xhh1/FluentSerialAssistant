#include "app/view/convert_dialog.h"

#include "app/core/app_i18n.h"
#include "app/core/hex_utils.h"
#include "app/core/text_encoding.h"

#include <FluentQtWidgets/FluentQtWidgets.h>

#include <QtGui/QClipboard>
#include <QtWidgets/QApplication>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QVBoxLayout>

using namespace FluentQt;

namespace {

constexpr int kColumnLabel = 0;
constexpr int kColumnValue = 1;

QString groupedBinary(const QByteArray &bytes, int width)
{
    quint64 value = 0;
    for (int index = 0; index < width && index < bytes.size(); ++index) {
        value |= static_cast<quint64>(static_cast<quint8>(bytes.at(index))) << (8 * index);
    }
    QString binary = QString::fromLatin1(QByteArray::number(static_cast<qulonglong>(value), 2).rightJustified(width * 8, '0'));
    QString grouped;
    grouped.reserve(binary.size() + binary.size() / 4);
    for (int index = 0; index < binary.size(); ++index) {
        if (index > 0 && index % 4 == 0) {
            grouped.append(QLatin1Char(' '));
        }
        grouped.append(binary.at(index));
    }
    return grouped;
}

// Interprets the first `width` bytes of `bytes` as an integer honoring the
// selected byte order. Missing bytes count as zero.
quint64 integerValue(const QByteArray &bytes, int width, bool bigEndian)
{
    quint64 value = 0;
    for (int index = 0; index < width; ++index) {
        const int source = bigEndian ? (width - 1 - index) : index;
        const quint8 byte = source < bytes.size() ? static_cast<quint8>(bytes.at(source)) : 0;
        value |= static_cast<quint64>(byte) << (8 * index);
    }
    return value;
}

qint64 signedValue(quint64 raw, int width)
{
    if (width == 8) {
        return static_cast<qint64>(raw);
    }
    const quint64 half = 1ULL << (width * 8 - 1);
    return raw < half ? static_cast<qint64>(raw) : static_cast<qint64>(raw - (half << 1));
}

QString hexFromInteger(quint64 value, int width)
{
    return QStringLiteral("0x") + QString::fromLatin1(
                                       QByteArray::number(static_cast<qulonglong>(value), 16).rightJustified(width * 2, '0'));
}

} // namespace

ConvertDialog::ConvertDialog(QWidget *parent) : QWidget(parent, Qt::Window)
{
    setWindowTitle(AppI18n::text("转换计算器"));
    setMinimumSize(640, 640);
    resize(720, 720);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    auto *inputTitle = new SubtitleLabel(AppI18n::text("数值转换"), this);
    root->addWidget(inputTitle);

    auto *inputForm = new QFormLayout;
    inputForm->setHorizontalSpacing(12);
    inputForm->setVerticalSpacing(8);

    m_modeCombo = new ComboBox(this);
    m_modeCombo->addItem(QStringLiteral("HEX"), QIcon(), QStringLiteral("hex"));
    m_modeCombo->addItem(QStringLiteral("DEC"), QIcon(), QStringLiteral("dec"));
    m_modeCombo->addItem(QStringLiteral("BIN"), QIcon(), QStringLiteral("bin"));
    m_modeCombo->addItem(AppI18n::text("ASCII"), QIcon(), QStringLiteral("ascii"));
    m_modeCombo->addItem(QStringLiteral("FLOAT(HEX)"), QIcon(), QStringLiteral("float"));
    m_modeCombo->setFixedSize(140, 32);
    inputForm->addRow(AppI18n::text("输入模式"), m_modeCombo);

    m_inputEdit = new LineEdit(this);
    m_inputEdit->setPlaceholderText(AppI18n::text("输入要转换的值"));
    m_inputEdit->setClearButtonEnabled(true);
    m_inputEdit->setFixedHeight(32);
    inputForm->addRow(AppI18n::text("输入值"), m_inputEdit);

    m_byteOrderCombo = new ComboBox(this);
    m_byteOrderCombo->addItem(AppI18n::text("小端序"), QIcon(), false);
    m_byteOrderCombo->addItem(AppI18n::text("大端序"), QIcon(), true);
    m_byteOrderCombo->setFixedSize(140, 32);
    inputForm->addRow(AppI18n::text("字节序"), m_byteOrderCombo);

    root->addLayout(inputForm);

    m_errorLabel = new CaptionLabel(QString(), this);
    m_errorLabel->setTextColor(QColor(196, 43, 28), QColor(255, 120, 110));
    m_errorLabel->hide();
    root->addWidget(m_errorLabel);

    m_resultTable = new TableWidget(this);
    m_resultTable->setColumnCount(2);
    m_resultTable->setHorizontalHeaderLabels({AppI18n::text("项目"), AppI18n::text("值")});
    m_resultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_resultTable->setAlternatingRowColors(true);
    m_resultTable->setWordWrap(false);
    m_resultTable->verticalHeader()->setVisible(false);
    m_resultTable->verticalHeader()->setDefaultSectionSize(30);
    m_resultTable->horizontalHeader()->setSectionResizeMode(kColumnLabel, QHeaderView::Fixed);
    m_resultTable->setColumnWidth(kColumnLabel, 180);
    m_resultTable->horizontalHeader()->setSectionResizeMode(kColumnValue, QHeaderView::Stretch);
    root->addWidget(m_resultTable, 1);

    auto *asciiTitle = new SubtitleLabel(AppI18n::text("ASCII ↔ HEX 转换"), this);
    root->addWidget(asciiTitle);

    auto *asciiForm = new QFormLayout;
    asciiForm->setHorizontalSpacing(12);
    asciiForm->setVerticalSpacing(8);

    m_encodingCombo = new ComboBox(this);
    for (const AppTextEncoding::EncodingOption &option : AppTextEncoding::options()) {
        m_encodingCombo->addItem(option.label, QIcon(), option.key);
    }
    m_encodingCombo->setCurrentIndex(
        qMax(0, m_encodingCombo->findData(AppTextEncoding::defaultKey())));
    m_encodingCombo->setFixedSize(140, 32);
    asciiForm->addRow(AppI18n::text("编码"), m_encodingCombo);

    m_textToHexEdit = new LineEdit(this);
    m_textToHexEdit->setPlaceholderText(AppI18n::text("输入文本"));
    m_textToHexEdit->setClearButtonEnabled(true);
    m_textToHexEdit->setFixedHeight(32);
    m_textToHexResult = new LineEdit(this);
    m_textToHexResult->setReadOnly(true);
    m_textToHexResult->setFixedHeight(32);
    asciiForm->addRow(AppI18n::text("文本转 HEX"), m_textToHexEdit);
    asciiForm->addRow(QString(), m_textToHexResult);

    m_hexToTextEdit = new LineEdit(this);
    m_hexToTextEdit->setPlaceholderText(AppI18n::text("输入 HEX 字节，例如 31 32 33"));
    m_hexToTextEdit->setClearButtonEnabled(true);
    m_hexToTextEdit->setFixedHeight(32);
    m_hexToTextResult = new LineEdit(this);
    m_hexToTextResult->setReadOnly(true);
    m_hexToTextResult->setFixedHeight(32);
    asciiForm->addRow(AppI18n::text("HEX 转文本"), m_hexToTextEdit);
    asciiForm->addRow(QString(), m_hexToTextResult);

    root->addLayout(asciiForm);

    connect(m_inputEdit, &LineEdit::textChanged, this, &ConvertDialog::rebuildResults);
    connect(m_modeCombo, &ComboBox::currentIndexChanged, this, &ConvertDialog::rebuildResults);
    connect(m_byteOrderCombo, &ComboBox::currentIndexChanged, this, &ConvertDialog::rebuildResults);
    connect(m_resultTable, &TableWidget::cellClicked, this, &ConvertDialog::copyResultCell);
    connect(m_textToHexEdit, &LineEdit::textChanged, this, &ConvertDialog::rebuildAsciiSection);
    connect(m_hexToTextEdit, &LineEdit::textChanged, this, &ConvertDialog::rebuildAsciiSection);
    connect(m_encodingCombo, &ComboBox::currentIndexChanged, this, &ConvertDialog::rebuildAsciiSection);

    rebuildResults();
    rebuildAsciiSection();
}

void ConvertDialog::rebuildResults()
{
    QByteArray bytes;
    bool ok = true;
    QString error;

    const QString input = m_inputEdit->text();
    const QString mode = m_modeCombo->currentData().toString();
    if (input.trimmed().isEmpty()) {
        m_errorLabel->hide();
        m_resultTable->setRowCount(0);
        return;
    }

    if (mode == QStringLiteral("hex") || mode == QStringLiteral("float")) {
        const HexParseResult parsed = parseHexPayload(input);
        ok = parsed.ok;
        error = parsed.errorMessage;
        bytes = parsed.bytes;
    } else if (mode == QStringLiteral("dec")) {
        const QString trimmed = input.trimmed();
        const bool negative = trimmed.startsWith(QLatin1Char('-'));
        const QString digits = negative ? trimmed.mid(1) : trimmed;
        bool valid = false;
        const qulonglong magnitude = digits.toULongLong(&valid, 10);
        if (valid && magnitude <= (negative ? 0x8000000000000000ULL : 0xFFFFFFFFFFFFFFFFULL)) {
            const quint64 value = negative ? ~magnitude + 1 : magnitude;
            bytes.resize(8);
            for (int index = 0; index < 8; ++index) {
                bytes[index] = static_cast<char>((value >> (8 * index)) & 0xFFU);
            }
        } else {
            ok = false;
        }
    } else if (mode == QStringLiteral("bin")) {
        QString bits = input;
        bits.remove(QLatin1Char(' '));
        bits.remove(QLatin1Char('_'));
        bool valid = false;
        const qulonglong value = bits.toULongLong(&valid, 2);
        if (valid) {
            bytes.resize(8);
            for (int index = 0; index < 8; ++index) {
                bytes[index] = static_cast<char>((value >> (8 * index)) & 0xFFU);
            }
        } else {
            ok = false;
        }
    } else if (mode == QStringLiteral("ascii")) {
        const AppTextEncoding::EncodeResult encoded =
            AppTextEncoding::encode(input, m_encodingCombo->currentData().toString());
        ok = encoded.ok;
        error = encoded.errorMessage;
        bytes = encoded.bytes;
    }

    m_errorLabel->setVisible(!ok);
    if (!ok) {
        m_errorLabel->setText(
            error.isEmpty() ? AppI18n::text("输入无效，请检查格式") : AppI18n::text("输入无效：%1").arg(error));
        m_resultTable->setRowCount(0);
        return;
    }

    const bool bigEndian = m_byteOrderCombo->currentData().toBool();
    struct ResultRow
    {
        QString label;
        QString value;
    };
    QList<ResultRow> rows;
    rows.append({QStringLiteral("HEX"), bytesToHex(bytes)});
    const struct WidthEntry
    {
        int width;
        QString name;
    } widths[] = {{1, QStringLiteral("8")}, {2, QStringLiteral("16")}, {4, QStringLiteral("32")}, {8, QStringLiteral("64")}};
    for (const WidthEntry &entry : widths) {
        const quint64 raw = integerValue(bytes, entry.width, bigEndian);
        rows.append({AppI18n::text("DEC %1位 无符号").arg(entry.name),
                     QString::number(static_cast<qulonglong>(raw))});
        rows.append({AppI18n::text("DEC %1位 有符号").arg(entry.name),
                     QString::number(signedValue(raw, entry.width))});
    }
    for (const WidthEntry &entry : widths) {
        rows.append({AppI18n::text("BIN %1位").arg(entry.name), groupedBinary(bytes, entry.width)});
    }
    auto floatRow = [&rows, &bytes, bigEndian](int width) {
        quint64 raw = integerValue(bytes, width, bigEndian);
        double value = 0;
        if (width == 4) {
            quint32 bits32 = static_cast<quint32>(raw);
            float single = 0;
            memcpy(&single, &bits32, sizeof(single));
            value = single;
        } else {
            memcpy(&value, &raw, sizeof(value));
        }
        rows.append({AppI18n::text("FLOAT %1位").arg(width),
                     QString::number(value, 'g', 9) + QStringLiteral(" (") + hexFromInteger(raw, width) +
                         QStringLiteral(")")});
    };
    floatRow(4);
    floatRow(8);

    m_resultTable->setRowCount(rows.size());
    for (int index = 0; index < rows.size(); ++index) {
        auto *labelItem = new QTableWidgetItem(rows.at(index).label);
        labelItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        auto *valueItem = new QTableWidgetItem(rows.at(index).value);
        valueItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        m_resultTable->setItem(index, kColumnLabel, labelItem);
        m_resultTable->setItem(index, kColumnValue, valueItem);
    }
}

void ConvertDialog::rebuildAsciiSection()
{
    const QString key = m_encodingCombo->currentData().toString();

    const QString text = m_textToHexEdit->text();
    if (text.isEmpty()) {
        m_textToHexResult->clear();
    } else {
        const AppTextEncoding::EncodeResult encoded = AppTextEncoding::encode(text, key);
        m_textToHexResult->setText(encoded.ok ? bytesToHex(encoded.bytes) : encoded.errorMessage);
    }

    const QString hex = m_hexToTextEdit->text();
    if (hex.isEmpty()) {
        m_hexToTextResult->clear();
    } else {
        const HexParseResult parsed = parseHexPayload(hex);
        m_hexToTextResult->setText(parsed.ok ? AppTextEncoding::decode(parsed.bytes, key) : parsed.errorMessage);
    }
}

void ConvertDialog::copyResultCell(int row, int column)
{
    if (column != kColumnValue) {
        column = kColumnValue;
    }
    if (const QTableWidgetItem *item = m_resultTable->item(row, column)) {
        QApplication::clipboard()->setText(item->text());
    }
}
