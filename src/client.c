/*
 * Race for the Galaxy AI
 *
 * Copyright (C) 2009-2011 Keldon Jones
 *
 * Source file modified by B. Nordli, August 2014.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <gtk/gtk.h>
#include "rftg.h"
#include "comm.h"
#include "client.h"

/*
 * File descriptor of connection to server.
 */
static int server_fd = -1;

/*
 * I/O source for connection to server.
 */
static GSource *server_src;

/*
 * Our current connection state.
 */
int client_state = CS_DISCONN;

/*
 * The version of the server we are connected to.
 */
char server_version[30];

/*
 * Whether the connected server accepts debug choices.
 */
int debug_server;

/*
 * Our joined session ID.
 */
static int client_sid = -1;

/*
 * Set when the connect dialog is closed.
 */
static int connect_dialog_closed;

/*
 * We are currently playing in a game.
 */
static int playing_game;

/*
 * Widgets used in multiple functions.
 */
static GtkWidget *login_status;

/*
 * We are currently making a choice.
 */
static int making_choice;

/*
 * Prevent displayed card updates until server catches up with us.
 */
static int prevent_update, prevent_phase;

/*
 * Various things have updated since last redraw.
 */
static int cards_updated, status_updated;

/*
 * Waiting status for each player.
 */
int waiting_player[MAX_PLAYER];

/*
 * Forward declaration.
 */
static void disconnect(void);

/*
 * Send message to server.
 */
void send_msg(int fd, char *msg)
{
	struct timeval timeout;
	int size, sent = 0, x;
	char *ptr;

	/* Go to size area of message */
	ptr = msg + 4;

	/* Read size */
	size = get_integer(&ptr);

	/* Send until finished */
	while (sent < size)
	{
		/* Write as much as possible */
		x = send(fd, msg + sent, size - sent, 0);

		/* Check for errors */
		if (x < 0)
		{
			/* Check for broken pipe */
			if (errno == EPIPE) return;

			/* Check for try again */
			if (errno == EAGAIN)
			{
				/* Create timeout */
				timeout.tv_sec = 0;
				timeout.tv_usec = 1000;

				/* Wait a bit */
				select(0, NULL, NULL, NULL, &timeout);

				/* Try again */
				continue;
			}

			/* Error */
			perror("send");
			disconnect();
			return;
		}

		/* Count bytes sent */
		sent += x;
	}
}

/*
 * Delete row from user list if it matches passed-in data.
 */
static gboolean delete_user(GtkTreeModel *model, GtkTreePath *path,
                            GtkTreeIter *iter, gpointer data)
{
	char *ptr;

	/* Get first column */
	gtk_tree_model_get(model, iter, 0, &ptr, -1);

	/* Check for match */
	if (!strcmp(ptr, (char *)data))
	{
		/* Delete row */
		gtk_list_store_remove(GTK_LIST_STORE(model), iter);

		/* Free copy of string */
		g_free(ptr);

		/* Found a match */
		return TRUE;
	}

	/* Free copy of string */
	g_free(ptr);

	/* No match, keep looking */
	return FALSE;
}

/*
 * Delete row from game list if it matches passed-in session ID.
 */
static gboolean delete_game(GtkTreeModel *model, GtkTreePath *path,
                            GtkTreeIter *iter, gpointer data)
{
	int x = GPOINTER_TO_INT(data);
	int y;

	/* Get first column */
	gtk_tree_model_get(model, iter, 0, &y, -1);

	/* Check for match */
	if (x == y)
	{
		/* Delete row */
		gtk_tree_store_remove(GTK_TREE_STORE(model), iter);

		/* Found a match */
		return TRUE;
	}

	/* No match, keep looking */
	return FALSE;
}

/*
 * Destroy all entries in game and user lists.
 */
static void clear_games_users(void)
{
	GtkTreeIter iter;

	/* Get first entry of user list */
	if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(user_list), &iter))
	{
		/* Remove rows */
		while (gtk_list_store_remove(user_list, &iter));
	}

	/* Get first entry of game list */
	if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(game_list), &iter))
	{
		/* Remove rows */
		while (gtk_tree_store_remove(game_list, &iter));
	}
}

/*
 * Expansion name abbreviations.
 */
static char *exp_abbr[MAX_EXPANSION] =
{
	"Base",
	"TGS",
	"RvI",
	"BoW"
};

/*
 * Find the given session ID in the game list.
 *
 * Return false if the given ID does not exist.
 */
static int find_game_iter(int id, GtkTreeIter *iter)
{
	int x;

	/* Get first row in tree */
	if (!gtk_tree_model_get_iter_first(GTK_TREE_MODEL(game_list), iter))
	{
		/* No rows */
		return 0;
	}

	/* Loop until match found or no more rows */
	while (1)
	{
		/* Get first column */
		gtk_tree_model_get(GTK_TREE_MODEL(game_list), iter, 0, &x, -1);

		/* Check for match */
		if (x == id) return 1;

		/* Go to next row */
		if (!gtk_tree_model_iter_next(GTK_TREE_MODEL(game_list), iter))
		{
			/* No more rows */
			return 0;
		}
	}
}

/*
 * Find a player in the given game.
 */
static int find_game_player(GtkTreeIter *parent, GtkTreeIter *child, int who)
{
	int x;

	/* Get first child of parent */
	if (!gtk_tree_model_iter_children(GTK_TREE_MODEL(game_list), child,
	                                  parent))
	{
		/* No match */
		return 0;
	}

	/* Loop until match found or no more rows */
	while (1)
	{
		/* Get player index column */
		gtk_tree_model_get(GTK_TREE_MODEL(game_list), child, 0, &x, -1);

		/* Check for match */
		if (x == who) return 1;

		/* Go to next row */
		if (!gtk_tree_model_iter_next(GTK_TREE_MODEL(game_list), child))
		{
			/* No matches */
			return 0;
		}
	}
}

/*
 * The current selection in the games list was changed.
 */
void game_view_changed(GtkTreeView *view, gpointer data)
{
	GtkTreePath *game_path;
	GtkTreeIter game_iter, parent_iter;
	int owned, minp, maxp, user = 1, self, nump;

	/* Check for ability to join game */
	gtk_widget_set_sensitive(join_button, client_sid == -1);

	/* Check for ability to create game */
	gtk_widget_set_sensitive(create_button, client_sid == -1);

	/* Check for ability to leave game */
	gtk_widget_set_sensitive(leave_button, client_sid != -1);

	/* Assume no ability to start game or kick player */
	gtk_widget_set_sensitive(start_button, FALSE);
	gtk_widget_set_sensitive(kick_button, FALSE);
	gtk_widget_set_sensitive(addai_button, FALSE);

	/* Get selected game */
	gtk_tree_view_get_cursor(GTK_TREE_VIEW(games_view), &game_path, NULL);

	/* Check for no selection */
	if (!game_path) return;

	/* Get iterator for path */
	gtk_tree_model_get_iter(GTK_TREE_MODEL(game_list), &game_iter,
	                        game_path);

	/* Free path */
	gtk_tree_path_free(game_path);

	/* Get parent iterator, if any */
	if (!gtk_tree_model_iter_parent(GTK_TREE_MODEL(game_list), &parent_iter,
	                                &game_iter))
	{
		/* Set parent to current cursor location */
		parent_iter = game_iter;

		/* Cursor is not pointing to a user */
		user = 0;
	}

	/* Get game information */
	gtk_tree_model_get(GTK_TREE_MODEL(game_list), &parent_iter,
	                   10, &owned, 12, &minp, 13, &maxp, -1);

	/* Check for user */
	if (user)
	{
		/* Check whether cursor is on ourself */
		gtk_tree_model_get(GTK_TREE_MODEL(game_list), &game_iter,
		                   10, &self, -1);
	}

	/* Get current number of players */
	nump = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(game_list),
	                                      &parent_iter);

	/* Check for ability to start game */
	gtk_widget_set_sensitive(start_button, owned && nump >= minp);

	/* Check for ability to kick player */
	gtk_widget_set_sensitive(kick_button, user && !self && owned);

	/* Check for ability to add AI player */
	gtk_widget_set_sensitive(addai_button, client_sid != -1 && owned &&
	                         nump < maxp);
}

/*
 * Handle a new open game message.
 */
