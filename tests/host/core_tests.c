#include "dmod.h"
#include "dmi2c.h"
#include "dmi2c_port.h"
#include "dmdrvi.h"
#include "../../src/validation.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct test_ini { const char *section,*instance,*address,*baudrate,*timeout,*role; };
static unsigned allocations, active, calls;
static bool fail_malloc;
static int port_result;
static uint16_t target;
void *Dmod_Malloc(size_t size) { if(fail_malloc) return NULL; void *p=malloc(size); if(p)++allocations; return p; }
void Dmod_Free(void *p) { if(p) { assert(allocations); --allocations; free(p); } }
int dmini_section_count(dmini_context_t i) { return i?1:0; }
const char *dmini_section_name(dmini_context_t i,int n) { return n==0?i->section:NULL; }
int dmini_has_key(dmini_context_t i,const char *s,const char *k)
{ return !strcmp(i->section,s) && !strcmp(k,"instance") && i->instance; }
const char *dmini_get_string(dmini_context_t i,const char *s,const char *k,const char *fallback)
{
    assert(!strcmp(i->section,s));
    const char *v=NULL;
    if(!strcmp(k,"instance"))v=i->instance;
    if(!strcmp(k,"address"))v=i->address;
    if(!strcmp(k,"baudrate"))v=i->baudrate;
    if(!strcmp(k,"timeout_ms"))v=i->timeout;
    if(!strcmp(k,"role"))v=i->role;
    return v?v:fallback;
}
int dmi2c_port_init(const dmi2c_config_t *c)
{ assert(i2c_config_valid(c)); if(active)return -EBUSY; ++active; return 0; }
int dmi2c_port_deinit(uint8_t i) { assert(i==1 && active); --active; return 0; }
int dmi2c_port_transfer(uint8_t i,const dmi2c_transfer_t *t)
{ assert(i==1); if(!i2c_transfer_valid(t))return -EINVAL; ++calls; target=t->messages[0].address; return port_result; }
dmdrvi_context_t driver_create(dmini_context_t,dmdrvi_dev_num_t *);
void driver_free(dmdrvi_context_t);
void *driver_open(dmdrvi_context_t,int,const dmdrvi_dev_num_t *);
void driver_close(dmdrvi_context_t,void *);
int driver_ioctl(dmdrvi_context_t,void *,int,void *);
dmdrvi_ssize_t driver_read(dmdrvi_context_t,void *,void *,size_t,dmdrvi_offset_t);
dmdrvi_ssize_t driver_write(dmdrvi_context_t,void *,const void *,size_t,dmdrvi_offset_t);
int driver_stat(dmdrvi_context_t,const char *,dmdrvi_stat_t *);
int driver_flush(dmdrvi_context_t,void *);
static void test_driver(void)
{
    struct test_ini ini={"touch_i2c","1","0x38","100000","100","master"};
    dmdrvi_dev_num_t num;
    dmdrvi_context_t c=driver_create(&ini,&num);
    assert(c && active==1 && num.minor==1 && !strcmp(num.alt_name,"touch_i2c"));
    assert(!driver_create(&ini,&num)); /* duplicate ownership rollback */
    void *a=driver_open(c,0,&num), *b=driver_open(c,0,&num);
    assert(a && b && a!=b);
    uint16_t addr=0x50;
    assert(driver_ioctl(c,a,dmi2c_ioctl_cmd_set_address,&addr)==0);
    assert(driver_ioctl(c,b,dmi2c_ioctl_cmd_get_address,&addr)==0 && addr==0x38);
    dmi2c_config_t config;
    assert(driver_ioctl(c,a,dmi2c_ioctl_cmd_get_config,&config)==0 && config.address==0x50);
    uint8_t byte=0;
    assert(driver_read(c,a,&byte,1,0)==1 && target==0x50);
    assert(driver_write(c,b,&byte,1,1)==1 && target==0x38);
    port_result=-ENXIO;
    assert(driver_read(c,a,&byte,1,0)==-ENXIO);
    assert(driver_write(c,a,&byte,1,0)==-ENXIO);
    unsigned before=calls;
    assert(driver_read(c,a,NULL,0,0)==0 && calls==before);
    assert(driver_write(c,a,NULL,1,0)==-EINVAL);
    assert(driver_read(c,a,&byte,1,-1)==-EINVAL);
    assert(driver_read(c,a,&byte,SIZE_MAX,0)==-EOVERFLOW);
    assert(driver_ioctl(c,a,0,NULL)==-ENOTTY);
    assert(driver_ioctl(c,a,dmi2c_ioctl_cmd_get_config,NULL)==-EINVAL);
    addr=0x80;
    assert(driver_ioctl(c,a,dmi2c_ioctl_cmd_set_address,&addr)==-EINVAL);
    assert(driver_ioctl(c,NULL,dmi2c_ioctl_cmd_get_address,&addr)==-EINVAL);
    dmdrvi_stat_t st;
    assert(driver_stat(c,NULL,&st)==0 && st.size==0 && st.mode==0666);
    assert(driver_flush(c,a)==0);
    driver_close(c,a); driver_close(c,b); driver_free(c);
    assert(!active && !allocations);
}
static void test_invalid_config(void)
{
    struct test_ini ini={"dmi2c","1","0x38","100000","100","master"};
    dmdrvi_dev_num_t num;
    const char *bad[]={"-1","+1","0x","1junk","256","4294967297","999999999999999999999"};
    for(size_t i=0;i<sizeof(bad)/sizeof(*bad);++i)
    { ini.instance=bad[i]; assert(!driver_create(&ini,&num)); }
    ini.instance="1"; ini.role="slave"; assert(!driver_create(&ini,&num));
    ini.role="master"; ini.address="0x78"; assert(!driver_create(&ini,&num));
    ini.address="0x38"; ini.timeout="0"; assert(!driver_create(&ini,&num));
    ini.timeout="100"; ini.baudrate="1000000"; assert(!driver_create(&ini,&num));
    ini.baudrate="100000"; fail_malloc=true; assert(!driver_create(&ini,&num));
    fail_malloc=false;
    assert(!active && !allocations);
}
int main(void)
{
    test_driver(); test_invalid_config();
    puts("PASS: named INI, strict number parsing, allocation rollback, ownership, per-handle addresses, ioctl, read/write errors, invalid buffers/offsets, lifecycle");
    return 0;
}
