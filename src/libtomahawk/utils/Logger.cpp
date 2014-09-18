/* === This file is part of Tomahawk Player - <http://tomahawk-player.org> ===
 *
 *   Copyright 2010-2011, Christian Muehlhaeuser <muesli@tomahawk-player.org>
 *
 *   Tomahawk is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   Tomahawk is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Tomahawk. If not, see <http://www.gnu.org/licenses/>.
 */

#include "Logger.h"

#include <iostream>
#include <fstream>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QTime>
#include <QVariant>

#include "utils/TomahawkUtils.h"

#define LOGFILE_SIZE 1024 * 256

#define RELEASE_LEVEL_THRESHOLD 0
#define DEBUG_LEVEL_THRESHOLD LOGEXTRA
#define LOG_SQL_QUERIES 1

using namespace std;

ofstream logfile;
static int s_threshold = -1;
QMutex s_mutex;
bool shutdownInProgress = false;

namespace Logger
{

static void
log( const char *msg, unsigned int debugLevel, bool toDisk = true )
{
    if ( s_threshold < 0 )
    {
        if ( qApp->arguments().contains( "--verbose" ) )
            s_threshold = LOGTHIRDPARTY;
        else
            #ifdef QT_NO_DEBUG
            s_threshold = RELEASE_LEVEL_THRESHOLD;
            #else
            s_threshold = DEBUG_LEVEL_THRESHOLD;
            #endif
    }

    if ( debugLevel > LOGTHIRDPARTY )
        toDisk = false;

    #ifdef LOG_SQL_QUERIES
    if ( debugLevel == LOGSQL )
        toDisk = true;
    #endif

    if ( toDisk || (int)debugLevel <= s_threshold )
    {
        QMutexLocker lock( &s_mutex );

        #ifdef LOG_SQL_QUERIES
        if ( debugLevel == LOGSQL )
            logfile << "TSQLQUERY: ";
        #endif

        if ( shutdownInProgress )
        {
            // Do not use locales anymore in shutdown
            logfile << QDate::currentDate().day() << "."
                    << QDate::currentDate().month() << "."
                    << QDate::currentDate().year() << " - "
                    << QTime::currentTime().hour() << ":"
                    << QTime::currentTime().minute() << ":"
                    << QTime::currentTime().second()
                    << " [" << QString::number( debugLevel ).toUtf8().data() << "]: "
                    << msg << endl;
        }
        else
        {
            logfile << QDate::currentDate().toString().toUtf8().data()
                    << " - "
                    << QTime::currentTime().toString().toUtf8().data()
                    << " [" << QString::number( debugLevel ).toUtf8().data() << "]: "
                    << msg << endl;
        }

        logfile.flush();
    }

    if ( debugLevel <= LOGEXTRA || (int)debugLevel <= s_threshold )
    {
        QMutexLocker lock( &s_mutex );

        if ( shutdownInProgress )
        {
            cout << QTime::currentTime().hour() << ":"
                 << QTime::currentTime().minute() << ":"
                 << QTime::currentTime().second()
                 << " [" << QString::number( debugLevel ).toUtf8().data() << "]: "
                 << msg << endl;
        }
        else
        {
            cout << QTime::currentTime().toString().toUtf8().data()
                 << " [" << QString::number( debugLevel ).toUtf8().data() << "]: "
                 << msg << endl;
        }

        cout.flush();
    }
}


void
#if QT_VERSION >= QT_VERSION_CHECK( 5, 0, 0 )
TomahawkLogHandler( QtMsgType type, const QMessageLogContext& context, const QString& msg )
#else
TomahawkLogHandler( QtMsgType type, const char* msg )
#endif
{
    static QMutex s_mutex;

#if QT_VERSION >= QT_VERSION_CHECK( 5, 0, 0 )
    QByteArray ba = msg.toUtf8();
    const char* message = ba.constData();
#else
    const char* message = msg;
#endif

    QMutexLocker locker( &s_mutex );
    switch( type )
    {
        case QtDebugMsg:
            log( message, LOGTHIRDPARTY );
            break;

        case QtCriticalMsg:
            log( message, 0 );
            break;

        case QtWarningMsg:
            log( message, 0 );
            break;

        case QtFatalMsg:
            log( message, 0 );
            break;
    }
}


QString
logFile()
{
    return TomahawkUtils::appLogDir().filePath( "Tomahawk.log" );
}


void
setupLogfile()
{
    if ( QFileInfo( logFile() ).size() > LOGFILE_SIZE )
    {
        QByteArray lc;
        {
            QFile f( logFile() );
            f.open( QIODevice::ReadOnly | QIODevice::Text );
            f.seek( f.size() - ( LOGFILE_SIZE - ( LOGFILE_SIZE / 4 ) ) );
            lc = f.readAll();
            f.close();
        }

        QFile::remove( logFile() );

        {
            QFile f( logFile() );
            f.open( QIODevice::WriteOnly | QIODevice::Text );
            f.write( lc );
            f.close();
        }
    }

    logfile.open( logFile().toUtf8().constData(), ios::app );
#if QT_VERSION >= QT_VERSION_CHECK( 5, 0, 0 )
    qInstallMessageHandler( TomahawkLogHandler );
#else
    qInstallMsgHandler( TomahawkLogHandler );
#endif
}

}

using namespace Logger;

TLog::TLog( unsigned int debugLevel )
    : QDebug( &m_msg )
    , m_debugLevel( debugLevel )
{
}


TLog::~TLog()
{
    log( m_msg.toUtf8().data(), m_debugLevel );
}


void
tLogNotifyShutdown()
{
    QMutexLocker locker( &s_mutex );
    shutdownInProgress = true;
}
