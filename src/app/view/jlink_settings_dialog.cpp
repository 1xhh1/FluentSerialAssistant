#include "app/view/jlink_settings_dialog.h"

#include "app/core/app_i18n.h"
#include "app/core/app_settings.h"
#include "app/debug/jlink_commander.h"

#include <FluentQtWidgets/FluentQtWidgets.h>

#include <QtCore/QRegularExpression>
#include <QtCore/QTemporaryDir>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QVBoxLayout>

using namespace FluentQt;

namespace {

QString formatSize(quint32 bytes)
{
    if (bytes >= 1024 * 1024) {
        return QStringLiteral("%1 MB").arg(bytes / 1024.0 / 1024.0, 0, 'f', 0);
    }
    return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 0);
}

} // namespace

JLinkDevicePickerDialog::JLinkDevicePickerDialog(const QString &dllPath, QWidget *parent)
    : QDialog(parent, Qt::Window)
{
    setWindowTitle(AppI18n::text("选择 J-Link 目标设备"));
    resize(1000, 700);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    m_searchEdit = new LineEdit(this);
    m_searchEdit->setPlaceholderText(AppI18n::text("搜索厂商、设备或内核，例如 ST、STM32G431、Cortex-M4"));
    m_searchEdit->setObjectName(QStringLiteral("deviceSearchEdit"));
    root->addWidget(m_searchEdit);

    m_table = new TableWidget(this);
    m_table->setObjectName(QStringLiteral("deviceTable"));
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({AppI18n::text("厂商"), AppI18n::text("设备"), AppI18n::text("Flash"),
                                        AppI18n::text("RAM")});
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    root->addWidget(m_table, 1);

    auto *bottomRow = new QHBoxLayout;
    bottomRow->setSpacing(8);
    m_acceptButton = new PrimaryPushButton(AppI18n::text("选择"), this);
    m_acceptButton->setEnabled(false);
    bottomRow->addStretch(1);
    bottomRow->addWidget(m_acceptButton);
    auto *cancelButton = new PushButton(AppI18n::text("取消"), this);
    bottomRow->addWidget(cancelButton);
    root->addLayout(bottomRow);

    connect(m_searchEdit, &LineEdit::textChanged, this, [this](const QString &) { rebuildTable(); });
    connect(m_table, &TableWidget::cellDoubleClicked, this, [this](int, int) { acceptSelection(); });
    connect(m_table, &TableWidget::currentCellChanged, this,
            [this](int row, int, int, int) { m_acceptButton->setEnabled(row >= 0); });
    connect(m_acceptButton, &PushButton::clicked, this, &JLinkDevicePickerDialog::acceptSelection);
    connect(cancelButton, &PushButton::clicked, this, &QDialog::reject);

    if (loadDeviceList()) {
        rebuildTable();
    }
}

