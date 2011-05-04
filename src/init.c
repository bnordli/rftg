/*
 * Race for the Galaxy AI
 * 
 * Copyright (C) 2009-2011 Keldon Jones
 *
 * Source file modified by B. Nordli, May 2011.
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
 * Card designs.
 */
design library[MAX_DESIGN];

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
		"CONSUME_RARE",
		"CONSUME_GENE",
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
		"GET_VP",
		"GET_PRESTIGE",
		"DRAW",
		"DRAW_LUCKY",
		"DISCARD_HAND",
		"ANTE_CARD",
		"VP",
		"RECYCLE",
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
		"DRAW_MOST_RARE",
		"DRAW_MOST_PRODUCED",
		"DRAW_DIFFERENT",
		"PRESTIGE_MOST_CHROMO",
		"DRAW_MILITARY",
		"DRAW_REBEL",
		"DRAW_CHROMO",
		"TAKE_SAVED",
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
	"NAME",
	NULL
};

/*
 * Lookup a power code.
 */
static uint64_t lookup_power(char *ptr, int phase)
{
	int i = 0;

	/* Loop over power names */
	while (power_name[phase][i])
	{
		/* Check this power */
		if (!strcmp(power_name[phase][i], ptr)) return 1ULL << i;

		/* Next effect */
		i++;
	}

	/* No match */
	printf("No power named '%s'\n", ptr);
	exit(1);
}

/*
 * Read card designs from 'cards.txt' file.
 */
int read_cards(void)
{
	FILE *fff;
	char buf[1024], *ptr;
	int num_design = 0;
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

	/* Check for failure */
	if (!fff)
	{
		/* Print error and exit */
		perror("cards.txt");
		return -1;
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
				d_ptr->type = strtol(ptr, NULL, 0);

				/* Get cost string */
				ptr = strtok(NULL, ":");

				/* Read cost */
				d_ptr->cost = strtol(ptr, NULL, 0);

				/* Get VP string */
				ptr = strtok(NULL, ":");

				/* Read VP */
				d_ptr->vp = strtol(ptr, NULL, 0);
				break;

			/* Expansion counts */
			case 'E':

				/* Get first count string */
				ptr = strtok(buf + 2, ":");

				/* Loop over number of expansions */
				for (i = 0; i < MAX_EXPANSION; i++)
				{
					/* Set count */
					d_ptr->expand[i] = strtol(ptr, NULL, 0);

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
				o_ptr->value = strtol(ptr, NULL, 0);

				/* Get times string */
				ptr = strtok(NULL, ":");

				/* Read power's number of times */
				o_ptr->times = strtol(ptr, NULL, 0);
				break;

			/* VP flags */
			case 'V':

				/* Get VP bonus */
				v_ptr = &d_ptr->bonuses[d_ptr->num_vp_bonus++];

				/* Get point string */
				ptr = strtok(buf + 2, ":");

				/* Read point value */
				v_ptr->point = strtol(ptr, NULL, 0);

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
 * Initialize a game.
 */
void init_game(game *g)
{
	player *p_ptr;
	design *d_ptr;
	card *c_ptr;
	int goal[MAX_GOAL];
	int i, j, k, n;

	/* Save current random seed */
	g->start_seed = g->random_seed;

#if 0
	sprintf(msg, "start seed: %u\n", g->start_seed);
	message_add(msg);
#endif

	/* Game is not simulated */
	g->simulation = 0;

	/* Set size of VP pool */
	g->vp_pool = g->num_players * 12;

	/* Increase size of pool in third expansion */
	if (g->expanded >= 3) g->vp_pool += 5;

	/* First game round */
	g->round = 1;

	/* No phase or turn */
	g->cur_action = ACT_ROUND_START;
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
	for (i = 0; i < MAX_DESIGN; i++)
	{
		/* Get design pointer */
		d_ptr = &library[i];

		/* Get number of cards in use */
		n = d_ptr->expand[g->expanded];

		/* Add cards */
		for (j = 0; j < n; j++)
		{
			/* Get card pointer */
			c_ptr = &g->deck[g->deck_size++];

			/* No owner */
			c_ptr->start_owner = c_ptr->owner = -1;

			/* Put location in draw deck */
			c_ptr->start_where = c_ptr->where = WHERE_DECK;

			/* Card is not unpaid */
			c_ptr->unpaid = 0;

			/* Card's location is not known */
			c_ptr->known = 0;

			/* Clear used power list */
			for (k = 0; k < MAX_POWER; k++) c_ptr->used[k] = 0;

			/* Card has not produced */
			c_ptr->produced = 0;

			/* Set card's design */
			c_ptr->d_ptr = d_ptr;

			/* Card is not covered by a good */
			c_ptr->covered = -1;

			/* Card is not followed by any other */
			c_ptr->next = -1;
		}
	}

	/* Add goals when expanded */
	if (g->expanded && !g->goal_disabled)
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

		/* Select four "first" goals at random */
		for (i = 0; i < 4; i++)
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

		/* Select two "most" goals at random */
		for (i = 0; i < 2; i++)
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
		p_ptr->fake_hand = p_ptr->total_fake = 0;
	       	p_ptr->fake_discards = 0;
	}
}
