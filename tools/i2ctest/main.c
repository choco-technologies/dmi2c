#include "dmod.h"
#include "dmi2c_types.h"
#include <errno.h>
#include <string.h>

static int parse(const char *s)
{
    unsigned value=0, base=10;
    if (s[0]=='0' && (s[1]=='x' || s[1]=='X')) { base=16; s+=2; }
    if (!*s) return -1;
    for (; *s; ++s)
    {
        unsigned d = *s>='0' && *s<='9' ? *s-'0' :
            (*s>='a' && *s<='f' ? *s-'a'+10 : 99);
        if (d>=base || value>65535/base) return -1;
        value=value*base+d;
    }
    return value<=65535 ? (int)value : -1;
}
static int read_register(void *file, uint16_t address, uint8_t reg, uint8_t *data, size_t size)
{
    dmi2c_message_t m[]={{address,false,&reg,1},{address,true,data,size}};
    dmi2c_transfer_t t={m,2};
    return Dmod_Ioctl(file,dmi2c_ioctl_cmd_transfer,&t);
}
static int check(const char *name, bool ok)
{
    Dmod_Printf("%s: %s\n",name,ok?"PASS":"FAIL");
    return ok?0:1;
}
static int ft5336(void *file, const char *path)
{
    uint8_t *data=Dmod_Malloc(256);
    void *second=Dmod_FileOpen(path,"r+");
    if (!data || !second) { Dmod_Free(data); if(second) Dmod_FileClose(second); return 1; }
    int failed=0;
    uint16_t first_address=0, second_address=0, address=0x38;
    int rc=Dmod_Ioctl(file,dmi2c_ioctl_cmd_set_address,&address);
    address=0x39;
    rc |= Dmod_Ioctl(second,dmi2c_ioctl_cmd_set_address,&address);
    rc |= Dmod_Ioctl(file,dmi2c_ioctl_cmd_get_address,&first_address);
    rc |= Dmod_Ioctl(second,dmi2c_ioctl_cmd_get_address,&second_address);
    failed+=check("independent handle addresses",!rc && first_address==0x38 && second_address==0x39);
    failed+=check("unknown ioctl",Dmod_Ioctl(file,0,NULL)==-ENOTTY);
    address=0x77;
    failed+=check("absent target NACK",Dmod_Ioctl(file,dmi2c_ioctl_cmd_probe,&address)==-ENXIO);
    for (size_t size=1;size<=4;++size)
    {
        memset(data,0,256);
        rc=read_register(file,0x38,0xa8,data,size);
        Dmod_Printf("FT5336 length=%u rc=%d id=0x%02x\n",(unsigned)size,rc,data[0]);
        failed+=check("repeated START / chip ID",!rc && data[0]==0x51);
    }
    rc=read_register(file,0x38,0,data,256);
    failed+=check("256-byte RELOAD / chip ID at 0xa8",!rc && data[0xa8]==0x51);
    unsigned i;
    for(i=0;i<100;++i)
        if(read_register(file,0x38,0xa8,data,1) || data[0]!=0x51) break;
    failed+=check("100 repeated reads",i==100);
    Dmod_FileClose(second); Dmod_Free(data);
    Dmod_Printf("I2C hardware failures: %d\n",failed);
    return failed;
}
static int command(void *file, int argc, char **argv)
{
    if(argc==3 && !strcmp(argv[2],"ft5336")) return ft5336(file,argv[1]);
    if(argc<4) return -EINVAL;
    int a=parse(argv[3]);
    if(a<8 || a>0x77) return -EINVAL;
    uint16_t address=a;
    if(argc==4 && !strcmp(argv[2],"probe"))
        return Dmod_Ioctl(file,dmi2c_ioctl_cmd_probe,&address);
    if(argc!=6 || strcmp(argv[2],"read")) return -EINVAL;
    int reg=parse(argv[4]), size=parse(argv[5]);
    if(reg<0 || reg>255 || size<1 || size>4096) return -EINVAL;
    uint8_t *data=Dmod_Malloc(size);
    if(!data) return -ENOMEM;
    int rc=read_register(file,address,reg,data,size);
    if(!rc)
    {
        for(int i=0;i<size;++i) Dmod_Printf("%02x%s",data[i],(i+1)%16?" ":"\n");
        Dmod_Printf("\n");
    }
    Dmod_Free(data);
    return rc;
}
int main(int argc,char **argv)
{
    if(argc<3)
    {
        Dmod_Printf("Usage: i2ctest DEVICE probe ADDRESS | read ADDRESS REGISTER LENGTH | ft5336\n");
        return 1;
    }
    void *file=Dmod_FileOpen(argv[1],"r+");
    if(!file) { Dmod_Printf("Cannot open %s\n",argv[1]); return 1; }
    int rc=command(file,argc,argv);
    Dmod_FileClose(file);
    Dmod_Printf("i2ctest result: %d\n",rc);
    return rc?1:0;
}
