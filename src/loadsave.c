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
 * Load a game from the given filename.
 */
int load_game(game *g, char *filename, int *player_us)
{
	FILE *fff;
	player *p_ptr;
	card *c_ptr;
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

	/* Read random seed information */
	fscanf(fff, "%u %u\n", &g->random_seed, &g->start_seed);

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

		/* Read name */
		fgets(buf, 1024, fff);

		/* Strip newline */
		buf[strlen(buf) - 1] = '\0';

		/* Copy name */
		p_ptr->name = strdup(buf);

		/* Read control information */
		fscanf(fff, "%d\n", &j);

		/* Check for human player */
		if (j == 1) *player_us = i;

		/* Loop over goals */
		for (j = 0; j < MAX_GOAL; j++)
		{
			/* Read goal information */
			fscanf(fff, "%d %d\n", &p_ptr->goal_claimed[j],
			                       &p_ptr->goal_progress[j]);
		}

		/* Read player's claimed VP chips */
		fscanf(fff, "%d\n", &p_ptr->vp);

		/* Read player's table order index */
		fscanf(fff, "%d\n", &p_ptr->table_order);

		/* Read player's previous turn actions */
		fscanf(fff, "%d %d\n", &p_ptr->prev_action[0],
		                       &p_ptr->prev_action[1]);
	}

	/* Read deck size */
	fscanf(fff, "%d\n", &g->deck_size);

	/* Loop over cards */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Read card's owner and location */
		fscanf(fff, "%d %d ", &c_ptr->owner, &c_ptr->where);

		/* Read card's list of players who know its location */
		fscanf(fff, "%x ", &c_ptr->known);

		/* Read card's design index */
		fscanf(fff, "%d ", &j);

		/* Set card's design pointer */
		c_ptr->d_ptr = &library[j];

		/* Read card's good information */
		fscanf(fff, "%*d %d ", &c_ptr->covered);

		/* Read card's order played information */
		fscanf(fff, "%d\n", &c_ptr->order);
	}

	/* Read size of VP pool */
	fscanf(fff, "%d\n", &g->vp_pool);

	/* Loop over goals */
	for (i = 0; i < MAX_GOAL; i++)
	{
		/* Read goal active/available */
		fscanf(fff, "%d %d\n", &g->goal_active[i], &g->goal_avail[i]);
	}

	/* Read round number */
	fscanf(fff, "%d\n", &g->round);

	/* Close file */
	fclose(fff);

	/* Success */
	return 0;
}

/*
 * Save a game to the given filename.
 *
 * We can only save a game that is in the "choose actions" phase of a turn.
 */
int save_game(game *g, char *filename, int player_us)
{
	FILE *fff;
	player *p_ptr;
	card *c_ptr;
	int i, j;

	/* Open file for writing */
	fff = fopen(filename, "w");

	/* Check for failure */
	if (!fff) return -1;

	/* Write header information */
	fputs("RFTG Save\n", fff);
	fprintf(fff, "%s\n", VERSION);

	/* Write random seeds */
	fprintf(fff, "%u %u\n", g->random_seed, g->start_seed);

	/* Write game setup information */
	fprintf(fff, "%d %d\n", g->num_players, g->expanded);
	fprintf(fff, "%d %d %d\n", g->advanced, g->goal_disabled,
	                           g->takeover_disabled);

	/* Loop over players */
	for (i = 0; i < g->num_players; i++)
	{
		/* Get player pointer */
		p_ptr = &g->p[i];

		/* Write player name */
		fprintf(fff, "%s\n", p_ptr->name);

		/* Write 0 for AI player, 1 for human */
		fprintf(fff, "%d\n", player_us == i);

		/* Loop over goals */
		for (j = 0; j < MAX_GOAL; j++)
		{
			/* Write goal information */
			fprintf(fff, "%d %d\n", p_ptr->goal_claimed[j],
			                        p_ptr->goal_progress[j]);
		}

		/* Write victory point chips earned */
		fprintf(fff, "%d\n", p_ptr->vp);

		/* Write counter for cards played */
		fprintf(fff, "%d\n", p_ptr->table_order);

		/* Write previous turn actions */
		fprintf(fff, "%d %d\n", p_ptr->prev_action[0],
		                        p_ptr->prev_action[1]);
	}

	/* Write size of deck */
	fprintf(fff, "%d\n", g->deck_size);

	/* Loop over cards */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Write card's owner and location */
		fprintf(fff, "%d %d ", c_ptr->owner, c_ptr->where);

		/* Write players who know this card's location */
		fprintf(fff, "%x ", c_ptr->where & ((1 << g->num_players) - 1));

		/* Write card's design index */
		fprintf(fff, "%d ", c_ptr->d_ptr->index);

		/* Write good information */
		fprintf(fff, "0 %d ", c_ptr->covered);

		/* Write order played */
		fprintf(fff, "%d\n", c_ptr->order);
	}

	/* Write size of VP pool */
	fprintf(fff, "%d\n", g->vp_pool);

	/* Loop over goals */
	for (i = 0; i < MAX_GOAL; i++)
	{
		/* Write goal active/available information */
		fprintf(fff, "%d %d\n", g->goal_active[i], g->goal_avail[i]);
	}

	/* Write round number */
	fprintf(fff, "%d\n", g->round);

	/* Close file */
	fclose(fff);

	/* Success */
	return 0;
}
