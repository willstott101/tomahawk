/* === This file is part of Tomahawk Player - <http://tomahawk-player.org> ===
 *
 *   Copyright 2010-2014, Christian Muehlhaeuser <muesli@tomahawk-player.org>
 *   Copyright 2010-2011, Jeff Mitchell <jeff@tomahawk-player.org>
 *   Copyright 2010-2012, Leo Franchi   <lfranchi@kde.org>
 *   Copyright 2013,      Teo Mrnjavac <teo@kde.org>
 *
 *   Tomahawk is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   Tomahawk is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Tomahawk. If not, see <http://www.gnu.org/licenses/>.
 */

#include "ViewManager.h"

#include "audio/AudioEngine.h"
#include "infobar/InfoBar.h"

#include "playlist/FlexibleView.h"
#include "playlist/FlexibleTreeView.h"
#include "playlist/TreeModel.h"
#include "playlist/PlaylistModel.h"
#include "playlist/PlaylistView.h"
#include "playlist/PlayableProxyModel.h"
#include "playlist/PlayableModel.h"
#include "playlist/ColumnView.h"
#include "playlist/TreeView.h"
#include "playlist/TreeWidget.h"
#include "playlist/GridView.h"
#include "playlist/AlbumModel.h"
#include "SourceList.h"
#include "TomahawkSettings.h"

#include "playlist/InboxModel.h"
#include "playlist/InboxView.h"
#include "playlist/TrackItemDelegate.h"
#include "playlist/RecentlyPlayedModel.h"
#include "playlist/dynamic/widgets/DynamicWidget.h"
#include "resolvers/ScriptCollection.h"
#include "widgets/NewReleasesWidget.h"
#include "widgets/infowidgets/SourceInfoWidget.h"
#include "widgets/infowidgets/ArtistInfoWidget.h"
#include "widgets/infowidgets/AlbumInfoWidget.h"
#include "widgets/infowidgets/TrackInfoWidget.h"
#include "widgets/NewPlaylistWidget.h"
#include "widgets/AnimatedSplitter.h"

#include "utils/Logger.h"
#include "utils/TomahawkUtilsGui.h"

#include <QVBoxLayout>
#include <QMetaMethod>


#define FILTER_TIMEOUT 280

using namespace Tomahawk;

ViewManager* ViewManager::s_instance = 0;


ViewManager*
ViewManager::instance()
{
    return s_instance;
}


ViewManager::ViewManager( QObject* parent )
    : QObject( parent )
    , m_widget( new QWidget() )
    , m_queue( 0 )
    , m_newReleasesWidget( 0 )
    , m_inboxWidget( 0 )
    , m_currentPage( 0 )
{
    s_instance = this;

    m_widget->setLayout( new QVBoxLayout() );
    m_infobar = new InfoBar();
    m_stack = new QStackedWidget();

    m_inboxModel = new InboxModel( this );
    m_inboxModel->setTitle( tr( "Inbox" ) );
    m_inboxModel->setDescription( tr( "Listening suggestions from your friends" ) );
    m_inboxModel->setIcon( TomahawkUtils::defaultPixmap( TomahawkUtils::Inbox ) );

    m_widget->layout()->addWidget( m_infobar );
    m_widget->layout()->addWidget( m_stack );

    m_superCollectionView = new TreeWidget();
    m_superCollectionView->view()->proxyModel()->setStyle( PlayableProxyModel::Collection );
    m_superCollectionModel = new TreeModel( m_superCollectionView );
    m_superCollectionView->view()->setTreeModel( m_superCollectionModel );
//    m_superCollectionView->proxyModel()->setShowOfflineResults( false );

    m_stack->setContentsMargins( 0, 0, 0, 0 );
    m_widget->setContentsMargins( 0, 0, 0, 0 );
    m_widget->layout()->setContentsMargins( 0, 0, 0, 0 );
    m_widget->layout()->setMargin( 0 );
    m_widget->layout()->setSpacing( 0 );

    connect( AudioEngine::instance(), SIGNAL( playlistChanged( Tomahawk::playlistinterface_ptr ) ), this, SLOT( playlistInterfaceChanged( Tomahawk::playlistinterface_ptr ) ) );

    connect( &m_filterTimer, SIGNAL( timeout() ), SLOT( applyFilter() ) );
    connect( m_infobar, SIGNAL( filterTextChanged( QString ) ), SLOT( setFilter( QString ) ) );

/*    connect( m_infobar, SIGNAL( flatMode() ), SLOT( setTableMode() ) );
    connect( m_infobar, SIGNAL( artistMode() ), SLOT( setTreeMode() ) );
    connect( m_infobar, SIGNAL( albumMode() ), SLOT( setAlbumMode() ) );*/
}


