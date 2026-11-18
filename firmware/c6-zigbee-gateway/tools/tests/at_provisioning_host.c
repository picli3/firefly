/* Production state machine/validation/NVS code. Only hardware and crypto ports
 * are substituted; OpenSSL computes real SHA/base64 and parses a real PEM. */
#include "../../main/gateway_settings.c"
#include "../../main/at_provisioning.c"
#include <assert.h>
#include <openssl/sha.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
static unsigned char durable[16384], staging[16384];
static size_t durable_size, staging_size;
static unsigned writes, commits;
static bool fail_set, fail_commit;
static int64_t now;
int64_t esp_timer_get_time(void) { return now; }
void ethernet_mqtt_status(char *out, size_t n) { snprintf(out,n,"+FFSTATUS: mqtt_connected=0\r\nOK\r\n"); }
int mbedtls_sha256(const unsigned char *p,size_t n,unsigned char *out,int mode) { assert(!mode); return SHA256(p,n,out)?0:-1; }
int mbedtls_base64_decode(unsigned char *out,size_t max,size_t *len,const unsigned char *p,size_t n) {
    if (n%4 || n/4*3 > max+2) return -1;
    unsigned char temp[MAX_ENCODED];
    int bytes=EVP_DecodeBlock(temp,p,(int)n);
    if(bytes<0)return -1;
    if(n&&p[n-1]=='=')--bytes;
    if(n>1&&p[n-2]=='=')--bytes;
    if(bytes<0||(size_t)bytes>max)return -1;
    memcpy(out,temp,bytes); *len=bytes; return 0;
}
void mbedtls_x509_crt_init(mbedtls_x509_crt *c) { c->crt=NULL; }
void mbedtls_x509_crt_free(mbedtls_x509_crt *c) { X509_free(c->crt); }
int mbedtls_x509_crt_parse(mbedtls_x509_crt *c,const unsigned char *p,size_t n) {
    BIO *bio=BIO_new_mem_buf(p,(int)n); c->crt=PEM_read_bio_X509(bio,NULL,NULL,NULL); BIO_free(bio); return c->crt?0:-1;
}
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *h) {
    assert(!strcmp(name,"gateway_prov"));
    if(mode==NVS_READONLY&&!durable_size)return ESP_ERR_NVS_NOT_FOUND;
    *h=1; return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *p,size_t *n) {
    if(!durable_size)return ESP_ERR_NVS_NOT_FOUND;
    if(p) { if(*n<durable_size)return ESP_ERR_INVALID_SIZE; memcpy(p,durable,durable_size); }
    *n=durable_size; return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *p,size_t n) {
    ++writes; if(fail_set)return ESP_FAIL;
    assert(n<=sizeof(staging));memcpy(staging,p,n);staging_size=n;return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) {
    ++commits;if(fail_commit)return ESP_FAIL;
    memcpy(durable,staging,staging_size);durable_size=staging_size;return ESP_OK;
}
void nvs_close(nvs_handle_t h) {}
static char response[1024];
static void send(const char *line,const char *expected) {
    assert(at_provisioning_command(line,response,sizeof(response)));
    assert(strstr(response,expected));
    assert(!strstr(response,"unit-secret")&&!strstr(response,"BEGIN CERTIFICATE"));
}
static void upload(const char *raw,bool bad_hash,bool duplicate) {
    size_t length=strlen(raw); unsigned char encoded[MAX_ENCODED+1];
    assert(length<=GATEWAY_CONFIG_MAX_JSON);
    size_t n=EVP_EncodeBlock(encoded,(const unsigned char*)raw,length);
    unsigned count=(n+191)/192;char line[256];
    snprintf(line,sizeof(line),"AT+FFPROVBEGIN=%u",count);send(line,"OK");
    for(unsigned i=0;i<count;++i) {
        size_t len=n-i*192;if(len>192)len=192;
        snprintf(line,sizeof(line),"AT+FFPROVDATA=%u,\"%.*s\"",i,(int)len,encoded+i*192);
        send(line,"OK");if(duplicate)send(line,"OK");
    }
    unsigned char sha[32];mbedtls_sha256((const unsigned char*)raw,length,sha,0);
    char hex[65];for(unsigned i=0;i<32;++i)snprintf(hex+i*2,3,"%02x",sha[i]);
    if(bad_hash)hex[0]=hex[0]=='a'?'b':'a';
    snprintf(line,sizeof(line),"AT+FFPROVCOMMIT=\"%s\"",hex);
    send(line,bad_hash?"ERROR:CHECKSUM":"SAVED,RESTART_REQUIRED");
}
static void reboot(void) {
    at_provisioning_abort(); memset(&s_active,0,sizeof(s_active));s_pending=s_from_nvs=s_storage_error=false;
    assert(gateway_settings_init()==ESP_OK);
}
int main(int argc,char **argv) {
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
    char raw[GATEWAY_CONFIG_MAX_JSON+1]={0};size_t n=fread(raw,1,GATEWAY_CONFIG_MAX_JSON,f);fclose(f);assert(n);
    assert(gateway_settings_init()==ESP_OK);send("AT","OK");send("ATI","firefly-at/v1");
    send("AT+FFPROVBEGIN=0","ERROR:ARGUMENT");send("AT+FFPROVBEGIN=99999999","ERROR:ARGUMENT");
    send("AT+FFPROVBEGIN=1","OK");send("AT+FFPROVDATA=1,\"AAAA\"","ERROR:ORDER");
    send("AT+FFPROVBEGIN=2","OK");send("AT+FFPROVCOMMIT=\"bad\"","ERROR:INCOMPLETE");
    send("AT+FFPROVBEGIN=1","OK");now=61000000;send("AT+FFPROVDATA=0,\"AAAA\"","ERROR:TIMEOUT");
    send("AT+FFPROVBEGIN=1","OK");now+=61000000;at_provisioning_poll();assert(!s_rx.data);
    upload(raw,true,false);assert(!writes&&!commits);
    upload(raw,false,true);assert(writes==1&&commits==1);
    assert(!gateway_settings_active()->uri[0]); /* commit never changes live MQTT pointers */
    send("AT+FFPROV?","pending_restart=1");upload(raw,false,false);assert(writes==1&&commits==1);
    reboot();assert(!strcmp(s_active.gateway_id,"unit-gateway")&&!strcmp(s_active.uri,"mqtts://192.168.18.40:8883"));
    assert(s_active.ntp_count==3&&s_active.keepalive_s==60&&s_active.heartbeat_s==60);
    send("AT+FFPROV?","pending_restart=0");
    cJSON *j=cJSON_Parse(raw);assert(j);cJSON *mqtt=cJSON_GetObjectItem(j,"mqtt");
    cJSON_ReplaceItemInObject(mqtt,"password",cJSON_CreateString("unit-secret-new"));
    char *changed=cJSON_PrintUnformatted(j);
    fail_set=true;assert(gateway_settings_commit(changed,strlen(changed))==ESP_FAIL);fail_set=false;
    fail_commit=true;assert(gateway_settings_commit(changed,strlen(changed))==ESP_FAIL);fail_commit=false;
    reboot();assert(!strcmp(s_active.password,"unit-secret"));
    cJSON_ReplaceItemInObject(mqtt,"tls",cJSON_CreateBool(false));char *invalid=cJSON_PrintUnformatted(j);
    assert(gateway_settings_commit(invalid,strlen(invalid))==ESP_ERR_INVALID_ARG);cJSON_free(invalid);
    cJSON_ReplaceItemInObject(mqtt,"tls",cJSON_CreateBool(true));
    cJSON_ReplaceItemInObject(mqtt,"topic_prefix",cJSON_CreateString("wrong"));invalid=cJSON_PrintUnformatted(j);
    assert(gateway_settings_commit(invalid,strlen(invalid))==ESP_ERR_INVALID_ARG);cJSON_free(invalid);
    cJSON_ReplaceItemInObject(mqtt,"topic_prefix",cJSON_CreateString("firefly/v1/gateways/unit-gateway"));
    cJSON_ReplaceItemInObject(mqtt,"ca_pem",cJSON_CreateString("-----BEGIN CERTIFICATE-----\nnot-a-cert\n-----END CERTIFICATE-----\n"));
    invalid=cJSON_PrintUnformatted(j);assert(gateway_settings_commit(invalid,strlen(invalid))==ESP_ERR_INVALID_ARG);
    cJSON_free(invalid);cJSON_Delete(j);cJSON_free(changed);
    assert(gateway_settings_commit("{\"schema_version\":1,\"schema_version\":1}",39)==ESP_ERR_INVALID_ARG);
    char deep[1000];memset(deep,'[',499);memset(deep+499,']',499);deep[998]=0;
    assert(gateway_settings_commit(deep,998)==ESP_ERR_INVALID_ARG);
    assert(gateway_settings_commit(raw,n)==ESP_OK);
    durable[sizeof(saved_config_t)+10]^=1;assert(gateway_settings_init()==ESP_ERR_INVALID_CRC);
    assert(!s_active.uri[0]);send("AT+FFPROV?","ERROR:STORAGE");
    assert(gateway_settings_commit(raw,n)==ESP_OK);reboot();send("AT+FFPROV?","unit-gateway");
    assert(!at_provisioning_command("csv status",response,sizeof(response)));
    puts("AT: fragments, retry, timeout, SHA256, TLS/JSON validation, atomic NVS, reboot and secret redaction passed");
}
