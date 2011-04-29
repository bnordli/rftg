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
 * BGG format tags in output?
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
static char card_subs[MAX_DESIGN][1024];

/*
 * Substitute for card with goods.
 */
static char good_subs[MAX_DESIGN][1024];

/*
 * Substitute for goals.
 */
static char goal_subs[MAX_GOAL][1024];

/*
 * Print messages to standard output.
 */
void message_add(game *g, char *msg)
{
	/* Discard message if not verbose */
	if (!verbose) return;

	/* Print message */
	printf("%s", msg);
}

/*
 * Print messages to standard output.
 */
void message_add_formatted(game *g, char *msg, char *tag)
{
	/* Discard message if not verbose */
	if (!verbose) return;

	/* Check for formatted */
	if (!formatted)
	{
		/* Print without formatting and exit */
		message_add(g, msg);
		return;
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
 * Prints the currently selected actions of a player.
 */
static void print_action(game *g, int who)
{
	int act0, act1;

	/* Check for actions known */
	if (g->advanced && g->cur_action < ACT_SEARCH && who == player_us &&
	    count_active_flags(g, player_us, FLAG_SELECT_LAST))
	{
		/* Copy first action only */
		act0 = g->p[who].action[0];
		act1 = -1;
	}
	else if (g->cur_action >= ACT_SEARCH ||
	         count_active_flags(g, player_us, FLAG_SELECT_LAST))
	{
		/* Copy actions */
		act0 = g->p[who].action[0];
		act1 = g->p[who].action[1];
	}
	else
	{
		/* Actions aren't known */
		act0 = act1 = -1;
	}

	/* Check for advanced game */
	if (g->advanced)
	{
		if (act0 == -1 && act1 == -1)
		{
			/* No actions known */
			printf("Actions: N/A");
		}
		else if (act1 == -1)
		{
			/* Only first action known */
			printf("Action: %s", action_name(act0));
		}
		else
		{
			/* Both actions known */
			printf("Actions: %s - %s", action_name(act0), action_name(act1));
		}
	}
	else
	{
		if (act0 == -1)
		{
			/* Action not known */
			printf("Action: N/A");
		}
		else
		{
			/* Actions known */
			printf("Action: %s", action_name(act0));
		}
	}

	/* Print newlines */
	printf("\n\n");
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
			/* Check for good on card */
			if (cards[p].covered != -1)
			{
				/* Print substitute with good */
				printf("%s", good_subs[cards[p].d_ptr->index]);
			}
			else
			{
				/* Print substitute */
				printf("%s", card_subs[cards[p].d_ptr->index]);
			}
		}
		else
		{
			/* Print card name (and possibly good) */
			printf("%s%s", cards[p].d_ptr->name, (cards[p].covered != -1 ? " (good)" : ""));
		}

		/* Print newline if needed */
		if ((p+1) % cards_per_line == 0) printf("\n");
	}
}

/*
 * Print a single goal to stdout.
 */
static void print_goal(int g)
{
	/* Check for substitutes */
	if (subs_file)
	{
		/* Print substitute */
		printf("%s", goal_subs[g]);
	}
	else
	{
		/* Print goal name */
		printf("%s\n", goal_name[g]);
	}
}

/*
 * Print goals claimed by one player to stdout.
 */
static void print_goals(player *p_ptr)
{
	int i, found = 0;

	/* Loop over goals */
	for (i = 0; i < MAX_GOAL; ++i)
	{
		/* Check if player has goal */
		if (p_ptr->goal_claimed[i])
		{
			/* Print goal */
			print_goal(i);

			/* Set the found flag */
			found = 1;
		}
	}

	/* Print newlines if any goal is claimed */
	if (found) printf("\n\n");
}

/*
 * Print the game state to stdout.
 */
