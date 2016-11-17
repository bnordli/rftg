/*
 * Race for the Galaxy AI
 *
 * Copyright (C) 2009-2011 Keldon Jones
 *
 * Source file modified by J.-R. Reinhard, October 2016.
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
 * Our incoming message buffer
 */
static char buf[BUF_LEN];

/*
 * The number of received bytes in the buffer
 */
static int buf_full;

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
	get_integer(&size, msg, HEADER_LEN, &ptr);

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
static void handle_status_meta(char *ptr, int size)
{
	char name[1024];
	int i, x;

	/* Skip header */
	ptr += HEADER_LEN;

	/* Read basic game parameters */
	if (!get_integer(&x, buf, size, &ptr)) goto format_error;
	real_game.num_players = x;
	if (!get_integer(&x, buf, size, &ptr)) goto format_error;
	real_game.expanded = x;
	if (!get_integer(&x, buf, size, &ptr)) goto format_error;
	real_game.advanced = x;
	if (!get_integer(&x, buf, size, &ptr)) goto format_error;
	real_game.goal_disabled = x;
	if (!get_integer(&x, buf, size, &ptr)) goto format_error;
	real_game.takeover_disabled = x;

	/* Initialize card designs for this expansion level */
	init_game(&real_game);

	/* Load AI neural networks for this game */
	ai_func.init(&real_game, 0, 0);

	/* Loop over goals */
	for (i = 0; i < MAX_GOAL; i++)
	{
		/* Read goal presence */
		if (!get_integer(&x, buf, size, &ptr)) goto format_error;
		real_game.goal_active[i] = x;
	}

	/* Loop over players */
	for (i = 0; i < real_game.num_players; i++)
	{
		/* Read player name */
		if (!get_string(name, 1024, buf, size, &ptr)) goto format_error;

		/* Copy name */
		real_game.p[i].name = strdup(name);
	}

	/*
	 * Process badly formatted message, missing \0 in a string or with a
	 * format leading to read beyond the message length.
	 */
	if (0)
	{
format_error:
		display_error("Message format error");
		exit(1);
	}
}

/*
 * Handle a status update about a player.
 */
static void handle_status_player(char *ptr, int size)
{
	player *p_ptr;
	int i, x;

	/* Skip header */
	ptr += HEADER_LEN;

	/* Get player index */
	if (!get_integer(&x, buf, size, &ptr)) goto format_error;

	/* Get player pointer */
	p_ptr = &real_game.p[x];

	/* Read actions */
	if (!get_integer(p_ptr->action, buf, size, &ptr)) goto format_error;
	if (!get_integer(p_ptr->action + 1, buf, size, &ptr)) goto format_error;

	/* Read prestige action used flag */
	if (!get_integer(&x, buf, size, &ptr)) goto format_error;
	p_ptr->prestige_action_used = x;

	/* Loop over goals */
	for (i = 0; i < MAX_GOAL; i++)
	{
		/* Read goal claimed */
		if (!get_integer(&x, buf, size, &ptr)) goto format_error;
		p_ptr->goal_claimed[i] = x;

		/* Real goal progress */
		if (!get_integer(&x, buf, size, &ptr)) goto format_error;
		p_ptr->goal_progress[i] = x;
	}

	/* Read player's prestige count */
	if (!get_integer(&x, buf, size, &ptr)) goto format_error;
	p_ptr->prestige = x;

	/* Read player's VP count */
	if (!get_integer(&x, buf, size, &ptr)) goto format_error;
	p_ptr->vp = x;

	/* Read player's phase bonuses */
	if (!get_integer(&x, buf, size, &ptr)) goto format_error;
	p_ptr->phase_bonus_used = x;

	/* Read player's bonus military */
	if (!get_integer(&x, buf, size, &ptr)) goto format_error;
	p_ptr->bonus_military = x;

	/* Read player's Xeno military bonus (only for XI games) */
	if (real_game.expanded == EXP_XI)
	{
		if (!get_integer(&x, buf, size, &ptr)) goto format_error;
		p_ptr->bonus_military_xeno = x;
	}

	/* Read player's reduce bonus */
	if (!get_integer(&x, buf, size, &ptr)) goto format_error;
	p_ptr->bonus_reduce = x;

	/*
	 * Process badly formatted message, missing \0 in a string or with a
	 * format leading to read beyond the message length.
	 */
	if (0)
	{
format_error:
		display_error("Message format error");
		exit(1);
	}
}

