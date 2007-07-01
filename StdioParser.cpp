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

#include "StdioParser.h"

StdioParser::StdioParser(const QByteArray & input) : AbstractParser(input) {}

bool StdioParser::parse()
{
    if (input.size() == 0) return false;
    
    // chunk format: <cmdNumber>:<errCode>:<chunkType>:<chunkSize>:<payload>
    commandNumber = getNumber();
    if (getNext() != ':') qFatal("%s: %d", __FILE__, __LINE__);
    errorCode = getNumber();
    if(getNext() != ':') qFatal("%s: %d", __FILE__, __LINE__);
    chunkType = getNext();
    if (!(chunkType == 'm' || chunkType == 'l'))  qFatal("%s: %d", __FILE__, __LINE__);
    if (getNext() != ':')  qFatal("%s: %d", __FILE__, __LINE__);
    chunkSize = getNumber();
    if (getNext() != ':')  qFatal("%s: %d", __FILE__, __LINE__);
    
    int charsLeft = input.size();
    if (chunkSize > charsLeft)
    {
        return false;
    }
    
    payload = input.mid(0, chunkSize);
    advance(chunkSize);
    
    return true;
}

int StdioParser::getNumber()
{
    int number = 0;
    int processedChars = 0;
    
    while (char ch = whatsNext())
    {
        if (ch < '0' || ch > '9')
        {
            // ensure that we've read at least one char
            if (processedChars == 0)  qFatal("%s: %d", __FILE__, __LINE__);
            break;
        }
        number *= 10;
        number += (ch - '0');
        advance();
        processedChars++;
    }
    
    return number;
}

