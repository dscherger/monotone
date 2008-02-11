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

#include "AbstractParser.h"

AbstractParser::AbstractParser(const QString & in)
{
    init(in.toUtf8());
}

AbstractParser::AbstractParser(const QByteArray & in)
{
    init(in);
}

void AbstractParser::init(const QByteArray & in)
{
    input = in;
    if (input.size() == 0)
        charPos = -1;
    else
        charPos = 0;
}

AbstractParser::~AbstractParser()
{
    input.clear();
}

void AbstractParser::eatSpaces()
{
    while (whatsNext(0) == ' ') { advance(1); }
}

char AbstractParser::whatsNext(int count /* = 0 */) const
{
    if (charPos == -1)
        return '\0';

    int nextPos = charPos + count;

    if (nextPos >= input.size())
        return '\0';

    return input.at(nextPos);
}

void AbstractParser::advance(int count /* = 1 */)
{
    if (charPos == -1)
        return;

    if (charPos + count >= input.size())
    {
        charPos = -1;
        return;
    }

    charPos += count;
}

char AbstractParser::getNext()
{
    const char ch = whatsNext(0);
    advance(1);
    return ch;
}

QByteArray AbstractParser::getNext(int count)
{
    if ((charPos + count) > (input.size() - 1))
    {
        QByteArray left = getLeftBytes();
        charPos = -1;
        return left;
    }

    QByteArray next = input.mid(charPos, count);
    advance(count);
    return next;
}

QByteArray AbstractParser::getLeftBytes() const
{
    if (charPos == -1) return QByteArray();
    QByteArray left = input.mid(charPos);
    return left;
}

int AbstractParser::getLeftBytesCount() const
{
    if (charPos == -1) return 0;
    return input.size() - charPos;
}