ViewManager::~ViewManager()
{
    delete m_newReleasesWidget;
    delete m_inboxWidget;
    delete m_widget;
}


FlexibleView*
ViewManager::createPageForPlaylist( const playlist_ptr& playlist )
{
    FlexibleView* view = new FlexibleView();
    PlaylistModel* model = new PlaylistModel();

    PlaylistView* pv = new PlaylistView();
    view->setDetailedView( pv );
    view->setPixmap( pv->pixmap() );

    // We need to set the model on the view before loading the playlist, so spinners & co are connected
    view->setPlaylistModel( model );
    pv->setPlaylistModel( model );

    model->loadPlaylist( playlist );
    playlist->resolve();

    return view;
}


FlexibleView*
ViewManager::createPageForList( const QString& title, const QList< query_ptr >& queries )
{
    FlexibleView* view = new FlexibleView();
    PlaylistModel* model = new PlaylistModel();

    PlaylistView* pv = new PlaylistView();
    view->setDetailedView( pv );
    view->setPixmap( pv->pixmap() );
    view->setTemporaryPage( true );

    // We need to set the model on the view before loading the playlist, so spinners & co are connected
    view->setPlaylistModel( model );
    pv->setPlaylistModel( model );

    model->setTitle( title );
    model->appendQueries( queries );

    return view;
}


playlist_ptr
ViewManager::playlistForPage( ViewPage* page ) const
{
    playlist_ptr p;
    if ( dynamic_cast< PlaylistView* >( page ) && dynamic_cast< PlaylistView* >( page )->playlistModel() &&
        !dynamic_cast< PlaylistView* >( page )->playlistModel()->playlist().isNull() )
    {
        p = dynamic_cast< PlaylistView* >( page )->playlistModel()->playlist();
    }
    else if ( dynamic_cast< DynamicWidget* >( page ) )
        p = dynamic_cast< DynamicWidget* >( page )->playlist();

    return p;
}


Tomahawk::ViewPage*
ViewManager::show( const Tomahawk::playlist_ptr& playlist )
{
    if ( !playlist->loaded() )
        playlist->loadRevision();

    FlexibleView* view;

    if ( !m_playlistViews.contains( playlist ) || m_playlistViews.value( playlist ).isNull() )
    {
        view = createPageForPlaylist( playlist );
        m_playlistViews.insert( playlist, view );
    }
    else
    {
        view = m_playlistViews.value( playlist ).data();
    }

    setPage( view );
    return view;
}


Tomahawk::ViewPage*
ViewManager::show( const Tomahawk::dynplaylist_ptr& playlist )
{
    if ( !m_dynamicWidgets.contains( playlist ) || m_dynamicWidgets.value( playlist ).isNull() )
    {
       m_dynamicWidgets[ playlist ] = new Tomahawk::DynamicWidget( playlist, m_stack );

       playlist->resolve();
    }

    setPage( m_dynamicWidgets.value( playlist ).data() );

    return m_dynamicWidgets.value( playlist ).data();
}


Tomahawk::ViewPage*
ViewManager::show( const Tomahawk::artist_ptr& artist )
{
    ArtistInfoWidget* swidget;
    if ( !m_artistViews.contains( artist ) || m_artistViews.value( artist ).isNull() )
    {
        swidget = new ArtistInfoWidget( artist );
        m_artistViews.insert( artist, swidget );
    }
    else
    {
        swidget = m_artistViews.value( artist ).data();
    }

    setPage( swidget );
    return swidget;
}


Tomahawk::ViewPage*
ViewManager::show( const Tomahawk::album_ptr& album )
{
    AlbumInfoWidget* swidget;
    if ( !m_albumViews.contains( album ) || m_albumViews.value( album ).isNull() )
    {
        swidget = new AlbumInfoWidget( album );
        m_albumViews.insert( album, swidget );
    }
    else
    {
        swidget = m_albumViews.value( album ).data();
    }

    setPage( swidget );
    return swidget;
}


Tomahawk::ViewPage*
ViewManager::show( const Tomahawk::query_ptr& query )
{
    TrackInfoWidget* swidget;
    if ( !m_trackViews.contains( query ) || m_trackViews.value( query ).isNull() )
    {
        swidget = new TrackInfoWidget( query );
        m_trackViews.insert( query, swidget );
    }
    else
    {
        swidget = m_trackViews.value( query ).data();
    }

    setPage( swidget );
    return swidget;
}


