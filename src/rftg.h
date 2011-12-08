/*
 * Race for the Galaxy AI
 * 
 * Copyright (C) 2009 Keldon Jones
 *
 * Source file modified by B. Nordli, November 2011.
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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef VERSION
#define VERSION "0.8.1"
#endif

#ifndef RELEASE
#define RELEASE VERSION "n"
#endif

#include <stdio.h>
#include <stdlib.h>
#ifdef WIN32
#include "stdint.h"
#else
#include <stdint.h>
#endif
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
 * Maximum number of variants.
 */
#define MAX_VARIANT 4

/*
 * Number of available card designs slots.
 */
#define AVAILABLE_DESIGN 256

/*
 * Number of original card designs.
 */
#define MAX_DESIGN 191

/*
 * Number of cards in the deck.
 */
#define MAX_DECK 328

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
 * Number of goods.
 */
#define MAX_GOOD 6

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
 * Number of card locations.
 */
#define MAX_WHERE 8


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
#define ACT_DRAFTING       -3
#define ACT_GAME_START     -2
#define ACT_ROUND_START    -1
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
#define ACT_ROUND_END      10

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
#define WHERE_REMOVED  7

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
 * Variants.
 */
#define VARIANT_TAKEOVER 1
#define VARIANT_DRAFTING 2
#define VARIANT_SEPARATE 3

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
#define CHOICE_DRAFT_FIRST      25
#define CHOICE_DRAFT            26

/*
 * Debug choices are negative
 */
#define CHOICE_D_MOVE           -10
#define CHOICE_D_SHUFFLE        -11
#define CHOICE_D_TAKE_CARD      -12
#define CHOICE_D_TAKE_VP        -13
#define CHOICE_D_TAKE_PRESTIGE  -14
#define CHOICE_D_ROTATE         -15

/*
 * GUI: Text formatting
 */
#define FORMAT_EM "em"
#define FORMAT_CHAT "chat"
#define FORMAT_PHASE "phase"
#define FORMAT_TAKEOVER "takeover"
#define FORMAT_GOAL "goal"
#define FORMAT_PRESTIGE "prestige"
#define FORMAT_VERBOSE "verbose"
#define FORMAT_DRAW "draw"
#define FORMAT_DISCARD "discard"
#define FORMAT_DEBUG "debug"

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
	int8_t phase;

	/* Power's effect code */
	uint64_t code;

	/* Power's value */
	int8_t value;

	/* Number of times power may be used */
	int8_t times;

} power;

/*
 * Location of a power.
 */
typedef struct power_where
{
	/* Card index */
	int16_t c_idx;

	/* Power index */
	int8_t o_idx;

	/* Pointer to power */
	power *o_ptr;

} power_where;

/*
 * A card's special VP bonus.
 */
typedef struct vp_bonus
{
	/* Points */
	int8_t point;

	/* Type */
	uint64_t type;

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
	int16_t index;

	/* Type (development or world) */
	int8_t type;

	/* Cost to play */
	int8_t cost;

	/* Victory points given */
	int8_t vp;

	/* Number of cards in each expansion deck */
	int8_t expand[MAX_EXPANSION];

	/* Type of good produced (if any) */
	int8_t good_type;

	/* Flags (military, windfall, alien, rebel, start world, etc) */
	uint32_t flags;

	/* Number of this design in the deck */
	int8_t dup;

	/* Number of card powers */
	int8_t num_power;

	/* List of powers */
	power powers[MAX_POWER];

	/* Number of vp bonuses */
	int8_t num_vp_bonus;

	/* List of VP bonuses */
	vp_bonus bonuses[MAX_VP_BONUS];

} design;

/*
 * Information about an instance of a card.
 */
typedef struct card
{
	/* Card's owner (if any) */
	int8_t owner;

	/* Card's location */
	int8_t where;

	/* Card's owner at start of phase */
	int8_t start_owner;

	/* Card's location at start of phase */
	int8_t start_where;

	/* Card is being placed and is not yet paid for */
	int8_t unpaid;

	/* Bitmask of players who know card's location */
	uint8_t known;

	/* Bitmask of card powers which have been used */
	uint8_t used;

	/* Card has produced this phase */
	int8_t produced;

	/* Card design */
	design *d_ptr;

	/* Card covering us (as a good) */
	int16_t covered;

	/* Order played on the table */
	int8_t order;

	/* Next card index if belonging to player */
	int16_t next;

	/* Next card index as of start of phase */
	int16_t start_next;

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

	/* Prepare for a phase */
	void (*prepare_phase)(struct game *g, int who, int phase, int arg);

	/* Make a choice among those given */
	void (*make_choice)(struct game *g, int who, int type, int list[],
	                    int *nl, int special[], int *ns, int arg1,
	                    int arg2, int arg3);

	/* Wait for answer to be ready */
	void (*wait_answer)(struct game *g, int who);

	/* Take sample cards into hand from Explore phase */
	void (*explore_sample)(struct game *g, int who, int draw, int keep,
	                       int discard_any);

	/* Game over */
	void (*game_over)(struct game *g, int who);

	/* Shutdown */
	void (*shutdown)(struct game *g, int who);

	/* Private message */
	void (*private_message)(struct game *g, int who, char *msg, char *tag);

} decisions;

