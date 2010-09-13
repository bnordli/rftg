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

#include "net.h"

/*
 * Maximum number of previous input sets.
 */
#define PAST_MAX 120

/*
 * Create a random weight value.
 */
static void init_weight(weight *wgt)
{
	/* Initialize weight to random value */
	wgt->value = 0.2 * rand() / RAND_MAX - 0.1;

	/* Clear change */
	wgt->delta = 0;
}

/*
 * Create a network of the given size.
 */
void make_learner(net *learn, int input, int hidden, int output)
{
	int i, j;

	/* Set number of outputs */
	learn->num_output = output;

	/* Set number of inputs */
	learn->num_inputs = input;

	/* Number of hidden nodes */
	learn->num_hidden = hidden;

	/* Clear error counters */
	learn->error = learn->num_error = 0;

	/* Create input array */
	learn->input_value = (double *)malloc(sizeof(double) * (input + 1));

	/* Create array for previous inputs */
	learn->prev_input = (double *)malloc(sizeof(double) * (input + 1));

	/* Create hidden sum array */
	learn->hidden_sum = (double *)malloc(sizeof(double) * hidden);

	/* Create hidden result array */
	learn->hidden_result = (double *)malloc(sizeof(double) * (hidden + 1));

	/* Create hidden error array */
	learn->hidden_error = (double *)malloc(sizeof(double) * hidden);

	/* Create output result array */
	learn->net_result = (double *)malloc(sizeof(double) * output);

	/* Create output probability array */
	learn->win_prob = (double *)malloc(sizeof(double) * output);

	/* Last input and hidden result are always 1 (for bias) */
	learn->input_value[input] = 1.0;
	learn->hidden_result[hidden] = 1.0;

	/* Create rows of hidden weights */
	learn->hidden_weight = (weight **)malloc(sizeof(weight *) *
	                                         (input + 1));

	/* Loop over hidden weight rows */
	for (i = 0; i < input + 1; i++)
	{
		/* Create weight row */
		learn->hidden_weight[i] = (weight *)malloc(sizeof(weight) *
		                                           hidden);

		/* Randomize weights */
		for (j = 0; j < hidden; j++)
		{
			/* Randomize this weight */
			init_weight(&learn->hidden_weight[i][j]);
		}
	}

	/* Create rows of output weights */
	learn->output_weight = (weight **)malloc(sizeof(weight *) *
	                                         (hidden + 1));

	/* Loop over output weight rows */
	for (i = 0; i < hidden + 1; i++)
	{
		/* Create weight row */
		learn->output_weight[i] = (weight *)malloc(sizeof(weight) *
		                                           output);

		/* Randomize weights */
		for (j = 0; j < output; j++)
		{
			/* Randomize this weight */
			init_weight(&learn->output_weight[i][j]);
		}
	}

	/* Clear hidden sums */
	memset(learn->hidden_sum, 0, sizeof(double) * hidden);

	/* Clear hidden errors */
	memset(learn->hidden_error, 0, sizeof(double) * hidden);

	/* Clear previous inputs */
	memset(learn->prev_input, 0, sizeof(double) * (input + 1));

	/* Create set of previous inputs */
	learn->past_input = (double **)malloc(sizeof(double *) * PAST_MAX);

	/* Create set of previous input players */
	learn->past_input_player = (int *)malloc(sizeof(int) * PAST_MAX);

	/* No past inputs available */
	learn->num_past = 0;

	/* No training done */
	learn->num_training = 0;
}

/*
 * Normalize a number using a 'sigmoid' function.
 */
static double sigmoid(double x)
{
	/* Return sigmoid result */
	return tanh(x);
}

/*
 * Compute a neural net's result.
 */
