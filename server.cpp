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
    qDebug()<<"Constructor";
    QSqlDatabase sdb = QSqlDatabase::addDatabase("QSQLITE");
    sdb.setConnectOptions();
    sdb.setDatabaseName("db.sqlite");

    if (!sdb.open()) {
        qCritical() << sdb.lastError().text();
        qApp->exit(1);
    }
    settings = Settings::Instance();
    settings->readSettings();
    zabbix = new QZabbix(settings->ZabbixUser,settings->ZabbixPassword,
                         settings->ZabbixURL+"api_jsonrpc.php");
    zabbix->login();
    if(!zabbix->isLoggedOn())
    {
        qDebug()<<"Could not connect to Zabbix server. Exiting.";
        qApp->exit(2);
    }
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

    qDebug()<<"Constructor end";
}

Server::~Server()
{
    sdb.close();
    qInfo()<<"Server shutdown. Bye.";
}

QList<QTcpSocket*> Server::getClients()
{
    qDebug()<<"getClients";
    return connectins;
}

void Server::newConnection()
{
    qDebug()<<"newConnection";
    QTcpSocket* clientSocket = server->nextPendingConnection();
    qDebug()<<"Client connected["
           << clientSocket->socketDescriptor()
           << "]: "<<clientSocket->peerAddress().toString()
           << ":"<<clientSocket->peerPort();

    connect(clientSocket, SIGNAL(disconnected()),
            this,SLOT(gotDisconnection()));
    connect(clientSocket, SIGNAL(disconnected()),
            clientSocket,SLOT(deleteLater()));
    connect(clientSocket, SIGNAL(readyRead()),
            this, SLOT(readMessage()));

    connectins<<clientSocket;

    qDebug()<<"newConnection end";
    //    sendMessage(clientSocket, "Reply: connection established");
}

