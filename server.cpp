#include "server.h"
#include <QTime>
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkReply>
#include <QProcess>
#include <QDir>
#include <QDateTime>

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
                         settings->ZabbixURL+"api_jsonrpc.php");
    zabbix->login();
    qDebug()<<zabbix->getAuthStr();

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
    if(clients.indexOf(tcpSocket)> -1 )
    {
        return tcpSocket->write(sendBuff);
    }

    return -1;
}

QJsonObject* Server::getHost(QJsonObject& request, QString requestUuid = "")
{
    QJsonObject* zabbixResult;
    QJsonObject hostRequest;
    QJsonObject search;
    QJsonArray hostOutput;
    hostOutput<<"hostid"<<"host"<<"name";
    search["host"] = request["host"];
    hostRequest["search"] = search;
    hostRequest["output"] = hostOutput;
    hostRequest["selectInterfaces"] = QJsonArray({"ip"});

    zabbixResult = zabbix->zabbixRequest( "host.get", &hostRequest);
    QJsonObject* reply = new QJsonObject();
    reply->insert("uuid", requestUuid);
    reply->insert("replyType", "hostData");
    QJsonObject host(zabbixResult->value("result").toArray().at(0).toObject());
    QJsonObject replyData;
    replyData["hostid"] = host["hostid"];
    replyData["host"] = host["host"];
    replyData["name"] = host["name"];
    replyData["ip"] = host["interfaces"].toArray().at(0).toObject()["ip"];

    QJsonObject itemsRequest;
    QJsonArray itemsOutput;
    // Какие эелменты из Item запрашивать
    itemsOutput<<"itemid"<<"key_"<<"name"<<"description"<<"error"<<"state"<<"prevvalue"
              <<"lastvalue"<<"params"<<"units"<<"lastclock"<<"delay"<<"status";
    itemsRequest["output"] = itemsOutput;

    QJsonObject itemsFilter;
    itemsFilter["hostid"] = replyData["hostid"];

    QJsonArray  getItems = request["items"].toArray();
    if(getItems.size()>0)
    {
        itemsFilter["key_"] = getItems;
    }

    itemsRequest["filter"] = itemsFilter;

    QJsonArray items = zabbix->zabbixRequest("item.get", &itemsRequest)->value("result").toArray();

    foreach (const QJsonValue &aItem, items) {
        QJsonObject item = aItem.toObject();
        QString key = item["key_"].toString();

        // Преобразование параметра name (подстановка значений вместо $)
        if(item["name"].toString().contains('$'))
        {
            QString keyTmp = item["key_"].toString();
            keyTmp = keyTmp.split("[")[1];
            keyTmp = keyTmp.split("]")[0];
            QStringList keyParams = keyTmp.split(",");
            QString itemName = item["name"].toString();
            int num = itemName.split("$")[1].split(" ")[0].toInt();
            itemName = itemName.replace(QString("$") +
                                        QString::number(num),
                                        keyParams[num>0?num-1:0]);
            item["name"] = QJsonValue(itemName);
        }
        replyData[key] = item;
    }

    QJsonObject triggersRequest;
    QJsonObject triggersFilter;
    triggersFilter["value"] = 1;
    triggersRequest["host"] = replyData["host"];
    triggersRequest["filter"] = triggersFilter;
    triggersRequest["countOutput"] = "true";
    zabbixResult = zabbix->zabbixRequest("trigger.get", &triggersRequest);
    int triggersCount = zabbixResult->value("result").toString().toInt();
    replyData["triggersCount"] = triggersCount;

    if(triggersCount>0)
    {
        triggersRequest.remove("countOutput");
        QJsonArray triggerOutput;
        triggerOutput<<"lastchange"<<"description"<<"comment"
                    <<"templateid";
        triggersRequest["output"] = triggerOutput;
        triggersRequest["expandDescription"] = true;
        triggersRequest["expandComment"] = true;
        zabbixResult = zabbix->zabbixRequest("trigger.get", &triggersRequest);
        replyData["triggers"] = zabbixResult->value("result").toArray();
    }


    reply->insert("data", replyData);
    return reply;
}

QJsonObject *Server::getGraph(QJsonObject &request, QString requestUuid)
{

    QJsonObject* zabbixResult;
    QJsonObject graphRequest;
    QJsonArray graphOutput;
    graphOutput<<"graphid"<<"name";
    graphRequest["graphids"] = request["graphid"];
    graphRequest["output"] = graphOutput;

    zabbixResult = zabbix->zabbixRequest( "graph.get", &graphRequest);
    QJsonObject* reply = new QJsonObject();
    reply->insert("uuid", requestUuid);
    reply->insert("replyType", "graphData");
    QJsonObject graph(zabbixResult->value("result").toArray().at(0).toObject());
    QJsonObject replyData;
    replyData["graphid"] = graph["graphid"];
    replyData["name"] = graph["name"];
    if(replyData["graphid"].toString().trimmed()!="")
    {
        QByteArray graphData =this->getGraphImage(replyData["graphid"].toString(),
                QString::number(request["period"].toInt()),
                QString::number(request["width"].toInt()),
                QString::number(request["height"].toInt()));
        replyData["data"] = QJsonValue(QString::fromUtf8(graphData.toBase64()));
    }
    uint timestamp = QDateTime::currentDateTime().toTime_t();
    replyData["clock"] = QJsonValue::fromVariant(timestamp);
    reply->insert("data",replyData);

    return reply;
}

void Server::parseMessage(QJsonObject* jsonRequest, QTcpSocket* clientSocket)
{
    qDebug()<<"RECV["
           << clientSocket->socketDescriptor()
           << "]: "<<jsonToStr(*jsonRequest);
    QString requestUuid = jsonRequest->value("uuid").toString();
    QString requestType = jsonRequest->value("requestType").toString();
    QJsonObject  request = jsonRequest->value("request").toObject();

    QJsonObject* reply = nullptr;

    if(requestType == "getHost")
    {
        reply = getHost(request, requestUuid);
    }
    else if(requestType == "getGraph")
    {
        reply = getGraph(request, requestUuid);
    }
    else
    {
        qDebug()<<"Unknown request:" <<requestUuid<<requestType<<request;
    }

    if(reply!=nullptr) sendMessage(clientSocket, reply);
}

//Использует внешний скрипт для аутентификации и получения графика с помощью WGET
QByteArray Server::getGraphImage(QString graphid, QString period,
                                 QString width, QString height)
{
    QString program = QCoreApplication::applicationDirPath()+
            QDir::separator()+"get_chart.sh";
    QStringList arguments;
    arguments << settings->ZabbixURL<< settings->ZabbixUser
              <<settings->ZabbixPassword<<graphid<<period<<width<<height;
    QProcess *myProcess = new QProcess();
    myProcess->start(program, arguments);
    myProcess->waitForFinished();
    QByteArray graph = myProcess->readAll();
    return graph;
}
