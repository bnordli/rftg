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

void dump_hand(game *g, int who)
{
	card *c_ptr;
	int i;

	/* Loop over cards */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Skip cards not owned */
		if (c_ptr->owner != who) continue;

		/* Skip cards in wrong area */
		if (c_ptr->where != WHERE_HAND) continue;

		printf("%s\n", c_ptr->d_ptr->name);
	}
}
void dump_active(game *g, int who)
{
	card *c_ptr;
	int i;

	/* Loop over cards */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Skip cards not owned */
		if (c_ptr->owner != who) continue;

		/* Skip cards in wrong area */
		if (c_ptr->where != WHERE_ACTIVE) continue;

		printf("%s\n", c_ptr->d_ptr->name);
	}
}

/*
 * Return a random number using the given argument as a seed.
 *
 * Algorithm from rand() manpage.
 */
int myrand(unsigned int *seed)
{
	*seed = *seed * 1103515245 + 12345;
	return ((unsigned)(*seed/65536) % 32768);
}

/*
 * Return the number of cards in the draw deck.
 */
static int count_draw(game *g)
{
	card *c_ptr;
	int i, n = 0;

	/* Loop over cards */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Count cards in draw deck */
		if (c_ptr->where == WHERE_DECK) n++;
	}

	/* Return count */
	return n;
}

/*
 * Return the number of card's in the given player's hand or active area.
 */
int count_player_area(game *g, int who, int where)
{
	card *c_ptr;
	int i, n = 0;

	/* Loop over cards */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Skip cards not owned */
		if (c_ptr->owner != who) continue;

		/* Skip cards in wrong area */
		if (c_ptr->where != where) continue;

		/* Count card */
		n++;
	}

	/* Return card */
	return n;
}

/*
 * Return true if the player has a given card design active.
 */
static int player_has(game *g, int who, design *d_ptr)
{
	card *c_ptr;
	int i;

	/* Loop over cards */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Skip incorrect cards */
		if (c_ptr->d_ptr != d_ptr) continue;

		/* Skip unowned cards */
		if (c_ptr->owner != who) continue;

		/* Skip inactive cards */
		if (c_ptr->where != WHERE_ACTIVE) continue;

		/* Card is active */
		return 1;
	}

	/* Assume not */
	return 0;
}

/*
 * Return the number of active cards with the given flags.
 */
int count_active_flags(game *g, int who, int flags)
{
	card *c_ptr;
	int i, count = 0;

	/* Loop over cards */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Skip unowned cards */
		if (c_ptr->owner != who) continue;

		/* Skip inactive cards */
		if (c_ptr->where != WHERE_ACTIVE) continue;

		/* Skip just-played cards */
		if (c_ptr->temp) continue;

		/* Check for correct flags */
		if ((c_ptr->d_ptr->flags & flags) == flags) count++;
	}

	/* Return count */
	return count;
}

/*
 * Check if a player has selected the given action.
 */
int player_chose(game *g, int who, int act)
{
	player *p_ptr;

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Check for selected */
	return (p_ptr->action[0] == act) || (p_ptr->action[1] == act);
}

/*
 * Refresh the draw deck.
 */
static void refresh_draw(game *g)
{
	card *c_ptr;
	int i;

	/* Message */
	if (!g->simulation)
	{
		/* Send message */
		message_add("Refreshing draw deck.\n");
	}

	/* Loop over cards */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Skip cards not in discard pile */
		if (c_ptr->where != WHERE_DISCARD) continue;

		/* Move card to draw deck */
		c_ptr->where = WHERE_DECK;

		/* Card's location is no longer known to anyone */
		c_ptr->known = 0;
	}
}

/*
 * Return a card pointer from the draw deck.
 */
card *random_draw(game *g)
{
	card *c_ptr = NULL;
	int i, n;

	/* Count draw deck size */
	n = count_draw(g);

	/* Check for no cards */
	if (!n)
	{
		/* Refresh draw deck */
		refresh_draw(g);

		/* Recount */
		n = count_draw(g);

		/* XXX Check for still no cards */
		if (!n)
		{
			/* Error */
			printf("Empty draw and discard piles!\n");
			exit(1);
		}
	}

	/* Choose randomly */
	n = myrand(&g->random_seed) % n;

	/* Loop over cards */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Skip cards not in draw deck */
		if (c_ptr->where != WHERE_DECK) continue;

		/* Check for chosen card */
		if (!(n--)) break;
	}

	/* Clear chosen card's location */
	c_ptr->where = -1;

	/* Check for just-emptied draw pile */
	if (!count_draw(g)) refresh_draw(g);

	/* Return chosen card */
	return c_ptr;
}

/*
 * Draw a card from the deck.
 *
 * We mark the card as "temporary", which will be removed at the end of
 * the phase, possibly after discarding some temporary cards (explore
 * phase).
 */
void draw_card(game *g, int who)
{
	player *p_ptr;
	card *c_ptr;

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Check for simulated game */
	if (g->simulation)
	{
		/* Count fake cards */
		p_ptr->fake_hand++;

		/* Track total number of fake cards seen */
		p_ptr->total_fake++;

		/* Get card from draw pile */
		c_ptr = random_draw(g);

		/* Move card to discard to simulate deck cycling */
		c_ptr->where = WHERE_DISCARD;

		/* Done */
		return;
	}

	/* Choose random card */
	c_ptr = random_draw(g);

	/* Move card to player */
	c_ptr->owner = who;

	/* Move card to hand */
	c_ptr->where = WHERE_HAND;

	/* Mark card as temporary */
	if (g->cur_action <= ACT_EXPLORE_5_0) c_ptr->temp = 1;

	/* Card's location is known to player */
	c_ptr->known |= 1 << who;
}

/*
 * Draw a number of cards, as in draw_card() above.
 */
void draw_cards(game *g, int who, int num)
{
	int i;

	/* Draw required number */
	for (i = 0; i < num; i++) draw_card(g, who);
}

/*
 * Clear temp flags on all cards and player structures.
 */
void clear_temp(game *g)
{
	player *p_ptr;
	card *c_ptr;
	int i, j;

	/* Loop over cards */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Clear temp flag */
		c_ptr->temp = 0;

		/* Clear produced flag */
		c_ptr->produced = 0;

		/* Loop over used flags */
		for (j = 0; j < MAX_POWER; j++)
		{
			/* Clear flag */
			c_ptr->used[j] = 0;
		}
	}

	/* Loop over players */
	for (i = 0; i < g->num_players; i++)
	{
		/* Get player pointer */
		p_ptr = &g->p[i];

		/* Clear bonus used flag */
		p_ptr->phase_bonus_used = 0;

		/* Clear bonus military */
		p_ptr->bonus_military = 0;
	}
}

/*
 * Called when player has chosen which cards to discard.
 */
void discard_callback(game *g, int who, int list[], int num)
{
	player *p_ptr;
	card *c_ptr;
	int i;

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Loop over choices */
	for (i = 0; i < num; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[list[i]];

		/* Disown card */
		c_ptr->owner = -1;

		/* Move card to discard */
		c_ptr->where = WHERE_DISCARD;

		/* Clear temp flag */
		c_ptr->temp = 0;
	}
}

/*
 * Ask the player to discard temporary cards, down to the number given.
 */
void discard_to(game *g, int who, int to, int discard_any)
{
	player *p_ptr;
	card *c_ptr;
	int list[MAX_DECK];
	int i, n = 0;

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Loop over cards */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Skip cards not owned by given player */
		if (c_ptr->owner != who) continue;

		/* Skip cards not in hand */
		if (c_ptr->where != WHERE_HAND) continue;

		/* Skip non-temporary cards if we can't discard them */
		if (!discard_any && !c_ptr->temp) continue;

		/* Keep an extra card if it wasn't newly drawn */
		if (discard_any && !c_ptr->temp) to++;

		/* Add card to list */
		list[n++] = i;
	}

	/* Loop over fake cards */
	for (i = 0; i < p_ptr->fake_hand - p_ptr->fake_discards; i++)
	{
		/* Add fake card to list */
		list[n++] = -1;
	}

	/* Check for nothing to discard */
	if (n - to == 0) return;

	/* Ask player to choose cards to discard */
	p_ptr->control->choose_discard(g, who, list, n, n - to);
}

/*
 * Return list of powers on all active cards of the given player.
 *
 * We ignore cards with the temp flag set.
 */
static int get_powers(game *g, int who, int phase, power *list)
{
	card *c_ptr;
	power *o_ptr;
	int i, j, n = 0;

	/* Loop over cards */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Skip cards not belonging to given player */
		if (c_ptr->owner != who) continue;

		/* Skip inactive cards */
		if (c_ptr->where != WHERE_ACTIVE) continue;

		/* Check for temp flag */
		if (c_ptr->temp) continue;

		/* Loop over card's powers */
		for (j = 0; j < c_ptr->d_ptr->num_power; j++)
		{
			/* Get power pointer */
			o_ptr = &c_ptr->d_ptr->powers[j];

			/* Skip used powers */
			if (c_ptr->used[j]) continue;

			/* Skip incorrect phase */
			if (o_ptr->phase != phase) continue;

			/* Copy power */
			list[n++] = *o_ptr;
		}
	}

	/* Return length of list */
	return n;
}

/*
 * Add a good to a played card.
 */
void add_good(game *g, card *c_ptr)
{
	card *good;

	/* Get random card to use as good */
	good = random_draw(g);

	/* Mark card as good */
	good->good = 1;

	/* Move card to owner */
	good->owner = c_ptr->owner;

	/* Move card to "good" location */
	good->where = WHERE_GOOD;

	/* Mark covered card */
	c_ptr->covered = good - g->deck;
}

/*
 * Handle the Explore Phase.
 */
void phase_explore(game *g)
{
	player *p_ptr;
	power o_list[100], *o_ptr;
	int i, j, n, draw, keep, drawn[MAX_PLAYER], discard_any = 0;
	char msg[1024];

	/* Loop over players */
	for (i = 0; i < g->num_players; i++)
	{
		/* Get player pointer */
		p_ptr = &g->p[i];

		/* Assume we draw 2 cards */
		draw = 2;

		/* Check for chosen "+5 draw" explore */
		if (player_chose(g, i, ACT_EXPLORE_5_0)) draw += 5;

		/* Check for chosen "+1 draw" explore */
		if (player_chose(g, i, ACT_EXPLORE_1_1)) draw += 1;

		/* Get list of explore powers */
		n = get_powers(g, i, PHASE_EXPLORE, o_list);

		/* Loop over powers */
		for (j = 0; j < n; j++)
		{
			/* Get power pointer */
			o_ptr = &o_list[j];

			/* Skip powers that do not have extra draw */
			if (!(o_ptr->code & P1_DRAW)) continue;

			/* Add value */
			draw += o_ptr->value;
		}

		/* Draw cards */
		draw_cards(g, i, draw);

		/* Remember cards drawn */
		drawn[i] = draw;
	}

	/* Loop over players */
	for (i = 0; i < g->num_players; i++)
	{
		/* Get player pointer */
		p_ptr = &g->p[i];

		/* Assume we keep one card */
		keep = 1;

		/* Assume player has no "discard any" power */
		discard_any = 0;

		/* Check for chosen "+1 keep" explore */
		if (player_chose(g, i, ACT_EXPLORE_1_1)) keep += 1;

		/* Get list of explore powers */
		n = get_powers(g, i, PHASE_EXPLORE, o_list);

		/* Loop over powers */
		for (j = 0; j < n; j++)
		{
			/* Get power pointer */
			o_ptr = &o_list[j];

			/* Skip powers that do not have extra keep */
			if (!(o_ptr->code & P1_KEEP)) continue;

			/* Add value */
			keep += o_ptr->value;
		}

		/* Loop over powers */
		for (j = 0; j < n; j++)
		{
			/* Get power pointer */
			o_ptr = &o_list[j];

			/* Check for "discard any" power */
			if (o_ptr->code & P1_DISCARD_ANY) discard_any = 1;
		}

		/* Have player discard extras */
		discard_to(g, i, keep, discard_any);

		/* Check for aborted game */
		if (g->game_over) return;

		/* Message */
		if (!g->simulation)
		{
			/* Check for discarding any */
			if (discard_any)
			{
				/* Format message */
				sprintf(msg, "%s draws %d and discards %d.\n",
				        p_ptr->name, drawn[i], drawn[i] - keep);
			}
			else
			{
				/* Format message */
				sprintf(msg, "%s draws %d and keeps %d.\n",
				        p_ptr->name, drawn[i], keep);
			}

			/* Add message */
			message_add(msg);
		}

		/* Check for our simulated game */
		if (g->simulation && g->sim_who == i &&
		    (player_chose(g, i, ACT_EXPLORE_5_0) ||
		     player_chose(g, i, ACT_EXPLORE_1_1)))
		{
			/* Place "sample" cards in hand */
			p_ptr->control->explore_sample(g, i, drawn[i], keep,
			                               discard_any);
		}
	}

	/* Clear leftover temp flags */
	clear_temp(g);
}

/*
 * Place a card on the table for the given player.
 */
void place_card(game *g, int who, int which)
{
	player *p_ptr;
	card *c_ptr;

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Get card being placed */
	c_ptr = &g->deck[which];

	/* Give card to player */
	c_ptr->owner = who;

	/* Place card */
	c_ptr->where = WHERE_ACTIVE;

	/* Location is known to all */
	c_ptr->known = ~0;

	/* Count order played */
	c_ptr->order = p_ptr->table_order++;

	/* Add a good to windfall worlds */
	if (c_ptr->d_ptr->flags & FLAG_WINDFALL) add_good(g, c_ptr);

	/* Set temp flag so that card's powers don't take effect immediately */
	c_ptr->temp = 1;
}

/*
 * Ask a player to discard to pay for a development card played.
 */
static void pay_devel(game *g, int who, int cost)
{
	player *p_ptr;
	card *c_ptr;
	power *o_ptr;
	int list[MAX_DECK], special[MAX_DECK];
	int i, j, n = 0, num_special = 0;

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Loop over active cards */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Skip cards not owned by given player */
		if (c_ptr->owner != who) continue;

		/* Skip inactive cards */
		if (c_ptr->where != WHERE_ACTIVE) continue;

		/* Skip just-played cards */
		if (c_ptr->temp) continue;

		/* Loop over card powers */
		for (j = 0; j < c_ptr->d_ptr->num_power; j++)
		{
			/* Get power pointer */
			o_ptr = &c_ptr->d_ptr->powers[j];

			/* Skip non-Develop power */
			if (o_ptr->phase != PHASE_DEVELOP) continue;

			/* Check for discard to reduce cost */
			if (o_ptr->code & P2_DISCARD_REDUCE)
			{
				/* Add to special list */
				special[num_special++] = i;
			}
		}
	}

	/* Loop over cards */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Skip cards not owned by given player */
		if (c_ptr->owner != who) continue;

		/* Skip cards not in hand */
		if (c_ptr->where != WHERE_HAND) continue;

		/* Add card to list */
		list[n++] = i;
	}

	/* Add fake cards to list */
	for (i = 0; i < p_ptr->fake_hand - p_ptr->fake_discards; i++)
	{
		/* Add one fake card */
		list[n++] = -1;
	}

	/* Do not ask for payment if not is needed or allowed */
	if (cost == 0 && !num_special) return;

	/* Ask player to decide how to pay */
	p_ptr->control->choose_pay(g, who, p_ptr->placing, list, n, special,
	                           num_special);
}

/*
 * Called when a player has chosen how to pay for a development.
 *
 * We return 0 if the payment would not succeed.
 */
int devel_callback(game *g, int who, int which, int list[], int num,
                   int special[], int num_special)
{
	player *p_ptr;
	card *c_ptr;
	power *o_ptr, o_list[100];
	int i, j, n;
	int cost, reduce = 0;
	char msg[1024];

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Get pointer to card being played */
	c_ptr = &g->deck[which];

	/* Get card cost */
	cost = c_ptr->d_ptr->cost;

	/* Get list of develop powers */
	n = get_powers(g, who, PHASE_DEVELOP, o_list);

	/* Check for develop action chosen */
	if (player_chose(g, who, g->cur_action)) reduce += 1;

	/* Loop over develop powers */
	for (i = 0; i < n; i++)
	{
		/* Get power pointer */
		o_ptr = &o_list[i];

		/* Check for reduce power */
		if (o_ptr->code & P2_REDUCE) reduce += o_ptr->value;
	}

	/* Loop over special cards used */
	for (i = 0; i < num_special; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[special[i]];

		/* Loop over card's powers */
		for (j = 0; j < c_ptr->d_ptr->num_power; j++)
		{
			/* Get power pointer */
			o_ptr = &c_ptr->d_ptr->powers[j];

			/* Skip non-Develop power */
			if (o_ptr->phase != PHASE_DEVELOP) continue;

			/* Check for discard to reduce cost power */
			if (o_ptr->code & P2_DISCARD_REDUCE)
			{
				/* Disown card */
				c_ptr->owner = -1;

				/* Discard card */
				c_ptr->where = WHERE_DISCARD;

				/* Message */
				if (!g->simulation)
				{
					/* Format message */
					sprintf(msg, "%s discards %s.\n",
					        p_ptr->name,
					        c_ptr->d_ptr->name);

					/* Send message */
					message_add(msg);
				}

				/* Reduce cost */
				reduce += o_ptr->value;
			}
		}
	}

	/* Get pointer to card being played */
	c_ptr = &g->deck[p_ptr->placing];

	/* Reduce cost */
	cost -= reduce;

	/* Do not reduce below zero */
	if (cost < 0) cost = 0;

	/* Check for incorrect payment */
	if (cost != num) return 0;

	/* Message */
	if (!g->simulation)
	{
		/* Format message */
		sprintf(msg, "%s pays %d.\n", p_ptr->name, num);

		/* Send message */
		message_add(msg);
	}

	/* Check for simulation */
	if (g->simulation)
	{
		/* Simulate payment */
		p_ptr->fake_discards += num;

		/* Success */
		return 1;
	}

	/* Loop over cards chosen as payment */
	for (i = 0; i < num; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[list[i]];

		/* Disown card */
		c_ptr->owner = -1;

		/* Move to discard */
		c_ptr->where = WHERE_DISCARD;
	}

	/* Payment is good */
	return 1;
}

/*
 * The second half of the Develop Phase -- paying for chosen developments.
 */
