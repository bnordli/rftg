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
#include "net.h"

/* #define DEBUG */

/*
 * Track number of times neural net is computed.
 */
static int num_computes;


/*
 * A neural net for evaluating hand and active cards.
 */
static net eval;

/*
 * A neural net for predicting role choices.
 */
static net role;

/*
 * Counters for tracking usefulness of role prediction.
 */
static int role_hit, role_miss;
static double role_avg;

static int eval_cache_hit, eval_cache_miss;

/*
 * Size of evaluator neural net.
 */
#define EVAL_HIDDEN 50

/*
 * Size of role predictor neural net.
 */
#define ROLE_HIDDEN 50

/*
 * Number of outputs for role predictor network (basic and advanced).
 */
#define ROLE_OUT          7
#define ROLE_OUT_ADV      23
#define ROLE_OUT_EXP3     15
#define ROLE_OUT_ADV_EXP3 76

/*
 * Amount of current turn to complete simulation of.
 */
#define COMPLETE_ROUND    0
#define COMPLETE_CHECK    1
#define COMPLETE_DEVSET   2

/*
 * Items to count per player to determine leader and amount behind.
 */
#define LEADER_VP        0
#define LEADER_PRESTIGE  1
#define LEADER_BUILT     2
#define LEADER_CARDS     3
#define LEADER_GOODS     4
#define MAX_LEADER       5


/*
 * Forward declaration.
 */
static void initial_training(game *g);
static void setup_nets(game *g);
static void fill_adv_combo(void);


/*
 * Initialize AI.
 */
static void ai_initialize(game *g, int who, double factor)
{
	char fname[1024], msg[1024];
	static int loaded_p, loaded_e, loaded_a;

	/* Create table of advanced action combinations */
	fill_adv_combo();

	/* Do nothing if correct networks already loaded */
	if (loaded_p == g->num_players && loaded_e == g->expanded &&
	    loaded_a == g->advanced) return;

	/* Free old networks if some already loaded */
	if (loaded_p > 0)
	{
		/* Free old networks */
		free_net(&eval);
		free_net(&role);
	}

	/* Compute size and input names of networks */
	setup_nets(g);

	/* Set learning rate */
	eval.alpha = 0.0001 * factor;
#ifdef DEBUG
	eval.alpha = 0.0;
#endif

	/* Create evaluator filename */
	sprintf(fname, RFTGDIR "/network/rftg.eval.%d.%d%s.net", g->expanded,
	        g->num_players, g->advanced ? "a" : "");

	/* Attempt to load network weights from disk */
	if (load_net(&eval, fname))
	{
		/* Try looking under current directory */
		sprintf(fname, "network/rftg.eval.%d.%d%s.net", g->expanded,
		        g->num_players, g->advanced ? "a" : "");

		/* Attempt to load again */
		if (load_net(&eval, fname))
		{
			/* Print warning */
			sprintf(msg, "Warning: Couldn't open %s\n", fname);
			display_error(msg);

			/* Perform initial training on new network */
			initial_training(g);
		}
	}

	/* Set learning rate */
	role.alpha = 0.0005 * factor;
#ifdef DEBUG
	role.alpha = 0.0;
#endif

	/* Create predictor filename */
	sprintf(fname, RFTGDIR "/network/rftg.role.%d.%d%s.net", g->expanded,
	        g->num_players, g->advanced ? "a" : "");

	/* Attempt to load network weights from disk */
	if (load_net(&role, fname))
	{
		/* Try looking under current directory */
		sprintf(fname, "network/rftg.role.%d.%d%s.net", g->expanded,
		        g->num_players, g->advanced ? "a" : "");

		/* Attempt to load again */
		if (load_net(&role, fname))
		{
			/* Print warning */
			sprintf(msg, "Warning: Couldn't open %s\n", fname);
			display_error(msg);
		}
	}

	/* Mark network as loaded */
	loaded_p = g->num_players;
	loaded_e = g->expanded;
	loaded_a = g->advanced;
}

/*
 * Called when player spots have been rotated.
 *
 * We need to do nothing.
 */
static void ai_notify_rotation(game *g, int who)
{
}

/*
 * Copy the game state to a temporary copy so that we can simulate the future.
 */
static void simulate_game(game *sim, game *orig, int who)
{
	int i;

	/* Copy game */
	memcpy(sim, orig, sizeof(game));

	/* Loop over players */
	for (i = 0; i < sim->num_players; i++)
	{
		/* Move choice log position to end */
		sim->p[i].choice_pos = sim->p[i].choice_size;
	}

	/* Check for first-level simulation */
	if (!sim->simulation)
	{
		/* Set simulation flag */
		sim->simulation = 1;

		/* Lose real random seed */
		sim->random_seed = 1;

		/* Remember whose point-of-view to use */
		sim->sim_who = who;

		/* Loop over players */
		for (i = 0; i < sim->num_players; i++)
		{
			/* Set action functions to AI  */
			sim->p[i].control = &ai_func;
		}
	}
}

/*
 * Structure holding most discardable cards.
 *
 * A list of these is created at the start of each round for each AI
 * player, and used to quickly determine which cards will no longer
 * be in the hand at the end of the round.
 */
typedef struct quick_discard
{
	/* Card index */
	int which;

	/* Score without this card */
	double score;

} quick_discard;

/*
 * List of most discardable cards (per player).
 */
quick_discard discard_list[MAX_PLAYER][MAX_DECK];

/*
 * Compare two quick discard entries.
 */
static int cmp_quick_discard(const void *a, const void *b)
{
	quick_discard *q_ptr, *r_ptr;

	/* Cast pointers */
	q_ptr = (quick_discard *)a;
	r_ptr = (quick_discard *)b;

	/* Compare scores */
	return r_ptr->score > q_ptr->score ? 1 : -1;
}

/*
 * Quick-discard the given number of cards.
 */
static void ai_quick_discard(game *g, int who, int amt)
{
	int i, x, n = 0;

	/* Loop until discards are satisfied */
	for (i = 0; n < amt; i++)
	{
		/* Get card */
		x = discard_list[who][i].which;

		/* XXX Check for running off end of list */
		if (x < 0) break;

		/* Check for no longer in hand */
		if (g->deck[x].owner != who || g->deck[x].where != WHERE_HAND)
		{
			/* Go to next card */
			continue;
		}

		/* Discard */
		move_card(g, x, -1, WHERE_DISCARD);

		/* Count cards discarded */
		n++;

		/* Reduce fake discards */
		g->p[who].fake_discards--;
	}
}


/*
 * Complete the current turn.
 *
 * We call this before evaluating a game state, to attempt to foresee
 * consequences of chosen actions.
 */
static void complete_turn(game *g, int partial)
{
	player *p_ptr;
	int i, target;

	/* Ensure game is simulated */
	if (!g->simulation)
	{
		/* Error */
		display_error("complete_turn() called with real game!\n");
		abort();
	}

	/* Do nothing in aborted games */
	if (g->game_over) return;

	/* Finish current phase */
	for (i = g->turn + 1; i < g->num_players; i++)
	{
		/* Get player pointer */
		p_ptr = &g->p[i];

		/* Set turn */
		g->turn = i;

		/* Check for consume phase */
		if (g->cur_action == ACT_CONSUME_TRADE)
		{
			/* Check for consume-trade chosen */
			if (player_chose(g, i, ACT_CONSUME_TRADE))
			{
				/* Trade a good */
				trade_action(g, i, 0, 1);
			}

			/* Use consume powers until none are available */
			while (consume_action(g, i));
		}

		/* Check for produce phase */
		if (g->cur_action == ACT_PRODUCE)
		{
			/* Use produce phase powers */
			while (produce_action(g, i));
		}
	}

	/* Resolve any pending takeovers */
	if (g->cur_action == ACT_SETTLE || g->cur_action == ACT_SETTLE2)
		resolve_takeovers(g);

	/* Use final produce powers */
	if (g->cur_action == ACT_PRODUCE) phase_produce_end(g);

	/* Clear temp flags from just-finished phase */
	clear_temp(g);

	/* Check goals from just-finished phase */
	check_goals(g);

	/* Check prestige from just-finished phase */
	check_prestige(g);

	/* Loop over remaining phases */
	for (i = g->cur_action + 1; i <= ACT_PRODUCE; i++)
	{
		/* Check for stop after develop/settle */
		if (partial == COMPLETE_DEVSET && i >= ACT_CONSUME_TRADE)
		{
			/* Stop simulation */
			break;
		}

		/* Check for only completing one phase */
		if (partial == COMPLETE_CHECK) break;

		/* Set game phase */
		g->cur_action = i;

		/* Skip unselected actions */
		if (!g->action_selected[i]) continue;

		/* Handle phase */
		switch (i)
		{
			case ACT_SEARCH: phase_search(g); break;
			case ACT_EXPLORE_5_0: phase_explore(g); break;
			case ACT_DEVELOP:
			case ACT_DEVELOP2: phase_develop(g); break;
			case ACT_SETTLE:
			case ACT_SETTLE2: phase_settle(g); break;
			case ACT_CONSUME_TRADE: phase_consume(g); break;
			case ACT_PRODUCE: phase_produce(g); break;
		}
	}

	/* End of round */
	g->cur_action = ACT_ROUND_END;

	/* Handle discard phase */
	if (partial == COMPLETE_ROUND) phase_discard(g);

	/* Check intermediate goals */
	if (partial == COMPLETE_ROUND) check_goals(g);

	/* Check for game end */
	if (g->vp_pool <= 0) g->game_over = 1;

	/* Check for full table */
	for (i = 0; i < g->num_players; i++)
	{
		/* Get player pointer */
		p_ptr = &g->p[i];

		/* Assume player needs 12 cards to end game */
		target = 12;

		/* Check for "game ends at 14" flag */
		if (count_active_flags(g, i, FLAG_GAME_END_14)) target = 14;

		/* Check for enough cards */
		if (count_player_area(g, i, WHERE_ACTIVE) >= target)
		{
			/* Game over */
			g->game_over = 1;
		}

		/* Check for enough prestige to end game */
		if (p_ptr->prestige >= 15) g->game_over = 1;
	}

	/* Handle beginning of next round if possible */
	if (!g->game_over && partial == COMPLETE_ROUND)
	{
		/* Award prestige bonuses */
		start_prestige(g);

		/* Get player pointer of simulating player */
		p_ptr = &g->p[g->sim_who];

		/* Count cards still believed to be in hand */
		target = count_player_area(g, g->sim_who, WHERE_HAND);

		/* Check for cards discarded */
		if (p_ptr->low_hand < target)
		{
			/* Discard cards to make up difference */
			ai_quick_discard(g, g->sim_who,
			                 target - p_ptr->low_hand);
		}
	}
}

/*
 * Score a consume power for consume_ability().
 *
 * This actually returns VP * 100, to avoid floating point math.
 */
static int consume_vp(power *o_ptr)
{
	int vp, cost;
	int score;

	/* Get VP worth of power */
	vp = o_ptr->value;

	/* Assume 1 good consumed */
	cost = 1;

	/* Check for consume two */
	if (o_ptr->code & P4_CONSUME_TWO) cost = 2;

	/* Check for consume three */
	if (o_ptr->code & P4_CONSUME_3_DIFF) cost = 3;

	/* Check for consume all remaining */
	if (o_ptr->code & P4_CONSUME_ALL)
	{
		/* Assume 3 goods remain */
		vp = 2;
		cost = 3;
	}

	/* Compute basic score */
	score = vp * 100 / cost;

	/* Slightly penalize generic consume powers */
	if (o_ptr->code & P4_CONSUME_ANY) score--;

	/* Return score */
	return score;
}

/*
 * Sort two consume powers by number of VPs/good they return.
 *
 * If they are equal, sort specific-type powers before general ones.
 */
static int cmp_consume(const void *a, const void *b)
{
	power_where *w_ptr, *x_ptr;

	/* Cast pointers */
	w_ptr = (power_where *)a;
	x_ptr = (power_where *)b;

	/* Score powers */
	return consume_vp(x_ptr->o_ptr) - consume_vp(w_ptr->o_ptr);
}

/*
 * Determine the amount of consume/produce power a player has.
 *
 * This is roughly equal to the amount of VPs earned from Consume
 * powers that have a matching production world.
 *
 * We also count as production worlds any windfall worlds with a
 * matching "produce on windfall" power.
 *
 * If "produce" is false, return amount of VPs earnable with current
 * goods on worlds.
 */
static int consume_ability(game *g, int who, int produce)
{
	player *p_ptr;
	card *c_ptr;
	design *d_ptr;
	power *o_ptr;
	power_where w_list[100];
	int i, j, k, x, n = 0;
	int consume_goods[6], produce_goods[6];
	int windfall_world[6], windfall_power[6];
	int total_goods, good_types;
	int good, cost, progress, count = 0;
	int hand_size, prestige_avail;

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Clear good type arrays */
	for (i = 0; i <= GOOD_ALIEN; i++)
	{
		/* Clear arrays */
		consume_goods[i] = 0;
		produce_goods[i] = 0;
		windfall_world[i] = 0;
		windfall_power[i] = 0;
	}

	/* Count hand size */
	hand_size = count_player_area(g, who, WHERE_HAND) + p_ptr->fake_hand -
	            p_ptr->fake_discards;

	/* Remember available prestige */
	prestige_avail = p_ptr->prestige;

	/* Start at player's first active card */
	x = p_ptr->head[WHERE_ACTIVE];

	/* Loop over active cards */
	for ( ; x != -1; x = g->deck[x].next)
	{
		/* Get card pointer */
		c_ptr = &g->deck[x];

		/* Get design pointer */
		d_ptr = c_ptr->d_ptr;

		/* Check for windfall world */
		if (d_ptr->flags & FLAG_WINDFALL)
		{
			/* Add to count of windfall worlds */
			windfall_world[d_ptr->good_type]++;
		}

		/* Check for goods on world */
		if (!produce && c_ptr->num_goods)
		{
			/* Count goods */
			produce_goods[d_ptr->good_type] += c_ptr->num_goods;
		}

		/* Loop over powers on card */
		for (i = 0; i < d_ptr->num_power; i++)
		{
			/* Get power pointer */
			o_ptr = &d_ptr->powers[i];

			/* Check for Consume power */
			if (o_ptr->phase == PHASE_CONSUME)
			{
				/* Chcek for free VP */
				if (o_ptr->code & P4_VP)
				{
					/* Just add to count */
					count += o_ptr->value;
					continue;
				}

				/* Skip powers that don't give VP */
				if (!(o_ptr->code & P4_GET_VP)) continue;

				/* Check for powers that don't consume goods */
				if (o_ptr->code & (P4_DISCARD_HAND |
						   P4_CONSUME_PRESTIGE))
				{
					/* Skip if not checking immediate */
					if (!produce) continue;

					/* Check for discarding from hand */
					if (o_ptr->code & P4_DISCARD_HAND)
					{
						/* Check for sufficient cards */
						if (hand_size >= o_ptr->times)
						{
							/* Add to count */
							count += o_ptr->times;
							hand_size-=o_ptr->times;
						}
					}

					/* Check for consuming prestige */
					if (o_ptr->code & P4_CONSUME_PRESTIGE)
					{
						/* Check for sufficient */
						if (prestige_avail)
						{
							/* Add to count */
							count += o_ptr->value;
							prestige_avail--;
						}
					}

					/* Next power */
					continue;
				}

				/* Assume one good consumed */
				cost = 1;

				/* Check for two */
				if (o_ptr->code & P4_CONSUME_TWO) cost = 2;

				/* Add power to list */
				w_list[n].c_idx = x;
				w_list[n].o_idx = i;
				w_list[n].o_ptr = o_ptr;
				n++;

				/* Check for specific type required */
				if (o_ptr->code & P4_CONSUME_NOVELTY)
					consume_goods[GOOD_NOVELTY] += cost;
				if (o_ptr->code & P4_CONSUME_RARE)
					consume_goods[GOOD_RARE] += cost;
				if (o_ptr->code & P4_CONSUME_GENE)
					consume_goods[GOOD_GENE] += cost;
				if (o_ptr->code & P4_CONSUME_ALIEN)
					consume_goods[GOOD_ALIEN] += cost;

				/* Check for multiple types consumed */
				if (o_ptr->code & (P4_CONSUME_3_DIFF |
				                   P4_CONSUME_N_DIFF))
				{
					/* All types required */
					consume_goods[GOOD_NOVELTY]++;
					consume_goods[GOOD_RARE]++;
					consume_goods[GOOD_GENE]++;
					consume_goods[GOOD_ALIEN]++;
				}
			}

			/* Skip produce powers if just checking goods */
			if (!produce) continue;

			/* Check for Produce power */
			if (o_ptr->phase == PHASE_PRODUCE)
			{
				/* Check for basic produce power */
				if (o_ptr->code & P5_PRODUCE)
				{
					/* Count goods produced */
					produce_goods[d_ptr->good_type]++;
					continue;
				}

				/* Skip powers that require discarding */
				if (o_ptr->code & P5_DISCARD) continue;

				/* Check for windfall production powers */
				if (o_ptr->code & P5_WINDFALL_ANY)
					windfall_power[GOOD_ANY]++;
				if (o_ptr->code & P5_WINDFALL_NOVELTY)
					windfall_power[GOOD_NOVELTY]++;
				if (o_ptr->code & P5_WINDFALL_RARE)
					windfall_power[GOOD_RARE]++;
				if (o_ptr->code & P5_WINDFALL_GENE)
					windfall_power[GOOD_GENE]++;
				if (o_ptr->code & P5_WINDFALL_ALIEN)
					windfall_power[GOOD_ALIEN]++;
			}
		}
	}

	/* Assume produce phase bonus is used */
	if (produce) windfall_power[GOOD_ANY]++;

	/* Convert windfall powers to good production */
	for (i = GOOD_ANY; i <= GOOD_ALIEN; i++)
	{
		/* Skip good types with no powers or worlds */
		if (!windfall_world[i]) continue;
		if (!windfall_power[i]) continue;

		/* Check for more windfall powers than worlds */
		if (windfall_power[i] > windfall_world[i])
		{
			/* Convert all windfall worlds to production */
			produce_goods[i] += windfall_world[i];
			windfall_power[i] -= windfall_world[i];
			windfall_world[i] = 0;
		}
		else
		{
			/* Convert all windfall powers to production */
			produce_goods[i] += windfall_power[i];
			windfall_world[i] -= windfall_power[i];
			windfall_power[i] = 0;
		}
	}

	/* Run loop at least once */
	progress = 1;

	/* Convert "any" windfall production to normal */
	while (windfall_power[GOOD_ANY] > 0 && progress)
	{
		/* Assume no progress will be made */
		progress = 0;

		/* Loop over types */
		for (i = GOOD_NOVELTY; i <= GOOD_ALIEN; i++)
		{
			/* Check for no windfall worlds of this type */
			if (!windfall_world[i]) continue;

			/* Check for no more worlds of this type needed */
			if (consume_goods[i] <= produce_goods[i]) continue;

			/* Use one any windfall power on this type */
			windfall_power[GOOD_ANY]--;
			produce_goods[i]++;
			windfall_world[i]--;

			/* Progress made */
			progress = 1;

			/* Stop looking */
			break;
		}
	}

	/* Run loop at least once */
	progress = 1;

	/* Convert "any" windfall production to normal */
	while (windfall_power[GOOD_ANY] > 0 && progress)
	{
		/* Assume no progress will be made */
		progress = 0;

		/* Loop over types */
		for (i = GOOD_NOVELTY; i <= GOOD_ALIEN; i++)
		{
			/* Check for no windfall worlds of this type */
			if (!windfall_world[i]) continue;

			/* Use one any windfall power on this type */
			windfall_power[GOOD_ANY]--;
			produce_goods[i]++;
			windfall_world[i]--;

			/* Progress made */
			progress = 1;

			/* Stop looking */
			break;
		}
	}

	/* Sort consume powers by score */
	qsort(w_list, n, sizeof(power_where), cmp_consume);

	/* Loop over consume powers, best first */
	for (i = 0; i < n; i++)
	{
		/* Get power pointer */
		o_ptr = w_list[i].o_ptr;

		/* Loop over power a number of times */
		for (j = 0; j < o_ptr->times; j++)
		{
			/* Assume one good consumed */
			cost = 1;

			/* Check for two */
			if (o_ptr->code & P4_CONSUME_TWO) cost = 2;

			/* No good type */
			good = 0;

			/* Check for specific consume type */
			if (o_ptr->code & P4_CONSUME_NOVELTY)
				good = GOOD_NOVELTY;
			if (o_ptr->code & P4_CONSUME_RARE)
				good = GOOD_RARE;
			if (o_ptr->code & P4_CONSUME_GENE)
				good = GOOD_GENE;
			if (o_ptr->code & P4_CONSUME_ALIEN)
				good = GOOD_ALIEN;

			/* Check for goods available */
			if (good && produce_goods[good] >= cost)
			{
				/* Award points and use goods */
				count += o_ptr->value;
				produce_goods[good] -= cost;
				consume_goods[good] -= cost;
				continue;
			}

			/* Check for insufficient goods */
			if (good) break;

			/* Assume no goods or good types */
			total_goods = good_types = 0;

			/* Loop over types */
			for (k = GOOD_NOVELTY; k <= GOOD_ALIEN; k++)
			{
				/* Add goods to total */
				total_goods += produce_goods[k];

				/* Check for type available */
				if (produce_goods[k]) good_types++;
			}

			/* Check for consume any goods */
			if (o_ptr->code & P4_CONSUME_ANY)
			{
				/* Check for insufficient goods */
				if (total_goods < cost) break;

				/* Loop over types */
				for (k = GOOD_NOVELTY; k <= GOOD_ALIEN; k++)
				{
					/* Skip unavailable types */
					if (!produce_goods[k]) continue;

					/* Skip good types needed later */
					if (consume_goods[k]) continue;

					/* Use a good */
					produce_goods[k]--;
					cost--;

					/* Check for done */
					if (!cost) break;

					/* Recheck this type */
					k--;
				}

				/* Loop over types */
				for (k = GOOD_NOVELTY; k <= GOOD_ALIEN; k++)
				{
					/* Check for done */
					if (!cost) break;

					/* Skip unavailable types */
					if (!produce_goods[k]) continue;

					/* Use a good */
					produce_goods[k]--;
					cost--;

					/* Check for done */
					if (!cost) break;

					/* Recheck this type */
					k--;
				}

				/* Award VPs */
				count += o_ptr->value;
			}

			/* Check for consume 3 different types */
			if (o_ptr->code & P4_CONSUME_3_DIFF)
			{
				/* Check for insufficient types */
				if (good_types < 3) break;

				/* Cost is 3 goods */
				cost = 3;

				/* Loop over types */
				for (k = GOOD_NOVELTY; k <= GOOD_ALIEN; k++)
				{
					/* Skip unavailable types */
					if (!produce_goods[k]) continue;

					/* Use a good */
					produce_goods[k]--;
					cost--;

					/* Check for done paying */
					if (!cost) break;
				}

				/* Award VPs */
				count += o_ptr->value;
			}

			/* Check for consume all different types */
			if (o_ptr->code & P4_CONSUME_N_DIFF)
			{
				/* Loop over types */
				for (k = GOOD_NOVELTY; k <= GOOD_ALIEN; k++)
				{
					/* Skip unavailable types */
					if (!produce_goods[k]) continue;

					/* Award VPs */
					count += o_ptr->value;

					/* Consume good */
					produce_goods[k]--;
				}
			}

			/* Check for consume all remaining goods */
			if (o_ptr->code & P4_CONSUME_ALL)
			{
				/* Check for insufficient goods */
				if (total_goods < 2) break;

				/* Award VPs */
				count += total_goods - 1;
			}
		}
	}

	/* Return count of VP earned */
	return count;
}

/*
 * List of all advanced game action combinations.
 */
static int adv_combo[ROLE_OUT_ADV_EXP3][2] =
{
	{ ACT_EXPLORE_5_0, ACT_EXPLORE_1_1 },
	{ ACT_EXPLORE_5_0, ACT_DEVELOP },
	{ ACT_EXPLORE_5_0, ACT_SETTLE },
	{ ACT_EXPLORE_5_0, ACT_CONSUME_TRADE },
	{ ACT_EXPLORE_5_0, ACT_CONSUME_X2 },
	{ ACT_EXPLORE_5_0, ACT_PRODUCE },
	{ ACT_EXPLORE_1_1, ACT_DEVELOP },
	{ ACT_EXPLORE_1_1, ACT_SETTLE },
	{ ACT_EXPLORE_1_1, ACT_CONSUME_TRADE },
	{ ACT_EXPLORE_1_1, ACT_CONSUME_X2 },
	{ ACT_EXPLORE_1_1, ACT_PRODUCE },
	{ ACT_DEVELOP, ACT_DEVELOP2 },
	{ ACT_DEVELOP, ACT_SETTLE },
	{ ACT_DEVELOP, ACT_CONSUME_TRADE },
	{ ACT_DEVELOP, ACT_CONSUME_X2 },
	{ ACT_DEVELOP, ACT_PRODUCE },
	{ ACT_SETTLE, ACT_SETTLE2 },
	{ ACT_SETTLE, ACT_CONSUME_TRADE },
	{ ACT_SETTLE, ACT_CONSUME_X2 },
	{ ACT_SETTLE, ACT_PRODUCE },
	{ ACT_CONSUME_TRADE, ACT_CONSUME_X2 },
	{ ACT_CONSUME_TRADE, ACT_PRODUCE },
	{ ACT_CONSUME_X2, ACT_PRODUCE }
};

/*
 * Mapping of role outputs in non-advanced game to actions.
 */
static int role_out[ROLE_OUT_EXP3] =
{
	ACT_EXPLORE_5_0,
	ACT_EXPLORE_1_1,
	ACT_DEVELOP,
	ACT_SETTLE,
	ACT_CONSUME_TRADE,
	ACT_CONSUME_X2,
	ACT_PRODUCE,
	ACT_SEARCH,
	ACT_PRESTIGE | ACT_EXPLORE_5_0,
	ACT_PRESTIGE | ACT_EXPLORE_1_1,
	ACT_PRESTIGE | ACT_DEVELOP,
	ACT_PRESTIGE | ACT_SETTLE,
	ACT_PRESTIGE | ACT_CONSUME_TRADE,
	ACT_PRESTIGE | ACT_CONSUME_X2,
	ACT_PRESTIGE | ACT_PRODUCE
};

/*
 * Mapping from card indices to neural network inputs.
 */
static int card_input[MAX_DESIGN], num_c_input;
static int good_input[MAX_DESIGN], num_g_input;

/*
 * Setup mappings of card indices to neural net inputs.
 *
 * Also create network input names.
 */
