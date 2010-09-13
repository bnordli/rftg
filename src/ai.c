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
#include "net.h"

/* #define DEBUG */

#ifdef DEBUG
static int num_computes;
#endif


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

/*
 * Size of evaluator neural net.
 */
#define EVAL_MISC   218
#define EVAL_PLAYER 414
#define EVAL_HIDDEN 50

/*
 * Size of role predictor neural net.
 */
#define ROLE_MISC   66
#define ROLE_PLAYER 394
#define ROLE_HIDDEN 50

/*
 * Number of outputs for role predictor network (basic and advanced).
 */
#define ROLE_OUT     7
#define ROLE_OUT_ADV 23

/*
 * Forward declaration.
 */
static void initial_training(game *g);


/*
 * Initialize AI.
 */
static void ai_initialize(game *g, int who, double factor)
{
	char fname[1024];
	static int loaded_p, loaded_e;
	int inputs;

	/* Do nothing if correct networks already loaded */
	if (loaded_p == g->num_players && loaded_e == g->expanded) return;

	/* Free old networks if some already loaded */
	if (loaded_p > 0)
	{
		/* Free old networks */
		free_net(&eval);
		free_net(&role);
	}

	/* Compute number of inputs */
	inputs = EVAL_MISC + EVAL_PLAYER * g->num_players;

	/* Create evaluator network */
	make_learner(&eval, inputs, EVAL_HIDDEN, g->num_players);

	/* Set learning rate */
	eval.alpha = 0.0001 * factor;
#ifdef DEBUG
	eval.alpha = 0.0;
#endif

	/* Create evaluator filename */
	sprintf(fname, DATADIR "/network/rftg.eval.%d.%d%s.net", g->expanded,
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
			printf("Warning: Couldn't open %s\n", fname);

			/* Perform initial training on new network */
			initial_training(g);
		}
	}

	/* Compute number of inputs */
	inputs = ROLE_MISC + ROLE_PLAYER * g->num_players;

	/* Create role predictor network */
	make_learner(&role, inputs, ROLE_HIDDEN,
	             g->advanced ? ROLE_OUT_ADV : ROLE_OUT);

	/* Set learning rate */
	role.alpha = 0.0005 * factor;
#ifdef DEBUG
	role.alpha = 0.0;
#endif

	/* Create predictor filename */
	sprintf(fname, DATADIR "/network/rftg.role.%d.%d%s.net", g->expanded,
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
			printf("Warning: Couldn't open %s\n", fname);
		}
	}

	/* Mark network as loaded */
	loaded_p = g->num_players;
	loaded_e = g->expanded;
}

/*
 * Called when player spots have been rotated.
 *
 * We need to do nothing.
 */
static void ai_notify_rotation(game *g)
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
 * Complete the current turn.
 *
 * We call this before evaluating a game state, to attempt to foresee
 * consequences of chosen actions.
 *
 * If the partial flag is set, we merely complete the current phase.
 */
static void complete_turn(game *g, int partial)
{
	player *p_ptr;
	int i, target;

	/* Ensure game is simulated */
	if (!g->simulation)
	{
		/* Error */
		printf("complete_turn() called with real game!\n");
		exit(1);
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
				trade_action(g, i, 0);
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

	/* Check for partial completion request */
	if (partial) return;

	/* Clear temp flags from just-finished phase */
	clear_temp(g);

	/* Check goals from just-finished phase */
	check_goals(g);

	/* Loop over remaining phases */
	for (i = g->cur_action + 1; i <= ACT_PRODUCE; i++)
	{
		/* Set game phase */
		g->cur_action = i;

		/* Skip unselected actions */
		if (!g->action_selected[i]) continue;

		/* Handle phase */
		switch (i)
		{
			case ACT_EXPLORE_5_0: phase_explore(g); break;
			case ACT_DEVELOP:
			case ACT_DEVELOP2: phase_develop(g); break;
			case ACT_SETTLE:
			case ACT_SETTLE2: phase_settle(g); break;
			case ACT_CONSUME_TRADE: phase_consume(g); break;
			case ACT_PRODUCE: phase_produce(g); break;
		}
	}

	/* Clear current action */
	g->cur_action = -1;

	/* Handle discard phase */
	phase_discard(g);

	/* Check intermediate goals */
	check_goals(g);

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
		if (count_player_area(g, i, WHERE_ACTIVE) +
		    p_ptr->fake_played_dev +
		    p_ptr->fake_played_world >= target)
		{
			/* Game over */
			g->game_over = 1;
		}
	}
}

/*
 * Set inputs of the evaluation network for the given player.
 *
 * We only use public knowledge in this function.
 */
