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
 * Load a game from the given filename.
 */
int load_game(game *g, char *filename)
{
	FILE *fff;
	player *p_ptr;
	char buf[1024];
	char version[1024];
	int i, j;

	/* Open file for reading */
	fff = fopen(filename, "r");

	/* Check for failure */
	if (!fff) return -1;

	/* Read header */
	fgets(buf, 1024, fff);

	/* Check for correct header */
	if (strcmp(buf, "RFTG Save\n")) return -1;

	/* Read version line */
	fgets(version, 1024, fff);

	/* Strip newline from version */
	version[strlen(version) - 1] = '\0';

	/* Check for too new version */
	if (strcmp(version, VERSION) > 0) return -1;

	/* Check for too old version */
	if (strcmp(version, "0.7.2") < 0) return -1;

	/* Read random seed information */
	fscanf(fff, "%u\n", &g->start_seed);

	/* Read game setup information */
	fscanf(fff, "%d %d\n", &g->num_players, &g->expanded);
	fscanf(fff, "%d %d %d\n", &g->advanced, &g->goal_disabled,
	                          &g->takeover_disabled);

	/* Clear simulation flag */
	g->simulation = 0;

	/* Load over players */
	for (i = 0; i < g->num_players; i++)
	{
		/* Get player pointer */
		p_ptr = &g->p[i];

		/* Read choice log size */
		fscanf(fff, "%d", &p_ptr->choice_size);

		/* Loop over choice log entries */
		for (j = 0; j < p_ptr->choice_size; j++)
		{
			/* Read choice log entry */
			fscanf(fff, "%d", &p_ptr->choice_log[j]);
		}

		/* Reset choice log position */
		p_ptr->choice_pos = 0;
	}

	/* Reset human name */
	g->human_name = NULL;

	/* Skip to next line */
	if (fgets(buf, 1024, fff))
	{
		/* Read player name (if any) */
		if (fgets(buf, 50, fff))
		{
			/* Check for end of line */
			if (buf[strlen(buf) - 1] == '\n')
			{
				/* Strip newline from buffer */
				buf[strlen(buf) - 1] = '\0';
			}

			/* If still characters left, set human name */
			if (strlen(buf)) g->human_name = strdup(buf);
		}
	}

	/* Close file */
	fclose(fff);

	/* Success */
	return 0;
}

/*
 * Save a game to the given filename.
 */
int save_game(game *g, char *filename, int player_us)
{
	FILE *fff;
	player *p_ptr;
	int i, j, n;

	/* Open file for writing */
	fff = fopen(filename, "w");

	/* Check for failure */
	if (!fff) return -1;

	/* Write header information */
	fputs("RFTG Save\n", fff);
	fprintf(fff, "%s\n", VERSION);

	/* Write start of game random seed */
	fprintf(fff, "%u\n", g->start_seed);

	/* Write game setup information */
	fprintf(fff, "%d %d\n", g->num_players, g->expanded);
	fprintf(fff, "%d %d %d\n", g->advanced, g->goal_disabled,
	                           g->takeover_disabled);

	/* Loop over players */
	for (i = 0; i < g->num_players; i++)
	{
		/* Get player index to save next */
		n = (player_us + i) % g->num_players;

		/* Get player pointer */
		p_ptr = &g->p[n];

		/* Write size of choice log */
		fprintf(fff, "%d ", p_ptr->choice_unread_pos);

		/* Loop over choice log entries */
		for (j = 0; j < p_ptr->choice_unread_pos; j++)
		{
			/* Write choice log entry */
			fprintf(fff, "%d ", p_ptr->choice_log[j]);
		}

		/* Finish line */
		fprintf(fff, "\n");
	}

	/* Save name of human player (if any) */
	if (g->human_name && strlen(g->human_name))
		fprintf(fff, "%s\n", g->human_name);

	/* Close file */
	fclose(fff);

	/* Success */
	return 0;
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
 * Print cards of the linked list starting with x, in a specified order.
 */
static void export_cards(FILE *fff, game *g, int x,
                         int (*cmp)(const void *, const void *))
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
		/* Print card name (* indicating good) */
		fprintf(fff, "%s%s;", cards[p].d_ptr->name,
			    (cards[p].covered != -1 ? "*" : ""));
	}

	/* Finish line */
	fputs("\n", fff);
}

/*
 * Export the game state to the given filename.
 */
