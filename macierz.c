#include "cacti.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// Usleep przujmuje mikrosekundy a chcemy czekać milisekundy.
#define RATIO 1000
#define SUPRESS_WARNING(x) (void)x
#define MSG_SON (message_type_t)0x1
#define MSG_MATRIX (message_type_t)0x2

typedef void (*custom_role) (void**, size_t, void*);

typedef struct matrix_struct
{
    int len;
    int cols;
    int *val;
    int *timeout;
    int actors_created;
    int *res;

} matrix_t;

// Zmienne globalne wykorzystywane w funkcjach także poza mainem.
message_t terminate_message = {.message_type = MSG_GODIE};
actor_id_t my_actor_id;

void receive_father_id(void **stateptr, size_t nbytes, void *data)
{
    SUPRESS_WARNING(nbytes);
    SUPRESS_WARNING(stateptr);

    message_t from_son_message;
    from_son_message.message_type = MSG_SON;
    from_son_message.data = (void*) actor_id_self();
    from_son_message.nbytes = sizeof(actor_id_t);

    if (actor_id_self() != my_actor_id)
        send_message(*(actor_id_t*) data, from_son_message);
}

void receive_son_id(void **stateptr, size_t nbytes, void *data)
{
    SUPRESS_WARNING(nbytes);
    actor_id_t son_id = (actor_id_t) data;

    message_t factorial_message;
    factorial_message.message_type = MSG_MATRIX;
    factorial_message.data = (*stateptr);
    factorial_message.nbytes = sizeof(stateptr);

    send_message(son_id, factorial_message);
    send_message(actor_id_self(), terminate_message);
}

// Predefinicja wymagana żeby można dać argument.
void proceed_matrix(void **stateptr, size_t nbytes, void *data);

#define SIZE_FUN 3

custom_role global_role[SIZE_FUN] =
{
    &receive_father_id,
    &receive_son_id,
    &proceed_matrix
};

role_t actor_role =
{
    .nprompts = SIZE_FUN,
    .prompts = global_role
};

void proceed_matrix(void **stateptr, size_t nbytes, void *data)
{
    SUPRESS_WARNING(nbytes);
    (*stateptr) = data;
    for (int i = 0; i < ((matrix_t*)(*stateptr))->len; ++i)
    {
        int pos = i * (((matrix_t*)(*stateptr))->cols) + (((matrix_t*)(*stateptr))->actors_created - 1);
        ((matrix_t*)(*stateptr))->res[i] += ((matrix_t*)(*stateptr))->val[pos];
        usleep(((matrix_t*)(*stateptr))->timeout[pos] * RATIO);
    }

    if (((matrix_t*)(*stateptr))->actors_created < ((matrix_t*)(*stateptr))->cols)
    {
        ((matrix_t*)(*stateptr))->actors_created++;
        message_t spawn_message;
        spawn_message.data = (void *) &actor_role;
        spawn_message.nbytes = sizeof(role_t);
        spawn_message.message_type = MSG_SPAWN;

        send_message(actor_id_self(), spawn_message);
    }
    else
    {
        for (int i = 0; i < ((matrix_t*)(*stateptr))->len; ++i)
            printf("%d\n", ((matrix_t*)(*stateptr))->res[i]);
    }
    send_message(actor_id_self(), terminate_message);
}

int main()
{
    matrix_t mdata;
    message_t matrix_message;
    scanf("%d %d", &mdata.len, &mdata.cols);
    // Bo stworzymy jednego actora przez system_create
    mdata.actors_created = 1;
    mdata.val = malloc(sizeof(int) * mdata.len * mdata.cols);
    mdata.timeout = malloc(sizeof(int) * mdata.len * mdata.cols);
    mdata.res = malloc(sizeof(int) * mdata.len);
    for (int i = 0; i < mdata.len; ++i)
        mdata.res[i] = 0;

    for (int i = 0; i < mdata.cols * mdata.len; ++i)
        scanf("%d %d", &mdata.val[i], &mdata.timeout[i]);

    actor_system_create(&my_actor_id, &actor_role);

    matrix_message.message_type = MSG_MATRIX;
    matrix_message.data = (void*) &mdata;
    matrix_message.nbytes = sizeof(message_t);

    send_message(my_actor_id, matrix_message);
    actor_system_join(my_actor_id);

    free(mdata.res);
    free(mdata.val);
    free(mdata.timeout);

    return 0;
}