Tomahawk::ViewPage*
ViewManager::show( const Tomahawk::collection_ptr& collection )
{
    m_currentCollection = collection;

    FlexibleTreeView* view;
    if ( !m_collectionViews.contains( collection ) || m_collectionViews.value( collection ).isNull() )
    {
        view = new FlexibleTreeView();

        view->columnView()->proxyModel()->setStyle( PlayableProxyModel::Collection );
        TreeModel* model = new TreeModel();
        PlayableModel* flatModel = new PlayableModel();
        PlayableModel* albumModel = new PlayableModel();

        view->setTreeModel( model );
        view->setFlatModel( flatModel );
        view->setAlbumModel( albumModel );

        model->addCollection( collection );
        flatModel->appendTracks( collection );
        albumModel->appendAlbums( collection );

        setPage( view );

        if ( !collection.isNull() )
            view->setEmptyTip( collection->emptyText() );

        if ( collection.objectCast<ScriptCollection>() )
            view->trackView()->setEmptyTip( tr( "Cloud collections aren't supported in the flat view yet. We will have them covered soon. Switch to another view to navigate them." ) );

        m_collectionViews.insert( collection, view );
    }
    else
    {
        view = m_collectionViews.value( collection ).data();
    }
    view->restoreViewMode();

    setPage( view );
    return view;
}


Tomahawk::ViewPage*
ViewManager::show( const Tomahawk::source_ptr& source )
{
    SourceInfoWidget* swidget;
    if ( !m_sourceViews.contains( source ) || m_sourceViews.value( source ).isNull() )
    {
        swidget = new SourceInfoWidget( source );
        m_sourceViews.insert( source, swidget );
    }
    else
    {
        swidget = m_sourceViews.value( source ).data();
    }

    setPage( swidget );
    return swidget;
}


Tomahawk::ViewPage*
ViewManager::show( ViewPage* page )
{
    setPage( page );

    return page;
}


Tomahawk::ViewPage*
ViewManager::showSuperCollection()
{
    if ( m_superCollections.isEmpty() )
        m_superCollectionModel->addAllCollections();

    foreach( const Tomahawk::source_ptr& source, SourceList::instance()->sources() )
    {
        if ( !m_superCollections.contains( source->dbCollection() ) )
        {
            m_superCollections.append( source->dbCollection() );
//            m_superAlbumModel->addCollection( source->collection() );
        }
    }

    m_superCollectionModel->setTitle( tr( "SuperCollection" ) );
    m_superCollectionModel->setDescription( tr( "Combined libraries of all your online friends" ) );

    setPage( m_superCollectionView );
    return m_superCollectionView;
}


void
ViewManager::playlistInterfaceChanged( Tomahawk::playlistinterface_ptr interface )
{
    playlist_ptr pl = playlistForInterface( interface );
    if ( !pl.isNull() )
    {
        TomahawkSettings::instance()->appendRecentlyPlayedPlaylist( pl->guid(), pl->author()->id() );
    }
    else
    {
        pl = dynamicPlaylistForInterface( interface );
        if ( !pl.isNull() )
            TomahawkSettings::instance()->appendRecentlyPlayedPlaylist( pl->guid(), pl->author()->id() );
    }
}


Tomahawk::ViewPage*
ViewManager::showNewReleasesPage()
{
    if ( !m_newReleasesWidget )
    {
        m_newReleasesWidget = new NewReleasesWidget();
        m_newReleasesWidget->fetchData();
    }

    return show( m_newReleasesWidget );
}


Tomahawk::ViewPage*
ViewManager::showQueuePage()
{
    if ( !m_queue )
        return 0;

    return show( m_queue );
}


Tomahawk::ViewPage *
ViewManager::showInboxPage()
{
    if ( !m_inboxWidget )
    {
        m_inboxWidget = new InboxPage( m_widget );
    }

    return show( m_inboxWidget );
}


void
ViewManager::setFilter( const QString& filter )
{
    m_filter = filter;

    m_filterTimer.stop();
    m_filterTimer.setInterval( FILTER_TIMEOUT );
    m_filterTimer.setSingleShot( true );
    m_filterTimer.start();
}


void
ViewManager::applyFilter()
{
    if ( m_currentPage )
        m_currentPage->setFilter( m_filter );
}


