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
static int verbose = 0;

/*
 * Player we're playing as.
 */
static int player_us = 0;

/*
 * Number of cards before newline separator.
 */
static int cards_per_line = 1;

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
 * Substite card file.
 */
static char *subs_file = NULL;

/*
 * Substitute for card names.
 */
static char substitutes[MAX_DESIGN][64];

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
	int n, p;
	int cards[MAX_DECK];

	/* Loop over cards */
	for (n = 0 ; x != -1; x = g->deck[x].next, ++n)
	{
		/* Save card */
		cards[n] = x;
	}

	/* Traverse cards in reverse order */
	for (p = 1; p <= n; ++p)
	{
		/* Check for substitutes */
		if (subs_file)
		{
			/* Print substitute */
			printf("%s ", substitutes[g->deck[cards[n - p]].d_ptr->index]);
		}
		else
		{
			/* Print card name */
			printf("%s ", g->deck[cards[n - p]].d_ptr->name);
		}

		/* Print newline if needed */
		if (p % cards_per_line == 0) printf("\n");
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
 * Choice function: Just end the game.
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
 * Read substitutes file.
 */
static void read_subs()
{
	FILE *fff;
	char buf[1024];
	char *p;
	int card_found, i;

	/* Clear all substitute strings */
	for (i = 0; i < MAX_DESIGN; ++i)
		substitutes[i][0] = '\0';

	/* Open file for reading */
	fff = fopen(subs_file, "r");

	/* Check for failure */
	if (!fff)
	{
		/* Print error and exit */
		printf("Could not open substitute file %s.\n", subs_file);
		exit(1);
	}

	/* Read first line */
	fgets(buf, 1024, fff);

	/* Set cards per line */
	cards_per_line = atoi(buf);

	/* Check for failure */
	if (cards_per_line == 0)
	{
		/* Print error and exit */
		printf("Could not parse first line "
		       "of substitute file as non-zero integer:\n%s\n", buf);
		exit(1);
	}

	/* Read lines */
	while (fgets(buf, 1024, fff))
	{
		/* Strip newline */
		if (buf[strlen(buf) - 1] == '\n') buf[strlen(buf) - 1] = '\0';

		/* Locate separator */
		p = strchr(buf, ';');

		/* Check for separator not found */
		if (!p)
		{
			/* Print error and exit */
			printf("Could not find separator (;) in line %s.\n", buf);
			exit(1);
		}

		/* End card name */
		*p = '\0';

		/* Move to start of substutite string */
		++p;

		/* Clear card found flag */
		card_found = 0;

		/* Loop over all designs */
		for (i = 0; i < MAX_DESIGN; ++i)
		{
			/* Check for matching card name */
			if (!strcmp(buf, library[i].name))
			{
				/* Copy substitute string */
				strcpy(substitutes[i], p);

				/* Remember that card is found */
				card_found = 1;
			}
		}

		/* Check whether card was found */
		if (!card_found)
		{
			/* Print error and exit */
			printf("Could not recognize card name %s.\n", buf);
			exit(1);
		}
	}

	/* Loop over all designs */
	for (i = 0; i < MAX_DESIGN; ++i)
	{
		/* Check for empty substitute */
		if (strlen(substitutes[i]) == 0)
		{
			/* Print error and exit */
			printf("Did not find substitute string for %s.\n",
				library[i].name);
			exit(1);
		}
	}
}

/*
 * Load a game and simulate until end of choice log.
 */
int main(int argc, char *argv[])
{
	game my_game;
	int i;
	char buf[1024];
	char *save_file = NULL;

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

		/* Check for substitutes */
		if (!strcmp(argv[i], "-s"))
		{
			/* Set substitute file name */
			subs_file = argv[++i];
		}

		/* Default argument is path to saved game */
		else if (!save_file)
		{
			/* Set file name */
			save_file = argv[i];
		}
	}

	/* Check for missing file name */
	if (!save_file)
	{
		/* Print error and exit */
		printf("No save file supplied! (use -s)");
		exit(1);
	}

	/* Check for substitute file requested */
	if (subs_file)
	{
		read_subs();
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
	if (load_game(&my_game, save_file) < 0)
	{
		/* Print error and exit */
		printf(buf, "Failed to load game from file %s\n", save_file);
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
