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
#include <stdint.h>
#include <ctype.h>
#include <string.h>
#include <time.h>

/*
 * Default data directory if not otherwise specified.
 */
#ifndef RFTGDIR
# define RFTGDIR "."
#endif

/*
 * Maximum number of players.
 */
#define MAX_PLAYER 6

/*
 * Maximum number of expansion levels.
 */
#define MAX_EXPANSION 4

/*
 * Number of card designs.
 */
#define MAX_DESIGN 191

/*
 * Number of cards in the deck.
 */
#define MAX_DECK 228

/*
 * Number of powers per card.
 */
#define MAX_POWER 5

/*
 * Number of special VP bonuses per card.
 */
#define MAX_VP_BONUS 6

/*
 * Maximum number of pending takeovers.
 */
#define MAX_TAKEOVER 12

/*
 * Number of intermediate goals.
 */
#define MAX_GOAL 20

/*
 * Number of turn phases.
 */
#define MAX_PHASE 7

/*
 * Number of available actions.
 */
#define MAX_ACTION 10

/*
 * Number of Search categories.
 */
#define MAX_SEARCH 9


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
#define ACT_SEARCH         0
#define ACT_EXPLORE_5_0    1
#define ACT_EXPLORE_1_1    2
#define ACT_DEVELOP        3
#define ACT_DEVELOP2       4
#define ACT_SETTLE         5
#define ACT_SETTLE2        6
#define ACT_CONSUME_TRADE  7
#define ACT_CONSUME_X2     8
#define ACT_PRODUCE        9

#define ACT_MASK           0x7f
#define ACT_PRESTIGE       0x80

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

#define FLAG_PRESTIGE      0x800

#define FLAG_STARTHAND_3   0x1000
#define FLAG_START_SAVE    0x2000
#define FLAG_DISCARD_TO_12 0x4000
#define FLAG_GAME_END_14   0x8000
#define FLAG_TAKE_DISCARDS 0x10000
#define FLAG_SELECT_LAST   0x20000


/*
 * Good types (and cost).
 */
#define GOOD_ANY      1
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
#define WHERE_SAVED    5
#define WHERE_ASIDE    6

/*
 * Search categories.
 */
#define SEARCH_DEV_MILITARY       0
#define SEARCH_MILITARY_WINDFALL  1
#define SEARCH_PEACEFUL_WINDFALL  2
#define SEARCH_CHROMO_WORLD       3
#define SEARCH_ALIEN_WORLD        4
#define SEARCH_CONSUME_TWO        5
#define SEARCH_MILITARY_5         6
#define SEARCH_6_DEV              7
#define SEARCH_TAKEOVER           8


/*
 * Card power effects by phase.
 */

/* Phase one -- Explore */
#define P1_DRAW               (1ULL << 0)
#define P1_KEEP               (1ULL << 1)

#define P1_DISCARD_ANY        (1ULL << 2)

#define P1_DISCARD_PRESTIGE   (1ULL << 3)


/* Phase two -- Develop */
#define P2_DRAW               (1ULL << 0)
#define P2_REDUCE             (1ULL << 1)

#define P2_DRAW_AFTER         (1ULL << 2)
#define P2_EXPLORE            (1ULL << 3)

#define P2_DISCARD_REDUCE     (1ULL << 4)
#define P2_SAVE_COST          (1ULL << 5)

#define P2_PRESTIGE           (1ULL << 6)
#define P2_PRESTIGE_REBEL     (1ULL << 7)
#define P2_PRESTIGE_SIX       (1ULL << 8)

#define P2_CONSUME_RARE       (1ULL << 9)


/* Phase three -- Settle */
#define P3_REDUCE             (1ULL << 0)

#define P3_NOVELTY            (1ULL << 1)
#define P3_RARE               (1ULL << 2)
#define P3_GENE               (1ULL << 3)
#define P3_ALIEN              (1ULL << 4)

#define P3_DISCARD            (1ULL << 5)

#define P3_REDUCE_ZERO        (1ULL << 6)
#define P3_MILITARY_HAND      (1ULL << 7)

#define P3_EXTRA_MILITARY     (1ULL << 8)
#define P3_AGAINST_REBEL      (1ULL << 9)
#define P3_AGAINST_CHROMO     (1ULL << 10)
#define P3_PER_MILITARY       (1ULL << 11)
#define P3_PER_CHROMO         (1ULL << 12)
#define P3_IF_IMPERIUM        (1ULL << 13)

#define P3_PAY_MILITARY       (1ULL << 14)
#define P3_PAY_DISCOUNT       (1ULL << 15)
#define P3_PAY_PRESTIGE       (1ULL << 16)

#define P3_CONQUER_SETTLE     (1ULL << 17)
#define P3_NO_TAKEOVER        (1ULL << 18)

