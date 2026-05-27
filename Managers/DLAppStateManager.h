#ifndef DLAPPSTATEMANAGER_H
#define DLAPPSTATEMANAGER_H

#include <QObject>

class DLAppStateManager : public QObject
{
    Q_OBJECT
public:
    explicit DLAppStateManager(QObject *parent = nullptr);

signals:
};

#endif // DLAPPSTATEMANAGER_H
