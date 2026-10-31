#pragma once

#include <QtWidgets/QWidget>

namespace FluentQt {
class TableWidget;
} // namespace FluentQt

// ASCII 0..127 reference table with copy-on-click.
class AsciiTableDialog : public QWidget
{
    Q_OBJECT

  public:
    explicit AsciiTableDialog(QWidget *parent = nullptr);

  private:
    void copyCell(int row, int column);

    FluentQt::TableWidget *m_table = nullptr;
};
