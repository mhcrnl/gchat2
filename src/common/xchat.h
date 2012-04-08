
#include <glib/gslist.h>
#include <glib/glist.h>
#include <glib/gutils.h>
#include <glib/giochannel.h>
#include <glib/gstrfuncs.h>
#include <time.h>			/* need time_t */
#include <glib.h>

#ifndef XCHAT_H
#define XCHAT_H

#ifdef _MSC_VER
# pragma warning (disable: 4996)
# pragma warning (disable: 4005)
#endif

#include "stdinc.h"
#include "debug.h"
#include "history.h"
#include "signal_factory.h"
#include "signal_printer.h"
#include "cap.h"
#include "linequeue.h"

#ifdef _CONSPIRE_INTERNAL
#include "conspire-config.h"

#ifndef HAVE_SNPRINTF
#undef snprintf
#define snprintf g_snprintf
#endif

#ifndef HAVE_VSNPRINTF
#undef vsnprintf
#define vsnprintf g_vsnprintf
#endif
#endif

#ifdef GNUTLS
#include <gnutls/gnutls.h>
#endif

#ifdef USE_DEBUG
#define malloc(n) xchat_malloc(n, __FILE__, __LINE__)
#define realloc(n, m) xchat_realloc(n, m, __FILE__, __LINE__)
#define free(n) xchat_dfree(n, __FILE__, __LINE__)
#define strdup(n) xchat_strdup(n, __FILE__, __LINE__)
void *xchat_malloc (int size, char *file, int line);
void *xchat_strdup (char *str, char *file, int line);
void xchat_dfree (void *buf, char *file, int line);
void *xchat_realloc (char *old, int len, char *file, int line);
#endif

#ifdef SOCKS
#ifdef __sgi
#include <sys/time.h>
#define INCLUDE_PROTOTYPES 1
#endif
#include <socks.h>
#endif

#ifdef __EMX__						  /* for o/s 2 */
#define OFLAGS O_BINARY
#define strcasecmp stricmp
#define strncasecmp strnicmp
#define PATH_MAX MAXPATHLEN
#define FILEPATH_LEN_MAX MAXPATHLEN
#endif

/* force a 32bit CMP.L */
#define CMPL(a, c0, c1, c2, c3) (a == (guint32)(c0 | (c1 << 8) | (c2 << 16) | (c3 << 24)))
#define WORDL(c0, c1, c2, c3) (guint32)(c0 | (c1 << 8) | (c2 << 16) | (c3 << 24))
#define WORDW(c0, c1) (guint16)(c0 | (c1 << 8))

#define OFLAGS 0

#define FONTNAMELEN	127
#define PATHLEN		255
#define DOMAINLEN	100
#define NICKLEN		64				/* including the NULL, so 63 really */
#define CHANLEN		300
#define PDIWORDS		32
#define USERNAMELEN 10
#define HIDDEN_CHAR	8			/* invisible character for xtext */

#define DISPLAY_NAME "GChat"

#if defined(ENABLE_NLS) && !defined(_)
#  include <libintl.h>
#  define _(x) gettext(x)
#  ifdef gettext_noop
#    define N_(String) gettext_noop (String)
#  else
#    define N_(String) (String)
#  endif
#endif
#if !defined(_)
#  define N_(String) (String)
#  define _(x) (x)
#endif

#include <mowgli.h>

struct xchatprefs
{
	char *nick1;
	char *nick2;
	char *nick3;
	char *realname;
	char *username;
	char *nick_suffix;			/* Only ever holds a one-character string. */
	char *awayreason;
	char *quitreason;
	char *partreason;
	char *font_normal;
	char *doubleclickuser;
	char *background;
	char *dccdir;
	char *dcc_completed_dir;
	char *irc_extra_hilight;
	char *irc_no_hilight;
	char *irc_nick_hilight;
	char *dnsprogram;
	char *hostname;
	char *cmdchar;
	char *logmask;
	char *stamp_format;
	char *timestamp_log_format;
	char *irc_id_ytext;
	char *irc_id_ntext;
	char *irc_time_format;

	char *text_overflow_start;
	char *text_overflow_stop;
	gint *text_overflow_limit;

