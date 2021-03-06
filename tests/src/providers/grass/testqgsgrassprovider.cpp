/***************************************************************************
     testqgsgrassprovider.cpp
     --------------------------------------
    Date                 : April 2015
    Copyright            : (C) 2015 by Radim Blazek
    Email                : radim dot blazek at gmail dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#include <cmath>

#include <QApplication>
#include <QDir>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTemporaryFile>
#include <QtTest/QtTest>

#include <qgsapplication.h>
#include <qgscoordinatereferencesystem.h>
#include <qgsgeometry.h>
#include <qgslinestringv2.h>
#include <qgspointv2.h>
#include <qgspolygonv2.h>
#include <qgsproviderregistry.h>
#include <qgsrasterbandstats.h>
#include <qgsrasterlayer.h>
#include <qgsvectordataprovider.h>
#include <qgsvectorlayer.h>

#include <qgsgrass.h>
#include <qgsgrassimport.h>
#include <qgsgrassprovider.h>

extern "C"
{
#ifndef _MSC_VER
#include <unistd.h>
#endif
#include <grass/version.h>
}

#define TINY_VALUE  std::numeric_limits<double>::epsilon() * 20

/** \ingroup UnitTests
 * This is a unit test for the QgsRasterLayer class.
 */
class TestQgsGrassProvider: public QObject
{
    Q_OBJECT
  private slots:
    void initTestCase();// will be called before the first testfunction is executed.
    void cleanupTestCase();// will be called after the last testfunction was executed.
    void init() {} // will be called before each testfunction is executed.
    void cleanup() {} // will be called after every testfunction.

    void fatalError();
    void locations();
    void mapsets();
    void maps();
    void vectorLayers();
    void region();
    void info();
    void rasterImport();
    void vectorImport();
    void edit();
  private:
    void reportRow( const QString& message );
    void reportHeader( const QString& message );
    // verify result and report result
    bool verify( bool ok );
    // compare expected and got string and set ok to false if not equal
    bool compare( const QString& expected, const QString& got, bool& ok );
    // lists are considered equal if contains the same values regardless order
    // set ok to false if not equal
    bool compare( const QStringList& expected, const QStringList& got, bool& ok );
    // compare with tolerance
    bool compare( double expected, double got, bool& ok );
    bool copyRecursively( const QString &srcFilePath, const QString &tgtFilePath, QString *error );
    bool removeRecursively( const QString &filePath, QString *error = 0 );
    bool copyLocation( QString& tmpGisdbase );
    bool createTmpLocation( QString& tmpGisdbase, QString& tmpLocation, QString& tmpMapset );
    bool equal( QgsFeature feature, QgsFeature expectedFeatures );
    bool compare( QList<QgsFeature> features, QList<QgsFeature> expectedFeatures, bool& ok );
    bool compare( QString uri, QgsGrassObject mapObject, QgsVectorLayer *expectedLayer, bool& ok );
    QList<QgsFeature> getFeatures( QgsVectorLayer *layer );
    QString mGisdbase;
    QString mLocation;
    QString mReport;
    QString mBuildMapset;
};

#define GVERIFY(x) QVERIFY( verify(x) )

void TestQgsGrassProvider::reportRow( const QString& message )
{
  mReport += message + "<br>\n";
}
void TestQgsGrassProvider::reportHeader( const QString& message )
{
  mReport += "<h2>" + message + "</h2>\n";
}

//runs before all tests
void TestQgsGrassProvider::initTestCase()
{
  // init QGIS's paths - true means that all path will be inited from prefix
  QgsApplication::init();
  // QgsApplication::initQgis() calls QgsProviderRegistry::instance() which registers providers.
  // Because providers are linked in build directory with rpath, it would also try to load GRASS providers
  // in version different form which we are testing here and it would also load GRASS libs in different version
  // and result in segfault when __do_global_dtors_aux() is called.
  // => we must set QGIS_PROVIDER_FILE before QgsApplication::initQgis() to avoid loading GRASS provider in different version
  QgsGrass::putEnv( "QGIS_PROVIDER_FILE", QString( "gdal|ogr|memoryprovider|grassprovider%1" ).arg( GRASS_BUILD_VERSION ) );
  QgsApplication::initQgis();
  QString mySettings = QgsApplication::showSettings();
  mySettings = mySettings.replace( "\n", "<br />\n" );
  mReport += QString( "<h1>GRASS %1 provider tests</h1>\n" ).arg( GRASS_BUILD_VERSION );
  mReport += "<p>" + mySettings + "</p>\n";

#ifndef Q_OS_WIN
  reportRow( "LD_LIBRARY_PATH: " + QString( getenv( "LD_LIBRARY_PATH" ) ) );
#else
  reportRow( "PATH: " + QString( getenv( "PATH" ) ) );
#endif

  QgsGrass::setMute();
  QgsGrass::init();

  //create some objects that will be used in all tests...
  //create a raster layer that will be used in all tests...
  mGisdbase = QString( TEST_DATA_DIR ) + "/grass";
  mLocation = "wgs84";
  mBuildMapset = QString( "test%1" ).arg( GRASS_BUILD_VERSION );
  reportRow( "mGisdbase: " + mGisdbase );
  reportRow( "mLocation: " + mLocation );
  reportRow( "mBuildMapset: " + mBuildMapset );
  qDebug() << "mGisdbase = " << mGisdbase << " mLocation = " << mLocation;
}

