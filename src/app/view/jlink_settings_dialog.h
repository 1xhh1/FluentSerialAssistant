#pragma once

#include <QtWidgets/QDialog>

class QTableWidget;
class QTimer;

namespace FluentQt {
class LineEdit;
class ComboBox;
class PushButton;
} // namespace FluentQt

// Device picker backed by the J-Link device database (JLINK_DEVICE_GetInfo):
// searchable table of vendor / device / flash / RAM.
class JLinkDevicePickerDialog : public QDialog
{
    Q_OBJECT

  public:
    explicit JLinkDevicePickerDialog(const QString &dllPath, QWidget *parent = nullptr);

    QString selectedDevice() const { return m_selectedDevice; }

  private slots:
    void rebuildTable();
    void acceptSelection();

  private:
    bool loadDeviceList();

    QString m_dllPath;
    QString m_selectedDevice;
    struct DeviceEntry
    {
        QString vendor;
        QString device;
        quint32 flash = 0;
        quint32 ram = 0;
    };
    QList<DeviceEntry> m_devices;
    FluentQt::LineEdit *m_searchEdit = nullptr;
    QTableWidget *m_table = nullptr;
    FluentQt::PushButton *m_acceptButton = nullptr;
};

// J-Link RTT parameters dialog (Synex's gear dialog): probe, interface,
// speed, RTT channels, target device with picker, DLL and script files.
class JLinkSettingsDialog : public QDialog
{
    Q_OBJECT

  public:
    struct Settings
    {
        QString probeConnection = QStringLiteral("USB");
        QString probeParameter = QStringLiteral("0");
        QString interfaceType = QStringLiteral("SWD");
        int speedKhz = 4000;
        int upChannel = 0;
        int downChannel = 0;
        QString device;
        QString dllPath;
        QString scriptFile;
    };

    explicit JLinkSettingsDialog(const Settings &current, QWidget *parent = nullptr);

    Settings settings() const;

  private:
    void pickDevice();

    FluentQt::ComboBox *m_probeConnectionCombo = nullptr;
    FluentQt::LineEdit *m_probeParameterEdit = nullptr;
    FluentQt::ComboBox *m_interfaceCombo = nullptr;
    FluentQt::LineEdit *m_speedEdit = nullptr;
    FluentQt::LineEdit *m_upChannelEdit = nullptr;
    FluentQt::LineEdit *m_downChannelEdit = nullptr;
    FluentQt::LineEdit *m_deviceEdit = nullptr;
    FluentQt::LineEdit *m_dllPathEdit = nullptr;
    FluentQt::LineEdit *m_scriptEdit = nullptr;
};