void develop_action(game *g, int who, int placing)
{
	player *p_ptr;
	card *c_ptr;
	power o_list[100], *o_ptr;
	int i, n, cost;
	char msg[1024];

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Get card placed */
	c_ptr = &g->deck[placing];

	/* Get cost */
	cost = c_ptr->d_ptr->cost;

	/* Get list of develop powers */
	n = get_powers(g, who, PHASE_DEVELOP, o_list);

	/* Look for draw card powers */
	for (i = 0; i < n; i++)
	{
		/* Get power pointer */
		o_ptr = &o_list[i];

		/* Check for reduce cost */
		if (o_ptr->code & P2_REDUCE) cost -= o_ptr->value;
	}

	/* Check for develop action chosen */
	if (player_chose(g, who, g->cur_action)) cost--;

	/* Do not reduce cost below zero */
	if (cost < 0) cost = 0;

	/* Have player pay cost */
	pay_devel(g, who, cost);

	/* Message */
	if (!g->simulation)
	{
		/* Format message */
		sprintf(msg, "%s develops %s.\n", p_ptr->name,
		        c_ptr->d_ptr->name);

		/* Send message */
		message_add(msg);
	}

	/* Loop over powers */
	for (i = 0; i < n; i++)
	{
		/* Get power pointer */
		o_ptr = &o_list[i];

		/* Skip powers that are not "draw after" */
		if (!(o_ptr->code & P2_DRAW_AFTER)) continue;

		/* Draw cards */
		draw_cards(g, who, o_ptr->value);
	}
}

/*
 * Handle the Develop Phase.
 *
 * Ask each player what they would like to develop.
 */
void phase_develop(game *g)
{
	player *p_ptr;
	card *c_ptr;
	power o_list[100], *o_ptr;
	int list[MAX_DECK];
	int i, j, n, reduce, max;

	/* Check for simulated game */
	if (g->simulation)
	{
		/* Loop over players */
		for (i = 0; i < g->num_players; i++)
		{
			/* Get player pointer */
			p_ptr = &g->p[i];

			/* Skip simulating player */
			if (g->sim_who == i) continue;

			/* Check for at least one card in hand */
			if (count_player_area(g, i, WHERE_HAND) +
			    p_ptr->fake_hand - p_ptr->fake_discards -
			    p_ptr->fake_played_dev -
			    p_ptr->fake_played_world > 0)
			{
				/* Assume a card is played */
				p_ptr->fake_played_dev++;
			}
		}
	}

	/* Loop over players */
	for (i = 0; i < g->num_players; i++)
	{
		/* Get player pointer */
		p_ptr = &g->p[i];

		/* Set current turn */
		g->turn = i;

		/* Get list of develop powers */
		n = get_powers(g, i, PHASE_DEVELOP, o_list);

		/* Look for draw card powers */
		for (j = 0; j < n; j++)
		{
			/* Get power pointer */
			o_ptr = &o_list[j];

			/* Skip non-draw powers */
			if (!(o_ptr->code & P2_DRAW)) continue;

			/* Draw cards */
			draw_cards(g, i, o_ptr->value);
		}

		/* Clear placing selection */
		p_ptr->placing = -1;

		/* Do not ask simulated opponents */
		if (g->simulation && g->sim_who != i) continue;

		/* Assume no reduction in cost */
		reduce = 0;

		/* Check for develop action chosen */
		if (player_chose(g, i, g->cur_action)) reduce += 1;

		/* Look for cost reduction powers */
		for (j = 0; j < n; j++)
		{
			/* Get power pointer */
			o_ptr = &o_list[j];

			/* Skip non-reduce powers */
			if (!(o_ptr->code & P2_REDUCE)) continue;

			/* Total reduction */
			reduce += o_ptr->value;
		}

		/* Look for "discard to reduce" power */
		for (j = 0; j < n; j++)
		{
			/* Get power pointer */
			o_ptr = &o_list[j];

			/* Skip non-discard powers */
			if (!(o_ptr->code & P2_DISCARD_REDUCE)) continue;

			/* Assume power can be used */
			reduce += o_ptr->value;
		}

		/* Compute maximum cost */
		max = count_player_area(g, i, WHERE_HAND) + p_ptr->fake_hand -
		      p_ptr->fake_discards + reduce - 1;

		/* No cards in list */
		n = 0;

		/* Loop over cards in hand */
		for (j = 0; j < g->deck_size; j++)
		{
			/* Get card pointer */
			c_ptr = &g->deck[j];

			/* Skip unowned cards */
			if (c_ptr->owner != i) continue;

			/* Skip cards not in hand */
			if (c_ptr->where != WHERE_HAND) continue;

			/* Skip non-developments */
			if (c_ptr->d_ptr->type != TYPE_DEVELOPMENT) continue;

			/* Skip too-expensive cards */
			if (c_ptr->d_ptr->cost > max) continue;

			/* Skip duplicate card designs */
			if (player_has(g, i, c_ptr->d_ptr)) continue;

			/* Add card to list */
			list[n++] = j;
		}

		/* Check for no choices */
		if (!n) continue;

		/* Ask player to choose */
		p_ptr->placing = p_ptr->control->choose_place(g, i, list, n,
		                                              PHASE_DEVELOP);

		/* Check for aborted game */
		if (g->game_over) return;
	}

	/* Loop over players */
	for (i = 0; i < g->num_players; i++)
	{
		/* Get player pointer */
		p_ptr = &g->p[i];

		/* Skip players who are not placing anything */
		if (p_ptr->placing == -1) continue;

		/* Place card */
		place_card(g, i, p_ptr->placing);
	}

	/* Loop over players */
	for (i = 0; i < g->num_players; i++)
	{
		/* Get player pointer */
		p_ptr = &g->p[i];

		/* Skip players who are not placing anything */
		if (p_ptr->placing == -1) continue;

		/* Handle choice */
		develop_action(g, i, p_ptr->placing);

		/* Clear placing choice */
		p_ptr->placing = -1;

		/* Check for aborted game */
		if (g->game_over) return;
	}

	/* Clear any temp flags on cards */
	clear_temp(g);

	/* Check intermediate goals */
	check_goals(g);
}

/*
 * Return player's military strength against the given world.
 */
static int strength_against(game *g, int who, int world, int defend)
{
	player *p_ptr;
	card *c_ptr;
	power o_list[100], *o_ptr;
	int i, n;
	int military, good;

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Get card pointer */
	c_ptr = &g->deck[world];

	/* Get world's good type */
	good = c_ptr->d_ptr->good_type;

	/* Get Settle phase powers */
	n = get_powers(g, who, PHASE_SETTLE, o_list);

	/* Assume zero military */
	military = 0;

	/* Loop over powers */
	for (i = 0; i < n; i++)
	{
		/* Get power pointer */
		o_ptr = &o_list[i];

		/* Check for extra military */
		if (o_ptr->code & P3_EXTRA_MILITARY)
		{
			/* Check for specific good required */
			if (((o_ptr->code & P3_NOVELTY)&&good != GOOD_NOVELTY)||
			    ((o_ptr->code & P3_RARE) && good != GOOD_RARE) ||
			    ((o_ptr->code & P3_GENE) && good != GOOD_GENE) ||
			    ((o_ptr->code & P3_ALIEN) && good != GOOD_ALIEN))
			{
				/* Skip power */
				continue;
			}

			/* Check for against rebels */
			if ((o_ptr->code & P3_AGAINST_REBEL) &&
			    !(c_ptr->d_ptr->flags & FLAG_REBEL))
			{
				/* Skip power */
				continue;
			}

			/* Check for per military world */
			if (o_ptr->code & P3_PER_MILITARY)
			{
				/* Add count of military worlds */
				military += count_active_flags(g, who,
				                               FLAG_MILITARY);

				/* Done */
				continue;
			}

			/* Add to military */
			military += o_ptr->value;
		}

		/* Check for takeover defense */
		if (defend && (o_ptr->code & P3_TAKEOVER_DEFENSE))
		{
			/* Add defense for military worlds */
			military += count_active_flags(g, who,
			                               FLAG_MILITARY);

			/* Add extra defense for Rebel military worlds */
			military += count_active_flags(g, who,
			                          (FLAG_REBEL | FLAG_MILITARY));
		}
	}

	/* Add in bonus temporary military strength */
	military += p_ptr->bonus_military;

	/* Return total military for this world */
	return military;
}

/*
 * Return true if the given player can settle the given world.
 */
static int settle_legal(game *g, int who, int world, int mil_bonus)
{
	player *p_ptr;
	card *c_ptr;
	power o_list[100], *o_ptr;
	int i, n, cost, defense, military, conquer, good, pay_military;
	int pay_cost, pay_discount;
	int conquer_peaceful, conquer_bonus;
	int hand_military, hand_size;
	int takeover;

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Get card pointer */
	c_ptr = &g->deck[world];

	/* Check for takeover attempt */
	takeover = (c_ptr->owner != who);

	/* Get settle phase powers */
	n = get_powers(g, who, PHASE_SETTLE, o_list);

	/* Get initial cost/defense */
	cost = defense = c_ptr->d_ptr->cost;

	/* Check for military world */
	conquer = c_ptr->d_ptr->flags & FLAG_MILITARY;

	/* Get good type of world to be settled (if any) */
	good = c_ptr->d_ptr->good_type;

	/* Assume player has 0 military strength */
	military = 0;

	/* Assume we cannot pay for military worlds */
	pay_military = 0;

	/* Assume no discounts while paying for military */
	pay_cost = pay_discount = 0;

	/* Assume we cannot conquer peaceful worlds */
	conquer_peaceful = conquer_bonus = 0;

	/* Assume we cannot use cards in hand for military strength */
	hand_military = 0;

	/* Compute effective hand size (minus one for card played) */
	hand_size = count_player_area(g, who, WHERE_HAND) + p_ptr->fake_hand -
	            p_ptr->fake_discards - p_ptr->fake_played_dev -
	            p_ptr->fake_played_world - 1;

	/* We don't play a card from hand for takeovers */
	if (takeover) hand_size++;

	/* No card is legal if we have none */
	if (!takeover && hand_size < 0) return 0;

	/* Loop over powers */
	for (i = 0; i < n; i++)
	{
		/* Get power pointer */
		o_ptr = &o_list[i];

		/* Check for reduce cost power */
		if (o_ptr->code & P3_REDUCE)
		{
			/* Check for specific good required */
			if (((o_ptr->code & P3_NOVELTY)&&good != GOOD_NOVELTY)||
			    ((o_ptr->code & P3_RARE) && good != GOOD_RARE) ||
			    ((o_ptr->code & P3_GENE) && good != GOOD_GENE) ||
			    ((o_ptr->code & P3_ALIEN) && good != GOOD_ALIEN))
			{
				/* Skip power */
				continue;
			}

			/* Reduce cost */
			cost -= o_ptr->value;
		}

		/* Check for reduce cost to zero */
		if (o_ptr->code & P3_DISCARD_ZERO)
		{
			/* Alien production worlds cannot be affected */
			if (good == GOOD_ALIEN) continue;

			/* World can be settled without cost */
			cost = 0;
		}

		/* Check for extra military */
		if (o_ptr->code & P3_EXTRA_MILITARY)
		{
			/* Check for specific good required */
			if (((o_ptr->code & P3_NOVELTY)&&good != GOOD_NOVELTY)||
			    ((o_ptr->code & P3_RARE) && good != GOOD_RARE) ||
			    ((o_ptr->code & P3_GENE) && good != GOOD_GENE) ||
			    ((o_ptr->code & P3_ALIEN) && good != GOOD_ALIEN))
			{
				/* Skip power */
				continue;
			}

			/* Check for against rebels */
			if ((o_ptr->code & P3_AGAINST_REBEL) &&
			    !(c_ptr->d_ptr->flags & FLAG_REBEL))
			{
				/* Skip power */
				continue;
			}

			/* Check for per military world */
			if (o_ptr->code & P3_PER_MILITARY)
			{
				/* Add count of military worlds */
				military += count_active_flags(g, who,
				                               FLAG_MILITARY);

				/* Done */
				continue;
			}

			/* Add to military */
			military += o_ptr->value;
		}

		/* Check for able to pay for military worlds */
		if (o_ptr->code & P3_PAY_MILITARY)
		{
			/* Cannot pay for alien production world */
			if (good == GOOD_ALIEN) continue;
			
			/* Check for against rebels */
			if ((o_ptr->code & P3_AGAINST_REBEL) &&
			    !(c_ptr->d_ptr->flags & FLAG_REBEL))
			{
				/* Skip power */
				continue;
			}

			/* Mark ability */
			pay_military = 1;

			/* Check for bigger discount */
			if (o_ptr->value > pay_cost)
			{
				/* Remember best cost */
				pay_cost = o_ptr->value;
			}
		}

		/* Check for discount when using pay for military */
		if (o_ptr->code & P3_PAY_DISCOUNT)
		{
			/* Track discount */
			pay_discount += o_ptr->value;
		}

		/* Check for use-once military strength */
		if (o_ptr->code & P3_DISCARD_MILITARY)
		{
			/* Assume ability will be used */
			military += o_ptr->value;
		}

		/* Check for ability to use cards from hand for military */
		if (o_ptr->code & P3_MILITARY_HAND)
		{
			/* Use ability to utmost */
			hand_military += o_ptr->value;

			/* Limit to handsize */
			if (hand_military > hand_size)
				hand_military = hand_size;
		}

		/* Check for ability to conquer peaceful world */
		if (o_ptr->code & P3_CONQUER_SETTLE)
		{
			/* Mark ability as available */
			conquer_peaceful = 1;

			/* Track best bonus */
			if (o_ptr->value > conquer_bonus)
			{
				/* Remember best bonus */
				conquer_bonus = o_ptr->value;
			}
		}
	}

	/* Apply bonus military accrued earlier in the phase */
	military += p_ptr->bonus_military;

	/* Apply bonus military from takeover power, if any */
	if (takeover) military += mil_bonus;

	/* Add owner's military strength to defense on takeovers */
	if (takeover) defense += strength_against(g, c_ptr->owner, world, 1);

	/* Check for military world and sufficient strength */
	if (conquer && military + hand_military >= defense) return 1;

	/* Takeovers must be military */
	if (takeover) return 0;

	/* Check for military and inability to pay */
	if (conquer && !pay_military) return 0;

	/* Paying for military world may grant discount */
	if (conquer && pay_military) cost -= pay_cost + pay_discount;

	/* Check for peaceful world and ability to conquer */
	if (!conquer && conquer_peaceful &&
	    military + hand_military + conquer_bonus >= defense)
	{
		/* Can be played */
		return 1;
	}

	/* Check for sufficient cards */
	if (hand_size >= cost)
	{
		/* Can afford */
		return 1;
	}

	/* Cannot afford */
	return 0;
}

/*
 * Called when player has chosen how to pay the world they are placing.
 *
 * We return 0 if the payment would not succeed.  We also return 0 in
 * cases where too many cards were discarded, to prevent stupid AI play.
 */
int settle_callback(game *g, int who, int which, int list[], int num,
                    int special[], int num_special)
{
	player *p_ptr;
	card *c_ptr;
	power *o_ptr, o_list[100];
	int conquer, pay_military = 0, military, cost, good;
	int hand_military = 0, conquer_peaceful = 0;
	int discard_zero = 0, takeover = 0;
	int i, j, n;
	char msg[1024];

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Get pointer to card being played */
	c_ptr = &g->deck[which];

	/* Check for takeover attempt */
	takeover = (c_ptr->owner != who);

	/* Get card cost */
	cost = c_ptr->d_ptr->cost;

	/* Get card's good type */
	good = c_ptr->d_ptr->good_type;

	/* Check for military world */
	conquer = c_ptr->d_ptr->flags & FLAG_MILITARY;

	/* Start military strength with bonuses from earlier in the phase */
	military = p_ptr->bonus_military;

	/* Loop over special cards used */
	for (i = 0; i < num_special; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[special[i]];

		/* Loop over card's powers */
		for (j = 0; j < c_ptr->d_ptr->num_power; j++)
		{
			/* Get power pointer */
			o_ptr = &c_ptr->d_ptr->powers[j];

			/* Skip non-settle phase power */
			if (o_ptr->phase != PHASE_SETTLE) continue;

			/* Check for pay for military ability */
			if (o_ptr->code & P3_PAY_MILITARY)
			{
				/* Don't use multiple pay abilities at once */
				if (pay_military) return 0;

				/* Check for non-alien */
				if (good != GOOD_ALIEN)
				{
					/* Mark ability */
					pay_military = 1;

					/* Reduce cost */
					cost -= o_ptr->value;

					/* Do not reduce cost below zero */
					if (cost < 0) cost = 0;

					/* Message */
					if (!g->simulation)
					{
						/* Format message */
						sprintf(msg,
						        "%s uses %s.\n",
						        p_ptr->name,
						        c_ptr->d_ptr->name);

						/* Send message */
						message_add(msg);
					}
				}
			}

			/* Check for discard to reduce cost */
			if (o_ptr->code & P3_DISCARD_ZERO)
			{
				/* Disown card */
				c_ptr->owner = -1;

				/* Discard card */
				c_ptr->where = WHERE_DISCARD;

				/* Mark use of card */
				discard_zero = 1;

				/* Message */
				if (!g->simulation)
				{
					/* Format message */
					sprintf(msg, "%s discards %s.\n",
					        p_ptr->name,
					        c_ptr->d_ptr->name);

					/* Send message */
					message_add(msg);
				}

				/* Check for non-alien */
				if (good != GOOD_ALIEN)
				{
					/* Reduce cost to zero */
					cost = 0;
				}
			}

			/* Check for discard for extra military */
			if (o_ptr->code & P3_DISCARD_MILITARY)
			{
				/* Disown card */
				c_ptr->owner = -1;

				/* Discard card */
				c_ptr->where = WHERE_DISCARD;

				/* Message */
				if (!g->simulation)
				{
					/* Format message */
					sprintf(msg, "%s discards %s.\n",
					        p_ptr->name,
					        c_ptr->d_ptr->name);

					/* Send message */
					message_add(msg);
				}

				/* Add extra military */
				military += o_ptr->value;

				/* Remember bonus for later */
				p_ptr->bonus_military += o_ptr->value;
			}

			/* Check for hand cards for military */
			if (o_ptr->code & P3_MILITARY_HAND)
			{
				/* Mark power as used */
				c_ptr->used[j] = 1;

				/* Assume cards are for military strength */
				hand_military += o_ptr->value;
			}

			/* Check for discard to conquer peaceful world */
			if (o_ptr->code & P3_CONQUER_SETTLE)
			{
				/* Disown card */
				c_ptr->owner = -1;

				/* Discard card */
				c_ptr->where = WHERE_DISCARD;

				/* Message */
				if (!g->simulation)
				{
					/* Format message */
					sprintf(msg, "%s discards %s.\n",
					        p_ptr->name,
					        c_ptr->d_ptr->name);

					/* Send message */
					message_add(msg);
				}

				/* Add extra military */
				military += o_ptr->value;

				/* Make world conquerable */
				conquer = 1;

				/* Mark ability as used */
				conquer_peaceful = 1;
			}
		}
	}

	/* Check for using military from hand */
	if (hand_military > 0)
	{
		/* Check for too many cards given */
		if (num > hand_military) return 0;

		/* Reduce hand military strength to cards given */
		hand_military = num;

		/* Remember bonus military for later */
		p_ptr->bonus_military += num;

		/* Military from hand is incompatible with pay for military */
		if (pay_military) return 0;
	}

	/* Cannot use "conquer peaceful" and "pay for military" together */
	if (conquer_peaceful && pay_military) return 0;

	/* Check for illegal use of "discard to reduce cost to zero" */
	if (discard_zero && conquer && !pay_military) return 0;

	/* Get all active settle powers */
	n = get_powers(g, who, PHASE_SETTLE, o_list);

	/* Get pointer to card being played */
	c_ptr = &g->deck[which];

	/* Loop over powers */
	for (i = 0; i < n; i++)
	{
		/* Get power pointer */
		o_ptr = &o_list[i];

		/* Check for reduce cost power */
		if (o_ptr->code & P3_REDUCE)
		{
			/* Check for specific good required */
			if (((o_ptr->code & P3_NOVELTY)&&good != GOOD_NOVELTY)||
			    ((o_ptr->code & P3_RARE) && good != GOOD_RARE) ||
			    ((o_ptr->code & P3_GENE) && good != GOOD_GENE) ||
			    ((o_ptr->code & P3_ALIEN) && good != GOOD_ALIEN))
			{
				/* Skip power */
				continue;
			}

			/* Reduce cost */
			cost -= o_ptr->value;

			/* Do not reduce cost below zero */
			if (cost < 0) cost = 0;
		}

		/* Check for extra military */
		if (o_ptr->code & P3_EXTRA_MILITARY)
		{
			/* Check for specific good required */
			if (((o_ptr->code & P3_NOVELTY)&&good != GOOD_NOVELTY)||
			    ((o_ptr->code & P3_RARE) && good != GOOD_RARE) ||
			    ((o_ptr->code & P3_GENE) && good != GOOD_GENE) ||
			    ((o_ptr->code & P3_ALIEN) && good != GOOD_ALIEN))
			{
				/* Skip power */
				continue;
			}

			/* Check for against rebels */
			if ((o_ptr->code & P3_AGAINST_REBEL) &&
			    !(c_ptr->d_ptr->flags & FLAG_REBEL))
			{
				/* Skip power */
				continue;
			}

			/* Check for per military world */
			if (o_ptr->code & P3_PER_MILITARY)
			{
				/* Add count of military worlds */
				military += count_active_flags(g, who,
				                               FLAG_MILITARY);

				/* Done */
				continue;
			}

			/* Add to military */
			military += o_ptr->value;
		}

		/* Discount when using pay for military */
		if (pay_military && (o_ptr->code & P3_PAY_DISCOUNT))
		{
			/* Apply extra discount */
			cost -= o_ptr->value;

			/* Do not reduce cost below zero */
			if (cost < 0) cost = 0;
		}
	}

	/* Check for insufficient military strength (except for takeovers) */
	if (!takeover && conquer && !pay_military &&
	    military + hand_military < c_ptr->d_ptr->cost)
	{
		/* Illegal payment */
		return 0;
	}

#if 0
	/* Check for hand military used and too much strength */
	if (hand_military > 0 && military + hand_military > c_ptr->d_ptr->cost)
	{
		/* Too much payment */
		return 0;
	}
#endif

	/* Check for insufficient payment */
	if ((!conquer || pay_military) && cost > num)
	{
		/* Insufficient payment */
		return 0;
	}

	/* Disallow overpayment */
	if ((!conquer || pay_military) && cost < num)
	{
		/* Too much payment */
		return 0;
	}

	/* Disallow normal paying for military */
	if (conquer && !pay_military && num > 0 && hand_military == 0)
	{
		/* Too much payment */
		return 0;
	}

	/* Message */
	if (!g->simulation)
	{
		/* Check for takeover attempt and payment for extra strength */
		if (takeover && hand_military > 0)
		{
			/* Format message */
			sprintf(msg, "%s pays %d for extra strength.\n",
			        p_ptr->name, num);
		}

		/* Otherwise no message for takeover attempt */
		else if (takeover)
		{
			/* No message */
			strcpy(msg, "");
		}

		/* Check for conquer with help from hand */
		else if (conquer && !pay_military && hand_military > 0)
		{
			/* Format message */
			sprintf(msg, "%s pays %d to conquer %s.\n", p_ptr->name,
			        num, c_ptr->d_ptr->name);
		}

		/* Check for normal conquer */
		else if (conquer && !pay_military)
		{
			/* Format message */
			sprintf(msg, "%s conquers %s.\n", p_ptr->name,
			        c_ptr->d_ptr->name);
		}

		/* Check for payment */
		else
		{
			/* Format message */
			sprintf(msg, "%s pays %d for %s.\n", p_ptr->name, num,
			        c_ptr->d_ptr->name);
		}

		/* Send message */
		if (strlen(msg)) message_add(msg);
	}

	/* Check for simulation */
	if (g->simulation)
	{
		/* Simulate payment */
		p_ptr->fake_discards += num;

		/* Success */
		return 1;
	}

	/* Loop over cards chosen as payment */
	for (i = 0; i < num; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[list[i]];

		/* Disown card */
		c_ptr->owner = -1;

		/* Move to discard */
		c_ptr->where = WHERE_DISCARD;
	}

	/* Payment is good */
	return 1;
}

