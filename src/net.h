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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/*
 * A two-layer neural net.
 */
typedef struct net
{
	/* Learning rate */
	double alpha;

	/* Cumulative error */
	double error;

	/* Number of error events (weighted by lambda parameter) */
	double num_error;

	/* Number of inputs */
	int num_inputs;

	/* Number of hidden nodes */
	int num_hidden;

	/* Number of output nodes */
	int num_output;

	/* Hidden layer weights */
	double **hidden_weight;

	/* Accumulated deltas to hidden weights */
	double **hidden_delta;

	/* Output layer weights */
	double **output_weight;

	/* Accumulated deltas to output weights */
	double **output_delta;

	/* Hidden node sums */
	double *hidden_sum;

	/* Cumulative hidden node error */
	double *hidden_error;

	/* Set of input values */
	double *input_value;

	/* Previous input values */
	double *prev_input;

	/* Set of hidden results */
	double *hidden_result;

	/* Set of network results */
	double *net_result;

	/* Set of output probabilities */
	double *win_prob;

	/* Sum that we divide results by to get probablities */
	double prob_sum;

	/* Sets of past inputs */
	double **past_input;

	/* Player who created past inputs */
	int *past_input_player;

	/* Number of past input sets available */
	int num_past;

	/* Training iterations this network has gone through */
	int num_training;

	/* Names of inputs */
	char **input_name;

} net;

/* External functions */
extern void make_learner(net *learn, int inputs, int hidden, int output);
extern void compute_net(net *learn);
extern void store_net(net *learn, int who);
extern void clear_store(net *learn);
extern void train_net(net *learn, double lambda, double *desired);
extern void apply_training(net *learn);
extern void free_net(net *learn);
extern int load_net(net *learn, char *fname);
extern void save_net(net *learn, char *fname);