static void setup_nets(game *g)
{
	design *d_ptr;
	int i, j, k, n;
	int outputs;
	char buf[1024], name[1024], *input_name[5000];

	/* Reset input numbers */
	num_c_input = num_g_input = 0;

	/* Loop over card designs */
	for (i = 0; i < MAX_DESIGN; i++)
	{
		/* Clear input mapping */
		card_input[i] = good_input[i] = -1;

		/* Get design pointer */
		d_ptr = &library[i];

		/* Skip cards that have no copies in this game */
		if (d_ptr->expand[g->expanded] == 0) continue;

		/* Add mapping of this card design */
		card_input[i] = num_c_input++;

		/* Skip cards that cannot hold goods */
		if (d_ptr->good_type == 0) continue;

		/* Add mapping of this good-holding card */
		good_input[i] = num_g_input++;
	}

	/* Start at first input */
	n = 0;

	/* Set input names */
	input_name[n++] = strdup("Game over");
	for (i = 0; i < 12; i++)
	{
		sprintf(buf, "VP Pool %d", i);
		input_name[n++] = strdup(buf);
	}
	for (i = 0; i < 12; i++)
	{
		sprintf(buf, "Max active %d", i);
		input_name[n++] = strdup(buf);
	}
	for (i = 0; i < 12; i++)
	{
		sprintf(buf, "Clock %d", i);
		input_name[n++] = strdup(buf);
	}
	if (g->expanded > 0 && g->expanded < 4)
	{
		for (i = 0; i < MAX_GOAL; i++)
		{
			sprintf(buf, "Goal active %d", i);
			input_name[n++] = strdup(buf);
		}
		for (i = 0; i < MAX_GOAL; i++)
		{
			sprintf(buf, "Goal available %d", i);
			input_name[n++] = strdup(buf);
		}
	}
	for (i = 0; i < num_c_input; i++)
	{
		for (j = 0; j < MAX_DESIGN; j++)
		{
			if (card_input[j] == i) break;
		}
		sprintf(buf, "%s in hand", library[j].name);
		input_name[n++] = strdup(buf);
	}
	for (i = 0; i < 5; i++)
	{
		sprintf(buf, "Dev buildable %d", i);
		input_name[n++] = strdup(buf);
	}
	for (i = 0; i < 5; i++)
	{
		sprintf(buf, "World buildable %d", i);
		input_name[n++] = strdup(buf);
	}
	for (i = 0; i < g->num_players; i++)
	{
		if (i == 0) strcpy(name, "Us");
		else
		{
			sprintf(name, "Opponent %d", i);
		}

		for (j = 0; j < num_c_input; j++)
		{
			for (k = 0; k < MAX_DESIGN; k++)
			{
				if (card_input[k] == j) break;
			}
			sprintf(buf, "%s active %s", name, library[k].name);
			input_name[n++] = strdup(buf);
		}

		for (j = 0; j < num_g_input; j++)
		{
			for (k = 0; k < MAX_DESIGN; k++)
			{
				if (good_input[k] == j) break;
			}
			sprintf(buf, "%s good %s", name, library[k].name);
			input_name[n++] = strdup(buf);
		}

		for (j = 0; j < 6; j++)
		{
			sprintf(buf, "%s %d goods", name, j);
			input_name[n++] = strdup(buf);
		}

		sprintf(buf, "%s Novelty good", name);
		input_name[n++] = strdup(buf);
		sprintf(buf, "%s Rare good", name);
		input_name[n++] = strdup(buf);
		sprintf(buf, "%s Gene good", name);
		input_name[n++] = strdup(buf);
		sprintf(buf, "%s Alien good", name);
		input_name[n++] = strdup(buf);

		for (j = 0; j < 12; j++)
		{
			sprintf(buf, "%s %d cards", name, j);
			input_name[n++] = strdup(buf);
		}

		for (j = 0; j < 15; j++)
		{
			sprintf(buf, "%s %d cards seen", name, j);
			input_name[n++] = strdup(buf);
		}

		for (j = 0; j < 10; j++)
		{
			sprintf(buf, "%s %d developments", name, j);
			input_name[n++] = strdup(buf);
		}

		for (j = 0; j < 10; j++)
		{
			sprintf(buf, "%s %d worlds", name, j);
			input_name[n++] = strdup(buf);
		}

		for (j = 0; j < 5; j++)
		{
			sprintf(buf, "%s %d six developments", name, j);
			input_name[n++] = strdup(buf);
		}

		for (j = 0; j < 10; j++)
		{
			sprintf(buf, "%s %d military", name, j);
			input_name[n++] = strdup(buf);
		}

		sprintf(buf, "%s conflicting military", name);
		input_name[n++] = strdup(buf);

		sprintf(buf, "%s skip develop", name);
		input_name[n++] = strdup(buf);

		sprintf(buf, "%s skip settle", name);
		input_name[n++] = strdup(buf);

		sprintf(buf, "%s explore mix", name);
		input_name[n++] = strdup(buf);

		for (j = 0; j < 6; j++)
		{
			sprintf(buf, "%s %d consume ability", name, j);
			input_name[n++] = strdup(buf);
		}

		for (j = 0; j < 6; j++)
		{
			sprintf(buf, "%s %d immediate consume", name, j);
			input_name[n++] = strdup(buf);
		}

		if (g->expanded > 0 && g->expanded < 4)
		{
			for (j = 0; j < MAX_GOAL; j++)
			{
				sprintf(buf, "%s goal %d claimed", name, j);
				input_name[n++] = strdup(buf);
			}
		}

		if (g->expanded == 3)
		{
			sprintf(buf, "%s prestige action used", name);
			input_name[n++] = strdup(buf);

			for (j = 0; j < 15; j++)
			{
				sprintf(buf, "%s %d prestige", name, j);
				input_name[n++] = strdup(buf);
			}
		}

		sprintf(buf, "%s winner", name);
		input_name[n++] = strdup(buf);
	}

	for (i = 0; i < g->num_players; i++)
	{
		if (i == 0) strcpy(name, "Us");
		else
		{
			sprintf(name, "Opponent %d", i);
		}
		for (j = 0; j < 20; j++)
		{
			sprintf(buf, "%s %d points behind", name, j);
			input_name[n++] = strdup(buf);
		}
	}

	if (g->expanded == 3)
	{
		for (i = 0; i < g->num_players; i++)
		{
			if (i == 0) strcpy(name, "Us");
			else
			{
				sprintf(name, "Opponent %d", i);
			}
			for (j = 0; j < 5; j++)
			{
				sprintf(buf, "%s %d prestige behind", name, j);
				input_name[n++] = strdup(buf);
			}
		}
	}

	for (i = 0; i < g->num_players; i++)
	{
		if (i == 0) strcpy(name, "Us");
		else
		{
			sprintf(name, "Opponent %d", i);
		}
		for (j = 0; j < 5; j++)
		{
			sprintf(buf, "%s %d built behind", name, j);
			input_name[n++] = strdup(buf);
		}
	}

	for (i = 0; i < g->num_players; i++)
	{
		if (i == 0) strcpy(name, "Us");
		else
		{
			sprintf(name, "Opponent %d", i);
		}
		for (j = 0; j < 10; j++)
		{
			sprintf(buf, "%s %d cards behind", name, j);
			input_name[n++] = strdup(buf);
		}
	}

	for (i = 0; i < g->num_players; i++)
	{
		if (i == 0) strcpy(name, "Us");
		else
		{
			sprintf(name, "Opponent %d", i);
		}
		for (j = 0; j < 5; j++)
		{
			sprintf(buf, "%s %d goods behind", name, j);
			input_name[n++] = strdup(buf);
		}
	}

	/* Create evaluator network */
	make_learner(&eval, n, EVAL_HIDDEN, g->num_players);

	/* Copy input names */
	for (i = 0; i < n; i++)
	{
		/* Copy name */
		eval.input_name[i] = input_name[i];
	}

	/* Check for third expansion */
	if (g->expanded == 3)
	{
		/* Use third expansion number of outputs */
		outputs = g->advanced ? ROLE_OUT_ADV_EXP3 : ROLE_OUT_EXP3;
	}
	else
	{
		/* Use simpler number of outputs */
		outputs = g->advanced ? ROLE_OUT_ADV : ROLE_OUT;
	}

	/* Start at first input */
	n = 0;

	/* Set input names */
	for (i = 0; i < g->num_players; i++)
	{
		if (i == 0) strcpy(name, "Us");
		else
		{
			sprintf(name, "Opponent %d", i);
		}

		for (j = 0; j < num_c_input; j++)
		{
			for (k = 0; k < MAX_DESIGN; k++)
			{
				if (card_input[k] == j) break;
			}
			sprintf(buf, "%s active %s", name, library[k].name);
			input_name[n++] = strdup(buf);
		}

		for (j = 0; j < 10; j++)
		{
			sprintf(buf, "%s %d developments", name, j);
			input_name[n++] = strdup(buf);
		}

		for (j = 0; j < 10; j++)
		{
			sprintf(buf, "%s %d worlds", name, j);
			input_name[n++] = strdup(buf);
		}

		for (j = 0; j < num_g_input; j++)
		{
			for (k = 0; k < MAX_DESIGN; k++)
			{
				if (good_input[k] == j) break;
			}
			sprintf(buf, "%s good %s", name, library[k].name);
			input_name[n++] = strdup(buf);
		}

		for (j = 0; j < 6; j++)
		{
			sprintf(buf, "%s %d goods", name, j);
			input_name[n++] = strdup(buf);
		}

		sprintf(buf, "%s Novelty good", name);
		input_name[n++] = strdup(buf);
		sprintf(buf, "%s Rare good", name);
		input_name[n++] = strdup(buf);
		sprintf(buf, "%s Gene good", name);
		input_name[n++] = strdup(buf);
		sprintf(buf, "%s Alien good", name);
		input_name[n++] = strdup(buf);

		for (j = 0; j < 12; j++)
		{
			sprintf(buf, "%s %d cards", name, j);
			input_name[n++] = strdup(buf);
		}

		for (j = 0; j < 15; j++)
		{
			sprintf(buf, "%s %d cards seen", name, j);
			input_name[n++] = strdup(buf);
		}

		for (j = 0; j < 10; j++)
		{
			sprintf(buf, "%s %d military", name, j);
			input_name[n++] = strdup(buf);
		}

		sprintf(buf, "%s skip develop", name);
		input_name[n++] = strdup(buf);

		sprintf(buf, "%s skip settle", name);
		input_name[n++] = strdup(buf);

		sprintf(buf, "%s explore mix", name);
		input_name[n++] = strdup(buf);

		for (j = 0; j < 6; j++)
		{
			sprintf(buf, "%s %d consume ability", name, j);
			input_name[n++] = strdup(buf);
		}

		for (j = 0; j < 6; j++)
		{
			sprintf(buf, "%s %d immediate consume", name, j);
			input_name[n++] = strdup(buf);
		}

		if (g->expanded > 0 && g->expanded < 4)
		{
			for (j = 0; j < MAX_GOAL; j++)
			{
				sprintf(buf, "%s goal %d claimed", name, j);
				input_name[n++] = strdup(buf);
			}
		}

		if (g->expanded == 3)
		{
			sprintf(buf, "%s prestige action used", name);
			input_name[n++] = strdup(buf);

			for (j = 0; j < 15; j++)
			{
				sprintf(buf, "%s %d prestige", name, j);
				input_name[n++] = strdup(buf);
			}
		}

		for (j = 0; j < MAX_ACTION; j++)
		{
			sprintf(buf, "%s prev %s", name, action_name(j));
			input_name[n++] = strdup(buf);
		}
	}

	for (i = 0; i < g->num_players; i++)
	{
		if (i == 0) strcpy(name, "Us");
		else
		{
			sprintf(name, "Opponent %d", i);
		}
		for (j = 0; j < 20; j++)
		{
			sprintf(buf, "%s %d points behind", name, j);
			input_name[n++] = strdup(buf);
		}
	}

	if (g->expanded == 3)
	{
		for (i = 0; i < g->num_players; i++)
		{
			if (i == 0) strcpy(name, "Us");
			else
			{
				sprintf(name, "Opponent %d", i);
			}
			for (j = 0; j < 5; j++)
			{
				sprintf(buf, "%s %d prestige behind", name, j);
				input_name[n++] = strdup(buf);
			}
		}
	}

	for (i = 0; i < g->num_players; i++)
	{
		if (i == 0) strcpy(name, "Us");
		else
		{
			sprintf(name, "Opponent %d", i);
		}
		for (j = 0; j < 5; j++)
		{
			sprintf(buf, "%s %d build behind", name, j);
			input_name[n++] = strdup(buf);
		}
	}

	for (i = 0; i < g->num_players; i++)
	{
		if (i == 0) strcpy(name, "Us");
		else
		{
			sprintf(name, "Opponent %d", i);
		}
		for (j = 0; j < 10; j++)
		{
			sprintf(buf, "%s %d cards behind", name, j);
			input_name[n++] = strdup(buf);
		}
	}

	for (i = 0; i < g->num_players; i++)
	{
		if (i == 0) strcpy(name, "Us");
		else
		{
			sprintf(name, "Opponent %d", i);
		}
		for (j = 0; j < 5; j++)
		{
			sprintf(buf, "%s %d goods behind", name, j);
			input_name[n++] = strdup(buf);
		}
	}

	for (i = 0; i < 12; i++)
	{
		sprintf(buf, "VP Pool %d", i);
		input_name[n++] = strdup(buf);
	}
	for (i = 0; i < 12; i++)
	{
		sprintf(buf, "Max active %d", i);
		input_name[n++] = strdup(buf);
	}
	for (i = 0; i < 12; i++)
	{
		sprintf(buf, "Clock %d", i);
		input_name[n++] = strdup(buf);
	}
	if (g->expanded > 0 && g->expanded < 4)
	{
		for (i = 0; i < MAX_GOAL; i++)
		{
			sprintf(buf, "Goal active %d", i);
			input_name[n++] = strdup(buf);
		}
		for (i = 0; i < MAX_GOAL; i++)
		{
			sprintf(buf, "Goal available %d", i);
			input_name[n++] = strdup(buf);
		}
	}
	for (i = 0; i < outputs; i++)
	{
		if (g->advanced)
		{
			sprintf(buf, "Raw %s/%s", action_name(adv_combo[i][0]),
						  action_name(adv_combo[i][1]));
		}
		else
		{
			sprintf(buf, "Raw %s", action_name(role_out[i]));
		}
		input_name[n++] = strdup(buf);
	}

	/* Create role predictor network */
	make_learner(&role, n, ROLE_HIDDEN, outputs);

	/* Copy input names */
	for (i = 0; i < n; i++)
	{
		/* Copy name */
		role.input_name[i] = input_name[i];
	}
}

/*
 * Cached result from eval_game.
 */
typedef struct eval_cache
{
	/* Hash value of game state */
	uint64_t key;

	/* Score to return */
	double score;

	/* Next cache entry in chain */
	struct eval_cache *next;

} eval_cache;

/*
 * Hash table for cached evaluation results.
 */
static eval_cache *eval_hash[65536];

/*
 * Hash table for cached opponent placement results.
 */
static eval_cache *opp_place_hash[65536];

/*
 * Generic hash mixer.
 */
#define mix(a,b,c) \
{ \
	a = a - b; a = a - c; a = a ^ (c >> 43); \
	b = b - c; b = b - a; b = b ^ (a << 9); \
	c = c - a; c = c - b; c = c ^ (b >> 8); \
	a = a - b; a = a - c; a = a ^ (c >> 38); \
	b = b - c; b = b - a; b = b ^ (a << 23); \
	c = c - a; c = c - b; c = c ^ (b >> 5); \
	a = a - b; a = a - c; a = a ^ (c >> 35); \
	b = b - c; b = b - a; b = b ^ (a << 49); \
	c = c - a; c = c - b; c = c ^ (b >> 11); \
	a = a - b; a = a - c; a = a ^ (c >> 12); \
	b = b - c; b = b - a; b = b ^ (a << 18); \
	c = c - a; c = c - b; c = c ^ (b >> 22); \
}

/*
 * Generic hash function.
 */
static uint64_t gen_hash(unsigned char *k, int length)
{
	uint64_t a, b, c, len;

	/* Internal state */
	len = length;
	a = b = 0x9e3779b97f4a7c13LL;
	c = 0;

	/* Most of key */
	while (len > 23)
	{
		a += *(uint64_t *)(&k[0]);
		b += *(uint64_t *)(&k[8]);
		c += *(uint64_t *)(&k[16]);
		mix(a, b, c);
		k += 24;
		len -= 24;
	}

	/* Rest of key */
	c += length;

	switch (len)
	{
		case 23: c += (uint64_t)k[22] << 56;
		case 22: c += (uint64_t)k[21] << 48;
		case 21: c += (uint64_t)k[20] << 40;
		case 20: c += (uint64_t)k[19] << 32;
		case 19: c += (uint64_t)k[18] << 24;
		case 18: c += (uint64_t)k[17] << 16;
		case 17: c += (uint64_t)k[16] << 8;
		case 16: b += (uint64_t)k[15] << 56;
		case 15: b += (uint64_t)k[14] << 48;
		case 14: b += (uint64_t)k[13] << 40;
		case 13: b += (uint64_t)k[12] << 32;
		case 12: b += (uint64_t)k[11] << 24;
		case 11: b += (uint64_t)k[10] << 16;
		case 10: b += (uint64_t)k[9] << 8;
		case 9: b += (uint64_t)k[8];
		case 8: a += (uint64_t)k[7] << 56;
		case 7: a += (uint64_t)k[6] << 48;
		case 6: a += (uint64_t)k[5] << 40;
		case 5: a += (uint64_t)k[4] << 32;
		case 4: a += (uint64_t)k[3] << 24;
		case 3: a += (uint64_t)k[2] << 16;
		case 2: a += (uint64_t)k[1] << 8;
		case 1: a += (uint64_t)k[0];
	}
	mix(a, b, c);

	/* Return result */
	return c;
}


/*
 * Look up a game state in the result cache.
 */
static eval_cache *lookup_eval(game *g, int who)
{
	player *p_ptr;
	card *c_ptr;
	eval_cache *e_ptr;
	uint64_t key;
	unsigned char value[1024];
	int len = 0;
	int i, j;

	/* Loop over cards */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Add location to value */
		value[len++] = (unsigned char)c_ptr->where;

		/* Check for location not in draw or discard pile */
		if (c_ptr->where != WHERE_DECK && c_ptr->where != WHERE_DISCARD)
		{
			/* Add owner to value */
			value[len++] = (unsigned char)c_ptr->owner;
		}

		/* Check for used as good */
		if (c_ptr->where == WHERE_GOOD)
		{
			/* Add card being covered to value */
			value[len++] = (unsigned char)c_ptr->covering;
		}
	}

	/* Loop over players */
	for (i = 0; i < g->num_players; i++)
	{
		/* Get player pointer */
		p_ptr = &g->p[i];

		/* Add victory points and fake card counts to value */
		value[len++] = (unsigned char)p_ptr->vp;
		value[len++] = (unsigned char)p_ptr->prestige;
		value[len++] = (unsigned char)p_ptr->prestige_action_used;
		value[len++] = (unsigned char)p_ptr->drawn_round;
		value[len++] = (unsigned char)p_ptr->fake_hand;
		value[len++] = (unsigned char)p_ptr->fake_discards;
		value[len++] = (unsigned char)p_ptr->skip_develop;
		value[len++] = (unsigned char)p_ptr->skip_settle;

		/* Loop over goals */
		for (j = 0; j < MAX_GOAL; j++)
		{
			/* Skip inactive goals */
			if (!g->goal_active[j]) continue;

			/* Add goal claimed to value */
			value[len++] = (unsigned char)p_ptr->goal_claimed[j];
		}
	}

	/* Add evaluating player to value */
	value[len++] = (unsigned char)who;

	/* Add simulating player to value */
	value[len++] = (unsigned char)g->sim_who;

	/* Add current phase to value */
	value[len++] = (unsigned char)g->cur_action;

	/* Add game over flag to value */
	value[len++] = (unsigned char)g->game_over;

	/* Get key for value */
	key = gen_hash(value, len);

	/* Look for key in hash table */
	for (e_ptr = eval_hash[key & 0xffff]; e_ptr; e_ptr = e_ptr->next)
	{
		/* Check for match */
		if (e_ptr->key == key) break;
	}

	/* Check for no match */
	if (!e_ptr)
	{
		/* Make new entry */
		e_ptr = (eval_cache *)malloc(sizeof(eval_cache));

		/* Set key of new entry */
		e_ptr->key = key;

		/* Clear score */
		e_ptr->score = -1;

		/* Insert into hash table */
		e_ptr->next = eval_hash[key & 0xffff];
		eval_hash[key & 0xffff] = e_ptr;
	}

	/* Return pointer */
	return e_ptr;
}

/*
 * Lookup a score in the opponent placement cache.
 */
static eval_cache *lookup_opp_place(game *g, int who, int opp, int which,
                                    int special)
{
	eval_cache *e_ptr;
	uint64_t key;
	unsigned char value[1024];
	int len = 0;
	int x;

	/* Start at opponent's first card */
	x = g->p[opp].head[WHERE_ACTIVE];

	/* Loop over active cards */
	for ( ; x != -1; x = g->deck[x].next)
	{
		/* Add card index to value */
		value[len++] = (unsigned char)x;
	}

	/* Add evaluating player to value */
	value[len++] = (unsigned char)who;

	/* Add opponent player to value */
	value[len++] = (unsigned char)opp;

	/* Add opponent's actions to value */
	value[len++] = (unsigned char)g->p[opp].action[0];
	value[len++] = (unsigned char)g->p[opp].action[1];

	/* Add proposed card to value */
	value[len++] = (unsigned char)which;

	/* Add current phase to value */
	value[len++] = (unsigned char)g->cur_action;

	/* Add special card used (if any) to value */
	value[len++] = (unsigned char)special;

	/* Get key for value */
	key = gen_hash(value, len);

	/* Look for key in hash table */
	for (e_ptr = opp_place_hash[key & 0xffff]; e_ptr; e_ptr = e_ptr->next)
	{
		/* Check for match */
		if (e_ptr->key == key) break;
	}

	/* Check for no match */
	if (!e_ptr)
	{
		/* Make new entry */
		e_ptr = (eval_cache *)malloc(sizeof(eval_cache));

		/* Set key of new entry */
		e_ptr->key = key;

		/* Clear score */
		e_ptr->score = -1;

		/* Insert into hash table */
		e_ptr->next = opp_place_hash[key & 0xffff];
		opp_place_hash[key & 0xffff] = e_ptr;
	}

	/* Return pointer */
	return e_ptr;
}

/*
 * Delete the entries in the evaluation cache.
 */
static void clear_eval_cache(void)
{
	eval_cache *e_ptr;
	int i;

	/* Loop over each row in hash table */
	for (i = 0; i < 65536; i++)
	{
		/* Delete entries until clear */
		while (eval_hash[i])
		{
			/* Get pointer to first entry */
			e_ptr = eval_hash[i];

			/* Move row to next entry */
			eval_hash[i] = e_ptr->next;

			/* Delete entry */
			free(e_ptr);
		}
	}
}

/*
 * Delete the entries in the opponent placement cache.
 */
static void clear_opp_place_cache(void)
{
	eval_cache *e_ptr;
	int i;

	/* Loop over each row in hash table */
	for (i = 0; i < 65536; i++)
	{
		/* Delete entries until clear */
		while (opp_place_hash[i])
		{
			/* Get pointer to first entry */
			e_ptr = opp_place_hash[i];

			/* Move row to next entry */
			opp_place_hash[i] = e_ptr->next;

			/* Delete entry */
			free(e_ptr);
		}
	}
}

static void dump_eval(void)
{
	int i;

	for (i = 0; i < eval.num_inputs; i++)
	{
		if (eval.input_value[i] != -1)
		{
			printf("%s: %f\n", eval.input_name[i], eval.input_value[i]);
		}
	}
}

/*
 * Set inputs of the evaluation network for the given player.
 *
 * We only use public knowledge in this function.
 */
static int eval_game_player(game *g, int who, int n, int *leader)
{
	player *p_ptr;
	card *c_ptr;
	power *o_ptr;
	int i, x, count, good[6], explore_mix = 0, num_build = 0;
	int pos_military = 0, neg_military = 0;
	int count_six = 0;

	/* Clear good type array */
	for (i = 0; i <= GOOD_ALIEN; i++)
	{
		/* Clear arrays */
		good[i] = 0;
	}

	/* Set player pointer */
	p_ptr = &g->p[who];

	/* Start at first active card */
	x = p_ptr->head[WHERE_ACTIVE];

	/* Loop over our active cards */
	for ( ; x != -1; x = g->deck[x].next)
	{
		/* Get card pointer */
		c_ptr = &g->deck[x];

		/* Set input for active card */
		eval.input_value[n + card_input[c_ptr->d_ptr->index]] = 1;

		/* Loop over card powers */
		for (i = 0; i < c_ptr->d_ptr->num_power; i++)
		{
			/* Get power pointer */
			o_ptr = &c_ptr->d_ptr->powers[i];

			/* Check for Explore power */
			if (o_ptr->phase == PHASE_EXPLORE)
			{
				/* Check for "discard any" power */
				if (o_ptr->code & P1_DISCARD_ANY)
					explore_mix = 1;
			}

			/* Check for Settle power */
			if (o_ptr->phase == PHASE_SETTLE)
			{
				/* Check for basic military power */
				if (o_ptr->code == P3_EXTRA_MILITARY)
				{
					/* Check for positive */
					if (o_ptr->value > 0)
					{
						/* Positive military */
						pos_military = 1;
					}
					else
					{
						/* Negative military */
						neg_military = 1;
					}
				}
			}
		}
	}

	/* Advance input index */
	n += num_c_input;

	/* Clear good count */
	count = 0;

	/* Start at first active card */
	x = p_ptr->head[WHERE_ACTIVE];

	/* Do not check for goods when game is over */
	if (g->game_over) x = -1;

	/* Loop over our active cards */
	for ( ; x != -1; x = g->deck[x].next)
	{
		/* Get card pointer */
		c_ptr = &g->deck[x];

		/* Skip cards without goods */
		if (!c_ptr->num_goods) continue;

		/* Count available goods */
		count += c_ptr->num_goods;

		/* Track type of good */
		good[c_ptr->d_ptr->good_type] = 1;

		/* Set input for card with good */
		eval.input_value[n + good_input[c_ptr->d_ptr->index]] =
			c_ptr->num_goods;
	}

	/* Advance input index */
	n += num_g_input;

	/* Set inputs for goods */
	for (i = 0; i < 6; i++)
	{
		/* Set input if this many goods */
		eval.input_value[n++] = (count > i) ? 1 : -1;
	}

	/* Remember total number of goods */
	leader[LEADER_GOODS] = count;

	/* Loop over good types */
	for (i = GOOD_NOVELTY; i <= GOOD_ALIEN; i++)
	{
		/* Set input if good type available */
		eval.input_value[n++] = good[i] ? 1 : -1;
	}

	/* Get count of cards in hand */
	count = count_player_area(g, who, WHERE_HAND) + p_ptr->fake_hand -
	        p_ptr->fake_discards;

	/* Do not count cards in hand if game over */
	if (g->game_over) count = 0;

	/* Set inputs for cards in hand */
	for (i = 0; i < 12; i++)
	{
		/* Set input if this many cards */
		eval.input_value[n++] = (count > i) ? 1 : -1;
	}

	/* Remember cards in hand */
	leader[LEADER_CARDS] = count;

	/* Set inputs for cards seen this round */
	for (i = 0; i < 15; i++)
	{
		/* Set input if this many cards seen */
		eval.input_value[n++] = (p_ptr->drawn_round > i) ? 1 : -1;
	}

	/* Clear count of developments */
	count = 0;

	/* Start at first active card */
	x = p_ptr->head[WHERE_ACTIVE];

	/* Loop over our active cards */
	for ( ; x != -1; x = g->deck[x].next)
	{
		/* Get card pointer */
		c_ptr = &g->deck[x];

		/* Skip non-developments */
		if (c_ptr->d_ptr->type != TYPE_DEVELOPMENT) continue;

		/* Count card */
		count++;

		/* Check for six-cost development */
		if (c_ptr->d_ptr->cost == 6) count_six++;
	}

	/* Set inputs for number of active developments */
	for (i = 0; i < 10; i++)
	{
		/* Set input if this many cards */
		eval.input_value[n++] = (count > i) ? 1 : -1;
	}

	/* Count number of built cards */
	num_build = count;

	/* Clear count of worlds */
	count = 0;

	/* Start at first active card */
	x = p_ptr->head[WHERE_ACTIVE];

	/* Loop over our active cards */
	for ( ; x != -1; x = g->deck[x].next)
	{
		/* Get card pointer */
		c_ptr = &g->deck[x];

		/* Skip non-worlds */
		if (c_ptr->d_ptr->type != TYPE_WORLD) continue;

		/* Count card */
		count++;
	}

	/* Set inputs for number of active worlds */
	for (i = 0; i < 10; i++)
	{
		/* Set input if this many cards */
		eval.input_value[n++] = (count > i) ? 1 : -1;
	}

	/* Count number of built cards */
	num_build += count;

	/* Set inputs for number of 6-cost developments */
	for (i = 0; i < 5; i++)
	{
		/* Set input if this 6-costs */
		eval.input_value[n++] = (count_six > i) ? 1 : -1;
	}

	/* Remember amount of cards build */
	leader[LEADER_BUILT] = num_build;

	/* Get total military strength */
	count = total_military(g, who);

	/* Set inputs for military strength */
	for (i = 0; i < 10; i++)
	{
		/* Set input if this much strength */
		eval.input_value[n++] = (count > i) ? 1 : -1;
	}

	/* Set input if player has conflicting military strength powers */
	eval.input_value[n++] = (pos_military && neg_military) ? 1 : -1;

	/* Set input if player skipped last Develop phase */
	eval.input_value[n++] = p_ptr->skip_develop ? 1 : -1;

	/* Set input if player skipped last Settle phase */
	eval.input_value[n++] = p_ptr->skip_settle ? 1 : -1;

	/* Set input if player has special Explore power */
	eval.input_value[n++] = explore_mix ? 1 : -1;

	/* Get amount of consumption ability */
	count = consume_ability(g, who, 1);

	/* Set inputs for consumption ability */
	for (i = 0; i < 6; i++)
	{
		/* Set input if this much consumption ability */
		eval.input_value[n++] = (count > i) ? 1 : -1;
	}

	/* Get amount of immediate consumption ability */
	count = consume_ability(g, who, 0);

	/* Do not count any consumption if game over */
	if (g->game_over) count = 0;

	/* Set inputs for immediate consumption */
	for (i = 0; i < 6; i++)
	{
		/* Set input if this much immediate consumption */
		eval.input_value[n++] = (count > i) ? 1 : -1;
	}

	/* Check for goals in expansion */
	if (g->expanded > 0 && g->expanded < 4)
	{
		/* Set inputs for claimed goals */
		for (i = 0; i < MAX_GOAL; i++)
		{
			/* Set input if goal claimed */
			eval.input_value[n++] = p_ptr->goal_claimed[i] ? 1 : -1;
		}
	}

	/* Check for prestige in expansion */
	if (g->expanded == 3)
	{
		/* Set input if player has used prestige/search action */
		eval.input_value[n++] = (p_ptr->prestige_action_used ||
		                         g->game_over) ? 1 : -1;

		/* Set inputs for prestige */
		for (i = 0; i < 15; i++)
		{
			/* Set input if this many prestige earned */
			eval.input_value[n++] = (p_ptr->prestige > i) ? 1 : -1;
		}

		/* Remember amount of prestige */
		leader[LEADER_PRESTIGE] = p_ptr->prestige;
	}

	/* Remember amount of VP */
	leader[LEADER_VP] = p_ptr->end_vp;

	/* Set input if winner */
	eval.input_value[n++] = p_ptr->winner ? 1 : -1;

	/* Return next index to be used */
	return n;
}

