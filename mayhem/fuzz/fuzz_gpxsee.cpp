// libFuzzer harness for GPXSee's file formats. One binary per format, chosen at compile time:
//   -DSUFFIX="gpx"                track/route/waypoint file  → exerciseData(path, false)
//   -DSUFFIX="bin" -DTRY_UNKNOWN  unknown suffix: every data parser in turn, as GPXSee does
//   -DSUFFIX="img" -DMAP_FORMAT   offline map file            → exerciseMap(path)
//   -DSUFFIX="ozf2" -DOZF_FORMAT  OZF2/OZF3 image, opened through a fixed OziExplorer .map naming it
//                                 (written once, read-only after) → exerciseMap(<dir>/input.map)
// Both GPXSee entry points take a *path* and dispatch on its suffix, so each input is written to
// <private temp dir>/input.<SUFFIX> (truncated and rewritten per input) and that path is opened,
// exactly as the GUI opens a file the user picks. See exercise.h for what is called.
#include <QTemporaryDir>
#include "fuzz_common.h"
#include "exercise.h"

#ifndef SUFFIX
#error "SUFFIX must be defined (the file extension the parser dispatches on)"
#endif

static QTemporaryDir *g_tmp;
static QString g_input;
#ifdef OZF_FORMAT
static QString g_map;
#endif

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	(void)argc;
	gpxseeInit(*argv);
	g_tmp = new QTemporaryDir(QDir::tempPath() + "/gpxsee-fuzz-XXXXXX");
	if (!g_tmp->isValid()) {
		fprintf(stderr, "gpxsee-fuzz: cannot create a temp dir in %s\n",
		  qPrintable(QDir::tempPath()));
		abort();
	}
	g_input = g_tmp->filePath("input." SUFFIX);
#ifdef OZF_FORMAT
	g_map = writeOziMap(g_tmp->path(), "input." SUFFIX);
	if (g_map.isNull())
		abort();
#endif
	return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	QFile f(g_input);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
		abort();
	if (size && f.write(reinterpret_cast<const char *>(data), (qint64)size)
	  != (qint64)size)
		abort();
	f.close();

#if defined(OZF_FORMAT)
	exerciseMap(g_map);
#elif defined(MAP_FORMAT)
	exerciseMap(g_input);
#elif defined(TRY_UNKNOWN)
	exerciseData(g_input, true);
#else
	exerciseData(g_input, false);
#endif
	return 0;
}