static int eval_game_player(game *g, int who, int n, int max_vp)
{
	player *p_ptr;
	card *c_ptr;
	power *o_ptr;
	int i, j, count, good[6], explore_mix = 0;
	int active[MAX_DECK], num_active = 0;

	/* Clear good type array */
	for (i = 0; i <= GOOD_ALIEN; i++) good[i] = 0;

	/* Set player pointer */
	p_ptr = &g->p[who];

	/* Loop over our active cards */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Skip unowned cards */
		if (c_ptr->owner != who) continue;

		/* Skip inactive cards */
		if (c_ptr->where != WHERE_ACTIVE) continue;

		/* Add to list of active cards */
		active[num_active++] = i;

		/* Set input for active card */
		eval.input_value[n + c_ptr->d_ptr->index] = 1;

		/* Loop over powers on card */
		for (j = 0; j < c_ptr->d_ptr->num_power; j++)
		{
			/* Get power pointer */
			o_ptr = &c_ptr->d_ptr->powers[j];

			/* Skip non-Explore powers */
			if (o_ptr->phase != PHASE_EXPLORE) continue;

			/* Check for "discard any" power */
			if (o_ptr->code & P1_DISCARD_ANY) explore_mix = 1;
		}
	}

	/* Advance input index */
	n += MAX_DESIGN;

	/* Clear good count */
	count = 0;

	/* Loop over active cards */
	for (i = 0; i < num_active; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[active[i]];

		/* Skip unowned cards */
		if (c_ptr->owner != who) continue;

		/* Skip inactive cards */
		if (c_ptr->where != WHERE_ACTIVE) continue;

		/* Skip cards without goods */
		if (c_ptr->covered == -1) continue;

		/* Count available goods */
		count++;

		/* Track type of good */
		good[c_ptr->d_ptr->good_type] = 1;

		/* Set input for card with good */
		eval.input_value[n + c_ptr->d_ptr->index] = 1;
	}

	/* Advance input index */
	n += MAX_DESIGN;

	/* Set inputs for goods */
	for (i = 0; i < 12; i++)
	{
		/* Set input if this many goods */
		eval.input_value[n++] = count > i;
	}

	/* Loop over good types */
	for (i = GOOD_NOVELTY; i <= GOOD_ALIEN; i++)
	{
		/* Set input if good type available */
		eval.input_value[n++] = good[i];
	}

	/* Get count of cards in hand */
	count = count_player_area(g, who, WHERE_HAND) + p_ptr->fake_hand -
	        p_ptr->fake_discards - p_ptr->fake_played_dev -
	        p_ptr->fake_played_world;

	/* Set inputs for cards in hand */
	for (i = 0; i < 12; i++)
	{
		/* Set input if this many cards */
		eval.input_value[n++] = count > i;
	}

	/* Clear count of developments */
	count = p_ptr->fake_played_dev;

	/* Loop over our active cards */
	for (i = 0; i < num_active; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[active[i]];

		/* Skip unowned cards */
		if (c_ptr->owner != who) continue;

		/* Skip inactive cards */
		if (c_ptr->where != WHERE_ACTIVE) continue;

		/* Skip non-developments */
		if (c_ptr->d_ptr->type != TYPE_DEVELOPMENT) continue;

		/* Count card */
		count++;
	}

	/* Set inputs for number of active developments */
	for (i = 0; i < 15; i++)
	{
		/* Set input if this many cards */
		eval.input_value[n++] = count > i;
	}

	/* Clear count of worlds */
	count = p_ptr->fake_played_world;

	/* Loop over our active cards */
	for (i = 0; i < num_active; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[active[i]];

		/* Skip unowned cards */
		if (c_ptr->owner != who) continue;

		/* Skip inactive cards */
		if (c_ptr->where != WHERE_ACTIVE) continue;

		/* Skip non-worlds */
		if (c_ptr->d_ptr->type != TYPE_WORLD) continue;

		/* Count card */
		count++;
	}

	/* Set inputs for number of active worlds */
	for (i = 0; i < 17; i++)
	{
		/* Set input if this many cards */
		eval.input_value[n++] = count > i;
	}

	/* Clear count */
	count = 0;

	/*
	 * The following loop is basically total_military(), but since we
	 * already have a list of active cards, it should be a bit faster.
	 */

	/* Loop over active cards */
	for (i = 0; i < num_active; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[active[i]];

		/* Loop over card powers */
		for (j = 0; j < c_ptr->d_ptr->num_power; j++)
		{
			/* Get power pointer */
			o_ptr = &c_ptr->d_ptr->powers[j];

			/* Skip non-Settle power */
			if (o_ptr->phase != PHASE_SETTLE) continue;

			/* Check for non-specific military */
			if (o_ptr->code == P3_EXTRA_MILITARY)
			{
				/* Add to military */
				count += o_ptr->value;
			}

			/* Check for military per military world */
			if (o_ptr->code ==(P3_EXTRA_MILITARY | P3_PER_MILITARY))
			{
				/* Add to military */
				count += count_active_flags(g, who,
				                            FLAG_MILITARY);
			}
		}
	}

	/* Set inputs for military strength */
	for (i = 0; i < 15; i++)
	{
		/* Set input if this much strength */
		eval.input_value[n++] = count > i;
	}

	/* Set input if player has special Explore power */
	eval.input_value[n++] = explore_mix;

	/* Set inputs for claimed goals */
	for (i = 0; i < MAX_GOAL; i++)
	{
		/* Set input if goal claimed */
		eval.input_value[n++] = p_ptr->goal_claimed[i];
	}

	/* Set inputs for points behind leader */
	for (i = 0; i < 20; i++)
	{
		/* Set input if this many points behind */
		eval.input_value[n++] = (p_ptr->end_vp + i < max_vp);
	}

	/* Set input if winner */
	eval.input_value[n++] = p_ptr->winner;

	/* Return next index to be used */
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
	int i, count, n = 0;
	int max = 0, clock;

	/* Clear inputs */
	memset(eval.input_value, 0, sizeof(double) * eval.num_inputs);

	/* Get end-of-game score */
	score_game(g);

	/* Declare winner if game over */
	if (g->game_over) declare_winner(g);

	/* Set input for game over */
	eval.input_value[n++] = g->game_over;

	/* Set inputs for VP pool size */
	for (i = 0; i < 12; i++)
	{
		/* Set input if this many points (per player) remain */
		eval.input_value[n++] = g->vp_pool > i * g->num_players;
	}

	/* Loop over players */
	for (i = 0; i < g->num_players; i++)
	{
		/* Get player pointer */
		p_ptr = &g->p[i];

		/* Count number of active cards */
		count = count_player_area(g, i, WHERE_ACTIVE) +
		        p_ptr->fake_played_dev + p_ptr->fake_played_world;

		/* Track most cards played */
		if (count > max) max = count;
	}

	/* Set inputs for cards played */
	for (i = 0; i < 12; i++)
	{
		/* Set input if someone has this many cards played */
		eval.input_value[n++] = max > i;
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
		eval.input_value[n++] = clock > i;
	}

	/* Set inputs for active goals */
	for (i = 0; i < MAX_GOAL; i++)
	{
		/* Set input if this goal is active for this game */
		eval.input_value[n++] = g->goal_active[i];
	}

	/* Set inputs for available goals */
	for (i = 0; i < MAX_GOAL; i++)
	{
		/* Set input if this goal is still available */
		eval.input_value[n++] = g->goal_avail[i];
	}

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Loop over cards in hand */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Skip unowned cards */
		if (c_ptr->owner != who) continue;

		/* Skip cards not in hand */
		if (c_ptr->where != WHERE_HAND) continue;

		/* Set input for card in hand */
		eval.input_value[n + c_ptr->d_ptr->index] = 1;
	}

	/* Clear count of unknown cards */
	count = 0;

	/* Loop over unknown cards */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Skip cards where we know the location */
		if (c_ptr->known & (1 << who)) continue;

		/* Count unknown cards */
		count++;
	}

	/* Loop over unknown cards */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Skip cards where we know the location */
		if (c_ptr->known & (1 << who)) continue;

		/* Add probability we would have this card */
		eval.input_value[n + c_ptr->d_ptr->index] +=
		                                1.0 * p_ptr->total_fake / count;
	}

	/* Advance input index */
	n += MAX_DESIGN;

	/* Assume zero maximum points */
	max = 0;

	/* Loop over players */
	for (i = 0; i < g->num_players; i++)
	{
		/* Check for more points than previous max */
		if (g->p[i].end_vp > max) max = g->p[i].end_vp;
	}

	/* Set public inputs for given player */
	n = eval_game_player(g, who, n, max);

	/* Go to next player */
	i = (who + 1) % g->num_players;

	/* Loop over other players */
	while (i != who)
	{
		/* Set public inputs for other player */
		n = eval_game_player(g, i, n, max);

		/* Go to next player */
		i = (i + 1) % g->num_players;
	}

	/* Compute network */
	compute_net(&eval);

#ifdef DEBUG
	num_computes++;
#endif

	/* Return score for given player */
	return eval.win_prob[0];
}

/*
 * Perform a training iteration on the eval network.
 */
static void perform_training(game *g, int who, double *desired)
{
	double target[MAX_PLAYER];
	double lambda = 1.0;
	int i;

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
static int predict_action_player(game *g, int who, int n)
{
	player *p_ptr;
	card *c_ptr;
	power *o_ptr;
	int i, j, good[6], count, count_dev, count_world, explore_mix = 0;

	/* Clear good types */
	for (i = 0; i <= GOOD_ALIEN; i++) good[i] = 0;

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Assume no active cards */
	count = count_dev = count_world = 0;

	/* Loop over player's active cards */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Skip unowned cards */
		if (c_ptr->owner != who) continue;

		/* Skip inactive cards */
		if (c_ptr->where != WHERE_ACTIVE) continue;

		/* Set input for active card */
		role.input_value[n + c_ptr->d_ptr->index] = 1;

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
		for (j = 0; j < c_ptr->d_ptr->num_power; j++)
		{
			/* Get power pointer */
			o_ptr = &c_ptr->d_ptr->powers[j];

			/* Skip non-Explore powers */
			if (o_ptr->phase != PHASE_EXPLORE) continue;

			/* Check for "discard any" power */
			if (o_ptr->code & P1_DISCARD_ANY) explore_mix = 1;
		}
	}

	/* Advance input index */
	n += MAX_DESIGN;

	/* Set inputs for number of active developments */
	for (i = 0; i < 12; i++)
	{
		/* Set input if this many cards */
		role.input_value[n++] = count_dev > i;
	}

	/* Set inputs for number of active worlds */
	for (i = 0; i < 12; i++)
	{
		/* Set input if this many cards */
		role.input_value[n++] = count_world > i;
	}

	/* Assume no goods */
	count = 0;

	/* Loop over cards with goods */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Skip unowned cards */
		if (c_ptr->owner != who) continue;

		/* Skip inactive cards */
		if (c_ptr->where != WHERE_ACTIVE) continue;

		/* Skip cards without goods */
		if (c_ptr->covered == -1) continue;

		/* Count goods */
		count++;

		/* Track good type */
		good[c_ptr->d_ptr->good_type] = 1;

		/* Set input for card with good */
		role.input_value[n + c_ptr->d_ptr->index] = 1;
	}

	/* Advance input index */
	n += MAX_DESIGN;

	/* Set inputs for available goods */
	for (i = 0; i < 12; i++)
	{
		/* Set input if this many goods */
		role.input_value[n++] = count > i;
	}

	/* Set inputs for good types */
	for (i = GOOD_NOVELTY; i <= GOOD_ALIEN; i++)
	{
		/* Set input */
		role.input_value[n++] = good[i];
	}

	/* Get count of cards in hand */
	count = count_player_area(g, who, WHERE_HAND);

	/* Set inputs for cards in hand */
	for (i = 0; i < 12; i++)
	{
		/* Set input if this many cards */
		role.input_value[n++] = count > i;
	}

	/* Get military strength */
	count = total_military(g, who);

	/* Set inputs for military strength */
	for (i = 0; i < 15; i++)
	{
		/* Set input if this much strength */
		role.input_value[n++] = count > i;
	}

	/* Set input for special Explore power */
	role.input_value[n++] = explore_mix;

	/* Set inputs for claimed goals */
	for (i = 0; i < MAX_GOAL; i++)
	{
		/* Set input if goal claimed */
		role.input_value[n++] = p_ptr->goal_claimed[i];
	}

	/* Set inputs for previous turn actions */
	for (i = 0; i < MAX_ACTION; i++)
	{
		/* Set input if action chosen last turn */
		role.input_value[n++] = (p_ptr->prev_action[0] == i ||
		                         p_ptr->prev_action[1] == i);
	}

	/* Return next input index */
	return n;
}

