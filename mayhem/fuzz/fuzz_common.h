// Shared setup for the GPXSee fuzz harnesses (mayhem/fuzz/fuzz_data.cpp, fuzz_map.cpp) and the
// mayhem/test/gpxsee_dump oracle tool: Qt + CRS initialisation as GPXSee's App does it.
//
// State kept across inputs, on purpose:
//  - the QApplication (offscreen platform) and the CRS tables GPXSee loads at startup
//    (App::loadDatums/loadPCSs in src/GUI/app.cpp) — read-only after init;
//  - the temp dir; its single input file is truncated and rewritten for every input. map-ozf also
//    keeps the fixed OziExplorer input.map there, written once at init and only read after.
// Reset per input: the maps are deleted and the process-wide TileCache is cleared at the end of
// exerciseMap (exercise.h), so no decoded tile carries over to the next input.
#ifndef GPXSEE_FUZZ_COMMON_H
#define GPXSEE_FUZZ_COMMON_H

#include <cstdint>
#include <cstdlib>
#include <cstddef>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>
#include <QThreadPool>
#include "common/programpaths.h"
#include "map/ellipsoid.h"
#include "map/gcs.h"
#include "map/conversion.h"
#include "map/pcs.h"

static QApplication *g_app;

static void quietHandler(QtMsgType type, const QMessageLogContext &, const QString &)
{
	// GPXSee logs every rejected input with qWarning(); keep the fuzzer output readable.
	// qFatal still aborts: Qt calls abort() after the handler returns for QtFatalMsg.
	(void)type;
}

static QString dataRoot(const char *argv0)
{
	// The binaries live in <repo>/mayhem/out/; GPXSee's shipped data (CRS tables, styles,
	// symbols) is <repo>/data, found by ProgramPaths through QStandardPaths::AppDataLocation
	// = <XDG_DATA_DIRS entry>/<applicationName>.
	QFileInfo exe(QString::fromLocal8Bit(argv0));
	QString self = QFile::symLinkTarget("/proc/self/exe");
	QDir dir(self.isEmpty() ? exe.absolutePath() : QFileInfo(self).absolutePath());
	dir.cdUp();
	dir.cdUp();
	return dir.absolutePath();
}

static void gpxseeInit(char **argv)
{
	QString root = dataRoot(argv[0]);
	setenv("QT_QPA_PLATFORM", "offscreen", 1);
	setenv("XDG_DATA_DIRS", QFile::encodeName(root).constData(), 1);
	setenv("XDG_DATA_HOME", "/nonexistent-gpxsee-fuzz", 1);
	qInstallMessageHandler(quietHandler);

	static int qargc = 1;
	static char qarg0[] = "gpxsee-fuzz";
	static char *qargv[] = {qarg0, nullptr};
	g_app = new QApplication(qargc, qargv);
	// AppDataLocation = <root>/data → <repo>/data/{CRS,style,symbols}.
	QCoreApplication::setApplicationName("data");
	QThreadPool::globalInstance()->setMaxThreadCount(1);
	Q_INIT_RESOURCE(common);

	// Same order as App::loadDatums()/App::loadPCSs().
	QString ell(ProgramPaths::ellipsoidsFile()), gcs(ProgramPaths::gcsFile());
	if (!ell.isNull() && !gcs.isNull()) {
		Ellipsoid::loadList(ell);
		GCS::loadList(gcs);
	}
	QString prj(ProgramPaths::projectionsFile()), pcs(ProgramPaths::pcsFile());
	if (!prj.isNull() && !pcs.isNull()) {
		Conversion::loadList(prj);
		PCS::loadList(pcs);
	}
	if (ell.isNull() || pcs.isNull()) {
		fprintf(stderr, "gpxsee-fuzz: CRS tables not found under %s/data/CRS\n",
		  qPrintable(root));
		abort();
	}

}

#endif // GPXSEE_FUZZ_COMMON_H
