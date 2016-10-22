/*
 * Race for the Galaxy AI
 *
 * Copyright (C) 2009-2015 B. Nordli
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

#include "rftg.h"
#include "comm.h"
#include <mysql/mysql.h>

/* Number of random bytes stored per game */
#define MAX_RAND     1024

/* The gid to start at */
static int gid_min = -1;

/* The gid to end at */
static int gid_max = -1;

/* Log exports folder */
static char* export_folder = ".";

/* Server name */
static char* server_name = NULL;

/* Export file style sheet */
static char* export_style_sheet = NULL;

/* Export file style sheet */
static char* export_style_sheet_replay = NULL;

/* Verbosity */
static int verbose = 0;

/* Connection to the database server */
MYSQL *mysql;

/* The current replaying gid */
static int gid;

/* Game to be replayed */
static game g;

/* Current replay pass */
static int pass;

/* Current game round */
static int current_round;

/* Number of rounds in the game */
static int num_rounds;

/* Pool of random bytes. */
static unsigned char random_pool[MAX_RAND];

/* Current position in random pool */
static int random_pos;

/* Choices loaded from db */
static int *choice_logs[MAX_PLAYER];

/* Size of choice_logs */
static int choice_size[MAX_PLAYER];

/* Number of decisions */
static int num_choices[MAX_PLAYER];

/* Number of decisions at start of each round */
static int decision_round[MAX_PLAYER][40];

/* Number of game rotations */
static int rotations;

/* Set if any errors occurred */
static int error = 0;

/* Player spots have been rotated */
static void replay_notify_rotation(game *g, int who)
{
	/* Only rotate once per set of players */
	if (who == 0) ++rotations;
}

/* Compute the original player id of a player */
static int original_id(int who)
{
	/* Start with substracting the number of rotations */
	int ret = who + rotations;

	/* If overflow, find the correct player */
	return ret < g.num_players ? ret : ret - g.num_players;
}

/* Compute the new player id of an original player */
static int new_id(int who)
{
	/* Start with substracting the number of rotations */
	int ret = who - rotations;

	/* If overflow, find the correct player */
	return ret >= 0 ? ret : ret + g.num_players;
}

/* A log message */
typedef struct message
{
	/* The text of the message */
	char text[256];

	/* The format of the message */
	char* format;

	/* The player id if a private message */
	int player;

} message;

/* The saved log */
static message log[4096];

/* The number of messages so far */
static int num_messages;

/* Log position for previous choice */
static int log_pos[MAX_PLAYER];

/* Save a game message */
static void save_message(char *txt, char *tag, int player)
{
	/* Print the message */
	if (verbose) printf("%s", txt);

	/* Copy the message text */
	strcpy(log[num_messages].text, txt);

	/* Chop newline */
	log[num_messages].text[strlen(log[num_messages].text) - 1] = '\0';

	/* Save the message format */
	log[num_messages].format = tag;

	/* Save the private id */
	log[num_messages].player = player;

	/* Increase the number of messages */
	++num_messages;
}

/*
 * Export log (from a specific player).
 */
static void export_log(FILE *fff, int who)
{
	int i, start;
	char name[1024];

	/* Compute start message */
	start = who < 0 ? 0 : log_pos[who];

	/* Loop over messages */
	for (i = start; i < num_messages; ++i)
	{
		/* Check for private message */
		if (who >= 0 && log[i].player >= 0)
		{
			/* Skip wrong player */
			if (log[i].player != who) continue;

			/* Add user name */
			sprintf(name, " private=\"%s\"",
			        xml_escape(g.p[log[i].player].name));
		}
		else
		{
			/* Clear user name */
			strcpy(name, "");
		}

		/* Check for no format */
		if (!log[i].format)
		{
			/* Write xml start tag */
			fprintf(fff, "    <Message%s>", name);
		}

		/* Formatted message */
		else
		{
			/* Write xml start tag with format attribute */
			fprintf(fff, "    <Message format=\"%s\"%s>", log[i].format, name);
		}

		/* Write message and xml end tag */
		fprintf(fff, "%s</Message>\n", xml_escape(log[i].text));
	}

	/* Update log position for this player */
	if (who >= 0) log_pos[who] = num_messages;
}

