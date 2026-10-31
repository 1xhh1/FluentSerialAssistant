#pragma once

#include "app/debug/jlink_debugger.h"

#include <QtWidgets/QWidget>

class QTimer;

namespace FluentQt {
class CaptionLabel;
class LineEdit;
class PlainTextEdit;
class PushButton;
class PrimaryPushButton;
} // namespace FluentQt

// J-Link debugger window: SWD target connect, raw memory read/write with
// verify, and an RTT channel terminal (Synex's HSS/RTT surfaces).
class DebuggerDialog : public QWidget
{
    Q_OBJECT

  public:
    explicit DebuggerDialog(QWidget *parent = nullptr);
    ~DebuggerDialog() override;

  private:
    void connectTarget();
    void disconnectTarget();
    void updateConnectionUi();
    void readMemory();
    void writeMemory();
    void pollRtt();
    void appendRttText(const QString &text);
    QString statusText() const;

    JLinkDebugger *m_debugger = nullptr;
    FluentQt::LineEdit *m_dllPathEdit = nullptr;
    FluentQt::LineEdit *m_deviceEdit = nullptr;
    FluentQt::LineEdit *m_speedEdit = nullptr;
    FluentQt::PushButton *m_connectButton = nullptr;
    FluentQt::CaptionLabel *m_connectionStatusLabel = nullptr;

    FluentQt::LineEdit *m_addressEdit = nullptr;
    FluentQt::LineEdit *m_sizeEdit = nullptr;
    FluentQt::LineEdit *m_valueEdit = nullptr;
    FluentQt::PushButton *m_readButton = nullptr;
    FluentQt::PushButton *m_writeButton = nullptr;
    FluentQt::CaptionLabel *m_memoryStatusLabel = nullptr;
    FluentQt::PlainTextEdit *m_memoryView = nullptr;

    FluentQt::LineEdit *m_rttChannelEdit = nullptr;
    FluentQt::PushButton *m_rttStartButton = nullptr;
    FluentQt::PushButton *m_rttStopButton = nullptr;
    FluentQt::PlainTextEdit *m_rttView = nullptr;
    QTimer *m_rttTimer = nullptr;
};
