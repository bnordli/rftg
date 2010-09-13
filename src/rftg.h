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

/*
 * Standard headers.
 */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

/*
 * Default data directory if not otherwise specified.
 */
#ifndef DATADIR
# define DATADIR "."
#endif

/*
 * Maximum number of players.
 */
#define MAX_PLAYER 6

/*
 * Maximum number of expansion levels.
 */
#define MAX_EXPANSION 3

/*
 * Number of card designs.
 */
#define MAX_DESIGN 151

/*
 * Number of cards in the deck.
 */
#define MAX_DECK 180

/*
 * Number of powers per card.
 */
#define MAX_POWER 5

/*
 * Number of special VP bonuses per card.
 */
#define MAX_VP_BONUS 5

/*
 * Maximum number of pending takeovers.
 */
#define MAX_TAKEOVER 12

/*
 * Number of intermediate goals.
 */
#define MAX_GOAL 15

/*
 * Number of turn phases.
 */
#define MAX_PHASE 7

/*
 * Number of available actions.
 */
#define MAX_ACTION 9


/*
 * Round phases.
 */
#define PHASE_ACTION   0
#define PHASE_EXPLORE  1
#define PHASE_DEVELOP  2
#define PHASE_SETTLE   3
#define PHASE_CONSUME  4
#define PHASE_PRODUCE  5
#define PHASE_DISCARD  6

/*
 * Player action choices.
 */
#define ACT_EXPLORE_5_0    0
#define ACT_EXPLORE_1_1    1
#define ACT_DEVELOP        2
#define ACT_DEVELOP2       3
#define ACT_SETTLE         4
#define ACT_SETTLE2        5
#define ACT_CONSUME_TRADE  6
#define ACT_CONSUME_X2     7
#define ACT_PRODUCE        8

/*
 * Card types.
 */
#define TYPE_WORLD        1
#define TYPE_DEVELOPMENT  2

/*
 * Card flags.
 */
#define FLAG_MILITARY      0x1
#define FLAG_WINDFALL      0x2
#define FLAG_START         0x4

#define FLAG_START_RED     0x8
#define FLAG_START_BLUE    0x10

#define FLAG_REBEL         0x20
#define FLAG_UPLIFT        0x40
#define FLAG_ALIEN         0x80
#define FLAG_TERRAFORMING  0x100
#define FLAG_IMPERIUM      0x200
#define FLAG_CHROMO        0x400

#define FLAG_STARTHAND_3   0x800
#define FLAG_DISCARD_TO_12 0x1000
#define FLAG_GAME_END_14   0x2000


/*
 * Good types (and cost).
 */
#define GOOD_NOVELTY  2
#define GOOD_RARE     3
#define GOOD_GENE     4
#define GOOD_ALIEN    5

/*
 * Card locations.
 */
#define WHERE_DECK     0
#define WHERE_DISCARD  1
#define WHERE_HAND     2
#define WHERE_ACTIVE   3
#define WHERE_GOOD     4

/*
 * Card power effects by phase.
 */

/* Phase one -- Explore */
#define P1_DRAW         0x1
#define P1_KEEP         0x2

#define P1_DISCARD_ANY  0x4


/* Phase two -- Develop */
#define P2_DRAW               0x1
#define P2_REDUCE             0x2
#define P2_DRAW_AFTER         0x4

#define P2_DISCARD_REDUCE     0x8


/* Phase three -- Settle */
#define P3_REDUCE             0x1

#define P3_NOVELTY            0x2
#define P3_RARE               0x4
#define P3_GENE               0x8
#define P3_ALIEN              0x10

#define P3_DISCARD_ZERO       0x20
#define P3_DISCARD_MILITARY   0x40
#define P3_MILITARY_HAND      0x80

#define P3_EXTRA_MILITARY     0x100
#define P3_AGAINST_REBEL      0x200
#define P3_PER_MILITARY       0x400

#define P3_PAY_MILITARY       0x800
#define P3_PAY_DISCOUNT       0x1000

