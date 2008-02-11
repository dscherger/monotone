/***************************************************************************
 *   Copyright (C) 2006 by Thomas Keller                                   *
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

#ifndef BASICIO_PARSER_H
#define BASICIO_PARSER_H

#include "AbstractParser.h"
#include "vocab.h"

class BasicIOParser : public AbstractParser
{
public:
    BasicIOParser(const QString &);

    bool parse();
    inline StanzaList getStanzas() const { return stanzas; }

private:
    Stanza getStanza();
    QString getSymbol();
    QString getValue();
    QString getHash();

    StanzaList stanzas;
};

#endif