void
ViewManager::historyBack()
{
    if ( !m_pageHistoryBack.count() )
        return;

    ViewPage* page = m_pageHistoryBack.takeLast();

    if ( m_currentPage )
    {
        m_pageHistoryFwd << m_currentPage;
        tDebug() << "Moved to forward history:" << m_currentPage->widget()->metaObject()->className();
    }

    tDebug() << "Showing page after moving backwards in history:" << page->widget()->metaObject()->className();
    setPage( page, false );
}


void
ViewManager::historyForward()
{
    if ( !m_pageHistoryFwd.count() )
        return;

    ViewPage* page = m_pageHistoryFwd.takeLast();

    if ( m_currentPage )
    {
        m_pageHistoryBack << m_currentPage;
        tDebug() << "Moved to backward history:" << m_currentPage->widget()->metaObject()->className();
    }

    tDebug() << "Showing page after moving forwards in history:" << page->widget()->metaObject()->className();
    setPage( page, false );
}


QList<ViewPage*>
ViewManager::allPages() const
{
    QList< ViewPage* > pages = m_pageHistoryBack + m_pageHistoryFwd;
    pages << m_currentPage;

    return pages;
}


QList<ViewPage*>
ViewManager::historyPages() const
{
    return m_pageHistoryBack + m_pageHistoryFwd;
}


void
ViewManager::destroyPage( ViewPage* page )
{
    if ( !page )
        return;

    tDebug() << Q_FUNC_INFO << "Deleting page:" << page->title();

    if ( historyPages().contains( page ) )
    {
        m_pageHistoryBack.removeAll( page );
        m_pageHistoryFwd.removeAll( page );

        emit historyBackAvailable( m_pageHistoryBack.count() );
        emit historyForwardAvailable( m_pageHistoryFwd.count() );
    }

    if ( m_currentPage == page )
    {
        m_currentPage = 0;

        historyBack();
    }

    emit viewPageAboutToBeDestroyed( page );
    delete page;
    emit viewPageDestroyed();
}


bool
ViewManager::destroyCurrentPage()
{
    if ( !currentPage() || !currentPage()->isTemporaryPage() )
        return false;

    destroyPage( currentPage() );
    return true;
}


void
ViewManager::setPage( ViewPage* page, bool trackHistory )
{
    if ( !page )
        return;
    if ( page == m_currentPage )
        return;

    if ( m_stack->indexOf( page->widget() ) < 0 )
    {
        m_stack->addWidget( page->widget() );
    }

    if ( m_currentPage && trackHistory )
    {
        m_pageHistoryBack << m_currentPage;
        m_pageHistoryFwd.clear();
    }
    m_currentPage = page;

    emit historyBackAvailable( m_pageHistoryBack.count() );
    emit historyForwardAvailable( m_pageHistoryFwd.count() );

    tDebug() << "View page shown:" << page->title();
    emit viewPageActivated( page );

    if ( page->isTemporaryPage() )
        emit tempPageActivated( page );

    if ( AudioEngine::instance()->state() == AudioEngine::Stopped )
        AudioEngine::instance()->setPlaylist( page->playlistInterface() );

    // UGH!
    if ( QObject* obj = dynamic_cast< QObject* >( currentPage() ) )
    {
        // if the signal exists (just to hide the qobject runtime warning...)
        if ( obj->metaObject()->indexOfSignal( "descriptionChanged(QString)" ) > -1 )
            connect( obj, SIGNAL( descriptionChanged( QString ) ), m_infobar, SLOT( setDescription( QString ) ), Qt::UniqueConnection );

        if ( obj->metaObject()->indexOfSignal( "descriptionChanged(Tomahawk::artist_ptr)" ) > -1 )
            connect( obj, SIGNAL( descriptionChanged( Tomahawk::artist_ptr ) ), m_infobar, SLOT( setDescription( Tomahawk::artist_ptr ) ), Qt::UniqueConnection );

        if ( obj->metaObject()->indexOfSignal( "descriptionChanged(Tomahawk::album_ptr)" ) > -1 )
            connect( obj, SIGNAL( descriptionChanged( Tomahawk::album_ptr ) ), m_infobar, SLOT( setDescription( Tomahawk::album_ptr ) ), Qt::UniqueConnection );

        if ( obj->metaObject()->indexOfSignal( "longDescriptionChanged(QString)" ) > -1 )
            connect( obj, SIGNAL( longDescriptionChanged( QString ) ), m_infobar, SLOT( setLongDescription( QString ) ), Qt::UniqueConnection );

        if ( obj->metaObject()->indexOfSignal( "nameChanged(QString)" ) > -1 )
            connect( obj, SIGNAL( nameChanged( QString ) ), m_infobar, SLOT( setCaption( QString ) ), Qt::UniqueConnection );

        if ( obj->metaObject()->indexOfSignal( "pixmapChanged(QPixmap)" ) > -1 )
            connect( obj, SIGNAL( pixmapChanged( QPixmap ) ), m_infobar, SLOT( setPixmap( QPixmap ) ), Qt::UniqueConnection );

        if ( obj->metaObject()->indexOfSignal( "destroyed(QWidget*)" ) > -1 )
            connect( obj, SIGNAL( destroyed( QWidget* ) ), SLOT( onWidgetDestroyed( QWidget* ) ), Qt::UniqueConnection );
    }

    QWidget *previousPage = m_stack->currentWidget();

    m_stack->setCurrentWidget( page->widget() );

    //This should save the CPU cycles, especially with pages like the visualizer
    if ( previousPage && previousPage != page->widget() )
        previousPage->hide();

    updateView();
}