/*
 * Export log of a specific game.
 */
static void export_end_log(FILE *fff, int gid)
{
	MYSQL_RES *res;
	MYSQL_ROW row;
	char query[1024];
	char msg[1024], name[1024], *ptr;

	/* Create lookup query */
	sprintf(query, "SELECT message, format, user "
	               "FROM messages LEFT JOIN users USING (uid) "
	               "WHERE gid=%d ORDER BY mid", gid);

	/* Run query */
	mysql_query(mysql, query);

	/* Fetch results */
	res = mysql_store_result(mysql);

	/* Check for no rows */
	if (mysql_num_rows(res) == 0)
	{
		/* Fallback to saved logs */
		export_log(fff, -1);
		return;
	}

	/* Loop over rows returned */
	while ((row = mysql_fetch_row(res)))
	{
		/* Reset message */
		ptr = msg;

		/* Check for chat message */
		if (!strcmp(row[1], FORMAT_CHAT))
		{
			/* Write xml start tag with format attribute */
			fprintf(fff, "    <Message format=\"%s\">", row[1]);

			/* Check for player chat */
			if (row[2])
			{
				/* Put user name */
				fprintf(fff, "%s: ", xml_escape(row[2]));
			}
		}
		else
		{
			/* Chop newline */
			row[0][strlen(row[0]) - 1] = '\0';

			/* Check for private message */
			if (row[2])
			{
				/* Add user name */
				sprintf(name, " private=\"%s\"", xml_escape(row[2]));
			}
			else
			{
				/* Clear user name */
				strcpy(name, "");
			}

			/* Check for no format */
			if (!strlen(row[1]))
			{
				/* Write xml start tag */
				fprintf(fff, "    <Message%s>", name);
			}

			/* Formatted message */
			else
			{
				/* Write xml start tag with format attribute */
				fprintf(fff, "    <Message format=\"%s\"%s>", row[1], name);
			}
		}

		/* Write message and xml end tag */
		fprintf(fff, "%s</Message>\n", xml_escape(row[0]));
	}

	/* Free results */
	mysql_free_result(res);
}

/* The name of the replay file for a given game, player and choices */
static char *replay_file_name(int gid, int who, int choice)
{
	static char filename[1024];

	/* Format the file name */
	sprintf(filename, "Game_%06d_p%d_d%d.xml", gid, who, choice);

	/* Return the formatted file name */
	return filename;
}

/*
 * Export callback for generating player name links.
 */
static void export_callback(FILE *fff, int orig_who)
{
	int i, j;

	/* Write start tag */
	fputs("  <Links text=\"Full game\">\n", fff);

	/* Export full game link */
	fprintf(fff, "    <Link text=\"Game #%d\">../Game_%06d.xml</Link>\n",
	        gid, gid);

	/* Write end tag */
	fputs("  </Links>\n", fff);

	/* Loop over players */
	for (i = 0; i < g.num_players; ++i)
	{
		/* Write start tag */
		fprintf(fff, "  <Links text=\"%s\">\n", g.p[new_id(i)].name);

		/* Print link to start of game for this player */
		fprintf(fff, "    <Link text=\"Start\">%s</Link>\n",
		        replay_file_name(gid, i, decision_round[i][0]));

		/* Loop over rounds */
		for (j = 1; j <= num_rounds; ++j)
		{
			/* Print link to this round for this player */
			fprintf(fff, "    <Link text=\"%d\">%s</Link>\n",
			        j, replay_file_name(gid, i, decision_round[i][j]));
		}

		/* Print link to end of game for this player */
		fprintf(fff, "    <Link text=\"End\">%s</Link>\n",
		        replay_file_name(gid, i, decision_round[i][num_rounds + 1]));

		/* Write end tag */
		fputs("  </Links>\n", fff);
	}

	/* Write start tag */
	fputs("  <Links text=\"Navigate\">\n", fff);

	/* Check for previous choice available */
	if (num_choices[orig_who] > 0)
	{
		/* Export previous choices */
		fprintf(fff, "    <Link text=\"Previous choice\">%s</Link>\n",
		        replay_file_name(gid, orig_who, num_choices[orig_who] - 1));
	}

	/* Check for next choice available */
	if (!g.game_over)
	{
		/* Export next choices */
		fprintf(fff, "    <Link text=\"Next choice\">%s</Link>\n",
		        replay_file_name(gid, orig_who, num_choices[orig_who] + 1));
	}

	/* Write end tag */
	fputs("  </Links>\n", fff);
}

