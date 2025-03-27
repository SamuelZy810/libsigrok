
#include "protocol.h"

int acquisition_callback(int fd, int events, void * cb_data) {

    (void) fd;
    (void) events;
    (void) cb_data;

    return true;

}