	char *proxy_host;
	int proxy_port;
	int proxy_type; /* 0=disabled, 1=wingate 2=socks4, 3=socks5, 4=http */
	int proxy_use; /* 0=all 1=IRC_ONLY 2=DCC_ONLY */
	unsigned int proxy_auth;
	char *proxy_user;
	char *proxy_pass;

	int first_dcc_send_port;
	int last_dcc_send_port;

	int tint_red;
	int tint_green;
	int tint_blue;

	int away_timeout;
	int away_size_max;

	int gui_pane_left_size;
	int gui_pane_right_size;

	int gui_ulist_pos;
	int tab_pos;

	int _tabs_position;
	int tab_layout;
	int max_auto_indent;
	int dcc_blocksize;
	int max_lines;
	int notify_timeout;
	int dcctimeout;
	int dccstalltimeout;
	int dcc_global_max_get_cps;
	int dcc_global_max_send_cps;
	int dcc_max_get_cps;
	int dcc_max_send_cps;
	int mainwindow_left;
	int mainwindow_top;
	int mainwindow_width;
	int mainwindow_height;
	int completion_sort;
	int gui_win_state;
	int gui_url_mod;
	int gui_usermenu;
	int gui_join_dialog;
	int gui_quit_dialog;
	int dialog_left;
	int dialog_top;
	int dialog_width;
	int dialog_height;
	int dccpermissions;
	int recon_delay;
	int bantype;
	int userlist_sort;
	guint32 local_ip;
	guint32 dcc_ip;
	char *dcc_ip_str;

	unsigned int tab_small;
	unsigned int tab_sort;
	unsigned int tab_icons;
	unsigned int mainwindow_save;
	unsigned int perc_color;
	unsigned int perc_ascii;
	unsigned int autosave;
	unsigned int autodialog;
	unsigned int gtk_colors;
	unsigned int autosave_url;
	unsigned int autoreconnect;
	unsigned int autoreconnectonfail;
	unsigned int invisible;
	unsigned int servernotice;
	unsigned int wallops;
	unsigned int skipmotd;
	unsigned int autorejoin;
	unsigned int colorednicks;
	unsigned int coloredhnicks;
	unsigned int chanmodebuttons;
	unsigned int userlistbuttons;
	unsigned int showhostname_in_userlist;
	unsigned int nickcompletion;
	unsigned int completion_amount;
	unsigned int tabchannels;
	unsigned int paned_userlist;
	unsigned int autodccchat;
	unsigned int autodccsend;
	unsigned int autoresume;
	unsigned int autoopendccsendwindow;
	unsigned int autoopendccrecvwindow;
	unsigned int autoopendccchatwindow;
	unsigned int transparent;
	unsigned int stripcolor;
	unsigned int timestamp;
	unsigned int fastdccsend;
	unsigned int dcc_send_fillspaces;
	unsigned int dcc_remove;
	unsigned int slist_select;
	unsigned int filterbeep;

	unsigned int input_balloon_chans;
	unsigned int input_balloon_hilight;
	unsigned int input_balloon_priv;

	unsigned int input_beep_chans;
	unsigned int input_beep_hilight;
	unsigned int input_beep_priv;

	unsigned int input_flash_chans;
	unsigned int input_flash_hilight;
	unsigned int input_flash_priv;

	unsigned int input_tray_chans;
	unsigned int input_tray_hilight;
	unsigned int input_tray_priv;

