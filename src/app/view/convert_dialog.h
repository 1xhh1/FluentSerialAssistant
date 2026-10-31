#pragma once

#include <QtWidgets/QWidget>

namespace FluentQt {
class CaptionLabel;
class ComboBox;
class LineEdit;
class TableWidget;
} // namespace FluentQt

// Number conversion calculator: one input field converted live to HEX, DEC,
// BIN and IEEE754 float representations, plus a two-way ASCII/HEX converter
// that honors the terminal text encoding.
class ConvertDialog : public QWidget
{
    Q_OBJECT

  public:
    explicit ConvertDialog(QWidget *parent = nullptr);

  private:
    void rebuildResults();
    void rebuildAsciiSection();
    void copyResultCell(int row, int column);

    FluentQt::ComboBox *m_modeCombo = nullptr;
    FluentQt::ComboBox *m_byteOrderCombo = nullptr;
    FluentQt::LineEdit *m_inputEdit = nullptr;
    FluentQt::CaptionLabel *m_errorLabel = nullptr;
    FluentQt::TableWidget *m_resultTable = nullptr;

    FluentQt::ComboBox *m_encodingCombo = nullptr;
    FluentQt::LineEdit *m_textToHexEdit = nullptr;
    FluentQt::LineEdit *m_textToHexResult = nullptr;
    FluentQt::LineEdit *m_hexToTextEdit = nullptr;
    FluentQt::LineEdit *m_hexToTextResult = nullptr;
};