static void handle_open_game(char *ptr)
{
	int x, y, new_game = FALSE;
	char buf[1024];
	GtkTreeIter list_iter;

	/* Read session ID */
	x = get_integer(&ptr);

	/* Look for row in game list */
	if (!find_game_iter(x, &list_iter))
	{
		/* Add new row to game tree */
		gtk_tree_store_append(game_list, &list_iter, NULL);

		/* Set ID in game tree */
		gtk_tree_store_set(game_list, &list_iter, 0, x, -1);

		/* Remember game is new */
		new_game = TRUE;
	}

	/* Read description */
	get_string(buf, &ptr);

	/* Set description */
	gtk_tree_store_set(game_list, &list_iter, 1, buf, -1);

	/* Read creator username */
	get_string(buf, &ptr);

	/* Set creator */
	gtk_tree_store_set(game_list, &list_iter, 2, buf, -1);

	/* Read password required */
	x = get_integer(&ptr);

	/* Set password required */
	gtk_tree_store_set(game_list, &list_iter, 3, x, -1);

	/* Read min/max number of players */
	x = get_integer(&ptr);
	y = get_integer(&ptr);

	/* Create string for min/max */
	if (x != y)
	{
		/* Create string with range */
		sprintf(buf, "%d-%d", x, y);
	}
	else
	{
		/* Create string with one number */
		sprintf(buf, "%d", x);
	}

	/* Set number of players */
	gtk_tree_store_set(game_list, &list_iter, 4, buf, 12, x, 13, y, -1);

	/* Read expansion level */
	x = get_integer(&ptr);

	/* Set expansion string */
	gtk_tree_store_set(game_list, &list_iter, 5, exp_abbr[x], -1);

	/* Read two-player advanced option */
	x = get_integer(&ptr);

	/* Set advanced option */
	gtk_tree_store_set(game_list, &list_iter, 6, x, -1);

	/* Read disable options */
	x = get_integer(&ptr);
	y = get_integer(&ptr);

	/* Set disable options */
	gtk_tree_store_set(game_list, &list_iter, 7, x, 8, y, -1);

	/* Read game speed option */
	x = get_integer(&ptr);

	/* Set speed option */
	gtk_tree_store_set(game_list, &list_iter, 9, x, -1);

	/* Read owner flag */
	x = get_integer(&ptr);

	/* Set owner information */
	gtk_tree_store_set(game_list, &list_iter, 10, x, -1);

	/* Check for owner */
	if (x && new_game)
	{
		/* Set the cursor at the new game */
		gtk_tree_view_set_cursor(
			GTK_TREE_VIEW(games_view),
			gtk_tree_model_get_path(GTK_TREE_MODEL(game_list), &list_iter),
			NULL, FALSE);
	}

	/* Make checkboxes visible */
	gtk_tree_store_set(game_list, &list_iter, 11, 1, -1);

	/* Sort game list by session ID */
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(game_list), 0,
	                                     GTK_SORT_ASCENDING);

	/* Reset button state */
	game_view_changed(GTK_TREE_VIEW(games_view), NULL);
}

/*
 * Handle a message about a player who has joined a game.
 */
static void handle_game_player(char *ptr)
{
	int x, y;
	char buf[1024];
	GtkTreeIter list_iter, child_iter;

	/* Read session ID */
	x = get_integer(&ptr);

	/* Read player spot */
	y = get_integer(&ptr);

	/* Read user name */
	get_string(buf, &ptr);

	/* Find game ID */
	find_game_iter(x, &list_iter);

	/* Check for player already in game */
	if (!find_game_player(&list_iter, &child_iter, y))
	{
		/* Check for no player to add */
		if (!strlen(buf)) return;

		/* Add a child row */
		gtk_tree_store_append(game_list, &child_iter, &list_iter);
	}

	/* Check for deleting player */
	if (!strlen(buf))
	{
		/* Delete row */
		gtk_tree_store_remove(game_list, &child_iter);

		/* Reset button state */
		game_view_changed(GTK_TREE_VIEW(games_view), NULL);

		/* Done */
		return;
	}

	/* Set player number */
	gtk_tree_store_set(game_list, &child_iter, 0, y, -1);

	/* Set username */
	gtk_tree_store_set(game_list, &child_iter, 1, buf, -1);

	/* Get online status */
	x = get_integer(&ptr);

	/* Create online string */
	strcpy(buf, x ? "" : "(offline)");

	/* Set online status */
	gtk_tree_store_set(game_list, &child_iter, 2, buf, -1);

	/* Get self flag */
	x = get_integer(&ptr);

	/* Store note of self */
	gtk_tree_store_set(game_list, &child_iter, 10, x, -1);

	/* Make checkboxes invisible on this row */
	gtk_tree_store_set(game_list, &child_iter, 11, 0, -1);

	/* Reset button state */
	game_view_changed(GTK_TREE_VIEW(games_view), NULL);
}

/*
 * Handle a message about game parameters.
 */
static void handle_status_meta(char *ptr, int size)
{
	char name[1024];
	int i;

	/* Read basic game parameters */
	real_game.num_players = get_integer(&ptr);
	real_game.expanded = get_integer(&ptr);
	real_game.advanced = get_integer(&ptr);
	real_game.goal_disabled = get_integer(&ptr);
	real_game.takeover_disabled = get_integer(&ptr);

	/* Clear campaign */
	real_game.camp = NULL;

	/* Initialize card designs for this expansion level */
	init_game(&real_game);

	/* Load AI neural networks for this game */
	ai_func.init(&real_game, 0, 0);

	/* Loop over goals */
	for (i = 0; i < MAX_GOAL; i++)
	{
		/* Read goal presence */
		real_game.goal_active[i] = get_integer(&ptr);
	}

	/* Loop over players */
	for (i = 0; i < real_game.num_players; i++)
	{
		/* Read player name */
		get_string(name, &ptr);

		/* Copy name */
		real_game.p[i].name = strdup(name);
	}

	/* Loop over players */
	for (i = 0; i < real_game.num_players; i++)
	{
		/* Copy ai flag */
		real_game.p[i].ai = get_integer(&ptr);
	}

	/* Redraw status areas */
	redraw_status();
	redraw_goal();

	/* Modify GUI for expansion and number of players */
	modify_gui(TRUE);
}

/*
 * Handle a status update about a player.
 */
static void handle_status_player(char *ptr, int size)
{
	player *p_ptr;
	int x;

	/* Get player index */
	x = get_integer(&ptr);

	/* Get player pointer */
	p_ptr = &real_game.p[x];

	/* Read actions */
	p_ptr->action[0] = get_integer(&ptr);
	p_ptr->action[1] = get_integer(&ptr);

	/* Read prestige action used flag */
	p_ptr->prestige_action_used = get_integer(&ptr);

	/* Loop over goals */
	for (x = 0; x < MAX_GOAL; x++)
	{
		/* Read goal claimed */
		p_ptr->goal_claimed[x] = get_integer(&ptr);

		/* Real goal progress */
		p_ptr->goal_progress[x] = get_integer(&ptr);
	}

	/* Read player's prestige count */
	p_ptr->prestige = get_integer(&ptr);

	/* Read player's VP count */
	p_ptr->vp = get_integer(&ptr);

	/* Read player's phase bonuses */
	p_ptr->phase_bonus_used = get_integer(&ptr);
	p_ptr->bonus_military = get_integer(&ptr);
	p_ptr->bonus_reduce = get_integer(&ptr);

	/* Copy prestige information */
	p_ptr->prestige_turn = get_integer(&ptr);

	/* Redraw status information later */
	status_updated = 1;
}

/*
 * Handle a status update about a card.
 */
static void handle_status_card(char *ptr, int size)
{
	card *c_ptr;
	int x;
	int owner, where, start_owner, start_where;

	/* Read card index */
	x = get_integer(&ptr);

	/* Get card pointer */
	c_ptr = &real_game.deck[x];

	/* Read card owner */
	owner = get_integer(&ptr);
	start_owner = get_integer(&ptr);

	/* Read card location */
	where = get_integer(&ptr);
	start_where = get_integer(&ptr);

	/* Move card to current location */
	move_card(&real_game, x, owner, where);

	/* Move "start of phase" location */
	move_start(&real_game, x, start_owner, start_where);

	/* Read card flags */
	c_ptr->misc = get_integer(&ptr);

	/* Read order played */
	c_ptr->order = get_integer(&ptr);

	/* Read number of goods */
	c_ptr->num_goods = get_integer(&ptr);

	/* Read covered card */
	c_ptr->covering = get_integer(&ptr);

	/* Card locations have been updated */
	cards_updated = 1;
	status_updated = 1;

	/* Track latest played card */
	if (c_ptr->owner >= 0 &&
	    c_ptr->order > real_game.p[c_ptr->owner].table_order)
	{
		/* Update order */
		real_game.p[c_ptr->owner].table_order = c_ptr->order;
	}
}

/*
 * Handle a goal status update.
 */
static void handle_status_goal(char *ptr)
{
	int i;

	/* Loop over goals */
	for (i = 0; i < MAX_GOAL; i++)
	{
		/* Read goal availability and progress */
		real_game.goal_avail[i] = get_integer(&ptr);
		real_game.goal_most[i] = get_integer(&ptr);
	}

	/* Redraw goal area */
	redraw_goal();
}

/*
 * Handle a miscellaneous status update.
 */
static void handle_status_misc(char *ptr)
{
	int i;

	/* Read round number */
	real_game.round = get_integer(&ptr);

	/* Read VP pool size */
	real_game.vp_pool = get_integer(&ptr);

	/* Loop over actions */
	for (i = 0; i < MAX_ACTION; i++)
	{
		/* Read action selected flag */
		real_game.action_selected[i] = get_integer(&ptr);
	}

	/* Read current action */
	real_game.cur_action = get_integer(&ptr);

	/* Check for server catching up */
	if (prevent_update && real_game.cur_action != prevent_phase)
	{
		/* Allow updates */
		prevent_update = 0;
	}

	/* Redraw phase status */
	redraw_phase();

	/* Check for locked hand/table areas */
	if (!prevent_update && !making_choice && cards_updated)
	{
		/* Reset cards in hand/table area */
		reset_cards(&real_game, TRUE, TRUE);

		/* Redraw hand and table areas */
		redraw_table();
		redraw_hand();

		/* Clear cards updated flag */
		cards_updated = 0;
	}

	/* Check for update in status */
	if (status_updated)
	{
		/* Loop over players */
		for (i = 0; i < real_game.num_players; i++)
		{
			/* Reset status information for player */
			reset_status(&real_game, i);
		}

		/* Redraw status areas */
		redraw_status();

		/* Clear update flag */
		status_updated = 0;
	}
}