void compute_net(net *learn)
{
	int i, j;
	double sum, adj = 0.0;

	/* Loop over inputs */
	for (i = 0; i < learn->num_inputs + 1; i++)
	{
		/* Check for difference from previous input */
		if (learn->input_value[i] != learn->prev_input[i])
		{
			/* Loop over hidden weights */
			for (j = 0; j < learn->num_hidden; j++)
			{
				/* Adjust sum */
				learn->hidden_sum[j] +=
				              learn->hidden_weight[i][j].value *
				                       (learn->input_value[i] -
				                        learn->prev_input[i]);
			}

			/* Store input */
			learn->prev_input[i] = learn->input_value[i];
		}
	}
	
	/* Normalize hidden node results */
	for (i = 0; i < learn->num_hidden; i++)
	{
		/* Set normalized result */
		learn->hidden_result[i] = sigmoid(learn->hidden_sum[i]);
	}

	/* Clear probability sum */
	learn->prob_sum = 0.0;

	/* Then compute output nodes */
	for (i = 0; i < learn->num_output; i++)
	{
		/* Start sum at zero */
		sum = 0.0;

		/* Loop over hidden results */
		for (j = 0; j < learn->num_hidden + 1; j++)
		{
			/* Add weighted result to sum */
			sum += learn->hidden_result[j] *
			       learn->output_weight[j][i].value;
		}

		/* Check for first node */
		if (!i)
		{
			/* Save adjustment */
			adj = -sum;
		}

		/* Save sum */
		learn->net_result[i] = sum + adj;

		/* Track total output */
		learn->prob_sum += exp(learn->net_result[i]);
	}

	/* Then compute output probabilities */
	for (i = 0; i < learn->num_output; i++)
	{
		/* Compute probability */
		learn->win_prob[i] = exp(learn->net_result[i]) /
		                     learn->prob_sum;
	}
}

/*
 * Store the current inputs into the past set array.
 */
void store_net(net *learn, int who)
{
	int i;

	/* Check for too many past inputs already */
	if (learn->num_past == PAST_MAX)
	{
		/* Destroy oldest set */
		free(learn->past_input[0]);

		/* Move all inputs up one spot */
		for (i = 0; i < PAST_MAX - 1; i++)
		{
			/* Move one set of inputs */
			learn->past_input[i] = learn->past_input[i + 1];

			/* Move one player index */
			learn->past_input_player[i] =
			                  learn->past_input_player[i + 1];
		}

		/* We now have one fewer set */
		learn->num_past--;
	}

	/* Make space for new inputs */
	learn->past_input[learn->num_past] = malloc(sizeof(double) *
	                                            (learn->num_inputs + 1));

	/* Copy inputs */
	memcpy(learn->past_input[learn->num_past], learn->input_value,
	       sizeof(double) * (learn->num_inputs + 1));

	/* Copy player index */
	learn->past_input_player[learn->num_past] = who;

	/* One additional set */
	learn->num_past++;
}

/*
 * Clean up past stored inputs.
 */
void clear_store(net *learn)
{
	int i;

	/* Loop over previous stored inputs */
	for (i = 0; i < learn->num_past; i++)
	{
		/* Free inputs */
		free(learn->past_input[i]);
	}

	/* Clear number of past inputs */
	learn->num_past = 0;
}

/*
 * Train a network so that the current results are more like the desired.
 */
void train_net(net *learn, double lambda, double *desired)
{
	int i, j, k;
	double error, corr, deriv, hderiv;
	double *hidden_corr;

	/* Count error events */
	learn->num_error += lambda;

	/* Loop over output nodes */
	for (i = 0; i < learn->num_output; i++)
	{
		/* Compute error */
		error = lambda * (learn->win_prob[i] - desired[i]);

		/* Accumulate squared error */
		learn->error += error * error;

		/* Output portion of partial derivatives */
		deriv = learn->win_prob[i] * (1.0 - learn->win_prob[i]);

		/* Loop over node's weights */
		for (j = 0; j < learn->num_hidden; j++)
		{
			/* Compute correction */
			corr = -error * learn->hidden_result[j] * deriv;

			/* Compute hidden node's effect on output */
			hderiv = deriv * learn->output_weight[j][i].value;

			/* Loop over other output nodes */
			for (k = 0; k < learn->num_output; k++)
			{
				/* Skip this output node */
				if (i == k) continue;

				/* Subtract this node's factor */
				hderiv -= learn->output_weight[j][k].value *
				          exp(learn->net_result[i] +
				              learn->net_result[k]) /
				          (learn->prob_sum * learn->prob_sum);
			}

			/* Compute hidden node's error */
			learn->hidden_error[j] += error * hderiv;

			/* Apply correction */
			learn->output_weight[j][i].delta += learn->alpha * corr;
		}

		/* Compute bias weight's correction */
		learn->output_weight[j][i].delta += learn->alpha * -error *
		                                    deriv;
	}

	/* Create array of hidden weight correction factors */
	hidden_corr = (double *)malloc(sizeof(double) * learn->num_hidden);

	/* Loop over hidden nodes */
	for (i = 0; i < learn->num_hidden; i++)
	{
		/* Output portion of partial derivatives */
		deriv = 1 - (learn->hidden_result[i] * learn->hidden_result[i]);

		/* Calculate correction factor */
		hidden_corr[i] = deriv * -learn->hidden_error[i] * learn->alpha;
	}

	/* Loop over inputs */
	for (i = 0; i < learn->num_inputs + 1; i++)
	{
		/* Skip zero inputs */
		if (!learn->input_value[i]) continue;

		/* Loop over hidden nodes */
		for (j = 0; j < learn->num_hidden; j++)
		{
			/* Adjust weight */
			learn->hidden_weight[i][j].delta += hidden_corr[j] *
			                                  learn->input_value[i];
		}
	}

	/* Destroy hidden correction factor array */
	free(hidden_corr);

	/* Loop over hidden nodes */
	for (i = 0; i < learn->num_hidden; i++)
	{
		/* Clear node's error */
		learn->hidden_error[i] = 0;

		/* Clear node's stored sum */
		learn->hidden_sum[i] = 0;
	}

	/* Clear previous inputs */
	memset(learn->prev_input, 0, sizeof(double) * (learn->num_inputs + 1));

#ifdef NOISY
	compute_net();
	for (i = 0; i < learn->num_output; i++)
	{
		printf("%lf -> %lf: %lf\n", orig[i], desired[i], learn->win_prob[i]);
	}
#endif
}

