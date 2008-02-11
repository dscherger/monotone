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

#include "Settings.h"
#include "vocab.h"

#include <QCoreApplication>

Settings * Settings::instance = 0;

Settings * Settings::singleton()
{
    if (!instance)
    {
        QCoreApplication::setOrganizationName("Thomas Keller");
        QCoreApplication::setOrganizationDomain("thomaskeller.biz");
        QCoreApplication::setApplicationName("guitone");

        instance = new Settings();
    }
    return instance;
}

Settings::Settings() : QSettings() {}

Settings::~Settings() {}

void Settings::setBool(const QString & name, bool value)
{
    I(!name.isEmpty());
    singleton()->setValue(name, value);
}

bool Settings::getBool(const QString & name, bool defaultVal)
{
    I(!name.isEmpty());
    return singleton()->value(name, defaultVal).toBool();
}

void Settings::setWindowGeometry(const QString & windowClass, const QByteArray & data)
{
    I(!windowClass.isEmpty());
    singleton()->setValue(windowClass, data);
}

QByteArray Settings::getWindowGeometry(const QString & windowClass)
{
    I(!windowClass.isEmpty());
    return singleton()->value(windowClass).toByteArray();
}

void Settings::sync()
{
    singleton()->QSettings::sync();
}

QString Settings::getMtnBinaryPath()
{
    return singleton()->value("MtnExePath", "mtn").toString();
}

void Settings::setMtnBinaryPath(QString path)
{
    singleton()->setValue("MtnExePath", path);
}

bool Settings::getConsoleLogEnabled()
{
    return singleton()->value("ConsoleLogEnabled", true).toBool();
}

void Settings::setConsoleLogEnabled(bool enabled)
{
    singleton()->setValue("ConsoleLogEnabled", enabled);
}

bool Settings::getFileLogEnabled()
{
    return singleton()->value("FileLogEnabled", false).toBool();
}

void Settings::setFileLogEnabled(bool enabled)
{
    singleton()->setValue("FileLogEnabled", enabled);
}

int Settings::getLogLevel()
{
    // 4 is debug, see DebugLog.h
    return singleton()->value("LogLevel", 4).toInt();
}

void Settings::setLogLevel(int verbosity)
{
    singleton()->setValue("LogLevel", verbosity);
}

void Settings::saveHeaderViewState(QHeaderView *view, QString name)
{
    I(!name.isEmpty());
    QStringList cols;
    for (int i=0, j=view->count(); i<j; i++)
    {
        // save col size and visual index separated by a single colon
        cols.append(QString::number(view->sectionSize(i)).
                    append(":").
                    append(QString::number(view->visualIndex(i)))
        );
    }
    Settings *settings = singleton();
    settings->setValue(name, cols.join(","));
}

void Settings::restoreHeaderViewState(QHeaderView *view, QString name)
{
    I(!name.isEmpty());
    QString colConfig(singleton()->value(name).toString());
    QStringList cols = colConfig.split(",", QString::SkipEmptyParts);

    int colCount = cols.size();
    if (colCount == 0) return;

    int curColCount = view->count();

    for (int i=0; i < colCount && i < curColCount; i++)
    {
        QStringList parts = cols.at(i).split(":", QString::SkipEmptyParts);

        I(parts.size() == 2);

        view->resizeSection(i, parts.at(0).toInt());
        view->moveSection(view->visualIndex(i), parts.at(1).toInt());
    }
}

QByteArray Settings::getSplitterState(QString name)
{
    I(!name.isEmpty());
    return singleton()->value(name).toByteArray();
}

void Settings::setSplitterState(const QByteArray & byteArray, QString name)
{
    I(!name.isEmpty());
    Settings *settings = singleton();
    settings->setValue(name, byteArray);
}

void Settings::setItemList(const QString & name, const QStringList & items)
{
    I(!name.isEmpty());
    Settings *settings = singleton();
    settings->setValue(name, items);
}

QStringList Settings::getItemList(const QString & name)
{
    I(!name.isEmpty());
    return singleton()->value(name).toStringList();
}

void Settings::addItemToList(const QString & name, const QString & item, int maxItems)
{
    QStringList list = getItemList(name);

    // move an already recorded item to the front
    int pos = list.indexOf(item);
    if (pos > -1)
    {
        list.move(pos, 0);
    }
    else
    {
        if (list.size() > maxItems)
        {
            list.removeLast();
        }
        list.prepend(item);
    }
    setItemList(name, list);
}

void Settings::removeItemFromList(const QString & name, const QString & item)
{
    QStringList list = getItemList(name);
    int pos = list.indexOf(item);
    if (pos == -1) return;
    list.removeAt(pos);
    setItemList(name, list);
}