/*
 * Handle message about waiting players.
 */
static void handle_waiting(char *ptr)
{
	int i, waiting_for_server = TRUE;
	char *msg;

	/* Loop over players */
	for (i = 0; i < real_game.num_players; i++)
	{
		/* Get wait status */
		waiting_player[i] = get_integer(&ptr);

		/* Check if we are waiting for the player */
		if (i != player_us && waiting_player[i] == WAIT_BLOCKED)
		{
			/* Remember we are waiting for a player */
			waiting_for_server = FALSE;
		}
	}

	/* Don't update the prompt if the user is currently making a choice */
	if (!making_choice)
	{
		/* Select appropriate message */
		if (waiting_for_server) msg = "Waiting for server";
		else if (real_game.num_players == 2) msg = "Waiting for opponent";
		else msg = "Waiting for opponents";

		/* Reset action prompt */
		gtk_label_set_text(GTK_LABEL(action_prompt), msg);
	}

	/* Update status areas */
	redraw_status();
}

/*
 * Server has asked us to make a choice.
 *
 * Ask player and return result.
 */
static void handle_choose(char *ptr)
{
	player *p_ptr;
	char msg[1024];
	int pos, type, num, num_special;
	int list[MAX_DECK], special[MAX_DECK];
	int arg1, arg2, arg3;
	int i;

	/* Allow display updates when asked a direct question */
	prevent_update = 0;

	/* Check for already making choice */
	if (making_choice) return;

	/* Get player pointer */
	p_ptr = &real_game.p[player_us];

	/* Read choice log position expected */
	pos = get_integer(&ptr);

	/* Check for further along in log than we are */
	if (pos > p_ptr->choice_pos)
	{
		/* Adjust current position */
		p_ptr->choice_size = p_ptr->choice_pos = pos;
	}

	/* Check for request for choice we have already made */
	else if (pos < p_ptr->choice_pos)
	{
		/* XXX Do nothing */
		return;
	}

	/* Read choice type */
	type = get_integer(&ptr);

	/* Read number of items in list */
	num = get_integer(&ptr);

	/* Loop over items in list */
	for (i = 0; i < num; i++)
	{
		/* Read list item */
		list[i] = get_integer(&ptr);
	}

	/* Read number of special items in list */
	num_special = get_integer(&ptr);

	/* Loop over special items */
	for (i = 0; i < num_special; i++)
	{
		/* Read special item */
		special[i] = get_integer(&ptr);
	}

	/* Read extra arguments */
	arg1 = get_integer(&ptr);
	arg2 = get_integer(&ptr);
	arg3 = get_integer(&ptr);

	/* Do not update hand/table areas while player is deciding */
	making_choice = 1;

	/* Notify gui */
	gui_client_state_changed(playing_game, making_choice);

	/* Ask player for decision */
	gui_func.make_choice(&real_game, player_us, type, list, &num,
	                     special, &num_special, arg1, arg2, arg3);

	/* Hand/table areas may be redrawn */
	making_choice = 0;

	/* Notify gui */
	gui_client_state_changed(playing_game, making_choice);

	/* Reset hand/table areas to default */
	reset_cards(&real_game, TRUE, TRUE);

	/* Redraw everything */
	redraw_everything();

	/* Check for disconnected */
	if (client_state == CS_DISCONN) return;

	/* Check for resigned */
	if (client_sid == -1) return;

	/* Start reply */
	ptr = msg;

	/* Begin message */
	start_msg(&ptr, MSG_CHOOSE);

	/* Put choice log position */
	put_integer(p_ptr->choice_pos, &ptr);

	/* Copy entries from choice log */
	for (i = p_ptr->choice_pos; i < p_ptr->choice_size; i++)
	{
		/* Copy entry */
		put_integer(p_ptr->choice_log[i], &ptr);
	}

	/* Move current position to end of choice log */
	p_ptr->choice_pos = p_ptr->choice_size;

	/* Finish message */
	finish_msg(msg, ptr);

	/* Send reply */
	send_msg(server_fd, msg);
}

/*
 * Handle a choice request in a simulated game.
 *
 * This function is called by the local rules engine when preparing answers
 * for the current phase in advance of the server asking.  We ask the
 * player to make the choice and send the answer to the server.
 */
static void prepare_make_choice(game *g, int who, int type, int list[], int *nl,
                                int special[], int *ns, int arg1, int arg2,
                                int arg3)
{
	player *p_ptr = &g->p[who];
	char msg[1024], *ptr = msg;
	int i;

	/* Check for random number generator used in simulated game */
	if (g->random_seed != 0 || g->p[who].fake_hand > 0)
	{
		/* Abort preparation */
		g->game_over = 1;
		return;
	}

	/* Ask player using normal GUI function */
	gui_func.make_choice(g, who, type, list, nl, special, ns,
	                     arg1, arg2, arg3);

	/* Check for disconnected or resigned */
	if (client_state == CS_DISCONN || client_sid == -1)
	{
		/* Abort simulated game */
		g->game_over = 1;
		return;
	}

	/* Begin message */
	start_msg(&ptr, MSG_CHOOSE);

	/* Put choice log position */
	put_integer(p_ptr->choice_pos, &ptr);

	/* Copy entries from choice log */
	for (i = p_ptr->choice_pos; i < p_ptr->choice_size; i++)
	{
		/* Copy entry */
		put_integer(p_ptr->choice_log[i], &ptr);
	}

	/* Finish message */
	finish_msg(msg, ptr);

	/* Send to server */
	send_msg(server_fd, msg);
}

/*
 * Control interface used only when preparing answers to predicted questions.
 */
static decisions prepare_func =
{
	NULL,
	NULL,
	NULL,
	prepare_make_choice,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
};

/*
 * Prepare choice respones for the given phase.
 */
static void handle_prepare(char *ptr)
{
	game sim;
	player *p_ptr = &real_game.p[player_us];
	int pos, phase, arg;

	/* Get position in choice log to fill */
	pos = get_integer(&ptr);

	/* Check for further along in log than we are */
	if (pos > p_ptr->choice_pos)
	{
		/* Adjust current position */
		p_ptr->choice_size = p_ptr->choice_pos = pos;
	}

	/* Check for request for choice we have already made */
	else if (pos < p_ptr->choice_pos)
	{
		/* XXX Do nothing */
		return;
	}

	/* Get phase and argument from message */
	phase = get_integer(&ptr);
	arg = get_integer(&ptr);

	/* Make simulated game */
	sim = real_game;
	sim.simulation = 1;
	sim.sim_who = player_us;

	/* Clear random number generator information to detect use */
	sim.random_seed = 0;

	/* Set our control interface to send answers to the server */
	sim.p[player_us].control = &prepare_func;

	/* We are making choices */
	making_choice = 1;

	/* Notify gui */
	gui_client_state_changed(playing_game, making_choice);

	/* Check phase */
	switch (phase)
	{
		/* Develop */
		case PHASE_DEVELOP:

			/* Set our placing argument */
			sim.p[player_us].placing = arg;

			/* Perform develop action */
			develop_action(&sim, player_us, arg);
			break;

		/* Settle */
		case PHASE_SETTLE:

			/* Set our placing argument */
			sim.p[player_us].placing = arg;

			/* Don't prepare for takeovers */
			if (arg == -1) break;

			/* Perform settle action */
			settle_finish(&sim, player_us, arg, 0, -1, 0);
			settle_extra(&sim, player_us, arg);
			break;

		/* Consume */
		case PHASE_CONSUME:

			/* Perform consume phase for ourself */
			consume_player(&sim, player_us);
			break;

		/* Produce */
		case PHASE_PRODUCE:

			/* Perform produce phase for ourself */
			produce_player(&sim, player_us);
			break;
	}

	/* Show preparation results */
	reset_cards(&sim, TRUE, TRUE);

	/* Redraw everything */
	redraw_everything();

	/* Done making choices */
	making_choice = 0;

	/* Notify gui */
	gui_client_state_changed(playing_game, making_choice);

	/* Copy simulated game choice log to real game */
	real_game.p[player_us].choice_size = sim.p[player_us].choice_size;
	real_game.p[player_us].choice_pos = sim.p[player_us].choice_pos;

	/* Check for completed preparation */
	if (!sim.game_over)
	{
		/* Tell server that we have finished */
		send_msgf(server_fd, MSG_PREPARE, "d", phase);
	}

	/* Prevent updates to displayed state until server catches up */
	prevent_update = 1;
	prevent_phase = sim.cur_action;
}

/*
 * A complete message has been read.
 */