/*
 * Add inputs to the evaluation network for amount behind the leader
 * in a category.
 */
static int eval_game_leader(game *g, int who, int n, int leader[][MAX_LEADER],
                            int cat, int num_inputs)
{
	int i, j, max = -1;

	/* Loop over players */
	for (i = 0; i < g->num_players; i++)
	{
		/* Track maximum */
		if (leader[i][cat] > max) max = leader[i][cat];
	}

	/* Start with evaluating player */
	i = who;

	/* Loop over players */
	do
	{
		/* Add inputs */
		for (j = 0; j < num_inputs; j++)
		{
			/* Add input for this much behind leader */
			eval.input_value[n++] =
			                   (leader[i][cat] + j) < max ? 1 : -1;
		}

		/* Advance to next player */
		i = (i + 1) % g->num_players;

	/* Stop loop once gone around */
	} while (i != who);

	/* Return new number of inputs */
	return n;
}

/*
 * Evaluate the given game state from the point of view of the given
 * player.
 */
static double eval_game(game *g, int who)
{
	player *p_ptr;
	card *c_ptr;
	eval_cache *e_ptr;
	int i, x, count, n = 0, hand = 0;
	int build_dev = 0, build_world = 0;
	int max = 0, max_build = 0, clock;
	int leader[MAX_PLAYER][MAX_LEADER];
	double score;

	/* Lookup game state in cached results */
	e_ptr = lookup_eval(g, who);

#ifndef DEBUG
	/* Check for valid result */
	if (e_ptr->score > -1)
	{
		eval_cache_hit++;
		return e_ptr->score;
	}
	else
	{
		eval_cache_miss++;
	}
#endif

	/* Get end-of-game score */
	score_game(g);

	/* Declare winner if game over */
	if (g->game_over) declare_winner(g);

	/* Clear inputs */
	for (i = 0; i < eval.num_inputs; i++) eval.input_value[i] = -1;

	/* Set input for game over */
	eval.input_value[n++] = g->game_over ? 1 : -1;

	/* Set inputs for VP pool size */
	for (i = 0; i < 12; i++)
	{
		/* Set input if this many points (per player) remain */
		eval.input_value[n++] = (g->vp_pool > i * g->num_players) ?
		                         1 : -1;
	}

	/* Loop over players */
	for (i = 0; i < g->num_players; i++)
	{
		/* Get player pointer */
		p_ptr = &g->p[i];

		/* Count number of active cards */
		count = count_player_area(g, i, WHERE_ACTIVE);

		/* Track most cards played */
		if (count > max_build) max_build = count;
	}

	/* Set inputs for cards played */
	for (i = 0; i < 12; i++)
	{
		/* Set input if someone has this many cards played */
		eval.input_value[n++] = (max_build > i) ? 1 : -1;
	}

	/* Compute "clock" of time remaining from cards played */
	clock = 12 - max_build;

	/* Check for VP pool depletion */
	if (g->vp_pool / g->num_players < clock)
	{
		/* Set clock to VPs left instead */
		clock = g->vp_pool / g->num_players;
	}

	/* XXX Check for long-running game */
	if (g->round > 20) clock = 0;

	/* Set inputs for time remaining */
	for (i = 0; i < 12; i++)
	{
		/* Set input if this much time remains */
		eval.input_value[n++] = (clock > i) ? 1 : -1;
	}

	/* Check for goals in expansion */
	if (g->expanded > 0 && g->expanded < 4)
	{
		/* Set inputs for active goals */
		for (i = 0; i < MAX_GOAL; i++)
		{
			/* Set input if this goal is active for this game */
			eval.input_value[n++] = g->goal_active[i] ? 1 : -1;
		}

		/* Set inputs for available goals */
		for (i = 0; i < MAX_GOAL; i++)
		{
			/* Set input if this goal is still available */
			eval.input_value[n++] = g->goal_avail[i] ? 1 : -1;
		}
	}

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Start at first card in hand */
	x = p_ptr->head[WHERE_HAND];

	/* Skip cards in hand if game over */
	if (g->game_over) x = -1;

	/* Loop over cards in hand */
	for ( ; x != -1; x = g->deck[x].next)
	{
		/* Get card pointer */
		c_ptr = &g->deck[x];

		/* Count cards in hand */
		hand++;

		/* Skip cards in hand if evaluating for opponent */
		if (g->simulation && g->sim_who != who) continue;

		/* Set input for card in hand */
		eval.input_value[n + card_input[c_ptr->d_ptr->index]] = 1;
	}

	/* Start at first saved card */
	x = p_ptr->head[WHERE_SAVED];

	/* Skip saved cards if game over */
	if (g->game_over) x = -1;

	/* Skip saved cards if not evaluating from our point of view */
	if (g->simulation && g->sim_who != who) x = -1;

	/* Loop over saved cards */
	for ( ; x != -1; x = g->deck[x].next)
	{
		/* Get card pointer */
		c_ptr = &g->deck[x];

		/* Set input for saved card */
		eval.input_value[n + card_input[c_ptr->d_ptr->index]] = 0.5;
	}

	/* Add simulated drawn cards to handsize */
	hand += g->game_over ? 0 : p_ptr->fake_hand - p_ptr->fake_discards;

	/* Advance input index */
	n += num_c_input;

	/* Start at first card in hand */
	x = p_ptr->head[WHERE_HAND];

	/* Skip cards in hand if game over */
	if (g->game_over) x = -1;

	/* Skip cards in hand if not evaluating from our point of view */
	if (g->simulation && g->sim_who != who) x = -1;

	/* Compute maximum cost of developments */
	max = hand + develop_discount(g, who) - 1;

	/* Loop over cards in hand */
	for ( ; x != -1; x = g->deck[x].next)
	{
		/* Get card pointer */
		c_ptr = &g->deck[x];

		/* Check for development */
		if (c_ptr->d_ptr->type == TYPE_DEVELOPMENT)
		{
			/* Check cost against hand size */
			if (c_ptr->d_ptr->cost > max) continue;

			/* One more buildable development */
			build_dev++;
		}
		else
		{
			/* Check for illegal world */
			if (!settle_legal(g, who, x, 0, 0, 0, 0)) continue;

			/* One more buildable world */
			build_world++;
		}
	}

	/* Set inputs for buildable developments in hand */
	for (i = 0; i < 5; i++)
	{
		/* Set input if this many developments available */
		eval.input_value[n++] = (build_dev > i) ? 1 : -1;
	}

	/* Set inputs for buildable worlds in hand */
	for (i = 0; i < 5; i++)
	{
		/* Set input if this many worlds available */
		eval.input_value[n++] = (build_world > i) ? 1 : -1;
	}

	/* Set public inputs for given player */
	n = eval_game_player(g, who, n, leader[who]);

	/* Go to next player */
	i = (who + 1) % g->num_players;

	/* Loop over other players */
	while (i != who)
	{
		/* Set public inputs for other player */
		n = eval_game_player(g, i, n, leader[i]);

		/* Go to next player */
		i = (i + 1) % g->num_players;
	}

	/* Add inputs for amount behind VP leader */
	n = eval_game_leader(g, who, n, leader, LEADER_VP, 20);

	/* Check for third expansion */
	if (g->expanded == 3)
	{
		/* Add inputs for amount behind prestige leader */
		n = eval_game_leader(g, who, n, leader, LEADER_PRESTIGE, 5);
	}

	/* Add inputs for amount behind building leader */
	n = eval_game_leader(g, who, n, leader, LEADER_BUILT, 5);

	/* Add inputs for amount behind cards in hand leader */
	n = eval_game_leader(g, who, n, leader, LEADER_CARDS, 10);

	/* Add inputs for amount behind available goods leader */
	n = eval_game_leader(g, who, n, leader, LEADER_GOODS, 5);

	/* Sanity check input size */
	if (n != eval.num_inputs)
	{
		/* Error */
		printf("Incorrect number of eval inputs %d %d\n", n,
		       eval.num_inputs);
		abort();
	}

	/* Compute network */
	compute_net(&eval);

	num_computes++;

#if 0
	insert_inputs();
#endif

	/* Compute game score */
	score = eval.win_prob[0] + p_ptr->end_vp * 0.001 +
	               hand * 0.0002 + (p_ptr->winner ? 0.2 : 0) + 0.1 -
		       (g->game_over ? 0.1 : 0);

#ifdef DEBUG
	if (e_ptr->score != -1 && fabs(e_ptr->score - score) > 0.0001)
	{
		printf("Bad result in eval cache!\n");
	}
#endif

	/* Save result in cache */
	e_ptr->score = score;

	/* Return score */
	return e_ptr->score;
}

/*
 * Perform a training iteration on the eval network.
 */
static void perform_training(game *g, int who, double *desired)
{
	double target[MAX_PLAYER];
	double lambda = 1.0;
	int i;

	/* Clear cached results of eval network */
	clear_eval_cache();

	/* Get current state */
	eval_game(g, who);

	/* Store current inputs */
	store_net(&eval, who);

	/* Check for passed in results */
	if (desired)
	{
		/* Copy results to target array */
		for (i = 0; i < g->num_players; i++) target[i] = desired[i];

		/* Train current inputs with desired outputs */
		train_net(&eval, 1.0, target);

		/* Reduce lambda for further training */
		lambda *= 0.7;
	}
	else
	{
		/* Use current results to train past input sets */
		for (i = 0; i < g->num_players; i++)
		{
			/* Copy player's predicted win probability */
			target[i] = eval.win_prob[i];
		}
	}

	/* Loop over past input sets (starting with most recent) */
	for (i = eval.num_past - 2; i >= 0; i--)
	{
		/* Skip input sets that do not belong to us */
		if (eval.past_input_player[i] != who) continue;

		/* Copy past inputs to network */
		memcpy(eval.input_value, eval.past_input[i],
		       sizeof(double) * (eval.num_inputs + 1));

		/* Compute network */
		compute_net(&eval);

		/* Train */
		train_net(&eval, lambda, target);

		/* Reduce training amount as we go back in time */
		lambda *= 0.7;
	}

	/* Apply accumulated training */
	apply_training(&eval);
}

/*
 * Add inputs to the role network about a player's public information.
 */
static int predict_action_player(game *g, int who, int n, int *leader)
{
	player *p_ptr;
	card *c_ptr;
	power *o_ptr;
	int i, x, good[6], count, count_dev, count_world, explore_mix = 0;

	/* Clear good types */
	for (i = 0; i <= GOOD_ALIEN; i++) good[i] = 0;

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Assume no active cards */
	count = count_dev = count_world = 0;

	/* Start at first active card */
	x = p_ptr->head[WHERE_ACTIVE];

	/* Loop over player's active cards */
	for ( ; x != -1; x = g->deck[x].next)
	{
		/* Get card pointer */
		c_ptr = &g->deck[x];

		/* Set input for active card */
		role.input_value[n + card_input[c_ptr->d_ptr->index]] = 1;

		/* Count active developments */
		if (c_ptr->d_ptr->type == TYPE_DEVELOPMENT)
		{
			/* Count active developments */
			count_dev++;
		}
		else
		{
			/* Count active worlds */
			count_world++;
		}

		/* Loop over powers on card */
		for (i = 0; i < c_ptr->d_ptr->num_power; i++)
		{
			/* Get power pointer */
			o_ptr = &c_ptr->d_ptr->powers[i];

			/* Skip non-Explore powers */
			if (o_ptr->phase != PHASE_EXPLORE) continue;

			/* Check for "discard any" power */
			if (o_ptr->code & P1_DISCARD_ANY) explore_mix = 1;
		}
	}

	/* Advance input index */
	n += num_c_input;

	/* Set inputs for number of active developments */
	for (i = 0; i < 10; i++)
	{
		/* Set input if this many cards */
		role.input_value[n++] = (count_dev > i) ? 1 : -1;
	}

	/* Set inputs for number of active worlds */
	for (i = 0; i < 10; i++)
	{
		/* Set input if this many cards */
		role.input_value[n++] = (count_world > i) ? 1 : -1;
	}

	/* Remember number of built cards */
	leader[LEADER_BUILT] = count_dev + count_world;

	/* Assume no goods */
	count = 0;

	/* Start at first active card */
	x = p_ptr->head[WHERE_ACTIVE];

	/* Loop over player's active cards */
	for ( ; x != -1; x = g->deck[x].next)
	{
		/* Get card pointer */
		c_ptr = &g->deck[x];

		/* Skip cards without goods */
		if (!c_ptr->num_goods) continue;

		/* Count goods */
		count++;

		/* Track good type */
		good[c_ptr->d_ptr->good_type] = 1;

		/* Set input for card with good */
		role.input_value[n + good_input[c_ptr->d_ptr->index]] = 1;
	}

	/* Advance input index */
	n += num_g_input;

	/* Set inputs for available goods */
	for (i = 0; i < 6; i++)
	{
		/* Set input if this many goods */
		role.input_value[n++] = (count > i) ? 1 : -1;
	}

	/* Remember number of goods */
	leader[LEADER_GOODS] = count;

	/* Set inputs for good types */
	for (i = GOOD_NOVELTY; i <= GOOD_ALIEN; i++)
	{
		/* Set input */
		role.input_value[n++] = good[i] ? 1 : -1;
	}

	/* Get count of cards in hand */
	count = count_player_area(g, who, WHERE_HAND);

	/* Set inputs for cards in hand */
	for (i = 0; i < 12; i++)
	{
		/* Set input if this many cards */
		role.input_value[n++] = (count > i) ? 1 : -1;
	}

	/* Remember number of cards in hand */
	leader[LEADER_CARDS] = count;

	/* Set inputs for cards seen last round */
	for (i = 0; i < 15; i++)
	{
		/* Set input if this many cards seen */
		role.input_value[n++] = (p_ptr->drawn_round > i) ? 1 : -1;
	}

	/* Get military strength */
	count = total_military(g, who);

	/* Set inputs for military strength */
	for (i = 0; i < 10; i++)
	{
		/* Set input if this much strength */
		role.input_value[n++] = (count > i) ? 1 : -1;
	}

	/* Set input if player skipped last Develop phase */
	role.input_value[n++] = p_ptr->skip_develop ? 1 : -1;

	/* Set input if player skipped last Settle phase */
	role.input_value[n++] = p_ptr->skip_settle ? 1 : -1;

	/* Set input for special Explore power */
	role.input_value[n++] = explore_mix ? 1 : -1;

	/* Get consume ability */
	count = consume_ability(g, who, 1);

	/* Set inputs for consume ability */
	for (i = 0; i < 6; i++)
	{
		/* Set input if this much consume ability */
		role.input_value[n++] = (count > i) ? 1 : -1;
	}

	/* Get immediate consume ability */
	count = consume_ability(g, who, 0);

	/* Set inputs for immediate consumption */
	for (i = 0; i < 6; i++)
	{
		/* Set input if this much immediate consumption */
		role.input_value[n++] = (count > i) ? 1 : -1;
	}

	/* Check for goals in expansion */
	if (g->expanded > 0 && g->expanded < 4)
	{
		/* Set inputs for claimed goals */
		for (i = 0; i < MAX_GOAL; i++)
		{
			/* Set input if goal claimed */
			role.input_value[n++] = p_ptr->goal_claimed[i] ? 1 : -1;
		}
	}

	/* Check for prestige in expansion */
	if (g->expanded == 3)
	{
		/* Set input if player has used prestige/search action */
		role.input_value[n++] = p_ptr->prestige_action_used ? 1 : -1;

		/* Set inputs for prestige */
		for (i = 0; i < 15; i++)
		{
			/* Set input if this much prestige */
			role.input_value[n++] = (p_ptr->prestige > i) ? 1 : -1;
		}

		/* Remember amount of prestige */
		leader[LEADER_PRESTIGE] = p_ptr->prestige;
	}

	/* Set inputs for previous turn actions */
	for (i = 0; i < MAX_ACTION; i++)
	{
		/* Set input if action chosen last turn */
		role.input_value[n++] = (p_ptr->prev_action[0] == i ||
		                         p_ptr->prev_action[1] == i) ? 1 : -1;
	}

	/* Remember amount of VP */
	leader[LEADER_VP] = p_ptr->end_vp;

	/* Return next input index */
	return n;
}

/*
 * Add inputs to the role predictor network for amount behind the leader
 * in a category.
 */
static int predict_action_leader(game *g, int who, int n,
                                 int leader[][MAX_LEADER], int cat,
                                 int num_inputs)
{
	int i, j, max = -1;

	/* Loop over players */
	for (i = 0; i < g->num_players; i++)
	{
		/* Track maximum */
		if (leader[i][cat] > max) max = leader[i][cat];
	}

	/* Start with predicting player */
	i = who;

	/* Loop over players */
	do
	{
		/* Add inputs */
		for (j = 0; j < num_inputs; j++)
		{
			/* Add input for this much behind leader */
			role.input_value[n++] =
			                   (leader[i][cat] + j) < max ? 1 : -1;
		}

		/* Advance to next player */
		i = (i + 1) % g->num_players;

	/* Stop loop once gone around */
	} while (i != who);

	/* Return new number of inputs */
	return n;
}

/*
 * Evaluate chances that player will choose each action.
 */
static void predict_action(game *g, int who, double prob[MAX_ACTION],
                           int sim_who)
{
	game sim;
	double act_scores[ROLE_OUT_ADV_EXP3], sum = 0;
	int i, j, n = 0, count, clock, max;
	int leader[MAX_PLAYER][MAX_LEADER];

	/* Clear inputs of role network */
	for (i = 0; i < role.num_inputs; i++) role.input_value[i] = -1;

	/* Score game */
	score_game(g);

	/* Start with given player */
	i = who;

	/* Add inputs for player being examined */
	n = predict_action_player(g, i, n, leader[i]);

	/* Loop over other players */
	while (1)
	{
		/* Go to next player */
		i = (i + 1) % g->num_players;

		/* Stop once we get back around to first player */
		if (i == who) break;

		/* Add inputs for this player */
		n = predict_action_player(g, i, n, leader[i]);
	}

	/* Add inputs for amount behind VP leader */
	n = predict_action_leader(g, who, n, leader, LEADER_VP, 20);

	/* Check for third expansion */
	if (g->expanded == 3)
	{
		/* Add inputs for amount behind prestige leader */
		n = predict_action_leader(g, who, n, leader,
		                          LEADER_PRESTIGE, 5);
	}

	/* Add inputs for amount behind building leader */
	n = predict_action_leader(g, who, n, leader, LEADER_BUILT, 5);

	/* Add inputs for amount behind VP leader */
	n = predict_action_leader(g, who, n, leader, LEADER_CARDS, 10);

	/* Add inputs for amount behind VP leader */
	n = predict_action_leader(g, who, n, leader, LEADER_GOODS, 5);

	/* Set inputs for VP pool size */
	for (i = 0; i < 12; i++)
	{
		/* Set input if this many points (per player) remain */
		role.input_value[n++] = (g->vp_pool > i * g->num_players) ?
		                         1 : -1;
	}

	/* Clear max count of cards played */
	max = 0;

	/* Loop over players */
	for (i = 0; i < g->num_players; i++)
	{
		/* Count number of active cards */
		count = count_player_area(g, i, WHERE_ACTIVE);

		/* Track most cards played */
		if (count > max) max = count;
	}

	/* Set inputs for cards played */
	for (i = 0; i < 12; i++)
	{
		/* Set input if someone has this many cards played */
		role.input_value[n++] = (max > i) ? 1 : -1;
	}

	/* Compute "clock" of time remaining from cards played */
	clock = 12 - max;

	/* Check for VP pool depletion */
	if (g->vp_pool / g->num_players < clock)
	{
		/* Set clock to VPs left instead */
		clock = g->vp_pool / g->num_players;
	}

	/* Set inputs for time remaining */
	for (i = 0; i < 12; i++)
	{
		/* Set input if this much time remains */
		role.input_value[n++] = (clock > i) ? 1 : -1;
	}

	/* Check for goals in expansion */
	if (g->expanded > 0 && g->expanded < 4)
	{
		/* Set inputs for active goals */
		for (i = 0; i < MAX_GOAL; i++)
		{
			/* Set input if this goal is active for this game */
			role.input_value[n++] = g->goal_active[i] ? 1 : -1;
		}

		/* Set inputs for available goals */
		for (i = 0; i < MAX_GOAL; i++)
		{
			/* Set input if this goal is still available */
			role.input_value[n++] = g->goal_avail[i] ? 1 : -1;
		}
	}

	/* Loop over possible actions */
	for (i = 0; i < role.num_output; i++)
	{
		/* Simulate game */
		simulate_game(&sim, g, sim_who);

		/* Clear all player actions */
		for (j = 0; j < g->num_players; j++)
		{
			/* Clear action */
			sim.p[j].action[0] = sim.p[j].action[1] = -1;
		}

		/* Clear selected phases */
		for (j = 0; j < MAX_ACTION; j++)
		{
			/* Clear action */
			sim.action_selected[j] = 0;
		}

		/* Check for advanced game */
		if (g->advanced)
		{
			/* Set actions */
			sim.p[who].action[0] = adv_combo[i][0];
			sim.p[who].action[1] = adv_combo[i][1];
		}
		else
		{
			/* Set action */
			sim.p[who].action[0] = role_out[i];
		}

		/* Note actions */
		note_actions(&sim);

		/* Complete turn with just this action */
		complete_turn(&sim, COMPLETE_ROUND);

		/* Get score */
		act_scores[i] = eval_game(&sim, who);

		/* Add to probability sum */
		sum += exp(20 * act_scores[i]);
	}

	/* Loop over possible actions */
	for (i = 0; i < role.num_output; i++)
	{
		/* Add input for raw action score */
		role.input_value[n++] = exp(20 * act_scores[i]) / sum;
	}

	/* Sanity check role inputs */
	if (n != role.num_inputs)
	{
		/* Error */
		printf("Incorrect number of role inputs %d %d\n", n,
		       role.num_inputs);
		abort();
	}

	/* Compute role choice probabilities */
	compute_net(&role);

#if 0
	printf("%d %d\n", g->round, who);
	for (i = 0; i < role.num_inputs + 1; i++)
	{
		printf("%f\n", role.input_value[i]);
	}
	printf("\n");
	for (i = 0; i < role.num_output; i++)
	{
		printf("%f\n", role.win_prob[i]);
	}
#endif

	/* Copy scores */
	for (i = 0; i < role.num_output; i++)
	{
		/* Copy scores for action */
		prob[i] = role.win_prob[i];
	}
}

#ifdef DEBUG
static void dump_game(game *g, game *sim)
{
	int i, j;

	score_game(sim);

	for (i = 0; i < g->num_players; i++)
	{
		printf("Player %d:\n", i);
		printf("%d cards active, %d cards seen, %d cards in hand, %d cards discarded, %d goods\n", count_player_area(sim, i, WHERE_ACTIVE), sim->p[i].drawn_round, count_player_area(sim, i, WHERE_HAND) + sim->p[i].fake_hand - sim->p[i].fake_discards, sim->p[i].fake_discards, count_player_area(sim, i, WHERE_GOOD));
		printf("%d VP, %d prestige, %d prestige action used\n", sim->p[i].end_vp, sim->p[i].prestige, sim->p[i].prestige_action_used);
		printf("Skipped develop: %d, Skipped settle: %d\n", sim->p[i].skip_develop, sim->p[i].skip_settle);
		for (j = 0; j < g->deck_size; j++)
		{
			if (sim->deck[j].owner == i && sim->deck[j].where == WHERE_ACTIVE && g->deck[j].where != WHERE_ACTIVE)
			{
				printf("New active: %s\n", sim->deck[j].d_ptr->name);
			}
		}
	}
	printf("--\n");
}
#endif

/*
 * Structure holding a score with associated sample cards.
 */
struct sample_score
{
	/* Entry is valid */
	int valid;

	/* Number of cards drawn */
	int drawn;

	/* Number of cards kept */
	int keep;

	/* Player gets to discard any from hand */
	int discard_any;

	/* Score for this sample */
	double score;

	/* Cards drawn or placed */
	int list[MAX_DECK];

	/* Cards discarded */
	int discards[MAX_DECK];
};

/*
 * Compare two scores for explored cards.
 */
