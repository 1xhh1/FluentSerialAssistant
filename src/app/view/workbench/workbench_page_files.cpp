#include "app/core/app_i18n.h"
#include "app/view/workbench/workbench_page_internal.h"

using namespace FluentQt;
using namespace WorkbenchPagePrivate;

namespace {

QByteArray csvLogHeader()
{
    return QByteArray("\xEF\xBB\xBFtimestamp,direction,source,length,hex,text\n");
}

} // namespace

namespace {

// The native Windows save dialog restores its own last folder and ignores the
// one we ask for, which breaks "start next to the program". Qt's own dialog
// honours it, so both save flows use this.
QString askForSavePath(QWidget *parent, const QString &title, const QString &folder, const QString &fileName,
                       const QStringList &filters, const QString &preselectedFilter, QString *chosenFilter)
{
    QFileDialog dialog(parent, title);
    AppUi::configureSaveDialog(dialog, folder, fileName, filters, preselectedFilter);
    if (dialog.exec() != QDialog::Accepted) {
        return QString();
    }
    if (chosenFilter) {
        *chosenFilter = dialog.selectedNameFilter();
    }
    return dialog.selectedFiles().value(0);
}

} // namespace

void WorkbenchPage::promptExportRecords()
{
    if (m_records.isEmpty()) {
        showWarning(AppI18n::text("没有可导出的记录"), AppI18n::text("当前会话还没有收发数据"));
        return;
    }

    AppSettings settings;
    const QString folder = settings.value(QStringLiteral("export/folder"), AppPaths::defaultExportFolder()).toString();
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString txtFilter = AppI18n::text("TXT 文本 (*.txt)");
    const QString csvFilter = AppI18n::text("CSV 表格 (*.csv)");
    const QString binFilter = AppI18n::text("BIN 原始字节 (*.bin)");
    QString selectedFilter;
    QString path = askForSavePath(window(), AppI18n::text("导出接收区"), folder,
                                  QStringLiteral("serial_session_%1.txt").arg(stamp),
                                  {txtFilter, csvFilter, binFilter}, txtFilter, &selectedFilter);
    if (path.isEmpty()) {
        return;
    }

    const ExportFormat format = exportFormatForFilter(selectedFilter, txtFilter, csvFilter, binFilter);
    path = withExportSuffix(path, format);
    settings.setValue(QStringLiteral("export/folder"), QFileInfo(path).absolutePath());
    if (writeRecordsToFile(path, format)) {
        showSuccess(AppI18n::text("导出完成"), path);
    }
}

WorkbenchPage::ExportFormat WorkbenchPage::exportFormatForFilter(const QString &selected, const QString &txtFilter,
                                                 const QString &csvFilter, const QString &binFilter)
{
    if (selected == csvFilter) {
        return ExportFormat::Csv;
    }
    if (selected == binFilter) {
        return ExportFormat::Bin;
    }
    if (selected == txtFilter) {
        return ExportFormat::Txt;
    }
    return ExportFormat::Txt;
}

QString WorkbenchPage::withExportSuffix(const QString &path, ExportFormat format)
{
    const QFileInfo info(path);
    const QString suffix = exportSuffix(format);
    if (info.suffix().compare(suffix, Qt::CaseInsensitive) == 0) {
        return path;
    }
    const QString base = info.completeBaseName().isEmpty() ? info.fileName() : info.completeBaseName();
    return QDir(info.absolutePath()).filePath(base + QLatin1Char('.') + suffix);
}

bool WorkbenchPage::writeRecordsToFile(const QString &path, ExportFormat format)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        showError(AppI18n::text("导出失败"), file.errorString());
        return false;
    }
    if (format == ExportFormat::Csv) {
        file.write(csvLogHeader());
    }
    for (const SessionRecord &record : m_records) {
        if (record.direction == RecordDirection::FrameBreak) {
            continue;
        }
        const QByteArray chunk = serializeLogRecord(record, format);
        if (!chunk.isEmpty()) {
            file.write(chunk);
        }
    }
    file.close();
    return true;
}

QString WorkbenchPage::autoLogFormatKey() const
{
    return m_autoLogFormatCombo ? m_autoLogFormatCombo->currentData().toString() : QStringLiteral("txt");
}

WorkbenchPage::ExportFormat WorkbenchPage::autoLogFormat() const
{
    const QString format = autoLogFormatKey();
    if (format == QStringLiteral("csv")) {
        return ExportFormat::Csv;
    }
    if (format == QStringLiteral("bin")) {
        return ExportFormat::Bin;
    }
    return ExportFormat::Txt;
}