static gboolean message_read(gpointer data)
{
	char *ptr = data;
	int type, size;
	char text[1024], format[1024], username[1024];
	GtkTreeIter list_iter;
	GtkTextIter end_iter;
	GtkTextMark *end_mark;
	GtkTextBuffer *chat_buffer;
	GtkWidget *dialog;
	int x;

	/* Read message type and size */
	type = get_integer(&ptr);
	size = get_integer(&ptr);

	/* Check message type */
	switch (type)
	{
		/* Login successful */
		case MSG_HELLO:

			/* Assume no server version */
			strcpy(server_version, "");
			debug_server = 0;

			/* Only new servers send version information */
			if (size > 8)
			{
				/* Get server version */
				get_string(server_version, &ptr);

				/* Check for debug server */
				debug_server = strstr(server_version, "-debug") != NULL;
			}

			/* Set state */
			client_state = CS_LOBBY;

			/* Notify gui */
			gui_client_state_changed(playing_game, making_choice);

			/* Quit from main loop inside connection dialog */
			gtk_main_quit();

			/* Clear session ID */
			client_sid = -1;

			/* Display lobby dialog */
			switch_view(1, 1);

			/* Reset buttons */
			game_view_changed(GTK_TREE_VIEW(games_view), NULL);

			/* XXX Do not free data */
			return FALSE;


		/* Login unsuccessful */
		case MSG_DENIED:

			/* Read denied message */
			get_string(text, &ptr);

			/* Set login status */
			gtk_label_set_text(GTK_LABEL(login_status), text);

			/* Disconnect from server */
			disconnect();

			/* Quit from main loop inside connection dialog */
			gtk_main_quit();
			break;

		/* Server has closed connection */
		case MSG_GOODBYE:

			/* Read message */
			get_string(text, &ptr);

			/* Create alert dialog */
			dialog = gtk_message_dialog_new(NULL,
			                         GTK_DIALOG_DESTROY_WITH_PARENT,
			                         GTK_MESSAGE_ERROR,
			                         GTK_BUTTONS_CLOSE,
			                         "Server closed connection: %s",
			                         text);

			/* Show dialog */
			gtk_widget_show_all(dialog);

			/* Destroy dialog when responded to */
			g_signal_connect_swapped(dialog, "response",
			                         G_CALLBACK(gtk_widget_destroy),
			                         dialog);

			/* Disconnect from server */
			disconnect();
			break;

		/* Server is asking if we still have a working connection */
		case MSG_PING:

			/* Say yes */
			send_msgf(server_fd, MSG_PING, "");
			break;

		/* A player has joined */
		case MSG_PLAYER_NEW:

			/* Get username */
			get_string(username, &ptr);

			/* Get in-game status */
			x = get_integer(&ptr);

			/* Remove any matching users from list */
			gtk_tree_model_foreach(GTK_TREE_MODEL(user_list),
			                       delete_user, username);

			/* Add row to list */
			gtk_list_store_append(user_list, &list_iter);

			/* Set name and status */
			gtk_list_store_set(user_list, &list_iter,
			                   0, username, 1, x, -1);
			break;

		/* A player has left */
		case MSG_PLAYER_LEFT:

			/* Get username */
			get_string(username, &ptr);

			/* Remove user from list */
			gtk_tree_model_foreach(GTK_TREE_MODEL(user_list),
			                       delete_user, username);
			break;

		/* New open game */
		case MSG_OPENGAME:

			/* Handle message */
			handle_open_game(ptr);
			break;

		/* A player in a game */
		case MSG_GAME_PLAYER:

			/* Handle message */
			handle_game_player(ptr);
			break;

		/* Game is closed */
		case MSG_CLOSE_GAME:

			/* Get session ID */
			x = get_integer(&ptr);

			/* Remove from game list */
			gtk_tree_model_foreach(GTK_TREE_MODEL(game_list),
			                       delete_game, GINT_TO_POINTER(x));
			break;

		/* A game has been joined successfully */
		case MSG_JOINACK:

			/* Get session ID */
			x = get_integer(&ptr);

			/* Save session ID */
			client_sid = x;

			/* Reset buttons */
			game_view_changed(GTK_TREE_VIEW(games_view), NULL);
			break;

		/* Failed to join a game */
		case MSG_JOINNAK:

			/* Read message */
			get_string(text, &ptr);

			/* Create alert dialog */
			dialog = gtk_message_dialog_new(NULL,
			                         GTK_DIALOG_DESTROY_WITH_PARENT,
			                         GTK_MESSAGE_ERROR,
			                         GTK_BUTTONS_CLOSE,
			                         "Couldn't join game: %s",
			                         text);

			/* Run dialog */
			gtk_dialog_run(GTK_DIALOG(dialog));

			/* Destroy dialog */
			gtk_widget_destroy(dialog);
			break;

		/* Game has started */
		case MSG_START:

			/* Reset GUI elements */
			reset_gui();

			/* Loop over players */
			for (x = 0; x < MAX_PLAYER; x++)
			{
				/* Clear choice log size */
				real_game.p[x].choice_size = 0;
				real_game.p[x].choice_pos = 0;
			}

			/* Reset prompt */
			gtk_label_set_text(GTK_LABEL(action_prompt),
			                   "Waiting for server");

			/* Client can be updated */
			prevent_update = 0;

			/* Revert to game view */
			switch_view(0, 1);

			/* Mark game as being played */
			playing_game = 1;

			/* Notify gui */
			gui_client_state_changed(playing_game, making_choice);

			break;

		/* We have been removed from a game */
		case MSG_LEAVE:

			/* Clear session ID */
			client_sid = -1;

			/* Reset buttons */
			game_view_changed(GTK_TREE_VIEW(games_view), NULL);
			break;

		/* Game log message */
		case MSG_LOG:

			/* Read message */
			get_string(text, &ptr);

			/* Add message to log */
			message_add(&real_game, text);
			break;

		/* Formatted game log message */
		case MSG_LOG_FORMAT:

			/* Read message */
			get_string(text, &ptr);

			/* Read format tag */
			get_string(format, &ptr);

			/* Add formatted message to log */
			message_add_formatted(&real_game, text, format);
			break;

		/* Received in-game chat message */
		case MSG_GAMECHAT:

			/* Read username */
			get_string(username, &ptr);

			/* Add colon to displayed username */
			if (strlen(username) > 0) strcat(username, ": ");

			/* Read text of message */
			get_string(text, &ptr);

			/* Add newline to message */
			strcat(text, "\n");

			/* Get chat buffer */
			chat_buffer = gtk_text_view_get_buffer(
			                           GTK_TEXT_VIEW(message_view));

			/* Get end mark */
			gtk_text_buffer_get_iter_at_mark(chat_buffer, &end_iter,
			                                 message_end);

			/* Add username */
			gtk_text_buffer_insert_with_tags_by_name(chat_buffer,
			                                 &end_iter,
			                                 username, -1, FORMAT_CHAT,
			                                 NULL);

			/* Get end mark */
			gtk_text_buffer_get_iter_at_mark(chat_buffer, &end_iter,
			                                 message_end);

			/* Check for message from server */
			if (!strlen(username))
			{
				/* Add text (emphasized) */
				gtk_text_buffer_insert_with_tags_by_name(
				                                 chat_buffer,
				                                 &end_iter,
				                                 text, -1,
				                                 FORMAT_CHAT,
				                                 NULL);
			}
			else
			{
				/* Add text */
				gtk_text_buffer_insert(chat_buffer, &end_iter,
				                       text, -1);
			}

			/* Scroll to end mark */
			gtk_text_view_scroll_mark_onscreen(
			                            GTK_TEXT_VIEW(message_view),
			                            message_end);
			break;

		/* Received chat message */
		case MSG_CHAT:

			/* Get username */
			get_string(username, &ptr);

			/* Add colon to displayed username */
			if (strlen(username) > 0) strcat(username, ": ");

			/* Get message */
			get_string(text, &ptr);

			/* Add newline to message */
			strcat(text, "\n");

			/* Get chat buffer */
			chat_buffer = gtk_text_view_get_buffer(
			                              GTK_TEXT_VIEW(chat_view));

			/* Get end of buffer */
			gtk_text_buffer_get_end_iter(chat_buffer, &end_iter);

			/* Add username to chat window */
			gtk_text_buffer_insert_with_tags_by_name(chat_buffer,
			                                 &end_iter,
			                                 username, -1, FORMAT_CHAT,
			                                 NULL);

			/* Get end of buffer */
			gtk_text_buffer_get_end_iter(chat_buffer, &end_iter);

			/* Add message to chat window */
			gtk_text_buffer_insert(chat_buffer, &end_iter, text,-1);

			/* Get end mark */
			end_mark = gtk_text_buffer_get_mark(chat_buffer, "end");

			/* Scroll mark onscreen */
			gtk_text_view_scroll_mark_onscreen(
			                    GTK_TEXT_VIEW(chat_view), end_mark);

			break;

		/* Meta-information update */
		case MSG_STATUS_META:

			/* Handle message */
			handle_status_meta(ptr, size - 8);

			/* XXX Do not free data */
			return FALSE;

		/* Player status update */
		case MSG_STATUS_PLAYER:

			/* Handle message */
			handle_status_player(ptr, size - 8);
			break;

		/* Card status update */
		case MSG_STATUS_CARD:

			/* Handle message */
			handle_status_card(ptr, size - 8);
			break;

		/* Goal status update */
		case MSG_STATUS_GOAL:

			/* Handle message */
			handle_status_goal(ptr);
			break;

		/* Misc status update */
		case MSG_STATUS_MISC:

			/* Handle message */
			handle_status_misc(ptr);
			break;

		/* Seat number update */
		case MSG_SEAT:

			/* Get seat number */
			x = get_integer(&ptr);

			/* Have GUI rotate players until we match */
			while (player_us != x)
			{
				/* Rotate */
				gui_func.notify_rotation(&real_game, 0);
			}

			/* Done */
			break;

		/* Waiting on players */
		case MSG_WAITING:

			/* Handle message */
			handle_waiting(ptr);
			break;

		/* Make a choice */
		case MSG_CHOOSE:

			/* Handle message */
			handle_choose(ptr);
			break;

		/* Prepare phase choices in advance */
		case MSG_PREPARE:

			/* Handle message */
			handle_prepare(ptr);
			break;

		/* Game is over */
		case MSG_GAMEOVER:

			/* Set game over */
			real_game.game_over = 1;

			/* Clear session ID */
			client_sid = -1;

			/* Clear game played flag */
			playing_game = 0;

			/* Set making choice (to enable disabled dialogs) */
			making_choice = 1;

			/* Notify gui */
			gui_client_state_changed(playing_game, making_choice);

			/* Reset displayed cards */
			reset_cards(&real_game, TRUE, TRUE);

			/* Redraw everything */
			redraw_everything();

			/* Reset prompt */
			gtk_label_set_text(GTK_LABEL(action_prompt),
			           "Game Over - Press Done to return to lobby");

			/* Auto export game */
			auto_export();

			/* Enable action button */
			gtk_widget_set_sensitive(action_button, TRUE);

			/* Wait until done button pressed */
			gtk_main();

			/* Tell server we are out of the game */
			send_msgf(server_fd, MSG_GAMEOVER, "");

			/* Unset making_choice */
			making_choice = 0;

			/* Notify gui */
			gui_client_state_changed(playing_game, making_choice);

			/* Check for disconnected */
			if (client_state != CS_DISCONN)
			{
				/* Switch back to lobby view */
				switch_view(1, 1);

				/* Reset buttons */
				game_view_changed(GTK_TREE_VIEW(games_view), NULL);
			}

			/* Done */
			break;

		default:
			sprintf(text, "Unknown message type %d\n", type);
			display_error(text);
			break;
	}

	/* Relinquish memory used */
	free(data);

	/* Do not get called again unless new message arrives */
	return FALSE;
}

