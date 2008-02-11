/***************************************************************************
 *   Copyright (C) 2007 by Thomas Keller                                   *
 *   me@thomaskeller.biz                                                   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation, either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 ***************************************************************************/

#include "DebugLog.h"
#include "Settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <QDir>
#include <QDate>
#include <QTime>

DebugLog* DebugLog::instance = 0;

DebugLog* DebugLog::singleton()
{
    if (!instance)
    {
        instance = new DebugLog();
    }
    return instance;
}

DebugLog::DebugLog()
{
    consoleLogging = Settings::getConsoleLogEnabled();
    fileLogging = Settings::getFileLogEnabled();
    int level = Settings::getLogLevel();
    if (level >= Fatal && level <= Debug) logLevel = static_cast<Level>(level);

    if (fileLogging && !openLogfile())
    {
        critical(QString("Cannot open logfile '%1' for writing, "
                         "disable file logging.").arg(logFilePath()));
        fileLogging = false;
    }

    QString sep;
    QDate today = QDate::currentDate();
    sep.fill('=', 40);
    log(Info, sep);
    log(Info,
        QString(" guitone session started  (%1)")
            .arg(today.toString("yyyy-MM-dd"))
    );
    log(Info, sep);
}

DebugLog::~DebugLog()
{
    if (logFile.isOpen()) closeLogfile();
}

bool DebugLog::openLogfile()
{
    logFile.setFileName(logFilePath());

    QFlags<QIODevice::OpenModeFlag> flags =
        QIODevice::WriteOnly | QIODevice::Append |
        QIODevice::Text | QIODevice::Unbuffered;

    if (!logFile.open(flags)) return false;

    return true;
}

void DebugLog::log(Type t, QString msg)
{
    if (!fileLogging && !consoleLogging) return;

    if (logLevel == Fatal && t > Fatal) return;
    if (logLevel == Critical && t > Critical) return;
    if (logLevel == Warn && t > Warn) return;
    if (logLevel == Info && t > Info) return;
    if (logLevel == Debug && t > Debug) return;

    QMap<int,QString> errors;
    errors[0] = "fatal";
    errors[1] = "critical";
    errors[2] = "warning";
    errors[3] = "info";
    errors[4] = "debug";

    // do not use I here since this calls qFatal and results in
    // an endless loop
    assert(t <= errors.size());

    QTime now = QTime::currentTime();

    QString logStr;
    logStr.append(
        QString("%1: %2: %3\n")
            .arg(now.toString("hh:mm:ss.zzz"))
            .arg(errors[t-1])
            .arg(msg)
    );

    QByteArray utf8_msg = logStr.toUtf8();

    // print the message on console
    if (consoleLogging)
    {
        fprintf(stderr, utf8_msg.constData());
    }

    // print the message to the logfile
    if (fileLogging && logFile.isOpen())
    {
        logFile.write(utf8_msg);
    }
}

void DebugLog::closeLogfile()
{
    if (logFile.isOpen()) logFile.close();
}

#ifndef QT_NO_DEBUG
void DebugLog::debug(QString msg)
{
    singleton()->log(Debug, msg);
}
#else
void DebugLog::debug(QString msg) { Q_UNUSED(msg); }
#endif

void DebugLog::info(QString msg)
{
    singleton()->log(Info, msg);
}

void DebugLog::warn(QString msg)
{
    singleton()->log(Warn, msg);
}

void DebugLog::critical(QString msg)
{
    singleton()->log(Critical, msg);
}

void DebugLog::fatal(QString msg)
{
    singleton()->log(Fatal, msg);
}

void DebugLog::setConsoleLogEnabled(bool enabled)
{
    DebugLog * log = singleton();
    log->consoleLogging = enabled;
    Settings::setConsoleLogEnabled(enabled);
}

bool DebugLog::getConsoleLogEnabled()
{
    return singleton()->consoleLogging;
}

void DebugLog::setFileLogEnabled(bool enabled)
{
    DebugLog * log = singleton();
    if (log->fileLogging && !enabled)
    {
        log->closeLogfile();
    }
    else if (!log->fileLogging && enabled)
    {
        if (!log->openLogfile())
        {
            log->critical(QString("Cannot open logfile '%1' for writing, "
                                  "disable file logging.").arg(logFilePath()));
            enabled = false;
        }
    }
    log->fileLogging = enabled;
    Settings::setFileLogEnabled(enabled);
}

bool DebugLog::getFileLogEnabled()
{
    return singleton()->fileLogging;
}

void DebugLog::setLogLevel(int level)
{
    if (level < Fatal || level > Debug) return;
    singleton()->logLevel = static_cast<Level>(level);
    Settings::setLogLevel(level);
}

int DebugLog::getLogLevel()
{
    return static_cast<int>(singleton()->logLevel);
}

QString DebugLog::logFilePath()
{
    QString path(QDir::homePath());
    path.append(QDir::separator()).append("guitone.log");
    return path;
}

