#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stdlib.h>
#include "globals.h"
#include "libraries/DebugPrint/DebugPrint.h"

#define TIME_PASSED(next) (next == 0 || ( ((long)(millis()-next)) >= 0 ))

void config_load(void);
void node_type_save(void);
void net_config_setup(void);
void net_config_loop(void);
void config_save(void);
void discover_poll(void);
void discovery_now(void);
char nibbleToChar(uint32_t val);
void print_hexdump(const char *buf, size_t buf_len);
void print_str(const char *name, const char *val);
void check_upgrade(void);
void node_setup(void);
unsigned node_loop(void);
void node_mqtt_connected(void);
bool node_is_powered(void);
void restart();
void deep_sleep(unsigned seconds);

class Node {
  public:
    Node() : m_loop_only_if_connected(false) {}
    virtual void setup(void) {}
    virtual unsigned loop(void) {
      return 0;
    }
    virtual void mqtt_connected_event(void) {}
    bool loop_only_if_connected(void) {
      return m_loop_only_if_connected;
    }

  protected:
    bool m_loop_only_if_connected;
};
#endif