/*
 * Data is ready to be read.
 */
static gboolean data_ready(GIOChannel *source, GIOCondition in, gpointer data)
{
	GtkWidget *dialog;
	static char buf[1024];
	static int buf_full;
	char *ptr, *copy;
	int x, type;

	/* Check for disconnection */
	if (client_state == CS_DISCONN)
	{
		/* Do not call handler any more */
		return FALSE;
	}

	/* Determine number of bytes to read */
	if (buf_full < 8)
	{
		/* Read 8 bytes for header */
		x = 8;
	}
	else
	{
		/* Skip to message size portion of header */
		ptr = buf + 4;
		x = get_integer(&ptr);
	}

	/* Check for overly long message */
	if (x > 1024)
	{
		/* Error */
		display_error("Received too long message!\n");
		exit(1);
	}

	/* Try to read enough bytes */
	x = recv(server_fd, buf + buf_full, x - buf_full, 0);

	/* Check for error reading */
	if (x <= 0)
	{
		/* Check for error */
		if (x < 0)
		{
			/* Check for try again error */
			if (errno == EAGAIN) return TRUE;

			/* Print error */
			perror("recv");
		}

		/* Create alert dialog */
		dialog = gtk_message_dialog_new(NULL,
					 GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_ERROR,
					 GTK_BUTTONS_CLOSE,
					 "Lost connection to server");

		/* Show dialog */
		gtk_widget_show_all(dialog);

		/* Destroy dialog when responded to */
		g_signal_connect_swapped(dialog, "response",
					 G_CALLBACK(gtk_widget_destroy),
					 dialog);

		/* Disconnect */
		disconnect();
		return FALSE;
	}

	/* Add to amount read */
	buf_full += x;

	/* Check for complete message header */
	if (buf_full >= 8)
	{
		/* Skip to length portion of header */
		ptr = buf + 4;
		x = get_integer(&ptr);

		/* Check for too-small message */
		if (x < 8)
		{
			/* Print error */
			display_error("Got too small message!\n");
			exit(1);
		}

		/* Check for complete message */
		if (buf_full == x)
		{
			/* Get type of message */
			ptr = buf;
			type = get_integer(&ptr);

			/* Clear buffer */
			buf_full = 0;

			/* Check for "meta game information" message */
			if (type == MSG_STATUS_META || type == MSG_HELLO)
			{
				/* Handle message immediately */
				message_read(buf);
			}
			else
			{
				/* Create temporary buffer for message */
				copy = (char *)malloc(x);

				/* Copy message bytes */
				memcpy(copy, buf, x);

				/* Handle message at next opportunity */
				g_timeout_add_full(G_PRIORITY_HIGH, 0,
				                   message_read, copy, NULL);
			}
		}
	}

	/* Continue reading data when able */
	return TRUE;
}

/*
 * Callback to trigger the accept response of a dialog
 */
static void enter_callback(GtkWidget *widget, GtkWidget *dialog)
{
    g_signal_emit_by_name(G_OBJECT(dialog), "response", GTK_RESPONSE_ACCEPT);
}

/*
 * Remember that the connect dialog is closed
 */
static gboolean deleted_callback(GtkWidget *widget, GdkEvent event, gpointer data)
{
	/* Set the dialog closed flag */
	connect_dialog_closed = TRUE;

	/* Continue handling events */
	return FALSE;
}

/*
 * Connect to a multiplayer server.
 */
