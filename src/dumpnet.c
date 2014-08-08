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

#include "net.h"

int main(int argc, char *argv[])
{
	net learner;
	FILE *fff;
	int input, hidden, output;
	int i, j;
	double *start;
	char buf[1024], *ptr;

	fff = fopen(argv[1], "r");

	fgets(buf, 1024, fff);

	fclose(fff);

	sscanf(buf, "%d %d %d", &input, &hidden, &output);

	make_learner(&learner, input, hidden, output);

	load_net(&learner, argv[1]);

	for (i = 0; i < input; i++) learner.input_value[i] = -1;

	compute_net(&learner);

	start = (double *)malloc(sizeof(double) * output);

	for (i = 0; i < output; i++)
	{
		start[i] = learner.win_prob[i];
	}

	for (i = 0; i < input; i++)
	{
		learner.input_value[i] = 1;

		compute_net(&learner);

		strcpy(buf, learner.input_name[i]);

		for (ptr = buf; *ptr; ptr++) if (*ptr == ' ') *ptr = '_';

		printf("%s: ", buf);

		for (j = 0; j < output; j++)
		{
			printf("%f ", learner.win_prob[j] - start[j]);
		}
		
		printf("\n");

		learner.input_value[i] = -1;
	}

	return 0;
}
