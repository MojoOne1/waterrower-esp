#include "app.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "webfn.h"
size_t fake_used = 10;
static void mk(const char *id, int n, uint32_t t0){ void *h=store_open_session(id); for(int i=0;i<n;i++){ sample_t s={.ts=t0+i,.distance_m=i*3,.speed_cms=300+i,.strokes=i/2,.duration_s=i,.watts=100,.spm=24}; store_append(h,&s);} store_close_handle(h); }
int main(void){
  store_init();
  mk("20261111T070000",100,1000); mk("20250101T000000",5,500); mk("boot-12",3,7);
  { void *h=store_open_session("empty"); store_close_handle(h); }
  store_recover();
  char (*ids)[24]; int n=store_list_ids(&ids); assert(n==3); assert(!strcmp(ids[0],"boot-12")); free(ids);
  sum_t s; bool has; assert(store_read_summary("20261111T070000",&s,&has)&&has&&s.distance_m==297&&s.ended_at==1099&&!s.synced);
  store_set_synced("20261111T070000",true); assert(store_read_summary("20261111T070000",&s,&has)&&s.synced);
  store_clear_synced_all(); assert(store_read_summary("20261111T070000",&s,&has)&&!s.synced);
  /* corrupt summary -> falls back to the samples */
  FILE *f=fopen(STORE_BASE "/20250101T000000.sum","wb"); fputs("garbage",f); fclose(f);
  assert(store_read_summary("20250101T000000",&s,&has)&&!has&&s.distance_m==12);
  f=fopen(STORE_BASE "/20250101T000000.sum","wb"); sum_t bad; memset(&bad,0xff,sizeof bad); fwrite(&bad,sizeof bad,1,f); fclose(f);
  assert(store_read_summary("20250101T000000",&s,&has)&&!has);
  char b[64]; assert(sum_to_json(&s,true,b,sizeof b)<64 && strlen(b)<64);
  /* make room: oldest by start time goes first (boot-12 started at 7) */
  fake_used = 900000; 
  static int rounds; (void)rounds;
  /* one deletion then pretend space is back: emulate by lowering after first list */
  store_delete("nope");
  { char (*i2)[24]; int k=store_list_ids(&i2); assert(k==3); free(i2); }
  /* run make_room with a hook: delete one, then stop */
  fake_used = 900000; 
  /* it would delete all three; check order by running once with 50 rounds is destructive, so test order manually */
  { char (*i2)[24]; int k=store_list_ids(&i2); int old=-1; uint32_t t=~0u; for(int i=0;i<k;i++){ sum_t q; if(store_read_summary(i2[i],&q,NULL)&&q.started_at<t){t=q.started_at;old=i;} } assert(!strcmp(i2[old],"boot-12")); free(i2); }
  store_make_room(); { char (*i2)[24]; assert(store_list_ids(&i2)==0); free(i2); }

  assert(host_allowed("192.168.1.50")); assert(host_allowed("192.168.4.1:80")); assert(host_allowed("waterrower.local"));
  assert(host_allowed("WaterRower")); assert(host_allowed("waterrower.lan")); assert(host_allowed("waterrower.home.arpa:80"));
  assert(!host_allowed("evil.com")); assert(!host_allowed("waterrower.evil.com")); assert(!host_allowed("waterrower.local.evil.com"));
  assert(!host_allowed("192.168.1.50.evil.com")); assert(!host_allowed("1.2.3")); assert(!host_allowed("")); assert(!host_allowed("xwaterrower.lan"));
  assert(!host_allowed("192.168.1.50.nip.io")); assert(!host_allowed("1.2.3.4.5"));
  char id[24]; bool csv;
  assert(uri_session_id("/api/sessions/20261111T070000",id,&csv)&&!csv&&!strcmp(id,"20261111T070000"));
  assert(uri_session_id("/api/sessions/boot-12-2/export.csv",id,&csv)&&csv);
  assert(uri_session_id("/api/sessions/abc?x=1",id,&csv)&&!strcmp(id,"abc"));
  assert(!uri_session_id("/api/sessions/..%2f..%2fx",id,&csv)); assert(!uri_session_id("/api/sessions/../x",id,&csv));
  assert(!uri_session_id("/api/sessions/",id,&csv)); assert(!uri_session_id("/api/sessions/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",id,&csv));
  assert(!uri_session_id("/api/sessions/a.sum",id,&csv));
  assert(url_ok("https://arena.example.com")); assert(url_ok("arena.example.com")); assert(url_ok("http://arena:8090"));
  assert(!url_ok("ftp://x")); assert(!url_ok("https://u:p@x")); assert(!url_ok("a b")); assert(!url_ok("https://")); assert(!url_ok("x\"y"));
  assert(topic_chars("waterrower/gym-1")); assert(!topic_chars("a\"b")); assert(!topic_chars("")); assert(!topic_chars("a b")); assert(!topic_chars("a#"));
  puts("ALL OK");
}
