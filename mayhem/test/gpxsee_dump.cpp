// gpxsee_dump — the functional oracle tool for mayhem/test.sh. Opens each file the way the GUI
// (and the fuzz harnesses) do (mayhem/fuzz/exercise.h) and prints one summary line per file:
//   gpxsee_dump -d <file>...   track/route/waypoint files (Data::Data)
//   gpxsee_dump -u <file>...   the same with tryUnknown (every parser in turn)
//   gpxsee_dump -m <file>...   map files (MapList::loadMaps + load/zoomFit/draw)
//   gpxsee_dump -o <file>...   OZF2/OZF3 images, copied to a temp dir as input.ozf2 and opened
//                              through the fixed OziExplorer .map the map-ozf harness uses
#include <cstdio>
#include <cstring>
#include <QFileInfo>
#include <QTemporaryDir>
#include "../fuzz/fuzz_common.h"
#include "../fuzz/exercise.h"

int main(int argc, char *argv[])
{
	if (argc < 3 || argv[1][0] != '-') {
		fprintf(stderr, "usage: %s -d|-u|-m|-o <file>...\n", argv[0]);
		return 2;
	}
	gpxseeInit(argv);
	char mode = argv[1][1];
	for (int i = 2; i < argc; i++) {
		QString path(QString::fromLocal8Bit(argv[i]));
		QString res;
		if (mode == 'o') {
			QTemporaryDir tmp;
			QString img(tmp.filePath("input.ozf2"));
			QString map(tmp.isValid() ? writeOziMap(tmp.path(), "input.ozf2") : QString());
			res = (!map.isNull() && QFile::copy(path, img)) ? exerciseMap(map)
			  : QString("setup failed");
		} else if (mode == 'm')
			res = exerciseMap(path);
		else
			res = exerciseData(path, mode == 'u');
		printf("%s: %s\n", qPrintable(QFileInfo(path).fileName()), qPrintable(res));
	}
	return 0;
}
