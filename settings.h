#ifndef SETTINGS_H
#define SETTINGS_H

#include <QObject>
#include <QString>
#include <QCoreApplication>

const QString OrganizationName = "rusich";
const QString OrganizationDomain = "https://github.com/rusich";
const QString ApplicationName = "MonitoringServer";

class Settings: public QObject {
    Q_OBJECT
    Q_PROPERTY(QString ZabbixUser MEMBER ZabbixUser)
    Q_PROPERTY(QString ZabbixPassword MEMBER ZabbixPassword)
    Q_PROPERTY(QString ZabbixURL MEMBER ZabbixURL)
    Q_PROPERTY(QString errorMsg MEMBER errorMsg NOTIFY errorMsgChanged)
private:
    Settings() { }
    Settings(const Settings&) = delete;
    Settings& operator=(const Settings&) = delete;
public:
    ~Settings() {writeSettings();}
    static Settings* Instance();
    QString ZabbixUser;
    QString ZabbixPassword;
    QString ZabbixURL;
    QString errorMsg;
    void readSettings();
    void writeSettings();
signals:
    void errorMsgChanged();
};

#endif // SETTINGS_H