/*
 * Evaluate chances that player will choose each action.
 */
static void predict_action(game *g, int who, double prob[MAX_ACTION])
{
	player *p_ptr;
	int i, n = 0, count, clock, max;

	/* Clear inputs of role network */
	memset(role.input_value, 0, sizeof(double) * role.num_inputs);

	/* Start with given player */
	i = who;

	/* Add inputs for player being examined */
	n = predict_action_player(g, i, n);

	/* Loop over other players */
	while (1)
	{
		/* Go to next player */
		i = (i + 1) % g->num_players;

		/* Stop once we get back around to first player */
		if (i == who) break;

		/* Add inputs for this player */
		n = predict_action_player(g, i, n);
	}

	/* Set inputs for VP pool size */
	for (i = 0; i < 12; i++)
	{
		/* Set input if this many points (per player) remain */
		role.input_value[n++] = g->vp_pool > i * g->num_players;
	}

	/* Clear max count of cards played */
	max = 0;

	/* Loop over players */
	for (i = 0; i < g->num_players; i++)
	{
		/* Get player pointer */
		p_ptr = &g->p[i];

		/* Count number of active cards */
		count = count_player_area(g, i, WHERE_ACTIVE) +
		        p_ptr->fake_played_dev + p_ptr->fake_played_world;

		/* Track most cards played */
		if (count > max) max = count;
	}

	/* Set inputs for cards played */
	for (i = 0; i < 12; i++)
	{
		/* Set input if someone has this many cards played */
		role.input_value[n++] = max > i;
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
		role.input_value[n++] = clock > i;
	}

	/* Set inputs for active goals */
	for (i = 0; i < MAX_GOAL; i++)
	{
		/* Set input if this goal is active for this game */
		role.input_value[n++] = g->goal_active[i];
	}

	/* Set inputs for available goals */
	for (i = 0; i < MAX_GOAL; i++)
	{
		/* Set input if this goal is still available */
		role.input_value[n++] = g->goal_avail[i];
	}

	/* Compute role choice probabilities */
	compute_net(&role);

	/* Copy scores */
	for (i = 0; i < role.num_output; i++)
	{
		/* Copy scores for action */
		prob[i] = role.win_prob[i];
	}
}

/*
 * List of all advanced game action combinations.
 */
static int adv_combo[ROLE_OUT_ADV][2] =
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
static int role_out[ROLE_OUT] =
{
	ACT_EXPLORE_5_0,
	ACT_EXPLORE_1_1,
	ACT_DEVELOP,
	ACT_SETTLE,
	ACT_CONSUME_TRADE,
	ACT_CONSUME_X2,
	ACT_PRODUCE
};

/*
 * Helper function for ai_choose_action_advanced(), below.
 */
static void ai_choose_action_advanced_aux(game *g, int who, int oa,
                                          double scores[ROLE_OUT_ADV],
                                          double prob)
{
	game sim1, sim2;
	int act;
	double score;
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

	/* Set opponent's actions as selected */
	sim1.action_selected[adv_combo[oa][0]] = 1;
	sim1.action_selected[adv_combo[oa][1]] = 1;

	/* Loop over our choices for actions */
	for (act = 0; act < ROLE_OUT_ADV; act++)
	{
		/* Simulate game */
		simulate_game(&sim2, &sim1, who);

		/* Set our actions */
		sim2.p[who].action[0] = adv_combo[act][0];
		sim2.p[who].action[1] = adv_combo[act][1];

		/* Set our actions as selected */
		sim2.action_selected[adv_combo[act][0]] = 1;
		sim2.action_selected[adv_combo[act][1]] = 1;

		/* Collapse explore phase */
		if (sim2.action_selected[ACT_EXPLORE_1_1])
		{
			/* Set only first explore action */
			sim2.action_selected[ACT_EXPLORE_5_0] = 1;
			sim2.action_selected[ACT_EXPLORE_1_1] = 0;
		}

		/* Collapse consume phase */
		if (sim2.action_selected[ACT_CONSUME_X2])
		{
			/* Set only first consume action */
			sim2.action_selected[ACT_CONSUME_TRADE] = 1;
			sim2.action_selected[ACT_CONSUME_X2] = 0;
		}

		/* Start at beginning of turn */
		sim2.cur_action = -1;

#ifdef DEBUG
		old_computes = num_computes;
#endif

		/* Complete turn */
		complete_turn(&sim2, 0);

		/* Evaluate state after turn */
		score = eval_game(&sim2, who);

#ifdef DEBUG
#if 0
		printf("Trying %s/%s:\nactive %d, goods %d, hand %d, VP %d: score %f\n", action_name[a1], action_name[a2], count_player_area(&sim2, who, WHERE_ACTIVE) + sim2.p[who].fake_played_dev + sim2.p[who].fake_played_world, count_player_area(&sim2, who, WHERE_GOOD), count_player_area(&sim2, who, WHERE_HAND) + sim2.p[who].fake_hand - sim2.p[who].fake_discards - sim2.p[who].fake_played_dev - sim2.p[who].fake_played_world, sim2.p[who].end_vp, score);
		dump_active(&sim2, who);
#endif
		printf("Trying %s/%s: %d (%f)\n", action_name[adv_combo[act][0]], action_name[adv_combo[act][1]], num_computes - old_computes, score);
#endif

		/* Add score to actions */
		scores[act] += score * prob;
	}
}

/*
 * Choose actions in advanced game.
 */
static void ai_choose_action_advanced(game *g, int who, int action[2])
{
	double scores[ROLE_OUT_ADV], b_s = -1, b_p, prob;
	double threshold_l, threshold_h, used = 0;
	double choice_prob[ROLE_OUT_ADV];
	double desired[ROLE_OUT_ADV], sum = 0.0;
	int opp;
	int b_a, b_i;
	int i, oa, act;
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
	predict_action(g, opp, choice_prob);

#ifdef DEBUG
	printf("----- Player %d probability\n", opp);
	for (act = 0; act < MAX_ACTION; act++)
	{
		printf("%.2f ", choice_prob[act]);
	}
	printf("\n");
#endif

	/* Clear scores array */
	for (act = 0; act < ROLE_OUT_ADV; act++)
	{
		/* Clear this score */
		scores[act] = 0.0;
	}

	/* Set initial thresholds */
	threshold_h = 1.0;
	threshold_l = 0.5;

	/* Check action combinations until most of probabilities are checked */
	while (used < 0.8)
	{
		/* Loop over opponent's actions */
		for (oa = 0; oa < ROLE_OUT_ADV; oa++)
		{
			/* Compute probability of this combination */
			prob = choice_prob[oa];

			/* Check for too low probability */
			if (prob < threshold_l) continue;

			/* Check for too high probability */
			if (prob >= threshold_h) continue;

#ifdef DEBUG
			taken++;
			printf("Investigating opponent %s/%s (prob %f)\n", action_name[adv_combo[oa][0]], action_name[adv_combo[oa][1]], prob);
#endif

			/* Check our action scores against this combo */
			ai_choose_action_advanced_aux(g, who, oa, scores, prob);

			/* Track used amount of probability space */
			used += prob;
		}

		/* Lower high threshold to current low threshold */
		threshold_h = threshold_l;

		/* Lower bottom threshold */
		threshold_l /= 2;
	}

#ifdef DEBUG
	printf("Probability space used: %f\n", used);
	printf("Threshold: %f, Taken: %d\n", threshold_l, taken);
#endif

	/* Loop over our action choices */
	for (act = 0; act < ROLE_OUT_ADV; act++)
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

	/* Set best actions */
	action[0] = adv_combo[b_a][0];
	action[1] = adv_combo[b_a][1];

	/* Predict our own actions */
	predict_action(g, who, desired);

	/* Track stats on predicted actions */
	role_avg += desired[b_a];

	/* Clear best score */
	b_p = -1;
	b_i = -1;

	/* Find most predicted action */
	for (i = 0; i < ROLE_OUT_ADV; i++)
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

	/* Compute probability sum */
	for (i = 0; i < ROLE_OUT_ADV; i++)
	{
		/* Add this action's portion */
		sum += exp(20 * (scores[i] / b_s));
	}

	/* Compute actual action probabilities */
	for (i = 0; i < ROLE_OUT_ADV; i++)
	{
		/* Compute probability ratio */
		desired[i] = exp(20 * (scores[i] / b_s)) / sum;
	}

	/* Train network */
	train_net(&role, 1.0, desired);
}