/*
 * Handle a status update about a card.
 */
static void handle_status_card(char *ptr, int size)
{
	card *c_ptr;
	int x, y;
	int owner, where, start_owner, start_where;

	/* Skip header */
	ptr += HEADER_LEN;

	/* Read card index */
	if (!get_integer(&x, buf, size, &ptr)) goto format_error;

	/* Get card pointer */
	c_ptr = &real_game.deck[x];

	/* Read card owner */
	if (!get_integer(&owner, buf, size, &ptr)) goto format_error;
	if (!get_integer(&start_owner, buf, size, &ptr)) goto format_error;

	/* Read card location */
	if (!get_integer(&where, buf, size, &ptr)) goto format_error;
	if (!get_integer(&start_where, buf, size, &ptr)) goto format_error;

	/* Move card to current location */
	move_card(&real_game, x, owner, where);

	/* Move "start of phase" location */
	move_start(&real_game, x, start_owner, start_where);

	/* Read misc flags */
	if (!get_integer(&y, buf, size, &ptr)) goto format_error;
	c_ptr->misc = y;

	/* Read order played */
	if (!get_integer(&y, buf, size, &ptr)) goto format_error;
	c_ptr->order = y;

	/* Read number of goods */
	if (!get_integer(&y, buf, size, &ptr)) goto format_error;
	c_ptr->num_goods = y;

	/* Read covered card */
	if (!get_integer(&y, buf, size, &ptr)) goto format_error;
	c_ptr->covering = y;

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

	/*
	 * Process badly formatted message, missing \0 in a string or with a
	 * format leading to read beyond the message length.
	 */
	if (0)
	{
format_error:
		display_error("Message format error");
		exit(1);
	}
}

/*
 * Handle a goal status update.
 */
static void handle_status_goal(char *ptr, int size)
{
	int i, x;

	/* Skip header */
	ptr += HEADER_LEN;

	/* Loop over goals */
	for (i = 0; i < MAX_GOAL; i++)
	{
		/* Read goal availability and progress */
		if (!get_integer(&x, buf, size, &ptr)) goto format_error;
		real_game.goal_avail[i] = x;
		if (!get_integer(&x, buf, size, &ptr)) goto format_error;
		real_game.goal_most[i] = x;
	}

	/*
	 * Process badly formatted message, missing \0 in a string or with a
	 * format leading to read beyond the message length.
	 */
	if (0)
	{
format_error:
		display_error("Message format error");
		exit(1);
	}
}

/*
 * Handle a miscellaneous status update.
 */
static void handle_status_misc(char *ptr, int size)
{
	int i, x;

	/* Skip header */
	ptr += HEADER_LEN;

	/* Read round number */
	if (!get_integer(&x, buf, size, &ptr)) goto format_error;
	real_game.round = x;

	/* Read VP pool size */
	if (!get_integer(&x, buf, size, &ptr)) goto format_error;
	real_game.vp_pool = x;

	/* Loop over actions */
	for (i = 0; i < MAX_ACTION; i++)
	{
		/* Read action selected flag */
		if (!get_integer(&x, buf, size, &ptr)) goto format_error;
		real_game.action_selected[i] = x;
	}

	/* Read current action */
	if (!get_integer(&x, buf, size, &ptr)) goto format_error;
	real_game.cur_action = x;

	/*
	 * Process badly formatted message, missing \0 in a string or with a
	 * format leading to read beyond the message length.
	 */
	if (0)
	{
format_error:
		display_error("Message format error");
		exit(1);
	}
}

/*
 * Server has asked us to make a choice.
 *
 * Ask AI and return result.
 */
