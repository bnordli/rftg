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
 * Number of loaded designs.
 */
int num_design;

/*
 * Card designs.
 */
design library[AVAILABLE_DESIGN];

/*
 * Campaign library.
 */
campaign *camp_library;
int num_campaign;

/*
 * Names of campaign flags.
 */
static char *camp_flags[] =
{
	"DRAW_EXTRA",
	NULL
};

/*
 * Names of card flags.
 */
static char *flag_name[] =
{
	"MILITARY",
	"WINDFALL",
	"START",
	"START_RED",
	"START_BLUE",
	"PROMO",
	"REBEL",
	"UPLIFT",
	"ALIEN",
	"TERRAFORMING",
	"IMPERIUM",
	"CHROMO",
	"PRESTIGE",
	"STARTHAND_3",
	"START_SAVE",
	"DISCARD_TO_12",
	"GAME_END_14",
	"TAKE_DISCARDS",
	"SELECT_LAST",
	"EXTRA_SURVEY",
	"NO_PRODUCE",
	"DISCARD_PRODUCE",
	NULL
};

/*
 * Good names (which start at cost/value 2).
 */
static char *good_name[] =
{
	"",
	"ANY",
	"NOVELTY",
	"RARE",
	"GENE",
	"ALIEN",
	NULL
};

/*
 * Special power flag names (by phase).
 */
static char *power_name[6][64] =
{
	/* No phase zero */
	{
		NULL,
	},

	/* Phase one */
	{
		"DRAW",
		"KEEP",
		"DISCARD_ANY",
		"DISCARD_PRESTIGE",
		"ORB_MOVEMENT",
		NULL,
	},

	/* Phase two */
	{
		"DRAW",
		"REDUCE",
		"DRAW_AFTER",
		"EXPLORE",
		"DISCARD_REDUCE",
		"SAVE_COST",
		"PRESTIGE",
		"PRESTIGE_REBEL",
		"PRESTIGE_SIX",
		"CONSUME_RARE",
		NULL,
	},

	/* Phase three */
	{
		"REDUCE",
		"NOVELTY",
		"RARE",
		"GENE",
		"ALIEN",
		"DISCARD",
		"REDUCE_ZERO",
		"MILITARY_HAND",
		"EXTRA_MILITARY",
		"AGAINST_REBEL",
		"AGAINST_CHROMO",
		"PER_MILITARY",
		"PER_CHROMO",
		"IF_IMPERIUM",
		"PAY_MILITARY",
		"PAY_DISCOUNT",
		"PAY_PRESTIGE",
		"CONQUER_SETTLE",
		"NO_TAKEOVER",
		"DRAW_AFTER",
		"EXPLORE_AFTER",
		"PRESTIGE",
		"PRESTIGE_REBEL",
		"SAVE_COST",
		"PLACE_TWO",
		"PLACE_MILITARY",
		"PLACE_LEFTOVER",
		"PLACE_ZERO",
		"CONSUME_RARE",
		"CONSUME_GENE",
		"CONSUME_ALIEN",
		"CONSUME_PRESTIGE",
		"AUTO_PRODUCE",
		"PRODUCE_PRESTIGE",
		"TAKEOVER_REBEL",
		"TAKEOVER_IMPERIUM",
		"TAKEOVER_MILITARY",
		"TAKEOVER_PRESTIGE",
		"DESTROY",
		"TAKEOVER_DEFENSE",
		"PREVENT_TAKEOVER",
		"UPGRADE_WORLD",
		"FLIP_ZERO",
		NULL,
	},

	/* Phase four */
	{
		"TRADE_ANY",
		"TRADE_NOVELTY",
		"TRADE_RARE",
		"TRADE_GENE",
		"TRADE_ALIEN",
		"TRADE_THIS",
		"TRADE_BONUS_CHROMO",
		"NO_TRADE",
		"TRADE_ACTION",
		"TRADE_NO_BONUS",
		"CONSUME_ANY",
		"CONSUME_NOVELTY",
		"CONSUME_RARE",
		"CONSUME_GENE",
		"CONSUME_ALIEN",
		"CONSUME_THIS",
		"CONSUME_TWO",
		"CONSUME_3_DIFF",
		"CONSUME_N_DIFF",
		"CONSUME_ALL",
		"CONSUME_PRESTIGE",
		"GET_CARD",
		"GET_2_CARD",
		"GET_3_CARD",
		"GET_VP",
		"GET_PRESTIGE",
		"DRAW",
		"DRAW_LUCKY",
		"DISCARD_HAND",
		"ANTE_CARD",
		"VP",
		NULL,
	},

	/* Phase five */
	{
		"PRODUCE",
		"WINDFALL_ANY",
		"WINDFALL_NOVELTY",
		"WINDFALL_RARE",
		"WINDFALL_GENE",
		"WINDFALL_ALIEN",
		"NOT_THIS",
		"DISCARD",
		"DRAW",
		"DRAW_IF",
		"PRESTIGE_IF",
		"DRAW_EACH_NOVELTY",
		"DRAW_EACH_RARE",
		"DRAW_EACH_GENE",
		"DRAW_EACH_ALIEN",
		"DRAW_WORLD_GENE",
		"DRAW_MOST_PRODUCED",
		"DRAW_DIFFERENT",
		"DRAW_MOST_NOVELTY",
		"DRAW_MOST_RARE",
		"DRAW_MOST_GENE",
		"PRESTIGE_MOST_CHROMO",
		"DRAW_MILITARY",
		"DRAW_REBEL",
		"DRAW_REBEL_MILITARY",
		"DRAW_IMPERIUM",
		"DRAW_CHROMO",
		"DRAW_5_DEV",
		"TAKE_SAVED",
		"SHIFT_RARE",
		NULL,
	}
};