/*
 * Export callback for complete game.
 */
static void export_end_callback(FILE *fff, int dummy)
{
	int i;

	/* Write start tag */
	fputs("  <Links text=\"Replays\">\n", fff);

	/* Loop over players */
	for (i = 0; i < g.num_players; ++i)
	{
		/* Print link to start of game for this player */
		fprintf(fff, "    <Link text=\"%s\">replay/%s</Link>\n",
		        g.p[new_id(i)].name, replay_file_name(gid, i, 0));
	}

	/* Write end tag */
	fputs("  </Links>\n", fff);
}

/* The current message */
static char msg[256];

/* The current number of special cards in an export */
static int num_special_cards;

/* The current special cards in an export */
static card *special_cards[20];

/* Export the game seen from a specific player */
static void export(game *g, int who)
{
	int orig_who;
	char filename[1024];

	/* Check for replay style sheet enabled */
	if (!export_style_sheet_replay) return;

	/* Compute the original player seat */
	orig_who = original_id(who);

	/* Create file name */
	sprintf(filename, "%s/replay/%s", export_folder,
	        replay_file_name(gid, orig_who, num_choices[orig_who]));

	/* Export game to file */
	if (export_game(g, filename, export_style_sheet_replay, server_name,
	    who, msg, num_special_cards, special_cards, 0,
	    export_log, export_callback, orig_who) < 0)
	{
		/* Log error */
		printf("Could not export game to %s\n", filename);

		/* Save error */
		error = 1;
	}
	else
	{
		/* Log export location */
		if (verbose) printf("Game exported to %s\n", filename);
	}
}

/* Export the complete game */
static void export_end(game *g)
{
	char filename[1024];

	/* Check for style sheet enabled */
	if (!export_style_sheet) return;

	/* Create file name */
	sprintf(filename, "%s/Game_%06d.xml", export_folder, gid);

	/* Delete any existing file */
	unlink(filename);

	/* Export game to file */
	if (export_game(g, filename, export_style_sheet, server_name,
	    -1, NULL, 0, NULL, 1, export_end_log, export_end_callback, gid) < 0)
	{
		/* Log error */
		printf("Could not export game to %s\n", filename);

		/* Save error */
		error = 1;
	}
	else
	{
		/* Log export location */
		if (verbose) printf("Game exported to %s\n", filename);
	}
}

/*
 * Choose a card to place for the Develop or Settle phases.
 */
static void choose_place(game *g, int who, int list[], int num, int phase,
                         int special)
{
	int allow_takeover = (phase == PHASE_SETTLE);

	/* Create prompt */
	sprintf(msg, "Choose card to %s",
	        phase == PHASE_DEVELOP ? "develop" : "settle");

	/* Check for special card used to provide power */
	if (special != -1)
	{
		/* Append name to prompt */
		strcat(msg, " using ");
		strcat(msg, g->deck[special].d_ptr->name);

		/* XXX Check for "Rebel Sneak Attack" */
		if (!strcmp(g->deck[special].d_ptr->name, "Rebel Sneak Attack"))
		{
			/* Takeover not allowed */
			allow_takeover = 0;
		}
	}

	/* Check for settle phase and possible takeover */
	if (allow_takeover && settle_check_takeover(g, who, NULL, 1))
	{
		/* Append takeover information */
		strcat(msg, " (or pass if you want to perform a takeover)");
	}
}

/*
 * Choose method of payment for a placed card.
 *
 * We include some active cards that have powers that can be triggered,
 * such as the Contact Specialist or Colony Ship.
 */