#define P3_CONQUER_SETTLE     0x2000

#define P3_DRAW_AFTER         0x4000

#define P3_PLACE_TWO          0x8000

#define P3_TAKEOVER_REBEL     0x10000
#define P3_TAKEOVER_IMPERIUM  0x20000
#define P3_TAKEOVER_MILITARY  0x40000

#define P3_TAKEOVER_DEFENSE   0x80000


/* Phase four -- Consume */
#define P4_TRADE_ANY          0x1
#define P4_TRADE_NOVELTY      0x2
#define P4_TRADE_RARE         0x4
#define P4_TRADE_GENE         0x8
#define P4_TRADE_ALIEN        0x10
#define P4_TRADE_THIS         0x20
#define P4_TRADE_BONUS_CHROMO 0x40

#define P4_TRADE_ACTION       0x80
#define P4_TRADE_NO_BONUS     0x100

#define P4_CONSUME_ANY        0x200
#define P4_CONSUME_NOVELTY    0x400
#define P4_CONSUME_RARE       0x800
#define P4_CONSUME_GENE       0x1000
#define P4_CONSUME_ALIEN      0x2000

#define P4_CONSUME_THIS       0x4000

#define P4_CONSUME_TWO        0x8000

#define P4_CONSUME_3_DIFF     0x10000
#define P4_CONSUME_N_DIFF     0x20000
#define P4_CONSUME_ALL        0x40000

#define P4_GET_CARD           0x80000
#define P4_GET_2_CARD         0x100000
#define P4_GET_VP             0x200000

#define P4_DRAW               0x400000
#define P4_DRAW_LUCKY         0x800000
#define P4_DISCARD_HAND       0x1000000
#define P4_ANTE_CARD          0x2000000
#define P4_VP                 0x4000000

#define P4_RECYCLE            0x8000000

/* Mask of trade powers */
#define P4_TRADE_MASK (P4_TRADE_ANY | P4_TRADE_NOVELTY | P4_TRADE_RARE | \
                       P4_TRADE_GENE | P4_TRADE_ALIEN | P4_TRADE_THIS | \
                       P4_TRADE_BONUS_CHROMO)


/* Phase five -- Produce */
#define P5_PRODUCE            0x1
#define P5_DISCARD_PRODUCE    0x2

#define P5_WINDFALL_ANY       0x4
#define P5_WINDFALL_NOVELTY   0x8
#define P5_WINDFALL_RARE      0x10
#define P5_WINDFALL_GENE      0x20
#define P5_WINDFALL_ALIEN     0x40
#define P5_NOT_THIS           0x80

#define P5_DRAW               0x100
#define P5_DRAW_IF            0x200

#define P5_DRAW_EACH_NOVELTY  0x400
#define P5_DRAW_EACH_RARE     0x800
#define P5_DRAW_EACH_GENE     0x1000
#define P5_DRAW_EACH_ALIEN    0x2000

#define P5_DRAW_WORLD_GENE    0x4000
#define P5_DRAW_MOST_RARE     0x8000
#define P5_DRAW_DIFFERENT     0x10000

#define P5_DRAW_MILITARY      0x20000
#define P5_DRAW_REBEL         0x40000
#define P5_DRAW_CHROMO        0x80000


/*
 * Special victory point flags.
 */
#define VP_NOVELTY_PRODUCTION   0
#define VP_RARE_PRODUCTION      1
#define VP_GENE_PRODUCTION      2
#define VP_ALIEN_PRODUCTION     3

#define VP_NOVELTY_WINDFALL     4
#define VP_RARE_WINDFALL        5
#define VP_GENE_WINDFALL        6
#define VP_ALIEN_WINDFALL       7

#define VP_DEVEL_EXPLORE        8
#define VP_WORLD_EXPLORE        9
#define VP_DEVEL_TRADE          10
#define VP_WORLD_TRADE          11
#define VP_DEVEL_CONSUME        12
#define VP_WORLD_CONSUME        13