/*
 * Special victory point flags.
 */
static char *vp_name[] =
{
	"NOVELTY_PRODUCTION",
	"RARE_PRODUCTION",
	"GENE_PRODUCTION",
	"ALIEN_PRODUCTION",
	"NOVELTY_WINDFALL",
	"RARE_WINDFALL",
	"GENE_WINDFALL",
	"ALIEN_WINDFALL",
	"DEVEL_EXPLORE",
	"WORLD_EXPLORE",
	"DEVEL_TRADE",
	"WORLD_TRADE",
	"DEVEL_CONSUME",
	"WORLD_CONSUME",
	"SIX_DEVEL",
	"DEVEL",
	"WORLD",
	"NONMILITARY_WORLD",
	"NONMILITARY_TRADE",
	"REBEL_FLAG",
	"ALIEN_FLAG",
	"TERRAFORMING_FLAG",
	"UPLIFT_FLAG",
	"IMPERIUM_FLAG",
	"CHROMO_FLAG",
	"MILITARY",
	"TOTAL_MILITARY",
	"NEGATIVE_MILITARY",
	"REBEL_MILITARY",
	"THREE_VP",
	"KIND_GOOD",
	"PRESTIGE",
	"ALIEN_TECHNOLOGY",
	"ALIEN_SCIENCE",
	"ALIEN_UPLIFT",
	"NAME",
	NULL
};

/*
 * Lookup a power code.
 */
static uint64_t lookup_power(char *ptr, int phase)
{
	int i = 0;
	char message[1024];

	/* Loop over power names */
	while (power_name[phase][i])
	{
		/* Check this power */
		if (!strcmp(power_name[phase][i], ptr)) return 1ULL << i;

		/* Next effect */
		i++;
	}

	/* No match */
	sprintf(message, "No power named '%s'\n", ptr);
	display_error(message);
	exit(1);
}

/*
 * Read card designs from 'cards.txt' file.
 */
