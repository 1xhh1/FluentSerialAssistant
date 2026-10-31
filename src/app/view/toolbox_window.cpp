#include "app/view/toolbox_window.h"

#include "app/core/app_i18n.h"

#include <FluentQtWidgets/Views/ItemViews.h>
#include <FluentQtWidgets/Widgets/ScrollArea.h>

#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QListWidgetItem>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QVBoxLayout>

using namespace FluentQt;

namespace {

constexpr int ToolListWidth = 168;

} // namespace

ToolboxWindow::ToolboxWindow(QWidget *parent) : QWidget(parent, Qt::Window)
{
    setWindowTitle(AppI18n::text("工具箱"));
    setObjectName(QStringLiteral("toolboxWindow"));
    resize(760, 660);

    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(12);

    m_list = new ListWidget(this);
    m_list->setObjectName(QStringLiteral("toolboxToolList"));
    m_list->setFixedWidth(ToolListWidth);
    m_list->setBorderVisible(false);
    root->addWidget(m_list);

    m_scroll = new ScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("toolboxScrollArea"));
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    if (m_scroll->horizontalFluentScrollBar()) {
        m_scroll->horizontalFluentScrollBar()->setForceHidden(true);
    }
    m_host = new QWidget(m_scroll);
    m_hostLayout = new QVBoxLayout(m_host);
    m_hostLayout->setContentsMargins(0, 0, 0, 0);
    m_hostLayout->setSpacing(0);
    m_scroll->setWidget(m_host);
    m_scroll->setWidgetResizable(true);
    root->addWidget(m_scroll, 1);

    connect(m_list, &ListWidget::currentRowChanged, this, [this](int row) {
        const QString key = m_keyByRow.value(row);
        if (!key.isEmpty()) {
            switchTool(key);
        }
    });
}

void ToolboxWindow::addTool(const QString &key, const QString &title, FluentIcon toolIcon, QWidget *card)
{
    if (!card || m_pages.contains(key)) {
        return;
    }

    auto *item = new QListWidgetItem(icon(toolIcon), title);
    item->setSizeHint(QSize(ToolListWidth - 16, 40));
    m_list->addItem(item);
    const int row = m_list->count() - 1;
    m_rowByKey.insert(key, row);
    m_keyByRow.insert(row, key);

    card->hide();
    m_pages.insert(key, card);
    m_hostLayout->addWidget(card);
}

void ToolboxWindow::switchTool(const QString &key)
{
    QWidget *page = m_pages.value(key);
    if (!page) {
        return;
    }
    const bool changed = m_currentKey != key;
    m_currentKey = key;
    for (auto it = m_pages.constBegin(); it != m_pages.constEnd(); ++it) {
        it.value()->setVisible(it.key() == key);
    }
    const int row = m_rowByKey.value(key, -1);
    if (row >= 0 && m_list->currentRow() != row) {
        m_list->setCurrentRow(row);
    }
    if (m_scroll->verticalScrollBar()) {
        m_scroll->verticalScrollBar()->setValue(0);
    }
    if (changed) {
        emit toolChanged(key);
    }
}