/*
 * Ask a player to pay to settle a world.
 *
 * This may require using cards with a one-off discard ability to lower
 * cost or increase military, etc.
 */
static void pay_settle(game *g, int who, int world)
{
	player *p_ptr;
	card *c_ptr;
	power *o_ptr, o_list[100];
	int list[MAX_DECK], special[MAX_DECK];
	int conquer, good, military, cost, takeover, flags;
	int n = 0, num_special = 0;
	int i, j;
	char msg[1024];

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Get pointer to world we are playing */
	c_ptr = &g->deck[world];

	/* Set flag if this is an intended takeover */
	takeover = (c_ptr->owner != who);

	/* Set flag if world is conquerable */
	conquer = c_ptr->d_ptr->flags & FLAG_MILITARY;

	/* Get good type of world to be settled (if any) */
	good = c_ptr->d_ptr->good_type;

	/* Get cost or defense of world */
	cost = c_ptr->d_ptr->cost;

	/* Get flags for world to be settled */
	flags = c_ptr->d_ptr->flags;

	/* Assume player has 0 military strength */
	military = 0;

	/* Get settle phase powers */
	n = get_powers(g, who, PHASE_SETTLE, o_list);

	/* Loop over powers */
	for (i = 0; i < n; i++)
	{
		/* Get power pointer */
		o_ptr = &o_list[i];

		/* Check for extra military */
		if (o_ptr->code & P3_EXTRA_MILITARY)
		{
			/* Check for specific good required */
			if (((o_ptr->code & P3_NOVELTY)&&good != GOOD_NOVELTY)||
			    ((o_ptr->code & P3_RARE) && good != GOOD_RARE) ||
			    ((o_ptr->code & P3_GENE) && good != GOOD_GENE) ||
			    ((o_ptr->code & P3_ALIEN) && good != GOOD_ALIEN))
			{
				/* Skip power */
				continue;
			}

			/* Check for against rebels */
			if ((o_ptr->code & P3_AGAINST_REBEL) &&
			    !(c_ptr->d_ptr->flags & FLAG_REBEL))
			{
				/* Skip power */
				continue;
			}

			/* Check for per military world */
			if (o_ptr->code & P3_PER_MILITARY)
			{
				/* Add count of military worlds */
				military += count_active_flags(g, who,
				                               FLAG_MILITARY);

				/* Done */
				continue;
			}

			/* Add to military */
			military += o_ptr->value;
		}
	}

	/* Loop over player's active cards */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Skip unowned cards */
		if (c_ptr->owner != who) continue;

		/* Skip inactive cards */
		if (c_ptr->where != WHERE_ACTIVE) continue;

		/* Skip just-played card */
		if (c_ptr->temp) continue;

		/* Loop over card powers */
		for (j = 0; j < MAX_POWER; j++)
		{
			/* Get power pointer */
			o_ptr = &c_ptr->d_ptr->powers[j];

			/* Skip already-used powers */
			if (c_ptr->used[j]) continue;

			/* Skip non-settle power */
			if (o_ptr->phase != PHASE_SETTLE) continue;

			/* Check for discard to reduce cost */
			if (!takeover && (good != GOOD_ALIEN) &&
			    (o_ptr->code & P3_DISCARD_ZERO))
			{
				/* Add to special list */
				special[num_special++] = i;
			}

			/* Check for discard for extra military */
			if (o_ptr->code & P3_DISCARD_MILITARY)
			{
				/* Add to special list */
				special[num_special++] = i;
			}

			/* Check for pay for military ability */
			if (!takeover && conquer &&
			    (o_ptr->code & P3_PAY_MILITARY))
			{
				/* Check for against Rebels only */
				if (o_ptr->code & P3_AGAINST_REBEL)
				{
					/* Check for non-Rebel world */
					if (!(flags & FLAG_REBEL))
					{
						/* Skip power */
						continue;
					}
				}

				/* Add to special list */
				special[num_special++] = i;
			}

			/* Check for military from hand ability */
			if (o_ptr->code & P3_MILITARY_HAND)
			{
				/* Add to special list */
				special[num_special++] = i;
			}

			/* Check for conquer peaceful world */
			if (!conquer && (o_ptr->code & P3_CONQUER_SETTLE))
			{
				/* Add to special list */
				special[num_special++] = i;
			}
		}
	}

	/* Check for no need to pay */
	if (!takeover && conquer && military >= cost && !num_special)
	{
		/* Get pointer to world we are playing */
		c_ptr = &g->deck[p_ptr->placing];

		/* Message */
		if (!g->simulation)
		{
			/* Format message */
			sprintf(msg, "%s conquers %s.\n", p_ptr->name,
			        c_ptr->d_ptr->name);

			/* Send message */
			message_add(msg);
		}

		/* Done */
		return;
	}

	/* Clear list */
	n = 0;

	/* Loop over cards in hand */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Skip unowned cards */
		if (c_ptr->owner != who) continue;

		/* Skip cards not in hand */
		if (c_ptr->where != WHERE_HAND) continue;

		/* Add card to list */
		list[n++] = i;
	}

	/* Add fake cards to list */
	for (i = 0; i < p_ptr->fake_hand - p_ptr->fake_discards; i++)
	{
		/* Add a fake card to list */
		list[n++] = -1;
	}

	/* Have player decide how to pay */
	p_ptr->control->choose_pay(g, who, world, list, n, special,
	                           num_special);
}

/*
 * Called to verify that a takeover power can be used against the given
 * world.
 *
 * Players can call this to make sure they are using the correct power
 * for an opponent.
 */
int takeover_callback(game *g, int special, int world)
{
	card *c_ptr;
	power *o_ptr;
	int i, owner, rebel;

	/* Get world being targetted */
	c_ptr = &g->deck[world];

	/* Get owner of disputed card */
	owner = c_ptr->owner;

	/* Check for target world having rebel flag */
	rebel = c_ptr->d_ptr->flags & FLAG_REBEL;

	/* Get special card */
	c_ptr = &g->deck[special];

	/* Loop over powers */
	for (i = 0; i < c_ptr->d_ptr->num_power; i++)
	{
		/* Get power pointer */
		o_ptr = &c_ptr->d_ptr->powers[i];

		/* Skip non-Settle powers */
		if (o_ptr->phase != PHASE_SETTLE) continue;

		/* Check for takeover rebel power */
		if (o_ptr->code & P3_TAKEOVER_REBEL)
		{
			/* Mark power as used */
			c_ptr->used[i] = 1;

			/* May only takeover Rebel worlds */
			return rebel;
		}

		/* Check for takeover imperium power */
		if (o_ptr->code & P3_TAKEOVER_IMPERIUM)
		{
			/* Mark power as used */
			c_ptr->used[i] = 1;

			/* Check for owner having imperium cards */
			return count_active_flags(g, owner, FLAG_IMPERIUM);
		}

		/* Check for takeover military power */
		if (o_ptr->code & P3_TAKEOVER_MILITARY)
		{
			/* Discard card */
			c_ptr->owner = -1;
			c_ptr->where = WHERE_DISCARD;

			/* Check for owner having positive military */
			return total_military(g, owner) > 0;
		}
	}

	/* XXX */
	return 0;
}

/*
 * Check if a player can takeover opponent's cards, and if so, ask
 * player which card to declare an attempt on.
 */
int settle_check_takeover(game *g, int who)
{
	player *p_ptr;
	card *c_ptr;
	power *o_ptr;
	int i, j, n, list[MAX_DECK];
	int special[MAX_DECK], num_special = 0;
	int takeover_rebel = 0, takeover_imperium = 0, takeover_military = 0;
	int rebel_vuln, all_vuln, target, bonus;
	char msg[1024];

	/* Don't ask opponents in simulated game */
	if (g->simulation && g->sim_who != who) return 0;

	/* Don't ask if takeovers disabled */
	if (g->takeover_disabled) return 0;

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Loop over player's active cards */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Skip unowned cards */
		if (c_ptr->owner != who) continue;

		/* Skip inactive cards */
		if (c_ptr->where != WHERE_ACTIVE) continue;

		/* Skip just-played cards */
		if (c_ptr->temp) continue;

		/* Loop over card powers */
		for (j = 0; j < c_ptr->d_ptr->num_power; j++)
		{
			/* Get power pointer */
			o_ptr = &c_ptr->d_ptr->powers[j];

			/* Skip non-Settle powers */
			if (o_ptr->phase != PHASE_SETTLE) continue;

			/* Skip used powers */
			if (c_ptr->used[j]) continue;

			/* Check for "takeover rebel" power */
			if (o_ptr->code & P3_TAKEOVER_REBEL)
			{
				/* Mark power */
				takeover_rebel = 1;

				/* Add to special list */
				special[num_special++] = i;
			}

			/* Check for "takeover imperium" power */
			if (o_ptr->code & P3_TAKEOVER_IMPERIUM)
			{
				/* Mark power */
				takeover_imperium = 1;

				/* Add to special list */
				special[num_special++] = i;
			}

			/* Check for "takeover military" power */
			if (o_ptr->code & P3_TAKEOVER_MILITARY)
			{
				/* Mark power */
				takeover_military = 1;

				/* Add to special list */
				special[num_special++] = i;
			}
		}
	}

	/* Check for no takeover powers */
	if (!takeover_rebel && !takeover_imperium && !takeover_military)
	{
		/* Nothing to do */
		return 0;
	}

	/* Clear list of target planets */
	n = 0;

	/* Loop over opponents */
	for (i = 0; i < g->num_players; i++)
	{
		/* Skip active player */
		if (i == who) continue;

		/* Assume player is not vulnerable */
		rebel_vuln = all_vuln = 0;

		/* Assume no military bonus */
		bonus = 0;

		/* Check for Rebel military world */
		if (takeover_rebel &&
		    count_active_flags(g, i, FLAG_REBEL | FLAG_MILITARY))
		{
			/* Player's Rebel worlds are vulnerable */
			rebel_vuln = 1;

			/* No military bonus from takeover power */
			bonus = 0;
		}

		/* Check for Imperium card */
		if (takeover_imperium &&
		    count_active_flags(g, i, FLAG_IMPERIUM))
		{
			/* Player is vulnerable */
			all_vuln = 1;

			/* Get bonus to military */
			bonus = 2 * count_active_flags(g, who, FLAG_REBEL |
			                                       FLAG_MILITARY);
		}

		/* Check for military power */
		if (takeover_military &&
		    total_military(g, i) > 0)
		{
			/* Player is vulnerable */
			all_vuln = 1;

			/* No military bonus from takeover power */
			bonus = 0;
		}

		/* Skip players who are not vulnerable */
		if (!rebel_vuln && !all_vuln) continue;

		/* Loop over cards in deck */
		for (j = 0; j < g->deck_size; j++)
		{
			/* Get card pointer */
			c_ptr = &g->deck[j];

			/* Skip cards not owned by target player */
			if (c_ptr->owner != i) continue;

			/* Skip inactive cards */
			if (c_ptr->where != WHERE_ACTIVE) continue;

			/* Skip developments */
			if (c_ptr->d_ptr->type != TYPE_WORLD) continue;

			/* Skip non-military worlds */
			if (!(c_ptr->d_ptr->flags & FLAG_MILITARY)) continue;

			/* Skip non-Rebel worlds unless completely vulnerable */
			if (!all_vuln && !(c_ptr->d_ptr->flags & FLAG_REBEL))
				continue;

			/* Skip newly-placed worlds */
			if (c_ptr->temp) continue;

			/* Check for sufficient military strength */
			if (!settle_legal(g, who, j, bonus)) continue;

			/* Add target world to list */
			list[n++] = j;
		}
	}

	/* Check for no legal choices */
	if (!n) return 0;

	/* Ask player which world to attempt to takeover */
	target = p_ptr->control->choose_takeover(g, who, list, n, special,
	                                         num_special);

	/* Check for no choice made */
	if (target == -1) return 0;

	/* Remember takeover for later */
	g->takeover_target[g->num_takeover] = target;
	g->takeover_who[g->num_takeover] = who;
	g->takeover_power[g->num_takeover] = special[0];

	/* One more takeover to resolve */
	g->num_takeover++;

	/* Get takeover card being used */
	c_ptr = &g->deck[special[0]];

	/* Loop over powers */
	for (i = 0; i < c_ptr->d_ptr->num_power; i++)
	{
		/* Get power pointer */
		o_ptr = &c_ptr->d_ptr->powers[i];

		/* Check for takeover power */
		if (o_ptr->code & (P3_TAKEOVER_REBEL | P3_TAKEOVER_IMPERIUM |
		                   P3_TAKEOVER_MILITARY))
		{
			/* Mark power as used */
			c_ptr->used[i] = 1;
		}
	}

	/* Message */
	if (!g->simulation)
	{
		/* Format message */
		sprintf(msg, "%s uses %s to attempt takeover of %s.\n",
		        p_ptr->name, c_ptr->d_ptr->name,
		        g->deck[target].d_ptr->name);

		/* Send message */
		message_add(msg);
	}

	/* Attempt declared */
	return 1;
}

/*
 * Award player bonus cards for successfully placing a world.
 */
static void settle_bonus(game *g, int who)
{
	player *p_ptr;
	power o_list[100], *o_ptr;
	int i, n;

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Get settle phase powers */
	n = get_powers(g, who, PHASE_SETTLE, o_list);

	/* Check for chosen settle action */
	if (player_chose(g, who, g->cur_action) && !p_ptr->phase_bonus_used)
	{
		/* Draw card */
		draw_card(g, who);

		/* Mark bonus as used */
		p_ptr->phase_bonus_used = 1;
	}

	/* Loop over pre-existing powers */
	for (i = 0; i < n; i++)
	{
		/* Get power pointer */
		o_ptr = &o_list[i];

		/* Check for draw power */
		if (o_ptr->code & P3_DRAW_AFTER)
		{
			/* Draw cards */
			draw_cards(g, who, o_ptr->value);
		}
	}
}

/*
 * The second half of the Settle Phase -- paying for chosen worlds.
 */
