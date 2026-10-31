#pragma once

#include <FluentQtWidgets/FluentIcon.h>

#include <QtCore/QHash>
#include <QtWidgets/QWidget>

namespace FluentQt {
class ListWidget;
class ScrollArea;
} // namespace FluentQt

class QVBoxLayout;

// Modeless tool window: a tool list on the left, the selected tool's card on
// the right. Keeps the workbench sidebar free of the long tail of settings.
class ToolboxWindow : public QWidget
{
    Q_OBJECT

  public:
    explicit ToolboxWindow(QWidget *parent = nullptr);

    void addTool(const QString &key, const QString &title, FluentQt::FluentIcon toolIcon, QWidget *card);
    void switchTool(const QString &key);
    QString currentToolKey() const { return m_currentKey; }
    QWidget *toolPage(const QString &key) const { return m_pages.value(key); }

  signals:
    void toolChanged(const QString &key);

  private:
    FluentQt::ListWidget *m_list = nullptr;
    FluentQt::ScrollArea *m_scroll = nullptr;
    QWidget *m_host = nullptr;
    QVBoxLayout *m_hostLayout = nullptr;
    QHash<QString, QWidget *> m_pages;
    QHash<QString, int> m_rowByKey;
    QHash<int, QString> m_keyByRow;
    QString m_currentKey;
};