static int cmp_sample_score(const void *p1, const void *p2)
{
	struct sample_score *s1 = (struct sample_score *)p1;
	struct sample_score *s2 = (struct sample_score *)p2;
	int i;

	/* Compare scores */
	if (s1->score > s2->score + 0.000001) return 1;
	if (s1->score < s2->score - 0.000001) return -1;

	for (i = 0; i < s1->drawn; i++)
	{
		if (s1->list[i] > s2->list[i]) return 1;
		if (s1->list[i] < s2->list[i]) return -1;
	}
	for (i = 0; i < s1->drawn - s1->keep; i++)
	{
		if (s1->discards[i] > s2->discards[i]) return 1;
		if (s1->discards[i] < s2->discards[i]) return -1;
	}
	return 0;
}

/*
 * Compare two scores for placed cards.
 */
static int cmp_place_score(const void *p1, const void *p2)
{
	struct sample_score *s1 = (struct sample_score *)p1;
	struct sample_score *s2 = (struct sample_score *)p2;

	/* Compare scores */
	if (s1->score > s2->score) return 1;
	if (s1->score < s2->score) return -1;
	return 0;
}

/*
 * Claim a card for ourself.
 *
 * The card may be in an opponent's hand or used as a good.  If so, find
 * a card to replace it.
 *
 * XXX This entire function is nasty.
 */
static int claim_card(game *g, int who, int which)
{
	card *c_ptr;
	int replace;

	/* Get card pointer */
	c_ptr = &g->deck[which];

	/* Check for card already claimed by player */
	if (c_ptr->owner == who && c_ptr->where == WHERE_HAND)
	{
		/* Do nothing */
		return 0;
	}

	/* Check for card in use */
	if (c_ptr->owner != -1)
	{
		/* Get replacement card */
		replace = first_draw(g);

		/* Check for failure to draw */
		if (replace == -1) return 0;

		/* Check for card used as good */
		if (c_ptr->where == WHERE_GOOD)
		{
			/* Mark replacement with covered card */
			g->deck[replace].covering = c_ptr->covering;
		}

		/* Replace claimed card */
		move_card(g, replace, c_ptr->owner, c_ptr->where);
	}

	/* Put card in hand */
	move_card(g, which, who, WHERE_HAND);

	/* Assume old location was draw deck */
	move_start(g, which, -1, WHERE_DECK);

	/* Owner knows location of card */
	c_ptr->misc &= ~MISC_KNOWN_MASK;
	c_ptr->misc |= (1 << who);

	/* Card moved */
	return 1;
}

/*
 * Number of explore samples to keep.
 */
#define MAX_EXPLORE_SAMPLE 10

/*
 * Explore samples we've seen this turn.
 */
static struct sample_score explore_seen[MAX_EXPLORE_SAMPLE];

/*
 * Clear sample results.
 */
static void ai_sample_clear(void)
{
	int i;

	/* Clear Explore samples */
	for (i = 0; i < MAX_EXPLORE_SAMPLE; i++)
	{
		/* Mark invalid */
		explore_seen[i].valid = 0;
	}
}

/*
 * Prepare discard list.
 *
 * We check our cards in hand, and create a sorted list of most
 * "discardable" cards.  When evaluating a game state, we will assume
 * that these cards are no longer in hand if we have discarded more
 * cards than we have drawn.
 */
static void ai_prepare_discard(game *g, int who)
{
	game sim;
	player *p_ptr;
	int x, n = 0;

	/* Get our player pointer */
	p_ptr = &g->p[who];

	/* Start at first card in hand */
	x = p_ptr->head[WHERE_HAND];

	/* Loop over cards in hand */
	for ( ; x != -1; x = g->deck[x].next)
	{
		/* Add card to list */
		discard_list[who][n].which = x;

		/* Simulate game */
		simulate_game(&sim, g, who);

		/* Discard card */
		move_card(&sim, x, -1, WHERE_DISCARD);

		/* Evaluate game */
		discard_list[who][n].score = eval_game(&sim, who);

		/* One more card in list */
		n++;
	}

	/* Sort quick discard list */
	qsort(discard_list[who], n, sizeof(quick_discard), cmp_quick_discard);

	/* Add dummy entry to end */
	discard_list[who][n].which = -1;
}

/*
 * Fill the advanced action combinations table.
 */
static void fill_adv_combo(void)
{
	int a1, a2, n = 0;

	/* Loop over first actions */
	for (a1 = ACT_EXPLORE_5_0; a1 <= ACT_PRODUCE; a1++)
	{
		/* Skip second Develop/Settle */
		if (a1 == ACT_DEVELOP2 || a1 == ACT_SETTLE2) continue;

		/* Loop over second actions */
		for (a2 = a1 + 1; a2 <= ACT_PRODUCE; a2++)
		{
			/* Skip illegal second actions */
			if (a2 == ACT_DEVELOP2 && a1 != ACT_DEVELOP) continue;
			if (a2 == ACT_SETTLE2 && a1 != ACT_SETTLE) continue;

			/* Add actions to table */
			adv_combo[n][0] = a1;
			adv_combo[n][1] = a2;
			n++;
		}
	}

	/* Add third expansion search actions to table */
	a1 = ACT_SEARCH;

	/* Loop over second actions */
	for (a2 = ACT_EXPLORE_5_0; a2 <= ACT_PRODUCE; a2++)
	{
		/* Skip second Develop/Settle */
		if (a2 == ACT_DEVELOP2 || a2 == ACT_SETTLE2) continue;

		/* Add actions to table */
		adv_combo[n][0] = a1;
		adv_combo[n][1] = a2;
		n++;
	}

	/* Loop over first actions */
	for (a1 = ACT_EXPLORE_5_0; a1 <= ACT_PRODUCE; a1++)
	{
		/* Skip second Develop/Settle */
		if (a1 == ACT_DEVELOP2 || a1 == ACT_SETTLE2) continue;

		/* Loop over second actions */
		for (a2 = a1 + 1; a2 <= ACT_PRODUCE; a2++)
		{
			/* Skip illegal second actions */
			if (a2 == ACT_DEVELOP2 && a1 != ACT_DEVELOP) continue;
			if (a2 == ACT_SETTLE2 && a1 != ACT_SETTLE) continue;

			/* Add prestige actions to table */
			adv_combo[n][0] = a1 | ACT_PRESTIGE;
			adv_combo[n][1] = a2;
			n++;

			/* Add prestige actions to table */
			adv_combo[n][0] = a1;
			adv_combo[n][1] = a2 | ACT_PRESTIGE;
			n++;
		}
	}
}

/*
 * Structure to hold a player's predicted action choice, and probability
 * of picking that action.
 */
typedef struct action_prob
{
	/* Probability that this action will be chosen */
	double prob;

	/* Action chosen */
	int choice;

} action_prob;

/*
 * Compare two probabilities of action choices.
 */
static int cmp_action_prob(const void *p1, const void *p2)
{
	struct action_prob *a1 = (struct action_prob *)p1;
	struct action_prob *a2 = (struct action_prob *)p2;

	/* Compare scores */
	if (a1->prob < a2->prob) return 1;
	if (a1->prob > a2->prob) return -1;
	return 0;
}

/*
 * Helper function for ai_choose_action_advanced(), below.
 */
static void ai_choose_action_advanced_aux(game *g, int who, int oa,
                                          double scores[],
                                          double prob, double prob_used,
                                          int one, int force_act)
{
	game sim1, sim2;
	int act, i, n = 0;
	double score;
	action_prob action_order[ROLE_OUT_ADV_EXP3];
	int opp;
#ifdef DEBUG
	int old_computes;
#endif

	/* Simulate game */
	simulate_game(&sim1, g, who);

	/* Get opponent index */
	opp = !who;

	/* Clear selected actions */
	for (act = 0; act <= ACT_PRODUCE; act++) sim1.action_selected[act] = 0;

	/* Set opponent's actions */
	sim1.p[opp].action[0] = adv_combo[oa][0];
	sim1.p[opp].action[1] = adv_combo[oa][1];

	/* Loop over our choices for actions */
	for (act = 0; act < role.num_output; act++)
	{
		/* Check for search action already used */
		if (adv_combo[act][0] == ACT_SEARCH &&
		    g->p[who].prestige_action_used) continue;

		/* Check for prestige action */
		if ((adv_combo[act][0] & ACT_PRESTIGE) ||
		    (adv_combo[act][1] & ACT_PRESTIGE))
		{
			/* Check for no prestige or action used */
			if (!g->p[who].prestige ||
			     g->p[who].prestige_action_used) continue;
		}

		/* Check for selecting only second action */
		if (one == 2 && adv_combo[act][0] != force_act &&
		                adv_combo[act][1] != force_act) continue;

		/* Copy current score to action order table */
		action_order[n].prob = scores[act];
		action_order[n++].choice = act;
	}

	/* Sort list of action choices */
	qsort(action_order, n, sizeof(action_prob), cmp_action_prob);

	/* Loop over our choices for actions */
	for (i = 0; i < n; i++)
	{
		/* Get action choice */
		act = action_order[i].choice;

		/* Check for enough choices checked */
		if ((1.0 * i / n) > (1.0 - prob_used)) continue;

		/* Simulate game */
		simulate_game(&sim2, &sim1, who);

		/* Set our actions */
		sim2.p[who].action[0] = adv_combo[act][0];
		sim2.p[who].action[1] = adv_combo[act][1];

		/* Note actions */
		note_actions(&sim2);

		/* Start at beginning of turn */
		sim2.cur_action = ACT_ROUND_START;

#ifdef DEBUG
		old_computes = num_computes;
#endif

		/* Complete turn */
		complete_turn(&sim2, COMPLETE_ROUND);

		/* Evaluate state after turn */
		score = eval_game(&sim2, who);

#ifdef DEBUG
#if 0
		printf("Trying %s/%s:\nactive %d, goods %d, hand %d, VP %d: score %f\n", action_name[a1], action_name[a2], count_player_area(&sim2, who, WHERE_ACTIVE), count_player_area(&sim2, who, WHERE_GOOD), count_player_area(&sim2, who, WHERE_HAND) + sim2.p[who].fake_hand - sim2.p[who].fake_discards, sim2.p[who].end_vp, score);
		dump_active(&sim2, who);
#endif
		printf("Trying %s/%s: %d (%f)\n", action_name(adv_combo[act][0]), action_name(adv_combo[act][1]), num_computes - old_computes, score);
#endif

		/* Add score to actions */
		scores[act] += score * prob;
	}
}

/*
 * Choose actions in advanced game.
 *
 * Depending on the "one" parameter, we need to choose one or both actions:
 *
 *  0 - choose both
 *  1 - choose first action
 *  2 - choose second action (and opponent's actions are known)
 */
static void ai_choose_action_advanced(game *g, int who, int action[2], int one)
{
	double scores[ROLE_OUT_ADV_EXP3], b_s = -1, b_p, prob;
	double act_scores[ROLE_OUT_EXP3];
	double used = 0;
	double choice_prob[ROLE_OUT_ADV_EXP3];
	double desired[ROLE_OUT_ADV_EXP3], sum = 0.0;
	action_prob action_order[ROLE_OUT_ADV_EXP3];
	int opp;
	int b_a = -1, b_i;
	int i, j, n = 0, oa, act;
#ifdef DEBUG
	int taken = 0;
#endif

#ifdef DEBUG
	printf("--- Player %d choosing actions\n", who);
	printf("--- Player %d hand\n", who);
	dump_hand(g, who);
#endif

	/* Get opponent index */
	opp = !who;

	/* Predict opponent's actions */
	predict_action(g, opp, choice_prob, who);

	/* Check for opponent's actions known */
	if (one == 2)
	{
		/* Loop over choices */
		for (act = 0; act < role.num_output; act++)
		{
			/* Check for match with opponent's selection */
			if (adv_combo[act][0] == g->p[opp].action[0] &&
			    adv_combo[act][1] == g->p[opp].action[1])
			{
				/* Set probability to 1 */
				choice_prob[act] = 1;
			}
			else
			{
				/* Clear probability */
				choice_prob[act] = 0;
			}
		}
	}

	/* Loop over choices */
	for (act = 0; act < role.num_output; act++)
	{
		/* Check for search action already used */
		if (adv_combo[act][0] == ACT_SEARCH &&
		    g->p[opp].prestige_action_used) continue;

		/* Check for prestige action */
		if ((adv_combo[act][0] & ACT_PRESTIGE) ||
		    (adv_combo[act][1] & ACT_PRESTIGE))
		{
			/* Check for no prestige or action used */
			if (!g->p[opp].prestige ||
			     g->p[opp].prestige_action_used) continue;
		}

		/* Copy choice to action order table */
		action_order[n].prob = choice_prob[act];
		action_order[n++].choice = act;

		/* Compute amount of probability space consumed */
		used += choice_prob[act];
	}

	/* Loop over actions */
	for (act = 0; act < n; act++)
	{
		/* Normalize action probability */
		action_order[act].prob /= used;
	}

	/* Sort action order by probability */
	qsort(action_order, n, sizeof(action_prob), cmp_action_prob);

#ifdef DEBUG
	printf("----- Player %d probability\n", opp);
	for (act = 0; act < MAX_ACTION; act++)
	{
		printf("%.2f ", choice_prob[act]);
	}
	printf("\n");
#endif

	/* Clear scores array */
	for (act = 0; act < role.num_output; act++)
	{
		/* Clear this score */
		scores[act] = 0.0;
	}

	/* Clear used */
	used = 0;

	/* Loop over opponent's actions */
	for (act = 0; act < role.num_output; act++)
	{
		/* Compute probability of this combination */
		prob = action_order[act].prob;

		/* Get opponent action combination */
		oa = action_order[act].choice;

#ifdef DEBUG
		taken++;
		printf("Investigating opponent %s/%s (prob %f)\n", action_name(adv_combo[oa][0]), action_name(adv_combo[oa][1]), prob);
#endif

		/* Check our action scores against this combo */
		ai_choose_action_advanced_aux(g, who, oa, scores, prob, used,
					      one, action[0]);

		/* Track used amount of probability space */
		used += prob;

		/* Check for enough probability space searched */
		if (used > 0.8) break;
	}

	/* Loop over our action choices */
	for (act = 0; act < role.num_output; act++)
	{
#ifdef DEBUG
		printf("Score %d: %f\n", act, scores[act]);
#endif

		/* Check for better score */
		if (scores[act] > b_s)
		{
			/* Track best score */
			b_s = scores[act];

			/* Track best action combination */
			b_a = act;
		}
	}

	/* Check for needing only one action so far */
	if (one == 1)
	{
		/* Clear individual action scores */
		for (i = 0; i < ROLE_OUT_EXP3; i++) act_scores[i] = 0.0;

		/* Loop over scores */
		for (i = 0; i < role.num_output; i++)
		{
			/* Add score to individual actions */
			for (j = 0; j < ROLE_OUT_EXP3; j++)
			{
				/* Check for match */
				if (role_out[j] == adv_combo[i][0] ||
				    role_out[j] == adv_combo[i][1])
				{
					/* Add score */
					act_scores[j] += scores[i];
				}
			}
		}

		/* Clear best score */
		b_s = -1;

		/* Loop over our action choices */
		for (i = 0; i < ROLE_OUT_EXP3; i++)
		{
			/* Check for better score */
			if (act_scores[i] > b_s)
			{
				/* Track best score */
				b_s = act_scores[i];

				/* Track best action combination */
				b_a = role_out[i];
			}
		}

		/* Set first action */
		action[0] = b_a;

		/* Done */
		return;
	}

	/* Set best actions */
	action[0] = adv_combo[b_a][0];
	action[1] = adv_combo[b_a][1];

	/* Predict our own actions */
	predict_action(g, who, desired, who);

	/* Track stats on predicted actions */
	role_avg += desired[b_a];

	/* Clear best score */
	b_p = -1;
	b_i = -1;

	/* Find most predicted action */
	for (i = 0; i < role.num_output; i++)
	{
		/* Check for higher than before */
		if (desired[i] > b_p)
		{
			/* Track best score */
			b_p = desired[i];
			b_i = i;
		}
	}

	/* Check for chosen action matching prediction */
	if (b_i == b_a)
	{
		/* Count hits */
		role_hit++;
	}
	else
	{
		/* Count miss */
		role_miss++;
	}

	/* Check for failure to search */
	if (b_s == 0)
	{
		/* Error */
		display_error("Did not find any action choices!\n");
		abort();
	}

	/* Compute probability sum */
	for (i = 0; i < role.num_output; i++)
	{
		/* Add this action's portion */
		sum += exp(20 * (scores[i] / b_s));
	}

	/* Compute actual action probabilities */
	for (i = 0; i < role.num_output; i++)
	{
		/* Compute probability ratio */
		desired[i] = exp(20 * (scores[i] / b_s)) / sum;
	}

	/* Train network */
	train_net(&role, 1.0, desired);

	/* Apply training */
	apply_training(&role);

	/* Clear placement cache */
	clear_opp_place_cache();
}

/*
 * Structure to hold opponent action choices.
 */
struct opponent_act
{
	/* Choices */
	int act[MAX_PLAYER];

	/* Probability of this action combination */
	double prob;
};

/*
 * List of action choice combinations.
 */
struct opponent_act *opponent_combos;
int opponent_combo_len, opponent_combo_size;

/*
 * Compare two opponent action choice combinations by probability.
 */
static int cmp_opponent_act(const void *p1, const void *p2)
{
	struct opponent_act *o1, *o2;

	/* Cast pointers */
	o1 = (struct opponent_act *)p1;
	o2 = (struct opponent_act *)p2;

	/* Compare probabilities */
	return o1->prob > o2->prob ? -1 : 1;
}

/*
 * Populate list of opponent combos.
 */
static void ai_choose_action_combo(game *g, int who, int current, double prob,
				   action_prob *action_order[MAX_PLAYER],
				   int acts[MAX_PLAYER], double threshold)
{
	int i;

	/* No need to predict our own action choice */
	if (current == who)
	{
		/* Go to next */
		ai_choose_action_combo(g, who, current + 1, prob, action_order,
				       acts, threshold);

		/* Done */
		return;
	}

	/* Skip unlikely possibilities */
	if (prob < threshold) return;
	if (prob == 0) return;

	/* Check for all other players chosen */
	if (current == g->num_players)
	{
		/* Check for full combo list */
		if (opponent_combo_len == opponent_combo_size)
		{
			/* Resize list */
			opponent_combo_size += 100;

			/* Reallocate */
			opponent_combos = (struct opponent_act *)realloc(
				opponent_combos,
				sizeof(struct opponent_act) *
				opponent_combo_size);
		}

		/* Copy actions to combo list */
		for (i = 0; i < g->num_players; i++)
		{
			/* Copy action */
			opponent_combos[opponent_combo_len].act[i] = acts[i];
		}

		/* Copy probability */
		opponent_combos[opponent_combo_len++].prob = prob;

		/* Done */
		return;
	}

	/* Loop over current player's choices */
	for (i = 0; i < role.num_output; i++)
	{
		/* Set player's action */
		acts[current] = role_out[action_order[current][i].choice];

		/* Try next player's actions */
		ai_choose_action_combo(g, who, current + 1,
		                     prob * action_order[current][i].prob,
				     action_order, acts, threshold);
	}
}

/*
 * Helper function for ai_choose_action().
 *
 * Score each of our action choices assuming given opponent action combo.
 */
static int ai_choose_action_aux(game *g, int who, int acts[], double prob,
                                double *prob_used, double scores[])
{
	game sim;
	int i, num = 0;
	double score, b_s = -1;
#ifdef DEBUG
	int old_computes, j;
#endif

	/* Copy opponent actions */
	for (i = 0; i < g->num_players; i++)
	{
		/* Skip ourself */
		if (i == who) continue;

		/* Copy actions */
		g->p[i].action[0] = acts[i];
		g->p[i].action[1] = -1;
	}

	/* Loop over available actions */
	for (i = 0; i < role.num_output; i++)
	{
		/* Track best score */
		if (scores[i] > b_s) b_s = scores[i];
	}

	/* Loop over available actions */
	for (i = 0; i < role.num_output; i++)
	{
		/* Check for prestige action used */
		if (g->p[who].prestige_action_used &&
		    (role_out[i] == ACT_SEARCH ||
		     role_out[i] & ACT_PRESTIGE)) continue;

		/* Check for no prestige available to spend */
		if (!g->p[who].prestige &&
		    (role_out[i] & ACT_PRESTIGE)) continue;

		/* Check for far-behind action score */
		if (scores[i] < (0.3 + *prob_used) * b_s) continue;

		/* Simulate game */
		simulate_game(&sim, g, who);

		/* Try this action */
		sim.p[who].action[0] = role_out[i];
		sim.p[who].action[1] = -1;

		/* Note actions */
		note_actions(&sim);

#ifdef DEBUG
		old_computes = num_computes;
		for (j = 0; j < g->num_players; j++)
		{
			printf("%s%s ", j == who ? "*" : "", action_name(sim.p[j].action[0]));
		}
		printf(": ");
#endif

		/* Start at beginning of turn */
		sim.cur_action = ACT_ROUND_START;

		/* Complete turn */
		complete_turn(&sim, COMPLETE_ROUND);

		/* Evaluate state after turn */
		score = eval_game(&sim, who);

#ifdef DEBUG
		printf("%f (%f)\n", score, prob);
		dump_game(g, &sim);
#endif

		/* Add score to chosen action */
		scores[i] += score * prob;

		/* Count number of actions tried */
		num++;
	}

	/* Total amount of "probability space" covered */
	*prob_used += prob;

	/* Return whether multiple actions tried */
	return num > 1;
}

/*
 * Choose role and bonus.
 */
static void ai_choose_action(game *g, int who, int action[2], int one)
{
	game sim;
	double scores[ROLE_OUT_EXP3], prob_used = 0, b_s = -1, b_p;
	double most_prob, threshold = 1.0;
	double *choice_prob[MAX_PLAYER];
	action_prob *action_order[MAX_PLAYER];
	double desired[ROLE_OUT_EXP3], sum = 0;
	int i, current, best = -1, b_i, acts[MAX_PLAYER], no_act[MAX_PLAYER];;

	/* Perform training at beginning of each round */
	perform_training(g, who, NULL);

	/* Clear sample results */
	ai_sample_clear();

	/* Clear placement cache */
	clear_opp_place_cache();

	/* Handle "advanced" game differently */
	if (g->advanced) return ai_choose_action_advanced(g, who, action, one);

	/* Clear scores */
	for (i = 0; i < role.num_output; i++) scores[i] = 0.0;

#ifdef DEBUG
	printf("\n--- Player %d choosing action\n", who);
	dump_hand(g, who);
#endif

	/* Create rows of probabilities */
	for (i = 0; i < g->num_players; i++)
	{
		/* Create row */
		choice_prob[i] =
		            (double *)malloc(sizeof(double) * role.num_output);
		action_order[i] =
		   (action_prob *)malloc(sizeof(action_prob) * role.num_output);
	}

	/* Get action predictions */
	for (current = 0; current < g->num_players; current++)
	{
		/* Skip given player */
		if (current == who) continue;

		/* Predict opponent's actions */
		predict_action(g, current, choice_prob[current], who);

		/* Loop over actions */
		for (i = 0; i < role.num_output; i++)
		{
			/* Check for prestige action used */
			if (g->p[current].prestige_action_used &&
			    (role_out[i] == ACT_SEARCH ||
			     role_out[i] & ACT_PRESTIGE))
			{
				/* Clear probability */
				choice_prob[current][i] = 0;
			}

			/* Check for no prestige available to spend */
			if (!g->p[current].prestige &&
			    (role_out[i] & ACT_PRESTIGE))
			{
				/* Clear probability */
				choice_prob[current][i] = 0;
			}
		}

#ifdef DEBUG
		printf("----- Player %d probability\n", current);
		for (i = 0; i < role.num_output; i++)
		{
			printf("%.2f ", choice_prob[current][i]);
		}
		printf("\n");
#endif

		/* Clear biggest probability */
		most_prob = 0;

		/* Loop over actions */
		for (i = 0; i < role.num_output; i++)
		{
			/* Check for bigger */
			if (choice_prob[current][i] > most_prob)
			{
				/* Track biggest */
				most_prob = choice_prob[current][i];
			}
		}

		/* Lower threshold */
		threshold *= most_prob;
	}

	/* Check for "select last" flag */
	if (count_active_flags(g, who, FLAG_SELECT_LAST))
	{
		/* Set probabilities to 1 of selected actions */
		for (current = 0; current < g->num_players; current++)
		{
			/* Skip given player */
			if (current == who) continue;

			/* Clear action probabilities */
			for (i = 0; i < role.num_output; i++)
			{
				/* Clear probability */
				choice_prob[current][i] = 0;

				/* Check for action chosen */
				if (role_out[i] == g->p[current].action[0])
				{
					/* Set probability to certain */
					choice_prob[current][i] = 1;
				}
			}
		}

		/* Clear threshold */
		threshold = 0;
	}

	/* Copy probabilities to action order table */
	for (current = 0; current < g->num_players; current++)
	{
		/* Skip given player */
		if (current == who) continue;

		/* Copy action probabilities */
		for (i = 0; i < role.num_output; i++)
		{
			/* Copy to action order table */
			action_order[current][i].prob = choice_prob[current][i];

			/* Set choice */
			action_order[current][i].choice = i;
		}

		/* Sort actions by probability */
		qsort(action_order[current], role.num_output,
		      sizeof(action_prob), cmp_action_prob);
	}

	/* Reduce threshold to check similar events */
	threshold /= 8;

	/* Always check everything with two players */
	if (g->num_players == 2) threshold = 0;

	/* Increase threshold with large numbers of players */
	if (g->num_players == 4) threshold *= 5;
	if (g->num_players >= 5) threshold *= 7;

#ifdef DEBUG
	printf("----- Threshold %.2f\n", threshold);
#endif

	/* Clear opponent action combo list */
	opponent_combo_len = 0;

	/* Compute opponent combination probabilities */
	ai_choose_action_combo(g, who, 0, 1.0, action_order, acts, threshold);

	/* Sort opponent combinations by probability */
	qsort(opponent_combos, opponent_combo_len, sizeof(struct opponent_act),
	      cmp_opponent_act);

	/* Simulate game */
	simulate_game(&sim, g, who);

	/* Clear active actions */
	for (i = 0; i < MAX_ACTION; i++) sim.action_selected[i] = 0;

	/* Create "no action" array */
	for (i = 0; i < MAX_PLAYER; i++) no_act[i] = -1;

	/* Evaluate our actions without any opponent actions */
	ai_choose_action_aux(&sim, who, no_act, threshold, &prob_used, scores);

	/* Loop over opponent combos */
	for (i = 0; i < opponent_combo_len; i++)
	{
		/* Evaluate our actions */
		if (!ai_choose_action_aux(&sim, who, opponent_combos[i].act,
	 	                          opponent_combos[i].prob, &prob_used,
		                          scores))
		{
			/* Only checked one action, done */
			break;
		}
	}

	/* Free rows of probabilities */
	for (i = 0; i < g->num_players; i++)
	{
		/* Free row */
		free(choice_prob[i]);
	}

#ifdef DEBUG
	printf("----- Prob used: %.2f\n", prob_used);

	printf("----- Action scores\n");
	for (i = 0; i < role.num_output; i++)
	{
		printf("%.2f ", scores[i]);
	}
	printf("\n");
#endif

	/* Loop over possible actions */
	for (i = 0; i < role.num_output; i++)
	{
		/* Check for better */
		if (scores[i] > b_s)
		{
			/* Track best */
			b_s = scores[i];
			best = i;
		}
	}

	/* Check for no action selected */
	if (b_s < 0)
	{
		/* Error */
		display_error("No action selected!\n");
		abort();
	}

	/* Select best action */
	action[0] = role_out[best];

	/* No second action */
	action[1] = -1;

	/* Predict our own action */
	predict_action(g, who, desired, who);

	/* Track stats on predicted actions */
	role_avg += desired[best];

	/* Clear best score */
	b_p = -1;
	b_i = -1;

	/* Find most predicted action */
	for (i = 0; i < role.num_output; i++)
	{
		/* Check for higher than before */
		if (desired[i] > b_p)
		{
			/* Track best score */
			b_p = desired[i];
			b_i = i;
		}
	}

	/* Check for chosen action matching prediction */
	if (b_i == best)
	{
		/* Count hits */
		role_hit++;
	}
	else
	{
		/* Count miss */
		role_miss++;
	}

	/* Compute probability sum */
	for (i = 0; i < role.num_output; i++)
	{
		/* Add this action's portion */
		sum += exp(20 * (scores[i] / b_s));
	}

	/* Compute actual action probabilities */
	for (i = 0; i < role.num_output; i++)
	{
		/* Compute probability ratio */
		desired[i] = exp(20 * (scores[i] / b_s)) / sum;
	}

	/* Train network */
	train_net(&role, 1.0, desired);

	/* Apply training */
	apply_training(&role);

	/* Clear placement cache */
	clear_opp_place_cache();
}