void settle_action(game *g, int who, int world)
{
	player *p_ptr;
	card *c_ptr;
	power *o_ptr;
	int i, j, n, list[MAX_DECK];
	int place_again = 0;
	int takeover = 0;

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Check for not placing anything */
	if (world == -1)
	{
		/* Get most recent takeover target */
		world = g->takeover_target[g->num_takeover - 1];

		/* Set takeover flag */
		takeover = 1;
	}

	/* Have user pay for card (in some way) */
	pay_settle(g, who, world);

	/* Check for aborted game */
	if (g->game_over) return;

	/* Check for placed world */
	if (!takeover) settle_bonus(g, who);

	/* Loop over active cards */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Skip unowned cards */
		if (c_ptr->owner != who) continue;

		/* Skip inactive cards */
		if (c_ptr->where != WHERE_ACTIVE) continue;

		/* Loop over card powers */
		for (j = 0; j < c_ptr->d_ptr->num_power; j++)
		{
			/* Get power pointer */
			o_ptr = &c_ptr->d_ptr->powers[j];

			/* Skip non-Settle power */
			if (o_ptr->phase != PHASE_SETTLE) continue;

			/* Skip used powers */
			if (c_ptr->used[j]) continue;

			/* Check for place second world power */
			if (o_ptr->code & P3_PLACE_TWO)
			{
				/* Ask player to place another world */
				place_again = 1;

				/* Mark power as used */
				c_ptr->used[j] = 1;
			}
		}
	}

	/* Check for card played to place again */
	if (place_again)
	{
		/* Clear placing selection */
		p_ptr->placing = -1;

		/* Assume no cards to play */
		n = 0;

		/* Loop over cards in hand */
		for (i = 0; i < g->deck_size; i++)
		{
			/* Get card pointer */
			c_ptr = &g->deck[i];

			/* Skip unowned cards */
			if (c_ptr->owner != who) continue;

			/* Skip cards not in hand */
			if (c_ptr->where != WHERE_HAND) continue;

			/* Skip developments */
			if (c_ptr->d_ptr->type != TYPE_WORLD) continue;

			/* Skip cards that cannot be settled */
			if (!settle_legal(g, who, i, 0)) continue;

			/* Add card to list */
			list[n++] = i;
		}

		/* Check for no choices */
		if (!n) return;

		/* Ask player to choose */
		p_ptr->placing = p_ptr->control->choose_place(g, who, list, n,
		                                              PHASE_SETTLE);

		/* Check for aborted game */
		if (g->game_over) return;

		/* Check for no choice */
		if (p_ptr->placing == -1)
		{
			/* Ask for takeover declaration if possible */
			if (!settle_check_takeover(g, who)) return;
		}
		else
		{
			/* Place card */
			place_card(g, who, p_ptr->placing);
		}

		/* Act on settle */
		settle_action(g, who, p_ptr->placing);
	}
}

/*
 * Called when player has chosen a method of defense.
 *
 * We return:
 *   0 if the method is illegal
 *   1 if the method is legal but insufficient to stop the takeover
 *   2 if the method is legal and stops the takeover
 */
int defend_callback(game *g, int who, int deficit, int list[], int num,
                    int special[], int num_special)
{
	player *p_ptr;
	card *c_ptr;
	power *o_ptr;
	int military = 0, hand_military = 0;
	int i, j;
	char msg[1024];

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Loop over special cards used */
	for (i = 0; i < num_special; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[special[i]];

		/* Loop over card's powers */
		for (j = 0; j < c_ptr->d_ptr->num_power; j++)
		{
			/* Get power pointer */
			o_ptr = &c_ptr->d_ptr->powers[j];

			/* Skip non-Settle power */
			if (o_ptr->phase != PHASE_SETTLE) continue;

			/* Check for discard for extra military */
			if (o_ptr->code & P3_DISCARD_MILITARY)
			{
				/* Disown card */
				c_ptr->owner = -1;

				/* Discard card */
				c_ptr->where = WHERE_DISCARD;

				/* Message */
				if (!g->simulation)
				{
					/* Format message */
					sprintf(msg, "%s discards %s.\n",
					        p_ptr->name,
					        c_ptr->d_ptr->name);

					/* Send message */
					message_add(msg);
				}

				/* Add extra military */
				military += o_ptr->value;

				/* Remember bonus for later */
				p_ptr->bonus_military += o_ptr->value;
			}

			/* Check for hand cards for military */
			if (o_ptr->code & P3_MILITARY_HAND)
			{
				/* Mark power as used */
				c_ptr->used[j] = 1;

				/* Assume cards are for military strength */
				hand_military += o_ptr->value;
			}
		}
	}

	/* Check for too many cards passed */
	if (num > hand_military) return 0;

	/* Use cards passed as military strength */
	p_ptr->bonus_military += num;
	military += num;

	/* Message */
	if (!g->simulation)
	{
		/* Format message */
		sprintf(msg, "%s pays %d for extra strength.\n",
			p_ptr->name, num);

		/* Send message */
		message_add(msg);
	}

	/* Check for simulation */
	if (g->simulation)
	{
		/* Simulate payment */
		p_ptr->fake_discards += num;
	}
	else
	{
		/* Discard cards given */
		for (i = 0; i < num; i++)
		{
			/* Get card pointer */
			c_ptr = &g->deck[list[i]];

			/* Discard card */
			c_ptr->owner = -1;
			c_ptr->where = WHERE_DISCARD;
		}
	}

	/* Check for sufficient strength */
	if (military > deficit) return 2;

	/* Legal but insufficient */
	return 1;
}

/*
 * Ask current owner for extra defense to spend in defense of a world.
 */
static void defend_takeover(game *g, int who, int world, int attacker,
                            int deficit)
{
	player *p_ptr;
	card *c_ptr;
	power *o_ptr;
	int list[MAX_DECK], special[MAX_DECK];
	int n = 0, num_special = 0;
	int max = 0, hand_military = 0, hand_size;
	int i, j;

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Loop over player's active cards */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Skip unowned cards */
		if (c_ptr->owner != who) continue;

		/* Skip inactive cards */
		if (c_ptr->where != WHERE_ACTIVE) continue;

		/* Skip just-played card */
		if (c_ptr->temp) continue;

		/* Loop over card powers */
		for (j = 0; j < MAX_POWER; j++)
		{
			/* Get power pointer */
			o_ptr = &c_ptr->d_ptr->powers[j];

			/* Skip already-used powers */
			if (c_ptr->used[j]) continue;

			/* Skip non-settle power */
			if (o_ptr->phase != PHASE_SETTLE) continue;

			/* Check for discard for extra military */
			if (o_ptr->code & P3_DISCARD_MILITARY)
			{
				/* Add to special list */
				special[num_special++] = i;

				/* Add value to max we can spend */
				max += o_ptr->value;
			}

			/* Check for military from hand ability */
			if (o_ptr->code & P3_MILITARY_HAND)
			{
				/* Add to special list */
				special[num_special++] = i;

				/* Track amount we can spend */
				hand_military += max;
			}
		}
	}

	/* Compute effective hand size */
	hand_size = count_player_area(g, who, WHERE_HAND) + p_ptr->fake_hand -
	            p_ptr->fake_discards - p_ptr->fake_played_dev -
	            p_ptr->fake_played_world;

	/* Reduce amount of military from hand we can use to hand size */
	if (hand_military > hand_size) hand_military = hand_size;

	/* Add maximum hand military */
	max += hand_military;

	/* Check for no way to successfully defend */
	if (max <= deficit) return;

	/* Clear list */
	n = 0;

	/* Loop over cards in hand */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Skip unowned cards */
		if (c_ptr->owner != who) continue;

		/* Skip cards not in hand */
		if (c_ptr->where != WHERE_HAND) continue;

		/* Add card to list */
		list[n++] = i;
	}

	/* Add fake cards to list */
	for (i = 0; i < p_ptr->fake_hand - p_ptr->fake_discards; i++)
	{
		/* Add a fake card to list */
		list[n++] = -1;
	}

	/* Check for no "military from hand" abilities */
	if (!hand_military) n = 0;

	/* Have player decide how to defend */
	p_ptr->control->choose_defend(g, who, world, attacker, deficit, list, n,
	                              special, num_special);
}

/*
 * Resolve a takeover.
 */
static int resolve_takeover(game *g, int who, int world, int special)
{
	player *p_ptr;
	card *c_ptr;
	power *o_ptr;
	int defense;
	char msg[1024];
	int bonus = 0, attack;
	int i;

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Get card pointer to special power used for takeover */
	c_ptr = &g->deck[special];

	/* Loop over powers */
	for (i = 0; i < c_ptr->d_ptr->num_power; i++)
	{
		/* Get power pointer */
		o_ptr = &c_ptr->d_ptr->powers[i];

		/* Skip non-Settle powers */
		if (o_ptr->phase != PHASE_SETTLE) continue;

		/* Check for "takeover imperium" power */
		if (o_ptr->code & P3_TAKEOVER_IMPERIUM)
		{
			/* Add bonus military to attacker */
			bonus += o_ptr->value *
			         count_active_flags(g, who, FLAG_REBEL |
			                                    FLAG_MILITARY);
		}

		/* Check for discard to use power */
		if (o_ptr->code & P3_TAKEOVER_MILITARY)
		{
			/* Discard card */
			c_ptr->owner = -1;
			c_ptr->where = WHERE_DISCARD;
		}
	}

	/* Get card pointer */
	c_ptr = &g->deck[world];

	/* Compute current owner's defense */
	defense = strength_against(g, c_ptr->owner, world, 1);

	/* Add world's defense */
	defense += c_ptr->d_ptr->cost;

	/* Compute total attack strength */
	attack = bonus + strength_against(g, who, world, 0);

	/* Check for successful takeover */
	if (attack >= defense)
	{
		/* Ask defender for any extra defense to spend */
		defend_takeover(g, c_ptr->owner, world, who, attack - defense);
	}

	/* Recompute defense */
	defense = strength_against(g, c_ptr->owner, world, 1);

	/* Add world's defense */
	defense += c_ptr->d_ptr->cost;

	/* Check for insufficient attack strength */
	if (attack < defense)
	{
		/* Message */
		if (!g->simulation)
		{
			/* Format message */
			sprintf(msg, "%s fails to takeover %s.\n", p_ptr->name,
			        c_ptr->d_ptr->name);

			/* Send message */
			message_add(msg);
		}

		/* Failure */
		return 0;
	}

	/* Transfer ownership */
	c_ptr->owner = who;

	/* Set now card order */
	c_ptr->order = p_ptr->table_order++;

	/* Message */
	if (!g->simulation)
	{
		/* Format message */
		sprintf(msg, "%s takes over %s.\n", p_ptr->name,
			c_ptr->d_ptr->name);

		/* Send message */
		message_add(msg);
	}

	/* Check for good on world */
	if (c_ptr->covered != -1)
	{
		/* Get good card pointer */
		c_ptr = &g->deck[c_ptr->covered];

		/* Transfer good as well */
		c_ptr->owner = who;
	}

	/* Award settle bonus */
	settle_bonus(g, who);

	/* Successful takeover */
	return 1;
}

/*
 * Resolve all pending takeovers.
 */
void resolve_takeovers(game *g)
{
	int i, j;

	/* Loop over takeovers */
	for (i = 0; i < g->num_takeover; i++)
	{
		/* Skip cleared takeovers */
		if (g->takeover_target[i] == -1) continue;

		/* Resolve takeover */
		if (!resolve_takeover(g, g->takeover_who[i],
		                         g->takeover_target[i],
		                         g->takeover_power[i]))
		{
			/* Clear future declarations if one fails */
			for (j = i + 1; j < g->num_takeover; j++)
			{
				/* Check for same player */
				if (g->takeover_who[i] == g->takeover_who[j])
				{
					/* Clear target */
					g->takeover_target[j] = -1;
				}
			}
		}
	}

	/* Clear takeovers */
	g->num_takeover = 0;
}

/*
 * Handle the Settle Phase.
 */
void phase_settle(game *g)
{
	player *p_ptr;
	card *c_ptr;
	int list[MAX_DECK];
	int i, j, n;

	/* Check for simulated game */
	if (g->simulation)
	{
		/* Loop over players */
		for (i = 0; i < g->num_players; i++)
		{
			/* Get player pointer */
			p_ptr = &g->p[i];

			/* Skip simulating player */
			if (g->sim_who == i) continue;

			/* Check for at least one card in hand */
			if (count_player_area(g, i, WHERE_HAND) +
			    p_ptr->fake_hand - p_ptr->fake_discards -
			    p_ptr->fake_played_dev -
			    p_ptr->fake_played_world > 0)
			{
				/* Assume a card is played */
				p_ptr->fake_played_world++;
			}
		}
	}

	/* Loop over players */
	for (i = 0; i < g->num_players; i++)
	{
		/* Get player pointer */
		p_ptr = &g->p[i];

		/* Set current player */
		g->turn = i;

		/* Clear placing selection */
		p_ptr->placing = -1;

		/* Do not ask simulated opponents */
		if (g->simulation && g->sim_who != i) continue;

		/* Assume no cards to play */
		n = 0;

		/* Loop over cards in hand */
		for (j = 0; j < g->deck_size; j++)
		{
			/* Get card pointer */
			c_ptr = &g->deck[j];

			/* Skip unowned cards */
			if (c_ptr->owner != i) continue;

			/* Skip cards not in hand */
			if (c_ptr->where != WHERE_HAND) continue;

			/* Skip developments */
			if (c_ptr->d_ptr->type != TYPE_WORLD) continue;

			/* Skip cards that cannot be settled */
			if (!settle_legal(g, i, j, 0)) continue;

			/* Add card to list */
			list[n++] = j;
		}

		/* Check for no choices */
		if (!n) continue;

		/* Ask player to choose */
		p_ptr->placing = p_ptr->control->choose_place(g, i, list, n,
		                                              PHASE_SETTLE);

		/* Check for aborted game */
		if (g->game_over) return;
	}

	/* Loop over players */
	for (i = 0; i < g->num_players; i++)
	{
		/* Get player pointer */
		p_ptr = &g->p[i];

		/* Skip players who are not placing anything */
		if (p_ptr->placing == -1) continue;

		/* Place card */
		place_card(g, i, p_ptr->placing);
	}

	/* Loop over players */
	for (i = 0; i < g->num_players; i++)
	{
		/* Get player pointer */
		p_ptr = &g->p[i];

		/* Set current player */
		g->turn = i;

		/* Check for no placement choice */
		if (p_ptr->placing == -1)
		{
			/* Ask player for takeover choice instead */
			if (!settle_check_takeover(g, i)) continue;
		}

		/* Handle choice */
		settle_action(g, i, p_ptr->placing);

		/* Clear placing choice */
		p_ptr->placing = -1;
	}

	/* Resolve takeovers */
	resolve_takeovers(g);

	/* Clear any temp flags on cards */
	clear_temp(g);

	/* Check intermediate goals */
	check_goals(g);
}

/*
 * Pass a payment callback either to the develop or settle callback.
 */
int payment_callback(game *g, int who, int which, int list[], int num,
                     int special[], int num_special)
{
	player *p_ptr;
	card *c_ptr;

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Get pointer of card being played */
	c_ptr = &g->deck[which];

	/* Check for development */
	if (c_ptr->d_ptr->type == TYPE_DEVELOPMENT)
	{
		/* Use development callback */
		return devel_callback(g, who, which, list, num, special,
		                      num_special);
	}
	else
	{
		/* Use settle callback */
		return settle_callback(g, who, which, list, num, special,
		                       num_special);
	}
}

/*
 * Called when player has chosen which good to trade.
 */
void trade_chosen(game *g, int who, int which, int no_bonus)
{
	player *p_ptr;
	card *c_ptr, *good;
	power *o_ptr, o_list[100];
	int i, n, type, value;
	char msg[1024];

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Get card pointer */
	c_ptr = &g->deck[which];

	/* Get good card pointer */
	good = &g->deck[c_ptr->covered];

	/* Clear good card flag */
	good->good = 0;

	/* Disown good card */
	good->owner = -1;

	/* Move good card to discard */
	good->where = WHERE_DISCARD;

	/* Uncover production card */
	c_ptr->covered = -1;

	/* Get good type */
	type = c_ptr->d_ptr->good_type;

	/* Value is equal to type */
	value = type;

	/* Get consume phase powers (including trade powers) */
	n = get_powers(g, who, PHASE_CONSUME, o_list);

	/* Loop over powers */
	for (i = 0; i < n; i++)
	{
		/* Get power pointer */
		o_ptr = &o_list[i];

		/* Check for any type bonus */
		if (!no_bonus && (o_ptr->code & P4_TRADE_ANY))
		{
			/* Add bonus */
			value += o_ptr->value;
		}

		/* Check for matching specific type bonus */
		if (!no_bonus &&
		    ((type == GOOD_NOVELTY &&(o_ptr->code & P4_TRADE_NOVELTY))||
		    (type == GOOD_RARE && (o_ptr->code & P4_TRADE_RARE)) ||
		    (type == GOOD_GENE && (o_ptr->code & P4_TRADE_GENE)) ||
		    (type == GOOD_ALIEN && (o_ptr->code & P4_TRADE_ALIEN))))
		{
			/* Add bonus */
			value += o_ptr->value;
		}

		/* Check for Chromosome bonus and gene good */
		if (!no_bonus && (type == GOOD_GENE) &&
		    (o_ptr->code & P4_TRADE_BONUS_CHROMO))
		{
			/* Increase value */
			value += count_active_flags(g, who, FLAG_CHROMO);
		}
	}
	
	/* Get card pointer */
	c_ptr = &g->deck[which];

	/* Loop over powers on card holding good */
	for (i = 0; i < c_ptr->d_ptr->num_power; i++)
	{
		/* Get power pointer */
		o_ptr = &c_ptr->d_ptr->powers[i];

		/* Skip non-consume power */
		if (o_ptr->phase != PHASE_CONSUME) continue;

		/* Check for "trade this" power */
		if (!no_bonus && (o_ptr->code & P4_TRADE_THIS))
		{
			/* Add bonus */
			value += o_ptr->value;
		}
	}

	/* Message */
	if (!g->simulation)
	{
		/* Format message */
		sprintf(msg, "%s trades good from %s for %d.\n", p_ptr->name,
		        c_ptr->d_ptr->name, value);

		/* Send message */
		message_add(msg);
	}

	/* Draw cards */
	draw_cards(g, who, value);

	/* Count reward */
	p_ptr->phase_cards += value;
}

/*
 * Handle a Trade action.
 *
 * This can occur when choosing the Consume-Trade role, or via some special
 * Consume phase powers.
 */
void trade_action(game *g, int who, int no_bonus)
{
	player *p_ptr;
	card *c_ptr;
	int list[MAX_DECK], n = 0;
	int i;

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Loop over cards */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Skip unowned cards */
		if (c_ptr->owner != who) continue;

		/* Skip inactive cards */
		if (c_ptr->where != WHERE_ACTIVE) continue;

		/* Skip cards without a good */
		if (c_ptr->covered == -1) continue;

		/* Add card to list */
		list[n++] = i;
	}

	/* Check for opponent's no goods in simulated game */
	if (!n && g->simulation && g->sim_who != who)
	{
		/* Check for newly settled world */
		if (p_ptr->fake_played_world)
		{
			/* XXX Assume world is Windfall worth 4 */
			draw_cards(g, who, 4);
		}
	}

	/* Check for no goods to trade */
	if (!n) return;

	/* Ask player to choose good to trade */
	p_ptr->control->choose_trade(g, who, list, n, no_bonus);
}

