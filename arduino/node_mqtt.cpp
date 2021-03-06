#define _GNU_SOURCE
#include <string.h>

#include "node_mqtt.h"
#include "libraries/PubSubClient/PubSubClient.h"
#include "WiFiClient.h"
#include <ESP8266WiFi.h>
#include <WString.h>
#include "globals.h"
#include "common.h"
#include "cached_vars.h"
#include "rtc_store.h"

#define MQTT_RECONNECT_TIMEOUT 2000
#define MQTT_TOPIC_LEN 40
#define NUM_MQTT_SUBS 5

static WiFiClient mqtt_client;
static PubSubClient mqtt(mqtt_client);
static char mqtt_upgrade_topic[MQTT_TOPIC_LEN];
static unsigned long next_reconnect = 0;

static struct {
  char topic[MQTT_TOPIC_LEN];
  mqtt_callback_t callback;
} subscription[NUM_MQTT_SUBS];


static void mqtt_callback(char* topic, byte* payload, int len) {
  debug.log("Message arrived [", topic, ']');
  for (int i = 0; i < len; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  if (strcmp(topic, mqtt_upgrade_topic) == 0) {
    if (strlen(VERSION) != len || strncmp((const char*)payload, VERSION, len) != 0) {
      check_upgrade();
    } else {
      debug.log("Asking to upgrade to our version, not doing anything");
    }
    // Only after we got an upgrade chance we should go to sleep
    debug.log("MQTT upgrade notification received, sleep lock released");
    sleep_postpone(50);
    return;
  }

  char *data = strndupa((char*)payload, len);
  for (int i = 0; i < NUM_MQTT_SUBS; i++) {
    if (strcmp(subscription[i].topic, topic) == 0) {
      subscription[i].callback(data);
      return;
    }
  }
}

void mqtt_topic(char *buf, int buf_len, const char *name)
{
  snprintf(buf, buf_len, "shm/%s/%s", node_name, name);
}

/* This is useful for a one-off publish, it uses only one ram location for any number of one-off publishes */
const char * mqtt_tmp_topic(const char *name)
{
  static char buf[40];

  mqtt_topic(buf, sizeof(buf), name);
  return buf;
}

void mqtt_setup() {
  debug.log("MQTT setup ", cache.get_mqtt_server(), ':', cache.get_mqtt_port());
  for (int i = 0; i < NUM_MQTT_SUBS; i++)
    subscription[i].callback = 0;
  mqtt_topic(mqtt_upgrade_topic, sizeof(mqtt_upgrade_topic), "upgrade");
  mqtt.setCallback(mqtt_callback);
  mqtt.setServer(cache.get_mqtt_server(), cache.get_mqtt_port());
}

void mqtt_update_server(const IPAddress &new_mqtt_server, int new_mqtt_port)
{
  if (cache.set_mqtt_server(new_mqtt_server) || cache.set_mqtt_port(new_mqtt_port))
  {
    cache.save(); // If we are here, something changed and we want to save
    mqtt.setServer(cache.get_mqtt_server(), cache.get_mqtt_port());
    debug.log("MQTT server updated to ", new_mqtt_server, ':', new_mqtt_port);
    next_reconnect = 0;
    mqtt_disconnect();
  }
}

void mqtt_disconnect(void)
{
  mqtt.disconnect();
}

bool mqtt_connected() {
  return mqtt.connected();
}


void mqtt_loop(void)
{
  if (!WiFi.isConnected()) {
    next_reconnect = 0;
    return;
  }

  if (mqtt.connected()) {
    mqtt.loop();
    return;
  }

  if (cache.get_mqtt_server() && cache.get_mqtt_port() && TIME_PASSED(next_reconnect)) {
    mqtt.setServer(cache.get_mqtt_server(), cache.get_mqtt_port());
    next_reconnect = millis() + MQTT_RECONNECT_TIMEOUT;
    char will_topic[MQTT_TOPIC_LEN];
    mqtt_topic(will_topic, sizeof(will_topic), "online");
    if (mqtt.connect(node_name, will_topic, 0, 1, "offline")) {
      debug.log("MQTT connected");
      sleep_postpone(5000);

      // Once connected, publish an announcement...
      mqtt_publish_str("bootreason", ESP.getResetInfo().c_str());
      mqtt_publish_int("rssi", WiFi.RSSI());
      mqtt_publish_int("connect_fail_num", rtc_store.num_connect_fails);
      mqtt_publish_int("connect_fail_rssi", rtc_store.last_rssi);

      // ... and resubscribe
      mqtt.subscribe(mqtt_upgrade_topic);
      for (int i = 0; i < NUM_MQTT_SUBS; i++) {
        if (subscription[i].callback) {
          mqtt.subscribe(subscription[i].topic);
        }
      }
      node_mqtt_connected();
      mqtt.publish(will_topic, "online", 1);
    }
  }
}

void mqtt_publish_str(const char *name, const char *val)
{
  debug.log("mqtt publish ", name, ' ', val);
  mqtt.publish(mqtt_tmp_topic(name), val, 1);
}

void mqtt_publish_float(const char *name, float val)
{
  char msg[20];
  dtostrf(val, 0, 4, msg);
  mqtt_publish_str(name, msg);
}

void mqtt_publish_bool(const char *name, bool val)
{
  mqtt_publish_str(name, val ? "1" : "0");
}

void mqtt_publish_int(const char *name, int val)
{
  char msg[16];
  itoa(val, msg, 10);
  mqtt_publish_str(name, msg);
}

void mqtt_subscribe(const char *name, mqtt_callback_t mqtt_cb)
{
  int i;
  debug.log("Asking to subscribe for ", name);
  for (i = 0; i < NUM_MQTT_SUBS; i++) {
    if (subscription[i].callback == 0) {
      // Empty slot
      break;
    }
  }
  if (i == NUM_MQTT_SUBS) {
    debug.log("BUG: Cannot allocate subscription slot");
    return;
  }

  const char *topic = mqtt_tmp_topic(name);
  strncpy(subscription[i].topic, topic, sizeof(subscription[i].topic)-1);
  subscription[i].topic[sizeof(subscription[i].topic)-1] = 0;

  subscription[i].callback = mqtt_cb;

  if (mqtt.connected()) {
    mqtt.subscribe(subscription[i].topic);
  }
}