qint64 WorkbenchPage::autoLogMaxFileBytes() const
{
    const qint64 megabytes = numberEditValue(m_autoLogMaxSizeEdit, 16, 1, 4096);
    return megabytes * 1024 * 1024;
}

void WorkbenchPage::updateAutoLog(bool enabled)
{
    AppSettings settings;
    settings.setValue(QStringLiteral("log/enabled"), enabled);

    if (!enabled) {
        closeAutoLog();
        return;
    }

    resetAutoLogSession();
    updateAutoLogStatus();
}

void WorkbenchPage::resetAutoLogSession()
{
    if (m_autoLogFile.isOpen()) {
        m_autoLogFile.close();
    }
    m_autoLogFile.setFileName(QString());
    m_autoLogSessionStamp.clear();
    m_autoLogFileIndex = 1;
    m_autoLogCurrentSize = 0;
}

void WorkbenchPage::closeAutoLog()
{
    if (m_autoLogFile.isOpen()) {
        m_autoLogFile.close();
    }
    updateAutoLogStatus();
}

QByteArray WorkbenchPage::serializeLogRecord(const SessionRecord &record, ExportFormat format) const
{
    if (format == ExportFormat::Bin) {
        return record.bytes;
    }

    if (format == ExportFormat::Csv) {
        const QString line = QStringLiteral("%1,%2,%3,%4,%5,%6\n")
                                 .arg(csvEscape(record.timestamp.toString(Qt::ISODateWithMs)),
                                      csvEscape(directionText(record.direction)), csvEscape(record.sourceLabel))
                                 .arg(record.bytes.size())
                                 .arg(csvEscape(bytesToHex(record.bytes)), csvEscape(record.displayText));
        return line.toUtf8();
    }

    const QString source =
        record.sourceLabel.trimmed().isEmpty() ? QString() : QStringLiteral(" [%1]").arg(record.sourceLabel.trimmed());
    const QString line = QStringLiteral("%1 %2%3 len=%4 hex=%5 text=%6\n")
                             .arg(record.timestamp.toString(Qt::ISODateWithMs), directionText(record.direction), source)
                             .arg(record.bytes.size())
                             .arg(bytesToHex(record.bytes), record.displayText);
    return line.toUtf8();
}

bool WorkbenchPage::ensureAutoLogFile(ExportFormat format, qint64 nextBytes)
{
    const qint64 maxBytes = autoLogMaxFileBytes();
    if (m_autoLogFile.isOpen() && m_autoLogCurrentSize > 0 && m_autoLogCurrentSize + nextBytes > maxBytes) {
        m_autoLogFile.close();
        ++m_autoLogFileIndex;
        m_autoLogCurrentSize = 0;
    }

    if (m_autoLogFile.isOpen()) {
        return true;
    }

    AppSettings settings;
    const QString folderPath = settings.value(QStringLiteral("export/folder"), AppPaths::defaultExportFolder()).toString();
    QDir folder(folderPath);
    if (!folder.exists() && !folder.mkpath(QStringLiteral("."))) {
        const QSignalBlocker blocker(m_autoLogCheck);
        if (m_autoLogCheck) {
            m_autoLogCheck->setChecked(false);
        }
        settings.setValue(QStringLiteral("log/enabled"), false);
        showError(AppI18n::text("自动日志失败"), AppI18n::text("无法创建目录：%1").arg(folderPath));
        updateAutoLogStatus();
        return false;
    }

    if (m_autoLogSessionStamp.isEmpty()) {
        m_autoLogSessionStamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"));
    }

    QString portName = m_lastConfig.type == LinkType::Serial ? m_lastConfig.serial.portName : m_lastConfig.key();
    if (portName.isEmpty() && m_portCombo) {
        portName = m_portCombo->currentData().toString();
    }
    const QString baudRate = m_lastConfig.type == LinkType::Serial && m_lastConfig.serial.baudRate > 0
                                 ? QString::number(m_lastConfig.serial.baudRate)
                                 : (m_baudCombo ? m_baudCombo->currentText().trimmed() : QStringLiteral("baud"));
    const QString fileName = QStringLiteral("serial_log_%1_%2_%3_%4.%5")
                                 .arg(m_autoLogSessionStamp, safeFileNamePart(portName), safeFileNamePart(baudRate))
                                 .arg(m_autoLogFileIndex, 3, 10, QLatin1Char('0'))
                                 .arg(exportSuffix(format));
    const QString path = folder.filePath(fileName);

    m_autoLogFile.setFileName(path);
    if (!m_autoLogFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString error = m_autoLogFile.errorString();
        const QSignalBlocker blocker(m_autoLogCheck);
        if (m_autoLogCheck) {
            m_autoLogCheck->setChecked(false);
        }
        settings.setValue(QStringLiteral("log/enabled"), false);
        m_autoLogFile.setFileName(QString());
        showError(AppI18n::text("自动日志失败"), error);
        updateAutoLogStatus();
        return false;
    }

    m_autoLogCurrentSize = 0;
    if (format == ExportFormat::Csv) {
        const QByteArray header = csvLogHeader();
        m_autoLogFile.write(header);
        m_autoLogCurrentSize += header.size();
    }
    updateAutoLogStatus();
    return true;
}

