/*
 * Reasons to restart main loop.
 */
#define RESTART_NEW  1
#define RESTART_LOAD 2
#define RESTART_UNDO 3
#define RESTART_NONE 4

/*
 * User options.
 */
typedef struct options
{
	/* Number of players */
	int num_players;

	/* Expansion level */
	int expanded;

	/* Two-player advanced game */
	int advanced;

	/* Disable goals */
	int disable_goal;

	/* Disable takeovers */
	int disable_takeover;

	/* Reduce/eliminate full-size card image */
	int full_reduced;

	/* Shrink opponent areas to fit without scrolling */
	int shrink_opponent;

	/* Server name to connect to */
	char *server_name;

	/* Server port */
	int server_port;

	/* Username to connect as */
	char *username;

	/* Password */
	char *password;

} options;

extern options opt;
extern GtkListStore *user_list;
extern GtkTreeStore *game_list;
extern GtkWidget *entry_label, *chat_view, *password_entry;
extern GtkWidget *games_view;
extern GtkWidget *create_button, *join_button, *leave_button;
extern GtkWidget *kick_button, *start_button;
extern GtkWidget *action_prompt, *action_button;
extern char *exp_names[];
extern game real_game;
extern int player_us;
extern int restart_loop;

extern int client_state;
extern int waiting_player[MAX_PLAYER];

extern void save_prefs(void);
extern void reset_cards(game *g, int color_hand, int color_table);
extern void redraw_hand(void);
extern void redraw_table(void);
extern void redraw_status(void);
extern void redraw_goal(void);
extern void redraw_phase(void);
extern void redraw_everything(void);
extern void modify_gui(void);
extern void reset_gui(void);
extern void switch_view(int lobby, int chat);

extern void game_view_changed(GtkTreeView *view, gpointer data);
extern void send_chat(GtkEntry *entry, gpointer data);
extern void connect_dialog(GtkMenuItem *menu_item, gpointer data);
extern void disconnect_server(GtkMenuItem *menu_item, gpointer data);
extern void create_dialog(GtkButton *button, gpointer data);
extern void join_game(GtkButton *button, gpointer data);
extern void leave_game(GtkButton *button, gpointer data);
extern void start_game(GtkButton *button, gpointer data);
extern void kick_player(GtkButton *button, gpointer data);