#define P3_DRAW_AFTER         (1ULL << 19)
#define P3_EXPLORE_AFTER      (1ULL << 20)
#define P3_PRESTIGE           (1ULL << 21)
#define P3_PRESTIGE_REBEL     (1ULL << 22)
#define P3_SAVE_COST          (1ULL << 23)

#define P3_PLACE_TWO          (1ULL << 24)
#define P3_PLACE_MILITARY     (1ULL << 25)

#define P3_CONSUME_RARE       (1ULL << 26)
#define P3_CONSUME_GENE       (1ULL << 27)
#define P3_CONSUME_PRESTIGE   (1ULL << 28)

#define P3_AUTO_PRODUCE       (1ULL << 29)
#define P3_PRODUCE_PRESTIGE   (1ULL << 30)

#define P3_TAKEOVER_REBEL     (1ULL << 31)
#define P3_TAKEOVER_IMPERIUM  (1ULL << 32)
#define P3_TAKEOVER_MILITARY  (1ULL << 33)
#define P3_TAKEOVER_PRESTIGE  (1ULL << 34)

#define P3_DESTROY            (1ULL << 35)

#define P3_TAKEOVER_DEFENSE   (1ULL << 36)
#define P3_PREVENT_TAKEOVER   (1ULL << 37)

#define P3_UPGRADE_WORLD      (1ULL << 38)

/* Mask of takeover powers */
#define P3_TAKEOVER_MASK (P3_TAKEOVER_REBEL | P3_TAKEOVER_IMPERIUM | \
                          P3_TAKEOVER_MILITARY | P3_PRESTIGE_TAKEOVER)

/* Phase four -- Consume */
#define P4_TRADE_ANY          (1ULL << 0)
#define P4_TRADE_NOVELTY      (1ULL << 1)
#define P4_TRADE_RARE         (1ULL << 2)
#define P4_TRADE_GENE         (1ULL << 3)
#define P4_TRADE_ALIEN        (1ULL << 4)
#define P4_TRADE_THIS         (1ULL << 5)
#define P4_TRADE_BONUS_CHROMO (1ULL << 6)

#define P4_NO_TRADE           (1ULL << 7)

#define P4_TRADE_ACTION       (1ULL << 8)
#define P4_TRADE_NO_BONUS     (1ULL << 9)

#define P4_CONSUME_ANY        (1ULL << 10)
#define P4_CONSUME_NOVELTY    (1ULL << 11)
#define P4_CONSUME_RARE       (1ULL << 12)
#define P4_CONSUME_GENE       (1ULL << 13)
#define P4_CONSUME_ALIEN      (1ULL << 14)

#define P4_CONSUME_THIS       (1ULL << 15)

#define P4_CONSUME_TWO        (1ULL << 16)

#define P4_CONSUME_3_DIFF     (1ULL << 17)
#define P4_CONSUME_N_DIFF     (1ULL << 18)
#define P4_CONSUME_ALL        (1ULL << 19)

#define P4_CONSUME_PRESTIGE   (1ULL << 20)

#define P4_GET_CARD           (1ULL << 21)
#define P4_GET_2_CARD         (1ULL << 22)
#define P4_GET_VP             (1ULL << 23)
#define P4_GET_PRESTIGE       (1ULL << 24)

#define P4_DRAW               (1ULL << 25)
#define P4_DRAW_LUCKY         (1ULL << 26)
#define P4_DISCARD_HAND       (1ULL << 27)
#define P4_ANTE_CARD          (1ULL << 28)
#define P4_VP                 (1ULL << 29)

#define P4_RECYCLE            (1ULL << 30)

/* Mask of trade powers */
#define P4_TRADE_MASK (P4_TRADE_ANY | P4_TRADE_NOVELTY | P4_TRADE_RARE | \
                       P4_TRADE_GENE | P4_TRADE_ALIEN | P4_TRADE_THIS | \
                       P4_TRADE_BONUS_CHROMO | P4_NO_TRADE)


/* Phase five -- Produce */
#define P5_PRODUCE              (1ULL << 0)

#define P5_WINDFALL_ANY         (1ULL << 1)
#define P5_WINDFALL_NOVELTY     (1ULL << 2)
#define P5_WINDFALL_RARE        (1ULL << 3)
#define P5_WINDFALL_GENE        (1ULL << 4)
#define P5_WINDFALL_ALIEN       (1ULL << 5)
#define P5_NOT_THIS             (1ULL << 6)
#define P5_DISCARD              (1ULL << 7)

#define P5_DRAW                 (1ULL << 8)
#define P5_DRAW_IF              (1ULL << 9)
#define P5_PRESTIGE_IF          (1ULL << 10)

