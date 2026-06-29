#include "DLDatabaseExportService.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include "DLLogging.h"

#if defined(Q_OS_IOS)
bool DLPresentIosShareSheet(const QString& filePath, QString* error);
#endif

bool DLDatabaseExportService::exportVocabularyDatabase(const QString& sourceDatabasePath,
                                                       QString* exportedPath,
                                                       QString* error) const
{
    if (exportedPath) {
        exportedPath->clear();
    }

    const QString destinationPath = defaultExportPath();
    if (destinationPath.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Unable to choose an export location.");
        }
        return false;
    }

    if (!exportDatabaseToPath(sourceDatabasePath, destinationPath, error)) {
        return false;
    }

    if (exportedPath) {
        *exportedPath = destinationPath;
    }

#if defined(Q_OS_IOS)
    return DLPresentIosShareSheet(destinationPath, error);
#else
    return true;
#endif
}

bool DLDatabaseExportService::exportDatabaseToPath(const QString& sourceDatabasePath,
                                                   const QString& targetPath,
                                                   QString* error) const
{
    const QFileInfo sourceInfo(sourceDatabasePath);
    if (!sourceInfo.exists() || !sourceInfo.isFile()) {
        if (error) {
            *error = QStringLiteral("Database file not found.");
        }
        qCWarning(dlApp) << "Database export failed: source file not found";
        return false;
    }

    QFileInfo destinationInfo(targetPath);
    QDir destinationDir = destinationInfo.absoluteDir();
    if (!destinationDir.exists() && !destinationDir.mkpath(QStringLiteral("."))) {
        if (error) {
            *error = QStringLiteral("Unable to create export folder.");
        }
        qCWarning(dlApp) << "Database export failed: unable to create folder";
        return false;
    }

    if (destinationInfo.exists() && !QFile::remove(destinationInfo.absoluteFilePath())) {
        if (error) {
            *error = QStringLiteral("Unable to replace the selected export file.");
        }
        qCWarning(dlApp) << "Database export failed: unable to replace destination";
        return false;
    }

    qCInfo(dlApp) << "Exporting database to" << destinationInfo.absoluteFilePath();
    if (!QFile::copy(sourceInfo.absoluteFilePath(), destinationInfo.absoluteFilePath())) {
        if (error) {
            *error = QStringLiteral("Unable to export database.");
        }
        qCWarning(dlApp) << "Database export failed while copying";
        return false;
    }

    if (error) {
        error->clear();
    }
    return true;
}

QString DLDatabaseExportService::defaultExportPath() const
{
#if defined(Q_OS_IOS)
    const QString basePath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
#elif defined(Q_OS_ANDROID)
    QString basePath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (basePath.isEmpty()) {
        basePath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    }
#else
    QString basePath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (basePath.isEmpty()) {
        basePath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    }
#endif

    const QString fallbackPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QString rootPath = basePath.isEmpty() ? fallbackPath : basePath;
    if (rootPath.isEmpty()) {
        return QString();
    }

#if defined(Q_OS_IOS)
    return QDir(rootPath).filePath(exportFileName());
#else
    return QDir(rootPath).filePath(QStringLiteral("DE Vocab Learner/%1").arg(exportFileName()));
#endif
}

QString DLDatabaseExportService::exportFileName() const
{
    return QStringLiteral("de-vocab-%1.devocab")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
}