/*
 * Return true if the first score is at least as good as the second.
 *
 * Due to small cumulative errors from the neural network, a simple >= does
 * not work.
 */
static int score_better(double s1, double s2)
{
	return s1 >= s2 - 0.000001;
}

/*
 * Helper function for ai_choose_discard().
 */
static void ai_choose_discard_aux(game *g, int who, int list[], int n, int c,
                                  int chosen, int *best, double *b_s)
{
	game sim;
	double score;
	int discards[MAX_DECK], num_discards = 0;
	int i;

	/* Check for too few choices */
	if (c > n) return;

	/* Check for end */
	if (!n)
	{
		/* Loop over chosen cards */
		for (i = 0; (1 << i) <= chosen; i++)
		{
			/* Check for bit set */
			if (chosen & (1 << i))
			{
				/* Add card to list */
				discards[num_discards++] = list[i];
			}
		}

		/* Copy game */
		simulate_game(&sim, g, who);

		/* Apply choice */
		discard_callback(&sim, who, discards, num_discards);

		/* Check for explore phase */
		if (sim.cur_action == ACT_EXPLORE_5_0)
		{
			/* Simulate most rest of turn */
			complete_turn(&sim, COMPLETE_ROUND);
		}

		/* Evaluate result */
		score = eval_game(&sim, who);

		/* Check for better score */
		if (score_better(score, *b_s))
		{
			/* Save better choice */
			*b_s = score;
			*best = chosen;
		}

		/* Done */
		return;
	}

	/* Try without current card */
	ai_choose_discard_aux(g, who, list, n - 1, c, chosen << 1, best, b_s);

	/* Try with current card (if more can be chosen) */
	if (c) ai_choose_discard_aux(g, who, list, n - 1, c - 1,
	                             (chosen << 1) + 1, best, b_s);
}

/*
 * Helper function for ai_choose_discard().
 */
static void ai_choose_discard_aux_action(game *g, int who, int list[], int n,
					 int c, int chosen, int *best,
					 double *b_s)
{
	game sim, sim2;
	double score;
	int discards[MAX_DECK], num_discards = 0;
	int i;

	/* Check for too few choices */
	if (c > n) return;

	/* Check for end */
	if (!n)
	{
		/* Loop over chosen cards */
		for (i = 0; (1 << i) <= chosen; i++)
		{
			/* Check for bit set */
			if (chosen & (1 << i))
			{
				/* Add card to list */
				discards[num_discards++] = list[i];
			}
		}

		/* Copy game */
		simulate_game(&sim, g, who);

		/* Apply choice */
		discard_callback(&sim, who, discards, num_discards);

		/* Loop over other players */
		for (i = 0; i < g->num_players; i++)
		{
			/* Skip ourself */
			if (i == who) continue;

			/* Make starting discards */
			sim.p[i].fake_discards = 2;
		}

		/* Loop over possible action choices for first turn */
		for (i = 0; i < role.num_output; i++)
		{
			/* Simulate game */
			simulate_game(&sim2, &sim, who);

			/* Check for advanced game */
			if (!g->advanced)
			{
				/* Disallow prestige actions */
				if (role_out[i] & ACT_PRESTIGE) continue;

				/* Set actions */
				sim2.p[who].action[0] = role_out[i];
				sim2.p[who].action[1] = -1;
			}
			else
			{
				/* Disallow prestige actions */
				if (adv_combo[i][0] & ACT_PRESTIGE) continue;
				if (adv_combo[i][1] & ACT_PRESTIGE) continue;

				/* Set actions */
				sim2.p[who].action[0] = adv_combo[i][0];
				sim2.p[who].action[1] = adv_combo[i][1];
			}

			/* Note actions */
			note_actions(&sim2);

			/* Start at beginning of first turn */
			sim2.cur_action = ACT_ROUND_START;

			/* Complete turn */
			complete_turn(&sim2, COMPLETE_ROUND);

			/* Evaluate results of first turn */
			score = eval_game(&sim2, who);

			/* Check for better score */
			if (score_better(score, *b_s))
			{
				/* Save better choice */
				*b_s = score;
				*best = chosen;
			}
		}

		/* Done */
		return;
	}

	/* Try without current card */
	ai_choose_discard_aux_action(g, who, list, n - 1, c, chosen << 1, best,
				     b_s);

	/* Try with current card (if more can be chosen) */
	if (c) ai_choose_discard_aux_action(g, who, list, n - 1, c - 1,
	                           (chosen << 1) + 1, best, b_s);
}

/*
 * Choose cards to discard.
 */
static void ai_choose_discard(game *g, int who, int list[], int *num,
                              int discard)
{
	game sim;
	player *p_ptr;
	double b_s = -1, score, percard[MAX_DECK];
	int discards[MAX_DECK], n = 0;
	int best, i, j, b_i;

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Check for simulated game */
	if (g->simulation)
	{
		/* Track cards discarded */
		p_ptr->fake_discards += discard;

		/* Don't actually discard anything */
		*num = 0;

		/* Done */
		return;
	}

	/* XXX - Check for more than 20 cards to choose from */
	if (*num > 20)
	{
		/* Get score of discarding each card */
		for (i = 0; i < *num; i++)
		{
			/* Simulate game */
			simulate_game(&sim, g, who);

			/* Discard one */
			discard_callback(&sim, who, &list[i], 1);

			/* Evaluate game */
			percard[i] = eval_game(&sim, who);
		}

		/* Loop over number of cards to discard */
		for (i = 0; i < discard; i++)
		{
			/* Clear best score */
			b_s = b_i = -1;

			/* Loop over per-card scores */
			for (j = 0; j < *num; j++)
			{
				/* Check for better than other score */
				if (percard[j] > b_s)
				{
					/* Track best */
					b_s = percard[j];
					b_i = j;
				}
			}

			/* Add card to list */
			discards[i] = list[b_i];

			/* Move last card into spot */
			list[b_i] = list[--(*num)];
			percard[b_i] = percard[*num];
		}

		/* Copy discards into list */
		for (i = 0; i < discard; i++) list[i] = discards[i];

		/* Set number of cards discarded */
		*num = discard;

		/* Done */
		return;
	}

	/* XXX - Check for lots of cards and discards */
	while (*num > 5 && discard > 2 && *num - discard > 2)
	{
		/* Clear best score */
		b_s = b_i = -1;

		/* Discard worst card */
		for (i = 0; i < *num; i++)
		{
			/* Simulate game */
			simulate_game(&sim, g, who);

			/* Discard previously discarded cards */
			discard_callback(&sim, who, discards, n);

			/* Discard one */
			discard_callback(&sim, who, &list[i], 1);

			/* Mark rest as fake discards */
			sim.p[who].fake_discards += discard - 1;

			/* Check for explore phase */
			if (sim.cur_action == ACT_EXPLORE_5_0)
			{
				/* Simulate most rest of turn */
				complete_turn(&sim, COMPLETE_ROUND);
			}

			/* Evaluate game */
			score = eval_game(&sim, who);

			/* Check for better score */
			if (score_better(score, b_s))
			{
				/* Track best */
				b_s = score;
				b_i = i;
			}
		}

		/* Discard worst card */
		discards[n++] = list[b_i];

		/* Move last card into given spot */
		list[b_i] = list[--(*num)];

		/* One less card to discard */
		discard--;
	}

	/* Clear best score */
	b_s = -1;

	/* Simulate game */
	simulate_game(&sim, g, who);

	/* Discard already chosen cards */
	discard_callback(&sim, who, discards, n);

	/* Check for action selection to happen after discarding */
	if (!g->simulation && g->cur_action == ACT_ROUND_START && g->round == 0)
	{
		/* Clear explore and place samples */
		ai_sample_clear();
		clear_opp_place_cache();

		/* Do deeper search for discarded cards */
		ai_choose_discard_aux_action(&sim, who, list, *num, discard, 0,
					     &best, &b_s);
	}
	else
	{
		/* Find best set of cards */
		ai_choose_discard_aux(&sim, who, list, *num, discard, 0,
				      &best, &b_s);
	}

	/* Check for failure */
	if (b_s == -1)
	{
		/* Error */
		display_error("Failed to find good discard set!\n");
		abort();
	}

	/* Loop over set of chosen cards */
	for (i = 0; (1 << i) <= best; i++)
	{
		/* Check for bit set */
		if (best & (1 << i))
		{
			/* Add card to discard list */
			discards[n++] = list[i];
		}
	}

	/* Copy discards to list */
	for (i = 0; i < n; i++)
	{
		/* Copy card */
		list[i] = discards[i];
	}

	/* Set number of chosen cards */
	*num = n;
}

/*
 * Choose a card to save for later.
 */
static void ai_choose_save(game *g, int who, int list[], int *num)
{
	game sim;
	int i, best = -1;
	double score, b_s = -1;

	/* Do not examine choices in simulated game */
	if (g->simulation)
	{
		/* Clear choice */
		list[0] = -1;
		return;
	}

	/* Loop over choices */
	for (i = 0; i < *num; i++)
	{
		/* Simulate game */
		simulate_game(&sim, g, who);

		/* Save card */
		move_card(&sim, list[i], who, WHERE_SAVED);

		/* Simulate rest of turn */
		complete_turn(&sim, COMPLETE_ROUND);

		/* Get score */
		score = eval_game(&sim, who);

		/* Check for better */
		if (score_better(score, b_s))
		{
			/* Track best */
			b_s = score;
			best = list[i];
		}
	}

	/* Set best choice */
	list[0] = best;
}

/*
 * Choose a card (if any) to discard for prestige.
 */
static void ai_choose_discard_prestige(game *g, int who, int list[], int *num)
{
	game sim;
	double b_s, score;
	int i, b_i = -1;

	/* Copy game */
	simulate_game(&sim, g, who);

	/* Finish turn without discarding anything */
	complete_turn(&sim, COMPLETE_ROUND);

	/* Get score */
	b_s = eval_game(&sim, who);

	/* Check for simulated game */
	if (g->simulation)
	{
		/* Copy game */
		simulate_game(&sim, g, who);

		/* Discard a fake card */
		sim.p[who].fake_discards++;

		/* Gain a prestige */
		gain_prestige(&sim, who, 1, NULL);

		/* Finish turn */
		complete_turn(&sim, COMPLETE_ROUND);

		/* Get score */
		score = eval_game(&sim, who);

		/* Check for better than before */
		if (score_better(score, b_s))
		{
			/* Discard */
			g->p[who].fake_discards++;
			gain_prestige(g, who, 1, NULL);
		}

		/* Return no card */
		*num = 0;

		/* Done */
		return;
	}

	/* Loop over card choices */
	for (i = 0; i < *num; i++)
	{
		/* Copy game */
		simulate_game(&sim, g, who);

		/* Discard chosen card */
		move_card(&sim, list[i], -1, WHERE_DISCARD);

		/* Gain prestige */
		gain_prestige(&sim, who, 1, NULL);

		/* Finish turn */
		complete_turn(&sim, COMPLETE_ROUND);

		/* Get score */
		score = eval_game(&sim, who);

		/* Check for better than before */
		if (score_better(score, b_s))
		{
			/* Remember best score */
			b_s = score;
			b_i = i;
		}
	}

	/* Check for card chosen */
	if (b_i >= 0)
	{
		/* Set choice */
		list[0] = list[b_i];
		*num = 1;
	}
	else
	{
		/* No card chosen */
		*num = 0;
	}
}

/*
 * Discard worst cards when checking for likely Explore results.
 */
static void ai_explore_sample_aux(game *g, int who, int draw, int keep,
                                  int discard_any, int discards[MAX_DECK])
{
	game sim, sim2;
	card *c_ptr;
	int list[MAX_DECK], num = 0, n = 0;
	int i, x, b_i, discard, old_act;
	int best;
	double score, b_s;

	/* Compute number of cards to discard */
	discard = draw - keep;

	/* Start at first card in hand */
	x = g->p[who].head[WHERE_HAND];

	/* Loop over cards */
	for ( ; x != -1; x = g->deck[x].next)
	{
		/* Get card pointer */
		c_ptr = &g->deck[x];

		/* Skip old cards if they can't be discarded */
		if (c_ptr->start_where == WHERE_HAND &&
		    c_ptr->start_owner == who && !discard_any) continue;

		/* Add card to list */
		list[num++] = x;
	}

	/* Check for too many cards in hand */
	if (num > 30)
	{
		/* Just discard first cards */
		for (i = 0; i < discard; i++)
		{
			/* Copy first cards */
			discards[i] = list[i];
		}

		/* Done */
		return;
	}

	/* Simulate game */
	simulate_game(&sim, g, who);

	/* XXX - Check for lots of cards and discards */
	while (num > 8 && discard > 2 && (num - discard) > 2)
	{
		/* Clear best score */
		b_s = b_i = -1;

		/* Discard worst card */
		for (i = 0; i < num; i++)
		{
			/* Simulate game */
			simulate_game(&sim2, &sim, who);

			/* Discard one */
			discard_callback(&sim2, who, &list[i], 1);

			/* Evaluate game */
			score = eval_game(&sim2, who);

			/* Check for better score */
			if (score_better(score, b_s))
			{
				/* Track best */
				b_s = score;
				b_i = i;
			}
		}

		/* Discard worst card */
		discard_callback(&sim, who, &list[b_i], 1);

		/* Remember worst card */
		discards[discard - 1] = list[b_i];

		/* Move last card into given spot */
		list[b_i] = list[--num];

		/* One less card to discard */
		discard--;
	}

	/* Clear best score */
	b_s = -1;

	/* XXX Change action so that we don't simulate rest of turn */
	old_act = sim.cur_action;
	sim.cur_action = ACT_ROUND_START;

	/* Find best set of cards */
	ai_choose_discard_aux(&sim, who, list, num, discard, 0, &best, &b_s);

	/* XXX Restore action */
	sim.cur_action = old_act;

	/* Loop over set of chosen cards */
	for (i = 0; (1 << i) <= best; i++)
	{
		/* Check for bit set */
		if (best & (1 << i))
		{
			/* Add card to list */
			discards[n++] = list[i];
		}
	}
}

/*
 * Apply the results from an Explore sample.
 */
static void ai_explore_sample_apply(game *g, int who, int draw, int keep,
                                    struct sample_score *s_ptr)
{
	int i;

	/* Claim cards */
	for (i = 0; i < draw; i++)
	{
		/* Claim card */
		claim_card(g, who, s_ptr->list[i]);

		/* Mark card as fake */
		g->deck[s_ptr->list[i]].misc |= MISC_FAKE;
	}

	/* Discard worst */
	discard_callback(g, who, s_ptr->discards, draw - keep);

	/* Clear fake drawn card counters */
	g->p[who].drawn_round = 0;
	g->p[who].fake_hand = 0;
	g->p[who].fake_discards = 0;
}

/*
 * Place a representative sample of possible cards from an Explore phase
 * in our hand.
 */
static void ai_explore_sample(game *g, int who, int draw, int keep,
                              int discard_any)
{
	game sim;
	card *c_ptr;
	int unknown[MAX_DECK], num_unknown = 0;
	struct sample_score scores[10];
	int i, j, k;
	unsigned int seed;

	/* Loop over previous results */
	for (i = 0; i < MAX_EXPLORE_SAMPLE; i++)
	{
		/* Skip invalid results */
		if (!explore_seen[i].valid) break;

		/* Skip results that don't match */
		if (explore_seen[i].drawn != draw) continue;
		if (explore_seen[i].keep != keep) continue;
		if (explore_seen[i].discard_any != discard_any) continue;

		/* Apply result */
		ai_explore_sample_apply(g, who, draw, keep, &explore_seen[i]);

		/* Done */
		return;
	}

	/* Loop over deck */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Check for unknown location */
		if (!(c_ptr->misc & (1 << who)))
		{
			/* Add to list */
			unknown[num_unknown++] = i;
		}
	}

	/* Try multiple random samples */
	for (i = 0; i < 10; i++)
	{
		/* Simulate game */
		simulate_game(&sim, g, who);

		/* Use iteration as seed */
		seed = i;

		/* Pick cards from unknown list */
		for (j = 0; j < draw; j++)
		{
			/* Choose card at random */
			k = simple_rand(&seed) % num_unknown;

			/* Claim card for ourself */
			claim_card(&sim, who, unknown[k]);

			/* Mark card as fake */
			sim.deck[unknown[k]].misc |= MISC_FAKE;

			/* Add claimed card to list for this iteration */
			scores[i].list[j] = unknown[k];

			/* Remove card from list */
			unknown[k] = unknown[--num_unknown];
		}

		/* Find worst cards */
		ai_explore_sample_aux(&sim, who, draw, keep, discard_any,
		                      scores[i].discards);

		/* Discard worst */
		discard_callback(&sim, who, scores[i].discards, draw - keep);

		/* Clear fake card counts */
		sim.p[who].drawn_round = 0;
		sim.p[who].fake_hand = 0;
		sim.p[who].fake_discards = 0;

		/* Score game */
		scores[i].score = eval_game(&sim, who);

		/* Save parameters */
		scores[i].drawn = draw;
		scores[i].keep = keep;
		scores[i].discard_any = discard_any;

		/* Put chosen cards back in unknown list */
		for (j = 0; j < draw; j++)
		{
			/* Add back to list */
			unknown[num_unknown++] = scores[i].list[j];
		}
	}

	/* Sort list of scores */
	qsort(scores, 10, sizeof(struct sample_score), cmp_sample_score);

	/* Use second-worst sample */
	j = 1;

	/* Loop over previous explore sample results */
	for (i = 0; i < MAX_EXPLORE_SAMPLE; i++)
	{
		/* Skip already valid results */
		if (explore_seen[i].valid) continue;

		/* Copy results */
		memcpy(&explore_seen[i], &scores[j],
		       sizeof(struct sample_score));

		/* Mark as valid */
		explore_seen[i].valid = 1;

		/* Apply result */
		ai_explore_sample_apply(g, who, draw, keep, &explore_seen[i]);

		/* Done */
		return;
	}

	/* XXX */
	display_error("Ran out of explore sample result entries!\n");
	abort();
}

/*
 * Helper function for ai_choose_start() below.
 */
static void ai_choose_start_aux(game *g, int who, int list[], int n, int c,
                                int chosen, int *best, double *b_s, int start)
{
	game sim, sim2;
	double score;
	int discards[MAX_DECK], num_discards = 0;
	int i;
	int special[1];

	/* Check for too few choices */
	if (c > n) return;

	/* Check for end */
	if (!n)
	{
		/* Loop over chosen cards */
		for (i = 0; (1 << i) <= chosen; i++)
		{
			/* Check for bit set */
			if (chosen & (1 << i))
			{
				/* Add card to list */
				discards[num_discards++] = list[i];
			}
		}

		/* Copy game */
		simulate_game(&sim, g, who);

		/* Create special array */
		special[0] = start;

		/* Apply choice */
		start_callback(&sim, who, discards, num_discards, special, 1);

		/* Handle special abilites of start world */
		start_chosen(&sim);

		/* Loop over players */
		for (i = 0; i < g->num_players; i++)
		{
			/* Skip ourself */
			if (i == who) continue;

			/* Assume opponent will discard 2 */
			sim.p[i].fake_discards = 2;
		}

		/* Loop over possible action choices for first turn */
		for (i = 0; i < role.num_output; i++)
		{
			/* Simulate game */
			simulate_game(&sim2, &sim, who);

			/* Check for advanced game */
			if (!g->advanced)
			{
				/* Disallow prestige actions */
				if (role_out[i] & ACT_PRESTIGE) continue;

				/* Set actions */
				sim2.p[who].action[0] = role_out[i];
				sim2.p[who].action[1] = -1;
			}
			else
			{
				/* Disallow prestige actions */
				if (adv_combo[i][0] & ACT_PRESTIGE) continue;
				if (adv_combo[i][1] & ACT_PRESTIGE) continue;

				/* Set actions */
				sim2.p[who].action[0] = adv_combo[i][0];
				sim2.p[who].action[1] = adv_combo[i][1];
			}

			/* Note actions */
			note_actions(&sim2);

			/* Start at beginning of first turn */
			sim2.cur_action = ACT_ROUND_START;

			/* Complete turn */
			complete_turn(&sim2, COMPLETE_ROUND);

			/* Evaluate results of first turn */
			score = eval_game(&sim2, who);

			/* Check for better score */
			if (score_better(score, *b_s))
			{
				/* Save better choice */
				*b_s = score;
				*best = chosen;
			}
		}

		/* Done */
		return;
	}

	/* Try without current card */
	ai_choose_start_aux(g, who, list, n - 1, c, chosen << 1, best, b_s,
	                    start);

	/* Try with current card (if more can be chosen) */
	if (c) ai_choose_start_aux(g, who, list, n - 1, c - 1,
	                           (chosen << 1) + 1, best, b_s, start);
}

/*
 * Choose a start world from the list given.
 */
static void ai_choose_start(game *g, int who, int list[], int *num,
                            int special[], int *ns)
{
	int i, discards, best = -1, best_discards = -1;
	double score, b_s = -1;
	int target;

	/* Loop over choices of start world */
	for (i = 0; i < *ns; i++)
	{
		/* Clear explore and place samples */
		ai_sample_clear();
		clear_opp_place_cache();

		/* Assume discard to 4 */
		target = 4;

		/* Clear score */
		score = -1;

		/* Score best starting discards */
		ai_choose_start_aux(g, who, list, *num, *num - target, 0,
		                    &discards, &score, special[i]);

		/* Check for better than before */
		if (score > b_s)
		{
			/* Track best */
			b_s = score;
			best = special[i];
			best_discards = discards;
		}
	}

	/* Remember start world chosen */
	special[0] = best;
	*ns = 1;

	/* Start with no cards to discard */
	*num = 0;

	/* Loop over set of chosen cards to discard */
	for (i = 0; (1 << i) <= best_discards; i++)
	{
		/* Check for bit set */
		if (best_discards & (1 << i))
		{
			/* Add card to discard list */
			list[(*num)++] = list[i];
		}
	}
}

/*
 * Simulate placing and paying for the given card (as an opponent).
 */
static double ai_choose_place_opp_aux(game *g, int who, int which, int phase,
				      int special)
{
	/* Place given card */
	g->p[who].placing = which;

	/* Check for card to place */
	if (which != -1)
	{
		/* Place card */
		place_card(g, who, which);
	}

	/* Check for Develop phase */
	if (phase == PHASE_DEVELOP)
	{
		/* Check for development chosen */
		if (which != -1)
		{
			/* Develop choice */
			develop_action(g, who, which);
		}
		else
		{
			/* Set no develop flag */
			g->p[who].skip_develop = 1;
		}
	}

	/* Check for Settle phase */
	if (phase == PHASE_SETTLE)
	{
		/* Check for takeovers or placement */
		if (which != -1 || settle_check_takeover(g, who, NULL, 0))
		{
			/* Pay for world */
			settle_finish(g, who, which, 0, special, 0);

			/* Use rest of settle powers */
			settle_extra(g, who, which);

			/* Resolve takeovers */
			resolve_takeovers(g);
		}
		else
		{
			/* Set no settle flag */
			g->p[who].skip_settle = 1;
		}
	}

	/* Finish placement and check for game over */
	complete_turn(g, COMPLETE_CHECK);

	/* Determine score for placing this card */
	return eval_game(g, g->sim_who);
}

/*
 * Choose a card to play for an opponent.
 */
