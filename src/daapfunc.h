/*
 * daapfunc.h
 *
 *  Created on: 2 Feb 2013
 *      Author: Rich
 */

#ifndef DAAPFUNC_H_
#define DAAPFUNC_H_

/*
 * daapfunc.c
 *
 *  Created on: 2 Feb 2013
 *      Author: Rich
 */

#include "libopendaap-0.4.0/client.h"
#include "libopendaap-0.4.0/daap.h"

void sendInitialCallback();
static void cb_hosts_updated();
static int cb_enum_hosts( DAAP_SClient *client,
                         DAAP_SClientHost *host,
                          void *ctx);
static void DAAP_StatusCB( DAAP_SClient *client, DAAP_Status status, int pos,  void* context);


typedef struct daap_hostTAG daap_host;
typedef struct playlistTAG playlist;
typedef struct artistTAG artist;
typedef struct albumTAG album;

int daap_host_is_connected(daap_host *host);

/* actually remove a host from the list, that is it no longer exists */
static void daap_host_remove(daap_host *host);

playlist *daap_host_enum_playlists(daap_host *host, playlist *prev);

void sourcelist_set_source_connect_state(daap_host *host, int connected);

/* gets the current database
 * FIXME: what if it updates in between getting the size and
 *        getting the database? */
static void update_databases(daap_host *host);

/******* sorting routine used by qsort on songs *********/
static int compare_songitems(const void *pva, const void *pvb);

static void free_albums(album *alb);
static void free_playlists(daap_host *host);

static void free_artists(daap_host *host);
/* adds a given artist / album if it doesn't exist
 * to the artist and album list of the current host
 */
static void artistalbumview_add(daap_host *host,
                                char *artist_s, char *album_s);
/* gets the songs from the first database (FIXME use multi databases?)
 * and sorts them.
 * update_databases must be called first.
 */
static void update_songs(daap_host *host);

static void update_playlists(daap_host *host);
/**** daap utility functions used by the rest of tb *****/
void daap_host_addref(daap_host *host);

static int get_songindex_by_id(daap_host *host, int song_id);

/******************* daap music playing *****************/

enum playsource
{
    PLAYSOURCE_NONE = 0,
    PLAYSOURCE_HOST,
    PLAYSOURCE_PARTY
};


void daap_host_play_song(enum playsource playsource, daap_host *host, int song_id);

/* callbacks */
void on_song_row_activated();
/* connects to the new daap host,
 * downloads database, etc
 */
daap_host* getVisibleHost();
int get_next_song();
void initial_connect(daap_host *host);

static void disconnect_host(daap_host *host);
/**** daap utility functions used by the rest of tb *****/

void daap_host_release(daap_host *host);
void daap_host_set_visible(daap_host *host, playlist *playlist);


static daap_host *visibleHost;
static playlist *visiblePlaylist;

static DAAP_SClient *clientInstance;
static daap_host *clientHosts;


static DAAP_Status prev_libopendaap_status;

static int songstarted;
static int playing;
static int songpipe[2];

enum stop_type
{
    STOP_NONE = 0,
    STOP_NEWSONG,
    STOP_HOSTDEAD,
    STOP_ERROR
};
static enum stop_type stop_type = STOP_NONE;


typedef struct
{
    enum playsource playsource;
    daap_host *host;
    int song_id;
} song_ref;
void daap_audiocb_finished();

static song_ref next_song;
static song_ref playing_song;

album* get_new_album();
artist* get_new_artist();

artist *daap_host_get_next_artist(daap_host *host, artist *curr);
album *daap_host_get_next_album(daap_host *host, album *curr);

mode_t mode;
mmr_connection_t *conn;
mmr_context_t *ctxt;
int audio_oid;

artist *daap_host_enum_artists(daap_host *host, artist *prev);
album *daap_host_enum_album(artist *artist, album *prev);
artist *daap_host_get_selected_artist(daap_host *host);
album *daap_host_get_selected_album(daap_host *host);
void daap_host_set_selected_artist(daap_host *host, artist *artist);
void daap_host_set_selected_album(daap_host *host, album *album);
char *daap_host_get_artistname(artist *artist);
char *daap_host_get_albumname(album *album);
int daap_host_enum_artist_album_songs(daap_host *host,
                                      DAAP_ClientHost_DatabaseItem *song,
                                      int prev_id,
                                      artist *artist, album *album);


#endif /* DAAPFUNC_H_ */
