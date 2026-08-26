#include "vic_212x.h"

// MUST BE BUILT -marm. DisableIRQ/RestoreIRQ use mrs/msr, which ARM7TDMI does
// not have in Thumb -- the assembler says "selected processor does not support
// `mrs r0,cpsr' in Thumb mode" and the build stops, which is the good case. The
// rest of the port is Thumb for size; this file is the exception, and the
// interworking is free because everything here is called, never inlined across.
#if defined(__thumb__)
#error "vic_212x.c must be compiled -marm: mrs/msr are not Thumb instructions"
#endif

#define VIC_BASE 0xFFFFF000u

#define VICIRQStatus  (*(volatile uint32_t *)(VIC_BASE + 0x000))
#define VICIntSelect  (*(volatile uint32_t *)(VIC_BASE + 0x00C))
#define VICIntEnable  (*(volatile uint32_t *)(VIC_BASE + 0x010))
#define VICIntEnClr   (*(volatile uint32_t *)(VIC_BASE + 0x014))
#define VICVectAddr   (*(volatile uint32_t *)(VIC_BASE + 0x030))
#define VICDefVectAddr (*(volatile uint32_t *)(VIC_BASE + 0x034))
#define VICVectAddrN(n) (*(volatile uint32_t *)(VIC_BASE + 0x100 + (n)*4))
#define VICVectCntlN(n) (*(volatile uint32_t *)(VIC_BASE + 0x200 + (n)*4))

#define VIC_SLOTS 16
#define VECTCNTL_EN (1u << 5)

// Which slot each source got, or 0xff. The slot NUMBER is the priority, so
// this is also the priority table -- there is nothing else to remember.
static uint8_t slot_of[32];

static void isr_none(void) { }

void Chip_VIC_Init(void)
{
    int i;
    VICIntEnClr = 0xffffffffu;
    VICIntSelect = 0;               // everything is IRQ, nothing is FIQ
    for (i = 0; i < VIC_SLOTS; i++) {
	VICVectCntlN(i) = 0;
	VICVectAddrN(i) = 0;
    }
    for (i = 0; i < 32; i++)
	slot_of[i] = 0xff;
    // A source that fires with no slot lands here rather than at address 0.
    // Without it a stray interrupt executes whatever the reset vector is.
    VICDefVectAddr = (uint32_t)isr_none;
    VICVectAddr = 0;
}

// First free slot, lowest number -- so an earlier caller keeps the higher
// priority. NVIC_SetPriority moves it deliberately.
static int slot_alloc(IRQn_Type irq)
{
    int i;
    if (slot_of[irq] != 0xff)
	return slot_of[irq];
    for (i = 0; i < VIC_SLOTS; i++) {
	if (!(VICVectCntlN(i) & VECTCNTL_EN)) {
	    slot_of[irq] = (uint8_t)i;
	    return i;
	}
    }
    return -1;                      // all sixteen taken
}

void Chip_VIC_SetHandler(IRQn_Type irq, vic_handler_t fn)
{
    int s = slot_alloc(irq);
    if (s < 0)
	return;
    VICVectAddrN(s) = (uint32_t)fn;
    VICVectCntlN(s) = VECTCNTL_EN | (uint32_t)irq;
}

void NVIC_EnableIRQ(IRQn_Type irq)  { VICIntEnable = (1u << irq); }
void NVIC_DisableIRQ(IRQn_Type irq) { VICIntEnClr  = (1u << irq); }

// Priority IS the slot on this controller, so setting it MOVES the source --
// and the slot it wants may be occupied, in which case the two swap. A
// Cortex-M would just write a register; here the ordering is the wiring.
void NVIC_SetPriority(IRQn_Type irq, uint32_t prio)
{
    int from = slot_of[irq];
    uint32_t addr, cntl;
    int i;

    if ((from == 0xff) || (prio >= VIC_SLOTS))
	return;
    if ((int)prio == from)
	return;
    addr = VICVectAddrN(from);
    cntl = VICVectCntlN(from);
    // Whoever is in the target slot goes where we came from.
    if (VICVectCntlN(prio) & VECTCNTL_EN) {
	uint32_t oaddr = VICVectAddrN(prio);
	uint32_t ocntl = VICVectCntlN(prio);
	VICVectAddrN(from) = oaddr;
	VICVectCntlN(from) = ocntl;
	for (i = 0; i < 32; i++)
	    if (slot_of[i] == (uint8_t)prio)
		slot_of[i] = (uint8_t)from;
    } else {
	VICVectCntlN(from) = 0;
	VICVectAddrN(from) = 0;
    }
    VICVectAddrN(prio) = addr;
    VICVectCntlN(prio) = cntl;
    slot_of[irq] = (uint8_t)prio;
}

// Called from the IRQ trampoline in startup. Reading VICVectAddr gives the
// handler the controller selected; writing it back signals end-of-interrupt.
//
// The write must come AFTER the handler returns, or a second interrupt from the
// same source can re-enter before the first is acknowledged.
void Chip_VIC_Dispatch(void)
{
    vic_handler_t fn = (vic_handler_t)VICVectAddr;
    if (fn)
	fn();
    VICVectAddr = 0;
}

// CPSR I/F bits. Returned rather than counted: a nesting counter gets out of
// step the first time a path returns early, and the caller already has a
// natural place to keep the old value.
void EnableIRQ(void)
{
    uint32_t cpsr;
    __asm__ volatile ("mrs %0, cpsr" : "=r"(cpsr));
    __asm__ volatile ("msr cpsr_c, %0" : : "r"(cpsr & ~0xc0u));
}

uint32_t DisableIRQ(void)
{
    uint32_t cpsr;
    __asm__ volatile ("mrs %0, cpsr" : "=r"(cpsr));
    __asm__ volatile ("msr cpsr_c, %0" : : "r"(cpsr | 0xc0));
    return cpsr;
}

void RestoreIRQ(uint32_t cpsr)
{
    __asm__ volatile ("msr cpsr_c, %0" : : "r"(cpsr));
}

// Idle until an interrupt. Here rather than in chip_212x.c because `mcr` is an
// ARM instruction: this file is the one built -marm, and everything else is
// Thumb for size. See chip_212x.h.
void __WFI(void)
{
    __asm__ volatile ("mcr p15, 0, %0, c7, c0, 4" : : "r" (0));
}
