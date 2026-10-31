#include "app/view/main_window.h"

#include "app/core/app_i18n.h"
#include "app/core/app_settings.h"
#include "app/core/font_preferences.h"

#include <FluentQtWidgets/FluentQtWidgets.h>

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QSettings>
#include <QtCore/QTimer>
#include <QtGui/QPixmap>
#include <QtWidgets/QApplication>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QSplashScreen>
#include <QtWidgets/QStyleFactory>

int main(int argc, char *argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    QApplication app(argc, argv);
    Q_INIT_RESOURCE(app);
    Q_INIT_RESOURCE(fluentqtwidgets);
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QCoreApplication::setOrganizationName(QStringLiteral("txp"));
    QCoreApplication::setApplicationName(QStringLiteral("FluentSerialAssistant"));
    QCoreApplication::setApplicationVersion(QStringLiteral(FLUENT_SERIAL_ASSISTANT_VERSION));

    const QString configDir = AppSettings::directoryPath();
    const QString legacyConfigDir = QCoreApplication::applicationDirPath();
    const auto migrateLegacyConfig = [&configDir, &legacyConfigDir](const QString &fileName) {
        const QString targetPath = QDir(configDir).filePath(fileName);
        const QString legacyPath = QDir(legacyConfigDir).filePath(fileName);
        if (!QFile::exists(targetPath) && QFile::exists(legacyPath)) {
            QFile::copy(legacyPath, targetPath);
        }
    };
    migrateLegacyConfig(QStringLiteral("FluentSerialAssistant.ini"));
    migrateLegacyConfig(QStringLiteral("FluentSerialAssistant.fluent.json"));

    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, configDir);
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, configDir);
    FluentQt::FluentConfig::instance()->setFileName(
        QDir(configDir).filePath(QStringLiteral("FluentSerialAssistant.fluent.json")));

    AppFontPreferences::loadCustomFonts();
    FluentQt::FluentConfig::instance()->load();
    AppI18n::installTranslators(&app, FluentQt::FluentConfig::instance()->localeName());
    FluentQt::ThemeManager::instance()->setTheme(FluentQt::FluentConfig::instance()->themeMode());
    FluentQt::ThemeManager::instance()->setAccentColor(FluentQt::FluentConfig::instance()->themeColor());
    AppFontPreferences::applyConfiguredUiFont();

    // Startup splash: a dark card with the logo, an indeterminate-style bar
    // and staged status text, replaced by the main window once it is up.
    QPixmap splashPixmap(QStringLiteral(":/app/splash.png"));
    QSplashScreen splash(splashPixmap);
    auto *splashProgress = new QProgressBar(&splash);
    splashProgress->setGeometry(46, 372, 420, 6);
    splashProgress->setTextVisible(false);
    splashProgress->setRange(0, 0);
    splashProgress->setStyleSheet(
        QStringLiteral("QProgressBar { background: rgba(255,255,255,38); border: none; border-radius: 3px; }"
                       "QProgressBar::chunk { background: #00d3d8; border-radius: 3px; }"));
    splash.show();
    splashProgress->raise();
    QApplication::processEvents();

    MainWindow window;
    splash.finish(&window);
    window.show();
    QTimer::singleShot(1000, &window, &MainWindow::startUpdateCheck);

    return app.exec();
}