static void choose_pay(game *g, int who, int which, int list[], int *num,
                       int special[], int *num_special, int mil_only)
{
	card *c_ptr;

	/* Get card we are paying for */
	c_ptr = &g->deck[which];

	/* Create prompt */
	sprintf(msg, "Choose payment for %s ", c_ptr->d_ptr->name);
}

/* Find the message to the player */
static void determine_message(game *g, int who, int type, int list[], int *nl,
                              int special[], int *ns, int arg1, int arg2,
                              int arg3)
{
	int i;

	/* Determine type of choice */
	switch (type)
	{
		/* Action(s) to play */
		case CHOICE_ACTION:

			/* Check for advanced game */
			if (g->advanced)
			{
				/* Check for two actions */
				if (arg1 == 0)
				{
					/* Create prompt */
					sprintf(msg, "Choose Actions");
				}
				/* Check for only first action */
				else if (arg1 == 1)
				{
					/* Create prompt */
					sprintf(msg, "Choose first Action");
				}
				/* Check for only second action */
				else if (arg1 == 2)
				{
					/* Create prompt */
					sprintf(msg, "Choose second Action");
				}
			}
			else
			{
				/* Create prompt */
				sprintf(msg, "Choose action");
			}

			break;

		/* Start world */
		case CHOICE_START:

			/* Save special cards */
			num_special_cards = *ns;
			for (i = 0; i < *ns; ++i) special_cards[i] = &g->deck[special[i]];

			/* Create prompt */
			sprintf(msg, "Choose start world and hand discards");
			break;

		/* Discard */
		case CHOICE_DISCARD:

			/* Create prompt */
			sprintf(msg, "Choose %d card%s to discard", arg1, PLURAL(arg1));
			break;

		/* Save a card under a world for later */
		case CHOICE_SAVE:

			/* Save special cards */
			num_special_cards = *nl;
			for (i = 0; i < *nl; ++i) special_cards[i] = &g->deck[list[i]];

			/* Create prompt */
			sprintf(msg, "Choose card to save for later");
			break;

		/* Choose to discard to gain prestige */
		case CHOICE_DISCARD_PRESTIGE:

			/* Create prompt */
			sprintf(msg, "Choose card to discard for prestige");
			break;

		/* Place a development/world */
		case CHOICE_PLACE:

			/* Choose card to place */
			choose_place(g, who, list, *nl, arg1, arg2);
			break;

		/* Pay for a development/world */
		case CHOICE_PAYMENT:

			/* Choose payment */
			choose_pay(g, who, arg1, list, nl, special, ns, arg2);
			break;

		/* Choose settle power to use next */
		case CHOICE_SETTLE:

			/* Create prompt */
			sprintf(msg, "Choose Settle power");
			break;

		/* Choose a world to takeover */
		case CHOICE_TAKEOVER:

			/* Create prompt */
			sprintf(msg, "Choose world to takeover and power to use");
			break;

		/* Choose a method of defense against a takeover */
		case CHOICE_DEFEND:

			/* Create prompt */
			sprintf(msg, "Choose defense for %s (need %d extra military)",
			        g->deck[arg1].d_ptr->name, arg3 + 1);
			break;

		/* Choose whether to prevent a takeover */
		case CHOICE_TAKEOVER_PREVENT:

			/* Create prompt */
			sprintf(msg, "Choose takeover to prevent");
			break;

		/* Choose world to upgrade with one from hand */
		case CHOICE_UPGRADE:

			/* Create prompt */
			sprintf(msg, "Choose world to replace");
			break;

		/* Choose a good to trade */
		case CHOICE_TRADE:

			/* Create prompt */
			sprintf(msg, "Choose good to trade%s", arg1 ? " (no bonuses)" : "");
			break;

		/* Choose a consume power to use */
		case CHOICE_CONSUME:

			/* Create prompt */
			sprintf(msg, "Choose Consume power");
			break;

		/* Choose discards from hand for VP */
		case CHOICE_CONSUME_HAND:

			/* Check for prestige trade bonus power */
			if (arg1 < 0)
			{
				/* Create prompt */
				sprintf(msg, "Choose up to 2 cards to consume on Prestige Trade bonus");
			}
			else
			{
				/* Check for needing two cards */
				if (g->deck[arg1].d_ptr->powers[arg2].code & P4_CONSUME_TWO)
				{
					/* Create prompt */
					sprintf(msg, "Choose cards to consume on %s", g->deck[arg1].d_ptr->name);
				}
				else
				{
					/* Read power size */
					i = g->deck[arg1].d_ptr->powers[arg2].times;

					/* Create prompt */
					sprintf(msg, "Choose up to %d card%s to consume on %s",
					        i, PLURAL(i), g->deck[arg1].d_ptr->name);
				}
			}
			break;

		/* Choose good(s) to consume */
		case CHOICE_GOOD:

			/* Create prompt */
			sprintf(msg, "Choose good%s to consume on %s",
			                     arg1 == 1 && arg2 == 1 ? "" :
			                     "s", g->deck[special[0]].d_ptr->name);
			break;

		/* Choose lucky number */
		case CHOICE_LUCKY:

			/* Choose number */
			sprintf(msg, "Choose Number");
			break;

		/* Choose card to ante */
		case CHOICE_ANTE:

			/* Create prompt */
			sprintf(msg, "Choose card to ante");
			break;

		/* Choose card to keep in successful gamble */
		case CHOICE_KEEP:

			/* Create prompt */
			sprintf(msg, "Choose card to keep");
			break;

		/* Choose windfall world to produce on */
		case CHOICE_WINDFALL:

			/* Create prompt */
			sprintf(msg, "Choose windfall world to produce");
			break;

		/* Choose produce power to use */
		case CHOICE_PRODUCE:

			/* Create prompt */
			sprintf(msg, "Choose Produce power");
			break;

		/* Choose card to discard in order to produce */
		case CHOICE_DISCARD_PRODUCE:

			/* Create prompt */
			sprintf(msg, "Choose discard to produce");
			break;

		/* Choose search category */
		case CHOICE_SEARCH_TYPE:

			/* Create prompt */
			sprintf(msg, "Choose Search category");
			break;

		/* Choose whether to keep searched card */
		case CHOICE_SEARCH_KEEP:

			/* Save special card */
			num_special_cards = 1;
			special_cards[0] = &g->deck[arg1];

			/* Create prompt */
			sprintf(msg, "Choose to keep/discard %s", g->deck[arg1].d_ptr->name);
			break;

		/* Choose color of Alien Oort Cloud Refinery */
		case CHOICE_OORT_KIND:

			/* Create prompt */
			sprintf(msg, "Choose Alien Oort Cloud Refinery kind");
			break;

		/* Error */
		default:
			display_error("Unknown choice type!\n");
			exit(1);
	}
}

