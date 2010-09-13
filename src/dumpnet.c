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
	else if (idx < 302)
	{
		sprintf(ptr, "Player %d good %s", who, library[idx - 151].name);
	}
	else if (idx < 314)
	{
		sprintf(ptr, "Player %d num goods %d", who, idx - 302);
	}
	else if (idx < 318)
	{
		sprintf(ptr, "Player %d good type %s", who, good_name[idx - 314]);
	}
	else if (idx < 330)
	{
		sprintf(ptr, "Player %d cards %d", who, idx - 318);
	}
	else if (idx < 345)
	{
		sprintf(ptr, "Player %d num devel %d", who, idx - 330);
	}
	else if (idx < 362)
	{
		sprintf(ptr, "Player %d num world %d", who, idx - 345);
	}
	else if (idx < 377)
	{
		sprintf(ptr, "Player %d military %d", who, idx - 362);
	}
	else if (idx < 378)
	{
		sprintf(ptr, "Player %d explore mix");
	}
	else if (idx < 393)
	{
		sprintf(ptr, "Player %d claimed goal %d", who, idx - 378);
	}
	else if (idx < 413)
	{
		sprintf(ptr, "Player %d points behind %d", who, idx - 393);
	}
	else
	{
		sprintf(ptr, "Player %d winner", who);
	}
}

static void input_name(char *ptr, int i)
{
	int index, num;
	char *type;

	if (i == 0)
	{
		sprintf(ptr, "Game over");
	}
	else if (i < 13)
	{
		sprintf(ptr, "VP Pool %d", i - 1);
	}
	else if (i < 25)
	{
		sprintf(ptr, "Most cards %d", i - 13);
	}
	else if (i < 37)
	{
		sprintf(ptr, "Clock %d", i - 25);
	}
	else if (i < 52)
	{
		sprintf(ptr, "Goal %d active", i - 37);
	}
	else if (i < 67)
	{
		sprintf(ptr, "Goal %d available", i - 52);
	}
	else if (i < 218)
	{
		sprintf(ptr, "Player 0 hand %s", library[i - 67].name);
	}
	else
	{
		input_name_player(ptr, (i - 218) / 414, (i - 218) % 414);
	}
}

int main(int argc, char *argv[])
{
	net learner;
	int i, p;
	double start;
	char buf[1024], *ptr;

	read_cards();

	p = atoi(argv[2]);

	make_learner(&learner, 218 + 414 * p, 50, p);

	load_net(&learner, argv[1]);

	for (i = 0; i < 218 + 414 * p; i++) learner.input_value[i] = 0;

	compute_net(&learner);

	start = learner.win_prob[0];

	for (i = 0; i < 218 + 414 * p; i++)
	{
		learner.input_value[i] = 1;

		compute_net(&learner);

		input_name(buf, i);

		for (ptr = buf; *ptr; ptr++) if (*ptr == ' ') *ptr = '_';

		printf("%s: %f\n", buf, learner.win_prob[0] - start);

		learner.input_value[i] = 0;
	}
}