void WorkbenchPage::writeAutoLogRecord(const SessionRecord &record)
{
    if (record.direction == RecordDirection::FrameBreak || !m_autoLogCheck || !m_autoLogCheck->isChecked()) {
        return;
    }

    const ExportFormat format = autoLogFormat();
    const QByteArray output = serializeLogRecord(record, format);
    if (output.isEmpty() && format != ExportFormat::Bin) {
        return;
    }
    if (!ensureAutoLogFile(format, output.size())) {
        return;
    }

    const qint64 written = m_autoLogFile.write(output);
    if (written != output.size()) {
        const QString error = m_autoLogFile.errorString();
        closeAutoLog();
        const QSignalBlocker blocker(m_autoLogCheck);
        m_autoLogCheck->setChecked(false);
        AppSettings().setValue(QStringLiteral("log/enabled"), false);
        showError(AppI18n::text("自动日志失败"), error.isEmpty() ? AppI18n::text("写入不完整") : error);
        return;
    }
    m_autoLogFile.flush();
    m_autoLogCurrentSize += written;
    updateAutoLogStatus();
}

void WorkbenchPage::updateAutoLogStatus()
{
    if (!m_autoLogStatusLabel) {
        return;
    }

    m_autoLogStatusLabel->setAccessibleDescription(QString());
    if (!m_autoLogCheck || !m_autoLogCheck->isChecked()) {
        m_autoLogStatusLabel->setText(AppI18n::text("自动日志未启用"));
        return;
    }

    if (m_autoLogFile.isOpen()) {
        const QString path = m_autoLogFile.fileName();
        m_autoLogStatusLabel->setText(AppI18n::text("自动日志：%1 · %2 / %3")
                                          .arg(QFileInfo(path).fileName(), formatBytes(m_autoLogCurrentSize),
                                               formatBytes(autoLogMaxFileBytes())));
        m_autoLogStatusLabel->setAccessibleDescription(path);
        return;
    }

    m_autoLogStatusLabel->setText(m_link.isOpen() ? AppI18n::text("自动日志已启用，等待数据写入")
                                                    : AppI18n::text("自动日志已启用，连接后写入"));
}