int read_cards(char *suggestion)
{
	FILE *fff;
	char buf[1024], *ptr;
	design *d_ptr = NULL;
	power *o_ptr;
	vp_bonus *v_ptr;
	int i, phase;
	uint64_t code;

	/* Open card database */
	fff = fopen(RFTGDIR "/cards.txt", "r");

	/* Check for error */
	if (!fff)
	{
		/* Try reading from current directory instead */
		fff = fopen("cards.txt", "r");
	}

	/* Check for error and alternative suggestion */
	if (!fff && suggestion)
	{
		/* Combine the paths */
		sprintf(buf, "%s/cards.txt", suggestion);

		/* Try reading from suggested directory instead */
		fff = fopen(buf, "r");
	}

	/* Check for failure */
	if (!fff)
	{
		/* Error */
		perror("cards.txt");
		return -1;
	}

	/* Loop over file */
	while (num_design < AVAILABLE_DESIGN)
	{
		/* Read a line */
		fgets(buf, 1024, fff);

		/* Check for end of file */
		if (feof(fff)) break;

		/* Strip newline */
		buf[strlen(buf) - 1] = '\0';

		/* Skip comments and blank lines */
		if (!buf[0] || buf[0] == '#') continue;

		/* Switch on type of line */
		switch (buf[0])
		{
			/* New card */
			case 'N':

				/* Current design pointer */
				d_ptr = &library[num_design];

				/* Set index */
				d_ptr->index = num_design++;

				/* Read name */
				d_ptr->name = strdup(buf + 2);
				break;

			/* Type, cost, and value */
			case 'T':

				/* Get type string */
				ptr = strtok(buf + 2, ":");

				/* Read type */
				d_ptr->type = (int8_t) strtol(ptr, NULL, 0);

				/* Get cost string */
				ptr = strtok(NULL, ":");

				/* Read cost */
				d_ptr->cost = (int8_t) strtol(ptr, NULL, 0);

				/* Get VP string */
				ptr = strtok(NULL, ":");

				/* Read VP */
				d_ptr->vp = (int8_t) strtol(ptr, NULL, 0);
				break;

			/* Expansion counts */
			case 'E':

				/* Get first count string */
				ptr = strtok(buf + 2, ":");

				/* Loop over number of expansions */
				for (i = 0; i < MAX_EXPANSION; i++)
				{
					/* Set count */
					d_ptr->expand[i] = (int8_t) strtol(ptr, NULL, 0);

					/* Read next count */
					ptr = strtok(NULL, ":");
				}

				/* Done */
				break;

			/* Flags */
			case 'F':

				/* Get first flag */
				ptr = strtok(buf + 2, " |");

				/* Loop over flags */
				while (ptr)
				{
					/* Check each flag */
					for (i = 0; flag_name[i]; i++)
					{
						/* Check this flag */
						if (!strcmp(ptr, flag_name[i]))
						{
							/* Set flag */
							d_ptr->flags |= 1 << i;
							break;
						}
					}

					/* Check for no match */
					if (!flag_name[i])
					{
						/* Error */
						printf("Unknown flag '%s'!\n",
						       ptr);
						return -2;
					}

					/* Get next flag */
					ptr = strtok(NULL, " |");
				}

				/* Done with flag line */
				break;

			/* Good */
			case 'G':

				/* Get good string */
				ptr = buf + 2;

				/* Loop over goods */
				for (i = 0; good_name[i]; i++)
				{
					/* Check this good */
					if (!strcmp(ptr, good_name[i]))
					{
						/* Set good */
						d_ptr->good_type = i;
						break;
					}
				}

				/* Check for no match */
				if (!good_name[i])
				{
					/* Error */
					printf("No good name '%s'!\n", ptr);
					return -2;
				}

				/* Done with good line */
				break;

			/* Power */
			case 'P':

				/* Get power pointer */
				o_ptr = &d_ptr->powers[d_ptr->num_power++];

				/* Get phase string */
				ptr = strtok(buf + 2, ":");

				/* Read power phase */
				phase = strtol(ptr, NULL, 0);

				/* Save phase */
				o_ptr->phase = phase;

				/* Clear power code */
				code = 0;

				/* Read power flags */
				while ((ptr = strtok(NULL, "|: ")))
				{
					/* Check for end of flags */
					if (isdigit(*ptr) ||
					    *ptr == '-') break;

					/* Lookup effect code */
					code |= lookup_power(ptr, phase);
				}

				/* Store power code */
				o_ptr->code = code;

				/* Read power's value */
				o_ptr->value = (int8_t) strtol(ptr, NULL, 0);

				/* Get times string */
				ptr = strtok(NULL, ":");

				/* Read power's number of times */
				o_ptr->times = (int8_t) strtol(ptr, NULL, 0);
				break;

			/* VP flags */
			case 'V':

				/* Get VP bonus */
				v_ptr = &d_ptr->bonuses[d_ptr->num_vp_bonus++];

				/* Get point string */
				ptr = strtok(buf + 2, ":");

				/* Read point value */
				v_ptr->point = (int8_t) strtol(ptr, NULL, 0);

				/* Get bonus type string */
				ptr = strtok(NULL, ":");

				/* Loop over VP bonus types */
				for (i = 0; vp_name[i]; i++)
				{
					/* Check this type */
					if (!strcmp(ptr, vp_name[i]))
					{
						/* Set type */
						v_ptr->type = i;
						break;
					}
				}

				/* Check for no match */
				if (!vp_name[i])
				{
					/* Error */
					printf("No VP type '%s'!\n", ptr);
					return -2;
				}

				/* Get name string */
				ptr = strtok(NULL, ":");

				/* Store VP name string */
				v_ptr->name = strdup(ptr);
				break;
		}
	}

	/* Close card design file */
	fclose(fff);

	/* Success */
	return 0;
}

