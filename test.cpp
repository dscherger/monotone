/***************************************************************************
 *   Copyright (C) 2006 by Thomas Keller                                   *
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

#include "test.h"

TestDlg::TestDlg() : QDialog(0)
{
    setupUi(this);

    mtn = new MonotoneThread("mtn", "~/Entwicklung/guitone.mtn", "~/Entwicklung/guitone");
    mtn->start();
    
    connect(
        doExec, SIGNAL(clicked()),
        this, SLOT(execute())
    );
    
    connect(
        mtn, SIGNAL(taskFinished(const MonotoneTask &)),
        this, SLOT(finished(const MonotoneTask &))
    );
    
    connect(
        mtn, SIGNAL(error(const QString &)),
        this, SLOT(mtnerror(const QString &))
    );
}

TestDlg::~TestDlg()
{
    mtn->terminate();
    mtn->wait();
    delete mtn;
}

void TestDlg::execute()
{
    QStringList in = input->text().split(" ");
    MonotoneTask task(in);
    mtn->enqueueTask(in);
}

void TestDlg::finished(const MonotoneTask & task)
{
    output->setText(task.getOutputUtf8());
}

void TestDlg::mtnerror(const QString & err)
{
    error->setText(err);
}