/*
 * Information about a player.
 */
typedef struct player
{
	/* Player's name/color */
	char *name;

	/* Whether the player is played by the AI */
	int8_t ai;

	/* Ask player to make decisions */
	decisions *control;

	/* Action(s) chosen */
	int action[2];

	/* Previous turn action(s) */
	int prev_action[2];

	/* Player has used prestige/search action */
	int8_t prestige_action_used;

	/* Player has used phase bonus */
	int8_t phase_bonus_used;

	/* Player's start world */
	int16_t start;

	/* Player's first card of each location */
	int16_t head[MAX_WHERE];

	/* Player's first card of each location as of the start of the phase */
	int16_t start_head[MAX_WHERE];

	/* Card chosen in Develop or Settle phase */
	int16_t placing;

	/* Bonus military accrued so far this phase */
	int8_t bonus_military;

	/* Bonus settle discount accrued so far this phase */
	int8_t bonus_reduce;

	/* Number of cards discarded at end of turn */
	int8_t end_discard;

	/* Bitmask of goal cards claimed */
	uint32_t goal_claimed;

	/* Progress toward each goal */
	int8_t goal_progress[MAX_GOAL];

	/* Prestige */
	int8_t prestige;

	/* Prestige earned this turn */
	int8_t prestige_turn;

	/* Victory point chips */
	int16_t vp;

	/* Victory points from goals */
	int16_t goal_vp;

	/* Total victory points (if game ended now) */
	int16_t end_vp;

	/* Player is the winner */
	int8_t winner;

	/* Number of "fake" drawn cards in simulated games */
	int16_t fake_hand;

	/* Total number of "fake" cards seen this turn */
	int16_t total_fake;

	/* Number of cards discarded this turn but not removed from hand */
	int16_t fake_discards;

	/* Counter for cards played */
	int8_t table_order;

	/* Cards, VP, and prestige earned during the current phase */
	int16_t phase_cards;
	int16_t phase_vp;
	int16_t phase_prestige;

	/* Player seed */
	unsigned int seed;

	/* Log of player's choices */
	int *choice_log;

	/* Size and current position of choice log */
	int choice_size;
	int choice_pos;

	/* History of log sizes */
	int *choice_history;

	/* Last write position for log */
	int choice_unread_pos;

} player;

/*
 * Information about a game.
 */
typedef struct game
{
	/* Session ID in online server */
	int session_id;

	/* Current random seed for the public deck */
	unsigned int random_seed;

	/* Random seed at start of game */
	unsigned int start_seed;

	/* Whether game is a debug game or not */
	int8_t debug_game;

	/* Number of debug rotations since last check */
	int8_t debug_rotate;

	/* Game is a simulation */
	int8_t simulation;

	/* Who initiated the simulation */
	int8_t sim_who;

	/* Name of human player */
	char *human_name;

	/* Players */
	player p[MAX_PLAYER];

	/* Number of players */
	int8_t num_players;

	/* This is an "advanced" 2 player game */
	int8_t advanced;

	/* Number of expansions in use */
	int8_t expanded;

	/* Disable goals in expanded games */
	int8_t goal_disabled;

	/* Disable takeovers in second (or later) expansion */
	int8_t takeover_disabled;

	/* Game variant */
	int8_t variant;

	/* Size of deck in use */
	int16_t deck_size;

	/* Information about each card */
	card deck[MAX_DECK];

	/* Victory points remaining in the pool */
	int8_t vp_pool;

	/* Bitmask of goals active in this game */
	uint32_t goal_active;

	/* Bitmask of goals yet unclaimed */
	uint32_t goal_avail;

	/* Maximum progress toward a "most" goal */
	int8_t goal_most[MAX_GOAL];

	/* Number of pending takeovers */
	int8_t num_takeover;

	/* Worlds targeted for takeover */
	int16_t takeover_target[MAX_TAKEOVER];

	/* Player attempting each takeover */
	int8_t takeover_who[MAX_TAKEOVER];

	/* Card holding takeover power */
	int16_t takeover_power[MAX_TAKEOVER];

	/* Takeover marked for failure */
	int8_t takeover_defeated[MAX_TAKEOVER];

	/* XXX Current kind of "any" good world */
	int8_t oort_kind;

	/* Current kind of "any" good giving owner the best score */
	int8_t best_oort_kind;

	/* Actions selected this round */
	int8_t action_selected[MAX_ACTION];

	/* Current action */
	int8_t cur_action;

	/* Current player in phase */
	int8_t turn;

	/* Current round number */
	int8_t round;

	/* Game is over */
	int8_t game_over;

} game;


