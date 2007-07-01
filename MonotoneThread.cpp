/***************************************************************************
 *   Copyright (C) 2007 by Thomas Keller                                   *
 *   me@thomaskeller.biz                                                   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include "MonotoneThread.h"
#include "StdioParser.h"

#include <QTextStream>
#include <QMetaType>

MonotoneTask::MonotoneTask()
{
    init(QList<QByteArray>(), QList<QByteArray>());
}

MonotoneTask::MonotoneTask(const QStringList & args)
{
    init(stringToByteArrayList(args), QList<QByteArray>());
}

MonotoneTask::MonotoneTask(const QStringList & args, const QStringList & opts)
{
    init(stringToByteArrayList(args), stringToByteArrayList(opts));
}

MonotoneTask::MonotoneTask(const QList<QByteArray> & args)
{
    init(args, QList<QByteArray>());
}

MonotoneTask::MonotoneTask(const QList<QByteArray> & args, const QList<QByteArray> & opts)
{
    init(args, opts);
}

void MonotoneTask::init(const QList<QByteArray> & args, const QList<QByteArray> & opts)
{
    arguments = args;
    options = opts;
    returnCode = -1;
}

QList<QByteArray> MonotoneTask::stringToByteArrayList(const QStringList & list)
{
    QList<QByteArray> byteArrayList;
    foreach (QString entry, list)
    {
        byteArrayList.append(entry.toUtf8());
    }
    return byteArrayList;
}

QByteArray MonotoneTask::getEncodedInput() const
{
    QByteArray commandLine;
    QTextStream streamCmdLine(&commandLine);
    
    if (options.size() > 0)
    {
        // currently mtn can only understand key => value option pairs
        if (options.size() % 2 != 0) qFatal("%s: %d", __FILE__, __LINE__);
        
        streamCmdLine << "o";
        for (int i=0, c=options.size(); i<c; i++)
        {
            QString fragment = options.at(i);
            streamCmdLine << fragment.size();
            streamCmdLine << ":";
            streamCmdLine << fragment;
        }
        // separate options from the command by a single whitespace
        streamCmdLine << "e ";
    }
    
    if (arguments.size() == 0)  qFatal("%s: %d", __FILE__, __LINE__);
    
    streamCmdLine << "l";
    for (int i=0, c=arguments.size(); i<c; i++)
    {
        QString fragment = arguments.at(i);
        streamCmdLine << fragment.size();
        streamCmdLine << ":";
        streamCmdLine << fragment;
    }
    streamCmdLine << "e";
    streamCmdLine.flush();
    
    return commandLine;
}

const int MonotoneThread::StdioBufferSize = 50 * 1024 * 1024;

MonotoneThread::MonotoneThread(const QString & mtn, const QString & database, const QString & workspace)
    : QThread(), doAbort(false)
{
    qRegisterMetaType<MonotoneTask>("MonotoneTask");
    
    process = new QProcess();
    
    QStringList args;
    args << "automate";
    args << "stdio";
    args << QString("--automate-stdio-size=%1").arg(StdioBufferSize);
    args << "--db" << database;
    
    if (!workspace.isEmpty())
    {
        process->setWorkingDirectory(workspace);
    }
    
    qDebug(QString("starting %1 %2").arg(mtn).arg(args.join(" ")).toLatin1());
    process->start(mtn, args);
}

MonotoneThread::~MonotoneThread()
{
    cleanup();
    delete process;
}

void MonotoneThread::enqueueTask(const MonotoneTask & task)
{
    queue.enqueue(task);
}

void MonotoneThread::run()
{
    if (!process->waitForStarted())
    {
        qDebug("not started");
        emit error(processErrorToString());
        cleanup();
        return;
    }
    
    QTextStream streamProcess(process);
    
    QByteArray buffer;
    QByteArray output;
    
    bool processingTask = false;
    
    while (!doAbort)
    {
        if (process->state() != QProcess::Running)
        {
            qDebug("not running");
            QString err;
            if (process->exitStatus() == QProcess::CrashExit)
            {
                err = processErrorToString();
            }
            
            // read anything we get from stderr
            err.append(QString::fromUtf8(process->readAllStandardError()));
            
            emit error(err);
            cleanup();
            return;
        }
        
        if (queue.size() == 0) continue;
        
        if (!processingTask)
        {
            qDebug("starting new task");
            MonotoneTask task = queue.head();
            streamProcess << task.getEncodedInput();
            streamProcess.flush();
            processingTask = true;
        }
        else
        {
            qDebug("continue on last task");
        }
        
        if (!process->waitForReadyRead(-1))
        {
            qDebug("timeout waiting for ready read");
            QString err(processErrorToString());
            err.append(QString::fromUtf8(process->readAllStandardError()));
            emit error(err);
            cleanup();
            return;
        }
        
        qDebug("got new data");
        
        // FIXME: what about stderr output here?
        buffer.append(process->readAllStandardOutput());
        StdioParser parser(buffer);
        
        // if the chunk is not yet complete, try again later
        if (!parser.parse())
        {
            qDebug("output incomplete / not parsable");
            continue;
        }
        
        buffer = parser.getLeftBytes();
        output.append(parser.getPayload());
        int returnCode = parser.getErrorCode();
        
        // FIXME: support for other chunk types here
        if (parser.getChunkType() == 'm')
        {
            qDebug("more data to come");
            continue;
        }

        MonotoneTask task = queue.dequeue();
        task.setOutput(output);
        task.setReturnCode(returnCode);
        processingTask = false;
        output.clear();
        
        emit taskFinished(task);
    }
    cleanup();
}

QString MonotoneThread::processErrorToString()
{
    QString desc;
    switch (process->error())
    {
        case QProcess::FailedToStart: desc = tr("failed to start");
        case QProcess::Crashed: desc = tr("crashed");
        case QProcess::Timedout: desc = tr("timed out");
        case QProcess::WriteError: desc = tr("write error");
        case QProcess::ReadError: desc = tr("read error");
        default: desc = tr("unknown or no error");
    }
    return desc;
}

void MonotoneThread::abort()
{
    doAbort = true;
}

void MonotoneThread::cleanup()
{
    if (queue.size() > 0)
    {
        foreach (MonotoneTask task, queue)
        {
            emit taskAborted(task);
        }
        queue.clear();
    }
    
    // close the pipes
    process->close();
    // send SIGTERM
    process->terminate();
    // block until the process has really been finished
    process->waitForFinished();
}

