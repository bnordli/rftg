/*
 * Race for the Galaxy AI
 * 
 * Copyright (C) 2009-2011 Keldon Jones
 *
 * Source file modified by B. Nordli, April 2011.
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

/*
 * Print messages?
 */
int verbose = 0;

/*
 * Player we're playing as.
 */
int player_us = 0;

/*
 * Default player names.
 */
static char *player_names[MAX_PLAYER] =
{
	"Blue",
	"Red",
	"Green",
	"Yellow",
	"Cyan",
	"Purple",
};

/*
 * Print messages to standard output.
 */
void message_add(game *g, char *msg)
{
	/* Print if verbose flag set */
	if (verbose) printf("%s", msg);
}

/*
 * Print messages to standard output.
 */
void message_add_formatted(game *g, char *msg, char *tag)
{
	/* Print without formatting */
	message_add(g, msg);
}

/*
 * Use simple random number generator.
 */
int game_rand(game *g)
{
	/* Call simple random number generator */
	return simple_rand(&g->random_seed);
}

/*
 * Player spots have been rotated.
 */
static void printer_notify_rotation(game *g, int who)
{
	/* Remember our new player index */
	player_us--;

	/* Handle wraparound */
	if (player_us < 0) player_us = g->num_players - 1;
}

/*
 * Print cards of the linked list starting with x, in reverse order.
 */
static void print_reverse(game *g, int x)
{
	int p;
	int cards[MAX_DECK];

	/* Loop over cards */
	for (p = 0 ; x != -1; x = g->deck[x].next, ++p)
	{
		/* Save card */
		cards[p] = x;
	}

	/* Decrease pointer by one */
	p--;

	/* Traverse cards in reverse order */
	for (; p >= 0; --p)
	{
		/* Print card name */
		printf("%s\n", g->deck[cards[p]].d_ptr->name);
	}
}


/*
 * Print the game state to stdout.
 */
static void print_game(game *g, int who)
{
	int i;

	/* Loop over all players */
	for (i = who + 1; ; ++i)
	{
		/* Check for wrap around */
		if (i == g->num_players) i = 0;

		/* Print player name */
		printf("%s tableau:\n", g->p[i].name);

		/* Dump active cards */
		print_reverse(g, g->p[i].head[WHERE_ACTIVE]);

		/* Print newline */
		printf("\n");

		/* Check for all players traversed */
		if (i == who) break;
	}

	/* Print hand header */
	printf("Hand:\n");

	/* Dump hand */
	print_reverse(g, g->p[who].head[WHERE_HAND]);
}

/*
 * Printer choice: Just end the game.
 */
static void printer_make_choice(game *g, int who, int type, int list[], int *nl,
                                int special[], int *ns, int arg1, int arg2, int arg3)
{
	/* Set game over */
	g->game_over = 1;
}


/*
 * Private message.
 */
void printer_private_message(struct game *g, int who, char *msg, char *tag)
{
	/* Add message if player matches */
	if (who == player_us) message_add(g, msg);
}

/*
 * Set of printer functions.
 */
decisions text_func =
{
	NULL,
	printer_notify_rotation,
	NULL,
	printer_make_choice,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	printer_private_message,
};

/*
 * Load a game and simulate until end of choice log.
 */
int main(int argc, char *argv[])
{
	game my_game;
	int i;
	char buf[1024];
	char *fname = NULL;

	/* Set random seed */
	my_game.random_seed = time(NULL);

	/* Read card database */
	if (read_cards() < 0)
	{
		/* Exit */
		exit(1);
	}

	/* Parse arguments */
	for (i = 1; i < argc; i++)
	{
		/* Check for verbosity */
		if (!strcmp(argv[i], "-v"))
		{
			/* Set verbose flag */
			verbose++;
		}

		/* Check for debug */
		if (!strcmp(argv[i], "-d"))
		{
			/* Wait some time */
			for (i = 0; i < 2000000000; ++i);
		}

		/* Default argument is path to saved game */
		else if (!fname)
		{
			/* Set file name */
			fname = argv[i];
		}
	}

	/* Check for missing file name */
	if (!fname)
	{
		/* Print error and exit */
		printf("No save file supplied! (use -s)");
		exit(1);
	}

	/* Initialize players */
	for (i = 0; i < MAX_PLAYER; i++)
	{
		/* Set player interfaces to printer functions */
		my_game.p[i].control = &text_func;

		/* Create choice log for player */
		my_game.p[i].choice_log = (int *)malloc(sizeof(int) * 4096);

		/* Set player name */
		my_game.p[i].name = player_names[i];
	}

	/* Try to load savefile into load state */
	if (load_game(&my_game, fname) < 0)
	{
		/* Print error and exit */
		printf(buf, "Failed to load game from file %s\n", fname);
		exit(1);
	}

	/* Start with start of game random seed */
	my_game.random_seed = my_game.start_seed;

	/* Initialize game */
	init_game(&my_game);

	/* Begin game */
	begin_game(&my_game);

	/* Play game rounds until finished */
	while (game_round(&my_game));

	/* Print separator if verbose */
	if (verbose) printf("====================\n");

	/* Dump game */
	print_game(&my_game, player_us);

	/* Done */
	return 0;
}