/*
 * Read the campaign descriptions from the 'campaign.txt' file.
 */
void read_campaign(void)
{
	FILE *fff;
	campaign *a_ptr = NULL;
	char buf[1024], *ptr;
	int who = 0, n = 0, len;
	int i;

	/* Open campaign description file */
	fff = fopen(RFTGDIR "/campaign.txt", "r");

	/* Check for error */
	if (!fff)
	{
		/* Try reading from current directory instead */
		fff = fopen("campaign.txt", "r");
	}

	/* Check for failure */
	if (!fff)
	{
		/* Print error */
		perror("campaign.txt");
		return;
	}

	/* Loop over file */
	while (1)
	{
		/* Read a line */
		fgets(buf, 1024, fff);

		/* Check for end of file */
		if (feof(fff)) break;

		/* Strip newline */
		buf[strlen(buf) - 1] = '\0';

		/* Skip comments and blank lines */
		if (!buf[0] || buf[0] == '#') continue;

		/* Switch on type of line */
		switch (buf[0])
		{
			/* New campaign */
			case 'N':

				/* One more campaign */
				num_campaign++;

				/* Resize library array */
				camp_library = (campaign *)realloc(camp_library,
				               sizeof(campaign) * num_campaign);

				/* Get campaign pointer */
				a_ptr = &camp_library[num_campaign - 1];

				/* Loop over players */
				for (i = 0; i < MAX_PLAYER; i++)
				{
					/* Reset campaign size data */
					a_ptr->size[i] = 0;
				}

				/* Clear flags */
				a_ptr->flags = 0;

				/* Reset campaign goal data */
				a_ptr->num_goal = 0;

				/* Start reading cards with first player */
				who = n = 0;

				/* Get campaign name */
				ptr = buf + 2;

				/* Copy name */
				a_ptr->name = strdup(ptr);

				/* Clear description */
				a_ptr->desc = strdup("");
				break;

			/* Campaign options */
			case 'O':

				/* Get expansion string */
				ptr = strtok(buf + 2, ":");

				/* Read type */
				a_ptr->expanded = strtol(ptr, NULL, 0);

				/* Get number of players string */
				ptr = strtok(NULL, ":");

				/* Read number of players */
				a_ptr->num_players = strtol(ptr, NULL, 0);

				/* Get advanced string */
				ptr = strtok(NULL, ":");

				/* Read advanced option */
				a_ptr->advanced = strtol(ptr, NULL, 0);

				/* Get goals disabled string */
				ptr = strtok(NULL, ":");

				/* Read goal disabled option */
				a_ptr->goal_disabled = strtol(ptr, NULL, 0);

				/* Get takeovers disabled string */
				ptr = strtok(NULL, ":");

				/* Read takeover disabled option */
				a_ptr->takeover_disabled = strtol(ptr, NULL, 0);
				break;

			/* Campaign description */
			case 'D':

				/* Get current length */
				len = strlen(a_ptr->desc);

				/* Add space if necessary */
				if (len) len++;

				/* Add space for terminator */
				len++;

				/* Get next piece of description */
				ptr = buf + 2;

				/* Increase allocation */
				a_ptr->desc = (char *)realloc(a_ptr->desc,
				                             len + strlen(ptr));

				/* Add newline if necessary */
				if (len > 1) strcat(a_ptr->desc, "\n");

				/* Add to description */
				strcat(a_ptr->desc, ptr);
				break;

			/* Flags */
			case 'F':

				/* Get first flag */
				ptr = strtok(buf + 2, " |");

				/* Loop over flags */
				while (ptr)
				{
					/* Check each flag */
					for (i = 0; camp_flags[i]; i++)
					{
						/* Check this flag */
						if (!strcmp(ptr, camp_flags[i]))
						{
							/* Set flag */
							a_ptr->flags |= 1 << i;
							break;
						}
					}

					/* Check for no match */
					if (!camp_flags[i])
					{
						/* Error */
						printf("Unknown flag '%s'!\n",
						       ptr);

						/* Exit */
						exit(1);
					}

					/* Get next flag */
					ptr = strtok(NULL, " |");
				}

				/* Done with flag line */
				break;

			/* Card */
			case 'C':

				/* Get card name */
				ptr = buf + 2;

				/* Check for next player */
				if (!strcmp(ptr, "---"))
				{
					/* Advance to next player */
					who++;

					/* Start at beginning of next player */
					n = 0;
					break;
				}

				/* Check for random card */
				if (!strcmp(ptr, "RANDOM"))
				{
					/* Add random card to order */
					a_ptr->order[who][n++] = NULL;

					/* Save size */
					a_ptr->size[who] = n;
					break;
				}

				/* Loop over designs */
				for (i = 0; i < MAX_DESIGN; i++)
				{
					/* Check for name match */
					if (!strcmp(ptr, library[i].name))
					{
						/* Add design to campaign */
						a_ptr->order[who][n++] =
						              &library[i];

						/* Save size */
						a_ptr->size[who] = n;

						/* Done looking */
						break;
					}
				}

				/* Check for no match */
				if (i == MAX_DESIGN)
				{
					/* Error */
					fprintf(stderr,
						"Could not find card %s!\n",
					        ptr);
					exit(1);
				}

				/* Done with line */
				break;

			/* Goal name */
			case 'G':

				/* Advance to goal name */
				ptr = buf + 2;

				/* Get current number of set goals */
				len = a_ptr->num_goal;

				/* Loop over goal names */
				for (i = 0; i < MAX_GOAL; i++)
				{
					/* Check for match */
					if (!strcmp(ptr, goal_name[i]))
					{
						/* Add to first goals list */
						a_ptr->goal[len] = i;

						/* One more goal */
						a_ptr->num_goal++;
						break;
					}
				}

				/* Check for no matched goal */
				if (i == MAX_GOAL)
				{
					/* Error */
					fprintf(stderr,
						"Could not find goal %s!\n",
					        ptr);
					exit(1);
				}
		}
	}

	/* Close campaign file */
	fclose(fff);
}