/*
 * Helper function for ai_choose_action().
 */
static void ai_choose_action_aux(game *g, int who, int current, double prob,
                                 double *prob_used, double scores[],
                                 double *choice_prob[MAX_PLAYER],
                                 double threshold)
{
	game sim;
	int i; 
	double score;
#ifdef DEBUG
	int old_computes, j;
#endif

	/* No need to predict our own action choice */
	if (current == who)
	{
		/* Go to next */
		ai_choose_action_aux(g, who, current + 1, prob, prob_used,
                                     scores, choice_prob, threshold);

		/* Done */
		return;
	}

	/* Skip unlikely possibilities */
	if (prob < threshold) return;

	/* Check for all other players chosen */
	if (current == g->num_players)
	{
		/* Loop over available actions */
		for (i = 0; i < ROLE_OUT; i++)
		{
			/* Simulate game */
			simulate_game(&sim, g, who);

			/* Try this action */
			sim.p[who].action[0] = role_out[i];
			sim.p[who].action[1] = -1;

			/* Mark action as selected */
			sim.action_selected[role_out[i]] = 1;

#ifdef DEBUG
			old_computes = num_computes;
			for (j = 0; j <= ACT_PRODUCE; j++)
			{
				if (!sim.action_selected[j]) continue;
				printf("%d ", j);
			}
			printf(": ");
#endif
			/* Collapse explore phase */
			if (sim.action_selected[ACT_EXPLORE_1_1])
			{
				/* Set only first explore action */
				sim.action_selected[ACT_EXPLORE_5_0] = 1;
				sim.action_selected[ACT_EXPLORE_1_1] = 0;
			}

			/* Collapse consume phase */
			if (sim.action_selected[ACT_CONSUME_X2])
			{
				/* Set only first consume action */
				sim.action_selected[ACT_CONSUME_TRADE] = 1;
				sim.action_selected[ACT_CONSUME_X2] = 0;
			}

			/* Start at beginning of turn */
			sim.cur_action = -1;

			/* Complete turn */
			complete_turn(&sim, 0);

#ifdef DEBUG
			printf("%d\n", num_computes - old_computes);
#endif

			/* Evaluate state after turn */
			score = eval_game(&sim, who);

			/* Add score to chosen action */
			scores[i] += score * prob;
		}

		/* Total amount of "probability space" covered */
		*prob_used += prob;

		/* Done */
		return;
	}

	/* Loop over current player's choices */
	for (i = 0; i < ROLE_OUT; i++)
	{
		/* Simulate game */
		simulate_game(&sim, g, who);

		/* Set player's action */
		sim.p[current].action[0] = role_out[i];
		sim.p[current].action[1] = -1;

		/* Add action's phase */
		sim.action_selected[role_out[i]] = 1;

		/* Try next player's actions */
		ai_choose_action_aux(&sim, who, current + 1,
		                     prob * choice_prob[current][i], prob_used,
		                     scores, choice_prob, threshold);
	}
}

/*
 * Choose role and bonus.
 */
