/*
 * daapfunc.c
 *
 *  Created on: 2 Feb 2013
 *      Author: Rich
 */



#include <mm/renderer.h>
#include <sys/stat.h>
#include <malloc.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>

#include "libopendaap-0.4.0/client.h"
#include "libopendaap-0.4.0/daap.h"
#include "daapfunc.h"
#include "libopendaap-0.4.0/debug/debug.h"

#define DEFAULT_DEBUG_CHANNEL "daapfunc"

#if ! defined(DEFAULT_AUDIO_OUT)
    #define DEFAULT_AUDIO_OUT "audio:default"
#endif

mode_t mode = S_IRUSR | S_IXUSR;
mmr_connection_t *conn = NULL;
mmr_context_t *ctxt = NULL;
int audio_oid = -1;

static daap_host *visibleHost = NULL;
static playlist *visiblePlaylist = NULL;

daap_host* getVisibleHost() {
	if (visibleHost == NULL) {
		visibleHost = clientHosts;
	}
	return visibleHost;
}

static DAAP_SClient *clientInstance = NULL;
static daap_host *clientHosts = NULL;


static song_ref next_song = { PLAYSOURCE_NONE, NULL, -1 };
static song_ref playing_song = { PLAYSOURCE_NONE, NULL, -1 };

/* for the browser, and primitive sorting */
struct playlistTAG
{
    char *name;
    int id;
    DAAP_ClientHost_DatabasePlaylistItem *items;
    int num; /* gets set whenever items is set, could be invalid if items is NULL */
    playlist *next;
};

struct albumTAG
{
    char *album;
    album *next;
};

struct artistTAG
{
    char *artist;
    album *albumhead;
    artist *next;
};


struct daap_hostTAG
{
    /* ref implies how many references are being kept to the
     * *connected* host. being in the list doesn't add a reference */
    int ref;
    DAAP_SClientHost *libopendaap_host;
    char *sharename;

    DAAP_ClientHost_Database *databases;
    int nDatabases;

    DAAP_ClientHost_DatabaseItem *songs;
    int nSongs;

    playlist *playlists;

    artist *artists;
    artist *selected_artist;
    album *selected_album;

    daap_host *prev, *next;

    /* used to check if this host still exists when we get a hosts
     * changed message */
    int marked;

    /* used to mark if the host is dead, but references
     * still exist */
    int dead;
};
/*************** Status Callback from libopendaap *******/

void daap_audiocb_finished()
{
    daap_host *prevhost = playing_song.host;
    int previd = playing_song.song_id;
    enum playsource prevsource = playing_song.playsource;

    TRACE("()\n");

    playing_song.host = NULL;
    playing_song.song_id = -1;
/*
#ifdef HAVE_GNOME
  if (prevhost)
  {
#endif
    daap_host_release(prevhost);

    close(songpipe[0]);
    songpipe[0] = -1;
#ifdef HAVE_GNOME
  }
#endif
*/

    if (stop_type == STOP_NEWSONG)
    {
        TRACE("STOP_NEWSONG\n");
        daap_host_play_song(next_song.playsource, next_song.host, next_song.song_id);
        daap_host_release(next_song.host); /* play_song will addref */
        next_song.host = NULL;
        next_song.song_id = -1;
    }
    else if (stop_type == STOP_NONE) /* ended normally, try and play the next song */
    {
        switch (prevsource)
        {
        case PLAYSOURCE_HOST:
            TRACE("STOP_NONE : PLAYSOURCE_HOST\n");
            {
                DAAP_ClientHost_DatabaseItem *song;

                /* if a playlist is visible, get the next song if we can */
                if (visiblePlaylist)
                {
                    int songindex;
                    int i;
                    int nextid = -1;
                    if (visiblePlaylist->items == NULL) break;

                    for (i = 0; i < visiblePlaylist->num; i++)
                    {
                        if (visiblePlaylist->items[i].songid == previd
                                && (i+1) < visiblePlaylist->num)
                        {
                            nextid = visiblePlaylist->items[i+1].songid;
                        }
                    }
                    if (nextid == -1) break;

                    songindex = get_songindex_by_id(prevhost, nextid);

                    song = &(prevhost->songs[songindex]);
                }
                else
                {
                    int songindex = get_songindex_by_id(prevhost, previd);
                    song = &(prevhost->songs[songindex]);

                    /* if some other artist / album is visible, break */
                    if (prevhost->selected_artist &&
                            strcasecmp(prevhost->selected_artist->artist,
                                       song->songartist) != 0)
                        break;
                    if (prevhost->selected_album &&
                            strcasecmp(prevhost->selected_album->album,
                                       song->songalbum) != 0)
                        break;

                    /* now get the next song */
                    /* not if it's the last song */
                    if (songindex+1 >= prevhost->nSongs) break;

                    song = &(prevhost->songs[songindex+1]);

                    /* if it's not in the visible list, break */
                    if (prevhost->selected_artist &&
                            strcasecmp(prevhost->selected_artist->artist,
                                       song->songartist) != 0)
                        break;
                    if (prevhost->selected_album &&
                            strcasecmp(prevhost->selected_album->album,
                                       song->songalbum) != 0)
                        break;
                }

                /* ok, lets play it */
                daap_host_play_song(PLAYSOURCE_HOST, prevhost, song->id);
            }
            break;
        case PLAYSOURCE_PARTY:
            TRACE("STOP_NONE : PLAYSOURCE_PARTY\n");
            //partyshuffle_play_next();
            break;
        case PLAYSOURCE_NONE:
            TRACE("STOP_NONE : PLAYSOURCE_NONE\n");
            break;
        }
    }
}