/*
 * Apply accumulated training information.
 */
void apply_training(net *learn)
{
	int i, j;

	/* Loop over hidden nodes */
	for (i = 0; i < learn->num_hidden + 1; i++)
	{
		/* Loop over output nodes */
		for (j = 0; j < learn->num_output; j++)
		{
			/* Apply training */
			learn->output_weight[i][j].value +=
			                       learn->output_weight[i][j].delta;

			/* Clear delta */
			learn->output_weight[i][j].delta = 0;
		}
	}

	/* Loop over input values */
	for (i = 0; i < learn->num_inputs + 1; i++)
	{
		/* Loop over hidden nodes */
		for (j = 0; j < learn->num_hidden; j++)
		{
			/* Apply training */
			learn->hidden_weight[i][j].value +=
			                       learn->hidden_weight[i][j].delta;

			/* Clear delta */
			learn->hidden_weight[i][j].delta = 0;
		}
	}
}

/*
 * Load network weights from disk.
 */
int load_net(net *learn, char *fname)
{
	FILE *fff;
	int i, j;
	int input, hidden, output;

	/* Open weights file */
	fff = fopen(fname, "r");

	/* Check for failure */
	if (!fff) return -1;

	/* Read network size from file */
	fscanf(fff, "%d %d %d\n", &input, &hidden, &output);

	/* Check for mismatch */
	if (input != learn->num_inputs ||
	    hidden != learn->num_hidden ||
	    output != learn->num_output) return -1;

	/* Read number of training iterations */
	fscanf(fff, "%d\n", &learn->num_training);

	/* Loop over hidden nodes */
	for (i = 0; i < learn->num_hidden; i++)
	{
		/* Loop over weights */
		for (j = 0; j < learn->num_inputs + 1; j++)
		{
			/* Load a weight */
			if (fscanf(fff, "%lf\n",
			           &learn->hidden_weight[j][i].value) != 1)
			{
				/* Failure */
				return -1;
			}
		}
	}

	/* Loop over output nodes */
	for (i = 0; i < learn->num_output; i++)
	{
		/* Loop over weights */
		for (j = 0; j < learn->num_hidden + 1; j++)
		{
			/* Load a weight */
			if (fscanf(fff, "%lf\n",
			           &learn->output_weight[j][i].value) != 1)
			{
				/* Failure */
				return -1;
			}
		}
	}

	/* Done */
	fclose(fff);

	/* Success */
	return 0;
}

/*
 * Save network weights to disk.
 */
void save_net(net *learn, char *fname)
{
	FILE *fff;
	int i, j;

	/* Open output file */
	fff = fopen(fname, "w");

	/* Save network size */
	fprintf(fff, "%d %d %d\n", learn->num_inputs, learn->num_hidden,
	                           learn->num_output);

	/* Save training iterations */
	fprintf(fff, "%d\n", learn->num_training);

	/* Loop over hidden nodes */
	for (i = 0; i < learn->num_hidden; i++)
	{
		/* Loop over weights */
		for (j = 0; j < learn->num_inputs + 1; j++)
		{
			/* Save a weight */
			fprintf(fff, "%.12le\n",
			        learn->hidden_weight[j][i].value);
		}
	}

	/* Loop over output nodes */
	for (i = 0; i < learn->num_output; i++)
	{
		/* Loop over weights */
		for (j = 0; j < learn->num_hidden + 1; j++)
		{
			/* Save a weight */
			fprintf(fff, "%.12le\n",
			        learn->output_weight[j][i].value);
		}
	}

	/* Done */
	fclose(fff);
}
