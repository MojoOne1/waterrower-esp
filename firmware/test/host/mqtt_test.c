#include "app.h"
#include <assert.h>
#include <string.h>
bool settings_get_str(const char *k,char *o,size_t n){o[0]=0;return false;}
int settings_get_i32(const char *k,int d){return d;}
void settings_set_i32(const char *k,int v){} void settings_set_str(const char*k,const char*v){}
void json_esc(char*d,size_t n,const char*s){snprintf(d,n,"%s",s);}
void live_get(live_t*o){memset(o,0,sizeof*o);o->speed_ms=3.2f;o->distance_m=1234;o->duration_s=3725;o->s4_connected=1;o->session_active=1;strcpy(o->session_id,"20261111T070000");}
void live_get_last(char*o,size_t n){snprintf(o,n,"{\"a\":1}");} uint32_t live_seq(void){return 1;}
void rower_cmd(rower_cmd_t c){} void rower_set_timeout_min(int m){} int rower_timeout_min(void){return 5;}
#define esp_crt_bundle_attach 0
#include "mqtt.c"
int main(void){ strcpy(prefix,"waterrower"); strcpy(disc,"homeassistant"); strcpy(devid,"waterrower_040506");
  publish_discovery(); publish_states(true); }
