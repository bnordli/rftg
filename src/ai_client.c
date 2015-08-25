/*
 * Race for the Galaxy AI
 *
 * Copyright (C) 2009-2011 Keldon Jones
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
#include "comm.h"

/*
 * Our copy of game data.
 */
static game real_game;

/*
 * Our player index.
 */
static int player_us;

/*
 * Send message to server.
 */
void send_msg(int fd, char *msg)
{
	int size, sent = 0, x;
	char *ptr;

	/* Go to size area of message */
	ptr = msg + 4;

	/* Read size */
	size = get_integer(&ptr);

	/* Send until finished */
	while (sent < size)
	{
		/* Write as much as possible */
		x = write(fd, msg + sent, size - sent);

		/* Check for errors */
		if (x < 0)
		{
			/* Check for broken pipe */
			if (errno == EPIPE) return;

			/* Error */
			perror("send");
			return;
		}

		/* Count bytes sent */
		sent += x;
	}
}

/*
 * Handle a message about game parameters.
 */
static void handle_status_meta(char *ptr)
{
	char name[1024];
	int i;

	/* Read basic game parameters */
	real_game.num_players = get_integer(&ptr);
	real_game.expanded = get_integer(&ptr);
	real_game.advanced = get_integer(&ptr);
	real_game.goal_disabled = get_integer(&ptr);
	real_game.takeover_disabled = get_integer(&ptr);

	/* Initialize card designs for this expansion level */
	init_game(&real_game);

	/* Load AI neural networks for this game */
	ai_func.init(&real_game, 0, 0);

	/* Loop over goals */
	for (i = 0; i < MAX_GOAL; i++)
	{
		/* Read goal presence */
		real_game.goal_active[i] = get_integer(&ptr);
	}

	/* Loop over players */
	for (i = 0; i < real_game.num_players; i++)
	{
		/* Read player name */
		get_string(name, &ptr);

		/* Copy name */
		real_game.p[i].name = strdup(name);
	}
}

/*
 * Handle a status update about a player.
 */
static void handle_status_player(char *ptr)
{
	player *p_ptr;
	int x;

	/* Get player index */
	x = get_integer(&ptr);

	/* Get player pointer */
	p_ptr = &real_game.p[x];

	/* Read actions */
	p_ptr->action[0] = get_integer(&ptr);
	p_ptr->action[1] = get_integer(&ptr);

	/* Read prestige action used flag */
	p_ptr->prestige_action_used = get_integer(&ptr);

	/* Loop over goals */
	for (x = 0; x < MAX_GOAL; x++)
	{
		/* Read goal claimed */
		p_ptr->goal_claimed[x] = get_integer(&ptr);

		/* Real goal progress */
		p_ptr->goal_progress[x] = get_integer(&ptr);
	}

	/* Read player's prestige count */
	p_ptr->prestige = get_integer(&ptr);

	/* Read player's VP count */
	p_ptr->vp = get_integer(&ptr);

	/* Read player's phase bonuses */
	p_ptr->phase_bonus_used = get_integer(&ptr);
	p_ptr->bonus_military = get_integer(&ptr);
	p_ptr->bonus_reduce = get_integer(&ptr);
}

/*
 * Handle a status update about a card.
 */
static void handle_status_card(char *ptr)
{
	card *c_ptr;
	int x;
	int owner, where, start_owner, start_where;

	/* Read card index */
	x = get_integer(&ptr);

	/* Get card pointer */
	c_ptr = &real_game.deck[x];

	/* Read card owner */
	owner = get_integer(&ptr);
	start_owner = get_integer(&ptr);

	/* Read card location */
	where = get_integer(&ptr);
	start_where = get_integer(&ptr);

	/* Move card to current location */
	move_card(&real_game, x, owner, where);

	/* Move "start of phase" location */
	move_start(&real_game, x, start_owner, start_where);

	/* Read misc flags */
	c_ptr->misc = get_integer(&ptr);

	/* Read order played */
	c_ptr->order = get_integer(&ptr);

	/* Read number of goods */
	c_ptr->num_goods = get_integer(&ptr);

	/* Read covered card */
	c_ptr->covering = get_integer(&ptr);

	/* Set known flags for active and revealed cards */
	if (c_ptr->where == WHERE_ACTIVE || c_ptr->where == WHERE_ASIDE)
	{
		/* Card's location is known to everyone */
		c_ptr->misc |= MISC_KNOWN_MASK;
	}

	/* Set known flags for our cards in hand and saved cards */
	if (c_ptr->owner == player_us &&
	    (c_ptr->where == WHERE_HAND || c_ptr->where == WHERE_SAVED))
	{
		/* Set known flag */
		c_ptr->misc |= (1 << c_ptr->owner);
	}
}