int daap_host_is_connected(daap_host *host)
{
    return host->ref ? 1 : 0;
}

/* actually remove a host from the list, that is it no longer exists */
static void daap_host_remove(daap_host *host)
{
    /* only remove it from the list once */
    if (!host->dead)
    {
        if (host->prev) host->prev->next = host->next;
        else clientHosts = host->next;
        if (host->next) host->next->prev = host->prev;
    }

    /* if it's visible, remove visibility */
    //if (visibleHost == host) daap_host_set_visible(NULL, NULL);

    /* if it's playing, stop it */
    if (playing_song.host == host)
    {
        stop_type = STOP_HOSTDEAD;
#ifdef PLAYBOOK
        DAAP_ClientHost_MMRStop(host->libopendaap_host, ctxt);
#else
        DAAP_ClientHost_AsyncStop(host->libopendaap_host);
#endif
    }

    /* perhaps there are some songs in party shuffle? */
    //if (daap_host_is_connected(host)) partyshuffle_removehost(host);

    if (daap_host_is_connected(host))
    {
        /* delaying final deletion until all references are destroyed */
        host->dead = 1;
        return;
    }
    if (host->sharename) free(host->sharename);
    DAAP_ClientHost_Release(host->libopendaap_host);
    free(host);
}


playlist *daap_host_enum_playlists(daap_host *host, playlist *prev)
{
    if (!prev) return host->playlists;
    return prev->next;
}

void sourcelist_set_source_connect_state(daap_host *host, int connected)
{

}

/* gets the current database
 * FIXME: what if it updates in between getting the size and
 *        getting the database? */
static void update_databases(daap_host *host)
{
    int size;

    size = DAAP_ClientHost_GetDatabases(host->libopendaap_host,
                                        NULL, NULL, 0);

    if (host->databases) free(host->databases);
    host->databases = malloc(size);
    DAAP_ClientHost_GetDatabases(host->libopendaap_host,
                                 host->databases,
                                 &(host->nDatabases),
                                 size);
}

