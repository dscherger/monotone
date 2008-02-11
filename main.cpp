
#include <QtCore>
#include "BasicIOParser.h"

int
main(int argc, char ** argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().size() != 2)
        qFatal("Usage: ./basicio_opt BASIC_IO_FILE");
    QFile fp(app.arguments().at(1));
    if (!fp.open(QIODevice::ReadOnly))
        qFatal("Cannot open file for reading");
    QString contents(fp.readAll());
    fp.close();
    BasicIOParser p(contents);
/*
    StanzaList stanzas = p.getStanzas();
    foreach (Stanza st, stanzas)
    {
        foreach (StanzaEntry sten, st)
        {
            D(QString("/%1/ %2").arg(sten.sym).arg(sten.hash + sten.vals.join(",")));
        }
    }
*/
    return static_cast<int>(p.parse());
}

