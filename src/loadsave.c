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
	{
		fprintf(fff, "%s\n", g->human_name);
	}

	/* Close file */
	fclose(fff);

	/* Success */
	return 0;
}