/*
 * Called when a player has chosen goods to consume.
 */
int good_chosen(game *g, int who, power *o_ptr, int g_list[], int num)
{
	player *p_ptr;
	card *c_ptr, *good;
	int i, types[6], num_types, times, vp;
	char msg[1024];
	
	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Check for needing two goods */
	if (o_ptr->code & P4_CONSUME_TWO)
	{
		/* Check for not two */
		if (num != 2) return 0;
	}

	/* Check for needing three */
	else if (o_ptr->code & P4_CONSUME_3_DIFF)
	{
		/* Check for not three */
		if (num != 3) return 0;
	}

	/* Check for needing all goods */
	else if (o_ptr->code & P4_CONSUME_ALL)
	{
		/* XXX Check for all goods */
	}

	/* Check for needing 1-4 goods */
	else if (o_ptr->code & P4_CONSUME_N_DIFF)
	{
	}

	/* Otherwise check for too many */
	else if (num > o_ptr->times) return 0;

	/* Check for three different types needed */
	if (o_ptr->code & P4_CONSUME_3_DIFF)
	{
		/* Clear type counts */
		for (i = 0; i < 6; i++) types[i] = 0;

		/* Assume zero types */
		num_types = 0;

		/* Loop over goods */
		for (i = 0; i < num; i++)
		{
			/* Get card pointer */
			c_ptr = &g->deck[g_list[i]];

			/* Count good type */
			types[c_ptr->d_ptr->good_type]++;
		}

		/* Count good types */
		for (i = 0; i < 6; i++)
		{
			/* Check for type given */
			if (types[i]) num_types++;
		}

		/* Check for not three */
		if (num_types != 3) return 0;
	}

	/* Check for different types needed */
	if (o_ptr->code & P4_CONSUME_N_DIFF)
	{
		/* Clear type counts */
		for (i = 0; i < 6; i++) types[i] = 0;

		/* Assume zero types */
		num_types = 0;

		/* Loop over goods */
		for (i = 0; i < num; i++)
		{
			/* Get card pointer */
			c_ptr = &g->deck[g_list[i]];

			/* Count good type */
			types[c_ptr->d_ptr->good_type]++;
		}

		/* Count good types */
		for (i = 0; i < 6; i++)
		{
			/* Check for type given */
			if (types[i]) num_types++;
		}

		/* Check for duplicate types */
		if (num_types < num) return 0;
	}

	/* Consume goods */
	for (i = 0; i < num; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[g_list[i]];

		if (c_ptr->covered == -1)
		{
			printf("Passed card without good!\n");
			exit(1);
		}

		/* Get good card pointer */
		good = &g->deck[c_ptr->covered];

		/* Clear good card flag */
		good->good = 0;

		/* Disown good card */
		good->owner = -1;

		/* Move good card to discard */
		good->where = WHERE_DISCARD;

		/* Uncover production card */
		c_ptr->covered = -1;

		/* Message */
		if (!g->simulation)
		{
			/* Format message */
			sprintf(msg, "%s consumes good from %s.\n", p_ptr->name,
			        c_ptr->d_ptr->name);

			/* Send message */
			message_add(msg);
		}
	}

	/* Compute number of times award is given */
	times = o_ptr->times;

	/* Check for fewer goods given than max */
	if (num < times) times = num;

	/* When consuming all, give award of one less than goods given */
	if (o_ptr->code & P4_CONSUME_ALL) times = num - 1;

	/* When consuming different types, give award once per good */
	if (o_ptr->code & P4_CONSUME_N_DIFF) times = num;

	/* Give award */
	for (i = 0; i < times; i++)
	{
		/* Check for VP award */
		if (o_ptr->code & P4_GET_VP)
		{
			/* Base VP */
			vp = o_ptr->value;

			/* Check for double VP action */
			if (player_chose(g, who, ACT_CONSUME_X2)) vp *= 2;

			/* Award VPs */
			p_ptr->vp += vp;

			/* Remove from pool */
			g->vp_pool -= vp;

			/* Count reward */
			p_ptr->phase_vp += vp;
		}

		/* Check for card award */
		if (o_ptr->code & P4_GET_CARD)
		{
			/* Award cards */
			draw_cards(g, who, o_ptr->value);

			/* Count reward */
			p_ptr->phase_cards += o_ptr->value;
		}

		/* XXX Check for multiple card award */
		if (o_ptr->code & P4_GET_2_CARD)
		{
			/* Award cards */
			draw_cards(g, who, o_ptr->value * 2);

			/* Count reward */
			p_ptr->phase_cards += o_ptr->value * 2;
		}
	}

	/* Success */
	return 1;
}

/*
 * Ask player to choose a number and check for a match.
 */
static void draw_lucky(game *g, int who)
{
	player *p_ptr;
	card *c_ptr;
	char msg[1024];
	int cost;

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Ask player to choose lucky number */
	cost = p_ptr->control->choose_lucky(g, who);

	/* Check for aborted game */
	if (g->game_over) return;

	/* Draw top card */
	c_ptr = random_draw(g);

	/* Message */
	if (!g->simulation)
	{
		/* Format message */
		sprintf(msg, "%s guesses %d.\n", p_ptr->name, cost);

		/* Add message */
		message_add(msg);

		/* Format message */
		sprintf(msg, "%s draws %s (cost %d).\n", p_ptr->name,
		                                         c_ptr->d_ptr->name,
		                                         c_ptr->d_ptr->cost);

		/* Add message */
		message_add(msg);
	}

	/* Check for correct guess */
	if (cost == c_ptr->d_ptr->cost)
	{
		/* Move card to player */
		c_ptr->owner = who;

		/* Move card to hand */
		c_ptr->where = WHERE_HAND;

		/* Make card known to everyone */
		c_ptr->known = ~0;
	}
	else
	{
		/* Move card to discard */
		c_ptr->where = WHERE_DISCARD;

		/* Make card known to everyone */
		c_ptr->known = ~0;
	}
}

/*
 * Ask player to choose a card to ante, and draw cards and possibly reward
 * one to the player.
 */
static void ante_card(game *g, int who)
{
	player *p_ptr;
	card *c_ptr, *drawn[MAX_DECK];
	char msg[1024];
	int list[MAX_DECK], n = 0;
	int i, chosen;
	int cost, success = 0;

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Loop over player's cards in hand */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Skip unowned cards */
		if (c_ptr->owner != who) continue;

		/* Skip cards not in hand */
		if (c_ptr->where != WHERE_HAND) continue;

		/* Skip cards that are too cheap */
		if (c_ptr->d_ptr->cost < 1) continue;

		/* Skip cards that are too expensive */
		if (c_ptr->d_ptr->cost > 6) continue;

		/* Add card to list */
		list[n++] = i;
	}

	/* Check for no cards available to ante */
	if (!n) return;

	/* Ask player to choose ante */
	chosen = p_ptr->control->choose_ante(g, who, list, n);

	/* Check for aborted game */
	if (g->game_over) return;

	/* Check for no card chosen */
	if (chosen < 0) return;

	/* Get card pointer */
	c_ptr = &g->deck[chosen];

	/* Location is known to all */
	c_ptr->known = ~0;

	/* Get card cost */
	cost = c_ptr->d_ptr->cost;

	/* Message */
	if (!g->simulation)
	{
		/* Format message */
		sprintf(msg, "%s antes %s.\n", p_ptr->name, c_ptr->d_ptr->name);

		/* Add message */
		message_add(msg);
	}

	/* Draw cards */
	for (i = 0; i < cost; i++)
	{
		/* Take a card */
		drawn[i] = random_draw(g);

		/* Check for more expensive than ante */
		if (drawn[i]->d_ptr->cost > cost) success = 1;

		/* Location is known to all */
		drawn[i]->known = ~0;

		/* Message */
		if (!g->simulation)
		{
			/* Format message */
			sprintf(msg, "%s draws %s.\n", p_ptr->name,
			        drawn[i]->d_ptr->name);

			/* Add message */
			message_add(msg);
		}
	}

	/* Check for failure */
	if (!success)
	{
		/* Move ante to discard */
		c_ptr->where = WHERE_DISCARD;

		/* Disown ante */
		c_ptr->owner = -1;

		/* Loop over drawn cards */
		for (i = 0; i < cost; i++)
		{
			/* Discard drawn card */
			drawn[i]->where = WHERE_DISCARD;
		}

		/* Done */
		return;
	}

	/* Clear list */
	n = 0;

	/* Loop over cards drawn */
	for (i = 0; i < cost; i++)
	{
		/* Add drawn card to list */
		list[n++] = drawn[i] - g->deck;
	}

	/* Ask player which card to keep */
	chosen = p_ptr->control->choose_keep(g, who, list, n);

	/* Check for aborted game */
	if (g->game_over) return;

	/* Message */
	if (!g->simulation)
	{
		/* Format message */
		sprintf(msg, "%s keeps %s.\n", p_ptr->name,
			g->deck[chosen].d_ptr->name);

		/* Add message */
		message_add(msg);
	}

	/* Loop over cards drawn */
	for (i = 0; i < cost; i++)
	{
		/* Check for chosen card */
		if (drawn[i] - g->deck == chosen)
		{
			/* Give card to player */
			drawn[i]->owner = who;

			/* Move card to hand */
			drawn[i]->where = WHERE_HAND;
		}
		else
		{
			/* Discard card */
			drawn[i]->where = WHERE_DISCARD;
		}
	}
}

/*
 * Called when player has chosen cards in hand to consume.
 */
int consume_hand_chosen(game *g, int who, power *o_ptr, int list[], int n)
{
	player *p_ptr;
	card *c_ptr;
	int i;
	char msg[1024];

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Check for too many discards */
	if (n > o_ptr->times) return 0;

	/* Message */
	if (!g->simulation)
	{
		/* Format message */
		sprintf(msg, "%s consumes %d cards from hand.\n", p_ptr->name,
		        n);

		/* Send message */
		message_add(msg);
	}

	/* Loop over choices */
	for (i = 0; i < n; i++)
	{
		/* Give VP rewards */
		if (o_ptr->code & P4_GET_VP)
		{
			/* Add points */
			p_ptr->vp += o_ptr->value;

			/* Remove from pool */
			g->vp_pool -= o_ptr->value;

			/* Count reward */
			p_ptr->phase_vp += o_ptr->value;
		}
		
		/* Give card rewards */
		if (o_ptr->code & P4_GET_CARD)
		{
			/* Draw cards */
			draw_cards(g, who, o_ptr->value);

			/* Count reward */
			p_ptr->phase_cards += o_ptr->value;
		}

		/* Get card pointer */
		c_ptr = &g->deck[list[i]];

		/* Disown card */
		c_ptr->owner = -1;

		/* Move card to discard */
		c_ptr->where = WHERE_DISCARD;
	}

	/* Success */
	return 1;
}

/*
 * Ask player to discard cards from hand for VPs or cards.
 */
static void consume_discard(game *g, int who, power *o_ptr)
{
	player *p_ptr;
	card *c_ptr;
	int list[MAX_DECK], n = 0;
	int i;

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

		/* Add card to list */
		list[n++] = i;
	}

	/* Ask player to choose discards */
	p_ptr->control->choose_consume_hand(g, who, o_ptr, list, n);
}

/*
 * Called when a player has chosen a consume power.
 */
void consume_chosen(game *g, int who, power *o_ptr)
{
	player *p_ptr;
	card *c_ptr;
	int i, j, found = 0, min, max, vp;
	int good, g_list[MAX_DECK], num_goods = 0;

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Loop over cards */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Skip unowned cards */
		if (c_ptr->owner != who) continue;

		/* Skip inactive cards */
		if (c_ptr->where != WHERE_ACTIVE) continue;

		/* Loop over card powers */
		for (j = 0; j < c_ptr->d_ptr->num_power; j++)
		{
			/* Skip powers already marked as used */
			if (c_ptr->used[j]) continue;

			/* Check for matching power */
			if (!memcmp(&c_ptr->d_ptr->powers[j], o_ptr,
			            sizeof(power)))
			{
				/* Mark power as used */
				c_ptr->used[j] = 1;

				/* Power is found */
				found = 1;
				break;
			}
		}

		/* Stop looking if power found */
		if (found) break;
	}

	/* Check for trade action power */
	if (o_ptr->code & P4_TRADE_ACTION)
	{
		/* Perform trade action */
		trade_action(g, who, o_ptr->code & P4_TRADE_NO_BONUS);

		/* Done */
		return;
	}

	/* Check for draw a card */
	if (o_ptr->code & P4_DRAW)
	{
		/* Draw cards */
		draw_cards(g, who, o_ptr->value);

		/* Count reward */
		p_ptr->phase_cards += o_ptr->value;

		/* Done */
		return;
	}

	/* Check for "draw if lucky" */
	if (o_ptr->code & P4_DRAW_LUCKY)
	{
		/* Perform lucky draw */
		draw_lucky(g, who);

		/* Done */
		return;
	}

	/* Check for "ante card for card" */
	if (o_ptr->code & P4_ANTE_CARD)
	{
		/* Ask player to ante */
		ante_card(g, who);

		/* Done */
		return;
	}

	/* Check for "VP" */
	if (o_ptr->code & P4_VP)
	{
		/* Base VP */
		vp = o_ptr->value;

		/* Check for double VP action */
		if (player_chose(g, who, ACT_CONSUME_X2)) vp *= 2;

		/* Award VPs */
		p_ptr->vp += vp;

		/* Remove from pool */
		g->vp_pool -= vp;

		/* Count reward */
		p_ptr->phase_vp += vp;

		/* Done */
		return;
	}

	/* Check for discard from hand */
	if (o_ptr->code & P4_DISCARD_HAND)
	{
		/* Choose discards for points/cards */
		consume_discard(g, who, o_ptr);

		/* Done */
		return;
	}

	/* Loop over cards */
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

		/* Get good type */
		good = c_ptr->d_ptr->good_type;

		/* Check for specific good type needed */
		if (((o_ptr->code & P4_CONSUME_NOVELTY)&&good != GOOD_NOVELTY)||
		    ((o_ptr->code & P4_CONSUME_RARE) && good != GOOD_RARE) ||
		    ((o_ptr->code & P4_CONSUME_GENE) && good != GOOD_GENE) ||
		    ((o_ptr->code & P4_CONSUME_ALIEN) && good != GOOD_ALIEN))
		{
			/* Skip good */
			continue;
		}

		/* Check for specific good needed */
		if (o_ptr->code & P4_CONSUME_THIS)
		{
			/* Loop over card's powers */
			for (j = 0; j < c_ptr->d_ptr->num_power; j++)
			{
				/* Check for matching power */
				if (!memcmp(&c_ptr->d_ptr->powers[j], o_ptr,
					    sizeof(power)))
				{
					/* Add good (world) to list */
					g_list[num_goods++] =i;
				}
			}

			/* Next */
			continue;
		}

		/* Add good (world) to list */
		g_list[num_goods++] = i;
	}

	/* Compute number of goods needed */
	if (o_ptr->code & P4_CONSUME_TWO)
	{
		/* Exactly two goods needed */
		min = max = 2;
	}
	else if (o_ptr->code & P4_CONSUME_3_DIFF)
	{
		/* Exactly three goods needed */
		min = max = 3;
	}
	else if (o_ptr->code & P4_CONSUME_N_DIFF)
	{
		/* Up to four goods needed */
		min = 0;
		max = 4;
	}
	else if (o_ptr->code & P4_CONSUME_ALL)
	{
		/* All goods needed */
		min = max = num_goods;
	}
	else
	{
		/* Use power a number of times */
		min = max = o_ptr->times;
	}

	/* Check for fewer goods available */
	if (min > num_goods)
	{
		/* Use only what is available */
		min = num_goods;
	}

	/* Check for fewer goods available */
	if (max > num_goods)
	{
		/* Use only what is available */
		max = num_goods;
	}

	/* Ask player which good(s) to consume */
	p_ptr->control->choose_good(g, who, o_ptr, g_list, num_goods, min, max);
}

/*
 * Ask the player to use a consume power.
 *
 * We return 0 if there are no powers to be used, and 1 otherwise.
 */
int consume_action(game *g, int who)
{
	player *p_ptr;
	card *c_ptr;
	power *o_ptr, o_list[100];
	int goods = 0, types[6], num_types = 0;
	int i, j, need, n = 0;

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Clear good type counts */
	for (i = 0; i < 6; i++) types[i] = 0;

	/* Look for available goods */
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

		/* Count good */
		goods++;

		/* Count good type */
		types[c_ptr->d_ptr->good_type]++;
	}

	/* Count number of types */
	for (i = 0; i < 6; i++) if (types[i]) num_types++;

	/* Loop over active cards */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Skip unowned cards */
		if (c_ptr->owner != who) continue;

		/* Skip inactive cards */
		if (c_ptr->where != WHERE_ACTIVE) continue;

		/* Loop over card powers */
		for (j = 0; j < c_ptr->d_ptr->num_power; j++)
		{
			/* Get power pointer */
			o_ptr = &c_ptr->d_ptr->powers[j];

			/* Skip non-consume powers */
			if (o_ptr->phase != PHASE_CONSUME) continue;

			/* Skip used powers */
			if (c_ptr->used[j]) continue;

			/* Assume only one good needed */
			need = 1;

			/* Check for need two */
			if (o_ptr->code & P4_CONSUME_TWO) need = 2;

			/* Check for good on this world needed */
			if ((o_ptr->code & P4_CONSUME_THIS) &&
			    (c_ptr->covered == -1)) continue;

			/* Check for regular consume powers */
			if (((o_ptr->code & P4_CONSUME_ANY) && goods >= need) ||
			    ((o_ptr->code & P4_CONSUME_NOVELTY) &&
				types[GOOD_NOVELTY] >= need) ||
			    ((o_ptr->code & P4_CONSUME_RARE) &&
				types[GOOD_RARE] >= need) ||
			    ((o_ptr->code & P4_CONSUME_GENE) &&
				types[GOOD_GENE] >= need) ||
			    ((o_ptr->code & P4_CONSUME_ALIEN) &&
				types[GOOD_ALIEN] >= need))
			{
				/* Add power to list */
				o_list[n++] = *o_ptr;
			}

			/* Check for consume 3 types */
			if ((o_ptr->code & P4_CONSUME_3_DIFF) && num_types >= 3)
			{
				/* Add power to list */
				o_list[n++] = *o_ptr;
			}

			/* Check for consume different types */
			if ((o_ptr->code & P4_CONSUME_N_DIFF) && goods > 0)
			{
				/* Add power to list */
				o_list[n++] = *o_ptr;
			}

			/* Check for consume all */
			if ((o_ptr->code & P4_CONSUME_ALL) && goods > 0)
			{
				/* Add power to list */
				o_list[n++] = *o_ptr;
			}

			/* Check for discard from hand */
			if ((o_ptr->code & P4_DISCARD_HAND) &&
			    count_player_area(g, who, WHERE_HAND) +
			    p_ptr->fake_hand - p_ptr->fake_discards -
			    p_ptr->fake_played_dev -
			    p_ptr->fake_played_world > 0)
			{
				/* Add power to list */
				o_list[n++] = *o_ptr;
			}

			/* Check for trade action power */
			if ((o_ptr->code & P4_TRADE_ACTION) && goods > 0)
			{
				/* Add power to list */
				o_list[n++] = *o_ptr;
			}

			/* Check for other powers */
			if (o_ptr->code & (P4_DRAW | P4_DRAW_LUCKY | P4_VP |
			                   P4_ANTE_CARD))
			{
				/* Add power to list */
				o_list[n++] = *o_ptr;
			}
		}
	}

	/* Check for no usable powers */
	if (!n) return 0;

	/* Ask player to use power */
	p_ptr->control->choose_consume(g, who, o_list, n);

	/* Check for aborted game */
	if (g->game_over) return 0;

	/* Successfully used power */
	return 1;
}

