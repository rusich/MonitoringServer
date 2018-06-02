#include "settings.h"
#include <QSettings>
#include <QDir>
#include <QDebug>
#include <QRegExp>

Settings *Settings::Instance()
{
    static Settings * _instance = 0;
    if(_instance == 0)
        _instance = new Settings();
    return _instance ;
}


void Settings::readSettings()
{
    QCoreApplication::setOrganizationName(OrganizationName);
    QCoreApplication::setOrganizationDomain(OrganizationDomain);
    QCoreApplication::setApplicationName(ApplicationName);
    QSettings settings(QCoreApplication::applicationDirPath()+
                       QDir::separator()+"set.ini", QSettings::IniFormat);

    settings.beginGroup("Zabbix");
    ZabbixUser = settings.value("ZabbixUser","Admin").toString();
    ZabbixPassword = settings.value("ZabbixPassword","zabbix").toString();
    ZabbixURL = settings.value
            ("ZabbixURL","http://localhost/zabbix/api_jsonrpc.php").toString();
    settings.endGroup();

//    QRegExp ValidIP("^(([0-1]?[0-9]?[0-9]|2[0-4][0-9]|25[0-5])\\.){3}([0-1]?[0-9]?[0-9]|2[0-4][0-9]|25[0-5])$");
//    QRegExp ValidHostName("^(([a-zA-Z0-9]|[a-zA-Z0-9][a-zA-Z0-9\\-]*[a-zA-Z0-9])\\.)*([A-Za-z0-9]|[A-Za-z0-9][A-Za-z0-9\\-]*[A-Za-z0-9])$");
//    if(!ValidIP.exactMatch(Host))
//    {
//        if(!ValidHostName.exactMatch(Host))
//            errorMsg = "Host: неверное имя хоста "+Host +". Должен быть ip-адрес или корректное имя хоста.\n";
//    }

}

void Settings::writeSettings()
{
    QCoreApplication::setOrganizationName(OrganizationName);
    QCoreApplication::setOrganizationDomain(OrganizationDomain);
    QCoreApplication::setApplicationName(ApplicationName);
    QSettings settings(QCoreApplication::applicationDirPath()+
                       QDir::separator()+"set.ini", QSettings::IniFormat);

    settings.beginGroup("Zabbix");
    settings.setValue("ZabbixUser", ZabbixUser);
    settings.setValue("ZabbixPassword", ZabbixPassword);
    settings.setValue("ZabbixURL", ZabbixURL);
    settings.endGroup();

}