	unsigned int truncchans;
	unsigned int privmsgtab;
	unsigned int irc_join_delay;
	unsigned int logging;
	unsigned int timestamp_logs;
	unsigned int newtabstofront;
	unsigned int dccwithnick;
	unsigned int hidever;
	unsigned int ip_from_server;
	unsigned int show_away_once;
	unsigned int show_away_message;
	unsigned int auto_unmark_away;
	unsigned int away_track;
	unsigned int userhost;
	unsigned int use_server_tab;
	unsigned int notices_tabs;
	unsigned int style_namelistgad;
	unsigned int style_inputbox;
	unsigned int windows_as_tabs;
	unsigned int indent_nicks;
	unsigned int text_replay;
	unsigned int show_marker;
	unsigned int show_separator;
	unsigned int thin_separator;
	unsigned int auto_indent;
	unsigned int wordwrap;
	unsigned int gui_input_spell;
	unsigned int gui_tray;
	unsigned int gui_tray_flags;
	unsigned int gui_tweak_usercount;
	unsigned int gui_tweak_hidebutton;
	unsigned int gui_tweak_swappanes;
	unsigned int gui_tweak_nolines;
	unsigned int gui_tweak_showkey;
	unsigned int gui_tweak_smallrows;
	unsigned int gui_tweak_useprefixes;
	unsigned int gui_tweak_noattribute;
    unsigned int gui_tweak_ellipse;
	unsigned int _gui_ulist_left;
	unsigned int throttle;
	unsigned int topicbar;
	unsigned int hideuserlist;
	unsigned int hidemenu;
	unsigned int perlwarnings;
	unsigned int lagometer;
	unsigned int throttlemeter;
	unsigned int pingtimeout;
	unsigned int whois_on_notifyonline;
	unsigned int wait_on_exit;
	unsigned int confmode;
	unsigned int utf8_locale;
	unsigned int identd;
	unsigned int skip_serverlist;
#ifdef REGEX_SUBSTITUTION
	unsigned int text_regex_replace;
#endif
	unsigned int ctcp_number_limit;	/*flood */
	unsigned int ctcp_time_limit;	/*seconds of floods */

	unsigned int msg_number_limit;	/*same deal */
	unsigned int msg_time_limit;

	/* Tells us if we need to save, only when they've been edited.
		This is so that we continue using internal defaults (which can
		change in the next release) until the user edits them. */
	unsigned int save_pevents:1;

	gboolean redundant_nickstamps;
	gboolean strip_quits;
        gboolean hilight_enable;

	unsigned int gui_transparency;
};

/* Session types */
#define SESS_SERVER	1
#define SESS_CHANNEL	2
#define SESS_DIALOG	3
#define SESS_NOTICES	4
#define SESS_SNOTICES	5

/* Priorities in the "interesting sessions" priority queue
 * (see xchat.c:sess_list_by_lastact) */
#define LACT_NONE		-1		/* no queues */
#define LACT_QUERY_HI	0		/* query with hilight */
#define LACT_QUERY		1		/* query with messages */
#define LACT_CHAN_HI	2		/* channel with hilight */
#define LACT_CHAN		3		/* channel with messages */
#define LACT_CHAN_DATA	4		/* channel with other data */

typedef struct session
{
	struct server *server;
	void *usertree;
	void *usertree_alpha;
	struct User *me;					/* points to myself in the usertree */
	char channel[CHANLEN];
	char waitchannel[CHANLEN];		  /* waiting to join channel (/join sent) */
	char willjoinchannel[CHANLEN];	  /* will issue /join for this channel */
	char channelkey[64];			  /* XXX correct max length? */
	int limit;						  /* channel user limit */
	int logfd;
	int scrollfd;							/* scrollback filedes */
	int scrollwritten;					/* number of lines written */

	char lastnick[NICKLEN];			  /* last nick you /msg'ed */
	GSList *split_list;					/* a list of netsplitted users for this tab */

	struct history history;

	int ops;								/* num. of ops in channel */
	int hops;						  /* num. of half-oped users */
	int voices;							/* num. of voiced people */
	int total;							/* num. of users in channel */

	char *quitreason;
	char *topic;
	char *current_modes;					/* free() me */

	int mode_timeout_tag;

	struct session *lastlog_sess;

	struct session_gui *gui;		/* initialized by fe_new_window */
	struct restore_gui *res;

	int userlisthidden;
	int type; /* SESS_ */

	GList *lastact_elem;    /* our GList element in sess_list_by_lastact */
	int lastact_idx;                /* the sess_list_by_lastact[] index of the list we're in.
					 * For valid values, see defines of LACT_*. */

	int new_data:1;			/* new data avail? (purple tab) */
	int nick_said:1;		/* your nick mentioned? (blue tab) */
	int msg_said:1;			/* new msg available? (red tab) */
	int ignore_date:1;
	int ignore_mode:1;
	int ignore_names:1;
	int end_of_names:1;
	int doing_who:1;		/* /who sent on this channel */
	/* these are in the bottom-right menu */
	unsigned int hide_join_part:1;	/* hide join & part messages? */
	unsigned int beep:1;				/* beep enabled? */
	unsigned int tray:1;				/* tray flash for this chan? */
	unsigned int color_paste:1;
	int done_away_check:1;	/* done checking for away status changes */
	unsigned int lastlog_regexp:1;	/* this is a lastlog and using regexp */
	gboolean immutable;	/* don't close me. */
	gboolean ul_blocked;	/* don't update the GUI */
} session;