//runs after all tests
void TestQgsGrassProvider::cleanupTestCase()
{
  QString myReportFile = QDir::tempPath() + "/qgistest.html";
  QFile myFile( myReportFile );
  if ( myFile.open( QIODevice::WriteOnly | QIODevice::Append ) )
  {
    QTextStream myQTextStream( &myFile );
    myQTextStream << mReport;
    myFile.close();
  }

  //QgsApplication::exitQgis();
}

bool TestQgsGrassProvider::verify( bool ok )
{
  reportRow( "" );
  reportRow( QString( "Test result: " ) + ( ok ? "ok" : "error" ) );
  return ok;
}

bool TestQgsGrassProvider::compare( const QString& expected, const QString& got, bool &ok )
{
  if ( expected != got )
  {
    ok = false;
    return false;
  }
  return true;
}

bool TestQgsGrassProvider::compare( const QStringList& expected, const QStringList& got, bool &ok )
{
  QStringList e = expected;
  QStringList g = got;
  e.sort();
  g.sort();
  if ( e != g )
  {
    ok = false;
    return false;
  }
  return true;
}

bool TestQgsGrassProvider::compare( double expected, double got, bool& ok )
{
  if ( qAbs( got - expected ) > TINY_VALUE )
  {
    ok = false;
    return false;
  }
  return true;
}

// G_fatal_error() handling
void TestQgsGrassProvider::fatalError()
{
  reportHeader( "TestQgsGrassProvider::fatalError" );
  bool ok = true;
  QString errorMessage = "test fatal error";
  G_TRY
  {
    G_fatal_error( "%s", errorMessage.toAscii().data() );
    ok = false; // should not be reached
    reportRow( "G_fatal_error() did not throw exception" );
  }
  G_CATCH( QgsGrass::Exception &e )
  {
    reportRow( QString( "Exception thrown and caught correctly" ) );
    reportRow( "expected error message: " + errorMessage );
    reportRow( "got error message: " + QString( e.what() ) );
    compare( errorMessage, e.what(), ok );
  }
  compare( errorMessage, QgsGrass::errorMessage(), ok );
  GVERIFY( ok );
}

void TestQgsGrassProvider::locations()
{
  reportHeader( "TestQgsGrassProvider::locations" );
  bool ok = true;
  QStringList expectedLocations;
  expectedLocations << "wgs84";
  QStringList locations = QgsGrass::locations( mGisdbase );
  reportRow( "expectedLocations: " + expectedLocations.join( ", " ) );
  reportRow( "locations: " + locations.join( ", " ) );
  compare( expectedLocations, locations, ok );
  GVERIFY( ok );
}

