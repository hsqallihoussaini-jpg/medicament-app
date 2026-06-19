#include <stdlib.h>

#include "app.h"

int main(void) {
    int port = 8080;
    const char *port_value = getenv("PORT");

    if (port_value != NULL && *port_value != '\0') {
        int parsed_port = atoi(port_value);
        if (parsed_port > 0) {
            port = parsed_port;
        }
    }

    return run_server(port);
}