static int ai_choose_place_opp(game *g, int who, int phase, int special)
{
	game sim;
	card *c_ptr;
	int i, j, n = 0, type;
	unsigned int seed;
	power *o_ptr;
	int mil_only = 0, max = 0, reduce;
	int hand_size, extra_count = 0;
	int windfall_only = 0, force_place = 0;
	int unknown[MAX_DECK], num_unknown = 0;
	double score, no_place;
	eval_cache *e_ptr;
	struct sample_score scores[MAX_DECK];

	/* Determine type of card to look for */
	if (phase == PHASE_DEVELOP) type = TYPE_DEVELOPMENT;
	if (phase == PHASE_SETTLE) type = TYPE_WORLD;

	/* Check for skipped last phase */
	if (phase == PHASE_DEVELOP && g->p[who].skip_develop &&
	    !player_chose(g, who, g->cur_action)) return -1;
	if (phase == PHASE_SETTLE && g->p[who].skip_settle) return -1;

	/* Assume players who trade without goods will settle a windfall */
	if (phase == PHASE_SETTLE && player_chose(g, who, ACT_CONSUME_TRADE) &&
	    !count_player_area(g, who, WHERE_GOOD))
	{
		/* Restrict to windfall worlds only */
		windfall_only = 1;

		/* Do not allow no placement */
		force_place = 1;
	}

	/* Compute hand size */
	hand_size = count_player_area(g, who, WHERE_HAND) +
	            g->p[who].fake_hand - g->p[who].fake_discards;

	/* Check for empty hand */
	if (!hand_size) return -1;

	/* Check for placement due to "military only" power */
	if (special != -1)
	{
		/* Loop over powers on card */
		for (i = 0; i < g->deck[special].d_ptr->num_power; i++)
		{
			/* Get power pointer */
			o_ptr = &g->deck[special].d_ptr->powers[i];

			/* Skip non-settle powers */
			if (o_ptr->phase != PHASE_SETTLE) continue;

			/* Check for place military world power */
			if (o_ptr->code & P3_PLACE_MILITARY) mil_only = 1;

			/* Check for place with leftover power */
			if (o_ptr->code & P3_PLACE_LEFTOVER)
			{
				/* XXX Assume no second placement */
				return -1;
			}
		}
	}

	/* Check for development phase */
	if (phase == PHASE_DEVELOP)
	{
		/* Get maximum discount */
		reduce = develop_discount(g, who);

		/* Compute max cost */
		max = count_player_area(g, who, WHERE_HAND) +
		      g->p[who].fake_hand -
		      g->p[who].fake_discards + reduce - 1;
	}

	/* Loop over cards in deck */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Skip cards with known locations */
		if (c_ptr->misc & (1 << g->sim_who)) continue;

		/* Check for incorrect type */
		if (c_ptr->d_ptr->type != type) continue;

		/* Check for only windfall worlds */
		if (windfall_only && !(c_ptr->d_ptr->flags & FLAG_WINDFALL))
			continue;

		/* Add card to unknown list */
		unknown[num_unknown++] = i;
	}

	/* Check for no cards available to play */
	if (num_unknown == 0) return -1;

	/* Compute number of cards seen by this player this turn */
	hand_size = count_player_area(g, who, WHERE_HAND) +
	            g->p[who].drawn_round;

	/* Check for no cards to play */
	if (hand_size == 0) return -1;

	/* Check more samples if player chose this action */
	if (player_chose(g, who, g->cur_action))
	{
		/* Add count */
		hand_size += 2;

		/* Do not allow no placement */
		force_place = 1;
	}

	/* Cap maximum hand size */
	if (hand_size > 20) hand_size = 20;

	/* Use fake random seed */
	seed = 0;

	/* Loop over number of cards seen */
	for (i = 0; i < hand_size; i++)
	{
		/* XXX Check for forced placement */
		if (force_place && n == 0 && extra_count < 20)
		{
			/* Keep trying */
			i--;
			extra_count++;
		}

		/* Choose card at random */
		j = simple_rand(&seed) % num_unknown;

		/* Get card pointer */
		c_ptr = &g->deck[unknown[j]];

		/* Check for development */
		if (phase == PHASE_DEVELOP)
		{
			/* Check for too expensive */
			if (c_ptr->d_ptr->cost > max) continue;

			/* Check for duplicate development */
			if (player_has(g, who, c_ptr->d_ptr)) continue;
		}

		/* Check for world */
		if (phase == PHASE_SETTLE)
		{
			/* Check for illegal world */
			if (!settle_legal(g, who, unknown[j], 0, mil_only, 0,
			                  0))
				continue;
		}

		/* Get pointer to cache entry */
		e_ptr = lookup_opp_place(g, g->sim_who, who, unknown[j],
		                         special);

		/* Check for entry in cache */
		if (e_ptr->score != -1)
		{
			/* Get score from cache */
			scores[n].list[0] = unknown[j];
			scores[n++].score = e_ptr->score;
			continue;
		}

		/* Simulate game */
		simulate_game(&sim, g, who);

		/* Claim world */
		if (claim_card(&sim, who, unknown[j]))
		{
			/* Add a discard to make up for taken card */
			sim.p[who].fake_discards++;
		}

		/* Get score for playing this card */
		score = ai_choose_place_opp_aux(&sim, who, unknown[j], phase,
						special);

		/* Remember card played and resulting score */
		scores[n].list[0] = unknown[j];
		scores[n++].score = score;

		/* Add score to cache */
		e_ptr->score = score;
	}

	/* Check for no legal placements made */
	if (!n) return -1;

	/* Get pointer to cache score */
	e_ptr = lookup_opp_place(g, g->sim_who, who, -1, special);

	/* Check for score in no-placement cache */
	if (e_ptr->score != -1)
	{
		/* Get score from cache */
		no_place = e_ptr->score;
	}
	else
	{
		/* Simulate game */
		simulate_game(&sim, g, who);

		/* Get score for placing no card */
		no_place = ai_choose_place_opp_aux(&sim, who, -1, phase,
						   special);

		/* Store score in cache */
		e_ptr->score = no_place;
	}

	/* Skip adding no place scores if placement is forced */
	if (!force_place)
	{
		/* Add entries for placing no card */
		for (i = 0; i < 5; i++)
		{
			/* Add entry for placing no card */
			scores[n].list[0] = -1;
			scores[n++].score = no_place;
		}
	}

	/* Sort list of scores */
	qsort(scores, n, sizeof(struct sample_score), cmp_place_score);

	/* Use fourth-best score */
	i = 3;

	/* Check for this player called this phase */
	if (player_chose(g, who, g->cur_action))
	{
		/* Use best score instead */
		i = 0;
	}

	/* Check for end of list */
	if (i > n - 1) i = n - 1;

	/* Check for card to play */
	if (scores[i].list[0] != -1)
	{
		/* Claim card for ourself */
		if (claim_card(g, who, scores[i].list[0]))
		{
			/* Discard one card to make up for taken card */
			g->p[who].fake_discards++;
		}

		/* Mark card as known to everyone */
		g->deck[scores[i].list[0]].misc |= MISC_KNOWN_MASK;
	}

	/* Return card played */
	return scores[i].list[0];
}

/*
 * Choose card to play (develop or settle).
 */
static int ai_choose_place(game *g, int who, int list[], int num, int phase,
                           int special, int additional)
{
	game sim, sim2;
	int i, which, best = -1;
	double score, b_s;

	/* Check for real game */
	if (!g->simulation)
	{
		/* Clear placement cache */
		clear_opp_place_cache();
	}

	/* Check for simulated game for opponent */
	if (g->simulation && g->sim_who != who)
	{
		/* Choose for opponent */
		return ai_choose_place_opp(g, who, phase, special);
	}

	/* Check for all known cards placed or discarded */
	if (g->simulation && g->p[g->sim_who].low_hand == 0) return -1;

	/* Check for no choices */
	if (!num) return -1;

	/* Check for additional placement */
	if (!additional)
	{
		/* Simulate game */
		simulate_game(&sim, g, who);

		/* Loop over opponents */
		for (i = 0; i < g->num_players; i++)
		{
			/* Skip ourself */
			if (i == who) continue;

			/* Check for choice already made */
			if (g->simulation)
			{
				/* Get presaved choice */
				which = g->p[i].placing;
			}
			else
			{
				/* Select card for opponent to play */
				which = ai_choose_place_opp(&sim, i, phase,
				                            special);

				/* Save choice */
				sim.p[i].placing = which;
			}

			/* Check for card to place */
			if (which != -1)
			{
				/* Place card */
				place_card(&sim, i, which);

				/* Pay for choice */
				if (phase == PHASE_DEVELOP)
				{
					/* Develop choice */
					develop_action(&sim, i, which);
				}
				else
				{
					/* Settle choice */
					settle_finish(&sim, i, which, 0,
						      special, 0);
				}
			}
			else
			{
				/* Mark player as skipped */
				if (phase == PHASE_DEVELOP)
				{
					/* Mark as skipped */
					sim.p[i].skip_develop = 1;
				}
				else
				{
					/* Mark as skipped */
					sim.p[i].skip_settle = 1;
				}
			}
		}
	}
	else
	{
		/* Copy game */
		simulate_game(&sim, g, who);
	}

	/* Simulate game */
	simulate_game(&sim2, &sim, who);

	/* Check for Settle phase */
	if (phase == PHASE_SETTLE)
	{
		/* Check for takeovers */
		if (settle_check_takeover(&sim2, who, NULL, 0))
		{
			/* Take no-place action */
			settle_finish(&sim2, who, -1, 0, special, 0);
			settle_extra(&sim2, who, -1);

			/* Resolve takeovers */
			resolve_takeovers(&sim2);
		}
	}

	/* Mark us as skipped */
	if (phase == PHASE_DEVELOP)
	{
		/* Mark as skipped */
		sim2.p[who].skip_develop = 1;
	}
	else
	{
		/* Mark as skipped */
		sim2.p[who].skip_settle = 1;
	}

	/* Finish turn without placing */
	complete_turn(&sim2, COMPLETE_ROUND);

	/* Get score for doing nothing */
	b_s = eval_game(&sim2, who);

#ifdef DEBUG
	if (!g->simulation)
	{
		printf("Choosing placement for player %d:\n", who);
		printf("-- Score for no placement: %f\n", b_s);
		dump_game(g, &sim2);
	}
#endif

	/* Loop over choices */
	for (i = 0; i < num; i++)
	{
		/* Check for fake card and we called phase */
		if (player_chose(g, who, g->cur_action) &&
		    (g->deck[list[i]].misc & MISC_FAKE))
		{
			/* Skip choice */
			continue;
		}

		/* Simulate game */
		simulate_game(&sim2, &sim, who);

		/* Set placement option */
		sim2.p[who].placing = list[i];

		/* Place card */
		place_card(&sim2, who, list[i]);

		/* Try current choice */
		if (phase == PHASE_DEVELOP)
		{
			/* Develop choice */
			develop_action(&sim2, who, list[i]);
		}
		else
		{
			/* Settle choice */
			settle_finish(&sim2, who, list[i], 0, special, 0);
			settle_extra(&sim2, who, list[i]);
		}

		/* Simulate rest of turn */
		complete_turn(&sim2, COMPLETE_ROUND);

		/* Get score */
		score = eval_game(&sim2, who);

#ifdef DEBUG
		if (!g->simulation)
		{
			printf("-- Score for %s: %f\n", g->deck[list[i]].d_ptr->name, score);
			dump_game(g, &sim2);
		}
#endif

		/* Check for better */
		if (score_better(score, b_s))
		{
			/* Track best */
			b_s = score;
			best = list[i];
		}
	}

	/* Return best choice */
	return best;
}

/*
 * Helper function for "ai_choose_pay" below.
 *
 * Here we try different combinations of discards to pay the remaining cost
 * of a played card.
 */
static void ai_choose_pay_aux2(game *g, int who, int which, int list[],
                               int special[], int num_special, int mil_only,
                               int mil_bonus, int n, int c, int chosen,
                               int chosen_special, int *best, int *best_special,
                               double *b_s)
{
	game sim;
	int payment[MAX_DECK], num_payment = 0, used[MAX_DECK], n_used = 0;
	double score;
	int i;

	/* Check for too few choices */
	if (c > n) return;

	/* Check for no more cards to try */
	if (!n)
	{
		/* Loop over chosen special cards */
		for (i = 0; i < num_special; i++)
		{
			/* Check for bit set */
			if (chosen_special & (1 << i))
			{
				/* Add card to list */
				used[n_used++] = special[i];
			}
		}

		/* Loop over chosen payment cards */
		for (i = 0; (1 << i) <= chosen; i++)
		{
			/* Check for bit set */
			if (chosen & (1 << i))
			{
				/* Add card to list */
				payment[num_payment++] = list[i];
			}
		}

		/* Simulate game */
		simulate_game(&sim, g, who);

		/* Attempt to pay */
		if (!payment_callback(&sim, who, which, payment, num_payment,
		                      used, n_used, mil_only, mil_bonus))
		{
			/* Illegal payment */
			return;
		}

		/* Simulate most of rest of turn */
		complete_turn(&sim, COMPLETE_ROUND);

		/* Evaluate result */
		score = eval_game(&sim, who);

		/* Check for better */
		if (score_better(score, *b_s))
		{
			/* Save best */
			*b_s = score;
			*best = chosen;
			*best_special = chosen_special;
		}

		/* Done */
		return;
	}

	/* Try without current card */
	ai_choose_pay_aux2(g, who, which, list, special, num_special,
	                   mil_only, mil_bonus, n - 1, c, chosen << 1,
	                   chosen_special, best, best_special, b_s);

	/* Try with current card */
	if (c) ai_choose_pay_aux2(g, who, which, list, special,
	                          num_special, mil_only, mil_bonus, n - 1,
	                          c - 1, (chosen << 1) + 1, chosen_special,
	                          best, best_special, b_s);
}

/*
 * Structure to hold calculated legal payment.
 */
struct legal_payment
{
	/* Chosen special cards */
	int chosen_special;

	/* Number of cards needed from hand */
	int needed;
};

/*
 * List of legal payments.
 */
static struct legal_payment payment_list[100];
int num_legal_payment;

/*
 * Helper function for "ai_choose_pay" below.
 *
 * Here we try different combinations of special abilities to pay for a
 * played card.
 */
static void ai_choose_pay_aux1(game *g, int who, int which, int list[], int num,
                               int special[], int num_special, int mil_only,
                               int mil_bonus, int next, int chosen_special,
                               int *best, int *best_special, double *b_s)
{
	game sim;
	int used[MAX_DECK], n_used = 0;
	int i, need;

	/* Check for no more special abilities to try */
	if (next == num_special)
	{
		/* Loop over chosen special cards */
		for (i = 0; i < num_special; i++)
		{
			/* Check for bit set */
			if (chosen_special & (1 << i))
			{
				/* Add card to list */
				used[n_used++] = special[i];
			}
		}

		/* Determine cards needed */
		need = needed_callback(g, who, which, used, n_used, mil_only,
		                       mil_bonus);

		/* Check for illegal combination */
		if (need < 0) return;

		/* Check for more cards needed than available */
		if (need > num) return;

		/* Check for simulated game */
		if (g->simulation)
		{
			/* Add payment to list */
			payment_list[num_legal_payment].chosen_special =
				chosen_special;
			payment_list[num_legal_payment].needed = need;
			num_legal_payment++;

#if 0
			/* Simulate game */
			simulate_game(&sim, g, who);

			/* Attempt to pay */
			if (!payment_callback(&sim, who, which, payment, need,
			                      used, n_used, mil_only))
			{
				/* Error */
				printf("Payment failed!\n");
				abort();
			}

			/* Simulate rest of current phase only */
			complete_turn(&sim, COMPLETE_PHASE);

			/* Evaluate result */
			score = eval_game(&sim, who);

			/* Check for better */
			if (score_better(score, *b_s))
			{
				/* Save best */
				*b_s = score;
				*best = (1 << need) - 1;
				*best_special = chosen_special;
			}
#endif

			/* Done with simulated game */
			return;
		}

		/* Simulate game */
		simulate_game(&sim, g, who);

		/* Loop over players who have not yet paid */
		for (i = who + 1; i < g->num_players; i++)
		{
			/* Check for no placement */
			if (g->p[i].placing == -1) continue;

			/* Check for develop phase */
			if (g->deck[which].d_ptr->type == TYPE_DEVELOPMENT)
			{
				/* Ask for development payment */
				develop_action(&sim, i, g->p[i].placing);
			}
			else
			{
				/* Ask for settle payment */
				settle_finish(&sim, i, g->p[i].placing, 0, -1,
					      0);

				/* Ask about future settle powers */
				settle_extra(&sim, i, g->p[i].placing);
			}
		}

		/* Try payment with different card combinations */
		ai_choose_pay_aux2(&sim, who, which, list, special, num_special,
				   mil_only, mil_bonus, num, need, 0,
		                   chosen_special, best, best_special, b_s);

		/* Done */
		return;
	}

	/* Try without current ability */
	ai_choose_pay_aux1(g, who, which, list, num, special, num_special,
	                   mil_only, mil_bonus, next + 1, chosen_special, best,
	                   best_special, b_s);

	/* Try with current ability */
	ai_choose_pay_aux1(g, who, which, list, num, special, num_special,
	                   mil_only, mil_bonus, next + 1,
	                   chosen_special | (1 << next), best, best_special,
	                   b_s);
}

/*
 * Choose method of payment.
 */
static void ai_choose_pay(game *g, int who, int which, int list[], int *num,
                          int special[], int *num_special, int mil_only,
                          int mil_bonus)
{
	game sim;
	double b_s = -1, score;
	int i, j, n = 0, n_used;
	int best = 0, best_special = 0, cs;
	int payment[MAX_DECK], used[MAX_DECK];

	/* XXX Don't look at more than 15 cards to pay with */
	if (*num > 15) *num = 15;

	/* Clear list of legal payments */
	num_legal_payment = 0;

	/* Find best set of special abilities */
	ai_choose_pay_aux1(g, who, which, list, *num, special, *num_special,
	                   mil_only, mil_bonus, 0, 0, &best, &best_special,
	                   &b_s);

	/* Check for only one payment strategy */
	if (b_s == -1 && num_legal_payment == 1)
	{
		/* Set payment */
		b_s = 0;
		best_special = payment_list[0].chosen_special;
		best = (1 << payment_list[0].needed) - 1;
	}

	/* Check for multiple payment strategies */
	if (b_s == -1 && num_legal_payment > 0)
	{
		/* Fill payment array with fake cards */
		for (i = 0; i < *num; i++) payment[i] = -1;

		/* Loop over strategies */
		for (i = 0; i < num_legal_payment; i++)
		{
			/* Get chosen special cards */
			cs = payment_list[i].chosen_special;

			/* Clear number of special cards used */
			n_used = 0;

			/* Loop over set of chosen special cards */
			for (j = 0; (1 << j) <= cs; j++)
			{
				/* Check for bit set */
				if (cs & (1 << j))
				{
					/* Select card */
					used[n_used++] = special[j];
				}
			}

			/* Simulate game */
			simulate_game(&sim, g, who);

			/* Attempt to pay */
			if (!payment_callback(&sim, who, which, payment,
					      payment_list[i].needed,
			                      used, n_used, mil_only,
			                      mil_bonus))
			{
				/* Error */
				display_error("Payment failed!\n");
				abort();
			}

			/* Check for game end */
			complete_turn(&sim, COMPLETE_CHECK);

			/* Evaluate result */
			score = eval_game(&sim, who);

			/* Check for better */
			if (score_better(score, b_s))
			{
				/* Save best */
				b_s = score;
				best = (1 << payment_list[i].needed) - 1;
				best_special = cs;
			}
		}
	}

	if (b_s == -1)
	{
		display_error("Couldn't find valid payment!\n");
		abort();
	}

	/* Clear number of special cards used */
	n_used = 0;

	/* Loop over set of chosen special cards */
	for (i = 0; (1 << i) <= best_special; i++)
	{
		/* Check for bit set */
		if (best_special & (1 << i))
		{
			/* Select card */
			special[n_used++] = special[i];
		}
	}

	/* Set number of special cards used */
	*num_special = n_used;

	/* Check for all cards needed */
	if (best == (1 << *num) - 1) return;

	/* Loop over set of chosen payment cards */
	for (i = 0; (1 << i) <= best; i++)
	{
		/* Check for bit set */
		if (best & (1 << i))
		{
			/* Check for simulated game */
			if (g->simulation)
			{
				/* Add fake card to list */
				list[n++] = -1;
			}
			else
			{
				/* Select card */
				list[n++] = list[i];
			}
		}
	}

	/* Set number of cards paid */
	*num = n;
}

/*
 * Choose settle power to use.
 */
static void ai_choose_settle(game *g, int who, int cidx[], int oidx[], int *num)
{
	/* XXX Just use first power */
	*num = 1;
}

/*
 * Choose world to takeover.
 */
static int ai_choose_takeover(game *g, int who, int list[], int *num,
                              int special[], int *num_special)
{
	game sim;
	double score, b_s;
	int best = -1, best_special = 0;
	int match;
	int i, j, k;

	/* Simulate game */
	simulate_game(&sim, g, who);

	/* Complete turn without choosing a takeover */
	complete_turn(&sim, COMPLETE_ROUND);

	/* Score game */
	b_s = eval_game(&sim, who);

	/* Loop over special cards (takeover powers) to use */
	for (i = 0; i < *num_special; i++)
	{
		/* Loop over eligible takeover targets */
		for (j = 0; j < *num; j++)
		{
			/* Assume no match */
			match = 0;

			/* Loop over previous takeover targets */
			for (k = 0; k < g->num_takeover; k++)
			{
				/* Check for world already chosen */
				if (list[j] == g->takeover_target[k]) match = 1;
			}

			/* Skip world if already chosen for takeover */
			if (match) continue;

			/* Simulate game */
			simulate_game(&sim, g, who);

			/* Ensure this power can be used against this world */
			if (!takeover_callback(&sim, special[i], list[j]))
				continue;

			/* Mark takeover */
			sim.takeover_target[sim.num_takeover] = list[j];
			sim.takeover_who[sim.num_takeover] = who;
			sim.takeover_power[sim.num_takeover] = special[i];
			sim.takeover_defeated[sim.num_takeover] = 0;
			sim.num_takeover++;

			/* Pay for extra military for this takeover */
			settle_finish(&sim, who, -1, 0, -1, 0);

			/* Complete turn */
			complete_turn(&sim, COMPLETE_ROUND);

			/* Score game */
			score = eval_game(&sim, who);

			/* Check for better than before */
			if (score_better(score, b_s))
			{
				/* Track best option */
				best = list[j];
				best_special = special[i];
				b_s = score;
			}
		}
	}

	/* Use best special */
	special[0] = best_special;

	/* Return best world */
	return best;
}

/*
 * Helper function for "ai_choose_defend" below.
 *
 * Here we try different combinations of discards to pay for special abilities
 * chosen in "ai_choose_defend_aux1" below.
 */
static void ai_choose_defend_aux2(game *g, int who, int which, int opponent,
                                  int deficit, int list[], int num,
                                  int special[], int num_special,
                                  int next, int chosen, int chosen_special,
                                  int *best, int *best_special, double *b_s)
{
	game sim;
	card *c_ptr;
	int payment[MAX_DECK], used[MAX_DECK], n = 0, n_used = 0;
	double score;
	int i, x, rv;

	/* Check for simulated game */
	if (g->simulation)
	{
		/* Fill payment array with fake cards */
		for (i = 0; i < num; i++) payment[i] = -1;

		/* Loop over chosen special cards */
		for (i = 0; i < num_special; i++)
		{
			/* Check for bit set */
			if (chosen_special & (1 << i))
			{
				/* Add card to list */
				used[n_used++] = special[i];
			}
		}

		/* Try varying number of payment cards */
		for (i = 0; i <= num; i++)
		{
			/* Simulate game */
			simulate_game(&sim, g, who);

			/* Attempt to defend */
			rv = defend_callback(&sim, who, deficit, payment, i,
			                     used, n_used);

			/* Skip illegal combinations */
			if (!rv) continue;

			/* Check for failure to defend */
			if (rv == 1)
			{
				/* Get card pointer */
				c_ptr = &sim.deck[which];

				/* Move card to opponent */
				move_card(&sim, which, opponent, WHERE_ACTIVE);

				/* Check for good on card */
				if (c_ptr->num_goods)
				{
					/* Start at first good */
					x = sim.p[who].head[WHERE_GOOD];

					/* Loop over goods */
					for ( ; x != -1; x = sim.deck[x].next)
					{
						/* Check for covering good */
						if (sim.deck[x].covering ==
						    which)
						{
							/* Move good as well */
							move_card(&sim, x,
								  opponent,
								  WHERE_GOOD);
						}
					}
				}
			}

			/* Evaluate result */
			score = eval_game(&sim, who);

			/* Check for better */
			if (score_better(score, *b_s))
			{
				/* Save best */
				*b_s = score;
				*best = (1 << i) - 1;
				*best_special = chosen_special;
			}
		}

		/* Done */
		return;
	}

	/* Check for no more cards to try */
	if (next == num)
	{
		/* Loop over chosen special cards */
		for (i = 0; i < num_special; i++)
		{
			/* Check for bit set */
			if (chosen_special & (1 << i))
			{
				/* Add card to list */
				used[n_used++] = special[i];
			}
		}

		/* Loop over chosen payment cards */
		for (i = 0; i < num; i++)
		{
			/* Check for bit set */
			if (chosen & (1 << i))
			{
				/* Add card to list */
				payment[n++] = list[i];
			}
		}

		/* Simulate game */
		simulate_game(&sim, g, who);

		/* Attempt to defend */
		rv = defend_callback(&sim, who, deficit, payment, n, used,
		                     n_used);

		/* Skip illegal combinations */
		if (!rv) return;

		/* Check for failure to defend */
		if (rv == 1)
		{
			/* Get card pointer */
			c_ptr = &sim.deck[which];

			/* Move card to opponent */
			move_card(&sim, which, opponent, WHERE_ACTIVE);

			/* Check for good on card */
			if (c_ptr->num_goods)
			{
				/* Start at first good */
				x = sim.p[who].head[WHERE_GOOD];

				/* Loop over goods */
				for ( ; x != -1; x = sim.deck[x].next)
				{
					/* Check for covering good */
					if (sim.deck[x].covering == which)
					{
						/* Move good as well */
						move_card(&sim, x, opponent,
							  WHERE_GOOD);
					}
				}
			}
		}

		/* Evaluate result */
		score = eval_game(&sim, who);

		/* Check for better */
		if (score_better(score, *b_s))
		{
			/* Save best */
			*b_s = score;
			*best = chosen;
			*best_special = chosen_special;
		}

		/* Done */
		return;
	}

	/* Try without current card */
	ai_choose_defend_aux2(g, who, which, opponent, deficit, list, num,
	                      special, num_special, next + 1, chosen,
	                      chosen_special, best, best_special, b_s);

	/* Try with current card */
	ai_choose_defend_aux2(g, who, which, opponent, deficit, list, num,
	                      special, num_special, next + 1,
	                      chosen | (1 << next), chosen_special,
	                      best, best_special, b_s);
}


/*
 * Helper function for "ai_choose_defend" below.
 *
 * Here we try different combinations of special abilities to defend
 * against a takeover.
 */
static void ai_choose_defend_aux1(game *g, int who, int which, int opponent,
                                  int deficit, int list[], int num,
                                  int special[], int num_special,
                                  int next, int chosen_special,
                                  int *best, int *best_special, double *b_s)
{
	/* Check for no more special abilities to try */
	if (next == num_special)
	{
		/* Try payment with different card combinations */
		ai_choose_defend_aux2(g, who, which, opponent, deficit, list,
		                      num, special, num_special, 0, 0,
		                      chosen_special, best, best_special, b_s);

		/* Done */
		return;
	}

	/* Try without current ability */
	ai_choose_defend_aux1(g, who, which, opponent, deficit, list, num,
	                      special, num_special, next + 1, chosen_special,
	                      best, best_special, b_s);

	/* Try with current ability */
	ai_choose_defend_aux1(g, who, which, opponent, deficit, list, num,
	                      special, num_special, next + 1,
	                      chosen_special | (1 << next), best, best_special,
	                      b_s);
}
/*
 * Choose a method to defend against a takeover.
 */
static void ai_choose_defend(game *g, int who, int which, int opponent,
                             int deficit, int list[], int *num,
                             int special[], int *num_special)
{
	double b_s = -1;
	int n = 0, n_used = 0;
	int best = 0, best_special = 0, i;

	/* XXX Don't look at more than 30 cards to pay with */
	if (*num > 30) *num = 30;

	/* Find best set of special abilities */
	ai_choose_defend_aux1(g, who, which, opponent, deficit, list, *num,
	                      special, *num_special, 0, 0, &best, &best_special,
	                      &b_s);

	if (b_s == -1)
	{
		display_error("Couldn't find valid payment!\n");
		abort();
	}

	/* Loop over set of chosen special cards */
	for (i = 0; (1 << i) <= best_special; i++)
	{
		/* Check for bit set */
		if (best_special & (1 << i))
		{
			/* Select card */
			special[n_used++] = special[i];
		}
	}

	/* Set number of special cards used */
	*num_special = n_used;

	/* Loop over set of chosen payment cards */
	for (i = 0; (1 << i) <= best; i++)
	{
		/* Check for bit set */
		if (best & (1 << i))
		{
			/* Select card */
			list[n++] = list[i];
		}
	}

	/* Set number of cards used */
	*num = n;
}

/*
 * Choose whether to prevent a takeover (and which to prevent).
 */
static void ai_choose_takeover_prevent(game *g, int who, int list[], int *num,
                                       int special[])
{
	game sim;
	double b_s, score;
	int i, b_i = -1;

	/* Simulate game */
	simulate_game(&sim, g, who);

	/* Resolve takeovers as-is */
	resolve_takeovers(&sim);

	/* Get score */
	b_s = eval_game(&sim, who);

	/* Loop over choices */
	for (i = 0; i < *num; i++)
	{
		/* Simulate game */
		simulate_game(&sim, g, who);

		/* Defeat this takeover */
		sim.takeover_defeated[i] = 1;

		/* Spend a prestige */
		sim.p[who].prestige--;

		/* Resolve takeovers */
		resolve_takeovers(&sim);

		/* Get score */
		score = eval_game(&sim, who);

		/* Check for better than before */
		if (score_better(score, b_s))
		{
			/* Track best score */
			b_s = score;
			b_i = i;
		}
	}

	/* Check for no defeat */
	if (b_i == -1)
	{
		/* Clear takeover to defeat */
		*num = 0;
		return;
	}

	/* Set takeover to defeat */
	list[0] = list[b_i];
	special[0] = special[b_i];
	*num = 1;
}

/*
 * Choose a world to upgrade (if any).
 */