/*
 * Store the player's next choice.
 */
static void replay_make_choice(game *g, int who, int type, int list[], int *nl,
                               int special[], int *ns, int arg1, int arg2,
                               int arg3)
{
	int i, current, next, orig_who;

	/* Compute the original player seat */
	orig_who = original_id(who);

	/* Check for new round */
	if (pass == 1 && who == 0 && g->round != current_round)
	{
		/* Update round */
		current_round = g->round;

		/* Loop over players */
		for (i = 0; i < g->num_players; ++i)
		{
			/* Update this round */
			decision_round[i][current_round] = num_choices[i];
		}
	}

	/* Check for second pass */
	if (pass == 2)
	{
		/* Find message */
		determine_message(g, who, type, list, nl, special, ns, arg1, arg2, arg3);

		/* Export the game */
		export(g, who);

		/* Clear the number of special cards */
		num_special_cards = 0;
	}

	/* Read the current choice position */
	current = g->p[who].choice_pos;

	/* Set next position */
	next = current;

	/* Loop until a non-debug choice is found */
	while (choice_logs[orig_who][next] < 0)
	{
		/* Compute the next choice position */
		next = next_choice(choice_logs[orig_who], next);
	}

	/* Take the next choice position */
	next = next_choice(choice_logs[orig_who], next);

	/* Copy choices from database */
	memcpy(g->p[who].choice_log + current, choice_logs[orig_who] + current,
	       sizeof(int) * (next - current));

	/* Update choice position */
	g->p[who].choice_size = next;

	/* Increase the number of choices */
	++num_choices[orig_who];
}