#define VP_SIX_DEVEL            14
#define VP_DEVEL                15
#define VP_WORLD                16

#define VP_REBEL_FLAG           17
#define VP_ALIEN_FLAG           18
#define VP_TERRAFORMING_FLAG    19
#define VP_UPLIFT_FLAG          20
#define VP_IMPERIUM_FLAG        21
#define VP_CHROMO_FLAG          22

#define VP_MILITARY             23
#define VP_TOTAL_MILITARY       24

#define VP_REBEL_MILITARY       25

#define VP_THREE_VP             26
#define VP_KIND_GOOD            27

#define VP_NAME                 28


/*
 * The intermediate goal tiles.
 */
#define GOAL_FIRST_5_VP         0
#define GOAL_FIRST_4_TYPES      1
#define GOAL_FIRST_3_ALIEN      2
#define GOAL_FIRST_DISCARD      3
#define GOAL_FIRST_PHASE_POWER  4
#define GOAL_FIRST_SIX_DEVEL    5

#define GOAL_FIRST_3_UPLIFT     6
#define GOAL_FIRST_4_GOODS      7
#define GOAL_FIRST_8_ACTIVE     8

#define GOAL_MOST_MILITARY      9
#define GOAL_MOST_BLUE_BROWN    10
#define GOAL_MOST_DEVEL         11
#define GOAL_MOST_PRODUCTION    12

#define GOAL_MOST_EXPLORE       13
#define GOAL_MOST_REBEL         14


/*
 * Forward declaration.
 */
struct game;

/*
 * A card's special power.
 *
 * A card may have many of these.
 */
typedef struct power
{
	/* Phase power is used in */
	int phase;

	/* Power's effect code */
	int code;

	/* Power's value */
	int value;

	/* Number of times power may be used */
	int times;

} power;

/*
 * A card's special VP bonus.
 */
typedef struct vp_bonus
{
	/* Points */
	int point;

	/* Type */
	int type;

	/* String for "name" type */
	char *name;

} vp_bonus;

/*
 * Information about a card design.
 */
typedef struct design
{
	/* Name */
	char *name;

	/* Design index */
	int index;

	/* Type (development or world) */
	int type;

	/* Cost to play */
	int cost;

	/* Victory points given */
	int vp;

	/* Number of cards in each expansion deck */
	int expand[MAX_EXPANSION];

	/* Type of good produced (if any) */
	int good_type;

	/* Flags (military, windfall, alien, rebel, start world, etc) */
	int flags;

	/* Number of this design in the deck */
	int dup;

	/* Number of card powers */
	int num_power;

	/* List of powers */
	power powers[MAX_POWER];

	/* Number of vp bonuses */
	int num_vp_bonus;

	/* List of VP bonuses */
	vp_bonus bonuses[MAX_VP_BONUS];

} design;

/*
 * Information about an instance of a card.
 */
typedef struct card
{
	/* Card's owner (if any) */
	int owner;

	/* Card's location */
	int where;

	/* Card is one just drawn (explore) and may be discarded */
	int temp;

	/* Bitmask of players who know card's location */
	int known;

	/* Card powers which have been used */
	int used[MAX_POWER];

	/* Card has produced this phase */
	int produced;

	/* Card design */
	design *d_ptr;

	/* Card is face down as a good */
	int good;

	/* Card covering us (as a good) */
	int covered;

	/* Order played on the table */
	int order;

} card;

/*
 * Function type for choose callback.
 */
typedef int (*choose_result)(struct game *, int, int *, int);

/*
 * Collection of function pointers for a player's decisions.
 */