static void ai_choose_upgrade(game *g, int who, int list[], int *num,
                              int special[], int *num_special)
{
	game sim;
	double b_s, score;
	int b_i = -1, b_j = -1;
	int i, j;

	/* Get score for no upgrade */
	b_s = eval_game(g, who);

	/* Loop over choices of active cards */
	for (i = 0; i < *num_special; i++)
	{
		/* Loop over choices of cards in hand */
		for (j = 0; j < *num; j++)
		{
			/* Simulate game */
			simulate_game(&sim, g, who);

			/* Attempt upgrade */
			if (!upgrade_chosen(&sim, who, list[j], special[i]))
			{
				/* Try next */
				continue;
			}

			/* Get score of this upgrade */
			score = eval_game(&sim, who);

			/* Check for better */
			if (score_better(score, b_s))
			{
				/* Track best */
				b_s = score;
				b_i = i;
				b_j = j;
			}
		}
	}

	/* Check for no upgrade chosen */
	if (b_i == -1)
	{
		/* Clear choice */
		*num = *num_special = 0;
		return;
	}

	/* Set choice */
	list[0] = list[b_j];
	special[0] = special[b_i];
	*num = *num_special = 1;
}

/*
 * Compare two good (worlds) to see if they are either identical or the
 * second is preferred for consuming/trading.
 */
static int good_better(game *g, int good1, int good2)
{
	card *c_ptr1, *c_ptr2;
	int w1 = 0, w2 = 0;
	int d1 = 0, d2 = 0, t1 = 0, t2 = 0;
	power *o_ptr;
	int i;

	/* Get card pointers */
	c_ptr1 = &g->deck[good1];
	c_ptr2 = &g->deck[good2];

	/* Check for different good type */
	if (c_ptr1->d_ptr->good_type != c_ptr2->d_ptr->good_type) return 0;

	/* Get windfall status */
	w1 = c_ptr1->d_ptr->flags & FLAG_WINDFALL;
	w2 = c_ptr2->d_ptr->flags & FLAG_WINDFALL;

	/* Loop over powers on first card */
	for (i = 0; i < c_ptr1->d_ptr->num_power; i++)
	{
		/* Get power pointer */
		o_ptr = &c_ptr1->d_ptr->powers[i];

		/* Look for "trade this good" power */
		if (o_ptr->phase == PHASE_CONSUME &&
		    (o_ptr->code & P4_TRADE_THIS))
		{
			/* Store trade power */
			t1 = o_ptr->value;
		}

		/* Look for "draw if produced" power */
		if (o_ptr->phase == PHASE_PRODUCE &&
		    (o_ptr->code & P5_DRAW_IF))
		{
			/* Store draw power */
			d1 = o_ptr->value;
		}
	}

	/* Loop over powers on second card */
	for (i = 0; i < c_ptr2->d_ptr->num_power; i++)
	{
		/* Get power pointer */
		o_ptr = &c_ptr2->d_ptr->powers[i];

		/* Look for "trade this good" power */
		if (o_ptr->phase == PHASE_CONSUME &&
		    (o_ptr->code & P4_TRADE_THIS))
		{
			/* Store trade power */
			t2 = o_ptr->value;
		}

		/* Look for "draw if produced" power */
		if (o_ptr->phase == PHASE_PRODUCE &&
		    (o_ptr->code & P5_DRAW_IF))
		{
			/* Store draw power */
			d2 = o_ptr->value;
		}
	}

	/* Check for preferred second good */
	if (w1 == w2 && d2 > d1) return 2;
	if (w1 && !w2 && d1 <= d2 && t1 == t2) return 2;

	/* Check for difference */
	if (w1 != w2 || t1 != t2 || d1 != d2) return 0;

	/* Goods are identical */
	return 1;
}

/*
 * Condense a list of goods, removing identical goods or ones inferior to
 * others in the list.
 */
static int condense_goods(game *g, int list[], int num)
{
	int skip[MAX_DECK];
	int i, j, result;

	/* Clear skip list */
	for (i = 0; i < num; i++) skip[i] = 0;

	/* Loop over goods */
	for (i = 0; i < num; i++)
	{
		/* Loop over goods again */
		for (j = 0; j < num; j++)
		{
			/* Don't compare good with itself */
			if (i == j) continue;

			/* Compare goods */
			result = good_better(g, list[i], list[j]);

			/* Check for better good */
			if (result == 2) skip[i] = 1;

			/* Check for identical earlier good */
			if (i < j && result == 1) skip[i] = 1;
		}
	}

	/* Loop over goods */
	for (i = 0; i < num; i++)
	{
		/* Check for skipped good */
		if (skip[i])
		{
			/* Copy last good to current spot */
			list[i] = list[num - 1];

			/* Copy skip flag */
			skip[i] = skip[num - 1];

			/* One less good */
			num--;

			/* Recheck current spot */
			i--;
		}
	}

	/* Return new length of list */
	return num;
}

/*
 * Choose good to trade.
 */
static void ai_choose_trade(game *g, int who, int list[], int *num,
                            int no_bonus)
{
	game sim;
	int i, best = -1;
	double score, b_s = -1;

	/* Condense good list */
	*num = condense_goods(g, list, *num);

	/* Loop over choices */
	for (i = 0; i < *num; i++)
	{
		/* Simulate game */
		simulate_game(&sim, g, who);

		/* Try trading this good */
		trade_chosen(&sim, who, list[i], no_bonus);

		/* Check for simulated opponent's turn */
		if (g->simulation && g->sim_who != who)
		{
			/* Score based on cards received */
			score = sim.p[who].fake_hand;
		}
		else
		{
			/* Check for real trade */
			if (!g->simulation)
			{
				/* Use remaining consume powers */
				while (consume_action(&sim, who));

				/* Simulate rest of turn */
				complete_turn(&sim, COMPLETE_ROUND);
			}

			/* Get score */
			score = eval_game(&sim, who);
		}

		/* Check for better */
		if (score_better(score, b_s))
		{
			/* Track best */
			b_s = score;
			best = list[i];
		}
	}

	/* No good play */
	if (best == -1)
	{
		/* Error */
		display_error("Could not find trade\n");
		abort();
	}

	/* Set best choice */
	list[0] = best;
	*num = 1;
}

/*
 * Provide rough score of consume powers for simulating opponents' choices.
 */
static int score_consume(game *g, int who, int c_idx, int o_idx)
{
	card *c_ptr;
	power *o_ptr;
	int vp = 0, card = 0, goods = 1;
	int score;

	/* Check for bonus power from prestige trade */
	if (c_idx < 0) return 0;

	/* Get card pointer */
	c_ptr = &g->deck[c_idx];

	/* Get power pointer */
	o_ptr = &c_ptr->d_ptr->powers[o_idx];

	/* Always discard from hand last */
	if (o_ptr->code & P4_DISCARD_HAND) return 0;

	/* Check for VP awarded */
	if (o_ptr->code & P4_GET_VP) vp += o_ptr->value;

	/* Check for card awarded */
	if (o_ptr->code & P4_GET_CARD) card += o_ptr->value;

	/* Check for cards awarded */
	if (o_ptr->code & P4_GET_2_CARD) card += o_ptr->value * 2;
	if (o_ptr->code & P4_GET_3_CARD) card += o_ptr->value * 3;

	/* Assume trade will earn 4 cards */
	if (o_ptr->code & P4_TRADE_ACTION) card += 4;

	/* Assume trade without bonus will earn fewer cards */
	if (o_ptr->code & P4_TRADE_NO_BONUS) card--;

	/* Check for consuming two goods */
	if (o_ptr->code & P4_CONSUME_TWO) goods = 2;

	/* Check for consuming three goods */
	if (o_ptr->code & P4_CONSUME_3_DIFF) goods = 3;

	/* Check for consuming all goods */
	if (o_ptr->code & P4_CONSUME_ALL) goods = 4;

	/* Check for "Consume x2" chosen */
	if (player_chose(g, who, ACT_CONSUME_X2)) vp *= 2;

	/* Compute score */
	score = (vp * 110 + card * 50) / goods;

	/* Use specific consume powers first */
	if (!(o_ptr->code & P4_CONSUME_ANY)) score += 6 * o_ptr->times;

	/* Use multi-use powers later */
	if (o_ptr->times > 1) score -= 5 * o_ptr->times;

	/* Return score */
	return score;
}

/*
 * Mask of consume good type flags.
 */
#define CONSUME_TYPE_MASK (P4_CONSUME_ANY | P4_CONSUME_NOVELTY | \
                           P4_CONSUME_RARE | P4_CONSUME_GENE | P4_CONSUME_ALIEN)

/*
 * Choose consume power to use.
 */
static void ai_choose_consume(game *g, int who, int cidx[], int oidx[],
                              int *num, int optional)
{
	game sim;
	card *c_ptr, *b_ptr;
	power *o_ptr, *n_ptr;
	int code1, code2;
	int type1, type2;
	int cards1, cards2;
	int i, j, best = -1, skip[100];
	double score, b_s = -1;

	/* Check for simple powers */
	for (i = 0; i < *num; i++)
	{
		/* Skip special prestige bonus power */
		if (cidx[i] < 0) continue;

		/* Get card pointer */
		c_ptr = &g->deck[cidx[i]];

		/* Get power pointer */
		o_ptr = &c_ptr->d_ptr->powers[oidx[i]];

		/* Check for powers that should always be used first */
		if ((o_ptr->code & P4_DRAW) ||
		    (o_ptr->code & P4_DRAW_LUCKY) ||
		    (o_ptr->code & P4_VP))
		{
			/* Select power */
			cidx[0] = cidx[i];
			oidx[0] = oidx[i];

			/* Done */
			return;
		}
	}

	/* Check for simulated opponent's turn */
	if (g->simulation && who != g->sim_who)
	{
		/* Loop over choices */
		for (i = 0; i < *num; i++)
		{
			/* Get score */
			score = score_consume(g, who, cidx[i], oidx[i]);

			/* Check for better */
			if (score > b_s)
			{
				/* Track best */
				b_s = score;
				best = i;
			}
		}

		/* Use best option */
		cidx[0] = cidx[best];
		oidx[0] = oidx[best];

		/* Done */
		return;
	}

	/* Clear skip array */
	for (i = 0; i < *num; i++) skip[i] = 0;

	/* Loop over powers */
	for (i = 0; i < *num; i++)
	{
		/* Do not check for skippable powers in simulated games */
		if (g->simulation) break;

		/* Skip bonus power from prestige trade */
		if (cidx[i] < 0) continue;

		/* Get card pointer */
		c_ptr = &g->deck[cidx[i]];

		/* Get power pointer */
		o_ptr = &c_ptr->d_ptr->powers[oidx[i]];

		/* Do not compare unusual powers */
		if (o_ptr->code & (P4_DISCARD_HAND | P4_ANTE_CARD |
		                   P4_CONSUME_PRESTIGE | P4_CONSUME_ALL |
		                   P4_CONSUME_3_DIFF | P4_CONSUME_N_DIFF))
		{
			/* Skip power */
			continue;
		}

		/* Loop over other powers */
		for (j = 0; j < *num; j++)
		{
			/* Skip first power */
			if (i == j) continue;

			/* Ignore already skipped powers */
			if (skip[j]) continue;

			/* Skip bonus power from prestige trade */
			if (cidx[j] < 0) continue;

			/* Get card pointer */
			b_ptr = &g->deck[cidx[j]];

			/* Get power pointer */
			n_ptr = &b_ptr->d_ptr->powers[oidx[j]];

			/* Do not compare unusual powers */
			if (n_ptr->code & (P4_DISCARD_HAND | P4_ANTE_CARD |
					   P4_CONSUME_PRESTIGE |
					   P4_CONSUME_ALL | P4_CONSUME_3_DIFF |
					   P4_CONSUME_N_DIFF))
			{
				/* Skip power */
				continue;
			}

			/* Get power codes */
			code1 = o_ptr->code;
			code2 = n_ptr->code;

			/* Check for identical powers */
			if (code1 == code2 &&
			    o_ptr->value == n_ptr->value &&
			    o_ptr->times == n_ptr->times)
			{
				/* Skip power */
				skip[i] = 1;
				break;
			}

			/* Check for better trade power */
			if ((code1 & P4_TRADE_ACTION) &&
			    (code2 & P4_TRADE_ACTION) &&
			    (code1 & P4_TRADE_NO_BONUS))
			{
				/* Skip power */
				skip[i] = 1;
				break;
			}

			/* Stop looking at trade actions */
			if (code1 & P4_TRADE_ACTION) continue;
			if (code2 & P4_TRADE_ACTION) continue;

			/* Check for more reward */
			if (code1 == code2 &&
			    o_ptr->times == n_ptr->times &&
			    o_ptr->value < n_ptr->value)
			{
				/* Skip power */
				skip[i] = 1;
				break;
			}

			/* Check for identical reward but more times */
			if (code1 == code2 &&
			    o_ptr->value == n_ptr->value &&
			    o_ptr->times > n_ptr->times)
			{
				/* Skip power */
				skip[i] = 1;
				break;
			}

			/* Stop looking if times and value not identical */
			if (o_ptr->times != n_ptr->times ||
			    o_ptr->value != n_ptr->value) continue;

			/* Get consume types */
			type1 = code1 & CONSUME_TYPE_MASK;
			type2 = code2 & CONSUME_TYPE_MASK;

			/* Strip out consume types */
			code1 &= ~CONSUME_TYPE_MASK;
			code2 &= ~CONSUME_TYPE_MASK;

			/* Assume no cards rewards */
			cards1 = cards2 = 0;

			/* Check for 1 card reward */
			if (code1 & P4_GET_CARD) cards1 = 1;
			if (code2 & P4_GET_CARD) cards2 = 1;

			/* Check for 2 card reward */
			if (code1 & P4_GET_2_CARD) cards1 = 2;
			if (code2 & P4_GET_2_CARD) cards2 = 2;

			/* Check for 3 card reward */
			if (code1 & P4_GET_3_CARD) cards1 = 3;
			if (code2 & P4_GET_3_CARD) cards2 = 3;

			/* Strip card rewards from codes */
			code1 &= ~(P4_GET_CARD | P4_GET_2_CARD | P4_GET_3_CARD);
			code2 &= ~(P4_GET_CARD | P4_GET_2_CARD | P4_GET_3_CARD);

			/* Check for better card reward */
			if (code1 == code2 &&
			    type1 == type2 &&
			    cards1 < cards2)
			{
				/* Skip power */
				skip[i] = 1;
				break;
			}

			/* Check for better reward */
			if ((type1 == type2) &&
			    (cards1 == cards2) &&
			    ((code2 == (code1 | P4_GET_VP)) ||
			     (code2 == (code1 | P4_GET_PRESTIGE))))
			{
				/* Skip power */
				skip[i] = 1;
				break;
			}
		}
	}

	/* Loop over choices */
	for (i = 0; i < *num; i++)
	{
		/* Check for skip */
		if (skip[i]) continue;

		/* Check for bonus power from prestige trade */
		if (cidx[i] < 0) continue;

		/* Get card pointer */
		c_ptr = &g->deck[cidx[i]];

		/* Get power pointer */
		o_ptr = &c_ptr->d_ptr->powers[oidx[i]];

		/* Save optional powers for last */
		if (o_ptr->code & P4_DISCARD_HAND) continue;
		if (o_ptr->code & P4_CONSUME_PRESTIGE) continue;

		/* Simulate game */
		simulate_game(&sim, g, who);

		/* Try using current consume power */
		consume_chosen(&sim, who, cidx[i], oidx[i]);

		/* Check for real game */
		if (!g->simulation)
		{
			/* Use remaining consume powers */
			while (consume_action(&sim, who));

			/* Simulate rest of turn */
			complete_turn(&sim, COMPLETE_ROUND);
		}

		/* Evaluate end state */
		score = eval_game(&sim, who);

		/* Check for better */
		if (score_better(score, b_s))
		{
			/* Track best */
			b_s = score;
			best = i;
		}
	}

	/* Check for nothing tried */
	if (b_s == -1)
	{
		/* Simulate game */
		simulate_game(&sim, g, who);

		/* Check for real game */
		if (!g->simulation)
		{
			/* Simulate rest of turn */
			complete_turn(&sim, COMPLETE_ROUND);
		}

		/* Get score for choosing no power */
		b_s = eval_game(&sim, who);

		/* Loop over choices */
		for (i = 0; i < *num; i++)
		{
			/* Simulate game */
			simulate_game(&sim, g, who);

			/* Try using current consume power */
			consume_chosen(&sim, who, cidx[i], oidx[i]);

			/* Check for real game */
			if (!g->simulation)
			{
				/* Use remaining consume powers */
				while (consume_action(&sim, who));

				/* Simulate rest of turn */
				complete_turn(&sim, COMPLETE_ROUND);
			}

			/* Evaluate end state */
			score = eval_game(&sim, who);

			/* Check for better */
			if (score_better(score, b_s))
			{
				/* Track best */
				b_s = score;
				best = i;
			}
		}
	}

	/* Check for no power */
	if (best == -1)
	{
		if (!optional)
		{
			display_error("Selected no power, but some are mandatory!\n");
			abort();
		}
		/* Select nothing */
		*num = 0;
		return;
	}

	/* Select best option */
	cidx[0] = cidx[best];
	oidx[0] = oidx[best];
	*num = 1;
}

/*
 * Helper function for ai_choose_consume_hand().
 */
static void ai_choose_consume_hand_aux(game *g, int who, int c_idx, int o_idx,
                                       int list[], int n, int c, int chosen,
                                       int *best, double *b_s)
{
	game sim;
	double score;
	int discards[MAX_DECK], num_discards = 0;
	int i;

	/* Check for too few choices */
	if (c > n) return;

	/* Check for end */
	if (!n)
	{
		/* Loop over chosen cards */
		for (i = 0; (1 << i) <= chosen; i++)
		{
			/* Check for bit set */
			if (chosen & (1 << i))
			{
				/* Add card to list */
				discards[num_discards++] = list[i];
			}
		}

		/* Copy game */
		simulate_game(&sim, g, who);

		/* Apply choice */
		consume_hand_chosen(&sim, who, c_idx, o_idx,
		                    discards, num_discards);

		/* Use remaining consume powers */
		while (consume_action(&sim, who));

		/* Simulate rest of turn */
		complete_turn(&sim, COMPLETE_ROUND);

		/* Evaluate result */
		score = eval_game(&sim, who);

		/* Check for better score */
		if (score_better(score, *b_s))
		{
			/* Save better choice */
			*b_s = score;
			*best = chosen;
		}

		/* Done */
		return;
	}

	/* Try without current card */
	ai_choose_consume_hand_aux(g, who, c_idx, o_idx, list, n - 1, c,
	                           chosen << 1, best, b_s);

	/* Try with current card (if more can be chosen) */
	if (c) ai_choose_consume_hand_aux(g, who, c_idx, o_idx, list,n - 1,
	                                  c - 1, (chosen << 1) + 1, best, b_s);
}

/*
 * Choose card from hand to consume.
 */
static void ai_choose_consume_hand(game *g, int who, int c_idx, int o_idx,
                                   int list[], int *num)
{
	player *p_ptr;
	card *c_ptr;
	power *o_ptr, prestige_bonus;
	double b_s = -1;
	int n = 0;
	int best, i;

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* XXX Check for prestige trade power */
	if (c_idx < 0)
	{
		/* Make fake power */
		prestige_bonus.phase = PHASE_CONSUME;
		prestige_bonus.code = P4_DISCARD_HAND | P4_GET_VP;
		prestige_bonus.value = 1;
		prestige_bonus.times = 2;

		/* Use fake power */
		o_ptr = &prestige_bonus;
	}
	else
	{
		/* Get card pointer */
		c_ptr = &g->deck[c_idx];

		/* Get power pointer */
		o_ptr = &c_ptr->d_ptr->powers[o_idx];
	}

	/* Check for simulation */
	if (g->simulation)
	{
		/* Compute hand size */
		n = count_player_area(g, who, WHERE_HAND) +
		    p_ptr->fake_hand - p_ptr->fake_discards;

		/* Maximum number of discards */
		if (n > o_ptr->times) n = o_ptr->times;

		/* Discard from hand */
		p_ptr->fake_discards += n;

		/* Get reward */
		if (o_ptr->code & P4_GET_VP)
		{
			/* Award VPs */
			p_ptr->vp += n;
			g->vp_pool -= n;
		}
		else if (o_ptr->code & P4_GET_CARD)
		{
			/* Draw cards */
			draw_cards(g, who, n, NULL);
		}

		/* Done */
		*num = 0;
		return;
	}

	/* XXX - Check for more than 30 cards to choose from */
	if (*num > 30)
	{
		/* XXX - Just discard first cards */
		*num = o_ptr->times;

		/* Done */
		return;
	}

	/* Loop over number of cards discardable */
	for (i = 0; i <= o_ptr->times; i++)
	{
		/* Find best set of cards */
		ai_choose_consume_hand_aux(g, who, c_idx, o_idx, list, *num,
		                           i, 0, &best, &b_s);
	}

	/* Check for failure */
	if (b_s == -1)
	{
		/* Error */
		display_error("Failed to find good discard set!\n");
		abort();
	}

	/* Loop over set of chosen cards */
	for (i = 0; (1 << i) <= best; i++)
	{
		/* Check for bit set */
		if (best & (1 << i))
		{
			/* Add card to list */
			list[n++] = list[i];
		}
	}

	/* Record number of cards chosen */
	*num = n;
}

/*
 * Helper function for ai_choose_good().
 */
static void ai_choose_good_aux(game *g, int who, int list[], int n, int c,
                               int chosen, int c_idx, int o_idx,
                               int min, int max,
                               int *best, double *b_s)
{
	game sim;
	double score;
	int consume[MAX_DECK], num_consume = 0;
	int i;

	/* Check for too few choices */
	if (c > n) return;

	/* Check for end */
	if (!n)
	{
		/* Loop over chosen cards */
		for (i = 0; (1 << i) <= chosen; i++)
		{
			/* Check for bit set */
			if (chosen & (1 << i))
			{
				/* Add card to list */
				consume[num_consume++] = list[i];
			}
		}

		/* Copy game */
		simulate_game(&sim, g, who);

		/* Apply choice */
		if (!good_chosen(&sim, who, c_idx, o_idx, min, max,
		                 consume, num_consume))
		{
			/* Illegal choice */
			return;
		}

		/* Evaluate result */
		score = eval_game(&sim, who);

		/* Check for better score */
		if (score_better(score, *b_s))
		{
			/* Save better choice */
			*b_s = score;
			*best = chosen;
		}

		/* Done */
		return;
	}

	/* Try without current good */
	ai_choose_good_aux(g, who, list, n - 1, c, chosen << 1, c_idx, o_idx,
	                   min, max, best, b_s);

	/* Try with current good (if more can be chosen) */
	if (c) ai_choose_good_aux(g, who, list, n - 1, c - 1,
	                          (chosen << 1) + 1, c_idx, o_idx,
	                          min, max, best, b_s);
}

/*
 * Choose goods to consume.
 */
static void ai_choose_good(game *g, int who, int c_idx, int o_idx,
                           int goods[], int *num, int min, int max)
{
	double b_s = -1;
	int c, n = 0;
	int best, i;

	/* Check for simulated game and opponent's turn */
	if (g->simulation && who != g->sim_who)
	{
		/* Use first goods */
		*num = max;

		/* Done */
		return;
	}

	/* Remove identical goods from list if only needing one */
	if (max == 1)
	{
		/* Condense good list */
		*num = condense_goods(g, goods, *num);
	}

	/* Check for needing all goods */
	if (*num == min && *num == max)
	{
		/* Done */
		return;
	}

	/* Loop over allowed number of goods */
	for (c = min; c <= max; c++)
	{
		/* Find best set of goods */
		ai_choose_good_aux(g, who, goods, *num, c, 0, c_idx, o_idx,
		                   min, max, &best, &b_s);
	}

	/* Check for failure */
	if (b_s == -1)
	{
		/* Error */
		display_error("Failed to find consume set!\n");
		abort();
	}

	/* Loop over set of chosen cards */
	for (i = 0; (1 << i) <= best; i++)
	{
		/* Check for bit set */
		if (best & (1 << i))
		{
			/* Add card to list */
			goods[n++] = goods[i];
		}
	}

	/* Set number of goods used */
	*num = n;
}

/*
 * Choose lucky number.
 */
static int ai_choose_lucky(game *g, int who)
{
	game sim;
	card *c_ptr;
	double score, base, b_s = -1;
	int i, j, b_c = -1, count = 0;

	/* Don't check probabilities in simulated game */
	if (g->simulation) return 1;

	/* Count unknown cards in deck */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Skip known cards */
		if (c_ptr->misc & (1 << who)) continue;

		/* Count card */
		count++;
	}

	/* Get base score if we choose wrong */
	base = eval_game(g, who);

	/* Loop over legal cost choices */
	for (i = 1; i <= 7; i++)
	{
		/* Clear score */
		score = 0;

		/* Loop over cards */
		for (j = 0; j < g->deck_size; j++)
		{
			/* Get card pointer */
			c_ptr = &g->deck[j];

			/* Skip known cards */
			if (c_ptr->misc & (1 << who)) continue;

			/* Check for wrong cost */
			if (c_ptr->d_ptr->cost != i)
			{
				/* Add base score */
				score += base / count;

				/* Next card */
				continue;
			}

			/* Simulate game */
			simulate_game(&sim, g, who);

			/* Add card to hand */
			move_card(&sim, j, who, WHERE_HAND);

			/* Add score */
			score += eval_game(&sim, who) / count;
		}

		/* Check for better score */
		if (score_better(score, b_s))
		{
			/* Track best */
			b_s = score;
			b_c = i;
		}
	}

	/* Return best cost chance */
	return b_c;
}

/*
 * Combinations of n choose k.
 */
static unsigned long long choose(int n, int k)
{
	unsigned long long r = 1;
	int i;

	/* Degenerate case */
	if (k > n) return 0;

	/* Pick smaller symmetry */
	if (k > n / 2) k = n - k;

	/* Loop over k's */
	for (i = 1; i <= k; i++)
	{
		/* Accumulate combinations */
		r = r * (n - k + i) / i;
	}

	/* Return result */
	return r;
}

/*
 * Choose ante card.
 */
static int ai_choose_ante(game *g, int who, int list[], int num)
{
	game sim;
	card *c_ptr;
	double score, chance, b_s = -1;
	int i, j, b_i = -1, count = 0, num_win;
	int cost;

	/* Don't ante in simulated game */
	if (g->simulation) return -1;

	/* Count unknown cards in deck */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Skip known cards */
		if (c_ptr->misc & (1 << who)) continue;

		/* Count card */
		count++;
	}

	/* Get base score if we choose nothing */
	b_s = eval_game(g, who);

	/* Loop over card choices */
	for (i = 0; i < num; i++)
	{
		/* Get card cost */
		cost = g->deck[list[i]].d_ptr->cost;

		/* Assume no more expensive cards available */
		num_win = 0;

		/* Count unknown cards in deck */
		for (j = 0; j < g->deck_size; j++)
		{
			/* Get card pointer */
			c_ptr = &g->deck[j];

			/* Skip known cards */
			if (c_ptr->misc & (1 << who)) continue;

			/* Check for more expensive card */
			if (c_ptr->d_ptr->cost > cost) num_win++;
		}

		/* Get chance of losing */
		chance = 1.0 * choose(count - num_win, cost) /
		               choose(count, cost);

		/* Simulate game */
		simulate_game(&sim, g, who);

		/* Assume we lose the card */
		move_card(&sim, list[i], -1, WHERE_DISCARD);

		/* Start with losing chance */
		score = chance * eval_game(&sim, who);

		/* Simulate game */
		simulate_game(&sim, g, who);

		/* Assume we win a card */
		draw_card(&sim, who, NULL);

		/* Accumulate score */
		score += (1.0 - chance) * eval_game(&sim, who);

		/* Check for better score */
		if (score_better(score, b_s))
		{
			/* Track best score and choice */
			b_s = score;
			b_i = list[i];
		}
	}

	/* Return best chance */
	return b_i;
}