/*
 * Handle a game message.
 */
void message_add(game *g, char *txt)
{
	/* Save the message */
	save_message(txt, NULL, -1);
}

/*
 * Handle a formatted game message.
 */
void message_add_formatted(game *g, char *txt, char *tag)
{
	/* Save the message */
	save_message(txt, tag, -1);
}

/*
 * Handle a private message.
 */
void replay_private_message(game *g, int who, char *txt, char *tag)
{
	/* Save the message */
	save_message(txt, tag, original_id(who));
}

/*
 * Set of functions called by game engine to ask for choices.
 */
decisions replay_func =
{
	NULL,
	replay_notify_rotation,
	NULL,
	replay_make_choice,
	NULL,
	NULL,
	NULL,
	NULL,
	replay_private_message,
};

/*
 * Return the user name for a given user ID.
 */
static void db_user_name(int uid, char *name)
{
	MYSQL_RES *res;
	MYSQL_ROW row;
	char query[1024];

	/* Create query */
	sprintf(query, "SELECT user FROM users WHERE uid=%d", uid);

	/* Run query */
	mysql_query(mysql, query);

	/* Fetch results */
	res = mysql_store_result(mysql);

	/* Get row */
	row = mysql_fetch_row(res);

	/* Copy user name */
	strcpy(name, row[0]);

	/* Free result */
	mysql_free_result(res);
}

/*
 * Read game from database.
 */
static int db_load_game(int gid)
{
	MYSQL_RES *res;
	MYSQL_ROW row;
	int i, players = 0, uids[MAX_PLAYER];
	unsigned long *field_len;
	char query[1024];
	char name[80];

	/* Format query */
	sprintf(query, "SELECT exp, adv, dis_goal, dis_takeover \
	                FROM games WHERE gid = %d AND state = 'DONE'", gid);

	/* Run query */
	mysql_query(mysql, query);

	/* Fetch results */
	res = mysql_store_result(mysql);

	/* Check for no rows returned */
	if (!(row = mysql_fetch_row(res)))
	{
		/* Free result */
		mysql_free_result(res);

		/* No pool to load */
		printf("Could not load game\n");
		error = 1;
		return 0;
	}

	/* Clear simulated */
	g.simulation = 0;

	/* Read fields */
	g.expanded = strtol(row[0], NULL, 0);
	g.advanced = strtol(row[1], NULL, 0);
	g.goal_disabled = strtol(row[2], NULL, 0);
	g.takeover_disabled = strtol(row[3], NULL, 0);
	g.promo = 0;
	g.camp = NULL;

	/* Free results */
	mysql_free_result(res);

	/* Format query */
	sprintf(query, "SELECT uid, ai FROM attendance WHERE gid = %d ORDER BY seat",
	               gid);

	/* Run query */
	mysql_query(mysql, query);

	/* Fetch results */
	res = mysql_store_result(mysql);

	/* Loop over rows returned */
	while ((row = mysql_fetch_row(res)))
	{
		/* Store user ids */
		uids[players] = strtol(row[0], NULL, 0);

		/* Set player interface function */
		g.p[players].control = &replay_func;

		/* Create choice log */
		g.p[players].choice_log = (int *)malloc(sizeof(int) * 4096);
		choice_logs[players] = (int *)malloc(sizeof(int) * 4096);

		/* Get player's name */
		db_user_name(uids[players], name);

		/* Copy player's name */
		g.p[players].name = strdup(name);

		/* Copy ai information */
		g.p[players].ai = strtol(row[1], NULL, 0);

		/* Next player */
		++players;
	}

	/* Store the number of players */
	g.num_players = players;

	/* Check for advanced flag and more than two players */
	if (g.num_players > 2)
	{
		/* Clear advanced flag */
		g.advanced = 0;
	}

	/* Free results */
	mysql_free_result(res);

	/* Create query */
	sprintf(query, "SELECT pool FROM seed WHERE gid=%d", gid);

	/* Run query */
	mysql_query(mysql, query);

	/* Fetch results */
	res = mysql_store_result(mysql);

	/* Check for no rows returned */
	if (!(row = mysql_fetch_row(res)))
	{
		/* Free result */
		mysql_free_result(res);

		/* No pool to load */
		printf("Could not load random pool\n");
		return 0;
	}

	/* Copy returned data to random byte pool */
	memcpy(random_pool, row[0], MAX_RAND);

	/* Free result */
	mysql_free_result(res);

	/* Loop over players in session */
	for (i = 0; i < players; i++)
	{
		/* TODO: Join with query above */
		/* Create query to load choice log */
		sprintf(query,"SELECT log FROM choices WHERE gid=%d AND uid=%d",
		        gid, uids[i]);

		/* Run query */
		mysql_query(mysql, query);

		/* Fetch results */
		res = mysql_store_result(mysql);

		/* Check for no rows returned */
		if (!(row = mysql_fetch_row(res)))
		{
			/* Free result */
			mysql_free_result(res);

			/* Go to next player */
			continue;
		}

		/* Get length of log in bytes */
		field_len = mysql_fetch_lengths(res);

		/* Copy log */
		memcpy(choice_logs[i], row[0], field_len[0]);

		/* Remember length */
		choice_size[i] = field_len[0] / sizeof(int);

		/* Free result */
		mysql_free_result(res);
	}

	/* Success */
	return 1;
}