bool JLinkDevicePickerDialog::loadDeviceList()
{
    m_devices.clear();
    // Cache the export: enumerating 10k devices through the DLL takes a
    // moment, so only refresh when the cache is missing or stale.
    const QString cachePath =
        QDir(AppSettings::directoryPath()).filePath(QStringLiteral("debugger/devices_cache.txt"));
    const QString dllPath =
        AppSettings().value(QStringLiteral("debugger/dllPath"), JLinkCommander::defaultDllPath()).toString();
    QFileInfo cacheInfo(cachePath);
    if (!cacheInfo.exists() || cacheInfo.lastModified().daysTo(QDateTime::currentDateTime()) > 30) {
        QString error;
        const int count = JLinkCommander::exportDeviceList(dllPath, cachePath, &error);
        if (count < 0) {
            InfoBar::error(AppI18n::text("设备列表导出失败"), error, Qt::Horizontal, true, 5000,
                           InfoBarPosition::Top, window());
            return false;
        }
    }
    QFile cache(cachePath);
    if (!cache.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    while (!cache.atEnd()) {
        const QStringList parts = QString::fromUtf8(cache.readLine()).trimmed().split(QLatin1Char('|'));
        if (parts.size() != 4) {
            continue;
        }
        DeviceEntry entry;
        entry.vendor = parts.at(0);
        entry.device = parts.at(1);
        entry.flash = parts.at(2).toUInt();
        entry.ram = parts.at(3).toUInt();
        if (!entry.device.isEmpty()) {
            m_devices.append(entry);
        }
    }
    return !m_devices.isEmpty();
}

void JLinkDevicePickerDialog::rebuildTable()
{
    const QString filter = m_searchEdit->text().trimmed();
    const QStringList tokens = filter.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    m_table->setSortingEnabled(false);
    m_table->setRowCount(0);
    int shown = 0;
    for (const DeviceEntry &entry : m_devices) {
        bool match = true;
        for (const QString &token : tokens) {
            if (!entry.vendor.contains(token, Qt::CaseInsensitive)
                && !entry.device.contains(token, Qt::CaseInsensitive)) {
                match = false;
                break;
            }
        }
        if (!match) {
            continue;
        }
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setItem(row, 0, new QTableWidgetItem(entry.vendor));
        m_table->setItem(row, 1, new QTableWidgetItem(entry.device));
        m_table->setItem(row, 2, new QTableWidgetItem(formatSize(entry.flash)));
        m_table->setItem(row, 3, new QTableWidgetItem(formatSize(entry.ram)));
        ++shown;
        if (shown >= 2000) {
            break;
        }
    }
    m_table->setSortingEnabled(true);
}

void JLinkDevicePickerDialog::acceptSelection()
{
    const int row = m_table->currentRow();
    if (row < 0) {
        return;
    }
    m_selectedDevice = m_table->item(row, 1)->text();
    accept();
}

JLinkSettingsDialog::JLinkSettingsDialog(const Settings &current, QWidget *parent)
    : QDialog(parent, Qt::Window)
{
    setWindowTitle(AppI18n::text("J-Link RTT 参数"));
    resize(620, 480);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(10);

    auto *form = new QFormLayout;
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(10);

    m_probeConnectionCombo = new ComboBox(this);
    m_probeConnectionCombo->addItem(QStringLiteral("USB"), QIcon(), QStringLiteral("usb"));
    m_probeConnectionCombo->addItem(QStringLiteral("IP"), QIcon(), QStringLiteral("ip"));
    form->addRow(AppI18n::text("探针连接"), m_probeConnectionCombo);

    m_probeParameterEdit = new LineEdit(this);
    m_probeParameterEdit->setPlaceholderText(QStringLiteral("0"));
    form->addRow(AppI18n::text("探针参数"), m_probeParameterEdit);

    auto *interfaceRow = new QHBoxLayout;
    interfaceRow->setSpacing(8);
    m_interfaceCombo = new ComboBox(this);
    m_interfaceCombo->addItem(QStringLiteral("SWD"), QIcon(), QStringLiteral("swd"));
    m_interfaceCombo->addItem(QStringLiteral("JTAG"), QIcon(), QStringLiteral("jtag"));
    interfaceRow->addWidget(m_interfaceCombo, 1);
    m_speedEdit = new LineEdit(this);
    m_speedEdit->setText(QStringLiteral("4000"));
    interfaceRow->addWidget(m_speedEdit, 1);
    form->addRow(AppI18n::text("调试接口"), interfaceRow);

    auto *channelRow = new QHBoxLayout;
    channelRow->setSpacing(8);
    m_upChannelEdit = new LineEdit(this);
    m_upChannelEdit->setText(QStringLiteral("0"));
    channelRow->addWidget(m_upChannelEdit, 1);
    m_downChannelEdit = new LineEdit(this);
    m_downChannelEdit->setText(QStringLiteral("0"));
    channelRow->addWidget(m_downChannelEdit, 1);
    form->addRow(AppI18n::text("RTT 通道 (Up/Down)"), channelRow);

    auto *deviceRow = new QHBoxLayout;
    deviceRow->setSpacing(8);
    m_deviceEdit = new LineEdit(this);
    m_deviceEdit->setText(current.device);
    deviceRow->addWidget(m_deviceEdit, 1);
    auto *devicePickerButton = new TransparentToolButton(icon(FluentIcon::Search), this);
    devicePickerButton->setToolTip(AppI18n::text("选择 J-Link 目标设备"));
    deviceRow->addWidget(devicePickerButton);
    form->addRow(AppI18n::text("目标设备"), deviceRow);

    auto *dllRow = new QHBoxLayout;
    dllRow->setSpacing(8);
    m_dllPathEdit = new LineEdit(this);
    m_dllPathEdit->setText(current.dllPath);
    dllRow->addWidget(m_dllPathEdit, 1);
    auto *dllBrowseButton = new TransparentToolButton(icon(FluentIcon::Folder), this);
    dllRow->addWidget(dllBrowseButton);
    form->addRow(AppI18n::text("DLL 文件"), dllRow);

    auto *scriptRow = new QHBoxLayout;
    scriptRow->setSpacing(8);
    m_scriptEdit = new LineEdit(this);
    m_scriptEdit->setPlaceholderText(AppI18n::text("可选 J-Link ScriptFile"));
    scriptRow->addWidget(m_scriptEdit, 1);
    auto *scriptBrowseButton = new TransparentToolButton(icon(FluentIcon::Folder), this);
    scriptRow->addWidget(scriptBrowseButton);
    form->addRow(AppI18n::text("脚本文件"), scriptRow);

    root->addLayout(form);
    root->addStretch(1);

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    buttons->addStretch(1);
    auto *saveButton = new PrimaryPushButton(AppI18n::text("保存"), this);
    buttons->addWidget(saveButton);
    auto *cancelButton = new PushButton(AppI18n::text("取消"), this);
    buttons->addWidget(cancelButton);
    root->addLayout(buttons);

    m_probeConnectionCombo->setCurrentIndex(m_probeConnectionCombo->findData(current.probeConnection));
    m_probeParameterEdit->setText(current.probeParameter);
    m_interfaceCombo->setCurrentIndex(m_interfaceCombo->findData(current.interfaceType));
    m_speedEdit->setText(QString::number(current.speedKhz));
    m_upChannelEdit->setText(QString::number(current.upChannel));
    m_downChannelEdit->setText(QString::number(current.downChannel));

    connect(devicePickerButton, &TransparentToolButton::clicked, this, &JLinkSettingsDialog::pickDevice);
    connect(dllBrowseButton, &TransparentToolButton::clicked, this, [this]() {
        const QString file = QFileDialog::getOpenFileName(this, AppI18n::text("DLL 文件"),
                                                          m_dllPathEdit->text(), QStringLiteral("JLink_x64.dll"));
        if (!file.isEmpty()) {
            m_dllPathEdit->setText(file);
        }
    });
    connect(scriptBrowseButton, &TransparentToolButton::clicked, this, [this]() {
        const QString file =
            QFileDialog::getOpenFileName(this, AppI18n::text("脚本文件"), QString(), QStringLiteral("All (*.*)"));
        if (!file.isEmpty()) {
            m_scriptEdit->setText(file);
        }
    });
    connect(saveButton, &PrimaryPushButton::clicked, this, &QDialog::accept);
    connect(cancelButton, &PushButton::clicked, this, &QDialog::reject);
}

JLinkSettingsDialog::Settings JLinkSettingsDialog::settings() const
{
    Settings settings;
    settings.probeConnection = m_probeConnectionCombo->currentData().toString();
    settings.probeParameter = m_probeParameterEdit->text().trimmed();
    settings.interfaceType = m_interfaceCombo->currentData().toString();
    settings.speedKhz = m_speedEdit->text().trimmed().toInt();
    settings.upChannel = m_upChannelEdit->text().trimmed().toInt();
    settings.downChannel = m_downChannelEdit->text().trimmed().toInt();
    settings.device = m_deviceEdit->text().trimmed();
    settings.dllPath = m_dllPathEdit->text().trimmed();
    settings.scriptFile = m_scriptEdit->text().trimmed();
    return settings;
}

void JLinkSettingsDialog::pickDevice()
{
    JLinkDevicePickerDialog picker(m_dllPathEdit->text().trimmed(), this);
    if (picker.exec() == QDialog::Accepted && !picker.selectedDevice().isEmpty()) {
        m_deviceEdit->setText(picker.selectedDevice());
    }
}