/*
 * Handle the Consume Phase.
 */
void phase_consume(game *g)
{
	player *p_ptr;
	int i;
	char msg[1024], text[1024];

	/* Loop over players */
	for (i = 0; i < g->num_players; i++)
	{
		/* Get player pointer */
		p_ptr = &g->p[i];

		/* Set turn */
		g->turn = i;

		/* Clear count of earned rewards */
		p_ptr->phase_cards = p_ptr->phase_vp = 0;

		/* Check for consume-trade action chosen */
		if (player_chose(g, i, ACT_CONSUME_TRADE))
		{
			/* First trade a good */
			trade_action(g, i, 0);
		}

		/* Use consume powers until none are usable */
		while (consume_action(g, i));
	}
	
	/* Loop over players */
	for (i = 0; i < g->num_players; i++)
	{
		/* Get player pointer */
		p_ptr = &g->p[i];

		/* Check for earned rewards */
		if (!g->simulation && (p_ptr->phase_cards || p_ptr->phase_vp))
		{
			/* Begin message */
			sprintf(msg, "%s receives ", p_ptr->name);
			
			/* Check for cards received */
			if (p_ptr->phase_cards)
			{
				/* Create card string */
				sprintf(text, "%d card%s ", p_ptr->phase_cards,
				        p_ptr->phase_cards > 1 ? "s" : "");

				/* Add text to message */
				strcat(msg, text);

				/* Check for points as well */
				if (p_ptr->phase_vp)
				{
					/* Add conjuction */
					strcat(msg, "and ");
				}
			}

			/* Check for VP received */
			if (p_ptr->phase_vp)
			{
				/* Create VP string */
				sprintf(text, "%d VP ", p_ptr->phase_vp);

				/* Add text to message */
				strcat(msg, text);
			}

			/* Add conclusion */
			strcat(msg, "for Consume phase.\n");

			/* Send message */
			message_add(msg);
		}
	}

	/* Clear any temp flags on cards */
	clear_temp(g);

	/* Check intermediate goals */
	check_goals(g);
}

/*
 * Produce a good on a world.
 */
void produce_world(game *g, int who, int which)
{
	player *p_ptr;
	card *c_ptr;
	power *o_ptr;
	int i;
	char msg[1024];

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Get card pointer */
	c_ptr = &g->deck[which];

	/* Add good to card */
	add_good(g, c_ptr);

	/* Mark world as producing */
	c_ptr->produced = 1;

	/* Message */
	if (!g->simulation)
	{
		/* Format message */
		sprintf(msg, "%s produces on %s.\n", p_ptr->name,
		        c_ptr->d_ptr->name);

		/* Send message */
		message_add(msg);
	}

	/* Loop over card's powers */
	for (i = 0; i < c_ptr->d_ptr->num_power; i++)
	{
		/* Get power pointer */
		o_ptr = &c_ptr->d_ptr->powers[i];

		/* Skip non-produce powers */
		if (o_ptr->phase != PHASE_PRODUCE) continue;

		/* Check for "draw if produced here" power */
		if (o_ptr->code & P5_DRAW_IF)
		{
			/* Draw cards */
			draw_cards(g, who, o_ptr->value);

			/* Count reward */
			p_ptr->phase_cards += o_ptr->value;
		}
	}
}

/*
 * Called when a player has chosen a card to discard in order to produce
 * on a world.
 */
void discard_produce_chosen(game *g, int who, int world, int discard)
{
	player *p_ptr;
	card *c_ptr;
	char msg[1024];

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Get card to discard */
	c_ptr = &g->deck[discard];

	/* Disown card */
	c_ptr->owner = -1;

	/* Move card to discard */
	c_ptr->where = WHERE_DISCARD;

	/* Message */
	if (!g->simulation)
	{
		/* Format message */
		sprintf(msg, "%s discards one card.\n", p_ptr->name);

		/* Send message */
		message_add(msg);
	}

	/* Produce on world */
	produce_world(g, who, world);
}

/*
 * Ask the player to discard a card in order to produce on this world.
 */
static void discard_produce(game *g, int who, int world)
{
	player *p_ptr;
	card *c_ptr;
	int list[MAX_DECK], n = 0;
	int i;

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

		/* Add card to list */
		list[n++] = i;
	}

	/* Ask player to choose discard */
	p_ptr->control->choose_discard_produce(g, who, world, list, n);
}

/*
 * Produce a good on an empty windfall world.
 */
static void produce_windfall(game *g, int who, power *o_ptr)
{
	player *p_ptr;
	card *c_ptr;
	int list[MAX_DECK], n = 0;
	int good = 0, match;
	int i, j;

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Check for power passed */
	if (o_ptr)
	{
		/* Check for good type restriction */
		if (o_ptr->code & P5_WINDFALL_NOVELTY) good = GOOD_NOVELTY;
		if (o_ptr->code & P5_WINDFALL_RARE) good = GOOD_RARE;
		if (o_ptr->code & P5_WINDFALL_GENE) good = GOOD_GENE;
		if (o_ptr->code & P5_WINDFALL_ALIEN) good = GOOD_ALIEN;
	}

	/* Loop over active cards */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Skip unowned cards */
		if (c_ptr->owner != who) continue;

		/* Skip inactive cards */
		if (c_ptr->where != WHERE_ACTIVE) continue;

		/* Skip non-windfall worlds */
		if (!(c_ptr->d_ptr->flags & FLAG_WINDFALL)) continue;

		/* Skip worlds that do not produce goods */
		if (!c_ptr->d_ptr->good_type) continue;

		/* Skip worlds of incorrect type */
		if (good && c_ptr->d_ptr->good_type != good) continue;

		/* Skip worlds with goods already */
		if (c_ptr->covered != -1) continue;

		/* Check for "not this world" modifier on power */
		if (o_ptr && o_ptr->code & P5_NOT_THIS)
		{
			/* Assume this card doesn't match */
			match = 0;

			/* Loop over this card's powers */
			for (j = 0; j < c_ptr->d_ptr->num_power; j++)
			{
				/* Check for match */
				if (!memcmp(&c_ptr->d_ptr->powers[j], o_ptr,
				            sizeof(power)))
				{
					/* Card matches */
					match = 1;
				}
			}

			/* Skip world if match */
			if (match) continue;
		}

		/* Add to list */
		list[n++] = i;
	}

	/* Do nothing if no worlds available */
	if (!n) return;

	/* Produce automatically if only one world available */
	if (n == 1)
	{
		/* Produce */
		produce_world(g, who, list[0]);

		/* Done */
		return;
	}

	/* Ask player to choose world to produce */
	p_ptr->control->choose_windfall(g, who, list, n);
}

/*
 * Called when a player has chosen a produce power.
 */
void produce_chosen(game *g, int who, power *o_ptr)
{
	player *p_ptr;
	card *c_ptr;
	int i, j, found = 0, count, produced[6];

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Check for produce phase bonus used */
	if (!o_ptr)
	{
		/* Phase bonus is used */
		p_ptr->phase_bonus_used = 1;

		/* Produce on a windfall world */
		produce_windfall(g, who, NULL);

		/* Done */
		return;
	}

	/* Loop over cards */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Skip unowned cards */
		if (c_ptr->owner != who) continue;

		/* Skip inactive cards */
		if (c_ptr->where != WHERE_ACTIVE) continue;

		/* Check for produce power */
		if (o_ptr->code & P5_PRODUCE)
		{
			/* Skip worlds with goods already */
			if (c_ptr->covered != -1) continue;
		}

		/* Loop over card powers */
		for (j = 0; j < c_ptr->d_ptr->num_power; j++)
		{
			/* Skip powers already marked as used */
			if (c_ptr->used[j]) continue;

			/* Check for matching power */
			if (!memcmp(&c_ptr->d_ptr->powers[j], o_ptr,
			            sizeof(power)))
			{
				/* Mark power as used */
				c_ptr->used[j] = 1;

				/* Power is found */
				found = 1;
				break;
			}
		}

		/* Stop looking if power found */
		if (found) break;
	}

	/* Check for regular produce */
	if (o_ptr->code & P5_PRODUCE)
	{
		/* Produce */
		produce_world(g, who, i);

		/* Done */
		return;
	}

	/* Check for discard to produce */
	if (o_ptr->code & P5_DISCARD_PRODUCE)
	{
		/* Discard card to produce */
		discard_produce(g, who, i);

		/* Done */
		return;
	}

	/* Check for draw cards */
	if (o_ptr->code & P5_DRAW)
	{
		/* Draw cards */
		draw_cards(g, who, o_ptr->value);

		/* Count reward */
		p_ptr->phase_cards += o_ptr->value;

		/* Done */
		return;
	}

	/* Check for draw per gene world */
	if (o_ptr->code & P5_DRAW_WORLD_GENE)
	{
		/* Assume no gene worlds */
		count = 0;

		/* Loop over cards */
		for (i = 0; i < g->deck_size; i++)
		{
			/* Get card pointer */
			c_ptr = &g->deck[i];

			/* Skip unowned cards */
			if (c_ptr->owner != who) continue;

			/* Skip inactive cards */
			if (c_ptr->where != WHERE_ACTIVE) continue;

			/* Check for gene world */
			if (c_ptr->d_ptr->good_type == GOOD_GENE) count++;
		}

		/* Draw cards */
		draw_cards(g, who, count * o_ptr->value);

		/* Count reward */
		p_ptr->phase_cards += count * o_ptr->value;

		/* Done */
		return;
	}

	/* Check for draw per military world */
	if (o_ptr->code & P5_DRAW_MILITARY)
	{
		/* Count military worlds */
		count = count_active_flags(g, who, FLAG_MILITARY);

		/* Draw cards */
		draw_cards(g, who, count * o_ptr->value);

		/* Count reward */
		p_ptr->phase_cards += count * o_ptr->value;

		/* Done */
		return;
	}

	/* Check for draw per rebel world */
	if (o_ptr->code & P5_DRAW_REBEL)
	{
		/* Assume no rebel worlds */
		count = 0;

		/* Loop over cards */
		for (i = 0; i < g->deck_size; i++)
		{
			/* Get card pointer */
			c_ptr = &g->deck[i];

			/* Skip unowned cards */
			if (c_ptr->owner != who) continue;

			/* Skip inactive cards */
			if (c_ptr->where != WHERE_ACTIVE) continue;

			/* Skip developments */
			if (c_ptr->d_ptr->type == TYPE_DEVELOPMENT) continue;

			/* Check for rebel world */
			if (c_ptr->d_ptr->flags & FLAG_REBEL) count++;
		}

		/* Draw cards */
		draw_cards(g, who, count * o_ptr->value);

		/* Count reward */
		p_ptr->phase_cards += count * o_ptr->value;

		/* Done */
		return;
	}

	/* Check for draw per chromosome card */
	if (o_ptr->code & P5_DRAW_CHROMO)
	{
		/* Count chromosome worlds */
		count = count_active_flags(g, who, FLAG_CHROMO);

		/* Draw cards */
		draw_cards(g, who, count * o_ptr->value);

		/* Count reward */
		p_ptr->phase_cards += count * o_ptr->value;

		/* Done */
		return;
	}

	/* Check for produce on windfall */
	if (o_ptr->code & (P5_WINDFALL_ANY | P5_WINDFALL_NOVELTY |
			   P5_WINDFALL_RARE | P5_WINDFALL_GENE |
			   P5_WINDFALL_ALIEN))
	{
		/* Produce on a windfall world */
		produce_windfall(g, who, o_ptr);

		/* Done */
		return;
	}

	/* Clear production counts */
	for (i = 0; i < 6; i++) produced[i] = 0;

	/* Loop over cards */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Skip unowned cards */
		if (c_ptr->owner != who) continue;

		/* Skip inactive cards */
		if (c_ptr->where != WHERE_ACTIVE) continue;

		/* Skip cards that did not produce */
		if (!c_ptr->produced) continue;

		/* Count types */
		produced[c_ptr->d_ptr->good_type]++;
	}

	/* Check for draw per novelty produced */
	if (o_ptr->code & P5_DRAW_EACH_NOVELTY)
	{
		/* Award cards */
		draw_cards(g, who, produced[GOOD_NOVELTY]);

		/* Count reward */
		p_ptr->phase_cards += produced[GOOD_NOVELTY];

		/* Done */
		return;
	}

	/* Check for draw per rare produced */
	if (o_ptr->code & P5_DRAW_EACH_RARE)
	{
		/* Award cards */
		draw_cards(g, who, produced[GOOD_RARE]);

		/* Count reward */
		p_ptr->phase_cards += produced[GOOD_RARE];

		/* Done */
		return;
	}

	/* Check for draw per gene produced */
	if (o_ptr->code & P5_DRAW_EACH_GENE)
	{
		/* Award cards */
		draw_cards(g, who, produced[GOOD_GENE]);

		/* Count reward */
		p_ptr->phase_cards += produced[GOOD_GENE];

		/* Done */
		return;
	}

	/* Check for draw per alien produced */
	if (o_ptr->code & P5_DRAW_EACH_ALIEN)
	{
		/* Award cards */
		draw_cards(g, who, produced[GOOD_ALIEN]);

		/* Count reward */
		p_ptr->phase_cards += produced[GOOD_ALIEN];

		/* Done */
		return;
	}

	/* Check for draw per different kind produced */
	if (o_ptr->code & P5_DRAW_DIFFERENT)
	{
		/* Start count at zero */
		count = 0;

		/* Count types */
		for (i = 0; i < 6; i++)
		{
			/* Check for this type produced */
			if (produced[i]) count++;
		}

		/* Award cards */
		draw_cards(g, who, count);

		/* Count reward */
		p_ptr->phase_cards += count;

		/* Done */
		return;
	}
}

/*
 * Loop over produce powers and use them.
 *
 * It is occasionally necessary to ask the player which order to use some
 * powers.
 */
int produce_action(game *g, int who)
{
	player *p_ptr;
	card *c_ptr;
	power *o_ptr, o_list[100];
	int all_codes = 0;
	int i, j, n = 0;

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Loop over active cards */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Skip unowned cards */
		if (c_ptr->owner != who) continue;

		/* Skip inactive cards */
		if (c_ptr->where != WHERE_ACTIVE) continue;

		/* Loop over card powers */
		for (j = 0; j < c_ptr->d_ptr->num_power; j++)
		{
			/* Get power pointer */
			o_ptr = &c_ptr->d_ptr->powers[j];

			/* Skip non-produce powers */
			if (o_ptr->phase != PHASE_PRODUCE) continue;

			/* Skip used powers */
			if (c_ptr->used[j]) continue;

			/* Check for draw cards */
			if (o_ptr->code & (P5_DRAW))
			{
				/* Use immediately */
				produce_chosen(g, who, o_ptr);
			}

			/* Check for produce */
			if (o_ptr->code & (P5_PRODUCE))
			{
				/* Skip worlds with good already */
				if (c_ptr->covered != -1) continue;

				/* Use power immediately */
				produce_chosen(g, who, o_ptr);
			}

			/* Check for draw per card type powers */
			if (o_ptr->code & (P5_DRAW_WORLD_GENE |
			                   P5_DRAW_MILITARY |
			                   P5_DRAW_REBEL |
			                   P5_DRAW_CHROMO))
			{
				/* Use power immediately */
				produce_chosen(g, who, o_ptr);
			}

			/* Check for windfall production powers */
			if (o_ptr->code & (P5_WINDFALL_ANY |
			                   P5_WINDFALL_NOVELTY |
			                   P5_WINDFALL_RARE |
			                   P5_WINDFALL_GENE |
			                   P5_WINDFALL_ALIEN))
			{
				/* Use power immediately */
				produce_chosen(g, who, o_ptr);
			}

			/* Check for discard to produce */
			if (o_ptr->code & (P5_DISCARD_PRODUCE))
			{
				/* Skip worlds with good already */
				if (c_ptr->covered != -1) continue;

				/* Add power to list */
				o_list[n++] = *o_ptr;

				/* Track powers in list */
				all_codes |= o_ptr->code;
			}

			/* Check for draw per type produced */
			if (o_ptr->code & (P5_DRAW_EACH_NOVELTY |
			                   P5_DRAW_EACH_RARE |
			                   P5_DRAW_EACH_GENE |
			                   P5_DRAW_EACH_ALIEN |
			                   P5_DRAW_DIFFERENT))
			{
				/* Add power to list */
				o_list[n++] = *o_ptr;

				/* Track powers in list */
				all_codes |= o_ptr->code;
			}
		}
	}

	/* Check for unused produce phase bonus */
	if (player_chose(g, who, ACT_PRODUCE) && !p_ptr->phase_bonus_used)
	{
		/* Use bonus */
		produce_chosen(g, who, NULL);
	}

	/* Check for no additional usable powers */
	if (!n) return 0;

	/* Check for necessity of asking for power usage order */
	if ((all_codes & P5_DISCARD_PRODUCE) &&
	    (all_codes & (P5_DRAW_DIFFERENT | P5_DRAW_EACH_ALIEN)))
	{
		/* Ask player to use power */
		p_ptr->control->choose_produce(g, who, o_list, n);

		/* Check for aborted game */
		if (g->game_over) return 0;

		/* Successfully used power */
		return 1;
	}

	/* Otherwise use all powers in any order */
	for (i = 0; i < n; i++)
	{
		/* Use power */
		produce_chosen(g, who, &o_list[i]);
	}

	/* No other powers to use */
	return 0;
}

/*
 * Do end-of-phase produce powers for everyone.
 */
