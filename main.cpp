
#include <QtCore>
#include "util/BasicIOParser.h"


int
main(int argc, char ** argv)
{
	QCoreApplication app(argc, argv);
	if (app.arguments().size() != 1)
		qFatal("Usage: ./basicio_opt BASIC_IO_FILE");
	QFile fp(app.arguments().at(0));
	if (!fp.open(QIODevice::ReadOnly))
		qFatal("Cannot open file for reading");
	QString contents(fp.readAll());
	fp.close();
	BasicIOParser p(contents);
	return static_cast<int>(p.parse());
}

