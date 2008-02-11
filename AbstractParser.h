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

#ifndef ABSTRACT_PARSER_H
#define ABSTRACT_PARSER_H

#include <QByteArray>
#include <QString>

class AbstractParser
{
public:
        AbstractParser(const QByteArray &);
        AbstractParser(const QString &);
        virtual ~AbstractParser();
        virtual bool parse() = 0;
        QByteArray getLeftBytes() const;
        int getLeftBytesCount() const;

protected:
        char whatsNext(int count = 0) const;
        char getNext();
        QByteArray getNext(int);
        void eatSpaces();
        void advance(int count = 1);

private:
        void init(const QByteArray &);
        QByteArray input;
        int charPos;
};

#endif

