#include <QCoreApplication>
#include "server.h"
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QScopedPointer>
#include <QLoggingCategory>
#include <iostream>

QScopedPointer<QFile> logFile;


void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    // Открываем поток записи в файл
    QTextStream out(logFile.data());
    // Записываем дату записи
    out << QDateTime::currentDateTime().
           toString("yyyy-MM-dd hh:mm:ss.zzz ");
    std::cout<<QDateTime::currentDateTime().
               toString("yyyy-MM-dd hh:mm:ss.zzz ").toStdString();
    // По типу определяем, к какому уровню относится сообщение
    switch (type)
    {
    case QtInfoMsg:     out << "INF "; std::cout<< "INF "; break;
    case QtDebugMsg:    out << "DBG "; std::cout<< "DBG ";break;
    case QtWarningMsg:  out << "WRN "; std::cout<< "WRN ";break;
    case QtCriticalMsg: out << "CRT "; std::cout<< "CRT ";break;
    case QtFatalMsg:    out << "FTL "; std::cout<< "FLT ";break;
    }
    // Записываем в вывод категорию сообщения и само сообщение
    out << ": " << msg << endl;
    std::cout << ": " << msg.toStdString() << std::endl;
    out.flush();    // Очищаем буферизированные данные
}

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    logFile.reset(new QFile("monitoring_server.log"));
    logFile.data()->open(QFile::Append | QFile::Text);
    qInstallMessageHandler(messageHandler);
    Server srv(&a);
    return a.exec();
}
