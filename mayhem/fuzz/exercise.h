// What the harnesses (and the mayhem/test/gpxsee_dump oracle) do with one opened file — the same
// public calls the GPXSee GUI makes, returning a one-line summary of the result.
//
//  exerciseData: Data::Data(path, tryUnknown) (src/data/data.cpp) — the track/route/waypoint file
//    parsers, chosen by file suffix (all of them in turn for an unknown suffix with tryUnknown) —
//    then the post-processing the GUI graphs (paths, distance/time, speed and sensor graphs).
//  exerciseMap: MapList::loadMaps(path, projection) (src/map/maplist.cpp) — the offline map
//    formats, chosen by file suffix — then what MapView (src/GUI/mapview.cpp) does: load() with the
//    GUI's default projections, zoomFit() to the map's bounds and draw() of the centre of the map at
//    that zoom and a few zoom-in levels. Map::Block (MapView's flag for printing/exporting) makes the
//    tile decoding and rendering run synchronously, inside the call.
#ifndef GPXSEE_EXERCISE_H
#define GPXSEE_EXERCISE_H

#include <QFile>
#include <QImage>
#include <QPainter>
#include <QString>
#include "data/data.h"
#include "map/map.h"
#include "map/maplist.h"
#include "map/projection.h"
#include "map/pcs.h"
#include "map/gcs.h"
#include "map/tilecache.h"

#define DRAW_SIZE 256
#define ZOOM_STEPS 3

static QString exerciseData(const QString &path, bool tryUnknown)
{
	Data d(path, tryUnknown);
	if (!d.isValid())
		return QString("invalid");

	int points = 0;
	qreal dist = 0, time = 0, moving = 0;
	int graphs = 0;
	for (const Track &t : d.tracks()) {
		Path p(t.path());
		for (const PathSegment &s : p)
			points += s.size();
		dist += t.distance();
		time += t.time();
		moving += t.movingTime();
		graphs += t.speed().primary().size() + t.heartRate().size()
		  + t.temperature().size() + t.cadence().size() + t.power().size()
		  + t.ratio().size();
	}
	for (const Route &r : d.routes()) {
		Path p(r.path());
		for (const PathSegment &s : p)
			points += s.size();
		dist += r.distance();
	}
	QString names;
	for (const Waypoint &w : d.waypoints())
		names += w.name().left(16) + ",";
	int rings = 0;
	for (const Area &a : d.areas())
		rings += a.polygons().size();

	return QString("valid tracks=%1 routes=%2 waypoints=%3 areas=%4 rings=%5 points=%6"
	  " dist=%7 time=%8 moving=%9 graphs=%10 names=%11")
	  .arg(d.tracks().size()).arg(d.routes().size()).arg(d.waypoints().size())
	  .arg(d.areas().size()).arg(rings).arg(points).arg(dist, 0, 'f', 0)
	  .arg(time, 0, 'f', 0).arg(moving, 0, 'f', 0).arg(graphs).arg(names);
}

// Draws the centre of the map at the current zoom; returns the number of non-white pixels.
static int drawCentre(Map *map)
{
	QRectF b(map->bounds());
	if (!b.isValid() || !qIsFinite(b.left()) || !qIsFinite(b.top())
	  || !qIsFinite(b.width()) || !qIsFinite(b.height()))
		return -1;

	QRectF rect(b.center() - QPointF(DRAW_SIZE / 2, DRAW_SIZE / 2),
	  QSizeF(DRAW_SIZE, DRAW_SIZE));
	rect &= b;
	if (rect.isEmpty())
		return -1;

	QImage img(DRAW_SIZE, DRAW_SIZE, QImage::Format_ARGB32_Premultiplied);
	img.fill(Qt::white);
	{
		QPainter p(&img);
		p.translate(-rect.topLeft());
		map->draw(&p, rect, Map::Block);
	}

	int ink = 0;
	for (int y = 0; y < img.height(); y++) {
		const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
		for (int x = 0; x < img.width(); x++)
			if (line[x] != 0xffffffffu)
				ink++;
	}
	return ink;
}

