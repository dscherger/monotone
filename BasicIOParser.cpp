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

#include "BasicIOParser.h"
#include "vocab.h"

BasicIOParser::BasicIOParser(const QString & input) : AbstractParser(input) {}

bool BasicIOParser::parse()
{
    while (getLeftBytesCount() > 0)
    {
        Stanza stanza = getStanza();
        stanzas.append(stanza);
        advance();
    }
    return true;
}

Stanza BasicIOParser::getStanza()
{
    Stanza stanza;

    while (true)
    {
        char next = whatsNext();
        if (next == '\n' || next == '\0') break;

        StanzaEntry entry;
        entry.sym = getSymbol();
        if (entry.sym.size() == 0)
        {
            W("Couldn't get symbol.");
        }
        QString hash(getHash());

        // was this a hash?
        if (!hash.isNull())
        {
            entry.hash = hash;
        }
        else
        {
            //grab all string values and put them into a list
            while (true)
            {
                // get value returns an empty string if there are no
                // more opening quotes
                QString value(getValue());
                if (value.isNull()) break;
                entry.vals.append(value);
            }
        }
        stanza.append(entry);

        eatSpaces();

        char cur = getNext();
        if (cur != '\n' && cur != '\0')
        {
            W(QString("Expected '\\n' or '\\0', got '%1'").arg(cur));
        }
    }

    return stanza;
}

QString BasicIOParser::getSymbol()
{
    eatSpaces();
    QByteArray payload;
    while (true)
    {
        char cur = whatsNext();
        if ((cur >= 'a' && cur <= 'z') || cur == '_')
        {
            payload.append(cur);
            advance();
            continue;
        }
        break;
    }

    // ascii conv is enough here
    return QString(payload);
}

QString BasicIOParser::getValue()
{
    eatSpaces();
    if (whatsNext() != '"') return QString();
    advance();

    QByteArray payload;
    char last = '\0';
    while (true)
    {
        char cur = getNext();
        I(cur != '\0');
        // string end?
        if (cur == '"' && last != '\\') break;
        last = cur;
        payload.append(cur);
    }

    QString ret = QString::fromUtf8(payload);
    ret.replace("\\\\", "\\");
    ret.replace("\\\"", "\"");
    return ret;
}

QString BasicIOParser::getHash()
{
    eatSpaces();
    if (whatsNext() != '[') return QString();
    advance();

    QString hash;
    while (true)
    {
        char ch = whatsNext();
        if (ch < '0' || (ch > '9' && ch < 'a') || ch > 'f')
        {
            break;
        }
        hash.append(ch);
        advance();
    }
    I(getNext() == ']');

    return hash;
}

