#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>


//each customer from the input file
typedef struct {
    int id;
    //0 = economy, 1 = buisness
    int class_type; 
    //in 10ths of a second
    int arrival_time;
    int service_time;
    //queue enters
    double real_arrival_time; 
    // when a clerk picks up
    double start_service_time;
} Customer;

int total_customers = 0;
Customer customers[1000];

//store the customer IDs here
int business_queue[1000];
int business_queue_count = 0;
int economy_queue[1000];
int economy_queue_count = 0;

int customers_served = 0;
struct timeval start_time;

// Synchronization tools
pthread_mutex_t system_mutex;
pthread_cond_t clerk_cv;
pthread_cond_t customer_cvs[1000]; 
int customer_ready_for_clerk[1000]; 


// gets time since start
double get_current_time() {
    struct timeval now;
    gettimeofday(&now, NULL);
    double seconds = (double)(now.tv_sec - start_time.tv_sec);
    double microseconds = (double)(now.tv_usec - start_time.tv_usec);
    return seconds + (microseconds / 1000000.0);
}


void* clerk_function(void* arg) {
    int clerk_id = *(int*)arg;
    free(arg);

    while (1) {
        pthread_mutex_lock(&system_mutex);

        // no one is in either queue = the clerk sleeps
        while (business_queue_count == 0 && economy_queue_count == 0) {
            // End thread if all customers are finished
            if (customers_served >= total_customers) {
                pthread_mutex_unlock(&system_mutex);
                return NULL;
            }
            pthread_cond_wait(&clerk_cv, &system_mutex);
        }

        // business class has priority
        int selected_customer_id = -1;
        if (business_queue_count > 0) {
            selected_customer_id = business_queue[0];
            // shift queue left
            for (int i = 0; i < business_queue_count - 1; i++) {
                business_queue[i] = business_queue[i+1];
            }
            business_queue_count--;
        } else if (economy_queue_count > 0) {
            selected_customer_id = economy_queue[0];
            // shift queue left
            for (int i = 0; i < economy_queue_count - 1; i++) {
                economy_queue[i] = economy_queue[i+1];
            }
            economy_queue_count--;
        }

        if (selected_customer_id != -1) {
            Customer* c = &customers[selected_customer_id - 1];
            c->start_service_time = get_current_time();
            
            printf("A clerk starts serving a customer: start time %.2f, the customer ID %2d, the clerk ID %1d. \n", 
                   c->start_service_time, c->id, clerk_id);

            // wake up the specific customer
            customer_ready_for_clerk[selected_customer_id] = 1;
            pthread_cond_signal(&customer_cvs[selected_customer_id]);
            pthread_mutex_unlock(&system_mutex);

            usleep(c->service_time * 100000); 

            printf("A clerk finishes serving a customer: end time %.2f, the customer ID %2d, the clerk ID %1d. \n", 
                   get_current_time(), c->id, clerk_id);

            pthread_mutex_lock(&system_mutex);
            customers_served++;
            pthread_mutex_unlock(&system_mutex);
        } else {
            pthread_mutex_unlock(&system_mutex);
        }
    }
}


void* customer_function(void* arg) {
    Customer* c = (Customer*)arg;

    // wait until arrival time
    usleep(c->arrival_time * 100000);

    pthread_mutex_lock(&system_mutex);
    printf("A customer arrives: customer ID %2d. \n", c->id);

    // enter queue 
    if (c->class_type == 1) {
        business_queue[business_queue_count] = c->id;
        business_queue_count++;
        printf("A customer enters a queue: the queue ID 1, and length of the queue %2d. \n", business_queue_count);
    } else {
        economy_queue[economy_queue_count] = c->id;
        economy_queue_count++;
        printf("A customer enters a queue: the queue ID 0, and length of the queue %2d. \n", economy_queue_count);
    }

    c->real_arrival_time = get_current_time();

    // alerrt clerks someone is here
    pthread_cond_broadcast(&clerk_cv);

    // wait until a clerk picks customer
    while (customer_ready_for_clerk[c->id] == 0) {
        pthread_cond_wait(&customer_cvs[c->id], &system_mutex);
    }

    pthread_mutex_unlock(&system_mutex);
    return NULL;
}


int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: ./ACS customers.txt\n");
        return 1;
    }

    // initialize mutex , start time
    if (pthread_mutex_init(&system_mutex, NULL) != 0) return 1;
    gettimeofday(&start_time, NULL);

    // read file
    FILE* file = fopen(argv[1], "r");
    if (!file) return 1;

    fscanf(file, "%d", &total_customers);
    for (int i = 0; i < total_customers; i++) {
        // ID:Class,Arrival,Service 
        fscanf(file, "%d:%d,%d,%d", &customers[i].id, &customers[i].class_type, 
               &customers[i].arrival_time, &customers[i].service_time);
        
        if (customers[i].arrival_time < 0 || customers[i].service_time < 0) {
            printf("Error: Illegal time values.\n");
            return 1;
        }
        pthread_cond_init(&customer_cvs[customers[i].id], NULL);
        customer_ready_for_clerk[customers[i].id] = 0;
    }
    fclose(file);

    pthread_cond_init(&clerk_cv, NULL);

    // create 5 clerk threads
    pthread_t clerks[5];
    for (int i = 0; i < 5; i++) {
        int* id = malloc(sizeof(int));
        *id = i;
        pthread_create(&clerks[i], NULL, clerk_function, id);
    }

    // create customer threads
    pthread_t cust_threads[total_customers];
    for (int i = 0; i < total_customers; i++) {
        pthread_create(&cust_threads[i], NULL, customer_function, &customers[i]);
    }

    // wait for all customers to finish
    for (int i = 0; i < total_customers; i++) {
        pthread_join(cust_threads[i], NULL);
    }

    // wake up clerks to finish and exit
    pthread_mutex_lock(&system_mutex);
    pthread_cond_broadcast(&clerk_cv);
    pthread_mutex_unlock(&system_mutex);

    for (int i = 0; i < 5; i++) {
        pthread_join(clerks[i], NULL);
    }

    double total_wait = 0, business_wait = 0, economy_wait = 0;
    int b_count = 0, e_count = 0;

    for (int i = 0; i < total_customers; i++) {
        double wait = customers[i].start_service_time - customers[i].real_arrival_time;
        total_wait += wait;
        if (customers[i].class_type == 1) {
            business_wait += wait;
            b_count++;
        } else {
            economy_wait += wait;
            e_count++;
        }
    }

    printf("The average waiting time for all customers in the system is: %.2f seconds.\n", total_wait / total_customers);
    if (b_count > 0) printf("The average waiting time for all business-class customers is: %.2f seconds.\n", business_wait / b_count);
    if (e_count > 0) printf("The average waiting time for all economy-class customers is: %.2f seconds.\n", economy_wait / e_count);

    pthread_mutex_destroy(&system_mutex);
    return 0;
}