/******* sorting routine used by qsort on songs *********/
static int compare_songitems(const void *pva, const void *pvb)
{
    DAAP_ClientHost_DatabaseItem *a = (DAAP_ClientHost_DatabaseItem*)pva;
    DAAP_ClientHost_DatabaseItem *b = (DAAP_ClientHost_DatabaseItem*)pvb;

    int ret;

    char *artist[2], *album[2], *name[2];
    int track_i[2], disc_i[2];

    /* compare artist */
    artist[0] = a->songartist;
    artist[1] = b->songartist;
    if (!artist[0]) return 1;
    if (!artist[1]) return -1;
    if (!artist[0][0]) return 1;
    if (!artist[1][0]) return -1;
    if (strncasecmp(artist[0], "the ", 4) == 0)
        artist[0] += 4;
    if (strncasecmp(artist[1], "the ", 4) == 0)
        artist[1] += 4;
    ret = strcasecmp(artist[0], artist[1]);
    if (ret != 0) return ret;

    /* compare album */
    album[0] = a->songalbum;
    album[1] = b->songalbum;
    if (!album[0]) return 1;
    if (!album[1]) return -1;
    if (!album[0][0]) return 1;
    if (!album[1][0]) return -1;
    if (strncasecmp(album[0], "the ", 4) == 0)
        album[0] += 4;
    if (strncasecmp(album[1], "the ", 4) == 0)
        album[1] += 4;
    ret = strcasecmp(album[0], album[1]);
    if (ret != 0) return ret;

    /* compare disc */
    disc_i[0] = a->songdiscnumber;
    disc_i[1] = b->songdiscnumber;
    ret = (disc_i[0] < disc_i[1] ? -1 :
            (disc_i[0] > disc_i[1] ? 1 : 0));
    if (ret != 0) return ret;

    /* compare track */
    track_i[0] = a->songtracknumber;
    track_i[1] = b->songtracknumber;
    ret = (track_i[0] < track_i[1] ? -1 :
            (track_i[0] > track_i[1] ? 1 : 0));
    if (ret != 0) return ret;

    name[0] = a->itemname;
    name[1] = b->itemname;
    if (!name[0][0]) return 1;
    if (!name[1][0]) return -1;
    if (strncasecmp(name[0], "the ", 4) == 0)
        name[0] += 4;
    if (strncasecmp(name[1], "the ", 4) == 0)
        name[1] += 4;
    ret = strcasecmp(name[0], name[1]);
    if (ret != 0) return ret;

    return 0;
}

static void free_albums(album *alb)
{
    album *cur = alb;
    while (cur)
    {
        album *next = cur->next;
        if (cur->album) free(cur->album);
        free(cur);
        cur = next;
    }
}
static void free_playlists(daap_host *host)
{
    playlist *cur = host->playlists;

    while (cur)
    {
        playlist *next = cur->next;
        if (cur->name) free(cur->name);
        if (cur->items) free(cur->items);
        free(cur);
        cur = next;
    }

    host->playlists = NULL;
}

static void free_artists(daap_host *host)
{
    artist *cur = host->artists;

    host->selected_artist = NULL;
    host->selected_album = NULL;

    while (cur)
    {
        artist *next = cur->next;
        if (cur->artist) free(cur->artist);
        if (cur->albumhead) free_albums(cur->albumhead);
        free(cur);
        cur = next;
    }

    host->artists = NULL;
}

/* adds a given artist / album if it doesn't exist
 * to the artist and album list of the current host
 */
static void artistalbumview_add(daap_host *host,
                                char *artist_s, char *album_s)
{
    artist *cur_artist = host->artists;
    album *cur_album = NULL;
    if (!artist_s || !album_s) return;
    if (!artist_s[0] || !album_s[0]) return;
    while (cur_artist)
    {
        if (strcasecmp(cur_artist->artist, artist_s) == 0)
            break;
        cur_artist = cur_artist->next;
    }
    if (!cur_artist)
    {
        artist *newartist = malloc(sizeof(artist));

        newartist->artist = malloc(strlen(artist_s) +1);
        strcpy(newartist->artist, artist_s);

        newartist->albumhead = NULL;

        newartist->next = host->artists;
        host->artists = newartist;

        cur_artist = newartist;
    }
    cur_album = cur_artist->albumhead;
    while (cur_album)
    {
        if (strcasecmp(cur_album->album, album_s) == 0)
            break;
        cur_album = cur_album->next;
    }
    if (!cur_album)
    {
        album *newalbum = malloc(sizeof(album));

        newalbum->album = malloc(strlen(album_s) + 1);
        strcpy(newalbum->album, album_s);

        newalbum->next = cur_artist->albumhead;
        cur_artist->albumhead = newalbum;

        cur_album = newalbum;
    }
}

