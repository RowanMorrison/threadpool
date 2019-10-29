#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#include "threadpool.h"
#include "queue.h"

int n = 0;


int read_lines(FILE *handle, char ***buffer, size_t *buf_len)
{
	int i = 0;
	while (1) {
		if (i * sizeof(char *) >= *buf_len) {
			*buf_len *= 2;
			*buffer = realloc(*buffer, *buf_len);
		}
		char *str = NULL;
		int len = 0;

		if (getline(&str, &len, handle) <= 0)
			break;

		(*buffer)[i] = str;
		i++;
	}
	fclose(handle);
	*buffer = realloc(*buffer, i * sizeof(char *));
	return i;

}

int compare(const void *a, const void *b) {
    const char *sa = *(const char**)a;
    const char *sb = *(const char**)b;

    return strcmp(sa, sb);
}

typedef struct __params {
	void *base;
	size_t num;
} t_params, *p_params;

void to_thread(void *args)
{
	p_params params = (p_params)args;
	qsort(params->base, params->num, sizeof(char *), compare);
	free(args);
}

void stuff(void)
{
	
}


int main(void)
{
	int thread_num = 460;

	threadpool tp = pool_create(thread_num);

	size_t len = 5000 * sizeof(char *);
	char **buffer = malloc(len);
	
	FILE *handle = fopen("E:\\test.txt", "r");
	if (handle == NULL)
		return -1;

	int lines_num = read_lines(handle, &buffer, &len);
	if (lines_num == -1) {
		fprintf(stderr, "read_lines: Nothing to read\n");
		exit(-1);
	}

	// split and enqueue
	// malloc is free'd inside of to_thread()
	{
		int per_thread = lines_num / thread_num;
		for (int i = 0; i < thread_num - 1 && per_thread != 0; i++)
		{

			p_params params = malloc(sizeof(t_params));
			if (params == NULL) {
				fprintf(stderr, "malloc: NULL\n");
				exit(-1);
			}

			params->base = &buffer[i * per_thread];
			params->num = per_thread;

			pool_add_task(tp, to_thread, (void *)params);
			params = NULL;
		}

		// the last one has to get the residue
		p_params params = malloc(sizeof(t_params));
		if (params == NULL) {
			fprintf(stderr, "malloc: NULL\n");
			exit(-1);
		}
		params->base = &buffer[per_thread * (thread_num - 1)];
		params->num = per_thread + lines_num % thread_num;
		pool_add_task(tp, to_thread, (void *)params);
	}

	for (int i = 0; i < lines_num; i++)
		printf("%s", buffer[i]);
	pool_destroy(tp);
}
