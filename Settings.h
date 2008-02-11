/***************************************************************************
 *   Copyright (C) 2006 by Ingo Maindorfer								   *
 *   ingo@liquidcooling.de												   *
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

#ifndef SETTINGS_H
#define SETTINGS_H

#include <QSettings>
#include <QSize>
#include <QHeaderView>

class Settings : public QSettings
{

public:
    static void setBool(const QString &, bool);
    static bool getBool(const QString &, bool);

    static void setWindowGeometry(const QString &, const QByteArray &);
    static QByteArray getWindowGeometry(const QString &);

    static QStringList getItemList(const QString &);
    static void setItemList(const QString &, const QStringList &);
    static void addItemToList(const QString&, const QString &, int);
    static void removeItemFromList(const QString &, const QString &);

    static QString getMtnBinaryPath();
    static void setMtnBinaryPath(QString);

    static bool getConsoleLogEnabled();
    static void setConsoleLogEnabled(bool);
    static bool getFileLogEnabled();
    static void setFileLogEnabled(bool);
    static int getLogLevel();
    static void setLogLevel(int);

    static void saveHeaderViewState(QHeaderView *, QString);
    static void restoreHeaderViewState(QHeaderView *, QString);

    static QByteArray getSplitterState(QString);
    static void setSplitterState(const QByteArray &, QString);

    static void sync();

private:
    Settings();
    ~Settings(void);
    static Settings* singleton();
    static Settings* instance;
};


#endif
