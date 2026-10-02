#include <stdbool.h>
typedef void *dmosi_mutex_t;
dmosi_mutex_t dmosi_mutex_create(bool recursive);
void dmosi_mutex_destroy(dmosi_mutex_t mutex);
int dmosi_mutex_lock(dmosi_mutex_t mutex);
int dmosi_mutex_unlock(dmosi_mutex_t mutex);
