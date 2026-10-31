#pragma once

#include <QtCore/QHash>
#include <QtWidgets/QWidget>

class JLinkCommander;
class QHideEvent;
class QShowEvent;
class QTimer;

namespace FluentQt {
class CaptionLabel;
class ComboBox;
class LineEdit;
class PlainTextEdit;
class PushButton;
class PrimaryPushButton;
class RealtimePlotWidget;
} // namespace FluentQt

class WorkbenchPage;

// Synex-style dashboard: one source bar (serial RX or J-Link RTT) feeding
// tiles - a live plot with auto-registered channels, the RTT console and the
// SWD memory R/W inspector. Replaces the sidebar debugger card.
class WorkbenchDashboard : public QWidget
{
    Q_OBJECT

  public:
    explicit WorkbenchDashboard(WorkbenchPage *page, QWidget *parent = nullptr);
    ~WorkbenchDashboard() override;

  signals:
    void backRequested();

  protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

  private slots:
    void startSource();
    void stopSource();
    void pollSource();
    void readMemory();
    void writeMemory();
    void clearConsole();

  private:
    void appendConsoleText(const QString &text);
    void feedLines(const QString &text);
    void feedSample(const QString &channel, double value);
    void updateUi();

    WorkbenchPage *m_page = nullptr;
    JLinkCommander *m_jlink = nullptr;
    QTimer *m_pollTimer = nullptr;
    QTimer *m_feedTimer = nullptr;
    QString m_lineBuffer;

    FluentQt::ComboBox *m_sourceCombo = nullptr;
    FluentQt::ComboBox *m_channelCombo = nullptr;
    FluentQt::LineEdit *m_deviceEdit = nullptr;
    FluentQt::LineEdit *m_speedEdit = nullptr;
    FluentQt::PushButton *m_startButton = nullptr;
    FluentQt::PushButton *m_stopButton = nullptr;
    FluentQt::CaptionLabel *m_statusLabel = nullptr;

    FluentQt::RealtimePlotWidget *m_plot = nullptr;
    QHash<QString, int> m_seriesIndex;

    FluentQt::PlainTextEdit *m_consoleView = nullptr;

    FluentQt::LineEdit *m_addressEdit = nullptr;
    FluentQt::LineEdit *m_sizeEdit = nullptr;
    FluentQt::LineEdit *m_writeEdit = nullptr;
    FluentQt::PushButton *m_readButton = nullptr;
    FluentQt::PushButton *m_writeButton = nullptr;
    FluentQt::CaptionLabel *m_memoryLabel = nullptr;
    FluentQt::PlainTextEdit *m_memoryView = nullptr;
};
