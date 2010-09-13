/*
 * Race for the Galaxy AI
 * 
 * Copyright (C) 2009 Keldon Jones
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
 * Print messages to standard output.
 */
void message_add(char *msg)
{
	/* Print if verbose flag set */
	if (verbose) printf("%s", msg);
}

/*
 * Play a number of training games.
 */
int main(int argc, char *argv[])
{
	game my_game;
	int i, j, n = 100;
	int num_players = 3;
	int expansion = 0, advanced = 0;
	int seed;
	char buf[1024];
	double factor = 1.0;

	/* Set random seed */
	my_game.random_seed = time(NULL);

	/* Read card database */
	read_cards();

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

	/* Call initialization functions */
	for (i = 0; i < num_players; i++)
	{
		/* Create player name */
		sprintf(buf, "Player %d", i);

		/* Set player name */
		my_game.p[i].name = strdup(buf);

		/* Set player interfaces to AI functions */
		my_game.p[i].control = &ai_func;

		/* Initialize AI */
		my_game.p[i].control->init(&my_game, i, factor);
	}

	/* Play a number of games */
	for (i = 0; i < n; i++)
	{
		/* Initialize game */
		init_game(&my_game);

		/* Play game rounds until finished */
		while (game_round(&my_game));

		/* Score game */
		score_game(&my_game);

		/* Print result */
		for (j = 0; j < num_players; j++)
		{
			/* Print score */
			printf("Player %d: %d\n", j, my_game.p[j].end_vp);
		}

		/* Declare winner */
		declare_winner(&my_game);

		/* Print winner */
		for (j = 0; j < num_players; j++)
		{
			/* Skip non-winner */
			if (!my_game.p[j].winner) continue;

			/* Do nothing if not verbose */
			if (!verbose) break;

			/* Print winner message */
			printf("Player %d wins with %d.\n", j,
			                                   my_game.p[j].end_vp);
		}

		/* Call player game over functions */
		for (j = 0; j < num_players; j++)
		{
			/* Call game over function */
			my_game.p[j].control->game_over(&my_game, j);
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