void TestQgsGrassProvider::mapsets()
{
  reportHeader( "TestQgsGrassProvider::mapsets" );
  bool ok = true;

  // User must be owner of mapset if it has to be opened (locked)
  // -> make copy because the source may have different user
  QString tmpGisdbase;
  if ( !copyLocation( tmpGisdbase ) )
  {
    reportRow( "cannot copy location" );
    GVERIFY( false );
    return;
  }

  QStringList expectedMapsets;
  expectedMapsets << "PERMANENT" << "test" << "test6" << "test7";
  QStringList mapsets = QgsGrass::mapsets( tmpGisdbase,  mLocation );
  reportRow( "expectedMapsets: " + expectedMapsets.join( ", " ) );
  reportRow( "mapsets: " + mapsets.join( ", " ) );
  compare( expectedMapsets, mapsets, ok );
  QgsGrass::setLocation( tmpGisdbase,  mLocation ); // for G_is_mapset_in_search_path
  // Disabled because adding of all mapsets to search path was disabled in setLocation()
#if 0
  foreach ( QString expectedMapset, expectedMapsets )
  {
    if ( G_is_mapset_in_search_path( expectedMapset.toAscii().data() ) != 1 )
    {
      reportRow( "mapset " + expectedMapset + " not in search path" );
      ok = false;
    }
  }
#endif

  // open/close mapset try twice to be sure that lock was not left etc.
  for ( int i = 1; i < 3; i++ )
  {
    reportRow( "" );
    reportRow( "Open/close mapset " + mBuildMapset + " for the " + QString::number( i ) + ". time" );
    QString error = QgsGrass::openMapset( tmpGisdbase, mLocation, mBuildMapset );
    if ( !error.isEmpty() )
    {
      reportRow( "QgsGrass::openMapset() failed: " + error );
      ok = false;
    }
    else
    {
      reportRow( "mapset successfully opened" );
      if ( !QgsGrass::activeMode() )
      {
        reportRow( "QgsGrass::activeMode() returns false after openMapset()" );
        ok = false;
      }

      error = QgsGrass::closeMapset();

      if ( !error.isEmpty() )
      {
        reportRow( "QgsGrass::close() failed: " + error );
        ok = false;
      }
      else
      {
        reportRow( "mapset successfully closed" );
      }

      if ( QgsGrass::activeMode() )
      {
        reportRow( "QgsGrass::activeMode() returns true after closeMapset()" );
        ok = false;
      }
    }
  }
  removeRecursively( tmpGisdbase );
  GVERIFY( ok );
}

void TestQgsGrassProvider::maps()
{
  reportHeader( "TestQgsGrassProvider::maps" );
  bool ok = true;
  QStringList expectedVectors;
  expectedVectors << "test";
  QStringList vectors = QgsGrass::vectors( mGisdbase,  mLocation, mBuildMapset );
  reportRow( "expectedVectors: " + expectedVectors.join( ", " ) );
  reportRow( "vectors: " + vectors.join( ", " ) );
  compare( expectedVectors, vectors, ok );

  reportRow( "" );
  QStringList expectedRasters;
  expectedRasters << "cell" << "dcell" << "fcell";
  QStringList rasters = QgsGrass::rasters( mGisdbase,  mLocation, "test" );
  reportRow( "expectedRasters: " + expectedRasters.join( ", " ) );
  reportRow( "rasters: " + rasters.join( ", " ) );
  compare( expectedRasters, rasters, ok );
  GVERIFY( ok );
}

void TestQgsGrassProvider::vectorLayers()
{
  reportHeader( "TestQgsGrassProvider::vectorLayers" );
  QString mapset = mBuildMapset;
  QString mapName = "test";
  QStringList expectedLayers;
  expectedLayers << "1_point" << "2_line" << "3_polygon";

  reportRow( "mapset: " + mapset );
  reportRow( "mapName: " + mapName );
  reportRow( "expectedLayers: " + expectedLayers.join( ", " ) );

  bool ok = true;
  G_TRY
  {
    QStringList layers = QgsGrass::vectorLayers( mGisdbase, mLocation, mapset, mapName );
    reportRow( "layers: " + layers.join( ", " ) );
    compare( expectedLayers, layers, ok );
  }
  G_CATCH( QgsGrass::Exception &e )
  {
    ok = false;
    reportRow( QString( "ERROR: %1" ).arg( e.what() ) );
  }
  GVERIFY( ok );
}

void TestQgsGrassProvider::region()
{
  reportHeader( "TestQgsGrassProvider::region" );
  struct Cell_head window;
  struct Cell_head windowCopy;
  bool ok = true;
  try
  {
    QgsGrass::region( mGisdbase, mLocation, "PERMANENT", &window );
  }
  catch ( QgsGrass::Exception &e )
  {
    Q_UNUSED( e );
    reportRow( "QgsGrass::region() failed" );
    ok = false;
  }

  if ( ok )
  {
    QString expectedRegion = "proj:3;zone:0;north:90N;south:90S;east:180E;west:180W;cols:1000;rows:500;e-w resol:0:21:36;n-s resol:0:21:36;";
    QString region = QgsGrass::regionString( &window );
    reportRow( "expectedRegion: " + expectedRegion );
    reportRow( "region: " + region );
    compare( expectedRegion, region, ok );
    windowCopy.proj = window.proj;
    windowCopy.zone = window.zone;
    windowCopy.rows = window.rows;
    windowCopy.cols = window.cols;
    QgsGrass::copyRegionExtent( &window, &windowCopy );
    QgsGrass::copyRegionResolution( &window, &windowCopy );
    QString regionCopy = QgsGrass::regionString( &windowCopy );
    reportRow( "regionCopy: " + regionCopy );
    compare( expectedRegion, regionCopy, ok );
  }
  GVERIFY( ok );
}