/* gets the songs from the first database (FIXME use multi databases?)
 * and sorts them.
 * update_databases must be called first.
 */
static void update_songs(daap_host *host)
{
    int size;
    int db_id;
    int i;

    if (!host->nDatabases) return;
    db_id = host->databases[0].id;

    size = DAAP_ClientHost_GetDatabaseItems(host->libopendaap_host, db_id,
                                            NULL, NULL, 0);

    if (host->songs) free(host->songs);
    host->songs = malloc(size);
    DAAP_ClientHost_GetDatabaseItems(host->libopendaap_host, db_id,
                                     host->songs,
                                     &(host->nSongs),
                                     size);

    /* sort it */
    qsort(host->songs, host->nSongs,
          sizeof(DAAP_ClientHost_DatabaseItem),
          compare_songitems);

    if (host->artists) free_artists(host);
    for (i = 0; i < host->nSongs; i++)
    {
    	if (i == 1000) {
    		TRACE("");
    	}
        artistalbumview_add(host, host->songs[i].songartist,
                            host->songs[i].songalbum);
    }
}


static void update_playlists(daap_host *host)
{
    int size;
    int db_id;
    int num = 0, i;
    DAAP_ClientHost_DatabasePlaylist *playlists;

    if (!host->nDatabases) return;
    db_id = host->databases[0].id;

    if (host->playlists) free_playlists(host);

    size = DAAP_ClientHost_GetPlaylists(host->libopendaap_host,
                                        db_id,
                                        NULL, NULL, 0);

    if (!size) return;

    playlists = malloc(size);
    DAAP_ClientHost_GetPlaylists(host->libopendaap_host, db_id,
                                 playlists, &num, size);

    /* if there's <= 1, return. 1 is the full playlist */
    if (num <= 1)
    {
        free(playlists);
        return;
    }

    /* keep the order, ignore the 0th element, it's the full list */
    for (i = num - 1; i > 0; i--)
    {
        playlist *new = malloc(sizeof(playlist));
        new->next = host->playlists;
        host->playlists = new;
        new->name = strdup(playlists[i].itemname);
        new->id = playlists[i].id;
        new->items = NULL;
    }

    free(playlists);
}

/**** daap utility functions used by the rest of tb *****/
void daap_host_addref(daap_host *host)
{
    //if (!(host->ref++)) initial_connect(host);
}

static int get_songindex_by_id(daap_host *host, int song_id)
{
    int i;

    for (i = 0; i < host->nSongs; i++)
    {
        if (host->songs[i].id == song_id)
            return i;
    }
    return -1;
}


/******************* daap music playing *****************/

void daap_host_play_song(enum playsource playsource, daap_host *host, int song_id)
{
    int songindex;

    if (playing_song.host)
    {
        /* if a song is being played from any host, stop it */

        /* schedule the next song */
        daap_host_addref(host);
        next_song.playsource = playsource;
        next_song.host = host;
        next_song.song_id = song_id;

        stop_type = STOP_NEWSONG;
        /* this should then call cb_eos in gstreamer, which will
         * do the rest */
#ifdef PLAYBOOK
        DAAP_ClientHost_MMRStop(playing_song.host->libopendaap_host, ctxt);
#else
        DAAP_ClientHost_AsyncStop(playing_song.host->libopendaap_host);
#endif
        return;
    }

    stop_type = STOP_NONE;

    songindex = get_songindex_by_id(host, song_id);
    if (songindex == -1) return;

    if (strcmp(host->songs[songindex].songformat, "m4p") == 0)
    {
        char* bling = "The song you are trying to play is a protected AAC file.\nThis file has likely been purchased from the iTunes Music Store\nand is not able to be shared through traditional sharing mechanisms.\nThis song will not be played.\n";
    }

    daap_host_addref(host);
    if (host->selected_artist == NULL) {
    	host->selected_artist = host->artists;
    	while (host->selected_artist->next != NULL) {
    		host->selected_artist = host->selected_artist->next;
    	}
    }
    if (host->selected_album == NULL) {
    	host->selected_album = host->selected_artist->albumhead;
    }
    playing_song.playsource = playsource;
    playing_song.host = host;
    playing_song.song_id = song_id;

    if (pipe(songpipe) == -1) return;

#ifdef PLAYBOOK
    if (DAAP_ClientHost_MMRGetAudioFile(host->libopendaap_host,
                                          host->databases[0].id, song_id,
                                          host->songs[songindex].songformat,
                                          ctxt))
#else
    if (DAAP_ClientHost_AsyncGetAudioFile(host->libopendaap_host,
                                          host->databases[0].id, song_id,
                                          host->songs[songindex].songformat,
                                          songpipe[1]))
#endif
        goto fail;

    /* we will call into the audioplayer and start playing on the
     * daap status callback, once download has started
     */

    return;

fail:
    close(songpipe[0]);
    close(songpipe[1]);
}

