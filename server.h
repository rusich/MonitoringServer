#ifndef SERVER_H
#define SERVER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QDataStream>
#include <QList>
#include <QHash>
#include <QDebug>
#include <qzabbix.h>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QtSql>
#include "settings.h"
#include "errors.h"

class Server : public QObject
{
    Q_OBJECT
public:
    explicit Server(QObject *parent = nullptr);
    virtual ~Server();
    QList<QTcpSocket *> getClients();
private slots:
    virtual void newConnection();
    void readMessage();
    void gotDisconnection();
    quint64 sendMessage(QTcpSocket* clientSocket, QJsonObject* jsonReply);
    void parseMessage(QJsonObject* jsonRequest, QTcpSocket* clientSocket);
signals:
    void messageReceived(QJsonObject* jsonRequest, QTcpSocket* clientSocket);
    void disconnected();

private:
    void sendError(QTcpSocket* clientSocket, ErrorType errorCode, QJsonObject *jsonRequest = nullptr);
    QZabbix* zabbix;
    quint16 nextMessageSize;
    QList<QTcpSocket*> connectins;
//    QList<QTcpSocket*> authorizedClients;
    QHash<QTcpSocket*,QString> authorizedClients;
    QTcpServer *server;
    Settings* settings;
    QByteArray getGraphImage(QString graphid, QString period,
                             QString width, QString height);
    QJsonObject* getHost(QJsonObject& request, QString requestUuid, QTcpSocket* clientSocket = nullptr);
    QJsonObject* getGraph(QJsonObject& request, QString requestUuid);
    QJsonObject* getGroups(QJsonObject& request, QString requestUuid);
    QSqlDatabase sdb;
};

#endif // SERVER_H
