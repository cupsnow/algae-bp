#include <stdio.h>
#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include "fcgiapp.h"

#define log_m(_lv, _fmt, _args...) printf(_lv "[%s][#%d]" _fmt, __func__, __LINE__, ##_args)
#define log_d(...) log_m("[Debug]", ##__VA_ARGS__)

#define THREAD_COUNT 20
static int counts[THREAD_COUNT];

static void *doit(void *a)
{
    int rc, i, thread_id = (int)(unsigned long)a;
    pid_t pid = getpid();
    FCGX_Request request;
    char *server_name;

    FCGX_InitRequest(&request, 0, 0);

    for (;;)
    {
        static pthread_mutex_t accept_mutex = PTHREAD_MUTEX_INITIALIZER;
        static pthread_mutex_t counts_mutex = PTHREAD_MUTEX_INITIALIZER;

        /* Some platforms require accept() serialization, some don't.. */
        pthread_mutex_lock(&accept_mutex);
        rc = FCGX_Accept_r(&request);
        pthread_mutex_unlock(&accept_mutex);

        if (rc < 0)
            break;

        server_name = FCGX_GetParam("SERVER_NAME", request.envp);

        FCGX_FPrintF(request.out,
            "Content-type: text/html\r\n"
            "\r\n"
            "<title>FastCGI Hello! (multi-threaded C, fcgiapp library)</title>"
            "<h1>FastCGI Hello! (multi-threaded C, fcgiapp library)</h1>"
            "Thread %d, Process %ld<p>"
            "Request counts for %d threads running on host <i>%s</i><p><code>",
            thread_id, pid, THREAD_COUNT, server_name ? server_name : "?");

        sleep(2);

        pthread_mutex_lock(&counts_mutex);
        ++counts[thread_id];
        for (i = 0; i < THREAD_COUNT; i++)
            FCGX_FPrintF(request.out, "%5d " , counts[i]);
        pthread_mutex_unlock(&counts_mutex);

        FCGX_Finish_r(&request);
    }

    return NULL;
}

static int test_threaded(void*, int, char**) {
	int ret = -1, i;
    pthread_t id[THREAD_COUNT];

    FCGX_Init();

    for (i = 1; i < THREAD_COUNT; i++)
        pthread_create(&id[i], NULL, doit, (void*)i);

    doit(0);
    ret = 0;
    return ret;
}

int main(int argc, char **argv) {
    for (int i = 0; i < argc; i++) {
        log_d("argv[%d/%d]: %s\n", i + 1, argc, argv[i]);
    }
        
    return 0;
}