typedef struct interface
{
	/* Initialize */
	void (*init)(struct game *g, int who, double factor);

	/* Player spots have been rotated */
	void (*notify_rotation)(struct game *g);

	/* Choose a start world */
	int (*choose_start)(struct game *g, int who, int list[], int num);

	/* Choose action */
	void (*choose_action)(struct game *g, int who, int action[2]);

	/* React to action choice */
	void (*react_action)(struct game *g, int who, int action[2]);

	/* Choose cards to discard */
	void (*choose_discard)(struct game *g, int who, int list[], int num,
	                       int discard);

	/* Take sample cards into hand from Explore phase */
	void (*explore_sample)(struct game *g, int who, int draw, int keep,
	                       int discard_any);

	/* Choose card to place */
	int (*choose_place)(struct game *g, int who, int list[], int num,
	                    int phase);

	/* Choose method of payment */
	void (*choose_pay)(struct game *g, int who, int which, int list[],
	                   int num, int special[], int num_special);

	/* Choose world to takeover */
	int (*choose_takeover)(struct game *g, int who, int list[], int num,
	                       int special[], int num_special);

	/* Choose method of takeover defense */
	void (*choose_defend)(struct game *g, int who, int which, int opponent,
	                      int deficit, int list[], int num,
	                      int special[], int num_special);

	/* Choose good to trade */
	void (*choose_trade)(struct game *g, int who, int list[], int num,
	                     int no_bonus);

	/* Choose consume power to use */
	void (*choose_consume)(struct game *g, int who, power olist[], int num);

	/* Choose cards from hand to consume */
	void (*choose_consume_hand)(struct game *g, int who, power *o_ptr,
	                            int list[], int num);

	/* Choose goods to consume */
	void (*choose_good)(struct game *g, int who, power *o_ptr, int goods[],
	                    int num, int min, int max);

	/* Choose lucky number */
	int (*choose_lucky)(struct game *g, int who);

	/* Choose card to ante while gambling */
	int (*choose_ante)(struct game *g, int who, int list[], int num);

	/* Choose card to keep after gambling successfully */
	int (*choose_keep)(struct game *g, int who, int list[], int num);

	/* Choose windfall world to produce on */
	void (*choose_windfall)(struct game *g, int who, int list[], int num);

	/* Choose produce power to use */
	void (*choose_produce)(struct game *g, int who, power olist[], int num);

	/* Choose card to discard when needed to produce */
	void (*choose_discard_produce)(struct game *g, int who, int world,
	                               int list[], int num);

	/* Game over */
	void (*game_over)(struct game *g, int who);

	/* Shutdown */
	void (*shutdown)(struct game *g, int who);

} interface;

/*
 * Information about a player.
 */
typedef struct player
{
	/* Player's name/color */
	char *name;

	/* Ask player to make decisions */
	interface *control;

	/* Action(s) chosen */
	int action[2];

	/* Previous turn action(s) */
	int prev_action[2];

	/* Player has used phase bonus */
	int phase_bonus_used;

	/* Player's start world */
	int start;

	/* Card chosen in Develop or Settle phase */
	int placing;

	/* Bonus military accrued so far this phase */
	int bonus_military;

	/* Number of cards discarded at end of turn */
	int end_discard;

	/* Goal cards claimed */
	int goal_claimed[MAX_GOAL];

	/* Progress toward each goal */
	int goal_progress[MAX_GOAL];

	/* Victory point chips */
	int vp;

	/* Victory points from goals */
	int goal_vp;

	/* Total victory points (if game ended now) */
	int end_vp;

	/* Player is the winner */
	int winner;

	/* Number of "fake" drawn cards in simulated games */
	int fake_hand;

	/* Total number of "fake" cards seen this turn */
	int total_fake;

	/* Number of "fake" developments played this turn */
	int fake_played_dev;

	/* Number of "fake" worlds played this turn */
	int fake_played_world;

	/* Number of cards discarded this turn but not removed from hand */
	int fake_discards;

	/* Counter for cards played */
	int table_order;

	/* Cards and VP earned during the current phase */
	int phase_cards;
	int phase_vp;

} player;

/*
 * Information about a game.
 */
