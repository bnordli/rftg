/*
 * Reasons to restart main loop.
 */
#define RESTART_NEW        1
#define RESTART_NONE       2
#define RESTART_LOAD       3
#define RESTART_RESTORE    4
#define RESTART_UNDO       5
#define RESTART_UNDO_ROUND 6
#define RESTART_UNDO_GAME  7
#define RESTART_REDO       8
#define RESTART_REDO_ROUND 9
#define RESTART_REDO_GAME  10
#define RESTART_REPLAY     11
#define RESTART_CURRENT    12

/*
 * Column ids of lobby game list.
 */
#define GAME_COL_ID                0
#define GAME_COL_DESC_NAME         1
#define GAME_COL_DESC_NAME_CMP     2
#define GAME_COL_CREATOR_OFFLINE   3
#define GAME_COL_CREATOR_CMP       4
#define GAME_COL_PASSWORD          5
#define GAME_COL_MIN_PLAYERS       6
#define GAME_COL_MAX_PLAYERS       7
#define GAME_COL_PLAYERS_STR       8
#define GAME_COL_EXPANSION         9
#define GAME_COL_EXPANSION_STR    10
#define GAME_COL_ADVANCED         11
#define GAME_COL_DISABLE_GOAL     12
#define GAME_COL_DISABLE_TO       13
#define GAME_COL_NO_TIMEOUT       14
#define GAME_COL_SELF             15
#define GAME_COL_CHECK_VISIBLE    16
#define GAME_COL_WEIGHT           17
#define GAME_MAX_COLUMN           18

/*
 * Column ids of lobby player list.
 */
#define PLAYER_COL_USERNAME        0
#define PLAYER_COL_USERNAME_CMP    1
#define PLAYER_COL_IN_GAME         2
#define PLAYER_COL_WEIGHT          3
#define PLAYER_MAX_COLUMN          4

/*
 * Column ids of debug cards list.
 */
#define DEBUG_COL_CARD_ID          0
#define DEBUG_COL_CARD_NAME        1
#define DEBUG_COL_OWNER            2
#define DEBUG_COL_LOCATION         3
#define DEBUG_MAX_COLUMN           4

/*
 * User options.
 */
typedef struct options
{
	/* Number of players */
	int num_players;

	/* Expansion level */
	int expanded;

	/* Player name */
	char *player_name;

	/* Two-player advanced game */
	int advanced;

	/* Promo cards */
	int promo;

	/* Disable goals */
	int disable_goal;

	/* Disable takeovers */
	int disable_takeover;

	/* Customize seed */
	int customize_seed;

	/* Seed value */
	unsigned int seed;

	/* Hide/show card preview (For legacy reasons: 0: Show, 2: Hide) */
	int hide_card;

	/* Campaign name */
	char *campaign_name;

	/* Reduce/eliminate full-size card image */
	int full_reduced;

	/* Shrink opponent areas to fit without scrolling */
	int shrink_opponent;

	/* Display the settle discount icon */
	int settle_discount;

	/* Display the VP value for cards in hand */
	int vp_in_hand;

	/* Display cost for cards during placement */
	int cost_in_hand;

	/* Always display key cues */
	int key_cues;

	/* Auto select forced choices */
	int auto_select;

	/* Server name to connect to */
	char *server_name;

	/* Server port */
	int server_port;

	/* Previous server names */
	GtkListStore *servers;

	/* Username to connect as */
	char *username;

	/* Password */
	char *password;

	/* Export style sheet */
	char *export_style_sheet;

	/* Game description when creating */
	char *game_desc;

	/* Game password when creating */
	char *game_pass;

	/* Number of players in multiplayer */
	int multi_min;
	int multi_max;

	/* Card size */
	int card_size;

	/* Log width */
	int log_width;

	/* Autosave */
	int auto_save;

	/* Export card locations */
	int export_cards;

	/* Export game at end of game */
	int auto_export;

	/* Verbose log */
	int verbose_log;

	/* Draw log */
	int draw_log;

	/* Discard log */
	int discard_log;

	/* Last save location */
	char *last_save;

	/* Data folder (autosave) location */
	char *data_folder;

	/* Export folder location */
	char *export_folder;

} options;

extern options opt;
extern GtkListStore *user_list;
extern GtkTreeStore *game_list;
extern GtkWidget *entry_label, *chat_view, *password_entry;
extern GtkWidget *games_view;
extern GtkWidget *create_button, *join_button, *leave_button;
extern GtkWidget *kick_button, *addai_button, *start_button;
extern GtkWidget *action_prompt, *action_button;
extern GtkWidget *message_view;
extern GtkTextMark *message_end;
extern char *exp_names[];
extern game real_game;
extern int player_us;
extern int restart_loop;

extern int client_state, playing_game, making_choice;
extern char server_version[30];
extern int debug_server;
extern int waiting_player[MAX_PLAYER];

extern char *create_cmp_key(char *str);
extern void save_prefs(void);
extern void reset_cards(game *g, int color_hand, int color_table);
extern void reset_status(game *g, int who);
extern void redraw_hand(void);
extern void redraw_table(void);
extern void redraw_status(void);
extern void redraw_goal(void);
extern void redraw_phase(void);
extern void redraw_everything(void);
extern void modify_gui(int reset_card);
extern void reset_gui(void);
extern void switch_view(int lobby, int chat);
extern void update_menu_items();

extern void game_view_changed(GtkTreeView *view, gpointer data);
extern void send_chat(GtkEntry *entry, gpointer data);
extern void connect_dialog(GtkMenuItem *menu_item, gpointer data);
extern void disconnect_server(GtkMenuItem *menu_item, gpointer data);
extern void resign_game(GtkMenuItem *menu_item, gpointer data);
extern void create_dialog(GtkButton *button, gpointer data);
extern void join_game(GtkButton *button, gpointer data);
extern void leave_game(GtkButton *button, gpointer data);
extern void start_game(GtkButton *button, gpointer data);
extern void kick_player(GtkButton *button, gpointer data);
extern void add_ai_player(GtkButton *button, gpointer data);