void TestQgsGrassProvider::info()
{
  // info() -> getInfo() -> runModule() -> startModule()
  reportHeader( "TestQgsGrassProvider::info" );
  bool ok = true;

  // GRASS modules must be run in a mapset owned by user, the source code may have different user.
  QString tmpGisdbase;
  if ( !copyLocation( tmpGisdbase ) )
  {
    reportRow( "cannot copy location" );
    GVERIFY( false );
    return;
  }

  QgsRectangle expectedExtent( -5, -5, 5, 5 );
  QMap<QString, QgsRasterBandStats> expectedStats;
  QgsRasterBandStats es;
  es.minimumValue = -20;
  es.maximumValue = 20;
  expectedStats.insert( "cell", es );
  es.minimumValue = -20.25;
  es.maximumValue = 20.25;
  expectedStats.insert( "dcell", es );
  es.minimumValue = -20.25;
  es.maximumValue = 20.25;
  expectedStats.insert( "fcell", es );
  foreach ( QString map, expectedStats.keys() )
  {
    es = expectedStats.value( map );
    // TODO: QgsGrass::info() may open dialog window on error which blocks tests
    QString error;
    QHash<QString, QString> info = QgsGrass::info( tmpGisdbase, mLocation, "test", map, QgsGrassObject::Raster, "stats",
                                   expectedExtent, 10, 10, 5000, error );
    if ( !error.isEmpty() )
    {
      ok = false;
      reportRow( "error: " + error );
      continue;
    }

    reportRow( "map: " + map );
    QgsRasterBandStats s;
    s.minimumValue = info["MIN"].toDouble();
    s.maximumValue = info["MAX"].toDouble();

    reportRow( QString( "expectedStats: min = %1 max = %2" ).arg( es.minimumValue ).arg( es.maximumValue ) ) ;
    reportRow( QString( "stats: min = %1 max = %2" ).arg( s.minimumValue ).arg( s.maximumValue ) ) ;
    compare( es.minimumValue, s.minimumValue, ok );
    compare( es.maximumValue, s.maximumValue, ok );

    QgsRectangle extent = QgsGrass::extent( tmpGisdbase, mLocation, "test", map, QgsGrassObject::Raster, error );
    reportRow( "expectedExtent: " + expectedExtent.toString() );
    reportRow( "extent: " + extent.toString() );
    if ( !error.isEmpty() )
    {
      ok = false;
      reportRow( "error: " + error );
    }
    if ( extent != expectedExtent )
    {
      ok = false;
    }
  }

  reportRow( "" );
  QgsCoordinateReferenceSystem expectedCrs;
  expectedCrs.createFromOgcWmsCrs( "EPSG:4326" );

  reportRow( "expectedCrs: " + expectedCrs.toWkt() );
  QString error;
  QgsCoordinateReferenceSystem crs = QgsGrass::crs( tmpGisdbase, mLocation, error );
  if ( !error.isEmpty() )
  {
    ok = false;
    reportRow( "crs: cannot read crs: " + error );
  }
  else
  {
    if ( !crs.isValid() )
    {
      reportRow( "crs: cannot read crs: " + QgsGrass::errorMessage() );
      ok = false;
    }
    else
    {
      reportRow( "crs: " + crs.toWkt() );
      if ( crs != expectedCrs )
      {
        ok = false;
      }
    }
  }
  removeRecursively( tmpGisdbase );
  GVERIFY( ok );
}


// From Qt creator
bool TestQgsGrassProvider::copyRecursively( const QString &srcFilePath, const QString &tgtFilePath, QString *error )
{
  QFileInfo srcFileInfo( srcFilePath );
  if ( srcFileInfo.isDir() )
  {
    QDir targetDir( tgtFilePath );
    targetDir.cdUp();
    if ( !targetDir.mkdir( QFileInfo( tgtFilePath ).fileName() ) )
    {
      if ( error )
      {
        *error = QCoreApplication::translate( "Utils::FileUtils", "Failed to create directory '%1'." )
                 .arg( QDir::toNativeSeparators( tgtFilePath ) );
        return false;
      }
    }
    QDir sourceDir( srcFilePath );
    QStringList fileNames = sourceDir.entryList( QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System );
    foreach ( const QString &fileName, fileNames )
    {
      const QString newSrcFilePath
      = srcFilePath + QLatin1Char( '/' ) + fileName;
      const QString newTgtFilePath
      = tgtFilePath + QLatin1Char( '/' ) + fileName;
      if ( !copyRecursively( newSrcFilePath, newTgtFilePath, error ) )
        return false;
    }
  }
  else
  {
    if ( !QFile::copy( srcFilePath, tgtFilePath ) )
    {
      if ( error )
      {
        *error = QCoreApplication::translate( "Utils::FileUtils", "Could not copy file '%1' to '%2'." )
                 .arg( QDir::toNativeSeparators( srcFilePath ),
                       QDir::toNativeSeparators( tgtFilePath ) );
      }
      return false;
    }
  }
  return true;
}

