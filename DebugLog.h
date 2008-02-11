/***************************************************************************
 *   Copyright (C) 2007 by Thomas Keller								   *
 *   me@thomaskeller.biz												   *
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

#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include <QObject>
#include <QFile>

class DebugLog : public QObject
{
public:
    // higher levels include lower ones, i.e.
    // 'warn' also prints out 'critical' and 'fatal';
    // 'debug' prints out everything we have
    enum Level {Fatal = 1, Critical, Warn, Info, Debug };
    // as we have different Levels each level's name is also
    // the name for a specific log type
    typedef Level Type;

    static void setFileLogEnabled(bool);
    static void setConsoleLogEnabled(bool);
    static void setLogLevel(int);

    static bool getFileLogEnabled();
    static bool getConsoleLogEnabled();
    static int getLogLevel();

    static QString logFilePath();

    static void info(QString);
    static void debug(QString);
    inline static void debug(const char * msg) { return debug(QString::fromUtf8(msg)); }
    static void warn(QString);
    inline static void warn(const char * msg) { return warn(QString::fromUtf8(msg)); }
    static void critical(QString);
    inline static void critical(const char * msg) { return critical(QString::fromUtf8(msg)); }
    static void fatal(QString);
    inline static void fatal(const char * msg) { return fatal(QString::fromUtf8(msg)); }

private:

    DebugLog();
    ~DebugLog();
    bool openLogfile();
    void closeLogfile();
    void log(Type, QString);

    bool consoleLogging;
    bool fileLogging;
    Level logLevel;
    QFile logFile;

    static DebugLog * singleton();
    static DebugLog * instance;
};


#endif
