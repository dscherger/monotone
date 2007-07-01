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

#ifndef MONOTONE_THREAD_H
#define MONOTONE_THREAD_H

#include <QThread>
#include <QProcess>
#include <QQueue>

class MonotoneTask
{
public:
    MonotoneTask();
    MonotoneTask(const QStringList &);
    MonotoneTask(const QStringList &, const QStringList &);
    MonotoneTask(const QList<QByteArray> &);
    MonotoneTask(const QList<QByteArray> &, const QList<QByteArray> &);
    
    QByteArray getEncodedInput() const;
    
    void setOutput(const QByteArray & out) { output = out; }
    void setReturnCode(int code) { returnCode = code; }
    
    QByteArray getOutput() const { return output; }
    QString getOutputUtf8() const { return QString::fromUtf8(output); }
    bool getReturnCode() const { return returnCode; }
    
private:
    void init(const QList<QByteArray> &, const QList<QByteArray> &);
    QList<QByteArray> stringToByteArrayList(const QStringList &);

    int returnCode;
    
    QList<QByteArray> arguments;
    QList<QByteArray> options;
    QByteArray output;
};

class MonotoneThread : public QThread
{
    Q_OBJECT

public:
    MonotoneThread(const QString &, const QString &, const QString & workspace = QString());
    ~MonotoneThread();

protected:
    void run();

public slots:
    void enqueueTask(const MonotoneTask &);
    void abort();

signals:
    void taskFinished(const MonotoneTask &);
    void taskAborted(const MonotoneTask &);
    void error(const QString &);

private:
    QString processErrorToString();
    void cleanup();
    
    static const int StdioBufferSize;
    bool doAbort;
    QProcess * process;
    QQueue<MonotoneTask> queue;
};

#endif