/*
 * Choose card to keep after successful gamble.
 */
static int ai_choose_keep(game *g, int who, int list[], int num)
{
	game sim;
	double score, b_s = -1;
	int i, b_i = -1;

	/* Loop over choices */
	for (i = 0; i < num; i++)
	{
		/* Simulate game */
		simulate_game(&sim, g, who);

		/* Take card and put it in hand */
		move_card(&sim, list[i], who, WHERE_HAND);

		/* Score game */
		score = eval_game(&sim, who);

		/* Check for better */
		if (score_better(score, b_s))
		{
			/* Track best score and choice */
			b_s = score;
			b_i = list[i];
		}
	}

	/* Return best choice */
	return b_i;
}

/*
 * Choose windfall world to produce on.
 */
static void ai_choose_windfall(game *g, int who, int list[], int *num,
                               int c_idx, int o_idx)
{
	game sim;
	int i, best = -1;
	double score, b_s = -1;

	/* Condense good list */
	*num = condense_goods(g, list, *num);

	/* Check for only one remaining choice */
	if (*num == 1)
	{
		/* Done */
		return;
	}

	/* Loop over choices */
	for (i = 0; i < *num; i++)
	{
		/* Simulate game */
		simulate_game(&sim, g, who);

		/* Try producing on this world */
		produce_world(&sim, who, list[i], c_idx, o_idx);

		/* Use remaining produce powers */
		while (produce_action(&sim, who));

		/* Simulate rest of turn */
		complete_turn(&sim, COMPLETE_ROUND);

		/* Get score */
		score = eval_game(&sim, who);

		/* Check for better */
		if (score_better(score, b_s))
		{
			/* Track best */
			b_s = score;
			best = list[i];
		}
	}

	/* No good play */
	if (best == -1)
	{
		/* Error */
		display_error("Could not find windfall production\n");
		abort();
	}

	/* Use best choice */
	list[0] = best;
	*num = 1;
}

/*
 * Choose produce power to use.
 */
static void ai_choose_produce(game *g, int who, int cidx[], int oidx[], int num)
{
	game sim;
	card *c_ptr;
	power *o_ptr;
	int i, best = -1;
	double score, b_s = -1;

	/* Loop over choices */
	for (i = 0; i < num; i++)
	{
		/* Check for produce bonuses */
		if (cidx[i] < 0) continue;

		/* Get card pointer */
		c_ptr = &g->deck[cidx[i]];

		/* Get power pointer */
		o_ptr = &c_ptr->d_ptr->powers[oidx[i]];

		/* Skip powers needing discard */
		if (o_ptr->code & P5_DISCARD) continue;

		/* Check for specific windfall production */
		if (o_ptr->code & (P5_WINDFALL_NOVELTY | P5_WINDFALL_RARE |
		                   P5_WINDFALL_GENE | P5_WINDFALL_ALIEN))
		{
			/* Choose power first */
			cidx[0] = cidx[i];
			oidx[0] = oidx[i];
			return;
		}
	}

	/* Loop over choices */
	for (i = 0; i < num; i++)
	{
		/* Check for produce bonuses */
		if (cidx[i] < 0)
		{
			/* Choose produce bonus next if available */
			cidx[0] = cidx[i];
			oidx[0] = oidx[i];
			return;
		}

		/* Get card pointer */
		c_ptr = &g->deck[cidx[i]];

		/* Get power pointer */
		o_ptr = &c_ptr->d_ptr->powers[oidx[i]];

		/* Skip powers needing discard */
		if (o_ptr->code & P5_DISCARD) continue;

		/* Check for windfall production */
		if (o_ptr->code & P5_WINDFALL_ANY)
		{
			/* Choose power */
			cidx[0] = cidx[i];
			oidx[0] = oidx[i];
			return;
		}
	}

	/* Loop over choices */
	for (i = 0; i < num; i++)
	{
		/* Simulate game */
		simulate_game(&sim, g, who);

		/* Try using current produce power */
		produce_chosen(&sim, who, cidx[i], oidx[i]);

		/* Use remaining produce powers */
		while (produce_action(&sim, who));

		/* Simulate rest of turn */
		complete_turn(&sim, COMPLETE_ROUND);

		/* Evaluate end state */
		score = eval_game(&sim, who);

		/* Check for better */
		if (score_better(score, b_s))
		{
			/* Track best */
			b_s = score;
			best = i;
		}
	}

	/* Select best option */
	cidx[0] = cidx[best];
	oidx[0] = oidx[best];
}

/*
 * Choose card from hand to discard in order to produce on one of the
 * given worlds.
 */
static void ai_choose_discard_produce(game *g, int who, int list[], int *num,
                                      int special[], int *num_special,
                                      int c_idx, int o_idx)
{
	game sim;
	player *p_ptr;
	double b_s, score;
	int i, b_i = -1;
	int j, b_j = -1;

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Simulate game */
	simulate_game(&sim, g, who);

	/* Use remaining produce powers without discarding */
	while (produce_action(&sim, who));

	/* Simulate rest of turn */
	complete_turn(&sim, COMPLETE_ROUND);

	/* Get score without doing anything */
	b_s = eval_game(&sim, who);

	/* Condense list of windfall worlds */
	*num_special = condense_goods(g, special, *num_special);

	/* Check for simulation */
	if (g->simulation)
	{
		/* Check for at no cards in hand */
		if (count_player_area(g, who, WHERE_HAND) +
		    p_ptr->fake_hand - p_ptr->fake_discards == 0)
		{
			/* Do nothing */
			*num = 0;
			return;
		}

		/* Loop over world choices */
		for (i = 0; i < *num_special; i++)
		{
			/* Simulate game */
			simulate_game(&sim, g, who);

			/* Discard from hand */
			sim.p[who].fake_discards++;

			/* Produce */
			produce_world(&sim, who, special[i], c_idx, o_idx);

			/* Simulate rest of turn */
			complete_turn(&sim, COMPLETE_ROUND);

			/* Get score */
			score = eval_game(&sim, who);

			/* Check for better */
			if (score_better(score, b_s))
			{
				/* Track best */
				b_i = i;
				b_s = score;
			}
		}

		/* Check for no choice */
		if (b_i == -1)
		{
			/* Set no choice */
			*num = 0;
			return;
		}

		/* Discard */
		g->p[who].fake_discards++;

		/* Produce on best world */
		produce_world(g, who, special[b_i], c_idx, o_idx);

		/* Do not discard any specific card */
		*num = 0;

		/* Done */
		return;
	}

	/* Loop over world choices */
	for (i = 0; i < *num_special; i++)
	{
		/* Loop over choices of cards to discard */
		for (j = 0; j < *num; j++)
		{
			/* Simulate game */
			simulate_game(&sim, g, who);

			/* Try discard */
			discard_produce_chosen(&sim, who, special[i], list[j],
			                       c_idx, o_idx);

			/* Use remaining produce powers */
			while (produce_action(&sim, who));

			/* Simulate rest of turn */
			complete_turn(&sim, COMPLETE_ROUND);

			/* Get score */
			score = eval_game(&sim, who);

			/* Check for better */
			if (score_better(score, b_s))
			{
				/* Track best */
				b_i = i;
				b_j = j;
				b_s = score;
			}
		}
	}

	/* Check for card chosen */
	if (b_i > -1)
	{
		/* Select card */
		special[0] = special[b_i];
		list[0] = list[b_j];
		*num = 1;
	}
	else
	{
		/* No card selected */
		*num = 0;
	}
}

/*
 * Choose a search category.
 */
static int ai_choose_search_type(game *g, int who)
{
	game sim;
	card *c_ptr;
	int i, j, num, b_i = -1;
	double score, b_s = -1;

	/* Loop over search categories */
	for (i = 0; i < MAX_SEARCH; i++)
	{
		/* Skip takeover category if disabled */
		if (g->takeover_disabled && i == SEARCH_TAKEOVER) continue;

		/* No score for this type */
		score = 0;
		num = 0;

		/* Loop over cards in deck */
		for (j = 0; j < g->deck_size; j++)
		{
			/* Get card pointer */
			c_ptr = &g->deck[j];

			/* Skip cards with known location */
			if (c_ptr->misc & (1 << who)) continue;

			/* Skip cards that do not match search category */
			if (!search_match(g, j, i)) continue;

			/* Simulate game */
			simulate_game(&sim, g, who);

			/* Claim matching card */
			claim_card(&sim, who, j);

			/* Score game */
			score += eval_game(&sim, who);

			/* Add to number of scores */
			num++;
		}

		/* Skip categories that have no matches */
		if (!num) continue;

		/* Compute average score */
		score /= num;

		/* Check for better */
		if (score_better(score, b_s))
		{
			/* Track best */
			b_s = score;
			b_i = i;
		}
	}

	/* Check for no good category found */
	if (b_i < 0)
	{
		/* Choose six-cost development category */
		return SEARCH_6_DEV;
	}

	/* Return best choice */
	return b_i;
}

/*
 * Choose whether to keep a given card from a search.
 */
static int ai_choose_search_keep(game *g, int who, int which, int category)
{
	game sim;
	card *c_ptr;
	int i, num = 0;
	double score = 0, b_s = -1;

	/* Always keep card in simulated game */
	if (g->simulation) return 1;

	/* Simulate game */
	simulate_game(&sim, g, who);

	/* Take offered card */
	claim_card(&sim, who, which);

	/* Finish turn with card */
	complete_turn(&sim, COMPLETE_ROUND);

	/* Score game */
	b_s = eval_game(&sim, who);

	/* Loop over cards in deck */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Skip cards with known location */
		if (c_ptr->misc & (1 << who)) continue;

		/* Skip cards that do not match search category */
		if (!search_match(g, i, category)) continue;

		/* Simulate game */
		simulate_game(&sim, g, who);

		/* Claim matching card */
		claim_card(&sim, who, i);

		/* Finish turn */
		complete_turn(&sim, COMPLETE_ROUND);

		/* Score game */
		score += eval_game(&sim, who);

		/* Add to number of scores */
		num++;
	}

	/* Check for no other matches */
	if (!num) return 1;

	/* Compute average score */
	score /= num;

	/* Check for better */
	if (score_better(score, b_s))
	{
		/* Decline card */
		return 0;
	}

	/* Accept card */
	return 1;
}

/*
 * Choose kind of Alien Oort Cloud Refinery.
 */
static int ai_choose_oort_kind(game *g, int who)
{
	/* XXX */
	return GOOD_ALIEN;
}


/*
 * Make a choice of the given type.
 */
static void ai_make_choice(game *g, int who, int type, int list[], int *nl,
                      int special[], int *ns, int arg1, int arg2, int arg3)
{
	player *p_ptr;
	int i, rv;
	int *l_ptr;

	/* Check for real game */
	if (!g->simulation)
	{
		/* Prepare quick discard list */
		ai_prepare_discard(g, who);

		/* Set current hand size as low */
		g->p[who].low_hand = count_player_area(g, who, WHERE_HAND);
	}

	/* Determine type of choice */
	switch (type)
	{
		/* Action(s) to play */
		case CHOICE_ACTION:

			/* Choose actions */
			ai_choose_action(g, who, list, arg1);
			rv = 0;
			break;

		/* Start world */
		case CHOICE_START:

			/* Choose start world and initial discards */
			ai_choose_start(g, who, list, nl, special, ns);
			rv = 0;
			break;

		/* Discard */
		case CHOICE_DISCARD:

			/* Choose discards */
			ai_choose_discard(g, who, list, nl, arg1);
			rv = 0;
			break;

		/* Save a card */
		case CHOICE_SAVE:

			/* Choose card to save */
			ai_choose_save(g, who, list, nl);
			rv = 0;
			break;

		/* Choose a card to discard for prestige */
		case CHOICE_DISCARD_PRESTIGE:

			/* Choose card to discard */
			ai_choose_discard_prestige(g, who, list, nl);
			rv = 0;
			break;

		/* Place a development/world */
		case CHOICE_PLACE:

			/* Choose card to place */
			rv = ai_choose_place(g, who, list, *nl, arg1, arg2,
			                     arg3);
			break;

		/* Pay for a development/world */
		case CHOICE_PAYMENT:

			/* Choose payment */
			ai_choose_pay(g, who, arg1, list, nl, special, ns,
			              arg2, arg3);
			rv = 0;
			break;

		/* Choose settle power to use next */
		case CHOICE_SETTLE:

			/* Choose settle power */
			ai_choose_settle(g, who, list, special, nl);
			rv = 0;
			break;

		/* Choose a world to takeover */
		case CHOICE_TAKEOVER:

			/* Choose takeover target/power */
			rv = ai_choose_takeover(g, who, list, nl, special, ns);
			break;

		/* Choose a method of defense against a takeover */
		case CHOICE_DEFEND:

			/* Choose defense method */
			ai_choose_defend(g, who, arg1, arg2, arg3, list, nl,
			                 special, ns);
			rv = 0;
			break;

		/* Decide whether to prevent a takeover */
		case CHOICE_TAKEOVER_PREVENT:

			/* Choose which takeover to prevent */
			ai_choose_takeover_prevent(g, who, list, nl, special);
			rv = 0;
			break;

		/* Choose world to upgrade */
		case CHOICE_UPGRADE:

			/* Choose which world to upgrade */
			ai_choose_upgrade(g, who, list, nl, special, ns);
			rv = 0;
			break;

		/* Choose a good to trade */
		case CHOICE_TRADE:

			/* Choose good */
			ai_choose_trade(g, who, list, nl, arg1);
			rv = 0;
			break;

		/* Choose a consume power to use */
		case CHOICE_CONSUME:

			/* Choose power */
			ai_choose_consume(g, who, list, special, nl, arg1);
			rv = 0;
			break;

		/* Choose discards from hand for VP */
		case CHOICE_CONSUME_HAND:

			/* Choose cards */
			ai_choose_consume_hand(g, who, arg1, arg2, list, nl);
			rv = 0;
			break;

		/* Choose good(s) to consume */
		case CHOICE_GOOD:

			/* Choose good(s) */
			ai_choose_good(g, who, special[0], special[1],
			               list, nl, arg1, arg2);
			rv = 0;
			break;

		/* Choose lucky number */
		case CHOICE_LUCKY:

			/* Choose number */
			rv = ai_choose_lucky(g, who);
			break;

		/* Choose card to ante */
		case CHOICE_ANTE:

			/* Choose card */
			rv = ai_choose_ante(g, who, list, *nl);
			break;

		/* Choose card to keep in successful gamble */
		case CHOICE_KEEP:

			/* Choose card */
			rv = ai_choose_keep(g, who, list, *nl);
			break;

		/* Choose windfall world to produce on */
		case CHOICE_WINDFALL:

			/* Choose world */
			ai_choose_windfall(g, who, list, nl, arg1, arg2);
			rv = 0;
			break;

		/* Choose produce power to use */
		case CHOICE_PRODUCE:

			/* Choose power */
			ai_choose_produce(g, who, list, special, *nl);
			rv = 0;
			break;

		/* Choose card to discard in order to produce */
		case CHOICE_DISCARD_PRODUCE:

			/* Choose card */
			ai_choose_discard_produce(g, who, list, nl,
			                          special, ns, arg1, arg2);
			rv = 0;
			break;

		/* Choose search category */
		case CHOICE_SEARCH_TYPE:

			/* Choose category */
			rv = ai_choose_search_type(g, who);
			break;

		/* Choose whether to keep searched card */
		case CHOICE_SEARCH_KEEP:

			/* Choose to keep */
			rv = ai_choose_search_keep(g, who, arg1, arg2);
			break;

		/* Choose kind of Alien Oort Cloud Refinery */
		case CHOICE_OORT_KIND:

			/* Choose kind */
			rv = ai_choose_oort_kind(g, who);
			break;

		/* Error */
		default:
			display_error("Unknown choice type!\n");
			abort();
	}

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Get pointer to end of choice log */
	l_ptr = &p_ptr->choice_log[p_ptr->choice_size];

	/* Add choice type to log */
	*l_ptr++ = type;

	/* Add return value to log */
	*l_ptr++ = rv;

	/* Check for number of list items available */
	if (nl)
	{
		/* Add number of returned list items */
		*l_ptr++ = *nl;

		/* Copy list items */
		for (i = 0; i < *nl; i++)
		{
			/* Copy list item */
			*l_ptr++ = list[i];
		}
	}
	else
	{
		/* Add no list items */
		*l_ptr++ = 0;
	}

	/* Check for number of special items available */
	if (ns)
	{
		/* Add number of returned special items */
		*l_ptr++ = *ns;

		/* Copy special items */
		for (i = 0; i < *ns; i++)
		{
			/* Copy special item */
			*l_ptr++ = special[i];
		}
	}
	else
	{
		/* Add no special items */
		*l_ptr++ = 0;
	}

	/* Mark new size of choice log */
	p_ptr->choice_size = l_ptr - p_ptr->choice_log;
}

/*
 * Game over.
 */
static void ai_game_over(game *g, int who)
{
	player *p_ptr;
	double result[MAX_PLAYER], sum = 0.0;
	int scores[MAX_PLAYER];
	int max = 0, i, n;

#if 0
	if (who == 0)
	{
		printf("Most expensive choice: %d\n", most_computes);
		printf("Choice tree: ");
		for (i = 0; i < most_depth; i++)
		{
			printf("(%d %d %d %d %d %d) ", most_args[i].type, most_args[i].num, most_args[i].num_special, most_args[i].arg1, most_args[i].arg2, most_args[i].arg3);
		}
		printf("\n");
		most_computes = 0;

		printf("Duplicated computes: %d/%d\n", dup_computes, num_computes);
		num_computes = dup_computes = 0;

		report_dups();
	}
#endif

	/* Find maximum score */
	for (i = 0; i < g->num_players; i++)
	{
		/* Get player pointer */
		p_ptr = &g->p[i];

		/* Copy score */
		scores[i] = p_ptr->end_vp;

		/* Give extra reward to winner */
		if (p_ptr->winner) scores[i]++;

		/* Check for more */
		if (scores[i] > max) max = scores[i];
	}

	/* Compute probability sum */
	for (i = 0; i < g->num_players; i++)
	{
		/* Add this player's portion */
		sum += exp(0.3 * (scores[i] - max));
	}

	/* Compute our personal victory ratio */
	result[0] = exp(0.3 * (scores[who] - max)) / sum;

	/* Start other players at slot 1 */
	n = 1;

	/* Go to next player */
	i = (who + 1) % g->num_players;

	/* Compute other player victory ratios */
	while (i != who)
	{
		/* Compute ratio */
		result[n++] = exp(0.3 * (scores[i] - max)) / sum;

		/* Go to next player */
		i = (i + 1) % g->num_players;
	}

	/* Perform final training */
	perform_training(g, who, result);

	/* Check for training done for all players */
	if (who == g->num_players - 1)
	{
		/* Clear stored past inputs */
		clear_store(&eval);
		clear_store(&role);

		/* Mark training iterations */
		eval.num_training++;
	}
}

/*
 * Shutdown.
 */
static void ai_shutdown(game *g, int who)
{
	char fname[1024];
	static int saved;

	/* Check for already saved */
	if (saved) return;

	/* Create evaluator filename */
	sprintf(fname, RFTGDIR "/network/rftg.eval.%d.%d%s.net", g->expanded,
	        g->num_players, g->advanced ? "a" : "");

	/* Save weights to disk */
	save_net(&eval, fname);

	/* Create predictor filename */
	sprintf(fname, RFTGDIR "/network/rftg.role.%d.%d%s.net", g->expanded,
	        g->num_players, g->advanced ? "a" : "");

	/* Save weights to disk */
	save_net(&role, fname);

	printf("Role hit: %d, Role miss: %d\n", role_hit, role_miss);
	printf("Role avg: %f\n", role_avg / (role_hit + role_miss));
	printf("Role error: %f\n", role.error / role.num_error);
	printf("Eval error: %f\n", eval.error / eval.num_error);

	/* Mark weights as saved */
	saved = 1;
}

/*
 * Set of AI functions.
 */
decisions ai_func =
{
	ai_initialize,
	ai_notify_rotation,
	NULL,
	ai_make_choice,
	NULL,
	ai_explore_sample,
	ai_game_over,
	ai_shutdown,
	NULL,
};

/*
 * Provide debugging information.
 */
void ai_debug(game *g, double win_prob[MAX_PLAYER][MAX_PLAYER],
                       double *role[], double *action_score[], int *num_action)
{
	game sim;
	int i, j, n, who;
	int oa;
	double used = 0;
	double prob, most_prob, threshold, threshold_h, threshold_l;

	/* Loop over point-of-view players */
	for (i = 0; i < g->num_players; i++)
	{
		/* Evaluate game state from this point of view */
		win_prob[i][i] = eval_game(g, i);

		/* Start with left opponent */
		n = (i + 1) % g->num_players;

		/* Copy probabilities */
		for (j = 1; j < g->num_players; j++)
		{
			/* Copy probability */
			win_prob[i][n] = eval.win_prob[j];

			/* Advance marker to next player */
			n = (n + 1) % g->num_players;
		}
	}

	/* Create role probability and action score arrays */
	if (g->advanced)
	{
		/* Loop over players */
		for (i = 0; i < g->num_players; i++)
		{
			/* Create role row */
			role[i] = (double *)malloc(sizeof(double) *
			                           ROLE_OUT_ADV_EXP3);

			/* Create action score row */
			action_score[i] = (double *)malloc(sizeof(double) *
			                                   ROLE_OUT_ADV_EXP3);

			/* Set number of action columns */
			*num_action = ROLE_OUT_ADV_EXP3;
		}
	}
	else
	{
		/* Loop over players */
		for (i = 0; i < g->num_players; i++)
		{
			/* Create role row */
			role[i] = (double *)malloc(sizeof(double) * ROLE_OUT_EXP3);

			/* Create action score row */
			action_score[i] = (double *)malloc(sizeof(double) *
			                                   ROLE_OUT_EXP3);

			/* Set number of action columns */
			*num_action = ROLE_OUT_EXP3;
		}
	}

	/* Loop over players */
	for (i = 0; i < g->num_players; i++)
	{
		/* Predict action choices */
		predict_action(g, i, role[i], 0);
	}

	/* Compute action scores for the advanced game */
	if (g->advanced)
	{
		/* Loop over players */
		for (who = 0; who < g->num_players; who++)
		{
			/* Clear scores */
			for (i = 0; i < ROLE_OUT_ADV_EXP3; i++)
				action_score[who][i] = 0.0;

			/* Simulate game */
			simulate_game(&sim, g, who);

			/* Set initial thresholds */
			threshold_h = 1.0;
			threshold_l = 0.5;

			/* No probability space checked */
			used = 0;

			/* Check action combinations */
			while (used < 0.8)
			{
				/* Loop over opponent's actions */
				for (oa = 0; oa < ROLE_OUT_ADV_EXP3; oa++)
				{
					/* Get probability of this combo */
					prob = role[!who][oa];

					/* Check for too low probability */
					if (prob < threshold_l) continue;

					/* Check for too high probability */
					if (prob >= threshold_h) continue;

#if 0
					/* Get our action scores against this */
					ai_choose_action_advanced_aux(g, who,
					           oa, action_score[who], prob, 0, 0);
#endif

					/* Track amount of probability space */
					used += prob;
				}

				/* Lower high threshold */
				threshold_h = threshold_l;

				/* Lower bottom threshold */
				threshold_l /= 2;
			}
		}

		/* Done */
		return;
	}

	/* Loop over players */
	for (who = 0; who < g->num_players; who++)
	{
		/* Simulate game */
		simulate_game(&sim, g, who);

		/* Start with high threshold */
		threshold = 1;

		/* Clear amount of probability space searched */
		used = 0;

		/* Loop over players */
		for (i = 0; i < g->num_players; i++)
		{
			/* Skip ourself */
			if (i == who) continue;

			/* Clear biggest probability */
			most_prob = 0;

			/* Loop over actions */
			for (j = 0; j < ROLE_OUT_EXP3; j++)
			{
				/* Check for bigger */
				if (role[i][j] > most_prob)
				{
					/* Track biggest */
					most_prob = role[i][j];
				}
			}

			/* Lower threshold */
			threshold *= most_prob;
		}

		/* Reduce threshold to check similar events */
		threshold /= 8;

		/* Always check everything with two players */
		if (g->num_players == 2) threshold = 0;

		/* Increase threshold with large numbers of players */
		if (g->num_players == 4) threshold *= 3;
		if (g->num_players >= 5) threshold *= 6;

		/* Clear scores */
		for (i = 0; i < ROLE_OUT_EXP3; i++) action_score[who][i] = 0.0;

		/* Clear active actions */
		for (i = 0; i < MAX_ACTION; i++) sim.action_selected[i] = 0;

#if 0
		/* Evaluate action tree */
		ai_choose_action_aux(&sim, who, 0, 1.0, &used,
		                     action_score[who], role, threshold);
#endif

		/* Normalize action scores */
		for (i = 0; i < ROLE_OUT_EXP3; i++)
		{
			/* Normalize score */
			action_score[who][i] /= used;
		}
	}
}

/*
 * Perform simple pre-training on a new neural net.
 *
 * We do this to jump-start the real training, by starting the network
 * with the idea that "winning" and "being ahead in points" are both
 * desirable things.  This makes the initial training games much more
 * productive, since the network will already make basic decisions to
 * place cards or consume for points.
 */
static void initial_training(game *g)
{
	game sim;
	int i, j, n, most;

	/* Increase learning rate */
	eval.alpha *= 10;

	/* Clear some important game fields that may yet be uninitialized */
	g->simulation = 0;
	g->vp_pool = 0;
	g->deck_size = 0;
	g->cur_action = 0;
	memset(g->deck, 0, sizeof(card) * MAX_DECK);
	memset(g->goal_active, 0, sizeof(int) * MAX_GOAL);
	memset(g->goal_avail, 0, sizeof(int) * MAX_GOAL);

	/* Clear some uninitialized player information */
	for (i = 0; i < g->num_players; i++)
	{
		/* Clear player's card counts and winner flag */
		memset(g->p[i].goal_claimed, 0, sizeof(int) * MAX_GOAL);
		g->p[i].fake_hand = 0;
		g->p[i].drawn_round = 0;
		g->p[i].fake_discards = 0;
		g->p[i].winner = 0;
		g->p[i].vp = 0;
		g->p[i].prestige = 0;
		g->p[i].prestige_action_used = 0;

		/* Clear player's card stacks */
		for (j = 0; j < MAX_WHERE; j++) g->p[i].head[j] = -1;
		for (j = 0; j < MAX_WHERE; j++) g->p[i].start_head[j] = -1;
	}

	/* Perform several training iterations */
	for (n = 0; n < 5000; n++)
	{
		/* Simulate end-game */
		simulate_game(&sim, g, 0);

		/* Make game as over */
		sim.game_over = 1;

		/* Create random scores for players */
		for (i = 0; i < g->num_players; i++)
		{
			/* Create random score */
			sim.p[i].vp = rand() % 50;
			sim.p[i].end_vp = sim.p[i].vp;
		}

		/* Clear best score */
		most = -1;

		/* Find best score */
		for (i = 0; i < g->num_players; i++)
		{
			/* Check for better score */
			if (sim.p[i].vp > most) most = sim.p[i].vp;
		}

		/* Award winner flag to player(s) with most */
		for (i = 0; i < g->num_players; i++)
		{
			/* Check for best score */
			if (sim.p[i].vp == most) sim.p[i].winner = 1;
		}

		/* Train network for each player */
		for (i = 0; i < g->num_players; i++)
		{
			/* Train network */
			ai_game_over(&sim, i);
		}
	}

	/* Reset learning rate */
	eval.alpha /= 10;
}
