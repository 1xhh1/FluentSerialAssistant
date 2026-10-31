#pragma once

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QStandardPaths>
#include <QtCore/QString>

namespace AppPaths {

// Where exports and captures start when no folder has been remembered yet: the
// directory the program runs from, so a portable copy keeps its files together.
inline QString defaultExportFolder()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        return appDir;
    }
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return documents.isEmpty() ? QDir::homePath() : documents;
}

} // namespace AppPaths
