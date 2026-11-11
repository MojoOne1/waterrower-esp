#pragma once
#include <stdio.h>
typedef void *esp_mqtt_client_handle_t; typedef int esp_event_base_t;
typedef int esp_mqtt_event_id_t;
enum { MQTT_EVENT_ANY, MQTT_EVENT_CONNECTED, MQTT_EVENT_DISCONNECTED, MQTT_EVENT_ERROR, MQTT_EVENT_DATA };
enum { MQTT_ERROR_TYPE_CONNECTION_REFUSED, MQTT_ERROR_TYPE_TCP_TRANSPORT };
typedef struct { int error_type; } eh_t;
typedef struct { eh_t *error_handle; const char *topic, *data; int topic_len, data_len, total_data_len; } ev_t;
typedef ev_t *esp_mqtt_event_handle_t;
typedef struct { struct { struct { const char *uri; } address; struct { void *crt_bundle_attach; } verification; } broker;
  struct { const char *username, *client_id; struct { const char *password; } authentication; } credentials;
  struct { struct { const char *topic, *msg; int msg_len, qos, retain; } last_will; int keepalive; } session;
  struct { int size; } buffer; struct { int reconnect_timeout_ms; } network; } esp_mqtt_client_config_t;
static inline int esp_mqtt_client_enqueue(void *c,const char*t,const char*d,int l,int q,int r,int s){printf("%s\t%s\n",t,d);return 1;}
static inline int esp_mqtt_client_publish(void*c,const char*t,const char*d,int l,int q,int r){return 1;}
static inline int esp_mqtt_client_subscribe(void*c,const char*t,int q){return 1;}
static inline void *esp_mqtt_client_init(const esp_mqtt_client_config_t*c){return 0;}
static inline int esp_mqtt_client_register_event(void*c,int e,void*h,void*a){return 0;}
static inline int esp_mqtt_client_start(void*c){return 0;} static inline int esp_mqtt_client_stop(void*c){return 0;}
static inline int esp_mqtt_client_destroy(void*c){return 0;}