void Server::readMessage()
{
    qDebug()<<"readMessage";
    QTcpSocket* clientSocket = (QTcpSocket*)sender();
    QDataStream in(clientSocket);

    while(true)
    {
        if (!nextMessageSize)
        {
            if(connectins.indexOf(clientSocket)== -1) {
                //                qDebug()<<"ERROR: Client disconnected on readMessage";
                return;
            }

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
    qDebug()<<"readMessage end";
}

void Server::gotDisconnection()
{
    qDebug()<<"gotDisc";
    QTcpSocket* clientSocket = (QTcpSocket*)sender();
    qDebug()<<"Client disconnected["
           << clientSocket->socketDescriptor()
           << "]: "<<clientSocket->peerAddress().toString()
           << ":"<<clientSocket->peerPort();
    clientSocket->disconnect();
    clientSocket->disconnectFromHost();
    connectins.removeAt(connectins.indexOf(clientSocket));
    authorizedClients.remove(clientSocket);
    qDebug()<<"gotDisc end";
}


quint64 Server::sendMessage(QTcpSocket *clientSocket, QJsonObject *jsonReply)
{
    qDebug()<<"sm";
    qDebug()<<jsonReply->value("replyType");;
    QByteArray sendBuff;
    QDataStream out(&sendBuff, QIODevice::WriteOnly);
    QJsonDocument jsonDoc(*jsonReply);
    QByteArray uncompressedMessage = jsonDoc.toJson(QJsonDocument::Compact);
    QByteArray compressedMessage = qCompress(uncompressedMessage,9);
    out << quint16(0) << compressedMessage;
    out.device()->seek(0);
    out << quint16(sendBuff.size() - sizeof(quint16));

    if(connectins.indexOf(clientSocket)> -1 )
    {
        qDebug()<<"SEND: U:"<<uncompressedMessage.size()
               << "b C:" << compressedMessage.size()
               << "b R:" << (float) uncompressedMessage.size()/
                  compressedMessage.size() << "%";
        return clientSocket->write(sendBuff);
    }


    qDebug()<<"sm end";
    return 0;
}

QJsonObject* Server::getHost(QJsonObject& request, QString requestUuid = "", QTcpSocket *clientSocket)
{
    qDebug()<<"getHost";

    QSqlQuery q;
    QString qTxt = QString(
                "select count(*) from ACL inner join Users on ACL.UserId = "
                "Users.UserId inner join Hosts on ACL.HostID = Hosts.HostId "
                "where Users.Username = '%1' AND Hosts.Host = '%2'")
            .arg(authorizedClients.value(clientSocket))
            .arg(request["host"].toString()
            );
    q.exec(qTxt);

    qDebug()<<qTxt;
    QSqlRecord rec = q.record();
    int recCount=0;
    while (q.next()) {
        recCount++;
    }

    if(recCount==0) {
        qDebug()<<qTxt;
        sendError(clientSocket, ErrorType::HostAccessDenied, &request);
        return nullptr;
    }

    QJsonObject* zabbixResult;
    QJsonObject hostRequest;
    QJsonObject search;
    QJsonArray hostOutput;
    hostOutput<<"hostid"<<"host"<<"name";
    search["host"] = request["host"];
    hostRequest["search"] = search;
    hostRequest["output"] = hostOutput;
    hostRequest["selectInterfaces"] = QJsonArray({"ip"});
	qDebug()<<hostRequest;
    zabbixResult = zabbix->zabbixRequest( "host.get", &hostRequest);
	qDebug()<<jsonToStr(*zabbixResult);
    //if(zabbixResult->value("result").toObject().count()==0) {
     //   sendError(clientSocket, ErrorType::NoSuchHost, &request);
      //  return nullptr;
    //}

    QJsonObject* reply = new QJsonObject();
    reply->insert("uuid", requestUuid);
    reply->insert("replyType", "hostData");
    QJsonObject host(zabbixResult->value("result").toArray().at(0).toObject());
    QJsonObject replyData;
    replyData["hostid"] = host["hostid"];
    replyData["host"] = host["host"];
    replyData["name"] = host["name"];
    replyData["ip"] = host["interfaces"].toArray().at(0).toObject()["ip"];

    //Items
    QJsonArray itemsOutput;
    QJsonObject itemsRequest;
    QJsonObject itemsFilter;
    // Какие эелменты из Item запрашивать
    itemsOutput<<"itemid"<<"key_"<<"name"<<"description"<<"error"<<"state"<<"prevvalue"
              <<"lastvalue"<<"params"<<"units"<<"lastclock"<<"delay"<<"status";
    itemsFilter["hostid"] = replyData["hostid"];
    itemsRequest["output"] = itemsOutput;

    if(request.contains("summary")) {
        itemsFilter["key_"] = "icmpping";
        itemsRequest["filter"] = itemsFilter;
        QJsonObject icmpping = zabbix->zabbixRequest("item.get", &itemsRequest)->
                value("result").toArray().at(0).toObject();
        replyData["icmpping"] = icmpping;
        replyData["summary"] = "true";
    }
    else {

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

    if(triggersCount>0 && !request.contains("summary"))
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
    qDebug()<<"getHost end";
    return reply;
}

QJsonObject *Server::getGraph(QJsonObject &request, QString requestUuid)
{
    qDebug()<<"getGraph";
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

QJsonObject *Server::getGroups(QJsonObject &request, QString requestUuid)
{
    qDebug()<<"getGroups";
    QJsonObject* zabbixResult;
    QJsonObject groupsRequest;
    groupsRequest["real_hosts"] = "true"; // Возвращять группы в которх есть хосты

    zabbixResult = zabbix->zabbixRequest( "hostgroup.get", &groupsRequest);
    QJsonObject* reply = new QJsonObject();

    reply->insert("uuid", requestUuid);
    reply->insert("replyType", "groupsData");

    QJsonObject replyData;
    QJsonArray groups(zabbixResult->value("result").toArray());

    for(int i=0; i<groups.size();i++)
    {
        replyData[groups.at(i).toObject().value("name").toString()] =
                QJsonObject(groups.at(i).toObject());
    }

    QJsonObject triggersByGroup;
    QJsonObject triggersFilter;
    triggersFilter["value"] = 1;
    triggersByGroup["countOutput"] = "true";
    triggersByGroup["filter"] = triggersFilter;

    for(int i=0; i<groups.size();i++)
    {
        QString groupName = groups.at(i).toObject().value("name").toString();
        triggersByGroup["group"] = groupName;
        zabbixResult = zabbix->zabbixRequest("trigger.get", &triggersByGroup);
        QString triggersCount = zabbixResult->value("result").toString();
        QJsonObject tmp = replyData[groupName].toObject();
        tmp["triggersCount"] = triggersCount;

        QJsonObject hostsCountByGroupId;
        hostsCountByGroupId["countOutput"] = "true";
        hostsCountByGroupId["groupids"] = tmp["groupid"];
        zabbixResult = zabbix->zabbixRequest("host.get", &hostsCountByGroupId);
        QString hostsCount = zabbixResult->value("result").toString();
        tmp["hostsCount"] = hostsCount;

        replyData[groupName] = tmp;
    }

    reply->insert("data",replyData);
    qDebug()<<"getGroups end";
    return reply;
}

void Server::parseMessage(QJsonObject* jsonRequest, QTcpSocket* clientSocket)
{
    qDebug()<<"parseMessage";
    qDebug()<<"RECV["
           << clientSocket->socketDescriptor()
           << "]: "<<jsonToStr(*jsonRequest);
    QString requestUuid = jsonRequest->value("uuid").toString();
    QString requestType = jsonRequest->value("requestType").toString();
    if(!authorizedClients.contains(clientSocket) && requestType!="auth")
    {
        sendError(clientSocket, ErrorType::NotAuthorized);
        return;
    }

    QJsonObject  request = jsonRequest->value("request").toObject();
    QJsonObject* reply = nullptr;

    if(requestType == "getHost")
    {
        reply = getHost(request, requestUuid, clientSocket);
    }
    else if(requestType == "getGraph")
    {
        reply = getGraph(request, requestUuid);
    }
    else if(requestType == "getGroups")
    {
        reply = getGroups(request, requestUuid);
    }
    else if(requestType == "auth")
    {
        QSqlQuery q;
        QString qTxt = QString(
                    "SELECT * FROM Users WHERE Username='%1' AND Password = '%2'")
                .arg(request["username"].toString())
                .arg(request["password"].toString() );
        q.exec(qTxt);


        QString FullName;
        QSqlRecord rec = q.record();
        QString Username = q.value(rec.indexOf("Username")).toString();
        int recCount=0;
        while (q.next()) {
            recCount++;
            int id = q.value(rec.indexOf("UserId")).toInt();
            QString Password = q.value(rec.indexOf("Password")).toString();
            Username = q.value(rec.indexOf("Username")).toString();
            FullName = q.value(rec.indexOf("FullName")).toString();
            qDebug() << Username<<Password<<FullName;
        }

        if(recCount==0) {
            sendError(clientSocket, ErrorType::IncorrectUserOrPassword);
            return;
        }
        reply = new QJsonObject();
        reply->insert("replyType","authSuccess");
        QJsonObject replyData;
        replyData["FullName"] = FullName;
        reply->insert("data",replyData);
        authorizedClients.insert(clientSocket, Username);
        qDebug()<<authorizedClients[clientSocket];
    }
    else
    {
        sendError(clientSocket, ErrorType::UnknownRequest, jsonRequest);
        return;
    }

    if(reply!=nullptr)
    {
        sendMessage(clientSocket, reply);
    }
    qDebug()<<"parseMessage end";
}

void Server::sendError(QTcpSocket *clientSocket, ErrorType errorCode, QJsonObject* jsonRequest)
{
    QString requestType;
    QJsonObject  request;
    QJsonObject* reply = nullptr;
    QJsonObject replyData;
    if(jsonRequest!=nullptr) {

        requestType = jsonRequest->value("requestType").toString();
        request= jsonRequest->value("request").toObject();
    }
    switch (errorCode) {
    case ErrorType::UnknownRequest:
        qDebug()<<"Unknown request:" <<requestType<<request;
        reply = new QJsonObject();
        reply->insert("replyType","error");
        replyData["errorId"] = ErrorType::UnknownRequest;
        replyData["errorMsg"] = QString("Неизвестный тип запроса: "+requestType);
        reply->insert("data",replyData);
        break;
    case ErrorType::NotAuthorized:
        reply = new QJsonObject();
        reply->insert("replyType","error");
        replyData["errorId"] = ErrorType::NotAuthorized;
        replyData["errorMsg"] = QString("Клиент не авторизован на сервере");
        reply->insert("data",replyData);
        break;
    case ErrorType::IncorrectUserOrPassword:
        reply = new QJsonObject();
        reply->insert("replyType","error");
        replyData["errorId"] = ErrorType::IncorrectUserOrPassword;
        replyData["errorMsg"] = QString("Неверное имя пользователя или пароль");
        reply->insert("data",replyData);
        break;
    case ErrorType::NoSuchHost:
        reply = new QJsonObject();
        reply->insert("replyType","error");
        replyData["errorId"] = ErrorType::NoSuchHost;
        replyData["errorMsg"] = QString("Клиент запросил несуществующий хост: "+
                                        jsonRequest->value("host").toString());
        replyData["host"] =  jsonRequest->value("host").toString();
        reply->insert("data",replyData);
        break;
    case ErrorType::HostAccessDenied:
        reply = new QJsonObject();
        reply->insert("replyType","error");
        replyData["errorId"] = ErrorType::HostAccessDenied;
        replyData["errorMsg"] = QString("Отказано в доступе к хосту: "+
                                        jsonRequest->value("host").toString());
        replyData["host"] =  jsonRequest->value("host").toString();
        reply->insert("data",replyData);
        break;
    default:
        break;
    }
    if(reply!=nullptr)
    {
        sendMessage(clientSocket, reply);
    }
}


//Использует внешний скрипт для аутентификации и получения графика с помощью WGET
QByteArray Server::getGraphImage(QString graphid, QString period,
                                 QString width, QString height)
{
    qDebug()<<"getGraphImage";
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
