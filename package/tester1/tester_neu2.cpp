/* $Id$
 *
 * Copyright (c) 2025, joelai
 * All Rights Reserved
 *
 * SPDX-License-Identifier: MIT
 *
 * @file noname
 * @brief noname
 */

#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h> // For TCP_KEEPIDLE, TCP_KEEPINTVL, etc. (Linux)
#include "priv.h"

typedef struct {
	double a, b;
} lr_t;

static double predict(lr_t *lr, double x) {
	return lr->a * x + lr->b;
}

static double error_calc(lr_t *lr, double dataset[], int n) {
	double sum = 0;

	for (int i = 0; i < n; i++) {
		double d = predict(lr, i) - dataset[i];
		sum += d * d;
	}
	return sum;
}

static double gradient_a_calc(lr_t *lr, double dataset[], int n) {
	lr_t lr2 = *lr;
	double step = 0.01;
	double err = error_calc(&lr2, dataset, n);
	double gradient_a;

	lr2.a += step;
	gradient_a = (error_calc(&lr2, dataset, n) - err) / step;
	return gradient_a;
}

static double gradient_b_calc(lr_t *lr, double dataset[], int n) {
	lr_t lr2 = *lr;
	double step = 0.01;
	double err = error_calc(&lr2, dataset, n);
	double gradient_b;

	lr2.b += step;
	gradient_b = (error_calc(&lr2, dataset, n) - err) / step;
	return gradient_b;
}

static void gradient_descent(void) {
	lr_t lr = {};
	double err, dataset[] = {3, 5, 7, 9}, learning_rate = 0.01;
	int n = aloe_arraysize(dataset);

	err = error_calc(&lr, dataset, n);
	log_d("[loop %d] a: %.02f, b: %.02f, err: %.02f\n", 0, lr.a, lr.b, err);
	for (int loop = 0; loop < 10000; loop++) {
		double ga, gb;
		ga = gradient_a_calc(&lr, dataset, n);
		gb = gradient_b_calc(&lr, dataset, n);
		lr.a -= learning_rate * ga;
		lr.b -= learning_rate * gb;
		err = error_calc(&lr, dataset, n);
		log_d("[loop %d] a: %.02f, b: %.02f, err: %.02f\n", loop + 1, lr.a, lr.b, err);
		if (err <= 0.001) break;
	}
}

typedef struct {
	int input_count;
	double *weight;
	double bias;
} neu2_neuron_t;

typedef struct {
	int neuron_count;
	neu2_neuron_t *neuron;
} neu2_layer_t;

typedef struct {
	int layer_count;
	neu2_layer_t *layer;
} neu2_network_t;

double neuron_forward(const double input[], const double weight[], int n,
		double bias) {
	double sum = bias;
	int i;

	for (i = 0; i < n; i++) sum += input[i] * weight[i];
	return sum;
}

double neuron_relu(double x) {
	return x <= 0.0 ? 0.0 : x;
}

double neuron_forward_relu(const double input[], const double weight[], int n,
		double bias) {
	return neuron_relu(neuron_forward(input, weight, n, bias));
}

static void neuron_relu2(void) {
	double input[] = {-10, -1, 0, 8};
	for (int i = 0; i < aloe_arraysize(input); i++) {
		log_d("relu[%d/%d] %f -> %f\n", i + 1, (int)aloe_arraysize(input),
				input[i], neuron_relu(input[i]));
	}
}

typedef struct {
	int input_count, output_count;
	double *weight;
	double *bias;
} neu2_layer2_t;

static void layer2_forward(neu2_layer2_t *lr2, const double input[],
		double output[]) {
	int idx_out, weight_offset, idx_in;

	for (idx_out = weight_offset = 0; idx_out < lr2->output_count;
			idx_out++, weight_offset += lr2->input_count) {
		double sum = lr2->bias[idx_out];

		for (idx_in = 0; idx_in < lr2->input_count; idx_in++) {
			sum += input[idx_in] * lr2->weight[weight_offset + idx_in];
		}
		output[idx_out] = sum;
	}
}