#include "session.h"

struct msproxy_state_t
{
	gint32				clientid;
	gint32				serverid;
	unsigned char		seq_recv;		/* seq number of last packet recv.	*/
	unsigned char		seq_sent;		/* seq number of last packet sent.	*/
};

typedef enum {
	SASL_INITIALIZED,
	SASL_AUTHENTICATING,
	SASL_COMPLETE
} SaslState;

typedef struct server
{
	/*  server control operations (in server*.c) */
	void (*connect)(struct server *, char *hostname, int port, int no_login);
	void (*disconnect)(struct session *, int sendquit, int err);
	int  (*cleanup)(struct server *);
	void (*flush_queue)(struct server *);
	void (*auto_reconnect)(struct server *, int send_quit, int err);
	/* irc protocol functions (in proto*.c) */
	void (*p_inline)(struct server *, char *buf, int len);
	void (*p_invite)(struct server *, char *channel, char *nick);
	void (*p_cycle)(struct server *, char *channel, char *key);
	void (*p_ctcp)(struct server *, char *to, char *msg);
	void (*p_nctcp)(struct server *, char *to, char *msg);
	void (*p_quit)(struct server *, char *reason);
	void (*p_kick)(struct server *, char *channel, char *nick, char *reason);
	void (*p_part)(struct server *, char *channel, char *reason);
	void (*p_ns_identify)(struct server *, char *pass);
	void (*p_ns_ghost)(struct server *, char *usname, char *pass);
	void (*p_join)(struct server *, char *channel, char *key);
	void (*p_login)(struct server *, char *user, char *realname);
	void (*p_join_info)(struct server *, char *channel);
	void (*p_mode)(struct server *, char *target, char *mode);
	void (*p_user_list)(struct server *, char *channel);
	void (*p_away_status)(struct server *, char *channel);
	void (*p_whois)(struct server *, char *nicks);
	void (*p_get_ip)(struct server *, char *nick);
	void (*p_get_ip_uh)(struct server *, char *nick);
	void (*p_set_back)(struct server *);
	void (*p_set_away)(struct server *, char *reason);
	void (*p_message)(struct server *, char *channel, char *text);
	void (*p_action)(struct server *, char *channel, char *act);
	void (*p_notice)(struct server *, char *channel, char *text);
	void (*p_topic)(struct server *, char *channel, char *topic);
	void (*p_list_channels)(struct server *, char *arg, int min_users);
	void (*p_change_nick)(struct server *, char *new_nick);
	void (*p_names)(struct server *, char *channel);
	void (*p_ping)(struct server *, char *to, char *timestring);
/*	void (*p_set_away)(struct server *);*/
	int (*p_raw)(struct server *, char *raw);
	int (*p_cmp)(const char *s1, const char *s2);

	int port;
	int sok;					/* is equal to sok4 or sok6 (the one we are using) */
	int sok4;					/* tcp4 socket */
	int sok6;					/* tcp6 socket */
	int proxy_sok;				/* Additional information for MS Proxy beast */
	int proxy_sok4;
	int proxy_sok6;
	struct msproxy_state_t msp_state;
	int id;					/* unique ID number (for plugin API) */
#ifdef GNUTLS
	gnutls_session_t gnutls_session;
	gnutls_certificate_credentials_t gnutls_x509cred;
#endif
	int childread;
	int childwrite;
	int childpid;
	int iotag;
	int recondelay_tag;				/* reconnect delay timeout */
	int joindelay_tag;				/* waiting before we send JOIN */
	char hostname[128];				/* real ip number */
	char servername[128];			/* what the server says is its name */
	char password[86];
	char nick[NICKLEN];
	char linebuf[2048];				/* RFC says 512 chars including \r\n */
	char *last_away_reason;
	int pos;								/* current position in linebuf */
	int nickcount;
	int nickservtype;					/* 0=/MSG nickserv 1=/NICKSERV 2=/NS */

	char *chantypes;					/* for 005 numeric - free me */
	char *chanmodes;					/* for 005 numeric - free me */
	char *nick_prefixes;				/* e.g. "*@%+" */
	char *nick_modes;					/* e.g. "aohv" */
	char *bad_nick_prefixes;		/* for ircd that doesn't give the modes */
	int modes_per_line;				/* 6 on undernet, 4 on efnet etc... */

	void *network;						/* points to entry in servlist.c or NULL! */

	int lag;								/* milliseconds */

	/* netsplit detection. --nenolod */
	guint split_timer;
	char *split_reason;
	char *split_serv1;
	char *split_serv2;

	struct session *front_session;	/* front-most window/tab */
	struct session *server_session;	/* server window/tab */

	struct server_gui *gui;		  /* initialized by fe_new_server */

	unsigned int ctcp_counter;	  /*flood */
	time_t ctcp_last_time;

	unsigned int msg_counter;	  /*counts the msg tab opened in a certain time */
	time_t msg_last_time;

	/*time_t connect_time;*/				/* when did it connect? */
	time_t lag_sent;
	time_t ping_recv;					/* when we last got a ping reply */
	time_t away_time;					/* when we were marked away */

	char *encoding;					/* NULL for system */

	int motd_skipped:1;
	unsigned int connected:1;
	unsigned int connecting:1;
	int no_login:1;
	int skip_next_userhost:1;			/* used for "get my ip from server" */
	int inside_whois:1;
	int doing_dns:1;				/* /dns has been done */
	unsigned int end_of_motd:1;	/* end of motd reached (logged in) */
	int sent_quit:1;				/* sent a QUIT already? */
	int use_listargs:1;			/* undernet and dalnet need /list >0,<10000 */
	unsigned int is_away:1;
	int reconnect_away:1;		/* whether to reconnect in is_away state */
	int dont_use_proxy:1;		/* to proxy or not to proxy */
	int supports_watch:1;		/* supports the WATCH command */
	int supports_monitor:1;		/* supports the MONITOR command */
	int inside_monitor:1;
	int bad_prefix:1;				/* gave us a bad PREFIX= 005 number */
	unsigned int have_namesx:1;	/* 005 tokens NAMESX and UHNAMES */
	unsigned int have_uhnames:1;
	unsigned int have_whox:1;	/* have undernet's WHOX features */
	unsigned int have_capab:1;	/* supports CAPAB (005 tells us) */
	unsigned int have_idmsg:1;	/* freenode's IDENTIFY-MSG */
	unsigned int have_except:1;	/* ban exemptions +e */
	unsigned int using_cp1255:1;	/* encoding is CP1255/WINDOWS-1255? */
	unsigned int using_irc:1;		/* encoding is "IRC" (CP1252/UTF-8 hybrid)? */
	int use_who:1;				/* whether to use WHO command to get dcc_ip */
#ifdef GNUTLS
	int use_ssl:1;					  /* is server SSL capable? */
	int accept_invalid_cert:1;	  /* ignore result of server's cert. verify */
#endif

	char *sasl_user;
	char *sasl_pass;
	SaslState sasl_state;
	int sasl_timeout_tag;
	CapState *cap;
	LineQueue *lq;
} server;

typedef int (*cmd_callback) (struct session * sess, char *tbuf, char *word[],
									  char *word_eol[]);

#include "command_factory.h"

struct commands
{
	char *name;
	CommandHandler callback;
	char needserver;
	char needchannel;
	gint16 handle_quotes;
	char *help;
};

struct away_msg
{
	struct server *server;
	char nick[NICKLEN];
	char *message;
};

/* not just for popups, but used for usercommands, ctcp replies,
   userlist buttons etc */

struct popup
{
	char *cmd;
	char *name;
};

/*
 * Regular expressions-driven substitution support, currently only for
 * outbound messages
 */

#ifdef REGEX_SUBSTITUTION
struct regex_entry
{
	char *cmd;
	char *name;
	GRegex *regex;
};
#endif

/* CL: get a random int in the range [0..n-1]. DON'T use rand() % n, it gives terrible results. */
#define RAND_INT(n) ((int)(rand() / (RAND_MAX + 1.0) * (n)))

#define xchat_filename_from_utf8 g_filename_from_utf8
#define xchat_filename_to_utf8 g_filename_to_utf8

#endif
