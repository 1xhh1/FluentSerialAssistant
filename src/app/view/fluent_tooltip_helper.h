#pragma once

#include <QtWidgets/QFileDialog>

#include <FluentQtWidgets/Widgets/ToolTip.h>

#include <QtCore/QEvent>
#include <QtCore/QString>
#include <QtCore/Qt>
#include <QtWidgets/QWidget>

namespace AppUi {

class ToolTipSuppressor final : public QObject
{
  public:
    explicit ToolTipSuppressor(QWidget *widget) : QObject(widget)
    {
        widget->installEventFilter(this);
        clearToolTip(widget);
    }

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::ToolTipChange) {
            clearToolTip(static_cast<QWidget *>(watched));
        } else if (event->type() == QEvent::ToolTip) {
            event->accept();
            return true;
        }
        return QObject::eventFilter(watched, event);
    }

  private:
    static void clearToolTip(QWidget *widget)
    {
        if (!widget->toolTip().isEmpty()) {
            widget->setAccessibleName(widget->toolTip());
            widget->setToolTip(QString());
        }
    }
};

inline void suppressFluentToolTip(QWidget *widget)
{
    if (widget && !widget->property("appToolTipSuppressed").toBool()) {
        widget->setProperty("appToolTipSuppressed", true);
        new ToolTipSuppressor(widget);
    }
}

inline void installFluentToolTip(QWidget *widget, FluentQt::ToolTipPosition position = FluentQt::ToolTipPosition::Top,
                                 int delayMs = 300)
{
    if (!widget) {
        return;
    }
    if (widget->findChild<FluentQt::ToolTipFilter *>(QString(), Qt::FindDirectChildrenOnly)) {
        return;
    }
    widget->installEventFilter(new FluentQt::ToolTipFilter(widget, delayMs, position));
}

inline void setFluentToolTip(QWidget *widget, const QString &text,
                             FluentQt::ToolTipPosition position = FluentQt::ToolTipPosition::Top, int delayMs = 300)
{
    if (!widget) {
        return;
    }
    widget->setToolTip(text);
    installFluentToolTip(widget, position, delayMs);
}

} // namespace AppUi

namespace AppUi {

// Shared setup for the app's save dialogs: our own QFileDialog so the requested
// start folder is honoured (the native Windows dialog restores its own last
// folder instead) plus the file type filters.
inline void configureSaveDialog(QFileDialog &dialog, const QString &folder, const QString &fileName,
                                const QStringList &filters, const QString &preselectedFilter)
{
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    if (!folder.isEmpty()) {
        dialog.setDirectory(folder);
    }
    if (!fileName.isEmpty()) {
        dialog.selectFile(fileName);
    }
    if (!filters.isEmpty()) {
        dialog.setNameFilters(filters);
        if (!preselectedFilter.isEmpty()) {
            dialog.selectNameFilter(preselectedFilter);
        }
    }
}

} // namespace AppUi