void WorkbenchPage::updateReceiveCapture(bool enabled)
{
    AppSettings settings;
    settings.setValue(QStringLiteral("receive/saveToFile"), enabled);

    // Restoring the option at startup must not ask for a file; the connection
    // resumes the saved path through ensureReceiveCaptureOpen().
    if (!m_settingsRestored) {
        return;
    }

    if (!enabled) {
        closeReceiveCapture();
        return;
    }
    if (m_receiveCaptureFile.isOpen()) {
        return;
    }

    // The dialog picks both the destination and the save type, so the sidebar
    // does not need one button per format.
    const QString folderPath = settings.value(QStringLiteral("export/folder"), AppPaths::defaultExportFolder()).toString();
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString txtFilter = AppI18n::text("TXT 文本 (*.txt)");
    const QString csvFilter = AppI18n::text("CSV 表格 (*.csv)");
    const QString binFilter = AppI18n::text("BIN 原始字节 (*.bin)");
    const QString currentFormat = settings.value(QStringLiteral("receive/saveFormat"), QStringLiteral("bin")).toString();
    const ExportFormat suggestedFormat = currentFormat == QStringLiteral("txt")  ? ExportFormat::Txt
                                         : currentFormat == QStringLiteral("csv") ? ExportFormat::Csv
                                                                                  : ExportFormat::Bin;
    const QString suggestedFilter = suggestedFormat == ExportFormat::Txt  ? txtFilter
                                    : suggestedFormat == ExportFormat::Csv ? csvFilter
                                                                           : binFilter;
    QString selectedFilter;
    QString path = askForSavePath(
        window(), AppI18n::text("选择接收保存文件"), folderPath,
        QStringLiteral("serial_rx_%1.%2").arg(stamp, exportSuffix(suggestedFormat)),
        {txtFilter, csvFilter, binFilter}, suggestedFilter, &selectedFilter);
    if (path.isEmpty()) {
        // Cancelled: put the switch back instead of failing halfway.
        settings.setValue(QStringLiteral("receive/saveToFile"), false);
        const QSignalBlocker blocker(m_saveReceiveCheck);
        m_saveReceiveCheck->setChecked(false);
        closeReceiveCapture();
        return;
    }

    const ExportFormat format = exportFormatForFilter(selectedFilter, txtFilter, csvFilter, binFilter);
    path = withExportSuffix(path, format);
    settings.setValue(QStringLiteral("receive/saveFormat"), exportSuffix(format));
    settings.setValue(QStringLiteral("receive/saveFile"), path);
    settings.setValue(QStringLiteral("export/folder"), QFileInfo(path).absolutePath());
    ensureReceiveCaptureOpen();
}

void WorkbenchPage::ensureReceiveCaptureOpen()
{
    // Never opens a dialog: called from the connect path and from the receive
    // path, where asking for a file would interrupt the stream.
    if (m_receiveCaptureFile.isOpen()) {
        return;
    }
    AppSettings settings;
    if (!settings.value(QStringLiteral("receive/saveToFile"), false).toBool() || !m_link.isOpen()) {
        if (m_receiveCaptureLabel) {
            updateReceiveCaptureLabel();
        }
        return;
    }
    const QString path = settings.value(QStringLiteral("receive/saveFile")).toString();
    if (path.isEmpty()) {
        if (m_receiveCaptureLabel) {
            updateReceiveCaptureLabel();
        }
        return;
    }

    QDir folder = QFileInfo(path).absoluteDir();
    if (!folder.exists() && !folder.mkpath(QStringLiteral("."))) {
        const QSignalBlocker blocker(m_saveReceiveCheck);
        m_saveReceiveCheck->setChecked(false);
        settings.setValue(QStringLiteral("receive/saveToFile"), false);
        showError(AppI18n::text("接收保存失败"), AppI18n::text("无法创建目录：%1").arg(folder.absolutePath()));
        return;
    }

    const bool freshFile = !QFileInfo::exists(path) || QFileInfo(path).size() == 0;
    m_receiveCaptureFile.setFileName(path);
    if (!m_receiveCaptureFile.open(QIODevice::WriteOnly | QIODevice::Append)) {
        const QSignalBlocker blocker(m_saveReceiveCheck);
        m_saveReceiveCheck->setChecked(false);
        settings.setValue(QStringLiteral("receive/saveToFile"), false);
        showError(AppI18n::text("接收保存失败"), m_receiveCaptureFile.errorString());
        return;
    }
    if (freshFile && receiveCaptureFormat() == ExportFormat::Csv) {
        m_receiveCaptureFile.write(csvLogHeader());
        m_receiveCaptureFile.flush();
    }

    updateReceiveCaptureLabel();
}

WorkbenchPage::ExportFormat WorkbenchPage::receiveCaptureFormat() const
{
    AppSettings settings;
    const QString format = settings.value(QStringLiteral("receive/saveFormat"), QStringLiteral("bin")).toString();
    if (format == QStringLiteral("csv")) {
        return ExportFormat::Csv;
    }
    if (format == QStringLiteral("txt")) {
        return ExportFormat::Txt;
    }
    return ExportFormat::Bin;
}