static void print_game(game *g, int who)
{
	card *c_ptr;
	player *p_ptr;
	int i, hand, deck = 0, discard = 0, found;

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

	/* Print phase header */
	printf("Chosen phases: ");

	/* Clear found flag */
	found = 0;

	/* Loop over phases */
	for (i = ACT_EXPLORE_5_0; i <= ACT_PRODUCE; ++i)
	{
		/* Check for phases selected */
		if (g->action_selected[i])
		{
			/* Add separator if not first phase */
			if (found) printf(" - ");

			/* Add action name */
			printf("%s", plain_actname[i]);

			/* Set found flag */
			found = 1;
		}
	}

	/* Check for no phases found */
	if (!found) printf("N/A");

	/* Print newlines */
	printf("\n\n");

	/* Print deck and discard */
	printf("Draw deck size: %d card%s - Discard pile size: %d card%s - VP chips left: %d chip%s\n\n",
	    deck, PLURAL(deck), discard, PLURAL(discard),
	    g->vp_pool, PLURAL(g->vp_pool));

	/* Check for goals enabled */
	if (goals_enabled(g))
	{
		/* Clear found flag */
		found = 0;

		/* Print goal header */
		printf("Available goals\n");

		/* Loop over all goals */
		for (i = 0; i < MAX_GOAL; ++i)
		{
			/* Print goal if still available */
			if (g->goal_avail[i])
			{
				/* Print goal */
				print_goal(i);

				/* Set the found flag */
				found = 1;
			}
		}

		/* Check if none available */
		if (!found) printf("None\n");

		/* Print newlines */
		printf("\n\n");
	}

	/* Loop over all players */
	for (i = who + 1; ; ++i)
	{
		/* Check for wrap around */
		if (i == g->num_players) i = 0;

		/* Get player pointer */
		p_ptr = &g->p[i];

		/* Check for BGG formatting */
		if (formatted)
		{
			/* Print formatted player name */
			printf("[b][size=18]%s[/size][/b]\n", p_ptr->name);
		}
		else
		{
			/* Print player name */
			printf("%s\n", p_ptr->name);
		}

		/* Print currently selected action(s) */
		print_action(g, i);

		/* Count hand */
		hand = count_player_area(g, i, WHERE_HAND);

		/* Print hand */
		printf("Hand: %d card%s\n", hand, PLURAL(hand));

		/* Print VP chips */
		printf("VP chips: %d\n", p_ptr->vp);

		/* Check for third expansion */
		if (g->expanded == 3)
		{
			/* Print prestige */
			printf("Prestige: %d\n", p_ptr->prestige);
		}

		/* Print total score */
		printf("Points: %d VP%s\n\n", p_ptr->end_vp, PLURAL(p_ptr->end_vp));

		/* Print claimed goals */
		print_goals(p_ptr);

		/* Dump tableau */
		print_cards(g, p_ptr->head[WHERE_ACTIVE], cmp_table);

		/* Print newlines */
		printf("\n\n");

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
	char *p, *n;
	int found, i, good;

	/* Clear card and good substitute strings */
	for (i = 0; i < MAX_DESIGN; ++i)
	{
		card_subs[i][0] = '\0';
		good_subs[i][0] = '\0';
	}

	/* Clear goal substitutes */
	for (i = 0; i < MAX_GOAL; ++i)
	{
		goal_subs[i][0] = '\0';
	}

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

		/* Skip comments and blank lines */
		if (!buf[0] || buf[0] == '#') continue;

		/* Locate separator */
		p = strchr(buf, ';');

		/* Check for separator not found */
		if (!p)
		{
			/* Print error and exit */
			printf("Could not find separator (;) in line %s.\n", buf);
			exit(1);
		}

		/* End name */
		*p = '\0';

		/* Move to start of substitute string */
		++p;

		/* Clear found and good flag */
		found = good = 0;

		/* Look for name at start of buffer */
		n = buf;

		/* Look for good indicator */
		if (*n == '*')
		{
			/* Set good flag and increase name pointer */
			good = 1;
			++n;
		}

		/* Loop over all designs */
		for (i = 0; i < MAX_DESIGN; ++i)
		{
			/* Check for matching card name */
			if (!strcmp(n, library[i].name))
			{
				/* Check for good substitute */
				if (good)
				{
					/* Copy substitute string */
					strcpy(good_subs[i], p);
				}
				else
				{
					/* Copy substitute string */
					strcpy(card_subs[i], p);
				}

				/* Set found flag */
				found = 1;
			}
		}

		/* Loop over all goals */
		for (i = 0; i < MAX_GOAL; ++i)
		{
			/* Check for matching goal name */
			if (!strcmp(n, goal_name[i]))
			{
				/* Copy substitute string */
				strcpy(goal_subs[i], p);

				/* Set found flag */
				found = 1;
			}
		}

		/* Check whether string was recognized */
		if (!found)
		{
			/* Print error and exit */
			printf("Could not recognize card or goal name %s.\n", buf);
			exit(1);
		}
	}

	/* Loop over all designs */
	for (i = 0; i < MAX_DESIGN; ++i)
	{
		/* Check for empty substitute */
		if (strlen(card_subs[i]) == 0)
		{
			/* Print error and exit */
			printf("Did not find substitute string for card %s.\n",
			       library[i].name);
			exit(1);
		}

		/* Check for world with good */
		if (library[i].type == TYPE_WORLD && library[i].good_type)
		{
			/* Check for empty substitute */
			if (strlen(good_subs[i]) == 0)
			{
				/* Print error and exit */
				printf("Did not find substitute string for good on world %s.\n",
				       library[i].name);
				exit(1);
			}
		}
	}

	/* Loop over all goals */
	for (i = 0; i < MAX_GOAL; ++i)
	{
		/* Check for empty substitute */
		if (strlen(goal_subs[i]) == 0)
		{
			/* Print error and exit */
			printf("Did not find substitute string for goal %s.\n",
				goal_name[i]);
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

	/* Set name of human player */
	if (my_game.human_name && strlen(my_game.human_name))
		my_game.p[0].name = my_game.human_name;

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