// From Qt creator
bool TestQgsGrassProvider::removeRecursively( const QString &filePath, QString *error )
{
  QFileInfo fileInfo( filePath );
  if ( !fileInfo.exists() )
    return true;
  QFile::setPermissions( filePath, fileInfo.permissions() | QFile::WriteUser );
  if ( fileInfo.isDir() )
  {
    QDir dir( filePath );
    QStringList fileNames = dir.entryList( QDir::Files | QDir::Hidden
                                           | QDir::System | QDir::Dirs | QDir::NoDotAndDotDot );
    foreach ( const QString &fileName, fileNames )
    {
      if ( !removeRecursively( filePath + QLatin1Char( '/' ) + fileName, error ) )
        return false;
    }
    dir.cdUp();
    if ( !dir.rmdir( fileInfo.fileName() ) )
    {
      if ( error )
      {
        *error = QCoreApplication::translate( "Utils::FileUtils", "Failed to remove directory '%1'." )
                 .arg( QDir::toNativeSeparators( filePath ) );
      }
      return false;
    }
  }
  else
  {
    if ( !QFile::remove( filePath ) )
    {
      if ( error )
      {
        *error = QCoreApplication::translate( "Utils::FileUtils", "Failed to remove file '%1'." )
                 .arg( QDir::toNativeSeparators( filePath ) );
      }
      return false;
    }
  }
  return true;
}

// copy test location to temporary
bool TestQgsGrassProvider::copyLocation( QString& tmpGisdbase )
{
  // use QTemporaryFile to generate name (QTemporaryDir since 5.0)
  QTemporaryFile* tmpFile = new QTemporaryFile( QDir::tempPath() + "/qgis-grass-test" );
  tmpFile->open();
  tmpGisdbase = tmpFile->fileName();
  delete tmpFile;
  reportRow( "tmpGisdbase: " + tmpGisdbase );

  QString error;
  if ( !copyRecursively( mGisdbase, tmpGisdbase, &error ) )
  {
    reportRow( "cannot copy location " + mGisdbase + " to " + tmpGisdbase + " : " + error );
    return false;
  }
  return true;
}

// create temporary output location
bool TestQgsGrassProvider::createTmpLocation( QString& tmpGisdbase, QString& tmpLocation, QString& tmpMapset )
{
  // use QTemporaryFile to generate name (QTemporaryDir since 5.0)
  QTemporaryFile* tmpFile = new QTemporaryFile( QDir::tempPath() + "/qgis-grass-test" );
  tmpFile->open();
  tmpGisdbase = tmpFile->fileName();
  delete tmpFile;
  //tmpGisdbase = QDir::tempPath() + "/qgis-grass-test/test"; // debug
  reportRow( "tmpGisdbase: " + tmpGisdbase );
  tmpLocation = "test";
  tmpMapset = "PERMANENT";

  QString tmpMapsetPath = tmpGisdbase + "/" + tmpLocation + "/" + tmpMapset;
  reportRow( "tmpMapsetPath: " + tmpMapsetPath );
  QDir tmpDir = QDir::temp();
  if ( !tmpDir.mkpath( tmpMapsetPath ) )
  {
    reportRow( "cannot create " + tmpMapsetPath );
    return false;
  }

  QStringList cpFiles;
  cpFiles << "DEFAULT_WIND" << "WIND" << "PROJ_INFO" << "PROJ_UNITS";
  QString templateMapsetPath = mGisdbase + "/" + mLocation + "/PERMANENT";
  foreach ( QString cpFile, cpFiles )
  {
    if ( !QFile::copy( templateMapsetPath + "/" + cpFile, tmpMapsetPath + "/" + cpFile ) )
    {
      reportRow( "cannot copy " + cpFile );
      return false;
    }
  }
  return true;
}