/* callbacks */
void on_song_row_activated()
{
    daap_host *host = visibleHost;
    //int id = songlist_get_selected_id();
    //RPJ
    int id = 4785;

    if (!host) return;
    if (id == -1) return;

    daap_host_play_song(PLAYSOURCE_HOST, host, id);
}

/* connects to the new daap host,
 * downloads database, etc
 */
void initial_connect(daap_host *host)
{
	debugMsg("initial_connect", -1);
    int ret;



    DAAP_ClientHost_AddRef(host->libopendaap_host);
    ret = DAAP_ClientHost_Connect(host->libopendaap_host);
    while (ret == -401)
    {

        {

        }

    }
    if (ret <= -1)
    {

    }

    sourcelist_set_source_connect_state(host, 1);

    //RPJ why is databases an unallocated object in an initial connect?
	host->databases = NULL;
    debugMsg("update_databases", -1);
    update_databases(host);

    if (host->nDatabases > 1)
    {
        int i;
        printf("not really sure what to do with multiple databases "
               "(never seen it myself). The following databases will "
               "be invisible\n");
        for (i = 1; i < host->nDatabases; i++)
        {
            printf("invisible database: '%s'\n",
                   host->databases[i].name);
        }
    }

    //RPJ why is playlists an unallocated object in an initial connect?
    host->playlists = NULL;
    debugMsg("update_playlists", -1);
    update_playlists(host);

    //RPJ why is artists an unallocated object in an initial connect?
    host->songs = NULL;
    host->artists = NULL;
    debugMsg("update_songs", -1);
    update_songs(host);

    /* now make sure it tells us when updates have happened */
    //DAAP_ClientHost_AsyncWaitUpdate(host->libopendaap_host);

    /* schedule a sourcelist redraw if playlists exist */
    //if (host->playlists)
        //schedule_lists_draw(1, 0, 0, 0);

    //RPJ
    visibleHost = host;

    on_song_row_activated();

}

static void disconnect_host(daap_host *host)
{
    int had_playlists = 0;

    if (host->databases) free(host->databases);
    host->databases = NULL;
    host->nDatabases = 0;

    if (host->songs) free(host->songs);
    host->songs = NULL;
    host->nSongs = 0;

    if (host->playlists) had_playlists = 1;
    free_playlists(host);
    free_artists(host);

    DAAP_ClientHost_AsyncStopUpdate(host->libopendaap_host);
    DAAP_ClientHost_Disconnect(host->libopendaap_host);
    DAAP_ClientHost_Release(host->libopendaap_host);
    sourcelist_set_source_connect_state(host, 0);

    //if (had_playlists)
        //schedule_lists_draw(1, 0, 0, 0);
}


/**** daap utility functions used by the rest of tb *****/

void daap_host_release(daap_host *host)
{
    if (--(host->ref))
    {
        return;
    }
    disconnect_host(host);
    if (host->dead) daap_host_remove(host);
}


