#include "server.h"
#include <QTime>

QString jsonToStr(QJsonObject obj)
{
    QString str(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    return str;
}

Server::Server(QObject *parent) : QObject(parent), nextMessageSize(0)
{
    settings = Settings::Instance();
    settings->readSettings();
    zabbix = new QZabbix(settings->ZabbixUser,settings->ZabbixPassword,
                         settings->ZabbixURL);
    zabbix->login();

    server = new QTcpServer();
    connect(server,SIGNAL(newConnection()),
            this,SLOT(newConnection()));
    connect(this,SIGNAL(messageReceived(QJsonObject*,QTcpSocket*)),
            this,SLOT(parseMessage(QJsonObject*,QTcpSocket*)));

    if(!server->listen(QHostAddress::Any, 9999))
    {
        qCritical()<<"Server could not start: "<<server->errorString();
    }
    else
    {
        qInfo()<<"Server started!";
    }
}

Server::~Server()
{
    qInfo()<<"Server shutdown. Bye.";
}

QList<QTcpSocket*> Server::getClients()
{
    return clients;
}

void Server::newConnection()
{
    QTcpSocket* clientSocket = server->nextPendingConnection();
    qDebug()<<"Client connected["
           << clientSocket->socketDescriptor()
           << "]: "<<clientSocket->peerAddress().toString()
           << ":"<<clientSocket->peerPort();

    connect(clientSocket, SIGNAL(disconnected()),
            clientSocket,SLOT(deleteLater()));
    connect(clientSocket, SIGNAL(disconnected()),
            this,SLOT(gotDisconnection()));
    connect(clientSocket, SIGNAL(readyRead()),
            this, SLOT(readMessage()));

    clients<<clientSocket;

    //    sendMessage(clientSocket, "Reply: connection established");
}

void Server::readMessage()
{
    QTcpSocket* clientSocket = (QTcpSocket*)sender();
    QDataStream in(clientSocket);

    while(true)
    {
        if (!nextMessageSize)
        {
            if ((quint64)clientSocket->bytesAvailable() < sizeof(quint16)) { break; }
            in >> nextMessageSize;
        }

        if (clientSocket->bytesAvailable() < nextMessageSize) { break; }

        QByteArray compressedMessage;
        QByteArray uncompressedMessage;
        in>>compressedMessage;
        uncompressedMessage = qUncompress(compressedMessage);
        QJsonDocument jsonDoc= QJsonDocument::fromJson(uncompressedMessage);
        QJsonObject* jsonRequest = new QJsonObject(jsonDoc.object());

        emit messageReceived(jsonRequest, clientSocket);

        nextMessageSize = 0;

    }
}

void Server::gotDisconnection()
{
    QTcpSocket* clientSocket = (QTcpSocket*)sender();
    qDebug()<<"Client disconnected["
           << clientSocket->socketDescriptor()
           << "]: "<<clientSocket->peerAddress().toString()
           << ":"<<clientSocket->peerPort();
    clients.removeAt(clients.indexOf(clientSocket));
    emit disconnected();
}


quint64 Server::sendMessage(QTcpSocket *tcpSocket, QJsonObject *jsonReply)
{
    QByteArray sendBuff;
    QDataStream out(&sendBuff, QIODevice::WriteOnly);
    QJsonDocument jsonDoc(*jsonReply);
    QByteArray uncompressedMessage = jsonDoc.toJson(QJsonDocument::Compact);
    QByteArray compressedMessage = qCompress(uncompressedMessage,9);
    out << quint16(0) << compressedMessage;
    out.device()->seek(0);
    out << quint16(sendBuff.size() - sizeof(quint16));
    qDebug()<<"SEND: U:"<<uncompressedMessage.size()
           << "b C:" << compressedMessage.size()
           << "b R:" << (float) uncompressedMessage.size()/
              compressedMessage.size() << "%";
    return tcpSocket->write(sendBuff);
}

void Server::parseMessage(QJsonObject* jsonRequest, QTcpSocket* clientSocket)
{
    qDebug()<<"RECV["
           << clientSocket->socketDescriptor()
           << "]: "<<jsonToStr(*jsonRequest);
    QString requestUuid = jsonRequest->value("uuid").toString();
    QString requestType = jsonRequest->value("requestType").toString();

    if(requestType == "zabbixSingleRequest")
    {
           QJsonObject zr = jsonRequest->value("request").toObject();
           QJsonObject* params = new QJsonObject(zr["params"].toObject());
           QJsonObject* result = zabbix->zabbixRequest(zr["method"].toString()
                   .toStdString().c_str(), params);
           result->insert("uuid", requestUuid);
           sendMessage(clientSocket, result);
    }
    else
    {
        qDebug()<<"Unknown Request";
    }
}