#define P5_DRAW_EACH_NOVELTY    (1ULL << 11)
#define P5_DRAW_EACH_RARE       (1ULL << 12)
#define P5_DRAW_EACH_GENE       (1ULL << 13)
#define P5_DRAW_EACH_ALIEN      (1ULL << 14)

#define P5_DRAW_WORLD_GENE      (1ULL << 15)
#define P5_DRAW_MOST_RARE       (1ULL << 16)
#define P5_DRAW_MOST_PRODUCED   (1ULL << 17)
#define P5_DRAW_DIFFERENT       (1ULL << 18)

#define P5_PRESTIGE_MOST_CHROMO (1ULL << 19)

#define P5_DRAW_MILITARY        (1ULL << 20)
#define P5_DRAW_REBEL           (1ULL << 21)
#define P5_DRAW_CHROMO          (1ULL << 22)

#define P5_TAKE_SAVED           (1ULL << 23)


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
#define VP_NEGATIVE_MILITARY    25

#define VP_REBEL_MILITARY       26

#define VP_THREE_VP             27
#define VP_KIND_GOOD            28
#define VP_PRESTIGE             29

#define VP_NAME                 30


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

#define GOAL_FIRST_NEG_MILITARY 9
#define GOAL_FIRST_2_PRESTIGE   10
#define GOAL_FIRST_4_MILITARY   11

#define GOAL_MOST_MILITARY      12
#define GOAL_MOST_BLUE_BROWN    13
#define GOAL_MOST_DEVEL         14
#define GOAL_MOST_PRODUCTION    15

#define GOAL_MOST_EXPLORE       16
#define GOAL_MOST_REBEL         17

#define GOAL_MOST_PRESTIGE      18
#define GOAL_MOST_CONSUME       19

/*
 * Choice types we can send to players.
 */
#define CHOICE_ACTION           0
#define CHOICE_START            1
#define CHOICE_DISCARD          2
#define CHOICE_SAVE             3
#define CHOICE_DISCARD_PRESTIGE 4
#define CHOICE_PLACE            5
#define CHOICE_PAYMENT          6
#define CHOICE_SETTLE           7
#define CHOICE_TAKEOVER         8
#define CHOICE_DEFEND           9
#define CHOICE_TAKEOVER_PREVENT 10
#define CHOICE_UPGRADE          11
#define CHOICE_TRADE            12
#define CHOICE_CONSUME          13
#define CHOICE_CONSUME_HAND     14
#define CHOICE_GOOD             15
#define CHOICE_LUCKY            16
#define CHOICE_ANTE             17
#define CHOICE_KEEP             18
#define CHOICE_WINDFALL         19
#define CHOICE_PRODUCE          20
#define CHOICE_DISCARD_PRODUCE  21
#define CHOICE_SEARCH_TYPE      22
#define CHOICE_SEARCH_KEEP      23
#define CHOICE_OORT_KIND        24


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
	uint64_t code;

	/* Power's value */
	int value;

	/* Number of times power may be used */
	int times;

} power;

/*
 * Location of a power.
 */
typedef struct power_where
{
	/* Card index */
	int c_idx;

	/* Power index */
	int o_idx;

	/* Pointer to power */
	power *o_ptr;

} power_where;

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

	/* Card's owner at start of phase */
	int start_owner;

	/* Card's location at start of phase */
	int start_where;

	/* Card is one just drawn or placed */
	int temp;

	/* Card is being placed and is not yet paid for */
	int unpaid;

	/* Bitmask of players who know card's location */
	int known;

	/* Card powers which have been used */
	int used[MAX_POWER];

	/* Card has produced this phase */
	int produced;

	/* Card design */
	design *d_ptr;

	/* Card covering us (as a good) */
	int covered;

	/* Order played on the table */
	int order;

} card;

/*
 * Collection of function pointers for a player's decisions.
 */
typedef struct decisions
{
	/* Initialize */
	void (*init)(struct game *g, int who, double factor);

	/* Player spots have been rotated */
	void (*notify_rotation)(struct game *g, int who);

	/* Make a choice among those given */
	void (*make_choice)(struct game *g, int who, int type, int list[],
	                    int *nl, int special[], int *ns, int arg1,
	                    int arg2, int arg3);

	/* Take sample cards into hand from Explore phase */
	void (*explore_sample)(struct game *g, int who, int draw, int keep,
	                       int discard_any);

	/* Game over */
	void (*game_over)(struct game *g, int who);

	/* Shutdown */
	void (*shutdown)(struct game *g, int who);

} decisions;

/*
 * Information about a player.
 */