static QString exerciseMap(const QString &path)
{
	Projection out(PCS::pcs(3857));
	Projection in(GCS::gcs(4326));
	TreeNode<Map*> maps(MapList::loadMaps(path, out));
	Map *map = maps.items().isEmpty() ? nullptr : maps.items().first();
	QString ret;

	if (map && map->isValid()) {
		map->load(in, out, 1.0, false, true, -1, -1);
		RectC ll(map->llBounds());
		map->zoomFit(QSize(DRAW_SIZE, DRAW_SIZE), ll);
		ret = QString("valid name=%1 ll=%2,%3,%4,%5 draw=")
		  .arg(map->name())
		  .arg(ll.left(), 0, 'f', 3).arg(ll.top(), 0, 'f', 3)
		  .arg(ll.right(), 0, 'f', 3).arg(ll.bottom(), 0, 'f', 3);
		ret += QString::number(drawCentre(map));
		for (int i = 0; i < ZOOM_STEPS; i++) {
			int z = map->zoom();
			if (map->zoomIn() == z)
				break;
			ret += "," + QString::number(drawCentre(map));
		}
		map->unload();
	} else
		ret = "invalid";

	qDeleteAll(maps.items());
	// TileCache (src/map/tilecache.h) is one process-wide cache keyed by the map object's address;
	// QCT, JNX, RMAP, AQM and GEMF maps fill it and never clear it. A later input's map can get the
	// same address, so drop every tile here: each input decodes its own tiles.
	TileCache::clear();
	return ret;
}

// OZF2/OZF3 images (src/map/ozf.cpp) are opened only through an OziExplorer .map calibration file
// that names them (OziMap::setImageInfo, src/map/ozimap.cpp; there is no "open an .ozf2" entry
// point). writeOziMap writes a fixed one — WGS 84, Latitude/Longitude, two calibration points
// spanning 14..15 E, 50..49 N over a 128x128 image — to <dir>/input.map naming <image> (a file in
// the same dir), and returns its path. map-ozf writes it once and rewrites <image> per input; the
// oracle (gpxsee_dump -o) does the same with a sample image, so both go through exerciseMap.
static QString writeOziMap(const QString &dir, const QString &image)
{
	QString path(dir + "/input.map");
	QByteArray ba("OziExplorer Map Data File Version 2.2\r\nozf\r\n");
	ba += image.toUtf8() + "\r\n";
	ba += "1 ,Map Code,\r\n"
	  "WGS 84,WGS 84,   0.0000,   0.0000,WGS 84\r\n"
	  "Reserved 1\r\nReserved 2\r\nMagnetic Variation,,,E\r\n"
	  "Map Projection,Latitude/Longitude,PolyCal,No,AutoCalOnly,No,BSBUseWPX,No\r\n"
	  "Point01,xy,    0,    0,in, deg,50,0.0000,N,14,0.0000,E, grid,   ,           ,           ,N\r\n"
	  "Point02,xy,  127,  127,in, deg,49,0.0000,N,15,0.0000,E, grid,   ,           ,           ,N\r\n";
	for (int i = 3; i <= 30; i++)
		ba += QString("Point%1,xy,     ,     ,in, deg,    ,        ,N,    ,        ,W, grid,"
		  "   ,           ,           ,N\r\n").arg(i, 2, 10, QChar('0')).toLatin1();
	ba += "Projection Setup,,,,,,,,,,\r\n"
	  "Map Feature = MF ; Map Comment = MC     These follow if they exist\r\n"
	  "MM0,Yes\r\nMMPNUM,4\r\nIWH,Map Image Width/Height,128,128\r\n";

	QFile f(path);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(ba) != ba.size())
		return QString();
	return path;
}

#endif // GPXSEE_EXERCISE_H