void daap_host_set_visible(daap_host *host, playlist *playlist)
{
    if (visibleHost == host && visiblePlaylist == playlist)
        return;

    if (visibleHost != host)
    {
        if (visibleHost) daap_host_release(visibleHost);

        if (host) daap_host_addref(host);

        /* connection failed */
        if (host->ref == 0)
        {

            visibleHost = NULL;
            visiblePlaylist = NULL;
            return;
        }

        visibleHost = host;
    }

    if (visiblePlaylist != NULL && playlist == NULL)
    {
        //schedule_browser_visible(1);
    }

    if (visiblePlaylist == NULL && playlist != NULL)
    {
        //schedule_browser_visible(0);
    }

    if (visiblePlaylist && visiblePlaylist != playlist)
    { /* free the item cache, just to save some memory */
        free(visiblePlaylist->items);
        visiblePlaylist->items = NULL;
    }

    visiblePlaylist = playlist;

    //schedule_lists_draw(0, 1, 1, 1);

}

void sendInitialCallback() {
	clientInstance = DAAP_Client_Create(DAAP_StatusCB, NULL);
	//cb_hosts_updated();
	DAAP_Client_EnumerateHosts(clientInstance, cb_enum_hosts, NULL);
}

static void DAAP_StatusCB( DAAP_SClient *client, DAAP_Status status, int pos,  void* context)
{
    switch (status)
    {
        case DAAP_STATUS_hostschanged:
        {
        	cb_hosts_updated();
            break;
        }
        case DAAP_STATUS_downloading:
        {
        	if (prev_libopendaap_status == DAAP_STATUS_negotiating)
        	{
        		/* start playing the song */
        		//audioplayer_playpipe(songpipe[0]);

				mmr_play(ctxt);
        	}
            break;
        }
        case DAAP_STATUS_error:
        {
            if (prev_libopendaap_status == DAAP_STATUS_negotiating ||
                prev_libopendaap_status == DAAP_STATUS_downloading)
            {
                stop_type = STOP_ERROR;
				mmr_stop(ctxt);
				mmr_input_detach(ctxt);
            }
            break;
        }
        case DAAP_STATUS_idle:
        {
            if (prev_libopendaap_status == DAAP_STATUS_downloading)
            {
                /* downloading has finished, close the pipe,
                 * this will cause an EOF to be sent.
                 * FIXME: need to check that all data has been flushed?
                 */
                //close(songpipe[1]);
                //songpipe[1] = -1;
            }
            break;
        }
        default:

            break;
    }
    prev_libopendaap_status = status;
}

static void cb_hosts_updated()
{
    daap_host *cur;
    daap_host *first = NULL;

    for (cur = clientHosts; cur != NULL; cur = cur->next) {

    	cur->marked = 0;
    }


    DAAP_Client_EnumerateHosts(clientInstance, cb_enum_hosts, NULL);

    /* now find hosts that need to be removed */
    cur = clientHosts;
    while (cur)
    {
    	//RPJ
    	if (first == NULL) {
    		first = cur;
    	}
        daap_host *next = cur->next;
        if (cur->marked == 0)
        {
            daap_host_remove(cur);
        }
        cur = next;
    }

    for (cur = clientHosts; cur != NULL; cur = cur->next)
    {
        char *buf;
        int size;

        if (cur->sharename) continue;

        size = DAAP_ClientHost_GetSharename(cur->libopendaap_host, NULL, 0);

        buf = malloc(size);
        DAAP_ClientHost_GetSharename(cur->libopendaap_host, buf, size);
        cur->sharename = buf;
    }

    //RPJ
    //initial_connect(first);
    //schedule_lists_draw(1, 0, 0, 0);
    debugMsg(first->sharename, -1);
}