void WorkbenchPage::writeReceiveCaptureRecord(const SessionRecord &record)
{
    if (record.direction != RecordDirection::Rx || !m_receiveCaptureFile.isOpen()) {
        return;
    }
    const ExportFormat format = receiveCaptureFormat();
    const QByteArray output = serializeLogRecord(record, format);
    if (output.isEmpty() && format != ExportFormat::Bin) {
        return;
    }
    const qint64 written = m_receiveCaptureFile.write(output);
    if (written != output.size()) {
        const QString error = m_receiveCaptureFile.errorString();
        closeReceiveCapture();
        const QSignalBlocker blocker(m_saveReceiveCheck);
        if (m_saveReceiveCheck) {
            m_saveReceiveCheck->setChecked(false);
        }
        AppSettings settings;
        settings.setValue(QStringLiteral("receive/saveToFile"), false);
        showError(AppI18n::text("接收保存失败"), error.isEmpty() ? AppI18n::text("写入不完整") : error);
        return;
    }
    // One flush per frame would cost a syscall per frame; batch it and always
    // flush when the capture closes.
    m_receiveCapturePendingBytes += written;
    if (m_receiveCapturePendingBytes >= 64 * 1024) {
        m_receiveCaptureFile.flush();
        m_receiveCapturePendingBytes = 0;
    }
}

void WorkbenchPage::closeReceiveCapture()
{
    if (m_receiveCaptureFile.isOpen()) {
        m_receiveCaptureFile.flush();
        m_receiveCaptureFile.close();
    }
    m_receiveCapturePendingBytes = 0;
    updateReceiveCaptureLabel();
}

void WorkbenchPage::updateReceiveCaptureLabel()
{
    if (!m_receiveCaptureLabel) {
        return;
    }
    AppSettings settings;
    const bool enabled = m_saveReceiveCheck && m_saveReceiveCheck->isChecked();
    const QString path = settings.value(QStringLiteral("receive/saveFile")).toString();
    m_receiveCaptureLabel->setAccessibleDescription(path);
    if (!enabled) {
        m_receiveCaptureLabel->setText(AppI18n::text("接收保存未启用"));
        return;
    }
    if (path.isEmpty()) {
        m_receiveCaptureLabel->setText(AppI18n::text("接收保存已启用，尚未选择文件"));
        return;
    }
    // Only the file name is shown; the full path stays in the tooltip.
    const QString name = QFileInfo(path).fileName();
    m_receiveCaptureLabel->setText(m_receiveCaptureFile.isOpen() ? AppI18n::text("保存中：%1").arg(name)
                                                                : AppI18n::text("连接后开始保存：%1").arg(name));
    m_receiveCaptureLabel->setToolTip(path);
}

void WorkbenchPage::browseSendFile()
{
    AppSettings settings;
    const QString initialPath = m_filePathEdit && !m_filePathEdit->text().isEmpty()
                                    ? QFileInfo(m_filePathEdit->text()).absolutePath()
                                    : settings.value(QStringLiteral("export/folder"), AppPaths::defaultExportFolder()).toString();
    const QString path = QFileDialog::getOpenFileName(window(), AppI18n::text("选择发送文件"), initialPath);
    if (path.isEmpty()) {
        return;
    }

    const QFileInfo info(path);
    settings.setValue(QStringLiteral("fileSend/path"), path);
    settings.setValue(QStringLiteral("export/folder"), info.absolutePath());
    m_filePathEdit->setText(path);
    m_fileStatusLabel->setText(QStringLiteral("%1 · %2").arg(info.fileName(), formatBytes(info.size())));
    updateFileProgress();
}

void WorkbenchPage::startFileSend()
{
    if (!m_link.isOpen()) {
        showWarning(AppI18n::text("无法发送文件"), AppI18n::text("请先连接串口"));
        return;
    }
    if (m_fileSendFile.isOpen()) {
        return;
    }

    const QString path = m_filePathEdit->text().trimmed();
    if (path.isEmpty()) {
        showWarning(AppI18n::text("未选择文件"), AppI18n::text("请先选择待发送文件"));
        return;
    }

    QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        showError(AppI18n::text("文件不可用"), AppI18n::text("无法读取：%1").arg(path));
        return;
    }
    if (info.size() <= 0) {
        showWarning(AppI18n::text("文件为空"), AppI18n::text("请选择包含数据的文件"));
        return;
    }

    m_fileSendFile.setFileName(path);
    if (!m_fileSendFile.open(QIODevice::ReadOnly)) {
        showError(AppI18n::text("打开文件失败"), m_fileSendFile.errorString());
        return;
    }

    AppSettings settings;
    settings.setValue(QStringLiteral("fileSend/path"), path);
    settings.setValue(QStringLiteral("fileSend/chunkSize"),
                      numberEditValue(m_fileChunkSizeEdit, DefaultFileChunkSize, 1, 65536));
    settings.setValue(QStringLiteral("fileSend/intervalMs"),
                      numberEditValue(m_fileIntervalEdit, DefaultFileChunkIntervalMs, 0, 60000));

    m_fileSendTotal = m_fileSendFile.size();
    m_fileSendSent = 0;
    updateFileSendUi(true);
    updateFileProgress();
    sendNextFileChunk();
}