void TestQgsGrassProvider::rasterImport()
{
  reportHeader( "TestQgsGrassProvider::rasterImport" );
  bool ok = true;

  QString tmpGisdbase;
  QString tmpLocation;
  QString tmpMapset;

  if ( !createTmpLocation( tmpGisdbase, tmpLocation, tmpMapset ) )
  {
    reportRow( "cannot create temporary location" );
    GVERIFY( false );
    return;
  }

  QStringList rasterFiles;
  rasterFiles << "tenbytenraster.asc" << "landsat.tif" << "raster/band1_byte_ct_epsg4326.tif" << "raster/band1_int16_noct_epsg4326.tif";
  rasterFiles << "raster/band1_float32_noct_epsg4326.tif" << "raster/band3_int16_noct_epsg4326.tif";

  QgsCoordinateReferenceSystem mapsetCrs = QgsGrass::crsDirect( mGisdbase, mLocation );
  foreach ( QString rasterFile, rasterFiles )
  {
    QString uri = QString( TEST_DATA_DIR ) + "/" + rasterFile;
    QString name = QFileInfo( uri ).baseName();
    reportRow( "input raster: " + uri );
    QgsRasterDataProvider* provider = qobject_cast<QgsRasterDataProvider*>( QgsProviderRegistry::instance()->provider( "gdal", uri ) );
    if ( !provider )
    {
      reportRow( "Cannot create provider " + uri );
      ok = false;
      continue;
    }
    if ( !provider->isValid() )
    {
      reportRow( "Provider is not valid " + uri );
      ok = false;
      continue;
    }

    QgsRectangle newExtent = provider->extent();
    int newXSize = provider->xSize();
    int newYSize = provider->ySize();

    QgsRasterPipe* pipe = new QgsRasterPipe();
    pipe->set( provider );

    QgsCoordinateReferenceSystem providerCrs = provider->crs();
    if ( providerCrs.isValid() && mapsetCrs.isValid() && providerCrs != mapsetCrs )
    {
      QgsRasterProjector * projector = new QgsRasterProjector;
      projector->setCRS( providerCrs, mapsetCrs );
      projector->destExtentSize( provider->extent(), provider->xSize(), provider->ySize(),
                                 newExtent, newXSize, newYSize );

      pipe->set( projector );
    }

    QgsGrassObject rasterObject( tmpGisdbase, tmpLocation, tmpMapset, name, QgsGrassObject::Raster );
    QgsGrassRasterImport *import = new QgsGrassRasterImport( pipe, rasterObject,
        newExtent, newXSize, newYSize );
    if ( !import->import() )
    {
      reportRow( "import failed: " +  import->error() );
      ok = false;
    }
    delete import;
  }
  removeRecursively( tmpGisdbase );
  GVERIFY( ok );
}

void TestQgsGrassProvider::vectorImport()
{
  reportHeader( "TestQgsGrassProvider::vectorImport" );
  bool ok = true;

  QString tmpGisdbase;
  QString tmpLocation;
  QString tmpMapset;

  if ( !createTmpLocation( tmpGisdbase, tmpLocation, tmpMapset ) )
  {
    reportRow( "cannot create temporary location" );
    GVERIFY( false );
    return;
  }

  QStringList files;
  files << "points.shp" << "multipoint.shp" << "lines.shp" << "polys.shp";
  files << "polys_overlapping.shp" << "bug5598.shp";

  QgsCoordinateReferenceSystem mapsetCrs = QgsGrass::crsDirect( mGisdbase, mLocation );
  foreach ( QString file, files )
  {
    QString uri = QString( TEST_DATA_DIR ) + "/" + file;
    QString name = QFileInfo( uri ).baseName();
    reportRow( "input vector: " + uri );
    QgsVectorDataProvider* provider = qobject_cast<QgsVectorDataProvider*>( QgsProviderRegistry::instance()->provider( "ogr", uri ) );
    if ( !provider )
    {
      reportRow( "Cannot create provider " + uri );
      ok = false;
      continue;
    }
    if ( !provider->isValid() )
    {
      reportRow( "Provider is not valid " + uri );
      ok = false;
      continue;
    }

    QgsGrassObject vectorObject( tmpGisdbase, tmpLocation, tmpMapset, name, QgsGrassObject::Vector );
    QgsGrassVectorImport *import = new QgsGrassVectorImport( provider, vectorObject );
    if ( !import->import() )
    {
      reportRow( "import failed: " +  import->error() );
      ok = false;
    }
    delete import;

    QStringList layers = QgsGrass::vectorLayers( tmpGisdbase, tmpLocation, tmpMapset, name );
    reportRow( "created layers: " + layers.join( "," ) );
  }
  removeRecursively( tmpGisdbase );
  GVERIFY( ok );
}

