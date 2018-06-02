#ifndef SERVER_H
#define SERVER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QDataStream>
#include <QList>
#include <QDebug>
#include <qzabbix.h>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include "settings.h"


class Server : public QObject
{
    Q_OBJECT
public:
    explicit Server(QObject *parent = nullptr);
    ~Server();
    QList<QTcpSocket *> getClients();
public slots:
    virtual void newConnection();
    void readMessage();
    void gotDisconnection();
    quint64 sendMessage(QTcpSocket* socket, QJsonObject* jsonReply);
private slots:
    void parseMessage(QJsonObject* jsonRequest, QTcpSocket* clientSocket);
signals:
    void messageReceived(QJsonObject* jsonRequest, QTcpSocket* clientSocket);
    void disconnected();

private:
    QZabbix* zabbix;
    quint16 nextMessageSize;
    QList<QTcpSocket*> clients;
    QTcpServer *server;
    Settings* settings;
    QByteArray getGraphImage(QString graphid, QString period,
                             QString width, QString height);
    QJsonObject* getHost(QJsonObject& request, QString requestUuid);
    QJsonObject* getGraph(QJsonObject& request, QString requestUuid);

};

#endif // SERVER_H
