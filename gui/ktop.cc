/*
    KTop, the KDE Task Manager and System Monitor
   
	Copyright (c) 1999, 2000 Chris Schlaeger <cs@kde.org>
    
    This program is free software; you can redistribute it and/or
    modify it under the terms of version 2 of the GNU General Public
    License as published by the Free Software Foundation.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

	KTop is currently maintained by Chris Schlaeger <cs@kde.org>. Please do
	not commit any changes without consulting me first. Thanks!

	Early versions of ktop (<1.0) have been written by Bernd Johannes Wuebben
    <wuebben@math.cornell.edu> and Nicolas Leclercq <nicknet@planete.net>.
	While I tried to preserve their original ideas, KTop has been rewritten
    several times.

	$Id$
*/

#include <assert.h>
#include <stdio.h>
#include <ctype.h>

#include <ktmainwindow.h>
#include <kconfig.h>
#include <klocale.h>
#include <kcmdlineargs.h>
#include <kmessagebox.h>
#include <kaboutdata.h>
#include <kstdaccel.h>
#include <kaction.h>
#include <kstdaction.h>

#include "SensorBrowser.h"
#include "SensorManager.h"
#include "SensorAgent.h"
#include "Workspace.h"
#include "HostConnector.h"
#include "../version.h"
#include "ktop.moc"

static const char* Description = I18N_NOOP("KDE Task Manager");

#define KTOP_MIN_W	500
#define KTOP_MIN_H	300

/*
 * This is the constructor for the main widget. It sets up the menu and the
 * TaskMan widget.
 */
TopLevel::TopLevel(const char *name)
	: KTMainWindow(name)
{
	splitter = new QSplitter(this, "Splitter");
	CHECK_PTR(splitter);
	splitter->setOrientation(Horizontal);
	splitter->setOpaqueResize(TRUE);
	QValueList<int> sizes;
	setView(splitter);

	sb = new SensorBrowser(splitter, SensorMgr, "SensorBrowser");
	CHECK_PTR(sb);

	ws = new Workspace(splitter, "Workspace");
	CHECK_PTR(ws);

	// start with a 30/70 ratio
	sizes.append(30);
	sizes.append(70);
	splitter->setSizes(sizes);

	/* Create the status bar. It displays some information about the
	 * number of processes and the memory consumption of the local
	 * host. */
	statusbar = new KStatusBar(this, "statusbar");
	CHECK_PTR(statusbar);
	statusbar->insertFixedItem(i18n("88888 Processes"), 0);
	statusbar->insertFixedItem(i18n("Memory: 8888888 kB used, "
							   "8888888 kB free"), 1);
	statusbar->insertFixedItem(i18n("Swap: 8888888 kB used, "
							   "8888888 kB free"), 2);
	setStatusBar(statusbar);

	SensorMgr->engage("localhost", "", "ktopd");
	/* Request info about the swapspace size and the units it is measured in.
	 * The requested info will be received by answerReceived(). */
	SensorMgr->sendRequest("localhost", "mem/swap?", (SensorClient*) this, 5);

	// call timerEvent to fill the status bar with real values
	timerEvent(0);

	/* Restore size of the dialog box that was used at end of last
	 * session.  Due to a bug in Qt we need to set the width to one
	 * more than the defined min width. If this is not done the widget
	 * is not drawn properly the first time. Subsequent redraws after
	 * resize are no problem.
	 *
	 * I need to implement a propper session management some day! */
	QString t = kapp->config()->readEntry(QString("G_Toplevel"));
	if(!t.isNull())
	{
		if (t.length() == 19)
		{ 
			int xpos, ypos, ww, wh;
			sscanf(t.data(), "%04d:%04d:%04d:%04d", &xpos, &ypos, &ww, &wh);
			setGeometry(xpos, ypos,
						ww <= KTOP_MIN_W ? KTOP_MIN_W + 1 : ww,
						wh <= KTOP_MIN_H ? KTOP_MIN_H : wh);
		}
	}

	setMinimumSize(sizeHint());
	resize(500, 300);

	timerID = startTimer(2000);

    // setup File menu
    KStdAction::openNew(ws, SLOT(newWorkSheet()), actionCollection());
    KStdAction::open(ws, SLOT(loadWorkSheet()), actionCollection());
	KStdAction::close(ws, SLOT(deleteWorkSheet()), actionCollection());
	KStdAction::quit(this, SLOT(quitApp()), actionCollection());

    (void) new KAction(i18n("C&onnect Host"), "connect_established", 0,
					   this, SLOT(connectHost()), actionCollection(),
					   "connect_host");
    (void) new KAction(i18n("D&isconnect Host"), "connect_no", 0, this,
					   SLOT(disconnectHost()), actionCollection(),
					   "disconnect_host");
	createGUI("ktop.rc");

	// show the dialog box
	show();
}