static double layer2_error(neu2_layer2_t *lr2, double dataset[], int n) {
	double sum = 0;

	if (lr2->input_count != 1 || lr2->output_count != 1) {
		log_e("Sanity check invalid layer\n");
		return -1;
	}

	for (int i = 0; i < n; i++) {
		double d, input[1], output[1];

		input[0] = i;
		layer2_forward(lr2, input, output);
		d = output[0] - dataset[i];
		sum += (d * d);
	}
	return sum;
}

static double layer2_gradient_weight(neu2_layer2_t *lr2, double dataset[], int n) {
	double step = 0.01, err, weight[1], bias[1], grad;
	neu2_layer2_t lr2_r = *lr2;

	if (lr2->input_count != 1 || lr2->output_count != 1) {
		log_e("Sanity check invalid layer\n");
		return -1;
	}

	err = layer2_error(lr2, dataset, n);

	memcpy(weight, lr2->weight, lr2->input_count * lr2->output_count * sizeof(lr2->weight[0]));
	memcpy(bias, lr2->bias, lr2->output_count * sizeof(lr2->bias[0]));
	lr2_r.weight = weight;
	lr2_r.bias = bias;
	for (int i = 0; i < 1; i++) {
		for (int j = 0; j < 1; j++) {
			lr2_r.weight[i * lr2_r.output_count + j] += step;
		}
	}
	grad = (layer2_error(&lr2_r, dataset, n) - err) / step;
	return grad;
}

static double layer2_gradient_bias(neu2_layer2_t *lr2, double dataset[], int n) {
	double step = 0.01, err, weight[1], bias[1], grad;
	neu2_layer2_t lr2_r = *lr2;

	if (lr2->input_count != 1 || lr2->output_count != 1) {
		log_e("Sanity check invalid layer\n");
		return -1;
	}

	err = layer2_error(lr2, dataset, n);

	memcpy(weight, lr2->weight, lr2->input_count * lr2->output_count * sizeof(lr2->weight[0]));
	memcpy(bias, lr2->bias, lr2->output_count * sizeof(lr2->bias[0]));
	lr2_r.weight = weight;
	lr2_r.bias = bias;
	for (int i = 0; i < 1; i++) {
		lr2_r.bias[i] += step;
	}
	grad = (layer2_error(&lr2_r, dataset, n) - err) / step;
	return grad;
}

static void layer2_gradient_descent(void) {
	double weight[1], bias[1];
	neu2_layer2_t layer = {};
	double dataset[] = {3, 5, 7, 9}, err, learning_rate = 0.01;
	int n = aloe_arraysize(dataset);

	layer.weight = weight;
	layer.bias = bias;
	layer.output_count = 1;
	layer.input_count = 1;
	err = layer2_error(&layer, dataset, n);
	log_d("loop%d a: %.02f, b: %.02f, err: %.02f\n", 0, layer.weight[0], layer.bias[0], err);
	for (int loop = 0; loop < 10000; loop++) {
		double gweight, gbias;
		gweight = layer2_gradient_weight(&layer, dataset, n);
		gbias = layer2_gradient_bias(&layer, dataset, n);
		weight[0] -= learning_rate * gweight;
		bias[0] -= learning_rate * gbias;
		err = layer2_error(&layer, dataset, n);
		log_d("loop%d a: %.02f, b: %.02f, err: %.02f\n", loop + 1, layer.weight[0], layer.bias[0], err);
		if (err <= 0.001) break;
	}
}

extern "C" int cli_cmd_neu2(void*, int argc, const char **argv) {
	dump_argv(argc, argv);

//	gradient_descent();
//	neuron_forward();
//	neuron_relu2();
	layer2_gradient_descent();
	return 0;
}

__attribute__((weak)) int main(int argc, char **argv) {
	return cli_cmd_neu2(NULL, argc, (const char**)argv);
}
