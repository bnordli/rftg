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
 * Replace all occurenses of a character with a string.
 */
static void replace_char(char *s, char c, char *replacement)
{
	char tmp[1024];
	char *p = s;

	/* Search for occurrence of character */
	while ((p = strchr(p, c)))
	{
		/* Copy first part of string */
		strncpy(tmp, s, p - s);
		tmp[p - s] = '\0';

		/* Write replacement entity */
		strcat(tmp, replacement);
		
		/* Increase pointer */
		++p;

		/* Write second part of string */
		strcat(tmp, p);

		/* Copy back to original buffer */
		strcpy(s, tmp);
	}
}

/*
 * XML escape a string.
 */
char *xml_escape(const char *s)
{
	static char escaped[1024];

	/* Copy string */
	strcpy(escaped, s);

	/* Replace special xml characters */
	replace_char(escaped, '&', "&amp;");
	replace_char(escaped, '<', "&lt;");
	replace_char(escaped, '>', "&gt;");

	/* Return the escaped string */
	return escaped;
}

/*
 * Write cards of the linked list starting with x, in a specified order.
 */
static void export_cards(FILE *fff, char *header, game *g, int x,
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

	/* Start tag */
	fprintf(fff, "    <%s count=\"%d\">\n", header, n);

	/* Loop over sorted cards */
	for (p = 0; p < n; ++p)
	{
		/* Write card name and good indicator */
		fprintf(fff, "      <Card id=\"%d\"%s>%s</Card>\n",
		        cards[p].d_ptr->index,
		        cards[p].covered != -1 ? " good=\"yes\"" : "",
		        xml_escape(cards[p].d_ptr->name));
	}

	/* End tag */
	fprintf(fff, "    </%s>\n", header);
}

/*
 * Export the game state to the given filename.
 */