void WorkbenchPage::cancelFileSend()
{
    if (!m_fileSendFile.isOpen()) {
        return;
    }
    m_fileSendTimer.stop();
    m_fileSendFile.close();
    updateFileSendUi(false);
    updateFileProgress();
    showInfo(AppI18n::text("文件发送已取消"), AppI18n::text("已停止发送当前文件"));
}

void WorkbenchPage::sendNextFileChunk()
{
    if (!m_fileSendFile.isOpen()) {
        return;
    }
    if (!m_link.isOpen()) {
        cancelFileSend();
        showWarning(AppI18n::text("文件发送中止"), AppI18n::text("串口已断开"));
        return;
    }
    // Queue the next chunk only after the port has taken the previous one, so
    // progress, cancellation and completion follow what was actually sent.
    if (m_link.bytesToWrite() > 0) {
        m_fileSendTimer.start(FileSendDrainPollMs);
        return;
    }

    if (m_fileSendFile.atEnd()) {
        const QString name = QFileInfo(m_fileSendFile.fileName()).fileName();
        m_fileSendFile.close();
        updateFileSendUi(false);
        updateFileProgress();
        showSuccess(AppI18n::text("文件发送完成"), name);
        return;
    }

    const int chunkSize = numberEditValue(m_fileChunkSizeEdit, DefaultFileChunkSize, 1, 65536);
    const QByteArray data = m_fileSendFile.read(chunkSize);
    if (data.isEmpty()) {
        const QString error = m_fileSendFile.errorString();
        m_fileSendFile.close();
        updateFileSendUi(false);
        updateFileProgress();
        showError(AppI18n::text("读取文件失败"), error);
        return;
    }

    QString error;
    if (!m_link.writeData(data, &error)) {
        m_fileSendFile.close();
        updateFileSendUi(false);
        updateFileProgress();
        showError(AppI18n::text("文件发送失败"), error);
        return;
    }

    m_fileSendSent += data.size();
    appendRecord(RecordDirection::Tx, data);
    updateFileProgress();

    m_fileSendTimer.start(m_fileSendFile.atEnd()
                              ? 0
                              : numberEditValue(m_fileIntervalEdit, DefaultFileChunkIntervalMs, 0, 60000));
}

void WorkbenchPage::updateFileSendUi(bool sending)
{
    if (m_fileBrowseButton) {
        m_fileBrowseButton->setEnabled(!sending);
    }
    if (m_filePathEdit) {
        m_filePathEdit->setEnabled(!sending);
    }
    if (m_fileChunkSizeEdit) {
        m_fileChunkSizeEdit->setEnabled(!sending);
    }
    if (m_fileIntervalEdit) {
        m_fileIntervalEdit->setEnabled(!sending);
    }
    if (m_fileSendButton) {
        m_fileSendButton->setEnabled(!sending && m_link.isOpen());
    }
    if (m_fileCancelButton) {
        m_fileCancelButton->setEnabled(sending);
    }
}

void WorkbenchPage::updateFileProgress()
{
    const int percent = m_fileSendTotal > 0 ? static_cast<int>((m_fileSendSent * 100) / m_fileSendTotal) : 0;
    if (m_fileProgressBar) {
        m_fileProgressBar->setValue(qBound(0, percent, 100));
    }
    if (!m_fileStatusLabel) {
        return;
    }

    if (m_fileSendFile.isOpen()) {
        m_fileStatusLabel->setText(
            AppI18n::text("发送中：%1 / %2").arg(formatBytes(m_fileSendSent), formatBytes(m_fileSendTotal)));
        return;
    }

    const QString path = m_filePathEdit ? m_filePathEdit->text().trimmed() : QString();
    if (path.isEmpty()) {
        m_fileStatusLabel->setText(AppI18n::text("未选择文件"));
        return;
    }
    const QFileInfo info(path);
    m_fileStatusLabel->setText(info.exists() ? QStringLiteral("%1 · %2").arg(info.fileName(), formatBytes(info.size()))
                                             : AppI18n::text("文件不存在：%1").arg(path));
}
