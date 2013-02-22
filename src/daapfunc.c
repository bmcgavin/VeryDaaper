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
#include <stdbool.h>
#include <unicode/utf8.h>
#include <unicode/ustdio.h>
#include <time.h>
#include <sys/time.h>

#include "libopendaap-0.4.0/client.h"
#include "libopendaap-0.4.0/daap.h"
#include "daapfunc.h"
#include "libopendaap-0.4.0/debug/debug.h"
#include "events.h"
#include "gui.h"

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

    /* song position */
    char* position;
    char* playing_length;
    bool playing;
    bool paused;
    bool random;
    DAAP_ClientHost_DatabaseItem* selected_song;

};
/*************** Status Callback from libopendaap *******/

void daap_audiocb_finished()
{
    daap_host *prevhost = playing_song.host;
    if (!prevhost) {
    	//????
    	prevhost = visibleHost;
    	//return;
    }
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
        if (daap_host_get_random(prevhost)) {
            TRACE("STOP_NONE : PLAYSOURCE_RANDOM\n");
            {
                DAAP_ClientHost_DatabaseItem *song;

                //Get a random songID
                struct timeval tv;
                time_t curtime;
                gettimeofday(&tv, NULL);
                curtime=tv.tv_sec;
                srand(curtime);
                int i = rand() % prevhost->nSongs;
                //int i = random(prevhost->nSongs);
                //int i = rand_r(time()()) % prevhost->nSongs;

				song = &(prevhost->songs[i]);

				daap_host_set_selected_artist(prevhost, daap_host_get_artist_for_song(prevhost, song));
				daap_host_set_selected_album(prevhost, daap_host_get_album_for_song_and_artist(prevhost, song, daap_host_get_selected_artist(prevhost)));

                /* ok, lets play it */
                mm_stopEventsToIgnore++;
                daap_host_play_song(PLAYSOURCE_RANDOM, prevhost, song->id);
            }
            return;
        }
		switch (prevsource)
		{
		case PLAYSOURCE_HOST:
            TRACE("STOP_NONE : PLAYSOURCE_HOST\n");
            {
                DAAP_ClientHost_DatabaseItem *song;

                int songindex = -1;
                if (prevhost->selected_song->id != previd) {
                	song = prevhost->selected_song;
                } else {
					songindex = get_songindex_by_id(prevhost, previd);
					if (songindex == -1) {
						break;
					}
					song = &(prevhost->songs[songindex]);

					/* now get the next song */
					/* not if it's the last song */
					if (songindex+1 >= prevhost->nSongs) break;

					song = &(prevhost->songs[songindex+1]);
                }
				//UPDATE
				if (prevhost->selected_artist &&
						strcasecmp(prevhost->selected_artist->artist,
								   song->songartist) != 0) {
					daap_host_set_selected_artist(prevhost, daap_host_get_next_artist(prevhost, prevhost->selected_artist));
					daap_host_set_selected_album(prevhost, daap_host_get_next_album(prevhost, prevhost->selected_album));
				//Don't want to change artist + album AND album
				} else if (prevhost->selected_album &&
						strcasecmp(prevhost->selected_album->album,
								   song->songalbum) != 0) {
					daap_host_set_selected_album(prevhost, daap_host_get_next_album(prevhost, prevhost->selected_album));
				}

                /* ok, lets play it */
                mm_stopEventsToIgnore++;
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
    int track_i[2], disc_i[2], sorting_song_id[2];

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
    /* FIXME discnumber appears to be bitrate */
    /*
    disc_i[0] = a->songdiscnumber;
    disc_i[1] = b->songdiscnumber;
    ret = (disc_i[0] < disc_i[1] ? -1 :
            (disc_i[0] > disc_i[1] ? 1 : 0));
    if (ret != 0) return ret;
    */

    /* compare track */
    track_i[0] = a->songtracknumber;
    track_i[1] = b->songtracknumber;
    ret = (track_i[0] < track_i[1] ? -1 :
            (track_i[0] > track_i[1] ? 1 : 0));
    if (ret != 0) return ret;

    //Use the DAAP ID first as it takes filename into account
	sorting_song_id[0] = a->id;
	sorting_song_id[1] = b->id;
	ret = (sorting_song_id[0] < sorting_song_id[1] ? -1 :
		(sorting_song_id[0] > sorting_song_id[1] ? 1 : 0));
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

int get_songindex_by_id(daap_host *host, int song_id)
{
    int i;
    if (host == NULL) {
    	host = visibleHost;
    }

    for (i = 0; i < host->nSongs; i++)
    {
        if (host->songs[i].id == song_id)
            return i;
    }
    return -1;
}


/******************* daap music playing *****************/
void daap_host_stop_song(daap_host *host) {
#ifdef PLAYBOOK
	if (playing_song.host != NULL)
		DAAP_ClientHost_MMRStop(playing_song.host->libopendaap_host, ctxt);
#else
	//DAAP_ClientHost_AsyncStop(playing_song.host->libopendaap_host);
#endif
    host->playing = false;
    host->paused = false;
    playing_song.host = NULL;
}

void daap_host_pause_song(daap_host *host) {
#ifdef PLAYBOOK
	DAAP_ClientHost_MMRPause(playing_song.host->libopendaap_host, ctxt);
#else
    //DAAP_ClientHost_AsyncStop(playing_song.host->libopendaap_host);
#endif
	host->playing = false;
	host->paused = true;
}

bool daap_host_get_paused(daap_host* host) {
	return host->paused;
}

void daap_host_resume_song(daap_host *host) {
#ifdef PLAYBOOK
	DAAP_ClientHost_MMRResume(playing_song.host->libopendaap_host, ctxt);
#else
	//DAAP_ClientHost_AsyncStop(playing_song.host->libopendaap_host);
#endif
	host->playing = true;
	host->paused = false;
	playing_song.host = host;
}

void daap_host_set_position(daap_host* host, const char* position) {

	memcpy(host->position, position, strlen(position) + 1);
}

const char* daap_host_get_position (daap_host* host) {
	return host->position;
}

bool daap_host_get_playing (daap_host* host) {
	return host->playing;
}

void daap_host_set_playing (daap_host* host, bool playing) {
	host->playing = playing;
}

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
        host->playing = false;
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

    host->playing = true;
    host->paused = false;

    daap_host_set_selected_song(host, &host->songs[songindex]);
    itoa(host->selected_song->songtime, host->playing_length, 10);
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

int daap_host_enum_songs(daap_host *host)
{
    if (host->selected_album) {
    	if (host->selected_artist) {
    	    //Get first song
    		return host->songs->id;
    	} else {
    		//Get first artist
    		daap_host_set_selected_artist(host, daap_host_get_next_artist(host, NULL));
    		//Get first song
    		return host->songs->id;
    	}
    } else {
    	//Get first artist
    	daap_host_set_selected_artist(host, daap_host_get_next_artist(host, NULL));
    	//get first album
    	daap_host_set_selected_album(host, daap_host_get_next_album(host, NULL));
    	//get first song
    	return host->songs->id;
    }
    return -1;
}


/* callbacks */
void on_song_row_activated()
{
    daap_host *host = visibleHost;
    int id = daap_host_enum_songs(host);
    //int id = 3483;

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
    host->selected_artist = NULL;
    host->selected_album = NULL;
    host->selected_song = NULL;
    debugMsg("update_songs", -1);
    update_songs(host);

    /* now make sure it tells us when updates have happened */
    //DAAP_ClientHost_AsyncWaitUpdate(host->libopendaap_host);

    /* schedule a sourcelist redraw if playlists exist */
    //if (host->playlists)
        //schedule_lists_draw(1, 0, 0, 0);

    host->position = calloc(10, sizeof(char));
    host->playing_length = calloc(10, sizeof(char));
    host->playing = false;
    host->paused = false;
    host->random = false;
    //RPJ
    visibleHost = host;

    daap_host_set_selected_artist(host, daap_host_get_next_artist(host, NULL));
    daap_host_set_selected_album(host, daap_host_get_next_album(host, NULL));
    DAAP_ClientHost_DatabaseItem* song = (DAAP_ClientHost_DatabaseItem*)malloc(sizeof(DAAP_ClientHost_DatabaseItem));
    int selected_song_id = daap_host_enum_artist_album_songs(host, song, -1, host->selected_artist, host->selected_album);
    daap_host_set_selected_song(host, song);
    //on_song_row_activated();

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

bool daap_host_get_random(daap_host* host) {
	return host->random;
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
        		mm_stopEventsToIgnore++;
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
                //mm_stopEventsToIgnore++;
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

        //if (cur->sharename) continue;

        size = DAAP_ClientHost_GetSharename(cur->libopendaap_host, NULL, 0);

        buf = malloc(size);
        DAAP_ClientHost_GetSharename(cur->libopendaap_host, buf, size);
        cur->sharename = buf;
    }

    //RPJ
    //initial_connect(first);
    //schedule_lists_draw(1, 0, 0, 0);
    if (first == NULL) {
    	return;
    }
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

DAAP_ClientHost_DatabaseItem* daap_host_get_selected_song(daap_host* host) {
	return host->selected_song;
}

artist *daap_host_get_next_artist(daap_host *host, artist *curr) {
	artist* prev = host->artists;
	while (prev != NULL) {
		if (prev->next == curr) {
			break;
		}
		if (prev->next != NULL) {
			prev = prev->next;
		}
	}
	return prev;
}

artist* daap_host_get_first_artist(daap_host* host) {
	return host->artists;
}

album *daap_host_get_first_album_for_artist(daap_host* host, artist* curr) {
	album* prev;
	//Next pointer is actually previous alphabetically.
	prev = curr->albumhead;
	do {
		if (prev && prev->next == NULL) {
			return prev;
		}
	} while (prev = prev->next);
	//Should never get here.
	return NULL;
}

album *daap_host_get_prev_album(daap_host *host, album *curr) {
	album* prev;
	//Next pointer is actually previous alphabetically.
	prev = host->selected_artist->albumhead;
	do {
		if (prev && prev == curr) {
			return prev->next;
		}
	} while (prev = prev->next);
	//Only one album
	return NULL;
}

artist *daap_host_get_prev_artist(daap_host *host, artist *curr) {
	artist* prev = host->artists;
	while (prev != NULL) {
		if (prev == curr) {
			return prev->next;
		}
		if (prev->next != NULL) {
			prev = prev->next;
		}
	}
	return prev;
}

album *daap_host_get_next_album(daap_host *host, album *curr) {
	album* prev;
	//Next pointer is actually previous alphabetically.
	if (curr && host->selected_artist) {
		prev = host->selected_artist->albumhead;
		while (prev && prev->next != curr) {
			prev = prev->next;
		}
		return prev;
	}
	//Get first
	if (host->selected_artist && !host->selected_album) {
		prev = host->selected_artist->albumhead;
		while (prev && prev->next) {
			prev = prev->next;
		}
		return prev;
	}
	artist* prev_artist = daap_host_get_next_artist(host, host->selected_artist);
	while (prev_artist != NULL) {
		if (prev_artist && prev_artist->next == host->selected_artist) {
			break;
		}
		if (prev_artist->next != NULL) {
			prev_artist = prev_artist->next;
		}
	}
	return prev_artist->albumhead;
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

artist* daap_host_get_artist_for_song(daap_host* host, DAAP_ClientHost_DatabaseItem* song) {
	int i = get_songindex_by_id(host, song->id);
	if (i == -1) {
		return NULL;
	}
	char* artistname = host->songs[i].songartist;
	artist* newartist = host->artists;
	do {
		if (strcasecmp(newartist->artist, artistname) == 0) {
			return newartist;
		}
	} while (newartist = newartist->next);
	return NULL;
}

album *daap_host_get_album_for_song_and_artist(daap_host* host, DAAP_ClientHost_DatabaseItem * song, artist* artist) {
	int i = get_songindex_by_id(host, song->id);
	if (i == -1) {
		return NULL;
	}
	char* albumname = host->songs[i].songalbum;
	album* newalbum = artist->albumhead;
	do {
		if (strcasecmp(newalbum->album, albumname) == 0) {
			return newalbum;
		}
	} while (newalbum = newalbum->next);
	return NULL;


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

    /*
    UChar c = NULL;
    int32_t i = 0;
    size_t length = 12 + strlen(host->selected_artist->artist) + 1;
    UChar *conv = (UChar*)calloc(length, sizeof(UChar));
    uint8_t* str = (uint8_t*)host->selected_artist->artist;
    int32_t count = 0;
    int x = 12;
    conv[0] = 'N';
    conv[1] = 'e';
    conv[2] = 'w';
    conv[3] = ' ';
    conv[4] = 'a';
    conv[5] = 'r';
    conv[6] = 't';
    conv[7] = 'i';
    conv[8] = 's';
    conv[9] = 't';
    conv[10]= ':';
    conv[11]= ' ';
    while (count < length) {
    	U8_NEXT(str, i, length, c);
    	count += i;
    	conv[x] = c;
    	x++;
    }
    conv[x] = '\0';
	debugMsg(conv, 7);
    */
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

void daap_host_toggle_playsource_random(daap_host* host) {
	host->random = !host->random;
	toggleShuffle();
}

char* daap_host_get_song_length(daap_host* host) {
	return host->playing_length;
}

void daap_host_seek_percent(daap_host* host, int pos) {
	//Get the length
	char* length = daap_host_get_song_length(host);
	int int_length = atoi(length);
	//Work out position
	int seek_to = (int_length * pos) / 100;
	//convert to char*
	char buffer[10];
	itoa(seek_to, buffer, 10);

#ifdef PLAYBOOK
	DAAP_ClientHost_MMRSeek(host->libopendaap_host, ctxt, buffer);
#else
	//DAAP_ClientHost_AsyncStop(playing_song.host->libopendaap_host);
#endif
}

void daap_host_jump_to_letter(daap_host* host, char* c) {
	artist* nextartist = host->artists;
	album* nextalbum = NULL;
	bool found = false;
	char* target = NULL;
	while (nextartist->next) {
		target = nextartist->next->artist;
		if (strncasecmp(target, "the ", 4) == 0) {
			target += 4;
		}
		if (!found && strncasecmp(&(target[0]), c, sizeof(char)) == 0) {
			found = true;
		}
		if (found && strncasecmp(&(target[0]), c, sizeof(char)) != 0) {
			nextalbum = daap_host_get_first_album_for_artist(host, nextartist);
			DAAP_ClientHost_DatabaseItem* nextsong = (DAAP_ClientHost_DatabaseItem*) malloc(sizeof(DAAP_ClientHost_DatabaseItem));
			int song_id = daap_host_enum_artist_album_songs(host, nextsong, -1, nextartist, nextalbum);
			if (song_id != -1) {
				daap_host_set_selected_album(host, nextalbum);
				daap_host_set_selected_artist(host, nextartist);
				daap_host_set_selected_song(host, nextsong);
			}
			break;
		}
		nextartist = nextartist->next;
	}
}


void daap_host_set_selected_song(daap_host* host, DAAP_ClientHost_DatabaseItem* song) {

	host->selected_song = song;

    char* tmp = calloc(10 + strlen(song->itemname) + 1, sizeof(char));
    sprintf(tmp, "New song: %s", song->itemname);
    debugMsg(tmp, 9);

}

char *daap_host_get_artistname(artist *artist)
{
    return artist->artist;
}

char *daap_host_get_albumname(album *album)
{
    return album->album;
}

int daap_host_prev_artist_album_songs(daap_host *host,
                                      DAAP_ClientHost_DatabaseItem *song,
                                      int next_id,
                                      artist *artist, album *album)
{
    DAAP_ClientHost_DatabaseItem *thissong = (DAAP_ClientHost_DatabaseItem*)malloc(sizeof(DAAP_ClientHost_DatabaseItem*));

	if (next_id > 0) {
    	thissong = &(host->songs[next_id-1]);
        if (song)
        	memcpy(song, thissong, sizeof(DAAP_ClientHost_DatabaseItem));
        return next_id-1;
    }
	return -1;

	int i;
    int max_song_id = -1;
    int max_track_number = -1;
    if (next_id >= 0) {
    	//Got the previous song, so get the track number
    	max_track_number = host->selected_song->songtracknumber;
    }

    for (i = next_id; i >= 0; i--)
    {
        thissong = &(host->songs[i]);
        if (artist && (!thissong->songartist ||
                    strcasecmp(daap_host_get_artistname(artist),
                               thissong->songartist) != 0))
            continue;
        if (album && (!thissong->songalbum ||
                    strcasecmp(daap_host_get_albumname(album),
                               thissong->songalbum) != 0))
            continue;

        if (thissong->songtracknumber == (max_track_number - 1) ||
        	//Same album, tracknumbers are zero
        	thissong->songtracknumber == 0) {
            if (song)
            	memcpy(song, thissong, sizeof(DAAP_ClientHost_DatabaseItem));
            return i;
        }

        if (max_song_id == -1 ||
        	thissong->songtracknumber > max_track_number) {
        	max_song_id = i;
        	max_track_number = thissong->songtracknumber;
        	continue;
        }
        //What?
        /*
        if (thissong->songtracknumber != 1)
        	continue;*/

        //Got a song from the same album
        if (song)
        	memcpy(song, thissong, sizeof(DAAP_ClientHost_DatabaseItem));
        return i;

    }

    if (song && (max_song_id > -1)) {
    	memcpy(song, thissong, sizeof(DAAP_ClientHost_DatabaseItem));
    	return max_song_id;
    }
    return -1;
}


int daap_host_enum_artist_album_songs(daap_host *host,
                                      DAAP_ClientHost_DatabaseItem *song,
                                      int prev_id,
                                      artist *artist, album *album)
{
    int i;
    int min_song_id = -1;
    int min_track_number = -1;
    if (prev_id >= 0) {
    	//Got the previous song, so get the track number
    	min_track_number = host->selected_song->songtracknumber;
    }

    DAAP_ClientHost_DatabaseItem *thissong = (DAAP_ClientHost_DatabaseItem*)malloc(sizeof(DAAP_ClientHost_DatabaseItem*));
    for (i = prev_id+1; i < host->nSongs; i++)
    {
        thissong = &(host->songs[i]);
        if (artist && (!thissong->songartist ||
                    strcasecmp(daap_host_get_artistname(artist),
                               thissong->songartist) != 0))
            continue;
        if (album && (!thissong->songalbum ||
                    strcasecmp(daap_host_get_albumname(album),
                               thissong->songalbum) != 0))
            continue;

        if (thissong->songtracknumber == (min_track_number + 1) ||
        	//Same album, tracknumbers are zero
        	thissong->songtracknumber == 0) {
            if (song)
            	memcpy(song, thissong, sizeof(DAAP_ClientHost_DatabaseItem));
            return i;
        }

        if (min_song_id == -1 ||
        	thissong->songtracknumber <= min_track_number) {
        	min_song_id = i;
        	min_track_number = thissong->songtracknumber;
        }
        //What?
        /*
        if (thissong->songtracknumber != 1)
        	continue;*/

        //Got a song from the same album
        if (song)
        	memcpy(song, thissong, sizeof(DAAP_ClientHost_DatabaseItem));
        return i;

    }

    if (song && (min_song_id > -1)) {
    	memcpy(song, thissong, sizeof(DAAP_ClientHost_DatabaseItem));
    	return min_song_id;
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

char* get_current_song_length(daap_host* host) {
	if (host == NULL) {
		host = visibleHost;
	}
	if (playing_song.song_id && host && host->songs) {
		return host->playing_length;
	}
	return "-1";
}

static DAAP_Status prev_libopendaap_status = DAAP_STATUS_idle;

//static int songstarted = 0;
//static int playing = 0;
static int songpipe[2];
