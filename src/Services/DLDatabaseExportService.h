#ifndef DLDATABASEEXPORTSERVICE_H
#define DLDATABASEEXPORTSERVICE_H

#include <QString>

class DLDatabaseExportService
{
public:
    bool exportVocabularyDatabase(const QString& sourceDatabasePath,
                                  QString* exportedPath,
                                  QString* error) const;
    bool exportDatabaseToPath(const QString& sourceDatabasePath,
                              const QString& targetPath,
                              QString* error) const;

private:
    QString defaultExportPath() const;
    QString exportFileName() const;
};

#endif // DLDATABASEEXPORTSERVICE_H