class TestQgsGrassCommand
{
  public:
    enum Command
    {
      AddFeature
    };

    TestQgsGrassCommand( Command c, QgsFeature f, int t ) : command( c ), feature( f ), type( t )  {}

    QString toString();
    Command command;
    QgsFeature feature;
    int type; // GRASS type
};

QString TestQgsGrassCommand::toString()
{
  QString string;
  if ( command == AddFeature )
  {
    string += "AddFeature ";
    string += feature.geometry()->exportToWkt( 1 );
  }
  return string;
}

void TestQgsGrassProvider::edit()
{
  reportHeader( "TestQgsGrassProvider::edit" );
  bool ok = true;

  QString tmpGisdbase;
  QString tmpLocation;
  QString tmpMapset;

  if ( !createTmpLocation( tmpGisdbase, tmpLocation, tmpMapset ) )
  {
    reportRow( "cannot create temporary location" );
    GVERIFY( false );
    return;
  }

  QList<QList<TestQgsGrassCommand>> commandGroups;

  QList<TestQgsGrassCommand> commands;
  QgsFeature feature;
  feature.setGeometry( new QgsGeometry( new QgsPointV2( QgsWKBTypes::Point, 10, 10, 0 ) ) );
  commands << TestQgsGrassCommand( TestQgsGrassCommand::AddFeature, feature, GV_POINT );
  commandGroups << commands;

  for ( int i = 0; i < commandGroups.size(); i++ )
  {
    commands = commandGroups[i];

    // Create GRASS vector
    QString name = QString( "edit_%1" ).arg( i );
    QgsGrassObject mapObject = QgsGrassObject( tmpGisdbase, tmpLocation, tmpMapset, name, QgsGrassObject::Vector );
    reportRow( "create new map: " + mapObject.toString() );
    QString error;
    QgsGrass::createVectorMap( mapObject, error );
    if ( !error.isEmpty() )
    {
      reportRow( error );
      ok = false;
      break;
    }

    QString uri = mapObject.mapsetPath() + "/" + name + "/1_point";
    reportRow( "uri: " + uri );
    QgsVectorLayer *layer = new QgsVectorLayer( uri, name, "grass" );
    if ( !layer->isValid() )
    {
      reportRow( "layer is not valid" );
      ok = false;
      break;
    }
    QgsGrassProvider *provider = qobject_cast<QgsGrassProvider *>( layer->dataProvider() );
    if ( !provider )
    {
      reportRow( "cannot get provider" );
      ok = false;
      break;
    }

    // Create memory vector for verification, it has no fields until added
    QgsVectorLayer *expectedLayer = new QgsVectorLayer( "Point", "test", "memory" );
    if ( !expectedLayer->isValid() )
    {
      reportRow( "verification layer is not valid" );
      ok = false;
      break;
    }

    layer->startEditing();
    provider->startEditing( layer );

    expectedLayer->startEditing();

    for ( int j = 0; j < commands.size(); j++ )
    {
      TestQgsGrassCommand command = commands[j];
      if ( command.command == TestQgsGrassCommand::AddFeature )
      {
        reportRow( "command: " + command.toString() );
        provider->setNewFeatureType( command.type );

        QgsFeature feature = command.feature;
        feature.initAttributes( layer->fields().size() ); // attributes must match layer fields
        layer->addFeature( feature );

        QgsFeature expectedFeature = command.feature;
        expectedFeature.initAttributes( expectedLayer->fields().size() );
        //expectedFeature.setGeometry( new QgsGeometry( new QgsPointV2( QgsWKBTypes::Point, 10, 20, 0 ) ) ); // debug
        expectedLayer->addFeature( expectedFeature );
      }
      if ( !compare( uri, mapObject, expectedLayer, ok ) )
      {
        reportRow( "command failed" );
        break;
      }
      else
      {
        reportRow( "command ok" );
      }
    }

    layer->commitChanges();
    delete layer;
    delete expectedLayer;
  }

  removeRecursively( tmpGisdbase );
  GVERIFY( ok );
}

QList<QgsFeature> TestQgsGrassProvider::getFeatures( QgsVectorLayer *layer )
{
  QgsFeatureIterator iterator = layer->getFeatures( QgsFeatureRequest() );
  QgsFeature feature;
  QList<QgsFeature> features;
  while ( iterator.nextFeature( feature ) )
  {
    features << feature;
  }
  iterator.close();
  return features;
}