void connect_dialog(GtkMenuItem *menu_item, gpointer data)
{
	struct hostent *server_host;
	struct sockaddr_in server_addr;
	int portno;
	char *old_server_name;
	GtkWidget *dialog, *connect_button, *cancel_button;
	GtkWidget *label, *hsep;
	GtkWidget *server, *port, *user, *pass;
	GtkWidget *table;
	GtkTextBuffer *chat_buffer;
	GIOChannel *io;
	unsigned int id;
#ifdef WIN32
	static int wsa_init = 0;
	WSADATA wsa_data;

	/* Check for need to initialize Windows socket library */
	if (!wsa_init)
	{
		/* Initialize socket library */
		if (WSAStartup(0x202, &wsa_data))
		{
			/* XXX */
			return;
		}

		/* Mark library as initialized */
		wsa_init = 1;
	}
#endif

	/* Do nothing if already connected */
	if (server_fd >= 0) return;

	/* Create dialog box */
	dialog = gtk_dialog_new_with_buttons("Connect to Server", NULL,
	                                     GTK_DIALOG_MODAL, NULL);

	/* Create connect button */
	connect_button = gtk_dialog_add_button(GTK_DIALOG(dialog),
	                                       GTK_STOCK_CONNECT,
	                                       GTK_RESPONSE_ACCEPT);

	/* Create cancel button */
	cancel_button = gtk_dialog_add_button(GTK_DIALOG(dialog),
	                                      GTK_STOCK_CANCEL,
	                                      GTK_RESPONSE_REJECT);

	/* Create a table for labels and text entry fields */
	table = gtk_table_new(6, 4, FALSE);

	/* Create label and text entry */
	label = gtk_label_new("Server name:");
	server = gtk_entry_new();

	/* Check for no server name in preferences */
	if (!opt.server_name) opt.server_name = "keldon.net";

	/* Save previous server */
	old_server_name = opt.server_name;

	/* Set default server name */
	gtk_entry_set_text(GTK_ENTRY(server), opt.server_name);

	/* Add widgets to table */
	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 0, 1);
	gtk_table_attach_defaults(GTK_TABLE(table), server, 1, 2, 0, 1);

	/* Create label and text entry */
	label = gtk_label_new("Port:");
	port = gtk_spin_button_new_with_range(0, 65535, 1);

	/* Check for no port number in preferences */
	if (!opt.server_port) opt.server_port = 16309;

	/* Set default server port */
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(port), opt.server_port);

	/* Add widgets to table */
	gtk_table_attach_defaults(GTK_TABLE(table), label, 2, 3, 0, 1);
	gtk_table_attach_defaults(GTK_TABLE(table), port, 3, 4, 0, 1);

	/* Make a separator */
	hsep = gtk_hseparator_new();

	/* Add separator to table */
	gtk_table_attach_defaults(GTK_TABLE(table), hsep, 0, 4, 1, 2);

	/* Create a label explaining user registration */
	label = gtk_label_new("There is no need to register an account.  \
Simply connect with an unused username, and a new account will be created \
with the password you enter.");

	/* Set desired label width */
	gtk_label_set_width_chars(GTK_LABEL(label), 50);

	/* Wrap text */
	gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);

	/* Add label to table */
	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 4, 2, 3);

	/* Create label and text entry */
	label = gtk_label_new("Username:");
	user = gtk_entry_new();

	/* Check for no username in preferences */
	if (!opt.username) opt.username = "";

	/* Set default username */
	gtk_entry_set_text(GTK_ENTRY(user), opt.username);

	/* Set maximum username length */
	gtk_entry_set_max_length(GTK_ENTRY(user), 20);

	/* Add widgets to table */
	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 3, 4);
	gtk_table_attach_defaults(GTK_TABLE(table), user, 1, 4, 3, 4);

	/* Create label and text entry */
	label = gtk_label_new("Password:");
	pass = gtk_entry_new();

	/* Check for no password in preferences */
	if (!opt.password) opt.password = "";

	/* Set default username */
	gtk_entry_set_text(GTK_ENTRY(pass), opt.password);

	/* Set maximum password length */
	gtk_entry_set_max_length(GTK_ENTRY(pass), 20);

	/* Add widgets to table */
	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 4, 5);
	gtk_table_attach_defaults(GTK_TABLE(table), pass, 1, 4, 4, 5);

	/* Create a label showing connection status */
	login_status = gtk_label_new("");

	/* Add label to table */
	gtk_table_attach_defaults(GTK_TABLE(table), login_status, 0, 4, 5, 6);

	/* Add table to dialog box */
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), table);

	/* Connect the entries' activate signal to the accept response on the dialog */
	g_signal_connect(G_OBJECT(server), "activate",
	                 G_CALLBACK(enter_callback), (gpointer) dialog);
	g_signal_connect(G_OBJECT(port), "activate",
	                 G_CALLBACK(enter_callback), (gpointer) dialog);
	g_signal_connect(G_OBJECT(user), "activate",
	                 G_CALLBACK(enter_callback), (gpointer) dialog);
	g_signal_connect(G_OBJECT(pass), "activate",
	                 G_CALLBACK(enter_callback), (gpointer) dialog);

	/* Connect the dialog's delete event to catch that the dialog is closed */
	g_signal_connect(G_OBJECT(dialog), "delete-event",
	                 G_CALLBACK(deleted_callback), NULL);

	/* Unset the dialog closed flag */
	connect_dialog_closed = FALSE;

	/* Show all widgets */
	gtk_widget_show_all(dialog);

	/* Run dialog */
	while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
	{
		/* Disable button while processing connection */
		gtk_widget_set_sensitive(connect_button, FALSE);

		/* Get port number */
		portno = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(port));

		/* Store changes to parameters */
		opt.server_name = strdup(gtk_entry_get_text(GTK_ENTRY(server)));
		opt.server_port = portno;
		opt.username = strdup(gtk_entry_get_text(GTK_ENTRY(user)));
		opt.password = strdup(gtk_entry_get_text(GTK_ENTRY(pass)));

		/* Save change to file */
		save_prefs();

		/* Check for changed server */
		if (strcmp(opt.server_name, old_server_name))
		{
			/* Get chat buffer */
			chat_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(chat_view));

			/* Clear text */
			gtk_text_buffer_set_text(chat_buffer, "", 0);
		}

		/* Clear status label */
		gtk_label_set_text(GTK_LABEL(login_status), "");

		/* Check for uninitialized socket */
		if (server_fd < 0)
		{
			/* Set status */
			gtk_label_set_text(GTK_LABEL(login_status),
			                   "Looking up server name");

			/* Handle pending events */
			while (gtk_events_pending()) gtk_main_iteration();

			/* Back out if dialog is closed (by escape) */
			if (connect_dialog_closed) break;

			/* Lookup server hostname */
			server_host =
			   gethostbyname(gtk_entry_get_text(GTK_ENTRY(server)));

			/* Check for error in lookup */
			if (!server_host)
			{
				/* Set status label text */
				gtk_label_set_text(GTK_LABEL(login_status),
				                   "Failed to lookup name");

				/* Enable button */
				gtk_widget_set_sensitive(connect_button, TRUE);

				continue;
			}

			/* Create socket */
			server_fd = socket(AF_INET, SOCK_STREAM, 0);

			/* Check for error */
			if (server_fd < 0)
			{
#ifdef WIN32
				/* Set status label text */
				gtk_label_set_text(GTK_LABEL(login_status),
				                   "Failed to create socket");
#else
				/* Set status label text */
				gtk_label_set_text(GTK_LABEL(login_status),
				                   strerror(errno));
#endif
				/* Enable button */
				gtk_widget_set_sensitive(connect_button, TRUE);

				continue;
			}

			/* Set status */
			gtk_label_set_text(GTK_LABEL(login_status),
			                   "Connecting to server");

			/* Handle pending events */
			while (gtk_events_pending()) gtk_main_iteration();

			/* Back out if dialog is closed (by escape) */
			if (connect_dialog_closed) break;

			/* Create server address */
			server_addr.sin_family = AF_INET;
			server_addr.sin_port = htons(portno);
			memcpy(&server_addr.sin_addr,
			       server_host->h_addr_list[0],
			       sizeof(struct in_addr));

			/* Attempt to connect */
			if (connect(server_fd, (struct sockaddr *)&server_addr,
			            sizeof(struct sockaddr_in)) < 0)
			{
#ifdef WIN32
				char msg[1024];

				/* Format message */
				sprintf(msg, "Failed to connect error %d",
				        WSAGetLastError());

				/* Set status label text */
				gtk_label_set_text(GTK_LABEL(login_status),
				                   msg);

				/* Close socket */
				closesocket(server_fd);
#else
				/* Set status label text */
				gtk_label_set_text(GTK_LABEL(login_status),
				                   strerror(errno));

				/* Close socket */
				close(server_fd);
#endif

				/* Reset server socket number */
				server_fd = -1;

				/* Enable button */
				gtk_widget_set_sensitive(connect_button, TRUE);

				/* Try again */
				continue;
			}

#ifdef WIN32
			/* Create IO channel using file descriptor */
			io = g_io_channel_win32_new_socket(server_fd);
#else
			/* Set socket to nonblocking */
			fcntl(server_fd, F_SETFL, O_NONBLOCK);

			/* Create IO channel using file descriptor */
			io = g_io_channel_unix_new(server_fd);
#endif

			/* Add IO channel to event source for main loop */
			id = g_io_add_watch(io, G_IO_IN, data_ready, NULL);

			/* Get source from ID */
			server_src = g_main_context_find_source_by_id(NULL, id);

			/* Disallow recursive handler calls */
			g_source_set_can_recurse(server_src, FALSE);
		}

		/* Set client state */
		client_state = CS_INIT;

		/* Notify gui */
		gui_client_state_changed(playing_game, making_choice);

		/* Freeze server name/port once connection is established */
		gtk_widget_set_sensitive(server, FALSE);
		gtk_widget_set_sensitive(port, FALSE);

		/* Set status */
		gtk_label_set_text(GTK_LABEL(login_status), "Sending login");

		/* Handle pending events */
		while (gtk_events_pending()) gtk_main_iteration();

		/* Back out if dialog is closed (by escape) */
		if (connect_dialog_closed) break;

		/* Send login message to server */
		send_msgf(server_fd, MSG_LOGIN, "ssss",
		          gtk_entry_get_text(GTK_ENTRY(user)),
		          gtk_entry_get_text(GTK_ENTRY(pass)), VERSION, RELEASE);


		/* Enter main loop to wait for response */
		gtk_main();

		/* Check for successful login */
		if (client_state == CS_LOBBY)
		{
			/* Set chat entry label to given username */
			gtk_label_set_text(GTK_LABEL(entry_label),
			                   gtk_entry_get_text(GTK_ENTRY(user)));

			/* Quit loop */
			break;
		}

		/* Enable buttons and inputs */
		gtk_widget_set_sensitive(connect_button, TRUE);
		gtk_widget_set_sensitive(server, TRUE);
		gtk_widget_set_sensitive(port, TRUE);
	}

	/* Check for failure to login */
	if (client_state != CS_LOBBY)
	{
		/* Check for open socket */
		if (server_fd >= 0)
		{
#ifdef WIN32
			/* Close socket */
			closesocket(server_fd);
			server_fd = -1;
#else
			/* Close socket */
			close(server_fd);
			server_fd = -1;
#endif
		}

		/* End current local game */
		real_game.game_over = 1;

		/* Restore single-player game */
		restart_loop = RESTART_RESTORE;

		/* Set state to disconnected */
		client_state = CS_DISCONN;

		/* Notify gui */
		gui_client_state_changed(playing_game, making_choice);
	}
	else
	{
		/* End current local game */
		real_game.game_over = 1;

		/* Do not start new game */
		restart_loop = RESTART_NONE;

		/* Quit from current choice */
		gtk_main_quit();
	}

	/* Check if dialog still exists */
	if (GTK_IS_WIDGET(dialog))
	{
		/* Destroy dialog */
		gtk_widget_destroy(dialog);
	}
}

/*
 * Quit from the gtk main loop until all loops are done.
 */
static gboolean quit_from_main(gpointer data)
{
	/* Quit from loop */
	gtk_main_quit();

	/* Check for last level */
	if (gtk_main_level() == 1)
	{
		/* Stop calling */
		return FALSE;
	}

	/* Call again */
	return TRUE;
}

/*
 * Disconnect from the server.
 */