/*
 * Handle a goal status update.
 */
static void handle_status_goal(char *ptr)
{
	int i;

	/* Loop over goals */
	for (i = 0; i < MAX_GOAL; i++)
	{
		/* Read goal availability and progress */
		real_game.goal_avail[i] = get_integer(&ptr);
		real_game.goal_most[i] = get_integer(&ptr);
	}
}

/*
 * Handle a miscellaneous status update.
 */
static void handle_status_misc(char *ptr)
{
	int i;

	/* Read round number */
	real_game.round = get_integer(&ptr);

	/* Read VP pool size */
	real_game.vp_pool = get_integer(&ptr);

	/* Loop over actions */
	for (i = 0; i < MAX_ACTION; i++)
	{
		/* Read action selected flag */
		real_game.action_selected[i] = get_integer(&ptr);
	}

	/* Read current action */
	real_game.cur_action = get_integer(&ptr);
}

/*
 * Server has asked us to make a choice.
 *
 * Ask AI and return result.
 */
static void handle_choose(char *ptr)
{
	player *p_ptr;
	char msg[1024];
	int pos, type, num, num_special;
	int list[MAX_DECK], special[MAX_DECK];
	int arg1, arg2, arg3;
	int i;

	/* Get player pointer */
	p_ptr = &real_game.p[player_us];

	/* Read choice log position expected */
	pos = get_integer(&ptr);

	/* Check for further along in log than we are */
	if (pos > p_ptr->choice_pos)
	{
		/* Adjust current position */
		p_ptr->choice_size = p_ptr->choice_pos = pos;
	}

	/* Check for request for choice we have already made */
	else if (pos < p_ptr->choice_pos)
	{
		/* XXX Do nothing */
		return;
	}

	/* Read choice type */
	type = get_integer(&ptr);

	/* Read number of items in list */
	num = get_integer(&ptr);

	/* Loop over items in list */
	for (i = 0; i < num; i++)
	{
		/* Read list item */
		list[i] = get_integer(&ptr);
	}

	/* Read number of special items in list */
	num_special = get_integer(&ptr);

	/* Loop over special items */
	for (i = 0; i < num_special; i++)
	{
		/* Read special item */
		special[i] = get_integer(&ptr);
	}

	/* Read extra arguments */
	arg1 = get_integer(&ptr);
	arg2 = get_integer(&ptr);
	arg3 = get_integer(&ptr);

	/* Ask AI for decision */
	ai_func.make_choice(&real_game, player_us, type, list, &num,
	                    special, &num_special, arg1, arg2, arg3);

	/* Start reply */
	ptr = msg;

	/* Begin message */
	start_msg(&ptr, MSG_CHOOSE);

	/* Put choice log position */
	put_integer(p_ptr->choice_pos, &ptr);

	/* Copy entries from choice log */
	for (i = p_ptr->choice_pos; i < p_ptr->choice_size; i++)
	{
		/* Copy entry */
		put_integer(p_ptr->choice_log[i], &ptr);
	}

	/* Move current position to end of choice log */
	p_ptr->choice_pos = p_ptr->choice_size;

	/* Finish message */
	finish_msg(msg, ptr);

	/* Send reply */
	send_msg(0, msg);
}

/*
 * A complete message has been read.
 */