void phase_produce_end(game *g)
{
	player *p_ptr;
	card *c_ptr;
	power *o_ptr, o_list[100];
	int rare[MAX_PLAYER], most;
	int i, j, n;

	/* Loop over players */
	for (i = 0; i < g->num_players; i++)
	{
		/* Assume no rare goods produced */
		rare[i] = 0;

		/* Loop over cards */
		for (j = 0; j < g->deck_size; j++)
		{
			/* Get card pointer */
			c_ptr = &g->deck[j];

			/* Skip unowned cards */
			if (c_ptr->owner != i) continue;

			/* Skip inactive cards */
			if (c_ptr->where != WHERE_ACTIVE) continue;

			/* Skip cards that did not produce */
			if (!c_ptr->produced) continue;

			/* Check for rare */
			if (c_ptr->d_ptr->good_type == GOOD_RARE) rare[i]++;
		}
	}

	/* Loop over players again */
	for (i = 0; i < g->num_players; i++)
	{
		/* Get player pointer */
		p_ptr = &g->p[i];

		/* Assume player created most */
		most = 1;

		/* Loop over other players */
		for (j = 0; j < g->num_players; j++)
		{
			/* Skip same player */
			if (i == j) continue;

			/* Check for no more rares produced */
			if (rare[j] >= rare[i]) most = 0;
		}

		/* Skip player who did not make most */
		if (!most) continue;

		/* Get list of produce powers */
		n = get_powers(g, i, PHASE_PRODUCE, o_list);

		/* Loop over powers */
		for (j = 0; j < n; j++)
		{
			/* Get power pointer */
			o_ptr = &o_list[j];

			/* Check for produced most rare */
			if (o_ptr->code & P5_DRAW_MOST_RARE)
			{
				/* Draw cards */
				draw_cards(g, i, o_ptr->value);

				/* Count reward */
				p_ptr->phase_cards += o_ptr->value;
			}
		}
	}
}

/*
 * Handle the Produce Phase.
 */
void phase_produce(game *g)
{
	player *p_ptr;
	int i;
	char msg[1024];

	/* Loop over players */
	for (i = 0; i < g->num_players; i++)
	{
		/* Get player pointer */
		p_ptr = &g->p[i];

		/* Clear phase rewards */
		p_ptr->phase_cards = 0;

		/* Set current player */
		g->turn = i;

		/* Use player's produce powers */
		while (produce_action(g, i));
	}

	/* Handle end of phase powers */
	phase_produce_end(g);

	/* Loop over players */
	for (i = 0; i < g->num_players; i++)
	{
		/* Get player pointer */
		p_ptr = &g->p[i];

		/* Check for earned rewards */
		if (!g->simulation && p_ptr->phase_cards)
		{
			/* Format message */
			sprintf(msg,
			        "%s receives %d card%s for Produce phase.\n",
			        p_ptr->name, p_ptr->phase_cards,
			        p_ptr->phase_cards > 1 ? "s" : "");

			/* Send message */
			message_add(msg);
		}
	}

	/* Clear any temp flags on cards */
	clear_temp(g);

	/* Check intermediate goals */
	check_goals(g);
}

/*
 * Handle the Discard Phase.
 */
void phase_discard(game *g)
{
	player *p_ptr;
	card *c_ptr;
	int i, j, n, list[MAX_DECK], target;
	char msg[1024];

	/* Loop over players */
	for (i = 0; i < g->num_players; i++)
	{
		/* Get player pointer */
		p_ptr = &g->p[i];

		/* Set current player */
		g->turn = i;

		/* Assume player has no cards */
		n = 0;

		/* Assume player must discard to 10 */
		target = 10;

		/* Check for "discard to 12" flag */
		if (count_active_flags(g, i, FLAG_DISCARD_TO_12))
		{
			/* Set target count to 12 */
			target = 12;
		}

		/* Loop over cards */
		for (j = 0; j < g->deck_size; j++)
		{
			/* Get card pointer */
			c_ptr = &g->deck[j];

			/* Skip unowned cards */
			if (c_ptr->owner != i) continue;

			/* Skip cards not in hand */
			if (c_ptr->where != WHERE_HAND) continue;

			/* Add card to list */
			list[n++] = j;
		}

		/* Assume no cards discarded */
		p_ptr->end_discard = 0;

		/* Check for not too many cards */
		if (n <= target) continue;

		/* Remember cards discarded */
		p_ptr->end_discard = n - target;

		/* Message */
		if (!g->simulation)
		{
			/* Format message */
			sprintf(msg, "%s discards %d at end of turn.\n",
			        p_ptr->name, n - target);

			/* Send message */
			message_add(msg);
		}

		/* Check for opponent's turn in simulated game */
		if (g->simulation && g->sim_who != i)
		{
			/* Discard first cards */
			discard_callback(g, i, list, n - target);

			/* Next player */
			continue;
		}

		/* Ask player to discard remaining */
		p_ptr->control->choose_discard(g, i, list, n, n - target);

		/* Check for aborted game */
		if (g->game_over) return;
	}
}

/*
 * Return the minimum amount of progress needed to claim a "most" goal.
 */
int goal_minimum(int goal)
{
	/* Switch on goal type */
	switch (goal)
	{
		case GOAL_MOST_MILITARY: return 6;
		case GOAL_MOST_BLUE_BROWN: return 3;
		case GOAL_MOST_DEVEL: return 4;
		case GOAL_MOST_PRODUCTION: return 4;
		case GOAL_MOST_EXPLORE: return 3;
		case GOAL_MOST_REBEL: return 3;
	}

	/* XXX */
	return -1;
}

/*
 * Check a player's progress towards a goal.
 *
 * Return zero if the player does not qualify.
 */
static int check_goal_player(game *g, int goal, int who)
{
	player *p_ptr;
	card *c_ptr;
	power *o_ptr;
	int good[6], phase[6], count = 0;
	int i, j;

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Switch on goal type */
	switch (goal)
	{
		/* First to 5 VP chips */
		case GOAL_FIRST_5_VP:

			/* Check for 5 VP */
			return p_ptr->vp >= 5;

		/* First to 4 good types */
		case GOAL_FIRST_4_TYPES:

			/* Clear good marks */
			for (i = 0; i < 6; i++) good[i] = 0;

			/* Loop over cards */
			for (i = 0; i < g->deck_size; i++)
			{
				/* Get card pointer */
				c_ptr = &g->deck[i];

				/* Skip unowned cards */
				if (c_ptr->owner != who) continue;

				/* Skip inactive cards */
				if (c_ptr->where != WHERE_ACTIVE) continue;

				/* Skip non-worlds */
				if (c_ptr->d_ptr->type != TYPE_WORLD) continue;

				/* Mark good type */
				good[c_ptr->d_ptr->good_type] = 1;
			}

			/* Count types */
			for (i = GOOD_NOVELTY; i <= GOOD_ALIEN; i++)
			{
				/* Check for active type */
				if (good[i]) count++;
			}

			/* Check for all four types */
			return count == 4;

		/* First to three Alien cards */
		case GOAL_FIRST_3_ALIEN:

			/* Count ALIEN flags */
			count = count_active_flags(g, who, FLAG_ALIEN);

			/* Check for three alien cards */
			return count >= 3;

		/* First to discard at end of turn */
		case GOAL_FIRST_DISCARD:

			/* Check for previous discard */
			return p_ptr->end_discard;

		/* First to have powers for each phase */
		case GOAL_FIRST_PHASE_POWER:

			/* Clear phase marks */
			for (i = 0; i < 6; i++) phase[i] = 0;

			/* Loop over cards */
			for (i = 0; i < g->deck_size; i++)
			{
				/* Get card pointer */
				c_ptr = &g->deck[i];

				/* Skip unowned cards */
				if (c_ptr->owner != who) continue;

				/* Skip inactive cards */
				if (c_ptr->where != WHERE_ACTIVE) continue;

				/* Loop over card powers */
				for (j = 0; j < c_ptr->d_ptr->num_power; j++)
				{
					/* Get power pointer */
					o_ptr = &c_ptr->d_ptr->powers[j];

					/* Check for trade power */
					if (o_ptr->phase == PHASE_CONSUME &&
					    (o_ptr->code & P4_TRADE_MASK))
					{
						/* XXX Mark trade power */
						phase[0] = 1;
					}
					else
					{
						/* Mark phase */
						phase[o_ptr->phase] = 1;
					}
				}
			}

			/* Count phases with powers */
			for (i = 0; i < 6; i++)
			{
				/* Check for power */
				if (phase[i]) count++;
			}

			/* Check for all 6 phases */
			return count == 6;

		/* First to have a six-cost development */
		case GOAL_FIRST_SIX_DEVEL:

			/* Loop over cards */
			for (i = 0; i < g->deck_size; i++)
			{
				/* Get card pointer */
				c_ptr = &g->deck[i];

				/* Skip unowned cards */
				if (c_ptr->owner != who) continue;

				/* Skip inactive cards */
				if (c_ptr->where != WHERE_ACTIVE) continue;

				/* Skip worlds */
				if (c_ptr->d_ptr->type == TYPE_WORLD) continue;

				/* Check for variable points */
				if (c_ptr->d_ptr->num_vp_bonus) return 1;
			}

			/* No six-cost developments */
			return 0;

		/* First to three Uplift cards */
		case GOAL_FIRST_3_UPLIFT:

			/* Count UPLIFT flags */
			count = count_active_flags(g, who, FLAG_UPLIFT);

			/* Check for three Uplift cards */
			return count >= 3;

		/* First to 4 goods */
		case GOAL_FIRST_4_GOODS:

			/* Loop over cards */
			for (i = 0; i < g->deck_size; i++)
			{
				/* Get card pointer */
				c_ptr = &g->deck[i];

				/* Skip unowned cards */
				if (c_ptr->owner != who) continue;

				/* Skip inactive cards */
				if (c_ptr->where != WHERE_ACTIVE) continue;

				/* Skip non-worlds */
				if (c_ptr->d_ptr->type != TYPE_WORLD) continue;

				/* Check for good */
				if (c_ptr->covered != -1) count++;
			}

			/* Check for four goods */
			return count >= 4;

		/* First to 8 active cards */
		case GOAL_FIRST_8_ACTIVE:

			/* Loop over cards */
			for (i = 0; i < g->deck_size; i++)
			{
				/* Get card pointer */
				c_ptr = &g->deck[i];

				/* Skip unowned cards */
				if (c_ptr->owner != who) continue;

				/* Skip inactive cards */
				if (c_ptr->where != WHERE_ACTIVE) continue;

				/* Count card */
				count++;
			}

			/* Check for 8 cards */
			return count >= 8;

		/* Most military (minimum 6) */
		case GOAL_MOST_MILITARY:

			/* Get military strength */
			return total_military(g, who);

		/* Most blue/brown worlds (minimum 3) */
		case GOAL_MOST_BLUE_BROWN:

			/* Loop over cards */
			for (i = 0; i < g->deck_size; i++)
			{
				/* Get card pointer */
				c_ptr = &g->deck[i];

				/* Skip unowned cards */
				if (c_ptr->owner != who) continue;

				/* Skip inactive cards */
				if (c_ptr->where != WHERE_ACTIVE) continue;

				/* Skip non-worlds */
				if (c_ptr->d_ptr->type != TYPE_WORLD) continue;

				/* Check for blue or brown */
				if (c_ptr->d_ptr->good_type == GOOD_NOVELTY ||
				    c_ptr->d_ptr->good_type == GOOD_RARE)
				{
					/* Count world */
					count++;
				}
			}

			/* Return count */
			return count;

		/* Most developments (minimum 4) */
		case GOAL_MOST_DEVEL:

			/* Loop over cards */
			for (i = 0; i < g->deck_size; i++)
			{
				/* Get card pointer */
				c_ptr = &g->deck[i];

				/* Skip unowned cards */
				if (c_ptr->owner != who) continue;

				/* Skip inactive cards */
				if (c_ptr->where != WHERE_ACTIVE) continue;

				/* Skip worlds */
				if (c_ptr->d_ptr->type == TYPE_WORLD) continue;

				/* Count developments */
				count++;
			}

			/* Return count */
			return count;

		/* Most production worlds (minimum 4) */
		case GOAL_MOST_PRODUCTION:

			/* Loop over cards */
			for (i = 0; i < g->deck_size; i++)
			{
				/* Get card pointer */
				c_ptr = &g->deck[i];

				/* Skip unowned cards */
				if (c_ptr->owner != who) continue;

				/* Skip inactive cards */
				if (c_ptr->where != WHERE_ACTIVE) continue;

				/* Skip non-worlds */
				if (c_ptr->d_ptr->type != TYPE_WORLD) continue;

				/* Skip windfall worlds */
				if (c_ptr->d_ptr->flags & FLAG_WINDFALL)
					continue;

				/* Skip worlds with no good type */
				if (!c_ptr->d_ptr->good_type) continue;

				/* Count world */
				count++;
			}

			/* Return count */
			return count;

		/* Most explore powers (minimum 3) */
		case GOAL_MOST_EXPLORE:

			/* Loop over cards */
			for (i = 0; i < g->deck_size; i++)
			{
				/* Get card pointer */
				c_ptr = &g->deck[i];

				/* Skip unowned cards */
				if (c_ptr->owner != who) continue;

				/* Skip inactive cards */
				if (c_ptr->where != WHERE_ACTIVE) continue;

				/* Loop over card powers */
				for (j = 0; j < c_ptr->d_ptr->num_power; j++)
				{
					/* Get power pointer */
					o_ptr = &c_ptr->d_ptr->powers[j];

					/* Check for explore phase */
					if (o_ptr->phase == PHASE_EXPLORE)
					{
						/* Count card */
						count++;

						/* Stop looking */
						break;
					}
				}
			}

			/* Return count */
			return count;

		/* Most Rebel military worlds (minimum 3) */
		case GOAL_MOST_REBEL:

			/* Count military Rebel worlds */
			return count_active_flags(g, who, FLAG_REBEL |
			                                  FLAG_MILITARY);
	}

	/* XXX */
	return 0;
}

/*
 * Goal names.
 */
char *goal_name[MAX_GOAL] =
{
	"Galactic Standard of Living",
	"System Diversity",
	"Overlord Discoveries",
	"Budget Surplus",
	"Innovation Leader",
	"Galactic Status",
	"Uplift Knowledge",
	"Galactic Riches",
	"Expansion Leader",

	"Greatest Military",
	"Largest Industry",
	"Greatest Infrastructure",
	"Production Leader",
	"Research Leader",
	"Propaganda Edge",
};

/*
 * Award goals to players who meet requirements.
 */
void check_goals(game *g)
{
	player *p_ptr;
	int count[MAX_PLAYER], most;
	int i, j, k;
	char msg[1024];

	/* Loop over "first" goals */
	for (i = GOAL_FIRST_5_VP; i <= GOAL_FIRST_8_ACTIVE; i++)
	{
		/* Skip inactive goals */
		if (!g->goal_active[i]) continue;

		/* Skip already claimed goals */
		if (!g->goal_avail[i]) continue;

		/* Do not check goals that cannot happen yet */
		switch (i)
		{
			/* End of turn only */
			case GOAL_FIRST_DISCARD:
			
				/* Only check at end of turn */
				if (g->cur_action != -1) continue;
				break;

			/* Develop phase only */
			case GOAL_FIRST_SIX_DEVEL:

				/* Only check after develop */
				if (g->cur_action != ACT_DEVELOP &&
				    g->cur_action != ACT_DEVELOP2) continue;
				break;

			/* Settle phase only */
			case GOAL_FIRST_4_TYPES:

				/* Only check after settle */
				if (g->cur_action != ACT_SETTLE &&
				    g->cur_action != ACT_SETTLE2) continue;
				break;

			/* Develop/Settle phases only */
			case GOAL_FIRST_3_ALIEN:
			case GOAL_FIRST_PHASE_POWER:
			case GOAL_FIRST_3_UPLIFT:
			case GOAL_FIRST_8_ACTIVE:

				/* Only check after develop/settle */
				if (g->cur_action != ACT_DEVELOP &&
				    g->cur_action != ACT_DEVELOP2 &&
				    g->cur_action != ACT_SETTLE &&
				    g->cur_action != ACT_SETTLE2) continue;
				break;

			/* Consume phase only */
			case GOAL_FIRST_5_VP:

				/* Only check after consume phase */
				if (g->cur_action != ACT_CONSUME_TRADE)
					continue;
				break;

			/* Settle or Produce phases */
			case GOAL_FIRST_4_GOODS:

				/* Only check after settle or produce */
				if (g->cur_action != ACT_SETTLE &&
				    g->cur_action != ACT_SETTLE2 &&
				    g->cur_action != ACT_PRODUCE) continue;
				break;
		}

		/* Loop over players */
		for (j = 0; j < g->num_players; j++)
		{
			/* Get player pointer */
			p_ptr = &g->p[j];

			/* Check for player meeting requirement */
			if (check_goal_player(g, i, j))
			{
				/* Claim goal */
				p_ptr->goal_claimed[i] = 1;

				/* Remove goal availability */
				g->goal_avail[i] = 0;

				/* Message */
				if (!g->simulation)
				{
					/* Format message */
					sprintf(msg, "%s claims %s goal.\n",
					        p_ptr->name, goal_name[i]);

					/* Send message */
					message_add(msg);
				}
			}
		}
	}

	/* Loop over "most" goals */
	for (i = GOAL_MOST_MILITARY; i <= GOAL_MOST_REBEL; i++)
	{
		/* Skip inactive goals */
		if (!g->goal_active[i]) continue;

		/* Do not check goals that cannot happen yet */
		switch (i)
		{
			/* Settle phase only */
			case GOAL_MOST_BLUE_BROWN:
			case GOAL_MOST_PRODUCTION:
			case GOAL_MOST_REBEL:

				/* Only check after settle */
				if (g->cur_action != ACT_SETTLE &&
				    g->cur_action != ACT_SETTLE2) continue;
				break;

			/* Develop/Settle phases only */
			case GOAL_MOST_MILITARY:
			case GOAL_MOST_EXPLORE:
			case GOAL_MOST_DEVEL:

				/* Only check after develop/settle */
				if (g->cur_action != ACT_DEVELOP &&
				    g->cur_action != ACT_DEVELOP2 &&
				    g->cur_action != ACT_SETTLE &&
				    g->cur_action != ACT_SETTLE2) continue;
				break;
		}

		/* Clear most progress */
		g->goal_most[i] = 0;

		/* Loop over each player */
		for (j = 0; j < g->num_players; j++)
		{
			/* Get player's progress */
			count[j] = check_goal_player(g, i, j);

			/* Save progress */
			g->p[j].goal_progress[i] = count[j];

			/* Check for more than most */
			if (count[j] > g->goal_most[i])
			{
				/* Remember most */
				g->goal_most[i] = count[j];
			}

			/* Check for insufficient progress */
			if (count[j] < goal_minimum(i)) count[j] = 0;
		}

		/* Check for losing goal */
		for (j = 0; j < g->num_players; j++)
		{
			/* Get player pointer */
			p_ptr = &g->p[j];

			/* Check for goal claimed and lost */
			if (p_ptr->goal_claimed[i] && !count[j])
			{
				/* Lose goal */
				g->goal_avail[i] = 1;
				p_ptr->goal_claimed[i] = 0;
			}
		}

		/* Loop over players */
		for (j = 0; j < g->num_players; j++)
		{
			/* Assume this player has most */
			most = 1;

			/* Loop over opponents */
			for (k = 0; k < g->num_players; k++)
			{
				/* Do not compete with ourself */
				if (j == k) continue;

				/* Check for no more than opponent */
				if (count[j] <= count[k]) most = 0;
			}

			/* Get player pointer */
			p_ptr = &g->p[j];

			/* Check for more than anyone else */
			if (most && !p_ptr->goal_claimed[i])
			{
				/* Goal is no longer available */
				g->goal_avail[i] = 0;

				/* Loop over players */
				for (k = 0; k < g->num_players; k++)
				{
					/* Get player pointer */
					p_ptr = &g->p[k];

					/* Award card to player with most */
					p_ptr->goal_claimed[i] = (j == k);
				}

				/* Message */
				if (!g->simulation)
				{
					/* Get player pointer */
					p_ptr = &g->p[j];

					/* Format message */
					sprintf(msg, "%s claims %s goal.\n",
					        p_ptr->name, goal_name[i]);

					/* Send message */
					message_add(msg);
				}
			}
		}
	}
}