static void disconnect(void)
{
	/* Check for already disconnected */
	if (server_fd < 0) return;

#ifdef WIN32
	/* Close connection to server */
	closesocket(server_fd);
#else
	/* Close connection to server */
	close(server_fd);
#endif

	/* Clear file descriptor */
	server_fd = -1;

	/* Remove source from main loop */
	g_source_destroy(server_src);

	/* Clear source */
	server_src = NULL;

	/* Clear client state */
	client_state = CS_DISCONN;

	/* Remove all entries from user and game lists */
	clear_games_users();

	/* Switch view to game */
	switch_view(0, 0);

	/* Restore single-player game */
	restart_loop = RESTART_RESTORE;

	/* No longer making a choice */
	making_choice = 0;

	/* Not playing in a game */
	playing_game = 0;

	/* Notify gui */
	gui_client_state_changed(playing_game, making_choice);

	/* Quit from all nested main loops */
	g_timeout_add(0, quit_from_main, NULL);
}

/*
 * Disconnect menu item.
 */
void disconnect_server(GtkMenuItem *menu_item, gpointer data)
{
	GtkWidget *dialog;
	int response;

	/* Check for game being played */
	if (playing_game)
	{
		/* Create courtesy dialog */
		dialog = gtk_message_dialog_new(NULL,
		                                GTK_DIALOG_DESTROY_WITH_PARENT,
		                                GTK_MESSAGE_QUESTION,
		                                GTK_BUTTONS_YES_NO,
		"If you have no intention of returning to this game, it "
		"would be polite to also resign and allow the AI to take "
		"control for you.  Resign?");

		/* Run dialog */
		response = gtk_dialog_run(GTK_DIALOG(dialog));

		/* Destroy dialog */
		gtk_widget_destroy(dialog);

		/* Check for escape pressed */
		if (response == GTK_RESPONSE_DELETE_EVENT)
		{
			/* Stay in game */
			return;
		}

		/* Check for "yes" answer */
		else if (response == GTK_RESPONSE_YES)
		{
			/* Ask server to resign */
			send_msgf(server_fd, MSG_RESIGN, "");
		}
	}

	/* Disconnect from server */
	disconnect();
}

/*
 * Widgets for the create game dialog box.
 */
static GtkWidget *min_player, *max_player;
static GtkWidget *advanced_check;
static GtkWidget *disable_goal_check;
static GtkWidget *disable_takeover_check;

/*
 * React to an expansion level being toggled.
 */
static void exp_toggle(GtkToggleButton *button, gpointer data)
{
	int i = GPOINTER_TO_INT(data);
	int max;

	/* Check for button set */
	if (gtk_toggle_button_get_active(button))
	{
		/* Set maximum number of players */
		max = i + 4;
		if (max > 6) max = 6;
		if (i == 4) max = 5;

		/* Adjust scale widgets to new maximum */
		gtk_range_set_range(GTK_RANGE(min_player), 2, max);
		gtk_range_set_range(GTK_RANGE(max_player), 2, max);

		/* Set goal disabled checkbox sensitivity */
		gtk_widget_set_sensitive(disable_goal_check, i > 0 && i < 4);

		/* Set takeover disabled checkbox sensitivity */
		gtk_widget_set_sensitive(disable_takeover_check, i > 1 &&
		                                                 i < 4);
	}
}

/*
 * Enforce minimum player number is less than maximum number.
 */
static void player_changed(GtkRange *range, gpointer data)
{
	int i = GPOINTER_TO_INT(data);
	int min, max;

	/* Get current values of min and max */
	min = gtk_range_get_value(GTK_RANGE(min_player));
	max = gtk_range_get_value(GTK_RANGE(max_player));

	/* Check for minimum being set */
	if (i == 0)
	{
		/* Check for maximum too low */
		if (max < min)
		{
			/* Reset maximum */
			gtk_range_set_value(GTK_RANGE(max_player), min);
			max = min;
		}
	}
	else
	{
		/* Check for minimum too high */
		if (min > max)
		{
			/* Reset minimum */
			gtk_range_set_value(GTK_RANGE(min_player), max);
			min = max;
		}
	}

	/* Set two-player advanced checkbox sensitivity */
	gtk_widget_set_sensitive(advanced_check, min == 2);
}

/*
 * Display the "create game" dialog box.
 */
void create_dialog(GtkButton *button, gpointer data)
{
	GtkWidget *dialog;
	GtkWidget *radio[MAX_EXPANSION];
	GtkWidget *table, *label;
	GtkWidget *exp_box, *exp_frame;
	GtkWidget *desc, *pass;
	int i, exp = 0;

	/* Create dialog box */
	dialog = gtk_dialog_new_with_buttons("Create Game", NULL,
	                                     GTK_DIALOG_MODAL,
	                                     GTK_STOCK_ADD,
	                                     GTK_RESPONSE_ACCEPT,
	                                     GTK_STOCK_CANCEL,
	                                     GTK_RESPONSE_REJECT, NULL);

	/* Create a table for laying out widgets */
	table = gtk_table_new(8, 2, FALSE);

	/* Create label and text entry for game description */
	label = gtk_label_new("Description:");
	desc = gtk_entry_new();

	/* Cap the description length */
	gtk_entry_set_max_length(GTK_ENTRY(desc), 40);

	/* Check for no game description in preferences */
	if (!opt.game_desc) opt.game_desc = "";

	/* Set default description */
	gtk_entry_set_text(GTK_ENTRY(desc), opt.game_desc);

	/* Add widgets to table */
	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 0, 1);
	gtk_table_attach_defaults(GTK_TABLE(table), desc, 1, 2, 0, 1);

	/* Create label and text entry for password */
	label = gtk_label_new("Game password:");
	pass = gtk_entry_new();

	/* Cap the password length */
	gtk_entry_set_max_length(GTK_ENTRY(pass), 20);

	/* Check for no game password in preferences */
	if (!opt.game_pass) opt.game_pass = "";

	/* Set default password */
	gtk_entry_set_text(GTK_ENTRY(pass), opt.game_pass);

	/* Connect the entries' activate signal to the accept response on the dialog */
	g_signal_connect(G_OBJECT(desc), "activate", G_CALLBACK(enter_callback),
	                 (gpointer) dialog);
	g_signal_connect(G_OBJECT(pass), "activate", G_CALLBACK(enter_callback),
	                 (gpointer) dialog);

	/* Add widgets to table */
	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 1, 2);
	gtk_table_attach_defaults(GTK_TABLE(table), pass, 1, 2, 1, 2);

	/* Create vbox to hold expansion selection radio buttons */
	exp_box = gtk_vbox_new(FALSE, 0);

	/* Clear first radio button */
	radio[0] = NULL;

	/* Loop over expansion levels */
	for (i = 0; exp_names[i]; i++)
	{
		/* Create radio button */
		radio[i] = gtk_radio_button_new_with_label_from_widget(
		                                     GTK_RADIO_BUTTON(radio[0]),
		                                     exp_names[i]);

		/* Add handler */
		g_signal_connect(G_OBJECT(radio[i]), "toggled",
		                 G_CALLBACK(exp_toggle), GINT_TO_POINTER(i));

		/* Pack radio button into box */
		gtk_box_pack_start(GTK_BOX(exp_box), radio[i], FALSE, TRUE, 0);
	}

	/* Create frame around buttons */
	exp_frame = gtk_frame_new("Choose expansion level");

	/* Pack vbox into frame */
	gtk_container_add(GTK_CONTAINER(exp_frame), exp_box);

	/* Add expansion frame to table */
	gtk_table_attach_defaults(GTK_TABLE(table), exp_frame, 0, 2, 2, 3);

	/* Create label and scale for minimum number of players */
	label = gtk_label_new("Minimum players:");
	min_player = gtk_hscale_new_with_range(2, 4, 1);

	/* Add handler */
	g_signal_connect(G_OBJECT(min_player), "value-changed",
	                 G_CALLBACK(player_changed), GINT_TO_POINTER(0));

	/* Add widgets to table */
	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 3, 4);
	gtk_table_attach_defaults(GTK_TABLE(table), min_player, 1, 2, 3, 4);

	/* Create label and scale for maximum number of players */
	label = gtk_label_new("Maximum players:");
	max_player = gtk_hscale_new_with_range(2, 4, 1);

	/* Add handler */
	g_signal_connect(G_OBJECT(max_player), "value-changed",
	                 G_CALLBACK(player_changed), GINT_TO_POINTER(1));

	/* Add widgets to table */
	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 4, 5);
	gtk_table_attach_defaults(GTK_TABLE(table), max_player, 1, 2, 4, 5);

	/* Create check box for two-player advanced game */
	advanced_check = gtk_check_button_new_with_label("Two-player advanced");

	/* Set default */
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(advanced_check),
	                             opt.advanced);

	/* Add checkbox to table */
	gtk_table_attach_defaults(GTK_TABLE(table), advanced_check, 0, 2, 5, 6);

	/* Create check box for disabled goals */
	disable_goal_check = gtk_check_button_new_with_label("Disable goals");

	/* Set default */
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(disable_goal_check),
	                             opt.disable_goal);

	/* Make checkbox insensitive */
	gtk_widget_set_sensitive(disable_goal_check, FALSE);

	/* Add checkbox to table */
	gtk_table_attach_defaults(GTK_TABLE(table), disable_goal_check,
	                          0, 2, 6, 7);

	/* Create check box for disabled takeovers */
	disable_takeover_check =
	                   gtk_check_button_new_with_label("Disable takeovers");

	/* Set default */
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(disable_takeover_check),
	                             opt.disable_takeover);

	/* Make checkbox insensitive */
	gtk_widget_set_sensitive(disable_takeover_check, FALSE);

	/* Add checkbox to table */
	gtk_table_attach_defaults(GTK_TABLE(table), disable_takeover_check,
	                          0, 2, 7, 8);

	/* Add table to dialog box */
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), table);

	/* Loop over expansion levels */
	for (i = 0; exp_names[i]; i++)
	{
		/* Select radio button if needed */
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio[i]),
		                             i == opt.expanded);
	}

	/* Set default values */
	gtk_range_set_value(GTK_RANGE(min_player), opt.multi_min);
	gtk_range_set_value(GTK_RANGE(max_player), opt.multi_max);

	/* Show all widgets */
	gtk_widget_show_all(dialog);

	/* Run dialog */
	if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_ACCEPT)
	{
		/* Destroy dialog */
		gtk_widget_destroy(dialog);

		/* Done */
		return;
	}

	/* Loop over expansion radio buttons */
	for (i = 0; i < MAX_EXPANSION; i++)
	{
		/* Check for selected button */
		if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(radio[i])))
		{
			/* Set expansion level */
			opt.expanded = exp = i;
		}
	}

	/* Save options for later */
	opt.game_desc = strdup(gtk_entry_get_text(GTK_ENTRY(desc)));
	opt.game_pass = strdup(gtk_entry_get_text(GTK_ENTRY(pass)));
	opt.multi_min = (int)gtk_range_get_value(GTK_RANGE(min_player));
	opt.multi_max = (int)gtk_range_get_value(GTK_RANGE(max_player));
	opt.advanced = gtk_toggle_button_get_active(
	                             GTK_TOGGLE_BUTTON(advanced_check));
	opt.disable_goal = gtk_toggle_button_get_active(
	                             GTK_TOGGLE_BUTTON(disable_goal_check));
	opt.disable_takeover = gtk_toggle_button_get_active(
	                             GTK_TOGGLE_BUTTON(disable_takeover_check));

	/* Save change to file */
	save_prefs();

	/* Send create message to server */
	send_msgf(server_fd, MSG_CREATE, "ssddddddd",
	          gtk_entry_get_text(GTK_ENTRY(pass)),
	          gtk_entry_get_text(GTK_ENTRY(desc)),
	          (int)gtk_range_get_value(GTK_RANGE(min_player)),
	          (int)gtk_range_get_value(GTK_RANGE(max_player)),
	          exp,
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(advanced_check)),
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(disable_goal_check)),
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(disable_takeover_check)),
	          0);

	/* Destroy dialog */
	gtk_widget_destroy(dialog);
}

