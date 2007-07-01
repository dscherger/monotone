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

#include "AbstractParser.h"

AbstractParser::AbstractParser(const QString & in) : input(in.toUtf8()) {}

AbstractParser::AbstractParser(const QByteArray & in) : input(in) {}

AbstractParser::~AbstractParser() {}

void AbstractParser::eatSpaces()
{
    while (whatsNext() == ' ') { advance(); }
}

char AbstractParser::whatsNext(int count /* = 0 */)
{
    if (count > (input.size() - 1)) return '\0';
    return input.at(count);
}

bool AbstractParser::advance(int count /* = 1 */)
{
    if (count > input.size()) return false;
    input.remove(0, count);
    return true;
}

char AbstractParser::getNext()
{
    char ch = input.at(0);
    advance();
    return ch;
}