/*
 * Print errors to standard output.
 */
void display_error(char *msg)
{
	/* Forward message */
	printf("%s", msg);
}

/*
 * More complex random number generator for multiplayer games.
 *
 * Call simple RNG in simulated games, otherwise use the results from the
 * system RNG saved per session.
 */
int game_rand(game *g)
{
	unsigned int x;

	/* Check for simulated game */
	if (g->simulation)
	{
		/* Use simple random number generator */
		return simple_rand(&g->random_seed);
	}

	/* Check for end of random bytes reached */
	if (random_pos == MAX_RAND)
	{
		/* XXX Restart from beginning */
		random_pos = 0;
	}

	/* Create random number from next two bytes */
	x = random_pool[random_pos++];
	x |= random_pool[random_pos++] << 8;

	/* Return low bits */
	return x & 0x7fff;
}

/*
 * Replays a game.
 */
void replay_game()
{
	int i;

	/* Log message */
	printf("Replaying game %d (Pass %d)\n", gid, pass);

	/* Initialize game */
	init_game(&g);

	/* Begin game */
	begin_game(&g);

	/* Play game rounds until finished */
	while (game_round(&g));

	/* Score game */
	score_game(&g);

	/* Declare winner */
	declare_winner(&g);

	/* Check for first pass */
	if (pass == 1)
	{
		/* Store last round */
		num_rounds = g.round;

		/* Loop over players */
		for (i = 0; i < g.num_players; ++i)
		{
			/* Store end decisions */
			decision_round[i][num_rounds + 1] = num_choices[i];
		}

		/* Export full game */
		export_end(&g);
	}
	else
	{
		/* Clear message */
		strcpy(msg, "");

		/* Loop over players */
		for (i = 0; i < g.num_players; ++i)
		{
			/* Export the game */
			export(&g, i);
		}
	}
}

/*
 * Initialize connection to database, load game and replay it.
 */