int export_game(game *g, char *filename, int player_us)
{
	FILE *fff;
	player *p_ptr;
	card *c_ptr;
	int p, i, n, deck = 0, discard = 0, act0, act1;

	/* Open file for writing */
	fff = fopen(filename, "w");

	/* Check for failure */
	if (!fff) return -1;

	/* Score game to get end totals */
	score_game(g);

	/* Write header information */
	fputs("RFTG Export\n", fff);
	fputs(VERSION "\n", fff);

	/* Write game setup information */
	fprintf(fff, "%d %d\n", g->num_players, g->expanded);
	fprintf(fff, "%d %d %d\n", g->advanced, g->goal_disabled,
	                           g->takeover_disabled);

	/* Write current round and phase */
	fprintf(fff, "%d %s\n", g->round, g->cur_action == ACT_ROUND_START ?
	        "Start of round" : plain_actname[g->cur_action]);

	/* Loop over cards */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Check for card in draw pile */
		if (c_ptr->where == WHERE_DECK) deck++;

		/* Check for card in discard pile */
		if (c_ptr->where == WHERE_DISCARD) discard++;
	}

	/* Write game status information */
	fprintf(fff, "%d %d %d\n", deck, discard, g->vp_pool);

	/* Loop over phases */
	for (i = ACT_EXPLORE_5_0; i <= ACT_PRODUCE; ++i)
	{
		/* Print phase if selected */
		if (g->action_selected[i])
			fprintf(fff, "%s;", plain_actname[i]);
	}

	/* Finish line */
	fputs("\n", fff);

	/* Check for goals enabled */
	if (goals_enabled(g))
	{
		/* Loop over all goals */
		for (i = 0; i < MAX_GOAL; ++i)
		{
			/* Print goal if still available */
			if (g->goal_avail[i])
				fprintf(fff, "%s;", goal_name[i]);
		}

		/* Finish line */
		fputs("\n", fff);
	}
	else
	{
		/* Goals disabled */
		fputs("N/A\n", fff);
	}

	/* Loop over players */
	for (p = 0; p < g->num_players; p++)
	{
		/* Get player index to save next */
		n = (player_us + 1 + p) % g->num_players;

		/* Get player pointer */
		p_ptr = &g->p[n];

		/* Print player name */
		fprintf(fff, "%s\n", p_ptr->name);

		/* Assume actions aren't known */
		act0 = act1 = -1;

		/* Check for actions known */
		if (g->advanced && g->cur_action < ACT_SEARCH && n == player_us &&
			count_active_flags(g, player_us, FLAG_SELECT_LAST))
		{
			/* Copy first action only */
			act0 = p_ptr->action[0];
		}
		else if (g->cur_action >= ACT_SEARCH ||
				 count_active_flags(g, player_us, FLAG_SELECT_LAST))
		{
			/* Copy both actions */
			act0 = p_ptr->action[0];
			act1 = p_ptr->action[1];
		}

		/* Print actions if known */
		if (act0 != -1) fprintf(fff, "%s;", action_name(act0));
		if (act1 != -1) fprintf(fff, "%s;", action_name(act1));

		/* Finish line */
		fputs("\n", fff);

		/* Print player information */
		fprintf(fff, "%d %d %d\n", count_player_area(g, n, WHERE_HAND),
		        p_ptr->vp, p_ptr->end_vp);

		/* Check for last expansion */
		if (g->expanded == 3)
		{
			/* Print prestige and whether prestige action is used */
			fprintf(fff, "%d %d\n", p_ptr->prestige,
			        p_ptr->prestige_action_used);
		}
		else
		{
			/* No prestige */
			fputs("N/A\n", fff);
		}

		/* Check for goals enabled */
		if (goals_enabled(g))
		{
			/* Loop over goals */
			for (i = 0; i < MAX_GOAL; ++i)
			{
				/* Check if player has goal */
				if (p_ptr->goal_claimed[i])
				{
					/* Print goal name */
					fprintf(fff, "%s;", goal_name[i]);
					continue;
				}

				/* Check for insufficient progress */
				if (p_ptr->goal_progress[i] < goal_minimum(i))
					continue;

				/* Check for less progress than other players */
				if (p_ptr->goal_progress[i] < g->goal_most[i])
					continue;

				/* Print unclaimed goal */
				fprintf(fff, "%s*;", goal_name[i]);
			}

			/* Finish line */
			fputs("\n", fff);
		}
		else
		{
			/* Goals disabled */
			fputs("N/A\n", fff);
		}

		/* Print tableau */
		export_cards(fff, g, p_ptr->head[WHERE_ACTIVE], cmp_table);
	}

	/* Print human player's hand */
	export_cards(fff, g, g->p[player_us].head[WHERE_HAND], cmp_hand);

	/* Close file */
	fclose(fff);

	/* Success */
	return 0;
}
