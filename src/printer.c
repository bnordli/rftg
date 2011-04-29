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
 * Print all messages?
 */
static int verbose = 0;

/*
 * Format messages?
 */
static int formatted = 0;

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
 * Substite file name.
 */
static char *subs_file = NULL;

/*
 * Substitute for card names.
 */
static char substitutes[MAX_DESIGN][1024];

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
	/* Check for formatted */
	if (!formatted)
	{
		/* Print without formatting */
		message_add(g, msg);
	}

	/* Bold format */
	if (!strcmp(tag, FORMAT_BOLD))
		printf("[b]%s[/b]", msg);
	/* Phase (blue) */
	else if (!strcmp(tag, FORMAT_PHASE))
		printf("[color=#0000aa]%s[/color]", msg);
	/* Phase (blue) */
	else if (!strcmp(tag, FORMAT_TAKEOVER))
		printf("[color=#ff0000]%s[/color]", msg);
	/* Goal (yellow) */
	else if (!strcmp(tag, FORMAT_GOAL))
		printf("[color=#eeaa00]%s[/color]", msg);
	/* Prestige (purple) */
	else if (!strcmp(tag, FORMAT_PRESTIGE))
		printf("[color=#8800bb]%s[/color]", msg);
	/* Verbose (gray) */
	else if (!strcmp(tag, FORMAT_VERBOSE))
		printf("[color=#aaaaaa]%s[/color]", msg);
	/* Discard (gray) */
	else if (!strcmp(tag, FORMAT_DISCARD))
		printf("[color=#aaaaaa]%s[/color]", msg);
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
	/* Only rotate once */
	if (who != 0) return;

	/* Remember our new player index */
	player_us--;

	/* Handle wraparound */
	if (player_us < 0) player_us = g->num_players - 1;
}

/*
 * Function to compare two cards in a table for sorting.
 */
static int cmp_table(const void *h1, const void *h2)
{
	card *c_ptr1 = (card *)h1, *c_ptr2 = (card *)h2;

	/* Sort by order played */
	return c_ptr1->order - c_ptr2->order;
}

/*
 * Function to compare two cards in a hand for sorting.
 */
static int cmp_hand(const void *h1, const void *h2)
{
	card *c_ptr1 = (card *)h1, *c_ptr2 = (card *)h2;

	/* Worlds come before developments */
	if (c_ptr1->d_ptr->type != c_ptr2->d_ptr->type)
	{
		/* Check for development */
		if (c_ptr1->d_ptr->type == TYPE_DEVELOPMENT) return 1;
		if (c_ptr2->d_ptr->type == TYPE_DEVELOPMENT) return -1;
	}

	/* Sort by cost */
	if (c_ptr1->d_ptr->cost != c_ptr2->d_ptr->cost)
	{
		/* Return cost difference */
		return c_ptr1->d_ptr->cost - c_ptr2->d_ptr->cost;
	}

	/* Otherwise sort by index */
	return c_ptr1->d_ptr->index - c_ptr2->d_ptr->index;
}

/*
 * Print cards of the linked list starting with x in a specified order.
 */
static void print_cards(game *g, int x, int (*cmp)(const void *, const void *))
{
	int n, p;
	card cards[MAX_DECK];

	/* Loop over cards */
	for (n = 0 ; x != -1; x = g->deck[x].next, ++n)
	{
		/* Save card */
		cards[n] = g->deck[x];
	}

	/* Sort the cards */
	qsort(cards, n, sizeof(card), cmp);

	/* Loop over sorted cards */
	for (p = 0; p < n; ++p)
	{
		/* Check for substitutes */
		if (subs_file)
		{
			/* Print substitute */
			printf("%s", substitutes[cards[p].d_ptr->index]);
		}
		else
		{
			/* Print card name */
			printf("%s", cards[p].d_ptr->name);
		}

		/* Print newline if needed */
		if ((p+1) % cards_per_line == 0) printf("\n");
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
		printf("%s's tableau:\n", g->p[i].name);

		/* Dump tableau */
		print_cards(g, g->p[i].head[WHERE_ACTIVE], cmp_table);

		/* Print newline */
		printf("\n");

		/* Check for all players traversed */
		if (i == who) break;
	}

	/* Print hand header */
	printf("Hand:\n");

	/* Dump hand */
	print_cards(g, g->p[who].head[WHERE_HAND], cmp_hand);
}

/*
 * Choice function: Just end the game.
 */
static void printer_make_choice(game *g, int who, int type, int list[], int *nl,
                                int special[], int *ns, int arg1, int arg2, int arg3)
{
	/* Simulate game over */
	g->game_over = 2;
}

/*
 * Private message.
 */
void printer_private_message(struct game *g, int who, char *msg, char *tag)
{
	/* Add message if player matches */
	if (who == player_us) message_add_formatted(g, msg, tag);
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

		/* Check for formatted messages */
		if (!strcmp(argv[i], "-f"))
		{
			/* Set formatted flag */
			formatted++;
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
		printf("No save file supplied!\n");
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
		printf("Failed to load game from file %s.\n", save_file);
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

	/* Check if game ended normally */
	if (my_game.game_over == 1)
	{
		/* Declare winner */
		declare_winner(&my_game);
	}

	/* Print separator if verbose */
	if (verbose) printf("======================\n");

	/* Dump game */
	print_game(&my_game, player_us);

	/* Done */
	return 0;
}