int main(int argc, char *argv[])
{
	int i, j;
	my_bool reconnect = 1;
	char *db = "rftg";
	char *db_user = "rftg";
	char *db_pw = NULL;
	char buf[1024];

	/* Parse arguments */
	for (i = 1; i < argc; i++)
	{
		/* Check for help */
		if (!strcmp(argv[i], "-h"))
		{
			/* Print usage */
			printf("Race for the Galaxy replay utility, version " RELEASE "\n\n");
			printf("Arguments:\n");
			printf("  -gs    Game id to start replay from\n");
			printf("  -ge    Game id to end replay at\n");
			printf("  -d     MySQL database name. Default: \"rftg\"\n");
			printf("  -u     MySQL database user. Default: \"rftg\"\n");
			printf("  -pw    MySQL database password. Default: [none]\n");
			printf("  -e     Folder to put exported games. Default: \".\"\n");
			printf("  -s     Server name (to be used in exports). Default: [none]\n");
			printf("  -ss    XSLT style sheets for exported complete games. Default: [none]\n");
			printf("  -ssr   XSLT style sheets for exported replay games. Default: [none]\n");
			printf("  -v     Verbose (print messages as they appear). Default: false.\n\n");
			printf("  -h     Print this usage text and exit.\n\n");
			printf("For more information, see the following web sites:\n");
			printf("  http://keldon.net/rftg\n  http://dl.dropbox.com/u/7379896/rftg/index.html\n");
			exit(1);
		}

		/* Check for start game id */
		if (!strcmp(argv[i], "-gs"))
		{
			/* Set start game id */
			gid_min = atoi(argv[++i]);
		}

		/* Check for end game id */
		if (!strcmp(argv[i], "-ge"))
		{
			/* Set end game id */
			gid_max = atoi(argv[++i]);
		}

		/* Check for database name */
		if (!strcmp(argv[i], "-d"))
		{
			/* Set database name */
			db = argv[++i];
		}

		/* Check for database user */
		if (!strcmp(argv[i], "-u"))
		{
			/* Set database user */
			db_user = argv[++i];
		}

		/* Check for database password */
		if (!strcmp(argv[i], "-pw"))
		{
			/* Set database password */
			db_pw = argv[++i];
		}

		/* Check for server name */
		if (!strcmp(argv[i], "-s"))
		{
			/* Set server name */
			server_name = argv[++i];
		}

		/* Check for exports folder */
		if (!strcmp(argv[i], "-e"))
		{
			/* Set exports folder */
			export_folder = argv[++i];
		}

		/* Check for export style sheet */
		if (!strcmp(argv[i], "-ss"))
		{
			/* Set style sheet */
			export_style_sheet = argv[++i];
		}

		/* Check for export style sheet */
		if (!strcmp(argv[i], "-ssr"))
		{
			/* Set style sheet */
			export_style_sheet_replay = argv[++i];
		}

		/* Check for verbosity */
		if (!strcmp(argv[i], "-v"))
		{
			/* Set verbosity */
			verbose = 1;
		}
	}

	/* Make a copy of the program path */
	strcpy(buf, argv[0]);

	/* Search for path in program path */
	if (strrchr(buf, '/'))
	{
		/* Make a copy */
		strcpy(buf, argv[0]);

		/* Chop string */
		*strrchr(buf, '/') = '\0';

		/* Read card library */
		if (read_cards(buf) < 0)
		{
			/* Exit */
			exit(1);
		}
	}
	else
	{
		/* Read card library */
		if (read_cards(NULL) < 0)
		{
			/* Exit */
			exit(1);
		}
	}

	/* Initialize database library */
	mysql = mysql_init(NULL);

	/* Check for error */
	if (!mysql)
	{
		/* Print error and exit */
		printf("Couldn't initialize database library!");
		exit(1);
	}

	/* Attempt to connect to database server */
	if (!mysql_real_connect(mysql, NULL, db_user, db_pw, db, 0, NULL, 0))
	{
		/* Print error and exit */
		printf("Database connection: %s", mysql_error(mysql));
		exit(1);
	}

	/* Reconnect automatically when connection to database is lost */
	mysql_options(mysql, MYSQL_OPT_RECONNECT, &reconnect);

	/* Loop over all games */
	for (i = gid_min; i <= gid_max; ++i)
	{
		/* Set game */
		gid = i;

		/* Run two passes */
		for (pass = 1; pass <= 2; ++pass)
		{
			/* Read game state from database */
			if (!db_load_game(gid)) continue;

			/* Clear fields */
			random_pos = 0;
			current_round = 0;
			rotations = 0;
			num_messages = 0;

			/* Loop over players */
			for (j = 0; j < MAX_PLAYER; ++j)
			{
				/* Clear player fields */
				num_choices[j] = 0;
				decision_round[j][0] = 0;
				log_pos[j] = 0;

				/* Clear choice log size and position */
				g.p[j].choice_size = 0;
				g.p[j].choice_pos = 0;
			}

			/* Replay the game */
			replay_game();

			/* Check for only one pass needed */
			if (!export_style_sheet_replay) break;
		}
	}

	/* Return error value */
	return error;
}