bool
ViewManager::isNewPlaylistPageVisible() const
{
    return dynamic_cast< NewPlaylistWidget* >( currentPage() ) != 0;
}


void
ViewManager::updateView()
{
    if ( currentPlaylistInterface() )
    {
        m_infobar->setFilter( currentPage()->filter() );
    }

    emit filterAvailable( currentPage()->showFilter() );

    m_infobar->setVisible( currentPage()->showInfoBar() );
    m_infobar->setCaption( currentPage()->title() );
    m_infobar->setUpdaters( currentPage()->updaters() );

    switch( currentPage()->descriptionType() )
    {
        case ViewPage::TextType:
            m_infobar->setDescription( currentPage()->description() );
            break;
        case ViewPage::ArtistType:
            m_infobar->setDescription( currentPage()->descriptionArtist() );
            break;
        case ViewPage::AlbumType:
            m_infobar->setDescription( currentPage()->descriptionAlbum() );
            break;

    }
    m_infobar->setLongDescription( currentPage()->longDescription() );
    m_infobar->setPixmap( currentPage()->pixmap() );
}


void
ViewManager::onWidgetDestroyed( QWidget* widget )
{
    tDebug() << "Destroyed child:" << widget << widget->metaObject()->className();

    bool resetWidget = ( m_stack->currentWidget() == widget );

    QList< Tomahawk::ViewPage* > p = historyPages();
    for ( int i = 0; i < p.count(); i++ )
    {
        ViewPage* page = p.at( i );
        if ( page->widget() != widget )
            continue;

        if ( !playlistForInterface( page->playlistInterface() ).isNull() )
        {
            m_playlistViews.remove( playlistForInterface( page->playlistInterface() ) );
        }
        if ( !dynamicPlaylistForInterface( page->playlistInterface() ).isNull() )
        {
            m_dynamicWidgets.remove( dynamicPlaylistForInterface( page->playlistInterface() ) );
        }

        m_pageHistoryBack.removeAll( page );
        m_pageHistoryFwd.removeAll( page );
        break;
    }

    m_stack->removeWidget( widget );

    if ( resetWidget )
    {
        m_currentPage = 0;
        historyBack();
    }
}


ViewPage*
ViewManager::pageForDynPlaylist(const dynplaylist_ptr& pl) const
{
    return m_dynamicWidgets.value( pl ).data();
}


ViewPage*
ViewManager::pageForPlaylist(const playlist_ptr& pl) const
{
    return m_playlistViews.value( pl ).data();
}


ViewPage*
ViewManager::pageForInterface( Tomahawk::playlistinterface_ptr interface ) const
{
    QList< Tomahawk::ViewPage* > pages = allPages();

    for ( int i = 0; i < pages.count(); i++ )
    {
        ViewPage* page = pages.at( i );
        if ( page->playlistInterface() == interface )
            return page;
        if ( page->playlistInterface() && page->playlistInterface()->hasChildInterface( interface ) )
            return page;
    }

    return 0;
}


Tomahawk::playlistinterface_ptr
ViewManager::currentPlaylistInterface() const
{
    if ( currentPage() )
        return currentPage()->playlistInterface();
    else
        return Tomahawk::playlistinterface_ptr();
}


