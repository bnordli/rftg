/*
 * Race for the Galaxy AI
 *
 * Copyright (C) 2009-2015 Keldon Jones
 *
 * Source file modified by B. Nordli, August 2015.
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
 * Print errors to standard output.
 */
void display_error(char *msg)
{
	/* Forward message */
	printf("%s", msg);
}

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
 * Play a number of training games.
 */
int main(int argc, char *argv[])
{
	game my_game;
	int i, j, n = 100;
	int num_players = 3;
	int expansion = 0, advanced = 0, promo = 0;
	char buf[1024], *names[MAX_PLAYER];
	double factor = 1.0;

	/* Set random seed */
	my_game.random_seed = time(NULL);

	/* Read card database */
	if (read_cards(NULL) < 0)
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

		/* Check for number of players */
		else if (!strcmp(argv[i], "-p"))
		{
			/* Set number of players */
			num_players = atoi(argv[++i]);
		}

		/* Check for advanced game */
		else if (!strcmp(argv[i], "-a"))
		{
			/* Set advanced flag */
			advanced = 1;
		}

		/* Check for expansion level */
		else if (!strcmp(argv[i], "-e"))
		{
			/* Set expansion level */
			expansion = atoi(argv[++i]);
		}

		/* Check for promo cards */
		else if (!strcmp(argv[i], "-o"))
		{
			/* Set promo cards */
			promo = 1;
		}

		/* Check for number of games */
		else if (!strcmp(argv[i], "-n"))
		{
			/* Set number of games */
			n = atoi(argv[++i]);
		}

		/* Check for random seed */
		else if (!strcmp(argv[i], "-r"))
		{
			/* Set random seed */
			my_game.random_seed = atoi(argv[++i]);
		}

		/* Check for alpha factor */
		else if (!strcmp(argv[i], "-f"))
		{
			/* Set factor */
			factor = atof(argv[++i]);
		}
	}

	/* Set number of players */
	my_game.num_players = num_players;

	/* Set expansion level */
	my_game.expanded = expansion;

	/* Set advanced flag */
	my_game.advanced = advanced;

	/* Set promo flag */
	my_game.promo = promo;

	/* Assume no options disabled */
	my_game.goal_disabled = 0;
	my_game.takeover_disabled = 0;

	/* No campaign selected */
	my_game.camp = NULL;

	/* Call initialization functions */
	for (i = 0; i < num_players; i++)
	{
		/* Create player name */
		sprintf(buf, "Player %d", i);

		/* Set player name */
		my_game.p[i].name = strdup(buf);
		names[i] = my_game.p[i].name;

		/* Set player interfaces to AI functions */
		my_game.p[i].control = &ai_func;

		/* Initialize AI */
		my_game.p[i].control->init(&my_game, i, factor);

		/* Create choice log for player */
		my_game.p[i].choice_log = (int *)malloc(sizeof(int) * 4096);

		/* Clear choice log size and position */
		my_game.p[i].choice_size = 0;
		my_game.p[i].choice_pos = 0;
	}

	/* Play a number of games */
	for (i = 0; i < n; i++)
	{
		/* Initialize game */
		init_game(&my_game);

		/* Game is learning game */
		my_game.session_id = -2;

		printf("Start seed: %u\n", my_game.start_seed);

		/* Begin game */
		begin_game(&my_game);

		/* Play game rounds until finished */
		while (game_round(&my_game));

		/* Score game */
		score_game(&my_game);

		/* Print result */
		for (j = 0; j < num_players; j++)
		{
			/* Print score */
			printf("%s: %d\n", my_game.p[j].name,
			                   my_game.p[j].end_vp);
		}

		/* Declare winner */
		declare_winner(&my_game);

		/* Call player game over functions */
		for (j = 0; j < num_players; j++)
		{
			/* Call game over function */
			my_game.p[j].control->game_over(&my_game, j);

			/* Clear choice log */
			my_game.p[j].choice_size = 0;
			my_game.p[j].choice_pos = 0;
		}

		/* Reset player names */
		for (j = 0; j < num_players; j++)
		{
			/* Reset name */
			my_game.p[j].name = names[j];
		}
	}

	/* Call interface shutdown functions */
	for (i = 0; i < num_players; i++)
	{
		/* Call shutdown function */
		my_game.p[i].control->shutdown(&my_game, i);
	}

	/* Done */
	return 0;
}