/******************* specific callback handlers *********/
static int cb_enum_hosts( DAAP_SClient *client,
                         DAAP_SClientHost *host,
                          void *ctx)
{
    daap_host *cur, *prev = NULL;
    daap_host *newhost;

    /* check if the host is already on the list */
    for (cur = clientHosts; cur != NULL; cur = cur->next)
    {
        if (cur->libopendaap_host == host)
        {
            cur->marked = 1;
            return 1;
        }
        prev = cur;
    }

    /* if not add it to the end of the list */
    newhost = malloc(sizeof(daap_host));


    DAAP_ClientHost_AddRef(host);

    newhost->ref = 0;
    newhost->prev = prev;
    newhost->next = NULL;
    newhost->libopendaap_host = host;

    /* ensure we don't delete it */
    newhost->marked = 1;

    if (prev) prev->next = newhost;
    else clientHosts = newhost;

    return 1;
}

artist *daap_host_get_next_artist(daap_host *host, artist *curr) {
	artist* prev = host->artists;
	while (prev != NULL) {
		if (prev->next == curr) {
			return prev;
		}
		prev = prev->next;
	}
	return NULL;
}


album *daap_host_get_next_album(daap_host *host, album *curr) {
	if (curr->next != NULL) {
		return curr->next;
	}
	artist* prev = daap_host_get_next_artist(host, host->selected_artist);
	while (prev != NULL) {
		if (prev->next == host->selected_artist) {
			return prev->albumhead;
		}
		prev = prev->next;
	}
	return NULL;
}

artist *daap_host_enum_artists(daap_host *host, artist *prev)
{
    if (!prev) return host->artists;
    return prev->next;
}

album *daap_host_enum_album(artist *artist, album *prev)
{
    if (!prev) return artist->albumhead;
    return prev->next;
}

artist *daap_host_get_selected_artist(daap_host *host)
{
    return host->selected_artist;
}

album *daap_host_get_selected_album(daap_host *host)
{
    return host->selected_album;
}

void daap_host_set_selected_artist(daap_host *host, artist *artist)
{
    host->selected_artist = artist;

    /* unselect album if all was selected */
    if (host->selected_album && !artist)
        host->selected_album = NULL;

    /* unslected album if it's not in this artist */
    if (host->selected_album)
    {
        album *cur = artist->albumhead;
        while (cur && cur != host->selected_album)
        {
            cur = cur->next;
        }
        if (!cur) host->selected_album = NULL;
    }

    char* tmp = calloc(12 + strlen(host->selected_artist->artist) + 1, sizeof(char));
    sprintf(tmp, "New artist: %s", host->selected_artist->artist);
    debugMsg(tmp, 7);
    //free(tmp);
    //schedule_lists_draw(0, 0, 1, 1);
}

void daap_host_set_selected_album(daap_host *host, album *album)
{
    host->selected_album = album;

    char* tmp = calloc(11 + strlen(host->selected_album->album) + 1, sizeof(char));
    sprintf(tmp, "New album: %s", host->selected_album->album);
    debugMsg(tmp, 8);
    //free(tmp);

    //schedule_lists_draw(0, 0, 0, 1);
}

char *daap_host_get_artistname(artist *artist)
{
    return artist->artist;
}

char *daap_host_get_albumname(album *album)
{
    return album->album;
}

int daap_host_enum_artist_album_songs(daap_host *host,
                                      DAAP_ClientHost_DatabaseItem *song,
                                      int prev_id,
                                      artist *artist, album *album)
{
    int i;

    for (i = prev_id+1; i < host->nSongs; i++)
    {
        DAAP_ClientHost_DatabaseItem *thissong = &(host->songs[i]);
        if (artist && (!thissong->songartist ||
                    strcasecmp(daap_host_get_artistname(artist),
                               thissong->songartist) != 0))
            continue;
        if (album && (!thissong->songalbum ||
                    strcasecmp(daap_host_get_albumname(album),
                               thissong->songalbum) != 0))
            continue;

        if (song)
            memcpy(song, thissong, sizeof(DAAP_ClientHost_DatabaseItem));
        return i;
    }
    return -1;
}

album* get_new_album() {
	album* new_album = (album*)malloc(sizeof(album));
	return new_album;
}

artist* get_new_artist() {
	artist* new_artist = (artist*)malloc(sizeof(artist));
	return new_artist;
}

static DAAP_Status prev_libopendaap_status = DAAP_STATUS_idle;

//static int songstarted = 0;
//static int playing = 0;
static int songpipe[2];