typedef struct game
{
	/* Current random seed */
	unsigned int random_seed;

	/* Random seed at start of game */
	unsigned int start_seed;

	/* Game is a simulation */
	int simulation;

	/* Who initiated the simulation */
	int sim_who;

	/* Players */
	player p[MAX_PLAYER];

	/* Number of players */
	int num_players;

	/* This is an "advanced" 2 player game */
	int advanced;

	/* Number of expansions in use */
	int expanded;

	/* Disable goals in expanded games */
	int goal_disabled;

	/* Disable takeovers in second (or later) expansion */
	int takeover_disabled;

	/* Size of deck in use */
	int deck_size;

	/* Information about each card */
	card deck[MAX_DECK];

	/* Victory points remaining in the pool */
	int vp_pool;

	/* Goals active in this game */
	int goal_active[MAX_GOAL];

	/* Goals yet unclaimed */
	int goal_avail[MAX_GOAL];

	/* Maximum progress toward a "most" goal */
	int goal_most[MAX_GOAL];

	/* Number of pending takeovers */
	int num_takeover;

	/* Worlds targeted for takeover */
	int takeover_target[MAX_TAKEOVER];

	/* Player attempting each takeover */
	int takeover_who[MAX_TAKEOVER];

	/* Card holding takeover power */
	int takeover_power[MAX_TAKEOVER];

	/* Actions selected this round */
	int action_selected[MAX_ACTION];

	/* Current action */
	int cur_action;

	/* Current player in phase */
	int turn;

	/* Current round number */
	int round;

	/* Game is over */
	int game_over;

} game;


/*
 * External variables.
 */
extern design library[MAX_DESIGN];
extern char *action_name[MAX_ACTION];
extern char *goal_name[MAX_GOAL];
extern interface ai_func;
extern interface gui_func;

/*
 * External functions.
 */
extern void message_add(char *msg);
extern void read_cards(void);
extern void init_game(game *g);
extern int myrand(unsigned int *seed);
extern int count_player_area(game *g, int who, int where);
extern int count_active_flags(game *g, int who, int flags);
extern int player_chose(game *g, int who, int act);
extern card *random_draw(game *g);
extern void draw_card(game *g, int who);
extern void draw_cards(game *g, int who, int num);
extern void clear_temp(game *g);
extern void discard_callback(game *g, int who, int list[], int num);
extern void discard_to(game *g, int who, int to, int discard_any);
extern void add_good(game *g, card *c_ptr);
extern void phase_explore(game *g);
extern void place_card(game *g, int who, int which);
extern void develop_action(game *g, int who, int placing);
extern void phase_develop(game *g);
extern int payment_callback(game *g, int who, int which, int list[], int num,
                            int special[], int num_special);
extern int takeover_callback(game *g, int special, int world);
extern int settle_check_takeover(game *g, int who);
extern void settle_action(game *g, int who, int world);
extern int defend_callback(game *g, int who, int deficit, int list[], int num,
                           int special[], int num_special);
extern void resolve_takeovers(game *g);
extern void phase_settle(game *g);
extern void trade_chosen(game *g, int who, int which, int no_bonus);
extern void trade_action(game *g, int who, int no_bonus);
extern int good_chosen(game *g, int who, power *o_ptr, int g_list[], int num);
extern int consume_hand_chosen(game *g, int who, power *o_ptr, int list[],
                               int n);
extern void consume_chosen(game *g, int who, power *o_ptr);
extern int consume_action(game *g, int who);
extern void phase_consume(game *g);
extern void produce_world(game *g, int who, int which);
extern void discard_produce_chosen(game *g, int who, int world, int discard);
extern void produce_chosen(game *g, int who, power *o_ptr);
extern int produce_action(game *g, int who);
extern void phase_produce_end(game *g);
extern void phase_produce(game *g);
extern void phase_discard(game *g);
extern int goal_minimum(int goal);
extern void check_goals(game *g);
extern int total_military(game *g, int who);
extern void score_game(game *g);
extern int game_round(game *g);
extern void declare_winner(game *g);

extern void ai_debug(game *g, double win_prob[MAX_PLAYER][MAX_PLAYER],
                              double *role[], double *action_score[],
                              int *num_action);

extern int load_game(game *g, char *filename);
extern int save_game(game *g, char *filename);
