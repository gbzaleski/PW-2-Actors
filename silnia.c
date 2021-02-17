#include <stdio.h>
#include "cacti.h"

typedef void (*custom_role) (void**, size_t, void*);

#define SUPRESS_WARNING(x) (void)x
#define MSG_SON (message_type_t)0x1
#define MSG_FACTORIAL (message_type_t)0x2

// Zmienne globalne wykorzystywane w funkcjach także poza mainem.
message_t terminate_message = {.message_type = MSG_GODIE};
actor_id_t my_actor_id;

typedef struct factorial_struct
{
    long long step;
    long long limit;
    long long res;
} factorial_t;

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
    factorial_message.message_type = MSG_FACTORIAL;
    factorial_message.data = (*stateptr);
    factorial_message.nbytes = sizeof(stateptr);

    send_message(son_id, factorial_message);
    send_message(actor_id_self(), terminate_message);
}

// Predefinicja wymagana żeby można dać argument.
void calculate_factorial(void **stateptr, size_t nbytes, void *data);

#define SIZE_FUN 3

custom_role global_role[SIZE_FUN] =
{
    &receive_father_id,
    &receive_son_id,
    &calculate_factorial
};

role_t actor_role =
{
    .nprompts = SIZE_FUN,
    .prompts = global_role
};

void calculate_factorial(void **stateptr, size_t nbytes, void *data)
{
    SUPRESS_WARNING(nbytes);
    (*stateptr) = data;

    ((factorial_t*)(*stateptr))->res *= ((factorial_t*)(*stateptr))->step;
    ((factorial_t*)(*stateptr))->step++;

    if (((factorial_t*)(*stateptr))->step > ((factorial_t*)(*stateptr))->limit)
    {
        printf("%lld\n", ((factorial_t*)(*stateptr))->res);
        send_message(actor_id_self(), terminate_message);
    }
    else
    {
        message_t spawn_message;
        spawn_message.data = (void *) &actor_role;
        spawn_message.nbytes = sizeof(role_t);
        spawn_message.message_type = MSG_SPAWN;

        send_message(actor_id_self(), spawn_message);
    }
}

int main()
{
    // Zmienne wykorzystywane w programie + my_actor zadeklarowany wczesniej.
    long long input;
    factorial_t fdata;
    message_t factorial_message;

    // Założenie że dane są poprawne i dostatecznie małe żeby wynik dało się przedstawić w long longu;
    // Tj. 0 <= input <= 20 (via: forum).
    scanf("%lld", &input);

    fdata.res = 1;
    fdata.step = 1;
    fdata.limit = input;

    factorial_message.message_type = MSG_FACTORIAL;
    factorial_message.data = (void*) &fdata;
    factorial_message.nbytes = sizeof(message_t);

    actor_system_create(&my_actor_id, &actor_role);
    send_message(my_actor_id, factorial_message);
    actor_system_join(my_actor_id);
    return 0;
}
