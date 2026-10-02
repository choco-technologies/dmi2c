#include "stm32_common.h"
#include "../../src/validation.h"
#include "dmosi.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Register-side model: data accesses advance RX/TX state, CR2 controls chunks,
 * SR1->SR2 clears ADDR, STOP releases BUSY. Assertions enforce ACK/POS tails.
 * No driver-private functions are replaced. Timing remains separately checked. */
static uint32_t regs[16], rcc[64], polls, critical;
static uint32_t starts, stops, chunks, tx_count, rx_count, remaining;
static uint32_t sysclk, injected;
static bool modern, address_phase, read_direction, addressed, stuck, locked, bus_busy;
static size_t read_size;
static uintptr_t base = 0x40005400UL;
static uint32_t last_tx;

Dmod_Timestamp_t Dmod_GetUptime(void) { return polls++ / 100; }
void Dmod_EnterCritical(void) { ++critical; }
void Dmod_ExitCritical(void) { assert(critical); --critical; }
dmosi_mutex_t dmosi_mutex_create(bool recursive) { (void)recursive; return (void *)1; }
void dmosi_mutex_destroy(dmosi_mutex_t mutex) { assert(mutex); }
int dmosi_mutex_lock(dmosi_mutex_t mutex) { assert(mutex && !locked); locked=true; return 0; }
int dmosi_mutex_unlock(dmosi_mutex_t mutex) { assert(mutex && locked); locked=false; return 0; }
unsigned long dmclk_port_get_current_frequency(void) { return sysclk; }