/*
 * Initialize a campaign status.
 */
static void init_campaign(game *g)
{
	int i, j, k;

	/* Check for pre-existing campaign status */
	if (!g->camp_status)
	{
		/* Make a status structure */
		g->camp_status = (campaign_status *)
		                               malloc(sizeof(campaign_status));
	}

	/* Clear campaign status */
	memset(g->camp_status, 0, sizeof(campaign_status));

	/* Loop over players */
	for (i = 0; i < MAX_PLAYER; i++)
	{
		/* Loop over set aside cards for this player */
		for (j = 0; j < g->camp->size[i]; j++)
		{
			/* Loop over cards in deck */
			for (k = 0; k < g->deck_size; k++)
			{
				/* Skip cards not in deck */
				if (g->deck[k].where != WHERE_DECK) continue;

				/* Skip cards that do not match */
				if (g->deck[k].d_ptr != g->camp->order[i][j])
					continue;

				/* Move card to campaign location */
				move_card(g, k, i, WHERE_CAMPAIGN);

				/* Save index */
				g->camp_status->index[i][j] = k;

				/* Done looking */
				break;
			}

			/* Check for random card */
			if (!g->camp->order[i][j])
			{
				/* Add random card */
				g->camp_status->index[i][j] = -1;
				continue;
			}

			/* Check for failure to find card */
			if (k == g->deck_size)
			{
				/* Error */
				fprintf(stderr, "Could not find enough %s.\n",
				        g->camp->order[i][j]->name);
				exit(1);
			}
		}

		/* Set size */
		g->camp_status->size[i] = g->camp->size[i];
	}
}

