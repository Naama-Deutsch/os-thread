#include <dirent.h>
#include <stdio.h>
#include <sys/types.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>

typedef struct queue {
    char* directory;
    struct queue* next;
}queue;

/////// global vars. ///////
queue* head = NULL;
queue* tail = NULL;

int count=0;

pthread_mutex_t mutex_lock;
pthread_mutex_t mutex_lock_main;
pthread_cond_t full_queue;
pthread_cond_t no_finish;

int threads_number=0;
char print_search=0;

//////////////////////////

void enqueue(char* directory_name)
{
    
	pthread_mutex_lock(&mutex_lock);

    if (!head)
    {
        head = malloc(sizeof(queue));
        head->directory = malloc(sizeof(char) * strlen(directory_name) + 1);
        strcpy(head->directory, directory_name);
        head->next = NULL;
        tail = head;
    }

    else
    {
        tail->next = malloc(sizeof(queue));
        tail->next->directory = malloc(sizeof(char) * strlen(directory_name) + 1);
        strcpy(tail->next->directory, directory_name);
        tail->next->next = NULL;
        tail = tail->next;
    }

    pthread_cond_signal(&full_queue);
    pthread_mutex_unlock(&mutex_lock);

	pthread_testcancel();
}

char* dequeue()
{
    pthread_mutex_lock(&mutex_lock);

    while (!head)
    {
        if (threads_number == 1)
        {
            pthread_cond_signal(&no_finish);
        }
            __sync_fetch_and_sub(&threads_number, 1);
            pthread_cond_wait(&full_queue, &mutex_lock);
            __sync_fetch_and_add(&threads_number, 1);
    }
	
    queue* tmp = head;
    head = head->next;

    if (!head)
    {
        tail = NULL;
    }
	
	pthread_testcancel();

    pthread_mutex_unlock(&mutex_lock);
    char* tmp_name = tmp->directory;
    free(tmp);
    return tmp_name;
}

void finish_by_ctrl_c()
{
	while (head)
	{
		queue* tmp = head;
		head=head->next;
		free(tmp->directory);
		free(tmp);
	}

	print_search=1;
	pthread_cond_signal(&no_finish);
}

void* browse(void* str)
{

    struct sigaction act;
	act.sa_handler = finish_by_ctrl_c;
	sigaction(SIGINT,&act, NULL);

	char* direct_name = dequeue();
    DIR* dir = opendir(direct_name);
    struct dirent* entry;
    struct stat dir_stat;
    
	if (!dir)
	{
		perror(direct_name);
		return NULL;
	}

    while ((entry = readdir(dir)) != NULL)
	{
        char buff[strlen(direct_name) + strlen(entry->d_name) + 2];
        sprintf(buff, "%s/%s", direct_name, entry->d_name);
        stat(buff, &dir_stat);
		
        if (strcmp(entry->d_name, ".."))
		{

            if (((dir_stat.st_mode & S_IFMT) == S_IFDIR) && strcmp(entry->d_name, ".") )
            {
                enqueue(buff);
            }

            else
			{
                if (strstr(buff, str))
    			{
        			__sync_fetch_and_add(&count, 1);
					printf("%s\n", buff);
    			}
            }

        }
    }
	
	pthread_testcancel();

    free(direct_name);
    closedir(dir);
    browse(str);
}


int main(int argc, char* argv[])
{
	size_t i = 0;

    int size = atoi(argv[3]);

    threads_number = size;

    enqueue(argv[1]);

    pthread_mutex_init(&mutex_lock,NULL);
    pthread_mutex_init(&mutex_lock_main,NULL);
    pthread_cond_init(&full_queue,NULL);
    pthread_cond_init(&no_finish,NULL);
    pthread_t thread_arr[threads_number];
    
	int rc;

    for (i; i < size; ++i)
    {
        rc = pthread_create(&thread_arr[i], NULL, browse, argv[2]);
        
		if (rc)
        {
            printf("ERROR in pthread_create(): %s\n", strerror(rc));
            exit(-1);
        }
    }

    pthread_mutex_lock(&mutex_lock_main);
    pthread_cond_wait(&no_finish, &mutex_lock);
    pthread_mutex_unlock(&mutex_lock_main);

    for (i = 0; i < size; ++i)
    {
        pthread_cancel(thread_arr[i]);
		pthread_detach(thread_arr[i]);
    }

	if (print_search)
	{
		printf("Search stopped, found %d files\n",count);
	}

	else
	{
		printf("Done searching, found %d files\n",count);
	}

	pthread_mutex_destroy(&mutex_lock);
    pthread_mutex_destroy(&mutex_lock_main);
    pthread_cond_destroy(&full_queue);
    pthread_cond_destroy(&no_finish);
	
    return 0;
}
