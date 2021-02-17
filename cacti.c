
// Grzegorz B. Zaleski (418494)

#include "cacti.h"
#include <pthread.h>
#include <semaphore.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include "err.h"

// Początkowy rozmiar kolejek, podwajany przy zapełnieniu.
#define BASESIZE 512

// Struktura każdego aktora z danymi.
typedef struct actor_data
{
    pthread_mutex_t single_lock;
    void *stateptr;
    actor_id_t own_id;

    role_t role;
    bool is_dead;
    bool on_queue;

    message_t *mess_queue;
    int mess_front;
    int mess_end;
    int mess_size;
    int mess_max_size;

} actor_data_t;

// Globalne dane aktórów.
actor_data_t *actor_tab;
actor_id_t next_id_actor;
int actors_max_size;

// Kolejka aktorów z zadaniami
pthread_mutex_t main_lock;
actor_id_t *actor_queue;
int actor_q_front;
int actor_q_size;
int actor_q_maxsize;
int actor_q_end;

bool programme_aborted;

pthread_key_t thread_key;

pthread_cond_t free_threads;

int actors_alive;

// Dodatkowy thread na obsługę sygnałów.
pthread_t th[POOL_SIZE + 1];

pthread_attr_t attr;

// Wrzucanie aktora do kolejki głównej.
void push_actor_q(actor_id_t actor)
{
    pthread_mutex_lock(&main_lock);

    if (actor_q_size == actor_q_maxsize)
    {
        actor_q_maxsize *= 2;
        actor_id_t *holder = malloc(sizeof(actor_id_t) * actor_q_maxsize);
        int i = actor_q_front;
        int p = 0;
        int cnt = actor_q_size;
        while (cnt-- > 0)
        {
            holder[p++] = actor_queue[i];
            i = (i + 1) % actor_q_size;
        }

        free(actor_queue);
        actor_queue = holder;
        actor_q_end = actor_q_size;
        actor_q_front = 0;
    }

    actor_q_size++;
    actor_queue[actor_q_end] = actor;
    actor_q_end = (actor_q_end + 1) % actor_q_maxsize;

    // Obudzenie wątku do zajęcia się aktorem.
    pthread_cond_signal(&free_threads);

    pthread_mutex_unlock(&main_lock);
}

void *actor_run()
{
    while (true)
    {
        pthread_mutex_lock(&main_lock);

        while (actor_q_size == 0 && actors_alive > 0) // Brak aktorów z zadaniami.
            pthread_cond_wait(&free_threads, &main_lock);

        if (actors_alive == 0)
        {
            pthread_mutex_unlock(&main_lock);
            break;
        }

        actor_id_t actor_to_proceed = actor_queue[actor_q_front];
        actor_q_front = (actor_q_front + 1) % actor_q_maxsize;
        actor_q_size--;
        pthread_setspecific(thread_key, (void*) actor_to_proceed);
        pthread_mutex_unlock(&main_lock);

        pthread_mutex_lock(&actor_tab[actor_to_proceed].single_lock);
        actor_tab[actor_to_proceed].on_queue = false;
        message_t current_message =
                actor_tab[actor_to_proceed].mess_queue[actor_tab[actor_to_proceed].mess_front];

        actor_tab[actor_to_proceed].mess_front =
                (actor_tab[actor_to_proceed].mess_front + 1) % actor_tab[actor_to_proceed].mess_max_size;
        actor_tab[actor_to_proceed].mess_size--;

        if (current_message.message_type == MSG_GODIE)
        {
            actor_tab[actor_to_proceed].is_dead = true;
        }
        else if (current_message.message_type == MSG_SPAWN)
        {
            pthread_mutex_lock(&main_lock);
            if (programme_aborted == false) // wtedy nie tworzymy nowych aktorów.
            {
                if (next_id_actor == CAST_LIMIT)
                {
                    syserr(1, "Actor limit exceeded");
                }
                else if (next_id_actor == actors_max_size)
                {
                    actors_max_size *= 2;
                    actor_tab = realloc(actor_tab, sizeof(actor_data_t) * actors_max_size);
                }

                actor_tab[next_id_actor].role = *(role_t *) current_message.data;
                actor_tab[next_id_actor].is_dead = false;
                actor_tab[next_id_actor].on_queue = false;
                actor_tab[next_id_actor].own_id = next_id_actor;

                actor_tab[next_id_actor].mess_front = 0;
                actor_tab[next_id_actor].mess_end = 0;
                actor_tab[next_id_actor].mess_size = 0;
                actor_tab[next_id_actor].mess_max_size = BASESIZE;
                actor_tab[next_id_actor].mess_queue = malloc(sizeof(message_t) * BASESIZE);

                int err;
                if ((err = pthread_mutex_init(&actor_tab[next_id_actor].single_lock, 0)) != 0)
                    syserr(err, "new actor mutex init failed");

                actor_id_t new_actor_id = next_id_actor;
                next_id_actor++;
                actors_alive++;
                pthread_mutex_unlock(&main_lock);

                message_t hello_message;
                hello_message.message_type = MSG_HELLO;
                hello_message.nbytes = sizeof(actor_to_proceed);
                hello_message.data = &actor_tab[actor_to_proceed].own_id;
                send_message(new_actor_id, hello_message);
            }
        }
        else // Wykonanie komendy z tablicy rozdzielczej z funkcjami (w tym hello).
        {
            if (current_message.message_type < 0
                || actor_tab[actor_to_proceed].role.nprompts <= (size_t) current_message.message_type)
                syserr(-1, "Wrong function error");

            actor_tab[actor_to_proceed].role.prompts[current_message.message_type]
                    (&actor_tab[actor_to_proceed].stateptr,
                     current_message.nbytes,
                     current_message.data);
        }

        bool end_work = false;

        if (actor_tab[actor_to_proceed].mess_size > 0)
        {
            if (actor_tab[actor_to_proceed].on_queue == false)
            {
                actor_tab[actor_to_proceed].on_queue = true;
                push_actor_q(actor_to_proceed);
            }
        }
        else if (actor_tab[actor_to_proceed].is_dead) // dodatkowo z negacji ifa wynika mess_size = 0.
        {
            pthread_mutex_lock(&main_lock);
            actors_alive--;
            if (actors_alive == 0)
                end_work = true; // Wszyscy aktorzy są martwi i bez komunikatów.
            pthread_mutex_unlock(&main_lock);
        }

        pthread_mutex_unlock(&actor_tab[actor_to_proceed].single_lock);

        if (end_work)
        {
            pthread_cond_broadcast(&free_threads);
            break;
        }
    }

    return NULL;
}