/*
 * External variables.
 */
extern char program_path[1024];
extern int num_design;
extern design library[AVAILABLE_DESIGN];
extern char *actname[MAX_ACTION * 2 - 1];
extern char *plain_actname[MAX_ACTION + 1];
extern char *good_printable[MAX_GOOD];
extern char *goal_name[MAX_GOAL];
extern char *search_name[MAX_SEARCH];
extern char *exp_names[MAX_EXPANSION + 1];
extern char *player_labels[MAX_PLAYER];
extern char *location_names[MAX_WHERE];
extern char *variant_labels[MAX_VARIANT + 1];
extern decisions ai_func;
extern decisions gui_func;

/*
 * Macro functions.
 */
#define PLURAL(x) ((x) == 1 ? "" : "s")

/*
 * External functions.
 */
extern void display_error(char *msg);
extern void set_program_path(int argc, char **argv);
extern FILE *open_file(char *file);
extern void message_add(game *g, char *msg);
extern void message_add_formatted(game *g, char *msg, char *tag);
extern int goals_enabled(game *g);
extern int takeovers_enabled(game *g);
extern int separate_decks(game *g);
extern int min_expansion(int variant);
extern int max_players(int expansion, int variant);
extern int count_draw(game *g, int who);
extern void auto_export(void);
extern int game_rand(game *g, int who);
extern int read_cards(void);
extern void init_game(game *g);
extern int simple_rand(unsigned int *seed);
extern int next_choice(int* log, int pos);
extern int count_player_area(game *g, int who, int where);
extern int count_active_flags(game *g, int who, int flags);
extern int player_chose(game *g, int who, int act);
extern int prestige_on_tile(game *g, int who);
extern void refresh_draw(game *g, int who);
extern int random_draw(game *g, int who, int *emptied);
extern void move_card(game *g, int which, int who, int where);
extern void move_start(game *g, int which, int who, int where);
extern void draw_card(game *g, int who, char *reason);
extern void draw_cards(game *g, int who, int num, char *reason);
extern void discard_card(game *g, int who, int which);
extern void start_prestige(game *g);
extern void clear_temp(game *g);
extern int get_goods(game *g, int who, int goods[], int type);
extern void discard_callback(game *g, int who, int list[], int num);
extern void discard_to(game *g, int who, int to, int discard_any);
extern int get_powers(game *g, int who, int phase, power_where *w_list);
extern void add_good(game *g, card *c_ptr);
extern int search_match(game *g, int which, int category);
extern void phase_search(game *g);
extern void phase_explore(game *g);
extern void place_card(game *g, int who, int which);
extern int devel_cost(game *g, int who, int which);
extern int devel_callback(game *g, int who, int which, int list[], int num,
                          int special[], int num_special);
extern void develop_action(game *g, int who, int placing);
extern void phase_develop(game *g);
extern int strength_against(game *g, int who, int world, int attack, int defend);
extern int payment_callback(game *g, int who, int which, int list[], int num,
                            int special[], int num_special, int mil_only);
extern int settle_legal(game *g, int who, int which, int mil_bonus,
                        int mil_only);
extern int takeover_callback(game *g, int special, int world);
extern int settle_check_takeover(game *g, int who, card *extra, int ask);
extern int upgrade_chosen(game *g, int who, int replacement, int old);
extern void settle_action(game *g, int who, int world);
extern int defend_callback(game *g, int who, int deficit, int list[], int num,
                           int special[], int num_special);
extern int resolve_takeover(game *g, int who, int world, int special,
                            int defeated, int simulated);
extern void resolve_takeovers(game *g);
extern void phase_settle(game *g);
extern int trade_value(game *g, int who, card *c_ptr, int type, int no_bonus);
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
extern void produce_world(game *g, int who, int which, int c_idx, int o_idx);
extern void discard_produce_chosen(game *g, int who, int world, int discard,
                                   int c_idx, int o_idx);
extern void produce_chosen(game *g, int who, int c_idx, int o_idx);
extern int produce_action(game *g, int who);
extern void phase_produce_end(game *g);
extern void produce_player(game *g, int who);
extern void phase_produce(game *g);
extern void phase_discard(game *g);
extern int goal_minimum(int goal);
extern void check_goal_loss(game *g, int who, int goal);
extern void check_goals(game *g);
extern int total_military(game *g, int who);
extern int compute_card_vp(game *g, int who, int which);
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
extern void write_game(game *g, FILE *fff, int player_us);
extern char *xml_escape(const char *s);
extern int export_game(game *g, char *filename, char *style_sheet,
                       char *server, int player_us, const char *message,
                       int num_special, card** special_cards,
                       int export_card_locations,
                       void (*export_log)(FILE *fff, int gid),
                       void (*export_callback)(FILE *fff, int gid), int gid);
