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

extern design library[MAX_DESIGN];
int num_players;

char *good_name[4] =
{
	"Novelty",
	"Rare",
	"Gene",
	"Alien"
};

void message_add(char *msg)
{
}

static void input_name_player(char *ptr, int who, int idx)
{
	int n = 0, i, j;

	if (idx < 151)
	{
		sprintf(ptr, "Player %d active %s", who, library[idx].name);
	}
	else if (idx < 163)
	{
		sprintf(ptr, "Player %d num devel %d", who, idx - 151);
	}
	else if (idx < 175)
	{
		sprintf(ptr, "Player %d num world %d", who, idx - 163);
	}
	else if (idx < 326)
	{
		sprintf(ptr, "Player %d good %s", who, library[idx - 175].name);
	}
	else if (idx < 338)
	{
		sprintf(ptr, "Player %d goods %d", who, idx - 326);
	}
	else if (idx < 342)
	{
		sprintf(ptr, "Player %d good type %s", who, good_name[idx - 338]);
	}
	else if (idx < 354)
	{
		sprintf(ptr, "Player %d cards %d", who, idx - 342);
	}
	else if (idx < 369)
	{
		sprintf(ptr, "Player %d military %d", who, idx - 354);
	}
	else if (idx < 370)
	{
		sprintf(ptr, "Player %d explore mix", who);
	}
	else if (idx < 385)
	{
		sprintf(ptr, "Player %d claimed goal %d", who, idx - 370);
	}
	else if (idx < 394)
	{
		sprintf(ptr, "Player %d prev %s", who, action_name[idx - 385]);
	}
}

static void input_name(char *ptr, int idx)
{
	if (idx < num_players * 394)
	{
		input_name_player(ptr, idx / 394, idx % 394);
	}
	else if (idx < num_players * 394 + 12)
	{
		sprintf(ptr, "VP Pool %d", idx - num_players * 394);
	}
	else if (idx < num_players * 394 + 24)
	{
		sprintf(ptr, "Most played %d", idx - num_players * 394 - 12);
	}
	else if (idx < num_players * 394 + 36)
	{
		sprintf(ptr, "Clock %d", idx - num_players * 394 - 24);
	}
	else if (idx < num_players * 394 + 51)
	{
		sprintf(ptr, "Goal active %d", idx - num_players * 394 - 36);
	}
	else if (idx < num_players * 394 + 66)
	{
		sprintf(ptr, "Goal available %d", idx - num_players * 394 - 51);
	}
}

int main(int argc, char *argv[])
{
	net learner;
	int i, j, inputs;
	double start[9];
	char buf[1024], *ptr;

	read_cards();

	num_players = atoi(argv[2]);
	inputs = 66 + num_players * 394;

	make_learner(&learner, inputs, 50, 9);

	load_net(&learner, argv[1]);

	for (i = 0; i < inputs; i++) learner.input_value[i] = 0;

	compute_net(&learner);

	for (i = 0; i < 9; i++) start[i] = learner.win_prob[i];

	for (i = 0; i < inputs; i++)
	{
		learner.input_value[i] = 1;

		compute_net(&learner);

		input_name(buf, i);

		for (ptr = buf; *ptr; ptr++) if (*ptr == ' ') *ptr = '_';

		printf("%s: ", buf);

		for (j = 0; j < 9; j++)
		{
			printf("%.4f ", learner.win_prob[j] - start[j]);
		}
		printf("\n");

		learner.input_value[i] = 0;
	}
}