static void handle_choose(char *ptr, int size)
{
	player *p_ptr;
	char msg[BUF_LEN];
	int pos, type, num, num_special;
	int list[MAX_DECK], special[MAX_DECK];
	int arg1, arg2, arg3;
	int i;

	/* Skip header */
	ptr += HEADER_LEN;

	/* Get player pointer */
	p_ptr = &real_game.p[player_us];

	/* Read choice log position expected */
	if (!get_integer(&pos, buf, size, &ptr)) goto format_error;

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
	if (!get_integer(&type, buf, size, &ptr)) goto format_error;

	/* Read number of items in list */
	if (!get_integer(&num, buf, size, &ptr)) goto format_error;

	/* Loop over items in list */
	for (i = 0; i < num; i++)
	{
		/* Read list item */
		if (!get_integer(list + i, buf, size, &ptr)) goto format_error;
	}

	/* Read number of special items in list */
	if (!get_integer(&num_special, buf, size, &ptr)) goto format_error;

	/* Loop over special items */
	for (i = 0; i < num_special; i++)
	{
		/* Read special item */
		if (!get_integer(special + i, buf, size, &ptr)) goto format_error;
	}

	/* Read extra arguments */
	if (!get_integer(&arg1, buf, size, &ptr)) goto format_error;
	if (!get_integer(&arg2, buf, size, &ptr)) goto format_error;
	if (!get_integer(&arg3, buf, size, &ptr)) goto format_error;

	/*
	 * Process badly formatted message, missing \0 in a string or with a
	 * format leading to read beyond the message length.
	 */
	if (0)
	{
format_error:
		display_error("Message format error");
		exit(1);
	}

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
 * A complete message has been read. Parse its header and handle the message.
 */
static void message_read(char *data)
{
	char *ptr = data;
	char text[1024];
	int type, size;

	/* Read message type and size */
	get_integer(&type, data, HEADER_LEN, &ptr);
	get_integer(&size, data, HEADER_LEN, &ptr);

	/* Check message type */
	switch (type)
	{
		/* Meta-information update */
		case MSG_STATUS_META:

			/* Handle message */
			handle_status_meta(data, size);
			break;

		/* Player status update */
		case MSG_STATUS_PLAYER:

			/* Handle message */
			handle_status_player(data, size);
			break;

		/* Card status update */
		case MSG_STATUS_CARD:

			/* Handle message */
			handle_status_card(data, size);
			break;

		/* Goal status update */
		case MSG_STATUS_GOAL:

			/* Handle message */
			handle_status_goal(data, size);
			break;

		/* Misc status update */
		case MSG_STATUS_MISC:

			/* Handle message */
			handle_status_misc(data, size);
			break;

		/* Seat number update */
		case MSG_SEAT:

			/* Get seat number */
			if (!get_integer(&player_us, data, size, &ptr)) goto format_error;
			break;

		/* Make a choice */
		case MSG_CHOOSE:

			/* Handle message */
			handle_choose(data, size);
			break;

		/* Game is over */
		case MSG_GAMEOVER:

			/* Done */
			exit(0);
			break;

		/* Server disconnect */
		case MSG_GOODBYE:
			/* Read reason */
			if (!get_string(text, 1024, data, size, &ptr)) goto format_error;

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

	/*
	 * Process badly formatted message, missing \0 in a string or with a
	 * format leading to read beyond the message length.
	 */
	if (0)
	{
format_error:
		display_error("Message format error");
		exit(1);
	}
}

/*
 * Data is ready to be read.
 */
static void data_ready(void)
{
	char *ptr;
	int x, size;

	/* Determine number of bytes to read */
	if (buf_full < HEADER_LEN)
	{
		/* Undetermined message size, by default use the header size */
		size = HEADER_LEN;
	}
	else
	{
		/*
		 * Read message size. Note: size != HEADER_LEN because messages of this
		 * size are handled as soon as their length has been determined.
		 */
		ptr = buf + 4;
		get_integer(&size, buf, HEADER_LEN, &ptr);
	}

	/* Try to read enough bytes */
	x = read(0, buf + buf_full, size - buf_full);

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

	/* Check for complete message header if it was not determined previously */
	if (size == HEADER_LEN && buf_full >= HEADER_LEN)
	{
		/* Read message size */
		ptr = buf + 4;
		get_integer(&size, buf, HEADER_LEN, &ptr);

		/* Check for too small message */
		if (size < HEADER_LEN)
		{
			/* Print error */
			display_error("Got too small message!\n");
			exit(1);
		}

		/* Check for too long message */
		if (size > BUF_LEN)
		{
			/* Error */
			display_error("Received too long message!\n");
			exit(1);
		}
	}

	/* Check for complete message */
	if (buf_full == size)
	{
		/* Handle message */
		message_read(buf);

		/* Clear buffer */
		buf_full = 0;
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