TopLevel::~TopLevel()
{
	killTimer(timerID);

	delete statusbar;
	delete splitter;
}

void
TopLevel::quitApp()
{
	saveProperties(kapp->config());
	kapp->config()->sync();
	qApp->quit();
}

void 
TopLevel::connectHost()
{
	QDialog* d = new HostConnector(0, "HostConnector");
	d->exec();

	delete d;
}

void 
TopLevel::disconnectHost()
{
	sb->disconnect();
}

void
TopLevel::timerEvent(QTimerEvent*)
{
	/* Request some info about the memory status. The requested information
	 * will be received by answerReceived(). */
	SensorMgr->sendRequest("localhost", "pscount", (SensorClient*) this, 0);
	SensorMgr->sendRequest("localhost", "mem/free", (SensorClient*) this, 1);
	SensorMgr->sendRequest("localhost", "mem/used", (SensorClient*) this, 2);
	SensorMgr->sendRequest("localhost", "mem/swap", (SensorClient*) this, 3);
}

void
TopLevel::readProperties(KConfig* /*cfg*/)
{
	debug("TopLevel::readProperties");
}

void
TopLevel::saveProperties(KConfig* /*cfg*/)
{
	debug("TopLevel::saveProperties");
}

void
TopLevel::answerReceived(int id, const QString& answer)
{
	QString s;
	static QString unit;
	static long mUsed = 0;
	static long mFree = 0;
	static long sTotal = 0;
	static long sFree = 0;

	switch (id)
	{
	case 0:
		s = i18n("%1 Processes").arg(answer);
		statusbar->changeItem(s, 0);
		break;
	case 1:
		mFree = answer.toLong();
		break;
	case 2:
		mUsed = answer.toLong();
		s = i18n("Memory: %1 %2 used, %3 %4 free")
			.arg(mUsed).arg(unit).arg(mFree).arg(unit);
		statusbar->changeItem(s, 1);
		break;
	case 3:
		sFree = answer.toLong();
		s = i18n("Swap: %1 %2 used, %3 %4 free")
			.arg(sTotal - sFree).arg(unit).arg(sFree).arg(unit);
		statusbar->changeItem(s, 2);
		break;
	case 5:
		SensorIntegerInfo info(answer);
		sTotal = info.getMax();
		unit = info.getUnit();
		break;
	}
}


static const KCmdLineOptions options[] =
{
	{ "p <show>", I18N_NOOP("What to Show (list|perf)"), "list" },
	{ 0, 0, 0}
};


/*
 * Once upon a time...
 */
int
main(int argc, char** argv)
{
	KAboutData aboutData("ktop", I18N_NOOP("KDE Task Manager"),
						 KTOP_VERSION, Description, KAboutData::License_GPL,
						 I18N_NOOP("(c) 1996-2000, The KTop Developers"));
	aboutData.addAuthor("Chris Schlaeger", "Current Maintainer",
						"cs@kde.org");
	aboutData.addAuthor("Nicolas Leclercq", 0, "nicknet@planete.net");
	aboutData.addAuthor("Alex Sanda", 0, "alex@darkstart.ping.at");
	aboutData.addAuthor("Bernd Johannes Wuebben", 0,
						"wuebben@math.cornell.edu");
	aboutData.addAuthor("Ralf Mueller", 0, "rlaf@bj-ig.de");
	
	KCmdLineArgs::init(argc, argv, &aboutData);
	KCmdLineArgs::addCmdLineOptions(options);
	
	// initialize KDE application
	KApplication a;

	SensorMgr = new SensorManager();
	CHECK_PTR(SensorMgr);

	// create top-level widget
	TopLevel *toplevel = new TopLevel("TaskManager");
	CHECK_PTR(toplevel);
	if (a.isRestored())
		toplevel->restore(1);
	else
	{
		a.setMainWidget(toplevel);
		a.setTopWidget(toplevel);
		toplevel->show();
	}

	// run the application
	int result = a.exec();

    delete toplevel;

	return (result);
}