bool TestQgsGrassProvider::equal( QgsFeature feature, QgsFeature expectedFeature )
{
  if ( !feature.geometry()->equals( expectedFeature.geometry() ) )
  {
    return false;
  }
  // GRASS feature has always additional cat field
  QSet<int> indexes;
  for ( int i = 0; i < feature.fields()->size(); i++ )
  {
    QString name = feature.fields()->at( i ).name();
    if ( name == "cat" ) // skip cat
    {
      continue;
    }
    indexes << i;
  }
  for ( int i = 0; i < expectedFeature.fields()->size(); i++ )
  {
    QString name = expectedFeature.fields()->at( i ).name();
    int index = feature.fields()->indexFromName( name );
    if ( index < 0 )
    {
      // not found
      return false;
    }
    indexes.remove( index );
    if ( feature.attribute( index ) != expectedFeature.attribute( i ) )
    {
      return false;
    }
  }
  if ( indexes.size() > 0 )
  {
    // unexpected attribute in feature
    QStringList names;
    Q_FOREACH ( int i, indexes )
    {
      names << feature.fields()->at( i ).name();
    }
    reportRow( QString( "feature has %1 unexpected attributes: %2" ).arg( indexes.size() ).arg( names.join( "," ) ) );
    return false;
  }
  return true;
}

bool TestQgsGrassProvider::compare( QList<QgsFeature> features, QList<QgsFeature> expectedFeatures, bool& ok )
{
  bool localOk = true;
  if ( features.size() != expectedFeatures.size() )
  {
    reportRow( QString( "different number of features (%1) and expected features (%2)" ).arg( features.size() ).arg( expectedFeatures.size() ) );
    ok = false;
    return false;
  }
  // Check if each expected feature exists in features
  Q_FOREACH ( const QgsFeature& expectedFeature,  expectedFeatures )
  {
    bool found = false;
    Q_FOREACH ( const QgsFeature& feature, features )
    {
      if ( equal( feature, expectedFeature ) )
      {
        found = true;
        break;
      }
    }
    if ( !found )
    {
      reportRow( QString( "expected feature fid = %1 not found in features" ).arg( expectedFeature.id() ) );
      ok = false;
      localOk = false;
    }
  }
  return localOk;
}

bool TestQgsGrassProvider::compare( QString uri, QgsGrassObject mapObject, QgsVectorLayer *expectedLayer, bool& ok )
{
  QList<QgsFeature> expectedFeatures = getFeatures( expectedLayer );

  // read the map using another layer/provider
  QgsVectorLayer *layer = new QgsVectorLayer( uri, "test", "grass" );
  if ( !layer->isValid() )
  {
    reportRow( "shared layer is not valid" );
    ok = false;
    return false;
  }
  QList<QgsFeature> features = getFeatures( layer );
  delete layer;
  layer = 0;

  bool sharedOk = compare( features, expectedFeatures, ok );
  if ( sharedOk )
  {
    reportRow( "comparison with shared layer ok" );
  }
  else
  {
    reportRow( "comparison with shared layer failed" );
  }

  // Open an independent layer which does not share data with edited one
  // build topology
  G_TRY
  {
    struct Map_info *map = QgsGrass::vectNewMapStruct();
    QgsGrass::setMapset( mapObject );
    Vect_open_old( map, mapObject.name().toUtf8().data(), mapObject.mapset().toUtf8().data() );

#if ( GRASS_VERSION_MAJOR == 6 && GRASS_VERSION_MINOR >= 4 ) || GRASS_VERSION_MAJOR > 6
    Vect_build( map );
#else
    Vect_build( map, stderr );
#endif
    Vect_set_release_support( map );
    Vect_close( map );
    QgsGrass::vectDestroyMapStruct( map );
  }
  G_CATCH( QgsGrass::Exception &e )
  {
    reportRow( "Cannot build topology: " + QString( e.what() ) );
    ok = false;
    return false;
  }

  QgsGrassVectorMapStore * mapStore = new QgsGrassVectorMapStore();
  QgsGrassVectorMapStore::setStore( mapStore );

  layer = new QgsVectorLayer( uri, "test", "grass" );
  if ( !layer->isValid() )
  {
    reportRow( "independent layer is not valid" );
    ok = false;
    return false;
  }
  features = getFeatures( layer );
  delete layer;
  QgsGrassVectorMapStore::setStore( 0 );
  delete mapStore;

  bool independentOk = compare( features, expectedFeatures, ok );
  if ( independentOk )
  {
    reportRow( "comparison with independent layer ok" );
  }
  else
  {
    reportRow( "comparison with independent layer failed" );
  }

  return sharedOk && independentOk;
}

QTEST_MAIN( TestQgsGrassProvider )
#include "testqgsgrassprovider.moc"