int export_game(game *g, char *filename, int player_us,
                const char *message, void (*export_log)(FILE *fff))
{
	FILE *fff;
	player *p_ptr;
	card *c_ptr;
	int p, i, n, count, deck = 0, discard = 0, act[2];

	/* Open file for writing */
	fff = fopen(filename, "w");

	/* Check for failure */
	if (!fff) return -1;

	/* Score game to get end totals */
	score_game(g);

	/* Write header and top level tag */
	fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", fff);
	fputs("<RftgExport>\n", fff);

	/* Write version */
	fprintf(fff, "  <Version>%s</Version>\n", RELEASE);

	/* Write setup start tag */
	fputs("  <Setup>\n", fff);

	/* Write number of players (and advanced game) */
	fprintf(fff, "    <Players%s>%d</Players>\n",
		g->num_players == 2 && g->advanced ? " advanced=\"yes\"" : "",
		g->num_players);

	/* Write expansion */
	fprintf(fff, "    <Expansion id=\"%d\">%s</Expansion>\n",
	        g->expanded, xml_escape(exp_names[g->expanded]));

	/* Check for expansion with goals */
	if (g->expanded)
		fprintf(fff, "    <Goals>%s</Goals>\n",
		        g->goal_disabled ? "off" : "on");

	/* Check for expansion with takeovers */
	if (g->expanded > 1)
		fprintf(fff, "    <Takeovers>%s</Takeovers>\n",
		        g->takeover_disabled ? "off" : "on");

	/* Write end tag */
	fputs("  </Setup>\n", fff);

	/* Write status start tag */
	fputs("  <Status>\n", fff);

	/* Check for messsage */
	if (message) fprintf(fff, "    <Message>%s</Message>\n",
	                     xml_escape(message));

	/* Check for game over */
	if (g->game_over) fputs("    <GameOver />\n", fff);

	/* Write current round phase */
	fprintf(fff, "    <Round>%d</Round>\n", g->round);

	/* Write phases start tag */
	fputs("    <Phases>\n", fff);

	/* Loop over phases */
	for (i = ACT_EXPLORE_5_0; i <= ACT_PRODUCE; ++i)
	{
		/* Write phase if selected */
		if (g->action_selected[i])
			fprintf(fff, "      <Phase id=\"%d\"%s>%s</Phase>\n",
			        i, i == g->cur_action ? " current=\"yes\"" : "",
			        xml_escape(plain_actname[i]));
	}

	/* Write end tag */
	fputs("    </Phases>\n", fff);

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
	fprintf(fff, "    <Deck>%d</Deck>\n", deck);
	fprintf(fff, "    <Discard>%d</Discard>\n", discard);
	fprintf(fff, "    <Pool>%d</Pool>\n", g->vp_pool);

	/* Check for goals enabled */
	if (goals_enabled(g))
	{
		/* Write goals start tag */
		fputs("    <Goals>\n", fff);

		/* Loop over all goals */
		for (i = 0; i < MAX_GOAL; ++i)
		{
			/* Skip unused goals */
			if (!g->goal_active[i]) continue;

			/* Write goal and availability */
			fprintf(fff, "      <Goal id=\"%d\" claimed=\"%s\">%s</Goal>\n", i,
			        g->goal_avail[i] ? "no" : "yes", xml_escape(goal_name[i]));
		}

		/* Write end tag */
		fputs("    </Goals>\n", fff);
	}

	/* Write end tag */
	fputs("  </Status>\n", fff);

	/* Loop over players */
	for (p = 0; p < g->num_players; p++)
	{
		/* Get player index to save next */
		n = (player_us + 1 + p) % g->num_players;

		/* Get player pointer */
		p_ptr = &g->p[n];

		/* Write player start tag */
		fprintf(fff, "  <Player id=\"%d\"%s>\n", n,
		        p_ptr->winner ? " winner=\"yes\"" : "");

		/* Write player name */
		fprintf(fff, "    <Name>%s</Name>\n", xml_escape(p_ptr->name));

		/* Assume actions aren't known */
		act[0] = act[1] = -1;

		/* Check for actions known */
		if (g->advanced && g->cur_action < ACT_SEARCH && n == player_us &&
			count_active_flags(g, player_us, FLAG_SELECT_LAST))
		{
			/* Copy first action only */
			act[0] = p_ptr->action[0];
		}
		else if (g->cur_action >= ACT_SEARCH ||
				 count_active_flags(g, player_us, FLAG_SELECT_LAST))
		{
			/* Copy both actions */
			act[0] = p_ptr->action[0];
			act[1] = p_ptr->action[1];
		}

		/* Write action start tag */
		fputs("    <Actions>\n", fff);

		/* Loop over actions */
		for (i = 0; i < 2; ++i)
		{
			/* Write action if known */
			if (act[i] != -1)
				fprintf(fff, "      <Action id=\"%d\">%s</Action>\n",
				        act[i], xml_escape(action_name(act[i])));
		}

		/* Write end tag */
		fputs("    </Actions>\n", fff);

		/* Check for last expansion */
		if (g->expanded == 3)
		{
			/* Write prestige and whether prestige action is used */
			fprintf(fff, "    <Prestige used=\"%s\">%d</Prestige>\n",
			        p_ptr->prestige_action_used ? "yes" : "no",
			        p_ptr->prestige);
		}

		/* Write acquired chips */
		fprintf(fff, "    <Chips>%d</Chips>\n", p_ptr->vp);

		/* Write current score */
		fprintf(fff, "    <Score>%d</Score>\n", p_ptr->end_vp);

		/* Check for goals enabled */
		if (goals_enabled(g))
		{
			/* Write goals start tag*/
			fputs("    <Goals>\n", fff);

			/* Loop over goals */
			for (i = 0; i < MAX_GOAL; ++i)
			{
				/* Check if player has goal */
				if (p_ptr->goal_claimed[i])
				{
					/* Write goal */
					fprintf(fff, "      <Goal id=\"%d\">%s</Goal>\n",
					        i, xml_escape(goal_name[i]));
					continue;
				}

				/* Check for insufficient progress */
				if (p_ptr->goal_progress[i] < goal_minimum(i))
					continue;

				/* Check for less progress than other players */
				if (p_ptr->goal_progress[i] < g->goal_most[i])
					continue;

				/* Write unclaimed goal */
				fprintf(fff, "      <Goal id=\"%d\" unclaimed=\"yes\">%s"
				        "</Goal>\n", i, xml_escape(goal_name[i]));
			}

			/* Write end tag */
			fputs("    </Goals>\n", fff);
		}

		/* Write tableau */
		export_cards(fff, "Tableau", g, p_ptr->head[WHERE_ACTIVE], cmp_table);

		/* Check for saved cards */
		if (count_active_flags(g, n, FLAG_START_SAVE))
		{
			/* Reset count */
			count = 0;

			/* Loop over cards in deck */
			for (i = 0; i < g->deck_size; i++)
			{
				/* Count cards saved */
				if (g->deck[i].where == WHERE_SAVED) count++;
			}

			/* Write saved start tag */
			fprintf(fff, "    <Saved count=\"%d\">\n", count);

			/* Check for saved by human player */
			if (n == player_us)
			{
				/* Loop over cards in deck */
				for (i = 0; i < g->deck_size; i++)
				{
					/* Write saved card */
					if (g->deck[i].where == WHERE_SAVED)
					{
						/* Write card name */
						fprintf(fff, "      <Card id=\"%d\">%s</Card>\n",
						        g->deck[i].d_ptr->index,
						        xml_escape(g->deck[i].d_ptr->name));
					}
				}
			}

			/* Write end tag */
			fprintf(fff, "    </Saved>\n");
		}


		if (n == player_us)
		{
			/* Write human player's hand */
			export_cards(fff, "Hand", g, p_ptr->head[WHERE_HAND], cmp_hand);
		}
		else
		{
			/* Write hand start tag */
			fprintf(fff, "    <Hand count=\"%d\">\n",
			        count_player_area(g, n, WHERE_HAND));

			/* Write end tag */
			fputs("    </Hand>\n", fff);
		}

		/* Write end tag */
		fputs("  </Player>\n", fff);
	}

	if (export_log)
	{
		/* Write log start tag */
		fputs("  <Log>\n", fff);

		/* Write log */
		export_log(fff);

		/* Write log end tag */
		fputs("  </Log>\n", fff);
	}

	/* End top level tag */
	fputs("</RftgExport>\n", fff);

	/* Close file */
	fclose(fff);

	/* Success */
	return 0;
}