/*
 * Find a campaign, or return NULL if campaign is unknown.
 */
campaign *find_campaign(char *campaign_name)
{
	int i;

	/* Check for no campaign set */
	if (!campaign_name)
	{
		return NULL;
	}

	/* Loop over available campaigns */
	for (i = 0; i < num_campaign; i++)
	{
		/* Check for match */
		if (!strcmp(campaign_name, camp_library[i].name))
		{
			/* Return campaign */
			return &camp_library[i];
		}
	}

	/* Return no match. */
	return NULL;
}

/*
 * Apply campaign options to game.
 */
void apply_campaign(game *g)
{
	/* Check for campaign */
	if (g->camp)
	{
		/* Override game options with campaign versions */
		g->expanded = g->camp->expanded;
		g->num_players = g->camp->num_players;
		g->advanced = g->camp->advanced;
		g->goal_disabled = g->camp->goal_disabled;
		g->takeover_disabled = g->camp->takeover_disabled;
	}
}

/*
 * Initialize a game.
 */
void init_game(game *g)
{
	player *p_ptr;
	design *d_ptr;
	card *c_ptr;
	int goal[MAX_GOAL];
	int i, j, k, n;
	int num_goal = 0;

	/* Save current random seed */
	g->start_seed = g->random_seed;

#if 0
	sprintf(msg, "start seed: %u\n", g->start_seed);
	message_add(msg);
#endif

	/* Apply campaign options */
	apply_campaign(g);

	/* Game is not simulated */
	g->simulation = 0;

	/* Game is not a debug game */
	g->debug_game = 0;

	/* No rotation */
	g->debug_rotate = 0;

	/* Set size of VP pool */
	g->vp_pool = g->num_players * 12;

	/* Increase size of pool in third expansion */
	if (g->expanded == 3) g->vp_pool += 5;

	/* No game round yet */
	g->round = 0;

	/* Start of game */
	g->cur_action = ACT_GAME_START;
	g->turn = 0;

	/* Clear selected actions */
	for (i = 0; i < MAX_ACTION; i++) g->action_selected[i] = 0;

	/* Game is not over */
	g->game_over = 0;

	/* No cards in deck yet */
	g->deck_size = 0;

	/* Clear goals */
	for (i = 0; i < MAX_GOAL; i++)
	{
		/* Goal is not active */
		g->goal_active[i] = 0;

		/* Goal is not available */
		g->goal_avail[i] = 0;
	}

	/* Clear number of pending takeovers */
	g->num_takeover = 0;

	/* Set Oort Cloud kind to "any" */
	g->oort_kind = GOOD_ANY;

	/* Loop over card designs */
	for (i = 0; i < num_design; i++)
	{
		/* Get design pointer */
		d_ptr = &library[i];

		/* Get number of cards in use */
		n = d_ptr->expand[g->expanded];

		/* Skip promo cards if not included */
		if (!g->promo && (d_ptr->flags & FLAG_PROMO)) n = 0;

		/* Add cards */
		for (j = 0; j < n; j++)
		{
			/* Check for too large deck */
			if (g->deck_size >= MAX_DECK)
			{
				/* Error */
				display_error("Deck is too large!");
				exit(1);
			}

			/* Get card pointer */
			c_ptr = &g->deck[g->deck_size++];

			/* No owner */
			c_ptr->start_owner = c_ptr->owner = -1;

			/* Put location in draw deck */
			c_ptr->start_where = c_ptr->where = WHERE_DECK;

			/* Clear card misc flags */
			c_ptr->misc = 0;

			/* Set card's design */
			c_ptr->d_ptr = d_ptr;

			/* Card is not covering another */
			c_ptr->covering = -1;

			/* No goods on card */
			c_ptr->num_goods = 0;

			/* Card is not followed by any other */
			c_ptr->next = c_ptr->start_next = -1;
		}
	}

	/* Loop over players */
	for (i = 0; i < g->num_players; i++)
	{
		/* Get player pointer */
		p_ptr = &g->p[i];

		/* Clear all claimed goals */
		for (j = 0; j < MAX_GOAL; j++)
		{
			/* Goal is unclaimed */
			p_ptr->goal_claimed[j] = 0;

			/* No progress toward goal */
			p_ptr->goal_progress[j] = 0;
		}

		/* Player has no actions chosen */
		p_ptr->action[0] = p_ptr->prev_action[0] = -1;
		p_ptr->action[1] = p_ptr->prev_action[1] = -1;

		/* Player has not used prestige/search action */
		p_ptr->prestige_action_used = 0;

		/* Player has not used phase bonus */
		p_ptr->phase_bonus_used = 0;

		/* Player has no card to be placed */
		p_ptr->placing = -1;

		/* Player has no cards in any area */
		for (j = 0; j < MAX_WHERE; j++)
		{
			/* Clear list head */
			p_ptr->head[j] = -1;
			p_ptr->start_head[j] = -1;
		}

		/* Player has no bonus military accrued */
		p_ptr->bonus_military = 0;

		/* Player has no bonus settle discount */
		p_ptr->bonus_reduce = 0;

		/* Player has not used any partial hand military powers */
		p_ptr->hand_military_spent = 0;

		/* Player has not spent any military */
		p_ptr->military_spent = 0;

		/* Player has not discarded any end of turn cards */
		p_ptr->end_discard = 0;

		/* No cards played yet */
		p_ptr->table_order = 0;

		/* Player has no prestige */
		p_ptr->prestige = p_ptr->prestige_turn = 0;

		/* Player has no points */
		p_ptr->vp = p_ptr->goal_vp = p_ptr->end_vp = 0;

		/* Player is not the winner */
		p_ptr->winner = 0;

		/* Player has earned no rewards this phase */
		p_ptr->phase_cards = p_ptr->phase_vp = 0;
		p_ptr->phase_prestige = 0;

		/* Player has no fake cards */
		p_ptr->fake_hand = 0;
		p_ptr->fake_discards = 0;
		p_ptr->drawn_round = 0;

		/* Player has not skipped build phases */
		p_ptr->skip_develop = p_ptr->skip_settle = 0;

		/* Clear lowest hand size */
		p_ptr->low_hand = 0;
	}

	/* Check for campaign */
	if (g->camp)
	{
		/* Set aside campaign cards */
		init_campaign(g);
	}

	/* Add goals when expanded */
	if (g->expanded > 0 && g->expanded < 4 && !g->goal_disabled)
	{
		/* No goals available yet */
		n = 0;

		/* Use correct "first" goals */
		if (g->expanded == 1)
		{
			/* First expansion only */
			j = GOAL_FIRST_5_VP;
			k = GOAL_FIRST_SIX_DEVEL;
		}
		else if (g->expanded == 2)
		{
			/* First and second expansion */
			j = GOAL_FIRST_5_VP;
			k = GOAL_FIRST_8_ACTIVE;
		}
		else
		{
			/* All expansions */
			j = GOAL_FIRST_5_VP;
			k = GOAL_FIRST_4_MILITARY;
		}

		/* Add "first" goals to list */
		for (i = j; i <= k; i++)
		{
			/* Add goal to list */
			goal[n++] = i;
		}

		/* Assume no campaign goals */
		k = 0;

		/* Check for campaign goals */
		if (g->camp) num_goal = g->camp->num_goal;

		/* Loop over campaign goals */
		for (i = 0; i < num_goal; i++)
		{
			/* Skip "most" goals */
			if (g->camp->goal[i] > GOAL_FIRST_4_MILITARY) continue;

			/* Goal is active */
			g->goal_active[g->camp->goal[i]] = 1;

			/* Goal is available */
			g->goal_avail[g->camp->goal[i]] = 1;

			/* Remove campaign goal from list */
			for (j = 0; j < n; j++)
			{
				/* Check for match */
				if (goal[j] == g->camp->goal[i])
				{
					/* Remove from list */
					goal[j] = goal[--n];
				}
			}

			/* Count campaign "first" goals */
			k++;
		}

		/* Select four "first" goals at random */
		for (i = k; i < 4; i++)
		{
			/* Choose goal at random */
			j = game_rand(g) % n;

			/* Goal is active */
			g->goal_active[goal[j]] = 1;

			/* Goal is available */
			g->goal_avail[goal[j]] = 1;

			/* Remove chosen goal from list */
			goal[j] = goal[--n];
		}

		/* No goals available yet */
		n = 0;

		/* Use correct "most" goals */
		if (g->expanded == 1)
		{
			/* First expansion only */
			j = GOAL_MOST_MILITARY;
			k = GOAL_MOST_PRODUCTION;
		}
		else if (g->expanded == 2)
		{
			/* First and second expansion */
			j = GOAL_MOST_MILITARY;
			k = GOAL_MOST_REBEL;
		}
		else
		{
			/* All expansions */
			j = GOAL_MOST_MILITARY;
			k = GOAL_MOST_CONSUME;
		}

		/* Add "most" goals to list */
		for (i = j; i <= k; i++)
		{
			/* Add goal to list */
			goal[n++] = i;
		}

		/* Assume no campaign goals */
		k = 0;

		/* Loop over campaign goals */
		for (i = 0; i < num_goal; i++)
		{
			/* Skip "first" goals */
			if (g->camp->goal[i] < GOAL_MOST_MILITARY) continue;

			/* Goal is active */
			g->goal_active[g->camp->goal[i]] = 1;

			/* Goal is available */
			g->goal_avail[g->camp->goal[i]] = 1;

			/* Remove campaign goal from list */
			for (j = 0; j < n; j++)
			{
				/* Check for match */
				if (goal[j] == g->camp->goal[i])
				{
					/* Remove from list */
					goal[j] = goal[--n];
				}
			}

			/* Count campaign goals */
			k++;
		}

		/* Select two "most" goals at random */
		for (i = k; i < 2; i++)
		{
			/* Choose goal at random */
			j = game_rand(g) % n;

			/* Goal is active */
			g->goal_active[goal[j]] = 1;

			/* Goal is available */
			g->goal_avail[goal[j]] = 1;

			/* Remove chosen goal from list */
			goal[j] = goal[--n];
		}
	}
}