void *signal_control()
{
    int old_type;
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &old_type);

    int sig;
    sigset_t block_set;

    sigemptyset(&block_set);
    sigaddset(&block_set, SIGINT);
    pthread_sigmask(SIG_UNBLOCK, &block_set, NULL);
    sigwait(&block_set, &sig);

    programme_aborted = true;

    // Zapalenie flagi GO_DIE dla każdego aktora żeby nie mogł przyjmowac nowych komunikatów
    // i skończył jak przerobi wszystkie.
    pthread_mutex_lock(&main_lock);
    for (int i = 0; i < next_id_actor; ++i)
        actor_tab[i].is_dead = true;
    pthread_mutex_unlock(&main_lock);

    actor_system_join(0);
    return NULL;
}

actor_id_t actor_id_self()
{
    return (actor_id_t) pthread_getspecific(thread_key);
}

int actor_system_create(actor_id_t *actor, role_t *const role)
{
    if (actor == NULL || role == NULL) // Brak poprawnych argumentów.
        return -1;

    int err;
    if ((err = pthread_attr_init(&attr)) != 0)
        syserr(err, "attr_init failed");

    if ((err = pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_JOINABLE)) != 0)
        syserr(err, "attr_setdetachstate failed");

    // SIGWAIT thread
    if ((err = pthread_create(&th[POOL_SIZE], &attr, signal_control, NULL)) != 0)
        syserr(err, "create failed");


    // Zablokowanie sygnału które zostanie odziedziczone;
    sigset_t block_set;
    sigfillset(&block_set);
    pthread_sigmask(SIG_SETMASK, &block_set, NULL);

    *actor = 0;
    next_id_actor = 1;
    actor_tab = malloc(sizeof(actor_data_t) * BASESIZE);
    actors_max_size = BASESIZE;
    actors_alive = 1;

    actor_queue = malloc(sizeof(actor_id_t) * BASESIZE);
    actor_q_maxsize = BASESIZE;
    actor_q_front = 0;
    actor_q_end = 0;
    actor_q_size = 0;

    programme_aborted = false;
    const size_t FIRST = 0;

    if ((err = pthread_mutex_init(&main_lock, 0)) != 0)
        syserr(err, "main mutex init failed");

    if ((err = pthread_mutex_init(&actor_tab[FIRST].single_lock, 0)) != 0)
        syserr(err, "actor mutex init failed");


    actor_tab[FIRST].role = *role;
    actor_tab[FIRST].is_dead = false;
    actor_tab[FIRST].on_queue = false;
    actor_tab[FIRST].own_id = 0;

    actor_tab[FIRST].mess_front = 0;
    actor_tab[FIRST].mess_end = 0;
    actor_tab[FIRST].mess_size = 0;
    actor_tab[FIRST].mess_max_size = BASESIZE;
    actor_tab[FIRST].mess_queue = malloc(sizeof(message_t) * BASESIZE);

    if ((err = pthread_cond_init(&free_threads, 0)) != 0)
        syserr(err, "cond for free actors init failed");

    if ((err = pthread_key_create(&thread_key, 0)) != 0)
        syserr(err, "thread key init failed");

    for (int i = 0; i < POOL_SIZE; ++i)
    {
        if ((err = pthread_create(&th[i], &attr, actor_run, NULL)) != 0)
            syserr(err, "thread create failed");
    }

    // NOTE: Na forum i grupie były sprzeczne informacje dot. tego czy
    // być może system_create powinien wysyłać hello z NULLem jako ojcem
    // do nowego aktora czy robi to jedyne MSG_SPAWN.
    // Ja w swoim projekcie uzyłem tej drugiej interpretacji, ponieważ
    // pierwsza tworzy tylko zbędny kod (tj. wysłania teraz komunikatu który i
    // tak zostanie zignorowany bo nie zawiera ojca) tutaj oraz w funkcji hello,
    // tj. if ignorujący pusty komunikat.
    return 0;
}

