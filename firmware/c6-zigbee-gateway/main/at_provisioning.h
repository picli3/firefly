#pragma once
#include <stdbool.h>
#include <stddef.h>
/* Serialized console task only. No input/secret is echoed. */
bool at_provisioning_command(const char *line, char *reply, size_t size);
void at_provisioning_abort(void);
void at_provisioning_poll(void);
