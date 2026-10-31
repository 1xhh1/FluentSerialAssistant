#include "app/view/ascii_table_dialog.h"

#include "app/core/app_i18n.h"

#include <FluentQtWidgets/FluentQtWidgets.h>

#include <QtGui/QClipboard>
#include <QtWidgets/QApplication>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QVBoxLayout>

using namespace FluentQt;

namespace {

constexpr int kColumnDec = 0;
constexpr int kColumnHex = 1;
constexpr int kColumnChar = 2;

const char *const kControlMnemonics[32] = {
    "NUL", "SOH", "STX", "ETX", "EOT", "ENQ", "ACK", "BEL", "BS",  "HT",  "LF",  "VT",  "FF",  "CR",  "SO",  "SI",
    "DLE", "DC1", "DC2", "DC3", "DC4", "NAK", "SYN", "ETB", "CAN", "EM",  "SUB", "ESC", "FS",  "GS",  "RS",  "US"};

QString characterDisplay(int code)
{
    if (code < 0x20) {
        return QStringLiteral("%1 (^%2)").arg(QLatin1String(kControlMnemonics[code]),
                                              QChar(0x40 + code));
    }
    if (code == 0x7F) {
        return QStringLiteral("DEL (^?)");
    }
    return QChar(static_cast<char>(code));
}

} // namespace

AsciiTableDialog::AsciiTableDialog(QWidget *parent) : QWidget(parent, Qt::Window)
{
    setWindowTitle(AppI18n::text("ASCII字符表"));
    setMinimumSize(420, 560);
    resize(460, 640);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    m_table = new TableWidget(this);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels(
        {AppI18n::text("十进制"), AppI18n::text("十六进制"), AppI18n::text("字符")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->setWordWrap(false);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(28);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_table->setColumnWidth(kColumnDec, 90);
    m_table->setColumnWidth(kColumnHex, 90);
    m_table->horizontalHeader()->setSectionResizeMode(kColumnChar, QHeaderView::Stretch);
    root->addWidget(m_table);

    m_table->setRowCount(128);
    for (int code = 0; code < 128; ++code) {
        auto *decItem = new QTableWidgetItem(QString::number(code));
        decItem->setTextAlignment(Qt::AlignCenter);
        auto *hexItem = new QTableWidgetItem(QStringLiteral("0x") +
                                             QString::number(code, 16).rightJustified(2, QLatin1Char('0')).toUpper());
        hexItem->setTextAlignment(Qt::AlignCenter);
        auto *charItem = new QTableWidgetItem(characterDisplay(code));
        // Raw character copied on click, including control characters.
        charItem->setData(Qt::UserRole, QChar(static_cast<char>(code)));
        for (QTableWidgetItem *item : {decItem, hexItem, charItem}) {
            item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        }
        m_table->setItem(code, kColumnDec, decItem);
        m_table->setItem(code, kColumnHex, hexItem);
        m_table->setItem(code, kColumnChar, charItem);
    }

    connect(m_table, &TableWidget::cellClicked, this, &AsciiTableDialog::copyCell);
}

void AsciiTableDialog::copyCell(int row, int column)
{
    Q_UNUSED(column);
    if (row < 0 || row >= m_table->rowCount()) {
        return;
    }
    const QTableWidgetItem *charItem = m_table->item(row, kColumnChar);
    if (charItem) {
        QApplication::clipboard()->setText(charItem->data(Qt::UserRole).toString());
    }
}