/*
 * Send a line of chat to the server.
 */
void send_chat(GtkEntry *entry, gpointer data)
{
	/* Do not send empty messages */
	if (!strlen(gtk_entry_get_text(entry))) return;

	/* Send message to server */
	send_msgf(server_fd, MSG_CHAT, "s", gtk_entry_get_text(entry));

	/* Clear entry */
	gtk_entry_set_text(entry, "");
}

/*
 * Resign from current game and return to lobby.
 */
void resign_game(GtkMenuItem *menu_item, gpointer data)
{
	GtkWidget *dialog;
	int response;

	/* Do nothing if not connected to server */
	if (server_fd < 0) return;

	/* Create message dialog */
	dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_DESTROY_WITH_PARENT,
	                                GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO,
	"WARNING: Resigning from a game with other players still active is "
	"considered unsportsmanlike behavior.  A count of games quit in this "
	"manner will be tracked and may be displayed in the future.  But if "
	"all other players have already quit or disconnected, it is OK to "
	"continue.  Still wish to resign?");

	/* Run dialog */
	response = gtk_dialog_run(GTK_DIALOG(dialog));

	/* Destroy dialog */
	gtk_widget_destroy(dialog);

	/* Check for "no" answer */
	if (response != GTK_RESPONSE_YES) return;

	/* Ask server to resign */
	send_msgf(server_fd, MSG_RESIGN, "");

	/* Leave current session */
	client_sid = -1;

	/* Clear game played flag */
	playing_game = 0;

	/* Notify gui */
	gui_client_state_changed(playing_game, making_choice);

	/* Switch back to lobby view */
	switch_view(1, 1);

	/* Reset buttons */
	game_view_changed(GTK_TREE_VIEW(games_view), NULL);

	/* Abort choice if being made */
	if (making_choice) gtk_main_quit();
}

/*
 * Attempt to join a game.
 */
void join_game(GtkButton *button, gpointer data)
{
	GtkTreePath *game_path;
	GtkTreeIter game_iter, parent_iter;
	int x, pass_needed;
	GtkWidget *dialog, *hbox, *label, *password;
	char pass[1024] = "";
	int res;

	/* Get selected game */
	gtk_tree_view_get_cursor(GTK_TREE_VIEW(games_view), &game_path, NULL);

	/* Check for no selection */
	if (!game_path) return;

	/* Get iterator for path */
	gtk_tree_model_get_iter(GTK_TREE_MODEL(game_list), &game_iter,
	                        game_path);

	/* Free path */
	gtk_tree_path_free(game_path);

	/* Get parent iterator, if any */
	if (!gtk_tree_model_iter_parent(GTK_TREE_MODEL(game_list), &parent_iter,
	                                &game_iter))
	{
		/* Set parent to current cursor location */
		parent_iter = game_iter;
	}

	/* Get session ID of game to join */
	gtk_tree_model_get(GTK_TREE_MODEL(game_list), &parent_iter, 0, &x,
	                   3, &pass_needed, -1);

	/* Check for password required */
	if (pass_needed)
	{
		/* Create dialog to request password */
		dialog = gtk_dialog_new_with_buttons("Join Game", NULL,
		                                     GTK_DIALOG_MODAL,
		                                     GTK_STOCK_OK,
		                                     GTK_RESPONSE_ACCEPT,
		                                     GTK_STOCK_CANCEL,
		                                     GTK_RESPONSE_REJECT, NULL);

		/* Create hbox to hold password entry widgets */
		hbox = gtk_hbox_new(FALSE, 0);

		/* Create label and text entry for game password */
		label = gtk_label_new("Game password:");
		password = gtk_entry_new();

		/* Cap the password length */
		gtk_entry_set_max_length(GTK_ENTRY(password), 20);

		/* Add widgets to hbox */
		gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(hbox), password, TRUE, TRUE, 0);

		/* Add hbox to dialog */
		gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), hbox);

		/* Connect the entry's activate signal to the accept response on the dialog */
		g_signal_connect(G_OBJECT(password), "activate", G_CALLBACK(enter_callback),
		                 (gpointer) dialog);

		/* Show everything */
		gtk_widget_show_all(dialog);

		/* Run dialog */
		res = gtk_dialog_run(GTK_DIALOG(dialog));

		/* Get password entry contents */
		strcpy(pass, gtk_entry_get_text(GTK_ENTRY(password)));

		/* Destroy dialog box */
		gtk_widget_destroy(dialog);

		/* Check for accepted choice */
		if (res != GTK_RESPONSE_ACCEPT) return;
	}

	/* Send join message to server */
	send_msgf(server_fd, MSG_JOIN, "ds", x, pass);
}

/*
 * Leave the current game.
 */
void leave_game(GtkButton *button, gpointer data)
{
	/* Ask server to leave game */
	send_msgf(server_fd, MSG_LEAVE, "");

	/* Clear current session ID */
	client_sid = -1;
}

/*
 * Kick a player from a game.
 */
void kick_player(GtkButton *button, gpointer data)
{
	GtkTreePath *game_path;
	GtkTreeIter game_iter, parent_iter;
	int x, self;
	char *buf;

	/* Get selected game */
	gtk_tree_view_get_cursor(GTK_TREE_VIEW(games_view), &game_path, NULL);

	/* Check for no selection */
	if (!game_path) return;

	/* Get iterator for path */
	gtk_tree_model_get_iter(GTK_TREE_MODEL(game_list), &game_iter,
	                        game_path);

	/* Free path */
	gtk_tree_path_free(game_path);

	/* Get parent iterator, if any */
	if (!gtk_tree_model_iter_parent(GTK_TREE_MODEL(game_list), &parent_iter,
	                                &game_iter))
	{
		/* No user selected */
		return;
	}

	/* Get session ID of game */
	gtk_tree_model_get(GTK_TREE_MODEL(game_list), &parent_iter, 0, &x, -1);

	/* Get name of user to kick */
	gtk_tree_model_get(GTK_TREE_MODEL(game_list), &game_iter, 1, &buf,
	                   10, &self, -1);

	/* Check for self selected */
	if (self) return;

	/* Send request to kick player */
	send_msgf(server_fd, MSG_REMOVE, "ds", x, buf);

	/* Free string */
	g_free(buf);
}

/*
 * Ask the server to add an AI player to the game.
 */
void add_ai_player(GtkButton *button, gpointer data)
{
	/* Check for not joined a game */
	if (client_sid == -1) return;

	/* Send add AI message to server */
	send_msgf(server_fd, MSG_ADD_AI, "d", client_sid);
}

/*
 * Ask the server to start the joined game.
 */
void start_game(GtkButton *button, gpointer data)
{
	/* Send start message to server */
	send_msgf(server_fd, MSG_START, "d", client_sid);
}
