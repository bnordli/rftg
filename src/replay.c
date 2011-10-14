/*
 * Race for the Galaxy AI
 * 
 * Copyright (C) 2009-2011 Keldon Jones
 *
 * Source file modified by B. Nordli, October 2011.
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

/*
 * Number of random bytes stored per game.
 */
#define MAX_RAND     1024

/* Pool of random bytes. */
static unsigned char random_pool[MAX_RAND];

/* Current position in random pool */
static int random_pos;

/* The gid to replay */
static int gid;

/* Game to be replayed */
static game g;

/* Choices loaded from db */
static int *choice_logs[MAX_PLAYER];

/* Size of choice_logs */
static int choice_size[MAX_PLAYER] = { 0 };

/* Number of decisions */
static int num_choices[MAX_PLAYER] = { 0 };

/* Number of game rotations */
static int rotations = 0;

/* Compute the original player id of a player */
static int original_id(int who)
{
	/* Start with substracting the number of rotations */
	int ret = who + rotations;
	
	/* If overflow, find the correct player */
	return ret < g.num_players ? ret : ret - g.num_players;
}

/* A log message */
typedef struct message
{
	/* The text of the message */
	char text[150];
	
	/* The format of the message */
	char* format;
	
	/* The player id if a private message */
	int player;

} message;

/* The saved log */
static message log[4096];

/* The number of messages so far */
static int num_messages = 0;

/* Log position for previous choice */
static int log_pos[MAX_PLAYER] = { 0 };

/*
 * Log exports folder.
 */
static char* export_folder = ".";

/*
 * Export file style sheet.
 */
static char* export_style_sheet = NULL;

/*
 * Server name (used in exports).
 */
static char* server_name = NULL;

/*
 * Connection to the database server.
 */
MYSQL *mysql;

/* Save a game message */
static void save_message(char *txt, char *tag, int player)
{
	/* Print the message */
	printf("%s", txt);
	
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
 * Player spots have been rotated.
 */
static void replay_notify_rotation(game *g, int who)
{
	/* Only rotate once per set of players */
	if (who == 0) ++rotations;
}

/*
 * Export log from a specific player.
 */
static void export_log(FILE *fff, int who)
{
	int i;
	char name[1024];

	/* Loop over messages */
	for (i = log_pos[who]; i < num_messages; ++i)
	{
		/* Check for private message */
		if (log[i].player >= 0)
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
	log_pos[who] = num_messages;
}

/* Export the game seen from a specific player */
static void export(game *g, int who)
{
	int orig_who;
	char filename[1024];
	
	/* Compute the original player seat */
	orig_who = original_id(who);
	
	/* Create file name */
	sprintf(filename, "%s/Game_%06d_p%d_d%d.xml", export_folder, gid, orig_who, num_choices[orig_who]);
	
	/* Export game to file */
	if (export_game(g, filename, export_style_sheet, server_name,
	    -1, who, 0, NULL, 0, NULL, export_log, orig_who) < 0)
	{
		/* Log error */
		printf("Could not export game to %s\n", filename);
	}
	else
	{
		/* Log export location */
		printf("Game exported to %s\n", filename);
	}
}

/*
 * Store the player's next choice.
 */
static void replay_make_choice(game *g, int who, int type, int list[], int *nl,
                               int special[], int *ns, int arg1, int arg2,
                               int arg3)
{
	int current, next, orig_who;

	/* Compute the original player seat */
	orig_who = original_id(who);

	/* TODO: message and special cards */
	
	/* Export the game */
	export(g, who);

	/* Read the current choice position */
	current = g->p[who].choice_pos;

	/* Compute the next choice position */
	next = next_choice(choice_logs[orig_who], current);
	
	/* Copy choices from database */
	memcpy(g->p[who].choice_log + current, choice_logs[orig_who] + current, sizeof(int) * (next - current));
	
	/* Update choice position */
	g->p[who].choice_size = next;

	/* Increase the number of choices */
	++num_choices[orig_who];
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
 * Set of functions called by game engine to notify/ask clients.
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
static void db_load_game(int gid)
{
	MYSQL_RES *res;
	MYSQL_ROW row;
	int i, players = 0, uids[MAX_PLAYER];
	unsigned long *field_len;
	char query[1024];
	char name[80];
	
	/* Format query */
	sprintf(query, "SELECT exp, adv, dis_goal, dis_takeover \
	                FROM games WHERE gid = %d", gid);

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
		exit(1);
	}
	
	/* Clear simulated */
	g.simulation = 0;

	/* Read fields */
	g.expanded = strtol(row[0], NULL, 0);
	g.advanced = strtol(row[1], NULL, 0);
	g.goal_disabled = strtol(row[2], NULL, 0);
	g.takeover_disabled = strtol(row[3], NULL, 0);
		
	/* Free results */
	mysql_free_result(res);

	/* Format query */
	sprintf(query, "SELECT uid FROM attendance WHERE gid = %d ORDER BY seat", gid);

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

		/* Clear choice log size and position */
		g.p[players].choice_size = 0;
		g.p[players].choice_pos = 0;

		/* Get player's name */
		db_user_name(uids[players], name);

		/* Copy player's name */
		g.p[players].name = strdup(name);
	
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
		exit(1);
	}

	/* Copy returned data to random byte pool */
	memcpy(random_pool, row[0], MAX_RAND);

	/* Start at beginning of byte pool */
	random_pos = 0;

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

	/* Loop over players */
	for (i = 0; i < g.num_players; ++i)
	{
		/* Export the game */
		export(&g, i);
	}
}

/*
 * Initialize connection to database, open main listening socket, then loop
 * forever waiting for incoming data on connections.
 */
int main(int argc, char *argv[])
{
	int i;
	my_bool reconnect = 1;
	char *db = "rftg";

	/* Parse arguments */
	for (i = 1; i < argc; i++)
	{
		/* Check for help */
		if (!strcmp(argv[i], "-h"))
		{
			/* Print usage */
			printf("Race for the Galaxy replay utility, version " RELEASE "\n\n");
			printf("Arguments:\n");
			printf("  -g     Game id to replay\n");
			printf("  -d     MySQL database name. Default: \"rftg\"\n");
			printf("  -e     Folder to put exported games. Default: \".\"\n");
			printf("  -s     Server name (to be used in exports). Default: [none]\n");
			printf("  -ss    XSLT style sheets for exported games. Default: [none]\n");
			printf("  -h     Print this usage text and exit.\n\n");
			printf("For more information, see the following web sites:\n");
			printf("  http://keldon.net/rftg\n  http://dl.dropbox.com/u/7379896/rftg/index.html\n");
			exit(1);
		}

		/* Check for database name */
		if (!strcmp(argv[i], "-g"))
		{
			/* Set database name */
			gid = atoi(argv[++i]);
		}

		/* Check for database name */
		if (!strcmp(argv[i], "-d"))
		{
			/* Set database name */
			db = argv[++i];
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
	}

	/* Read card library */
	if (read_cards() < 0)
	{
		/* Exit */
		exit(1);
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
	if (!mysql_real_connect(mysql, NULL, "rftg", NULL, db, 0, NULL, 0))
	{
		/* Print error and exit */
		printf("Database connection: %s", mysql_error(mysql));
		exit(1);
	}

	/* Reconnect automatically when connection to database is lost */
	mysql_options(mysql, MYSQL_OPT_RECONNECT, &reconnect);
	
	/* Read game state from database */
	db_load_game(gid);

    /* Replay the game */	
	replay_game();
	
	/* Success */
	return 1;
}


/*
 * Send message to server.
 */
void send_msg(int fd, char *msg)
{
}