static uint32_t read_v1(uint32_t reg)
{
    if (reg == V1_SR1)
    {
        if (stuck) return 0;
        if (injected && starts) { if(injected & BIT(9)) { bus_busy=false; address_phase=false; regs[V1_CR1/4] &= ~BIT(8); } return injected; }
        if (address_phase) return BIT(0);
        if (!addressed) return BIT(1);
        return BIT(2) | BIT(6) | BIT(7);
    }
    if (reg == V1_SR2)
    {
        addressed = true;
        if (stuck) return BIT(1);
        return bus_busy ? 3 : 0;
    }
    if (reg == V1_DR)
    {
        size_t left = read_size - rx_count;
        assert(left > 0);
        if (read_size <= 2 || left <= 2) assert(!(regs[V1_CR1 / 4] & BIT(10)));
        if (read_size == 2) assert(regs[V1_CR1 / 4] & BIT(11));
        if (left <= 2 && read_size <= 2) assert(stops || address_phase || read_size == 2);
        ++rx_count;
        return (rx_count - 1) & 255;
    }
    return regs[reg / 4];
}
uint32_t i2c_read(uintptr_t address)
{
    if (address >= RCC_BASE && address < RCC_BASE + sizeof(rcc))
        return rcc[(address - RCC_BASE) / 4];
    assert(address >= base && address < base + sizeof(regs));
    uint32_t reg = address - base;
    if (!modern) return read_v1(reg);
    if (reg == V2_ISR && stuck) return BIT(15);
    if (reg == V2_ISR && injected && starts) { if(injected & BIT(9)) regs[V2_ISR/4]=0; return injected | BIT(15); }
    if (reg == V2_RXDR)
    {
        assert(remaining && read_direction);
        --remaining;
        if (!remaining) regs[V2_ISR / 4] = BIT(15) | (regs[V2_CR2 / 4] & BIT(24) ? BIT(7) : BIT(6));
        return rx_count++ & 255;
    }
    return regs[reg / 4];
}
static void write_v1(uint32_t reg, uint32_t value)
{
    if (reg == V1_CR1)
    {
        if ((value & BIT(8)) && !(regs[reg / 4] & BIT(8)))
        { ++starts; bus_busy=true; address_phase = true; }
        if (value & BIT(9)) { ++stops; bus_busy=false; value &= ~BIT(9); }
    }
    if (reg == V1_DR)
    {
        if (address_phase)
        {
            address_phase = false;
            addressed = false;
            read_direction = value & 1;
            regs[V1_CR1 / 4] &= ~BIT(8);
        }
        else { ++tx_count; last_tx = value; }
    }
    regs[reg / 4] = value;
}
void i2c_write(uintptr_t address, uint32_t value)
{
    if (address >= RCC_BASE && address < RCC_BASE + sizeof(rcc))
    {
        rcc[(address - RCC_BASE) / 4] = value;
        if(address==RCC_APB1RSTR && value) { memset(regs,0,sizeof(regs)); address_phase=addressed=bus_busy=false; }
        return;
    }
    assert(address >= base && address < base + sizeof(regs));
    uint32_t reg = address - base;
    if (!modern) { write_v1(reg, value); return; }
    if (reg == V2_ICR) { regs[V2_ISR / 4] &= ~value; return; }
    if (reg == V2_CR2 && (value & BIT(14)))
    { ++stops; regs[V2_ISR / 4] = BIT(5); value &= ~BIT(14); }
    else if (reg == V2_CR2 && value)
    {
        if (value & BIT(13)) ++starts;
        ++chunks;
        remaining = (value >> 16) & 255;
        read_direction = value & BIT(10);
        regs[V2_ISR / 4] = BIT(15) | (remaining ? (read_direction ? BIT(2) : BIT(1)) : BIT(6));
        value &= ~BIT(13);
    }
    if (reg == V2_TXDR)
    {
        assert(remaining && !read_direction);
        last_tx = value; ++tx_count; --remaining;
        if (!remaining) regs[V2_ISR / 4] = BIT(15) | (regs[V2_CR2 / 4] & BIT(24) ? BIT(7) : BIT(6));
    }
    regs[reg / 4] = value;
}
static void setup(bool v2)
{
    memset(regs, 0, sizeof(regs)); memset(rcc, 0, sizeof(rcc));
    modern=v2; polls=critical=starts=stops=chunks=tx_count=rx_count=remaining=0;
    injected=0; address_phase=addressed=read_direction=stuck=locked=bus_busy=false;
    sysclk=v2 ? 216000000 : 168000000;
    rcc[(RCC_CFGR-RCC_BASE)/4] = 5 << 10; /* APB1 /4 */
    assert(stm32_i2c_init(v2) == 0);
    dmi2c_config_t c = {1, 0x38, 100000, 20};
    assert(dmi2c_port_init(&c) == 0);
    assert(dmi2c_port_init(&c) == -EBUSY);
}
static void test_read(bool v2, size_t size)
{
    setup(v2);
    uint8_t *data=calloc(size, 1), reg=0xa8;
    assert(data); read_size=size;
    dmi2c_message_t messages[]={{0x38,false,&reg,1},{0x38,true,data,size}};
    dmi2c_transfer_t t={messages,2};
    assert(dmi2c_port_transfer(1,&t)==0);
    assert(starts==2 && stops==1 && tx_count==1 && rx_count==size && last_tx==0xa8);
    for(size_t i=0;i<size;++i) assert(data[i]==(uint8_t)i);
    if(v2) assert(chunks==1+(size+254)/255);
    assert(critical==0);
    free(data); assert(stm32_i2c_deinit()==0);
}
static void test_errors(bool v2, uint32_t bits, int expected)
{
    setup(v2); injected=bits;
    uint8_t data=0;
    dmi2c_message_t m={0x38,false,&data,1}; dmi2c_transfer_t t={&m,1};
    assert(dmi2c_port_transfer(1,&t)==expected);
    if(expected==-EAGAIN) assert(stops==0);
    injected=0;
    assert(dmi2c_port_transfer(1,&t)==0); /* reuse after error */
    stm32_i2c_deinit();
}
static void test_busy(bool v2)
{
    setup(v2); stuck=true;
    dmi2c_message_t m={0x38,false,NULL,0}; dmi2c_transfer_t t={&m,1};
    assert(dmi2c_port_transfer(1,&t)==-ETIMEDOUT);
    assert(stops==0 && starts==0);
    stuck=false;
    assert(dmi2c_port_transfer(1,&t)==0);
    assert(starts==1 && stops==1);
    stm32_i2c_deinit();
}
static void test_timing(void)
{
    const uint32_t clocks[]={16000000,42000000,50000000,54000000};
    for(size_t i=0;i<4;++i) for(unsigned fast=0;fast<2;++fast)
    {
        modern=true;
        uint32_t clock=clocks[i], rate=fast?400000:100000;
        assert(i2c_v2_configure(base,clock,rate)==0);
        uint32_t timing=regs[V2_TIMINGR/4];
        double tick=1e12/clock, unit=((timing>>28)+1)*tick;
        double low=((timing&255)+1)*unit;
        double high=(((timing>>8)&255)+1)*unit;
        double setup=(((timing>>20)&15)+1)*unit;
        double hold=((timing>>16)&15)*unit;
        assert(low >= (fast?1300000:4700000));
        assert(high >= (fast?600000:4000000));
        assert(low+high+100000+4*tick >= 1e12/rate);
        assert(setup >= (fast?400000:1250000));
        assert(hold >= 300000-50000-3*tick);
        assert(hold <= (fast?900000:3450000)-(fast?300000:1000000)-260000-4*tick);
    }
    modern=false;
    assert(i2c_v1_configure(base,42000000,100000)==0);
    assert(regs[V1_CCR/4]==210 && regs[V1_TRISE/4]==43);
    assert(i2c_v1_configure(base,42000000,400000)==0);
    assert(regs[V1_CCR/4]==(BIT(15)|35) && regs[V1_TRISE/4]==13);
}
static void test_instances(bool v2)
{
    /* Independently listed CMSIS peripheral bases, not the driver's formula. */
    const uintptr_t expected[]={0x40005400UL,0x40005800UL,0x40005c00UL,0x40006000UL};
    setup(v2);
    assert(dmi2c_port_deinit(1)==0);
    for(unsigned i=0;i<(v2?4U:3U);++i)
    {
        base=expected[i];
        uint32_t selectors=0xaaaaaaaa;
        rcc[(RCC_DCKCFGR2-RCC_BASE)/4]=selectors;
        dmi2c_config_t c={(uint8_t)(i+1),0x38,100000,20};
        assert(dmi2c_port_init(&c)==0);
        assert(rcc[(RCC_APB1ENR-RCC_BASE)/4] & BIT(21+i));
        if(v2) assert(rcc[(RCC_DCKCFGR2-RCC_BASE)/4]==(selectors & ~(3UL<<(16+2*i))));
        assert(dmi2c_port_deinit(i+1)==0);
        assert(rcc[(RCC_DCKCFGR2-RCC_BASE)/4]==selectors);
    }
    base=expected[0];
    dmi2c_config_t invalid={v2?5:4,0x38,100000,20};
    assert(dmi2c_port_init(&invalid)==-EINVAL);
    invalid.instance=1;
    rcc[(RCC_APB1ENR-RCC_BASE)/4]=BIT(21);
    assert(dmi2c_port_init(&invalid)==-EBUSY);
    assert(rcc[(RCC_APB1ENR-RCC_BASE)/4]==BIT(21));
    stm32_i2c_deinit();
}
static void test_deadline(bool v2)
{
    setup(v2);
    uint8_t *data=calloc(4096,1); assert(data); read_size=4096;
    dmi2c_message_t m={0x38,true,data,4096}; dmi2c_transfer_t t={&m,1};
    assert(dmi2c_port_transfer(1,&t)==-ETIMEDOUT);
    assert(rx_count>0 && rx_count<4096);
    free(data); stm32_i2c_deinit();
}
static void test_read_then_write(bool v2)
{
    setup(v2); read_size=2;
    uint8_t rx[2]={0}, tx=0xa8;
    dmi2c_message_t m[]={{0x38,true,rx,2},{0x38,false,&tx,1}};
    dmi2c_transfer_t t={m,2};
    assert(dmi2c_port_transfer(1,&t)==0);
    assert(starts==2 && stops==1 && rx_count==2 && tx_count==1);
    stm32_i2c_deinit();
}
int main(void)
{
    const size_t sizes[]={1,2,3,4,255,256,510,511};
    for(unsigned v2=0;v2<2;++v2)
    {
        for(size_t i=0;i<sizeof(sizes)/sizeof(*sizes);++i) test_read(v2,sizes[i]);
        test_errors(v2,v2?BIT(4):BIT(10),-ENXIO);
        test_errors(v2,BIT(9),-EAGAIN);
        test_errors(v2,BIT(8),-EIO);
        test_errors(v2,v2?BIT(10):BIT(11),-EOVERFLOW);
        test_busy(v2);
        test_instances(v2);
        test_deadline(v2);
        test_read_then_write(v2);
    }
    test_timing();
    puts("PASS: F4/F7 tails, repeated START, 255-byte boundaries, NACK/ARLO/BERR/OVR, BUSY deadlines, recovery, timing constraints");
    return 0;
}