static void message_read(char *data)
{
	char *ptr = data;
	char text[1024];
	int type, size;

	/* Read message type and size */
	type = get_integer(&ptr);
	size = get_integer(&ptr);

	/* Check message type */
	switch (type)
	{
		/* Meta-information update */
		case MSG_STATUS_META:

			/* Handle message */
			handle_status_meta(ptr);
			break;

		/* Player status update */
		case MSG_STATUS_PLAYER:

			/* Handle message */
			handle_status_player(ptr);
			break;

		/* Card status update */
		case MSG_STATUS_CARD:

			/* Handle message */
			handle_status_card(ptr);
			break;

		/* Goal status update */
		case MSG_STATUS_GOAL:

			/* Handle message */
			handle_status_goal(ptr);
			break;

		/* Misc status update */
		case MSG_STATUS_MISC:

			/* Handle message */
			handle_status_misc(ptr);
			break;

		/* Seat number update */
		case MSG_SEAT:

			/* Get seat number */
			player_us = get_integer(&ptr);
			break;

		/* Make a choice */
		case MSG_CHOOSE:

			/* Handle message */
			handle_choose(ptr);
			break;

		/* Game is over */
		case MSG_GAMEOVER:

			/* Done */
			exit(0);
			break;

		/* Server disconnect */
		case MSG_GOODBYE:
			/* Read reason */
			get_string(text, &ptr);

			/* Print reason and exit */
			sprintf(text + 512, "Server disconnected: %s\n", text);
			display_error(text + 512);
			exit(0);
			break;

		/* Unneeded message types */
		case MSG_LOG:
		case MSG_LOG_FORMAT:
		case MSG_CHAT:
		case MSG_GAMECHAT:
		case MSG_WAITING:
		case MSG_PREPARE:
			break;

		default:
			printf("Unknown message type %d\n", type);
			break;
	}
}

/*
 * Data is ready to be read.
 */
static void data_ready(void)
{
	static char buf[1024];
	static int buf_full;
	char *ptr;
	int x;

	/* Determine number of bytes to read */
	if (buf_full < 8)
	{
		/* Read 8 bytes for header */
		x = 8;
	}
	else
	{
		/* Skip to message size portion of header */
		ptr = buf + 4;
		x = get_integer(&ptr);
	}

	/* Check for overly long message */
	if (x > 1024)
	{
		/* Error */
		display_error("Received too long message!\n");
		exit(1);
	}

	/* Try to read enough bytes */
	x = read(0, buf + buf_full, x - buf_full);

	/* Check for error */
	if (x <= 0)
	{
		/* Check for try again error */
		if (x < 0 && errno == EAGAIN) return;

		/* Check for server disconnect */
		if (x == 0) exit(0);

		/* Print error */
		perror("read");
		exit(1);
	}

	/* Add to amount read */
	buf_full += x;

	/* Check for complete message header */
	if (buf_full >= 8)
	{
		/* Skip to length portion of header */
		ptr = buf + 4;
		x = get_integer(&ptr);

		/* Check for too-small message */
		if (x < 8)
		{
			/* Print error */
			display_error("Got too small message!\n");
			exit(1);
		}

		/* Check for complete message */
		if (buf_full == x)
		{
			/* Handle message at next opportunity */
			message_read(buf);

			/* Clear buffer */
			buf_full = 0;
		}
	}
}

/*
 * Print errors to standard output.
 */
void display_error(char *msg)
{
	/* Forward message */
	printf("%s", msg);
}

/*
 * Handle a message.
 */
void message_add(game *g, char *msg)
{
}

/*
 * Handle a formatted message.
 */
void message_add_formatted(game *g, char *msg, char *tag)
{
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
 * Read messages from server and have AI answer choice queries.
 */
int main(int argc, char *argv[])
{
	int i;
#if 0
	volatile int f = 1;

	while (f) ;
#endif

	/* Read card database */
	if (read_cards(NULL) < 0)
	{
		/* Exit */
		exit(1);
	}

	/* Create choice logs */
	for (i = 0; i < MAX_PLAYER; i++)
	{
		/* Create choice log for player */
		real_game.p[i].choice_log = (int *)malloc(sizeof(int) * 4096);
	}

	/* Loop forever */
	while (1)
	{
		/* Try to read data */
		data_ready();
	}
}