typedef struct player
{
	/* Player's name/color */
	char *name;

	/* Ask player to make decisions */
	decisions *control;

	/* Action(s) chosen */
	int action[2];

	/* Previous turn action(s) */
	int prev_action[2];

	/* Player has used prestige/search action */
	int prestige_action_used;

	/* Player has used phase bonus */
	int phase_bonus_used;

	/* Player's start world */
	int start;

	/* Card chosen in Develop or Settle phase */
	int placing;

	/* Bonus military accrued so far this phase */
	int bonus_military;

	/* Bonus settle discount accrued so far this phase */
	int bonus_reduce;

	/* Number of cards discarded at end of turn */
	int end_discard;

	/* Goal cards claimed */
	int goal_claimed[MAX_GOAL];

	/* Progress toward each goal */
	int goal_progress[MAX_GOAL];

	/* Prestige */
	int prestige;

	/* Prestige earned this turn */
	int prestige_turn;

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

	/* Cards, VP, and prestige earned during the current phase */
	int phase_cards;
	int phase_vp;
	int phase_prestige;

	/* Log of player's choices */
	int *choice_log;

	/* Size and current position of choice log */
	int choice_size;
	int choice_pos;

	/* History of log sizes */
	int *choice_history;

} player;

/*
 * Information about a game.
 */
typedef struct game
{
	/* Session ID in online server */
	int session_id;

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

	/* Takeover marked for failure */
	int takeover_defeated[MAX_TAKEOVER];

	/* XXX Current kind of "any" good world */
	int oort_kind;

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
extern char *goal_name[MAX_GOAL];
extern char *search_name[MAX_SEARCH];
extern decisions ai_func;
extern decisions gui_func;

/*
 * External functions.
 */
extern void message_add(game *g, char *msg);
extern void wait_for_answer(game *g, int who);
extern void prepare_phase(game *g, int who, int phase, int arg);
extern int game_rand(game *g);
extern void read_cards(void);
extern void init_game(game *g);
extern int simple_rand(unsigned int *seed);
extern int count_player_area(game *g, int who, int where);
extern int count_active_flags(game *g, int who, int flags);
extern int player_chose(game *g, int who, int act);
extern card *random_draw(game *g);
extern void draw_card(game *g, int who);
extern void draw_cards(game *g, int who, int num);
extern void start_prestige(game *g);
extern void clear_temp(game *g);
extern void discard_callback(game *g, int who, int list[], int num);
extern void discard_to(game *g, int who, int to, int discard_any);
extern void add_good(game *g, card *c_ptr);
extern int search_match(game *g, int which, int category);
extern void phase_search(game *g);
extern void phase_explore(game *g);
extern void place_card(game *g, int who, int which);
extern void develop_action(game *g, int who, int placing);
extern void phase_develop(game *g);
extern int payment_callback(game *g, int who, int which, int list[], int num,
                            int special[], int num_special, int mil_only);
extern int takeover_callback(game *g, int special, int world);
extern int settle_check_takeover(game *g, int who);
extern int upgrade_chosen(game *g, int who, int replacement, int old);
extern void settle_action(game *g, int who, int world);
extern int defend_callback(game *g, int who, int deficit, int list[], int num,
                           int special[], int num_special);
extern void resolve_takeovers(game *g);
extern void phase_settle(game *g);
extern void trade_chosen(game *g, int who, int which, int no_bonus);
extern void trade_action(game *g, int who, int no_bonus, int phase_bonus);
extern int good_chosen(game *g, int who, int c_idx, int o_idx, int g_list[],
                       int num);
extern int consume_hand_chosen(game *g, int who, int c_idx, int o_idx,
                               int list[], int n);
extern void consume_prestige_chosen(game *g, int who, int c_idx, int o_idx);
extern void consume_chosen(game *g, int who, int c_idx, int o_idx);
extern int consume_action(game *g, int who);
extern void consume_player(game *g, int who);
extern void phase_consume(game *g);
extern void produce_world(game *g, int who, int which);
extern void discard_produce_chosen(game *g, int who, int world, int discard);
extern void produce_chosen(game *g, int who, int c_idx, int o_idx);
extern int produce_action(game *g, int who);
extern void phase_produce_end(game *g);
extern void produce_player(game *g, int who);
extern void phase_produce(game *g);
extern void phase_discard(game *g);
extern int goal_minimum(int goal);
extern void check_goals(game *g);
extern int total_military(game *g, int who);
extern void score_game(game *g);
extern char *action_name(int act);
extern int start_callback(game *g, int who, int list[], int n, int special[],
                          int num_special);
extern void begin_game(game *g);
extern int game_round(game *g);
extern void declare_winner(game *g);

extern void ai_debug(game *g, double win_prob[MAX_PLAYER][MAX_PLAYER],
                              double *role[], double *action_score[],
                              int *num_action);

extern int load_game(game *g, char *filename);
extern int save_game(game *g, char *filename, int player_us);
