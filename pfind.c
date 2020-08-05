#include <dirent.h>
#include <stdio.h>
#include <sys/types.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdlib.h>
#include <fnmatch.h>
#include <unistd.h>
#include <signal.h>
/*-------global varibals----------*/
typedef struct node{
    char* name_dir;
    struct node* next;
}node;
node* global_queue_head = NULL;
node* global_queue_tail = NULL;
pthread_mutex_t lock;
pthread_mutex_t lock_cv;
pthread_cond_t noEmpty;
pthread_cond_t try;
int NUM_OF_WAKE_T = 0;
int NUM_OF_FOUND_FILE = 0 ;
int flag = 0;
/*-------------auxiliary check_success functions-------------------*/

void check_valid_input(int num) {
    if (num < 4) {
		fprintf(stderr,"invalid input\n");
		exit(-1);
	}
}
void check_status_thread(int status){
    if(status!=0){
        fprintf(stderr,"error create thread");
        exit(-1);
    }
}
void check_mutex(int status){
    if(status!=0)
    {
        fprintf(stderr,"error init mutex");
        exit(-1);
    }
}
void check_valid_root(char * root){
    DIR * dir = opendir(root);
    if(!dir){
        fprintf(stderr,"error open dir");
        exit(-1);
    }
}
/*---------------------------------------------------*/

int check_form_in_file(const char* file_name, const char * form){

    int len1 = strlen(file_name);
    int len2 = strlen(form);
    for (int i = 0; i < len1-len2; ++i) {
        int k;
        for ( k = 0; k < len2; ++k) {
            if (file_name[i+k]!=form[k])
                break;
        }
        if(k==len2)
            return 1;/* && strcmp(file_name,'.')*/
    }
    return 0;
}

node* create_new_node(char* path){

    node* new_node = (node*)malloc(sizeof(node));
    new_node->name_dir=malloc(sizeof(char)*strlen(path)+1);
    strcpy(new_node->name_dir,path);
    new_node->next = NULL;
    return new_node;
}
void free_resources()
{
    if(global_queue_head==NULL)
        return;;
    node* tmp = global_queue_head;
    global_queue_head = global_queue_head->next;
    free(tmp);
    free_resources();
}
/*-------------------signal functions------------------------*/
void sigint_cancelation(int num) {
    flag = 1;
    pthread_cond_signal(&try);
}

int register_signal_handling() {
    struct sigaction new_action;
    memset(&new_action, 0, sizeof(new_action));
    new_action.sa_handler = sigint_cancelation;
    return sigaction(SIGINT, &new_action, NULL);
}

void thread_error(){
    printf("im thread number %lu finish my job due to error opening file",pthread_self());
    int num_error = 1;
    pthread_exit(&num_error);
}
/*----------------------------------------------------------*/

/*----------------queue auxiliary functions-----------------------*/
int is_under_flow(){
    return global_queue_head==NULL;
}

void enqueue(char * path){

    node* new_node = create_new_node(path);
    pthread_mutex_lock(&lock);
    if(is_under_flow()){
        global_queue_head = new_node;
        global_queue_tail = global_queue_head;
    }
    else{
        global_queue_tail->next = new_node;
        global_queue_tail = new_node;
    }
    pthread_cond_signal(&noEmpty);
    pthread_mutex_unlock(&lock);

}

char* dequeue(){

    pthread_mutex_lock(&lock);
    while (global_queue_head==NULL){
        if(NUM_OF_WAKE_T==1) //check if all the threads are sleep and if true sent conditional signal
        {
            pthread_cond_signal(&try);
        }
        __sync_fetch_and_sub(&NUM_OF_WAKE_T,1);
        pthread_cond_wait(&noEmpty,&lock);
        __sync_fetch_and_add(&NUM_OF_WAKE_T,1);
    }
    node* res = global_queue_head;
    global_queue_head = res->next;
    if(is_under_flow()){
        global_queue_tail=NULL;
    }
    pthread_mutex_unlock(&lock);
    char * name = res->name_dir;
    free(res); //free resources
    return name;
}
/*---------------------------------------*/

void* do_thread(void* argv){

    register_signal_handling();
    pthread_testcancel();
    char * currency_path = dequeue();
    DIR * dir = opendir(currency_path);
    if(!dir){
        thread_error();
    }
    struct dirent * entry;
    struct  stat path_status;
    pthread_testcancel();
   /*------read directories-----*/
    while((entry=readdir(dir)) != NULL){
       char buffer[strlen(currency_path)+strlen(entry->d_name) + 2];
       sprintf(buffer, "%s/%s" , currency_path , entry->d_name);
       stat(buffer , &path_status);
        /*check if the file name is . or ..*/
       if(strcmp(entry->d_name,".."))
       {
           /*-----check if is directory-----*/
           if(((path_status.st_mode & S_IFMT)==S_IFDIR)&& (strcmp(entry->d_name,"."))){
               enqueue(buffer);
               pthread_testcancel();
           }
           else{
               /*check if its file if there is the correct form*/
               if(check_form_in_file(entry->d_name, ((char*)argv))){
                    printf("%s \n ",buffer);
			        __sync_fetch_and_add(&NUM_OF_FOUND_FILE,1);
                    pthread_testcancel();
                }
           }
       }
   }
    free(currency_path);
    closedir(dir);
    do_thread(argv);
    pthread_exit(0);
}

void multi_threading(char* pattern, int num_of_thread){

    pthread_t thread[num_of_thread];
    NUM_OF_WAKE_T = num_of_thread;
    int status;
    /*------------init mutex----------------*/
    status = pthread_mutex_init(&lock,NULL);
    check_mutex(status);
    status = pthread_mutex_init(&lock_cv,NULL);
    check_mutex(status);
    status = pthread_cond_init(&noEmpty,NULL);
    status = pthread_cond_init(&try,NULL);
    /*---------------lunch threads-----------*/
    for (int i = 0; i <num_of_thread ; ++i) {
        status = pthread_create(&thread[i],NULL,&do_thread,(void*)pattern);
        check_status_thread(status);
    }
    /*---------------wait for threads-------*/
    pthread_mutex_lock(&lock_cv);
    pthread_cond_wait(&try,&lock_cv);
    pthread_mutex_unlock(&lock_cv);
    pthread_mutex_unlock(&lock);
    for (int j = 0; j < num_of_thread; ++j) {
        pthread_cancel(thread[j]);
    }
    for (int j = 0; j < num_of_thread; ++j) {
        pthread_detach(thread[j]);
    }
    if(flag==0)// check if he exit due to error or due to sigint
    {
        printf("Done searching, found %d files \n",NUM_OF_FOUND_FILE );
    }
	else{
	    free_resources();
        printf("search stopped, found %d files \n",NUM_OF_FOUND_FILE );
	}
    /*-------------destroy mutexes------------*/
    pthread_mutex_destroy(&lock);
    pthread_mutex_destroy(&lock_cv);
    pthread_cond_destroy(&noEmpty);
    pthread_cond_destroy(&try);
}

int main(int argc, char** argv) {
/*notification: in order to check the sigint action i sleep the pthread_func every 2 minutes*/
	check_valid_input(argc);
	check_valid_root(argv[1]);
	enqueue(argv[1]);
	multi_threading(argv[2],atoi(argv[3]));
}