void actor_system_join(actor_id_t actor)
{
    if (actor < next_id_actor)
    {
        void *return_val;
        for (int i = 0; i < POOL_SIZE; ++i)
            pthread_join(th[i], &return_val);

        pthread_cancel(th[POOL_SIZE]);
        pthread_join(th[POOL_SIZE], &return_val);

        // Czyszczenie wszystkiego
        for (int i = 0; i < next_id_actor; ++i)
        {
            free(actor_tab[i].mess_queue);
            pthread_mutex_destroy(&actor_tab[i].single_lock);
        }

        free(actor_tab);
        free(actor_queue);
        pthread_cond_destroy(&free_threads);
        pthread_mutex_destroy(&main_lock);
        pthread_attr_destroy(&attr);

        next_id_actor = 0;
    }
}

// Dodania komunikatu do kolejki danego actora.
void push_message_q(actor_id_t actor, message_t message)
{
    if (actor_tab[actor].mess_size == actor_tab[actor].mess_max_size)
    {
        actor_tab[actor].mess_max_size *= 2;
        message_t *holder = malloc(sizeof(message_t) * actor_tab[actor].mess_max_size);

        int i = actor_tab[actor].mess_front;
        int p = 0;
        int cnt = actor_tab[actor].mess_size;
        while (cnt-- > 0)
        {
            holder[p++] = actor_tab[actor].mess_queue[i];
            i = (i + 1) % actor_tab[actor].mess_size;
        }

        free(actor_tab[actor].mess_queue);
        actor_tab[actor].mess_queue = holder;
        actor_tab[actor].mess_end = actor_tab[actor].mess_size;
        actor_tab[actor].mess_front = 0;
    }

    actor_tab[actor].mess_size++;
    actor_tab[actor].mess_queue[actor_tab[actor].mess_end] = message;
    actor_tab[actor].mess_end = (actor_tab[actor].mess_end + 1) % actor_tab[actor].mess_max_size;
}

int send_message(actor_id_t actor, message_t message)
{
    if (actor >= next_id_actor || actor < 0)
        return -2;

    if (actor != actor_id_self())
        pthread_mutex_lock(&actor_tab[actor].single_lock);

    if (actor_tab[actor].is_dead)
    {
        if (actor != actor_id_self())
            pthread_mutex_unlock(&actor_tab[actor].single_lock);
        return -1;
    }

    if (actor_tab[actor].mess_size == ACTOR_QUEUE_LIMIT)
    {
        if (actor != actor_id_self())
            pthread_mutex_unlock(&actor_tab[actor].single_lock);
        syserr(1, "Actor queue limit exceeded");
    }

    if (programme_aborted == false)
        push_message_q(actor, message);

    // Dodanie aktora dla kolejki obslugi threadów
    if (actor_tab[actor].mess_size == 1 && programme_aborted == false)
    {
        if (actor_tab[actor].on_queue == false)
        {
            actor_tab[actor].on_queue = true;
            push_actor_q(actor);
        }
    }

    if (actor != actor_id_self())
        pthread_mutex_unlock(&actor_tab[actor].single_lock);
    return 0;
}