static void ai_choose_action(game *g, int who, int action[2])
{
	game sim;
	double scores[ROLE_OUT], prob_used = 0, b_s = -1, b_p;
	double most_prob, threshold = 1.0;
	double *choice_prob[MAX_PLAYER];
	double desired[ROLE_OUT], sum = 0;
	int i, current, best = -1, b_i;

	/* Perform training at beginning of each round */
	perform_training(g, who, NULL);

	/* Handle "advanced" game differently */
	if (g->advanced) return ai_choose_action_advanced(g, who, action);

	/* Clear scores */
	for (i = 0; i < ROLE_OUT; i++) scores[i] = 0.0;

#ifdef DEBUG
	printf("--- Player %d choosing action\n", who);
#endif

	/* Create rows of probabilities */
	for (i = 0; i < g->num_players; i++)
	{
		/* Create row */
		choice_prob[i] = (double *)malloc(sizeof(double) * ROLE_OUT);
	}

	/* Get action predictions */
	for (current = 0; current < g->num_players; current++)
	{
		/* Skip given player */
		if (current == who) continue;

		/* Predict opponent's actions */
		predict_action(g, current, choice_prob[current]);

#ifdef DEBUG
		printf("----- Player %d probability\n", current);
		for (i = 0; i < ROLE_OUT; i++)
		{
			printf("%.2f ", choice_prob[current][i]);
		}
		printf("\n");
#endif

		/* Clear biggest probability */
		most_prob = 0;

		/* Loop over actions */
		for (i = 0; i < ROLE_OUT; i++)
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

	/* Reduce threshold to check similar events */
	threshold /= 8;

	/* Always check everything with two players */
	if (g->num_players == 2) threshold = 0;

	/* Increase threshold with large numbers of players */
	if (g->num_players == 4) threshold *= 3;
	if (g->num_players >= 5) threshold *= 6;

#ifdef DEBUG
	printf("----- Threshold %.2f\n", threshold);
#endif

	/* Simulate game */
	simulate_game(&sim, g, who);

	/* Clear active actions */
	for (i = 0; i < MAX_ACTION; i++) sim.action_selected[i] = 0;

	/* Evaluate action tree */
	ai_choose_action_aux(&sim, who, 0, 1.0, &prob_used, scores,
	                     choice_prob, threshold);

	/* Free rows of probabilities */
	for (i = 0; i < g->num_players; i++)
	{
		/* Free row */
		free(choice_prob[i]);
	}

#ifdef DEBUG
	printf("----- Prob used: %.2f\n", prob_used);

	printf("----- Action scores\n");
	for (i = 0; i < ROLE_OUT; i++)
	{
		printf("%.2f ", scores[i]);
	}
	printf("\n");
#endif

	/* Loop over possible actions */
	for (i = 0; i < ROLE_OUT; i++)
	{
		/* Check for better */
		if (scores[i] > b_s)
		{
			/* Track best */
			b_s = scores[i];
			best = i;
		}
	}

	/* Select best action */
	action[0] = role_out[best];

	/* No second action */
	action[1] = -1;

	/* Predict our own action */
	predict_action(g, who, desired);

	/* Track stats on predicted actions */
	role_avg += desired[best];

	/* Clear best score */
	b_p = -1;
	b_i = -1;

	/* Find most predicted action */
	for (i = 0; i < ROLE_OUT; i++)
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
	for (i = 0; i < ROLE_OUT; i++)
	{
		/* Add this action's portion */
		sum += exp(20 * (scores[i] / b_s));
	}

	/* Compute actual action probabilities */
	for (i = 0; i < ROLE_OUT; i++)
	{
		/* Compute probability ratio */
		desired[i] = exp(20 * (scores[i] / b_s)) / sum;
	}

	/* Train network */
	train_net(&role, 1.0, desired);
}

/*
 * React to all player's action choices.
 */
static void ai_react_action(game *g, int who, int action[2])
{
	/* Apply training */
	apply_training(&role);
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
			/* Simulate rest of turn */
			complete_turn(&sim, 0);
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
 * Choose cards to discard.
 */
static void ai_choose_discard(game *g, int who, int list[], int num,
                              int discard)
{
	game sim;
	player *p_ptr;
	double b_s = -1, score;
	int discards[MAX_DECK], n = 0;
	int best, i, b_i;

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Check for simulated game */
	if (g->simulation)
	{
		/* Track cards discarded */
		p_ptr->fake_discards += discard;

		/* Done */
		return;
	}

	/* XXX - Check for more than 30 cards to choose from */
	if (num > 30)
	{
		/* XXX - Just discard first cards */
		discard_callback(g, who, list, discard);

		/* Done */
		return;
	}

	/* XXX - Check for lots of cards and discards */
	while (num > 12 && discard > 3)
	{
		/* Clear best score */
		b_s = b_i = -1;

		/* Discard worst card */
		for (i = 0; i < num; i++)
		{
			/* Simulate game */
			simulate_game(&sim, g, who);

			/* Discard one */
			discard_callback(&sim, who, &list[i], 1);

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
		discard_callback(g, who, &list[b_i], 1);

		/* Move last card into given spot */
		list[b_i] = list[--num];

		/* One less card to discard */
		discard--;
	}

	/* Clear best score */
	b_s = -1;

	/* Find best set of cards */
	ai_choose_discard_aux(g, who, list, num, discard, 0, &best, &b_s);

	/* Check for failure */
	if (b_s == -1)
	{
		/* Error */
		printf("Failed to find good discard set!\n");
		exit(1);
	}

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

	/* Apply best choice */
	discard_callback(g, who, discards, discard);
}

/*
 * Structure holding a score with associated sample Explore cards.
 */
struct explore_score
{
	double score;
	int list[MAX_DECK];
};

/*
 * Compare two scores for explored cards.
 */
static int cmp_explore_score(const void *p1, const void *p2)
{
	struct explore_score *e1 = (struct explore_score *)p1;
	struct explore_score *e2 = (struct explore_score *)p2;

	/* Compare scores */
	if (e1->score > e2->score) return 1;
	if (e1->score < e2->score) return -1;
	return 0;
}

/*
 * Claim a card for ourself.
 *
 * The card may be in an opponent's hard or used as a good.  If so, find
 * a card to replace it.
 *
 * XXX This entire function is nasty.
 */
static void claim_card(game *g, int who, int which)
{
	card *c_ptr, *replace;
	int i;

	/* Get card pointer */
	c_ptr = &g->deck[which];

	/* Check for card in use */
	if (c_ptr->owner != -1)
	{
		/* Get replacement card */
		replace = random_draw(g);

		/* Check for card used as good */
		if (c_ptr->where == WHERE_GOOD)
		{
			/* Find covered card */
			for (i = 0; i < g->deck_size; i++)
			{
				/* Check for covered */
				if (g->deck[i].covered == which)
				{
					/* Switch to replacement card */
					g->deck[i].covered = replace - g->deck;

					/* Done looking */
					break;
				}
			}
		}

		/* Replace claimed card */
		replace->owner = c_ptr->owner;
		replace->where = c_ptr->where;
	}

	/* Put card in hand */
	c_ptr->owner = who;
	c_ptr->where = WHERE_HAND;

	/* Mark card as temporary */
	c_ptr->temp = 1;
}

/*
 * Discard worst cards when checking for likely Explore results.
 */
static void ai_explore_sample_aux(game *g, int who, int draw, int keep,
                                  int discard_any)
{
	game sim;
	card *c_ptr;
	int list[MAX_DECK], discards[MAX_DECK], num = 0, n = 0;
	int i, b_i, discard;
	int old_action, best;
	double score, b_s;

	/* Compute number of cards to discard */
	discard = draw - keep;

	/* Loop over cards */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Skip unowned cards */
		if (c_ptr->owner != who) continue;

		/* Skip cards not in hand */
		if (c_ptr->where != WHERE_HAND) continue;

		/* Skip old cards if they can't be discarded */
		if (!c_ptr->temp && !discard_any) continue;

		/* Add card to list */
		list[num++] = i;
	}

	/* XXX - Check for lots of cards and discards */
	while (num > 8 && discard > 2 && (num - discard) > 2)
	{
		/* Clear best score */
		b_s = b_i = -1;

		/* Discard worst card */
		for (i = 0; i < num; i++)
		{
			/* Simulate game */
			simulate_game(&sim, g, who);

			/* Discard one */
			discard_callback(&sim, who, &list[i], 1);

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
		discard_callback(g, who, &list[b_i], 1);

		/* Move last card into given spot */
		list[b_i] = list[--num];

		/* One less card to discard */
		discard--;
	}

	/* Clear best score */
	b_s = -1;

	/* XXX Change action so that we don't simulate rest of turn */
	old_action = g->cur_action;
	g->cur_action = -1;

	/* Find best set of cards */
	ai_choose_discard_aux(g, who, list, num, discard, 0, &best, &b_s);

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

	/* Apply best choice */
	discard_callback(g, who, discards, discard);

	/* XXX Reset action */
	g->cur_action = old_action;
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
	struct explore_score scores[5];
	int i, j, k;
	unsigned int seed;

	/* Loop over deck */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Check for unknown location */
		if (!(c_ptr->known & (1 << who)))
		{
			/* Add to list */
			unknown[num_unknown++] = i;
		}
	}

	/* Try multiple random samples */
	for (i = 0; i < 5; i++)
	{
		/* Simulate game */
		simulate_game(&sim, g, who);

		/* Use iteration as seed */
		seed = i;

		/* Pick cards from unknown list */
		for (j = 0; j < draw; j++)
		{
			/* Choose card at random */
			k = myrand(&seed) % num_unknown;

			/* Claim card for ourself */
			claim_card(&sim, who, unknown[k]);

			/* Add claimed card to list for this iteration */
			scores[i].list[j] = unknown[k];

			/* Remove card from list */
			unknown[k] = unknown[--num_unknown];
		}

		/* Discard worst cards */
		ai_explore_sample_aux(&sim, who, draw, keep, discard_any);

		/* Score game */
		scores[i].score = eval_game(&sim, who);

		/* Put chosen cards back in unknown list */
		for (j = 0; j < draw; j++)
		{
			/* Add back to list */
			unknown[num_unknown++] = scores[i].list[j];
		}
	}

	/* Sort list of scores */
	qsort(scores, 5, sizeof(struct explore_score), cmp_explore_score);

	/* Claim cards from median score */
	for (j = 0; j < draw; j++)
	{
		/* Claim card */
		claim_card(g, who, scores[2].list[j]);
	}

	/* Discard worst cards */
	ai_explore_sample_aux(g, who, draw, keep, discard_any);

	/* Clear fake drawn card counters */
	g->p[who].total_fake = 0;
	g->p[who].fake_hand = 0;
	g->p[who].fake_discards = 0;
}

/*
 * Choose a start world from the list given.
 */
static int ai_choose_start(game *g, int who, int list[], int num)
{
	game sim;
	card *c_ptr;
	int i, j, discards, best = -1;
	double score, b_s = -1;
	int hand[MAX_DECK], n, target;

	/* Loop over choices */
	for (i = 0; i < num; i++)
	{
		/* Simulate game */
		simulate_game(&sim, g, who);

		/* Try using this card */
		place_card(&sim, who, list[i]);

		/* Clear hand list */
		n = 0;

		/* Loop over cards in deck */
		for (j = 0; j < sim.deck_size; j++)
		{
			/* Get card pointer */
			c_ptr = &sim.deck[j];

			/* Skip cards not owned by us */
			if (c_ptr->owner != who) continue;

			/* Skip cards not in hand */
			if (c_ptr->where != WHERE_HAND) continue;

			/* Add card to list */
			hand[n++] = j;
		}

		/* Assume discard to 4 */
		target = 4;

		/* Check for discarding to 3 instead */
		if (g->deck[list[i]].d_ptr->flags & FLAG_STARTHAND_3)
		{
			/* Discard hand to 3 */
			target = 3;
		}

		/* Clear score */
		score = -1;

		/* Score best starting discards */
		ai_choose_discard_aux(&sim, who, hand, n, n - target, 0,
		                      &discards, &score);

		/* Check for better than before */
		if (score > b_s)
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
 * Choose card to play (develop or settle).
 */
static int ai_choose_place(game *g, int who, int list[], int num, int phase)
{
	game sim;
	int i, best = -1;
	double score, b_s;

	/* Simulate game */
	simulate_game(&sim, g, who);

	/* Check for Settle phase */
	if (phase == PHASE_SETTLE)
	{
		/* Check for takeovers */
		if (settle_check_takeover(&sim, who))
		{
			/* Take no-place action */
			settle_action(&sim, who, -1);

			/* Resolve takeovers */
			resolve_takeovers(&sim);
		}
	}

	/* Finish turn without placing */
	complete_turn(&sim, 0);

	/* Get score for doing nothing */
	b_s = eval_game(&sim, who);

	/* Loop over choices */
	for (i = 0; i < num; i++)
	{
		/* Simulate game */
		simulate_game(&sim, g, who);

		/* Set placement option */
		sim.p[who].placing = list[i];

		/* Place card */
		place_card(&sim, who, list[i]);

		/* Try current choice */
		if (phase == PHASE_DEVELOP)
		{
			/* Develop choice */
			develop_action(&sim, who, list[i]);
		}
		else
		{
			/* Settle choice */
			settle_action(&sim, who, list[i]);
		}

		/* Simulate rest of turn */
		complete_turn(&sim, 0);

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

	/* Return best choice */
	return best;
}

/*
 * Helper function for "ai_choose_pay" below.
 *
 * Here we try different combinations of discards to pay the remaining cost
 * of a played card.
 */
static void ai_choose_pay_aux2(game *g, int who, int which, int list[], int num,
                               int special[], int num_special,
                               int next, int chosen, int chosen_special,
                               int *best, int *best_special, double *b_s)
{
	game sim;
	int payment[MAX_DECK], used[MAX_DECK], n = 0, n_used = 0;
	double score;
	int i;

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

			/* Attempt to pay */
			if (!payment_callback(&sim, who, which, payment, i,
			                      used, n_used))
			{
				/* Skip */
				continue;
			}

			/* Simulate rest of current phase only */
			complete_turn(&sim, 1);

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

		/* Attempt to pay */
		if (!payment_callback(&sim, who, which, payment, n, used,
		                      n_used))
		{
			/* Illegal payment */
			return;
		}

		/* Simulate rest of turn */
		complete_turn(&sim, 0);

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
	ai_choose_pay_aux2(g, who, which, list, num, special, num_special,
	                   next + 1, chosen, chosen_special,
	                   best, best_special, b_s);

	/* Try with current card */
	ai_choose_pay_aux2(g, who, which, list, num, special, num_special,
	                   next + 1, chosen | (1 << next), chosen_special,
	                   best, best_special, b_s);
}

/*
 * Helper function for "ai_choose_pay" below.
 *
 * Here we try different combinations of special abilities to pay for a
 * played card.
 */
static void ai_choose_pay_aux1(game *g, int who, int which, int list[], int num,
                               int special[], int num_special,
                               int next, int chosen_special,
                               int *best, int *best_special, double *b_s)
{
	/* Check for no more special abilities to try */
	if (next == num_special)
	{
		/* Try payment with different card combinations */
		ai_choose_pay_aux2(g, who, which, list, num, special,
		                   num_special, 0, 0, chosen_special, best,
		                   best_special, b_s);

		/* Done */
		return;
	}

	/* Try without current ability */
	ai_choose_pay_aux1(g, who, which, list, num, special, num_special,
	                   next + 1, chosen_special, best, best_special, b_s);

	/* Try with current ability */
	ai_choose_pay_aux1(g, who, which, list, num, special, num_special,
	                   next + 1, chosen_special | (1 << next), best,
	                   best_special, b_s);
}

/*
 * Choose method of payment.
 */
static void ai_choose_pay(game *g, int who, int which, int list[], int num,
                          int special[], int num_special)
{
	player *p_ptr;
	double b_s = -1;
	int payment[MAX_DECK], used[MAX_DECK], n = 0, n_used = 0;
	int best = 0, best_special = 0, i;

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Find best set of special abilities */
	ai_choose_pay_aux1(g, who, which, list, num, special, num_special, 0, 0,
	                   &best, &best_special, &b_s);

	if (b_s == -1)
	{
		printf("Couldn't find valid payment!\n");
		exit(1);
	}

	/* Loop over set of chosen special cards */
	for (i = 0; (1 << i) <= best_special; i++)
	{
		/* Check for bit set */
		if (best_special & (1 << i))
		{
			/* Add card to list */
			used[n_used++] = special[i];
		}
	}

	/* Loop over set of chosen payment cards */
	for (i = 0; (1 << i) <= best; i++)
	{
		/* Check for bit set */
		if (best & (1 << i))
		{
			/* Add card to list */
			payment[n++] = list[i];
		}
	}

	/* Check for simulated game */
	if (g->simulation)
	{
		/* Clear payment cards */
		for (i = 0; i < n; i++) payment[i] = -1;
	}

	/* Pay using best set */
	payment_callback(g, who, which, payment, n, used, n_used);
}

/*
 * Choose world to takeover.
 */
static int ai_choose_takeover(game *g, int who, int list[], int num,
                              int special[], int num_special)
{
	game sim;
	player *p_ptr;
	double score, b_s = -1;
	int best = -1, best_special = 0;
	int match;
	int i, j, k;

	/* Loop over special cards (takeover powers) to use */
	for (i = 0; i < num_special; i++)
	{
		/* Loop over eligible takeover targets */
		for (j = 0; j < num; j++)
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

			/* Get player pointer */
			p_ptr = &sim.p[who];

			/* Mark takeover */
			sim.takeover_target[sim.num_takeover] = list[j];
			sim.takeover_who[sim.num_takeover] = who;
			sim.takeover_power[sim.num_takeover] = special[i];
			sim.num_takeover++;

			/* Pay for extra military for this takeover */
			settle_action(&sim, who, -1);

			/* Complete turn */
			complete_turn(&sim, 0);

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
	int i, rv;

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
				c_ptr->owner = opponent;
				
				/* Check for good on card */
				if (c_ptr->covered != -1)
				{
					/* Get good pointer */
					c_ptr = &sim.deck[c_ptr->covered];

					/* Move good as well */
					c_ptr->owner = opponent;
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
			c_ptr->owner = opponent;
			
			/* Check for good on card */
			if (c_ptr->covered != -1)
			{
				/* Get good pointer */
				c_ptr = &sim.deck[c_ptr->covered];

				/* Move good as well */
				c_ptr->owner = opponent;
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
                             int deficit, int list[], int num,
                             int special[], int num_special)
{
	player *p_ptr;
	double b_s = -1;
	int payment[MAX_DECK], used[MAX_DECK], n = 0, n_used = 0;
	int best = 0, best_special = 0, i;

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Find best set of special abilities */
	ai_choose_defend_aux1(g, who, which, opponent, deficit, list, num,
	                      special, num_special, 0, 0, &best, &best_special,
	                      &b_s);

	if (b_s == -1)
	{
		printf("Couldn't find valid payment!\n");
		exit(1);
	}

	/* Loop over set of chosen special cards */
	for (i = 0; (1 << i) <= best_special; i++)
	{
		/* Check for bit set */
		if (best_special & (1 << i))
		{
			/* Add card to list */
			used[n_used++] = special[i];
		}
	}

	/* Loop over set of chosen payment cards */
	for (i = 0; (1 << i) <= best; i++)
	{
		/* Check for bit set */
		if (best & (1 << i))
		{
			/* Add card to list */
			payment[n++] = list[i];
		}
	}

	/* Check for simulated game */
	if (g->simulation)
	{
		/* Clear payment cards */
		for (i = 0; i < n; i++) payment[i] = -1;
	}

	/* Defend using best set */
	defend_callback(g, who, deficit, payment, n, used, n_used);
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
static void ai_choose_trade(game *g, int who, int list[], int num, int no_bonus)
{
	game sim;
	int i, best = -1;
	double score, b_s = -1;

	/* Condense good list */
	num = condense_goods(g, list, num);

	/* Loop over choices */
	for (i = 0; i < num; i++)
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
#if 0
			/* Use remaining consume powers */
			while (consume_action(&sim, who));

			/* Simulate rest of turn */
			complete_turn(&sim, 0);
#endif

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
		printf("Could not find trade\n");
		exit(1);
	}

	/* Trade best choice */
	trade_chosen(g, who, best, no_bonus);
}

/*
 * Provide rough score of consume powers for simulating opponents' choices.
 */
static int score_consume(game *g, int who, power *o_ptr)
{
	int vp = 0, card = 0, goods = 1;
	int score;

	/* Always discard from hand last */
	if (o_ptr->code & P4_DISCARD_HAND) return 0;

	/* Check for VP awarded */
	if (o_ptr->code & P4_GET_VP) vp += o_ptr->value;

	/* Check for card awarded */
	if (o_ptr->code & P4_GET_CARD) card += o_ptr->value;

	/* Check for cards awarded */
	if (o_ptr->code & P4_GET_2_CARD) card += o_ptr->value * 2;

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
	score = (vp * 100 + card * 50) / goods;

	/* Use specific consume powers first */
	if (!(o_ptr->code & P4_CONSUME_ANY)) score++;

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
static void ai_choose_consume(game *g, int who, power o_list[], int num)
{
	game sim;
	power *o_ptr;
	int i, j, best = -1, skip[100];
	double score, b_s = -1;

	/* Check for only one power to use */
	if (num == 1)
	{
		/* Use power */
		consume_chosen(g, who, &o_list[0]);

		/* Done */
		return;
	}

	/* Check for simple powers */
	for (i = 0; i < num; i++)
	{
		/* Get power pointer */
		o_ptr = &o_list[i];

		/* Check for powers that should always be used first */
		if ((o_ptr->code & P4_DRAW) ||
		    (o_ptr->code & P4_DRAW_LUCKY) ||
		    (o_ptr->code & P4_VP))
		{
			/* Use power */
			consume_chosen(g, who, o_ptr);

			/* Done */
			return;
		}
	}

	/* Check for simulated opponent's turn */
	if (g->simulation && who != g->sim_who)
	{
		/* Loop over choices */
		for (i = 0; i < num; i++)
		{
			/* Get score */
			score = score_consume(g, who, &o_list[i]);

			/* Check for better */
			if (score > b_s)
			{
				/* Track best */
				b_s = score;
				best = i;
			}
		}

		/* Use best option */
		consume_chosen(g, who, &o_list[best]);

		/* Done */
		return;
	}

	/* Clear skip array */
	for (i = 0; i < num; i++) skip[i] = 0;

	/* Loop over powers */
	for (i = 0; i < num; i++)
	{
		/* Loop over other powers */
		for (j = 0; j < num; j++)
		{
			/* Skip first power */
			if (i == j) continue;

			/* Ignore already skipped powers */
			if (skip[j]) continue;

			/* Check for identical powers */
			if (o_list[i].code == o_list[j].code &&
			    o_list[i].value == o_list[j].value &&
			    o_list[i].times == o_list[j].times)
			{
				/* Skip power */
				skip[i] = 1;
				break;
			}

			/* Check for better trade power */
			if ((o_list[i].code & P4_TRADE_ACTION) &&
			    (o_list[j].code & P4_TRADE_ACTION) &&
			    (o_list[i].code & P4_TRADE_NO_BONUS))
			{
				/* Skip power */
				skip[i] = 1;
				break;
			}

			/* Check for more reward */
			if (o_list[i].code == o_list[j].code &&
			    o_list[i].times == o_list[j].times &&
			    o_list[i].value < o_list[j].value)
			{
				/* Skip power */
				skip[i] = 1;
				break;
			}

			/* Stop looking if times and value not identical */
			if (o_list[i].times != o_list[j].times ||
			    o_list[i].value != o_list[j].value) continue;

			/* Check for better reward */
			if (((o_list[j].code==(o_list[i].code | P4_GET_CARD)) ||
			     (o_list[j].code==(o_list[i].code | P4_GET_VP))))
			{
				/* Skip power */
				skip[i] = 1;
				break;
			}


			/* Check for more general consume power */
			if ((o_list[i].code & ~CONSUME_TYPE_MASK) ==
			    (o_list[j].code & ~CONSUME_TYPE_MASK) &&
			    (o_list[j].code & P4_CONSUME_ANY))
			{
				/* Skip power */
				skip[i] = 1;
				break;
			}

			/* Check for more general power and better reward */
			if ((((o_list[i].code&~CONSUME_TYPE_MASK)|P4_GET_CARD)==
			      (o_list[j].code & ~CONSUME_TYPE_MASK) ||
			     ((o_list[i].code&~CONSUME_TYPE_MASK)|P4_GET_VP) ==
			      (o_list[j].code & ~CONSUME_TYPE_MASK)) &&
			    (o_list[j].code & P4_CONSUME_ANY))
			{
				/* Skip power */
				skip[i] = 1;
				break;
			}
		}
	}

	/* Loop over choices */
	for (i = 0; i < num; i++)
	{
		/* Check for skip */
		if (skip[i]) continue;

		/* Save "consume from hand" for last */
		if (o_list[i].code & P4_DISCARD_HAND) continue;

		/* Simulate game */
		simulate_game(&sim, g, who);

		/* Try using current consume power */
		consume_chosen(&sim, who, &o_list[i]);

		/* Use remaining consume powers */
		/* while (consume_action(&sim, who)); */

		/* Simulate rest of turn */
		/* complete_turn(&sim, 0); */

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
		/* Use first power */
		consume_chosen(g, who, &o_list[0]);
	}
	else
	{
		/* Use best option */
		consume_chosen(g, who, &o_list[best]);
	}
}

/*
 * Helper function for ai_choose_consume_hand().
 */
static void ai_choose_consume_hand_aux(game *g, int who, power *o_ptr,
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
		consume_hand_chosen(&sim, who, o_ptr, discards, num_discards);

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
	ai_choose_consume_hand_aux(g, who, o_ptr, list, n - 1, c, chosen << 1,
	                           best, b_s);

	/* Try with current card (if more can be chosen) */
	if (c) ai_choose_consume_hand_aux(g, who, o_ptr, list, n - 1, c - 1,
	                                  (chosen << 1) + 1, best, b_s);
}

/*
 * Choose card from hand to consume.
 */
static void ai_choose_consume_hand(game *g, int who, power *o_ptr, int list[],
                                   int num)
{
	player *p_ptr;
	double b_s = -1;
	int discards[MAX_DECK], n = 0;
	int best, i;

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Check for simulation */
	if (g->simulation)
	{
		/* Compute hand size */
		num = count_player_area(g, who, WHERE_HAND) +
		      p_ptr->fake_hand - p_ptr->fake_played_dev -
		      p_ptr->fake_played_world - p_ptr->fake_discards;

		/* Maximum number of discards */
		if (num > o_ptr->times) num = o_ptr->times;

		/* Discard from hand */
		p_ptr->fake_discards += num;

		/* Get reward */
		if (o_ptr->code & P4_GET_VP)
		{
			/* Award VPs */
			p_ptr->vp += num;
			g->vp_pool -= num;
		}
		else if (o_ptr->code & P4_GET_CARD)
		{
			/* Draw cards */
			draw_cards(g, who, num);
		}

		/* Done */
		return;
	}

	/* XXX - Check for more than 30 cards to choose from */
	if (num > 30)
	{
		/* XXX - Just discard first cards */
		consume_hand_chosen(g, who, o_ptr, list, o_ptr->times);

		/* Done */
		return;
	}

	/* Loop over number of cards discardable */
	for (i = 0; i <= o_ptr->times; i++)
	{
		/* Find best set of cards */
		ai_choose_consume_hand_aux(g, who, o_ptr, list, num, i, 0,
		                           &best, &b_s);
	}

	/* Check for failure */
	if (b_s == -1)
	{
		/* Error */
		printf("Failed to find good discard set!\n");
		exit(1);
	}

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

	/* Apply best choice */
	consume_hand_chosen(g, who, o_ptr, discards, n);
}

/*
 * Helper function for ai_choose_good().
 */
static void ai_choose_good_aux(game *g, int who, int list[], int n, int c,
                               int chosen, power *o_ptr, int *best, double *b_s)
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
		if (!good_chosen(&sim, who, o_ptr, consume, num_consume))
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
	ai_choose_good_aux(g, who, list, n - 1, c, chosen << 1, o_ptr,
	                   best, b_s);

	/* Try with current good (if more can be chosen) */
	if (c) ai_choose_good_aux(g, who, list, n - 1, c - 1,
	                             (chosen << 1) + 1, o_ptr, best, b_s);
}

/*
 * Choose goods to consume.
 */
static void ai_choose_good(game *g, int who, power *o_ptr, int goods[], int num,
                           int min, int max)
{
	double b_s = -1;
	int consume[MAX_DECK], c, n = 0;
	int best, i;

	/* Check for simulated game and opponent's turn */
	if (g->simulation && who != g->sim_who)
	{
		/* Use first goods */
		for (i = 0; i < max; i++) consume[i] = goods[i];

		/* Consume goods */
		good_chosen(g, who, o_ptr, consume, max);

		/* Done */
		return;
	}

	/* Remove identical goods from list if only needing one */
	if (max == 1)
	{
		/* Condense good list */
		num = condense_goods(g, goods, num);
	}

	/* Check for needing all goods */
	if (num == min && num == max)
	{
		/* Choose set of all goods */
		good_chosen(g, who, o_ptr, goods, num);

		/* Done */
		return;
	}

	/* Loop over allowed number of goods */
	for (c = min; c <= max; c++)
	{
		/* Find best set of goods */
		ai_choose_good_aux(g, who, goods, num, c, 0, o_ptr, &best,&b_s);
	}

	/* Check for failure */
	if (b_s == -1)
	{
		/* Error */
		printf("Failed to find consume set!\n");
		exit(1);
	}

	/* Loop over set of chosen cards */
	for (i = 0; (1 << i) <= best; i++)
	{
		/* Check for bit set */
		if (best & (1 << i))
		{
			/* Add card to list */
			consume[n++] = goods[i];
		}
	}

	/* Apply best choice */
	good_chosen(g, who, o_ptr, consume, n);
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
		if (c_ptr->known & (1 << who)) continue;

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
			if (c_ptr->known & (1 << who)) continue;

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
			sim.deck[j].owner = who;
			sim.deck[j].where = WHERE_HAND;

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
unsigned long long choose(int n, int k)
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
		if (c_ptr->known & (1 << who)) continue;

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
			if (c_ptr->known & (1 << who)) continue;

			/* Check for more expensive card */
			if (c_ptr->d_ptr->cost > cost) num_win++;
		}

		/* Get chance of losing */
		chance = 1.0 * choose(count - num_win, cost) /
		               choose(count, cost);

		/* Simulate game */
		simulate_game(&sim, g, who);

		/* Assume we lose the card */
		sim.deck[list[i]].owner = -1;
		sim.deck[list[i]].where = WHERE_DISCARD;

		/* Start with losing chance */
		score = chance * eval_game(&sim, who);

		/* Simulate game */
		simulate_game(&sim, g, who);

		/* Assume we win a card */
		draw_card(&sim, who);

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
	card *c_ptr;
	double score, b_s = -1;
	int i, b_i = -1;

	/* Loop over choices */
	for (i = 0; i < num; i++)
	{
		/* Simulate game */
		simulate_game(&sim, g, who);

		/* Get card pointer */
		c_ptr = &sim.deck[list[i]];

		/* Take card and put it in hand */
		c_ptr->owner = who;
		c_ptr->where = WHERE_HAND;

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
static void ai_choose_windfall(game *g, int who, int list[], int num)
{
	game sim;
	int i, best = -1;
	double score, b_s = -1;

	/* Condense good list */
	num = condense_goods(g, list, num);

	/* Check for only one remaining choice */
	if (num == 1)
	{
		/* Produce on this world */
		produce_world(g, who, list[0]);

		/* Done */
		return;
	}

	/* Loop over choices */
	for (i = 0; i < num; i++)
	{
		/* Simulate game */
		simulate_game(&sim, g, who);

		/* Try producing on this world */
		produce_world(&sim, who, list[i]);

		/* Use remaining produce powers */
		while (produce_action(&sim, who));

		/* Simulate rest of turn */
		complete_turn(&sim, 0);

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
		printf("Could not find windfall production\n");
		exit(1);
	}

	/* Use best choice */
	produce_world(g, who, best);
}

/*
 * Choose produce power to use.
 */
static void ai_choose_produce(game *g, int who, power o_list[], int num)
{
	game sim;
	int i, best = -1;
	double score, b_s = -1;

	/* Loop over choices */
	for (i = 0; i < num; i++)
	{
		/* Simulate game */
		simulate_game(&sim, g, who);

		/* Try using current produce power */
		produce_chosen(&sim, who, &o_list[i]);

		/* Use remaining produce powers */
		while (produce_action(&sim, who));

		/* Simulate rest of turn */
		complete_turn(&sim, 0);

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

	/* Use best option */
	produce_chosen(g, who, &o_list[best]);
}

/*
 * Choose card from hand to discard in order to produce on the given world.
 */
static void ai_choose_discard_produce(game *g, int who, int world, int list[],
                                   int num)
{
	game sim;
	player *p_ptr;
	double b_s, score;
	int i, b_i = -1;

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Check for simulation */
	if (g->simulation)
	{
		/* Check for at least one card in hand */
		if (count_player_area(g, who, WHERE_HAND) +
		    p_ptr->fake_hand - p_ptr->fake_played_dev -
		    p_ptr->fake_played_world - p_ptr->fake_discards > 0)
		{
			/* Discard from hand */
			p_ptr->fake_discards++;

			/* Produce */
			produce_world(g, who, world);
		}

		/* Done */
		return;
	}

	/* Simulate game */
	simulate_game(&sim, g, who);

	/* Use remaining produce powers without discarding */
	while (produce_action(&sim, who));

	/* Simulate rest of turn */
	complete_turn(&sim, 0);

	/* Get score without doing anything */
	b_s = eval_game(&sim, who);

	/* Loop over choices */
	for (i = 0; i < num; i++)
	{
		/* Simulate game */
		simulate_game(&sim, g, who);

		/* Try discard */
		discard_produce_chosen(&sim, who, world, list[i]);

		/* Use remaining produce powers */
		while (produce_action(&sim, who));

		/* Simulate rest of turn */
		complete_turn(&sim, 0);

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

	/* Check for card chosen */
	if (b_i > -1)
	{
		/* Discard chosen card */
		discard_produce_chosen(g, who, world, list[b_i]);
	}
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
	sprintf(fname, DATADIR "/network/rftg.eval.%d.%d%s.net", g->expanded,
	        g->num_players, g->advanced ? "a" : "");

	/* Save weights to disk */
	save_net(&eval, fname);

	/* Create predictor filename */
	sprintf(fname, DATADIR "/network/rftg.role.%d.%d%s.net", g->expanded,
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
interface ai_func =
{
	ai_initialize,
	ai_notify_rotation,
	ai_choose_start,
	ai_choose_action,
	ai_react_action,
	ai_choose_discard,
	ai_explore_sample,
	ai_choose_place,
	ai_choose_pay,
	ai_choose_takeover,
	ai_choose_defend,
	ai_choose_trade,
	ai_choose_consume,
	ai_choose_consume_hand,
	ai_choose_good,
	ai_choose_lucky,
	ai_choose_ante,
	ai_choose_keep,
	ai_choose_windfall,
	ai_choose_produce,
	ai_choose_discard_produce,
	ai_game_over,
	ai_shutdown
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
			                           ROLE_OUT_ADV);

			/* Create action score row */
			action_score[i] = (double *)malloc(sizeof(double) *
			                                   ROLE_OUT_ADV);

			/* Set number of action columns */
			*num_action = ROLE_OUT_ADV;
		}
	}
	else
	{
		/* Loop over players */
		for (i = 0; i < g->num_players; i++)
		{
			/* Create role row */
			role[i] = (double *)malloc(sizeof(double) * ROLE_OUT);

			/* Create action score row */
			action_score[i] = (double *)malloc(sizeof(double) *
			                                   ROLE_OUT);

			/* Set number of action columns */
			*num_action = ROLE_OUT;
		}
	}

	/* Loop over players */
	for (i = 0; i < g->num_players; i++)
	{
		/* Predict action choices */
		predict_action(g, i, role[i]);
	}

	/* Compute action scores for the advanced game */
	if (g->advanced)
	{
		/* Loop over players */
		for (who = 0; who < g->num_players; who++)
		{
			/* Clear scores */
			for (i = 0; i < ROLE_OUT_ADV; i++)
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
				for (oa = 0; oa < ROLE_OUT_ADV; oa++)
				{
					/* Get probability of this combo */
					prob = role[!who][oa];

					/* Check for too low probability */
					if (prob < threshold_l) continue;

					/* Check for too high probability */
					if (prob >= threshold_h) continue;

					/* Get our action scores against this */
					ai_choose_action_advanced_aux(g, who,
					           oa, action_score[who], prob);

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
			for (j = 0; j < ROLE_OUT; j++)
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
		for (i = 0; i < ROLE_OUT; i++) action_score[who][i] = 0.0;

		/* Clear active actions */
		for (i = 0; i < MAX_ACTION; i++) sim.action_selected[i] = 0;

		/* Evaluate action tree */
		ai_choose_action_aux(&sim, who, 0, 1.0, &used,
		                     action_score[who], role, threshold);

		/* Normalize action scores */
		for (i = 0; i < ROLE_OUT; i++)
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
	int i, n, most;

	/* Increase learning rate */
	eval.alpha *= 10;

	/* Clear some important game fields that may yet be uninitialized */
	g->simulation = 0;
	g->vp_pool = 0;
	g->deck_size = 0;
	memset(g->deck, 0, sizeof(card) * MAX_DECK);
	memset(g->goal_active, 0, sizeof(int) * MAX_GOAL);
	memset(g->goal_avail, 0, sizeof(int) * MAX_GOAL);

	/* Clear some uninitialized player information */
	for (i = 0; i < g->num_players; i++)
	{
		/* Clear player's card counts and winner flag */
		memset(g->p[i].goal_claimed, 0, sizeof(int) * MAX_GOAL);
		g->p[i].fake_hand = 0;
		g->p[i].total_fake = 0;
		g->p[i].fake_discards = 0;
		g->p[i].fake_played_dev = 0;
		g->p[i].fake_played_world = 0;
		g->p[i].winner = 0;
		g->p[i].vp = 0;
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