Tomahawk::ViewPage*
ViewManager::currentPage() const
{
    return m_currentPage;
}


Tomahawk::playlist_ptr
ViewManager::playlistForInterface( Tomahawk::playlistinterface_ptr interface ) const
{
    foreach ( QPointer<FlexibleView> view, m_playlistViews.values() )
    {
        if ( !view.isNull() && view.data()->playlistInterface() == interface )
        {
            return m_playlistViews.key( view );
        }
    }

    return Tomahawk::playlist_ptr();
}


Tomahawk::dynplaylist_ptr
ViewManager::dynamicPlaylistForInterface( Tomahawk::playlistinterface_ptr interface ) const
{
    foreach ( QPointer<DynamicWidget> view, m_dynamicWidgets.values() )
    {
        if ( !view.isNull() && view.data()->playlistInterface() == interface )
        {
            return m_dynamicWidgets.key( view );
        }
    }

    return Tomahawk::dynplaylist_ptr();
}


bool
ViewManager::isSuperCollectionVisible() const
{
    return ( currentPage() != 0 &&
           ( currentPage()->playlistInterface() == m_superCollectionView->playlistInterface() ) );
}


void
ViewManager::showCurrentTrack()
{
    ViewPage* page = pageForInterface( AudioEngine::instance()->currentTrackPlaylist() );

    if ( page )
    {
        setPage( page );
        page->jumpToCurrentTrack();
    }
}


Tomahawk::ViewPage*
ViewManager::newReleasesWidget() const
{
    return m_newReleasesWidget;
}


Tomahawk::ViewPage*
ViewManager::inboxWidget() const
{
    return m_inboxWidget;
}


ViewPage*
ViewManager::dynamicPageWidget( const QString& pageName ) const
{
    if ( m_dynamicPages.contains( pageName ) )
        return m_dynamicPages.value( pageName );

    if ( m_dynamicPagePlugins.contains( pageName ) )
        return m_dynamicPagePlugins.value( pageName ).data();

    return 0;
}


void
ViewManager::addDynamicPage( Tomahawk::ViewPagePlugin* viewPage, const QString& pageName )
{
    const QString pageId = !pageName.isEmpty() ? pageName : viewPage->defaultName();

    tLog() << Q_FUNC_INFO << "Trying to add" << pageId;

    if ( m_dynamicPages.contains( pageId ) || m_dynamicPagePlugins.contains( pageId ) )
    {
        tLog() << "Not adding a second ViewPage with name" << pageName;
        Q_ASSERT( false );
    }

    m_dynamicPagePlugins.insert( pageId, viewPage );
    emit viewPageAdded( pageId, viewPage, viewPage->sortValue() );
}


/*void
ViewManager::addDynamicPage( const QString& pageName, const QString& text, const QIcon& icon, function<Tomahawk::ViewPage*()> instanceLoader, int sortValue )
{
    tLog() << Q_FUNC_INFO << "Trying to add" << pageName;

    if ( m_dynamicPages.contains( pageName ) || m_dynamicPagePlugins.contains( pageName ) )
    {
        tLog() << "Not adding a second ViewPage with name" << pageName;
        Q_ASSERT( false );
    }

    m_dynamicPagesInstanceLoaders.insert( pageName, instanceLoader );
    emit viewPageAdded( pageName, text, icon, sortValue );
}*/


ViewPage*
ViewManager::showDynamicPage( const QString& pageName )
{
    tLog() << Q_FUNC_INFO << "pageName:" << pageName;

    if ( !m_dynamicPages.contains( pageName ) && !m_dynamicPagePlugins.contains( pageName ) )
    {
        if ( !m_dynamicPagesInstanceLoaders.contains( pageName ) )
        {
           Q_ASSERT_X( false, Q_FUNC_INFO, QString("Trying to show a page that does not exist and does not have a registered loader: %d" ).arg( pageName ).toStdString().c_str() );
           return 0;
        }

        ViewPage* viewPage = m_dynamicPagesInstanceLoaders.value( pageName )();
        Q_ASSERT( viewPage );
        if ( !viewPage ) {
            return NULL;
        }
        m_dynamicPages.insert( pageName, viewPage );

        m_dynamicPagesInstanceLoaders.remove( pageName );
    }

    return show( dynamicPageWidget( pageName ) );
}


Tomahawk::ViewPage*
ViewManager::superCollectionView() const
{
    return m_superCollectionView;
}


InboxModel*
ViewManager::inboxModel()
{
    return m_inboxModel;
}
