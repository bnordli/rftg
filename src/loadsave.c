/*
 * Race for the Galaxy AI
 * 
 * Copyright (C) 2009-2011 Keldon Jones
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
	if (strcmp(version, "0.9.3") < 0) return -1;

	/* Read random seed information */
	fscanf(fff, "%u\n", &g->start_seed);

	/* Read game setup information */
	fscanf(fff, "%d %d\n", &g->num_players, &g->expanded);
	fscanf(fff, "%d %d %d\n", &g->advanced, &g->goal_disabled,
	                          &g->takeover_disabled);

	/* Read campaign name */
	fgets(buf, 1024, fff);

	/* Strip newline from campaign name */
	buf[strlen(buf) - 1] = '\0';

	/* Check for no campaign */
	if (!strcmp(buf, "none"))
	{
		/* Clear campaign */
		g->camp = NULL;
	}
	else
	{
		/* Loop over campaigns */
		for (i = 0; i < num_campaign; i++)
		{
			/* Check for match */
			if (!strcmp(camp_library[i].name, buf))
			{
				/* Set campaign */
				g->camp = &camp_library[i];
				break;
			}
		}

		/* Check for no match */
		if (i == num_campaign) return -1;
	}

	/* Clear other options */
	g->promo = 0;

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

	/* Write campaign information (if any) */
	fprintf(fff, "%s\n", g->camp ? g->camp->name : "none");

	/* Loop over players */
	for (i = 0; i < g->num_players; i++)
	{
		/* Get player index to save next */
		n = (player_us + i) % g->num_players;

		/* Get player pointer */
		p_ptr = &g->p[n];

		/* Write size of choice log */
		fprintf(fff, "%d ", p_ptr->choice_size);

		/* Loop over choice log entries */
		for (j = 0; j < p_ptr->choice_size; j++)
		{
			/* Write choice log entry */
			fprintf(fff, "%d ", p_ptr->choice_log[j]);
		}

		/* Finish line */
		fprintf(fff, "\n");
	}

	/* Close file */
	fclose(fff);

	/* Success */
	return 0;
}