/*
 * Action names.
 */
char *action_name[MAX_ACTION] =
{
	"Explore +5",
	"Explore +1,+1",
	"Develop",
	"Develop",
	"Settle",
	"Settle",
	"Consume-Trade",
	"Consume-x2",
	"Produce"
};

/*
 * One game round.
 */
int game_round(game *g)
{
	player *p_ptr;
	int i, j, target;
	char msg[1024];

	/* Assume no phases will be executed */
	for (i = 0; i < MAX_ACTION; i++) g->action_selected[i] = 0;

	/* Clear current action */
	g->cur_action = -1;

	/* Check for aborted game */
	if (g->game_over) return 0;

	/* Loop over players */
	for (i = 0; i < g->num_players; i++)
	{
		/* Get player pointer */
		p_ptr = &g->p[i];

		/* Set current turn */
		g->turn = i;

		/* Get player's role choice(s) */
		p_ptr->control->choose_action(g, i, p_ptr->action);

		/* Check for aborted game */
		if (g->game_over) return 0;
	}

	/* Loop over players */
	for (i = 0; i < g->num_players; i++)
	{
		/* Get player pointer */
		p_ptr = &g->p[i];

		/* Loop over action choices */
		for (j = 0; j < 2; j++)
		{
			/* Skip empty choices */
			if (p_ptr->action[j] == -1) continue;

			/* Mark action as selected */
			g->action_selected[p_ptr->action[j]] = 1;
		}

		/* Check for real game */
		if (!g->simulation && !g->advanced)
		{
			/* Format message */
			sprintf(msg, "%s chooses %s.\n", p_ptr->name,
				action_name[p_ptr->action[0]]);

			/* Send message */
			message_add(msg);
		}

		/* Check for real advanced game */
		if (!g->simulation && g->advanced)
		{
			/* Format message */
			sprintf(msg, "%s chooses %s/%s.\n", p_ptr->name,
			        action_name[p_ptr->action[0]],
			        action_name[p_ptr->action[1]]);

			/* Send message */
			message_add(msg);
		}

		/* React to choice */
		p_ptr->control->react_action(g, i, p_ptr->action);
	}

	/* Collapse explore actions */
	if (g->action_selected[ACT_EXPLORE_1_1])
	{
		/* Set first explore action */
		g->action_selected[ACT_EXPLORE_5_0] = 1;

		/* Clear second explore action */
		g->action_selected[ACT_EXPLORE_1_1] = 0;
	}

	/* Collapse consume actions */
	if (g->action_selected[ACT_CONSUME_X2])
	{
		/* Set first consume action */
		g->action_selected[ACT_CONSUME_TRADE] = 1;

		/* Clear second consume action */
		g->action_selected[ACT_CONSUME_X2] = 0;
	}

	/* Loop over actions in order */
	for (i = ACT_EXPLORE_5_0; i <= ACT_PRODUCE; i++)
	{
		/* Set current action */
		g->cur_action = i;

		/* Skip unchosen phases */
		if (!g->action_selected[i]) continue;

		/* Handle phase */
		switch (i)
		{
			/* Explore */
			case ACT_EXPLORE_5_0:

				/* Run explore phase */
				phase_explore(g);
				break;
			
			/* Develop */
			case ACT_DEVELOP:
			case ACT_DEVELOP2:

				/* Run develop phase */
				phase_develop(g);
				break;
			
			/* Settle */
			case ACT_SETTLE:
			case ACT_SETTLE2:

				/* Run settle phase */
				phase_settle(g);
				break;
			
			/* Consume */
			case ACT_CONSUME_TRADE:

				/* Run consume phase */
				phase_consume(g);
				break;
			
			/* Produce */
			case ACT_PRODUCE:

				/* Run produce phase */
				phase_produce(g);
				break;
		}

		/* Check for aborted game */
		if (g->game_over) return 0;
	}

	/* Clear current action */
	g->cur_action = -1;

	/* Handle discard phase */
	phase_discard(g);

	/* Check intermediate goals */
	check_goals(g);

	/* Check for out of VPs */
	if (g->vp_pool <= 0) g->game_over = 1;

	/* Loop over players */
	for (i = 0; i < g->num_players; i++)
	{
		/* Assume player needs 12 cards to end game */
		target = 12;

		/* Check for "game ends at 14" flag */
		if (count_active_flags(g, i, FLAG_GAME_END_14)) target = 14;

		/* Check for 12 or more cards played */
		if (count_player_area(g, i, WHERE_ACTIVE) >= target)
		{
			/* Game is over */
			g->game_over = 1;
		}

		/* Copy actions to previous */
		g->p[i].prev_action[0] = g->p[i].action[0];
		g->p[i].prev_action[1] = g->p[i].action[1];
	}

	/* Increment round counter */
	g->round++;

	/* Check for too many rounds */
	if (g->round > 30) g->game_over = 1;

	/* Check for finished game */
	if (g->game_over) return 0;

	/* Continue game */
	return 1;
}

/*
 * Return non-specific military strength.
 */
int total_military(game *g, int who)
{
	power o_list[100], *o_ptr;
	int i, n, amt = 0;

	/* Get list of settle powers */
	n = get_powers(g, who, PHASE_SETTLE, o_list);

	/* Loop over powers */
	for (i = 0; i < n; i++)
	{
		/* Get power pointer */
		o_ptr = &o_list[i];

		/* Check for non-specific military */
		if (o_ptr->code == P3_EXTRA_MILITARY)
		{
			/* Add to military */
			amt += o_ptr->value;
		}

		/* Check for non-specific military per military world */
		if (o_ptr->code == (P3_EXTRA_MILITARY | P3_PER_MILITARY))
		{
			/* Add to military */
			amt += count_active_flags(g, who, FLAG_MILITARY);
		}
	}
	
	/* Return amount of military */
	return amt;
}

/*
 * Return true if bonus criteria matches given card design.
 */
static int bonus_match(vp_bonus *v_ptr, design *d_ptr)
{
	power *o_ptr;
	int i, type = v_ptr->type;

	/* Switch on bonus type */
	switch (type)
	{
		/* Production */
		case VP_NOVELTY_PRODUCTION:
		case VP_RARE_PRODUCTION:
		case VP_GENE_PRODUCTION:
		case VP_ALIEN_PRODUCTION:

			/* Check for non-good */
			if (!d_ptr->good_type) return 0;

			/* Check for windfall */
			if (d_ptr->flags & FLAG_WINDFALL) return 0;

			/* Check for correct type */
			return type == d_ptr->good_type +
			                   VP_NOVELTY_PRODUCTION - GOOD_NOVELTY;

		/* Windfall */
		case VP_NOVELTY_WINDFALL:
		case VP_RARE_WINDFALL:
		case VP_GENE_WINDFALL:
		case VP_ALIEN_WINDFALL:

			/* Check for non-good */
			if (!d_ptr->good_type) return 0;

			/* Check for non-windfall */
			if (!(d_ptr->flags & FLAG_WINDFALL)) return 0;

			/* Check for correct type */
			return type == d_ptr->good_type +
			                     VP_NOVELTY_WINDFALL - GOOD_NOVELTY;

		/* Explore powers */
		case VP_DEVEL_EXPLORE:
		case VP_WORLD_EXPLORE:

			/* Skip wrong type */
			if (d_ptr->type == TYPE_WORLD &&
			    type == VP_DEVEL_EXPLORE) return 0;
			if (d_ptr->type == TYPE_DEVELOPMENT &&
			    type == VP_WORLD_EXPLORE) return 0;

			/* Loop over powers */
			for (i = 0; i < d_ptr->num_power; i++)
			{
				/* Get power pointer */
				o_ptr = &d_ptr->powers[i];

				/* Check for explore power */
				if (o_ptr->phase == PHASE_EXPLORE) return 1;
			}

			/* No explore powers */
			return 0;

		/* Trade/Consume powers */
		case VP_DEVEL_TRADE:
		case VP_WORLD_TRADE:
		case VP_DEVEL_CONSUME:
		case VP_WORLD_CONSUME:

			/* Skip worlds with development bonuses */
			if (d_ptr->type == TYPE_WORLD &&
			    (type == VP_DEVEL_TRADE ||
			     type == VP_DEVEL_CONSUME))
			{
				/* No match */
				return 0;
			}

			/* Skip developments with world bonuses */
			if (d_ptr->type == TYPE_DEVELOPMENT &&
			    (type == VP_WORLD_TRADE ||
			     type == VP_WORLD_CONSUME))
			{
				/* No match */
				return 0;
			}

			/* Loop over powers */
			for (i = 0; i < d_ptr->num_power; i++)
			{
				/* Get power pointer */
				o_ptr = &d_ptr->powers[i];

				/* Skip non-consume/trade power */
				if (o_ptr->phase != PHASE_CONSUME) continue;

				/* Check for trade power */
				if ((o_ptr->code & P4_TRADE_MASK) &&
				    (type == VP_DEVEL_TRADE ||
				     type == VP_WORLD_TRADE)) return 1;

				/* Check for consume power */
				if (!(o_ptr->code & P4_TRADE_MASK) &&
				     (type == VP_DEVEL_CONSUME ||
				      type == VP_WORLD_CONSUME)) return 1;
			}

			/* No correct powers */
			return 0;

		/* Six-cost development */
		case VP_SIX_DEVEL:

			/* Check for non-development */
			if (d_ptr->type == TYPE_WORLD) return 0;

			/* Check for correct cost */
			return d_ptr->cost == 6;

		/* Development */
		case VP_DEVEL:

			/* Check for development */
			return d_ptr->type == TYPE_DEVELOPMENT;

		/* World */
		case VP_WORLD:

			/* Check for world */
			return d_ptr->type == TYPE_WORLD;

		/* Rebel flag */
		case VP_REBEL_FLAG:

			/* Check for flag */
			return d_ptr->flags & FLAG_REBEL;

		/* Alien flag */
		case VP_ALIEN_FLAG:

			/* Check for flag */
			return d_ptr->flags & FLAG_ALIEN;

		/* Terraforming flag */
		case VP_TERRAFORMING_FLAG:

			/* Check for flag */
			return d_ptr->flags & FLAG_TERRAFORMING;

		/* Uplift flag */
		case VP_UPLIFT_FLAG:

			/* Check for flag */
			return d_ptr->flags & FLAG_UPLIFT;

		/* Imperium flag */
		case VP_IMPERIUM_FLAG:

			/* Check for flag */
			return d_ptr->flags & FLAG_IMPERIUM;

		/* Chromosome flag */
		case VP_CHROMO_FLAG:

			/* Check for flag */
			return d_ptr->flags & FLAG_CHROMO;

		/* Military flag */
		case VP_MILITARY:

			/* Check for flag */
			return d_ptr->flags & FLAG_MILITARY;

		/* Rebel military world */
		case VP_REBEL_MILITARY:

			/* Check for non-military */
			if (!(d_ptr->flags & FLAG_MILITARY)) return 0;

			/* Check for Rebel flag */
			return d_ptr->flags & FLAG_REBEL;

		/* Specific name */
		case VP_NAME:

			/* Check for correct name */
			return !strcmp(v_ptr->name, d_ptr->name);
	}

	/* Other types never match */
	return 0;
}

/*
 * Add bonuses to score from given card.
 */
static void add_score_bonus(game *g, int who, int which)
{
	player *p_ptr;
	card *c_ptr, *score;
	vp_bonus *v_ptr;
	int i, j, count = 0, types[6];

	/* Get player pointer */
	p_ptr = &g->p[who];

	/* Get scoring card pointer */
	score = &g->deck[which];

	/* Loop over bonuses */
	for (i = 0; i < score->d_ptr->num_vp_bonus; i++)
	{
		/* Get VP bonus pointer */
		v_ptr = &score->d_ptr->bonuses[i];

		/* Check for simple bonuses */
		if (v_ptr->type == VP_THREE_VP)
		{
			/* Add bonus for VP chips */
			p_ptr->end_vp += p_ptr->vp / 3;
		}
		else if (v_ptr->type == VP_TOTAL_MILITARY)
		{
			/* Add bonus for military strength */
			p_ptr->end_vp += total_military(g, who);
		}
		else if (v_ptr->type == VP_KIND_GOOD)
		{
			/* Clear type flags */
			for (j = 0; j < 6; j++) types[j] = 0;

			/* Loop over active cards */
			for (j = 0; j < g->deck_size; j++)
			{
				/* Get card pointer */
				c_ptr = &g->deck[j];

				/* Skip unowned cards */
				if (c_ptr->owner != who) continue;

				/* Skip inactive cards */
				if (c_ptr->where != WHERE_ACTIVE) continue;

				/* Skip developments */
				if (c_ptr->d_ptr->type == TYPE_DEVELOPMENT)
					continue;

				/* Mark type */
				types[c_ptr->d_ptr->good_type] = 1;
			}

			/* Count types */
			for (j = GOOD_NOVELTY; j <= GOOD_ALIEN; j++)
			{
				/* Count type if it appears */
				if (types[j]) count++;
			}

			/* Award points based on number of types */
			switch (count)
			{
				case 1: p_ptr->end_vp += 1; break;
				case 2: p_ptr->end_vp += 3; break;
				case 3: p_ptr->end_vp += 6; break;
				case 4: p_ptr->end_vp += 10; break;
			}
		}
	}

	/* Loop over player's active cards */
	for (i = 0; i < g->deck_size; i++)
	{
		/* Get card pointer */
		c_ptr = &g->deck[i];

		/* Skip unowned cards */
		if (c_ptr->owner != who) continue;

		/* Skip inactive cards */
		if (c_ptr->where != WHERE_ACTIVE) continue;

		/* Loop over scoring card's bonuses */
		for (j = 0; j < score->d_ptr->num_vp_bonus; j++)
		{
			/* Get bonus pointer */
			v_ptr = &score->d_ptr->bonuses[j];

			/* Check for match against current power */
			if (bonus_match(v_ptr, c_ptr->d_ptr))
			{
				/* Add score */
				p_ptr->end_vp += v_ptr->point;

				/* Skip remaining bonuses */
				break;
			}
		}
	}
}

/*
 * Handle end-game scoring.
 */
void score_game(game *g)
{
	player *p_ptr;
	card *c_ptr;
	int count[MAX_PLAYER];
	int i, j;

	/* Loop over players */
	for (i = 0; i < g->num_players; i++)
	{
		/* Get player pointer */
		p_ptr = &g->p[i];

		/* Start with VP chips */
		p_ptr->end_vp = p_ptr->vp;

		/* Start with no goal points */
		p_ptr->goal_vp = 0;

		/* Loop over cards */
		for (j = 0; j < g->deck_size; j++)
		{
			/* Get card pointer */
			c_ptr = &g->deck[j];

			/* Skip unowned cards */
			if (c_ptr->owner != i) continue;

			/* Skip inactive cards */
			if (c_ptr->where != WHERE_ACTIVE) continue;

			/* Add points from card */
			p_ptr->end_vp += c_ptr->d_ptr->vp;

			/* Check for VP bonuses */
			if (c_ptr->d_ptr->num_vp_bonus)
			{
				/* Add in bonuses */
				add_score_bonus(g, i, j);
			}
		}
	}

	/* Loop over "first" goals */
	for (i = GOAL_FIRST_5_VP; i <= GOAL_FIRST_8_ACTIVE; i++)
	{
		/* Skip inactive goals */
		if (!g->goal_active[i]) continue;

		/* Loop over players */
		for (j = 0; j < g->num_players; j++)
		{
			/* Get player pointer */
			p_ptr = &g->p[j];

			/* Check for goal claimed */
			if (p_ptr->goal_claimed[i]) p_ptr->goal_vp += 3;
		}
	}

	/* Loop over "most" goals */
	for (i = GOAL_MOST_MILITARY; i <= GOAL_MOST_REBEL; i++)
	{
		/* Skip inactive goals */
		if (!g->goal_active[i]) continue;

		/* Loop over players */
		for (j = 0; j < g->num_players; j++)
		{
			/* Get progress toward goal */
			count[j] = g->p[j].goal_progress[i];

			/* Check for insufficient progress */
			if (count[j] < goal_minimum(i)) count[j] = 0;
		}

		/* Loop over players */
		for (j = 0; j < g->num_players; j++)
		{
			/* Get player pointer */
			p_ptr = &g->p[j];

			/* Skip players with no progress */
			if (!count[j]) continue;

			/* Check for goal claimed */
			if (p_ptr->goal_claimed[i])
			{
				/* Award most points */
				p_ptr->goal_vp += 5;
			}
			else
			{
				/* Check for as much as most */
				if (count[j] == g->goal_most[i])
				{
					/* Award tie points */
					p_ptr->goal_vp += 3;
				}
			}
		}
	}

	/* Loop over players */
	for (i = 0; i < g->num_players; i++)
	{
		/* Get player pointer */
		p_ptr = &g->p[i];

		/* Add goal points to end score */
		p_ptr->end_vp += p_ptr->goal_vp;
	}
}

/*
 * Declare winner.
 */
void declare_winner(game *g)
{
	player *p_ptr;
	int i, t, b_s = -1, b_t = -1;

	/* Score game */
	score_game(g);

	/* Loop over players */
	for (i = 0; i < g->num_players; i++)
	{
		/* Get player pointer */
		p_ptr = &g->p[i];

		/* Check for bigger score */
		if (p_ptr->end_vp > b_s) b_s = p_ptr->end_vp;
	}

	/* Loop over players */
	for (i = 0; i < g->num_players; i++)
	{
		/* Get player pointer */
		p_ptr = &g->p[i];

		/* Skip players who do not have best score */
		if (p_ptr->end_vp < b_s) continue;

		/* Get tiebreaker */
		t = count_player_area(g, i, WHERE_HAND) +
		    count_player_area(g, i, WHERE_GOOD);

		/* Track biggest tiebreaker */
		if (t > b_t) b_t = t;
	}

	/* Loop over players */
	for (i = 0; i < g->num_players; i++)
	{
		/* Get player pointer */
		p_ptr = &g->p[i];

		/* Skip players who do not have best score */
		if (p_ptr->end_vp < b_s) continue;

		/* Get tiebreaker */
		t = count_player_area(g, i, WHERE_HAND) +
		    count_player_area(g, i, WHERE_GOOD);

		/* Skip players who do not have best tiebreaker */
		if (t < b_t) continue;

		/* Set winner flag */
		p_ptr->winner = 1;
	}
}
