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

#ifndef STDIO_PARSER_H
#define STDIO_PARSER_H

#include "AbstractParser.h"

class StdioParser : public AbstractParser
{
public:
    StdioParser(const QByteArray &);
    
    bool parse();
    inline int getCommandNumber() const { return commandNumber; }
    inline int getErrorCode() const { return errorCode; }
    inline char getChunkType() const { return chunkType; }
    inline int getChunkSize() const { return chunkSize; }
    inline QByteArray getPayload() const { return payload; }
    
private:
    int getNumber();
    
    int commandNumber;
    int errorCode;
    char chunkType;
    int chunkSize;
    QByteArray payload;
};

#endif

