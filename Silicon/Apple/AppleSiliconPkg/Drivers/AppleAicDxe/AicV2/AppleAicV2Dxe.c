/**
 * @file AppleAicV2Dxe.c
 * amarioguy (Arminder Singh)
 * 
 * AICv2 specific DXE initialization/driver code.
 * 
 * @version 1.0
 * @date 2022-09-29
 * 
 * @copyright Copyright (c) amarioguy (Arminder Singh), 2022.
 * 
 * Copyright (c) 2026 Aurora Silicon
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 * 
 */

#include <Library/AppleAicLib.h>
#include "AppleAicDxe.h"
#include <Library/AppleSysRegs.h>
#include <Library/ArmGenericTimerCounterLib.h>
#include <Library/AppleDTLib.h>
#include <Library/PcdLib.h>

#define APPLE_FAST_IPI_STATUS_PENDING BIT(0)
#define AIC_TIMER_REFLECT_CALL_MAGIC 0x4e54414943ULL /* "NTAIC" */

STATIC UINT64 AicV2Base;
AIC_INFO_STRUCT *AicInfoStruct;
STATIC UINT64 mAicV2IrqCfgOffset, mAicV2SoftwareSetRegOffset;
STATIC UINT64 mAicV2SoftwareClearRegOffset, mAicV2IrqMaskSetOffset;
STATIC UINT64 mAicV2IrqMaskClearOffset, mAicV2HwStateOffset;
STATIC UINT64 mAicV2EventReg;
STATIC APPLE_AIC_VERSION mAicVersion;

typedef struct {
    CHAR8           Magic[8];
    volatile UINT64 Stage;
    volatile UINT64 Value0;
    volatile UINT64 Value1;
    volatile UINT64 Status;
} AURORA_AIC_TRACE;

// Host-readable bring-up breadcrumbs.  Keep the magic initialized so the
// block can be found in the relocated DXE image after an EL2 watchdog break.
STATIC volatile AURORA_AIC_TRACE mAuroraAicTrace = {
    { 'A', 'U', 'R', 'A', 'I', 'C', '0', '1' },
    0,
    0,
    0,
    0
};

#define AURORA_AIC_STAGE(StageValue, FirstValue, SecondValue, StatusValue) \
    do {                                                                  \
        mAuroraAicTrace.Value0 = (UINT64)(FirstValue);                    \
        mAuroraAicTrace.Value1 = (UINT64)(SecondValue);                   \
        mAuroraAicTrace.Status = (UINT64)(StatusValue);                   \
        mAuroraAicTrace.Stage  = (UINT64)(StageValue);                    \
    } while (0)

STATIC EFI_STATUS EFIAPI AppleAicV2CalculateRegisterOffsets(IN VOID);

STATIC VOID
AppleAicV2CompleteReflectedTimer (
    IN UINTN Source
    )
{
    register UINTN Magic __asm__ ("x0") = AIC_TIMER_REFLECT_CALL_MAGIC;
    register UINTN TimerSource __asm__ ("x1") = Source;

    // The AIC software IRQ is the delivery half of the m1n1 native-AIC timer
    // ABI. Explicitly tell EL2 that TimerDxe has reprogrammed the deadline;
    // relying on CNTP/CNTV accesses to trap is not valid under every VHE/ECV
    // configuration used by these Apple CPUs.
    __asm__ __volatile__ (
        "smc #0"
        : "+r" (Magic), "+r" (TimerSource)
        :
        : "memory"
        );
}

STATIC VOID
AppleAicV2ClearSoftwareInterrupt (
    IN UINT32 Source
    )
{
    MmioWrite32 (
        AicV2Base + mAicV2SoftwareClearRegOffset + AIC_MASK_REG (Source),
        AIC_MASK_BIT (Source)
        );
}

VOID
AppleAicV2PrepareTimerInterrupt (
    IN HARDWARE_INTERRUPT_SOURCE Source
    )
{
    UINT32 Interrupt;
    UINT32 RangeStart;
    UINT32 RangeEnd;

    if (Source == 17) {
        RangeStart = AicInfoStruct->NumIrqs - (2 * AIC_TIMER_REFLECT_CPU_SLOTS);
        RangeEnd = AicInfoStruct->NumIrqs - AIC_TIMER_REFLECT_CPU_SLOTS;
    } else if (Source == 18) {
        RangeStart = AicInfoStruct->NumIrqs - AIC_TIMER_REFLECT_CPU_SLOTS;
        RangeEnd = AicInfoStruct->NumIrqs;
    } else {
        return;
    }

    if (AicInfoStruct->NumIrqs <= (2 * AIC_TIMER_REFLECT_CPU_SLOTS)) {
        return;
    }

    // TimerDxe registers sources 17 and 18 before it initializes mTimerTicks or
    // enables the architectural timer.  Start each reflection block from an
    // empty state at that registration boundary, then unmask it.  A software
    // event retained across a resident-m1n1 chainload is stale; replaying or
    // synthesizing it here can enter TimerInterruptHandler with mTimerTicks ==
    // 0 and make its CompareValue catch-up loop non-terminating.
    //
    // m1n1 holds timer FIQ reflection until TimerDxe's final ENABLE=1 control
    // write, so clearing these slots cannot discard a valid post-arm tick.
    for (Interrupt = RangeStart; Interrupt < RangeEnd; Interrupt++) {
        AppleAicV2ClearSoftwareInterrupt (Interrupt);
        AppleAicUnmaskInterrupt (
            AicV2Base,
            Interrupt,
            mAicV2IrqMaskClearOffset
            );
    }
}

extern EFI_HARDWARE_INTERRUPT_PROTOCOL   gHardwareInterruptAicV2Protocol;
extern EFI_HARDWARE_INTERRUPT2_PROTOCOL  gHardwareInterrupt2AicV2Protocol;

/**
 * Masks an IRQ on an AICv2 platform.
 * 
 * @param This - pointer to interrupt protocol instance
 * @param Source - IRQ number.
 * @return EFI_SUCCESS if successful, ASSERTs if source is > than max IRQs
 */

STATIC EFI_STATUS EFIAPI AppleAicV2MaskInterrupt(
    IN EFI_HARDWARE_INTERRUPT_PROTOCOL *This,
    IN HARDWARE_INTERRUPT_SOURCE Source
)
{
    if(Source >= AicInfoStruct->MaxIrqs)
    {
        DEBUG((DEBUG_INFO, "%a: Cannot mask IRQ higher than maximum supported IRQ!\n", __FUNCTION__));
        ASSERT(FALSE);
        return EFI_UNSUPPORTED;
    }

    AppleAicMaskInterrupt(AicV2Base, Source, mAicV2IrqMaskSetOffset);
    return EFI_SUCCESS;
}

/**
 * Unmasks an IRQ on an AICv2 platform.
 * 
 * @param This - pointer to interrupt protocol instance
 * @param Source - IRQ number.
 * @return EFI_SUCCESS if successful, ASSERTs if source is > than max IRQs
 */

STATIC EFI_STATUS EFIAPI AppleAicV2UnmaskInterrupt(
    IN EFI_HARDWARE_INTERRUPT_PROTOCOL *This,
    IN HARDWARE_INTERRUPT_SOURCE Source
)
{
    if(Source >= AicInfoStruct->MaxIrqs)
    {
        DEBUG((DEBUG_INFO, "%a: Cannot unmask IRQ higher than maximum supported IRQ!\n", __FUNCTION__));
        ASSERT(FALSE);
        return EFI_UNSUPPORTED;
    }

    AppleAicUnmaskInterrupt(AicV2Base, Source, mAicV2IrqMaskClearOffset);
    return EFI_SUCCESS;
}

/**
 * Gets the state of a particular interrupt on the platform.
 * 
 * TODO: figure out if we need to read HW_STATE or IRQ_CFG to read the state of interrupts
 * 
 * @param This - Interrupt protocol instance pointer
 * @param Source - IRQ number
 * @param State - pointer to BOOLEAN that holds the actual interrupt state. TRUE if enabled, FALSE if disabled.
 * @return EFI_SUCCESS if successful, ASSERTs if Source >= MAX_IRQs 
 */
STATIC EFI_STATUS EFIAPI AppleAicV2GetInterruptState(
    IN EFI_HARDWARE_INTERRUPT_PROTOCOL *This,
    IN HARDWARE_INTERRUPT_SOURCE Source,
    OUT BOOLEAN *State
)
{
    if(Source >= AicInfoStruct->MaxIrqs)
    {
        ASSERT(FALSE);
        return EFI_UNSUPPORTED;
    }
    *State = AppleAicReadInterruptState(AicV2Base, Source, mAicV2HwStateOffset);
    return EFI_SUCCESS;
}



/**
 * Sends the EOI signal to hardware.
 * 
 * @param This - interrupt protocol instance pointer
 * @param Source - IRQ number
 * @return EFI_SUCCESS if successful.
 */
STATIC EFI_STATUS EFIAPI AppleAicV2EndOfInterrupt(
    IN EFI_HARDWARE_INTERRUPT_PROTOCOL *This,
    IN HARDWARE_INTERRUPT_SOURCE Source
)
{
    // IRQ numbers 17, 18, and 19 are the firmware's logical timer sources.
    // Advancing the compare value in TimerInterruptHandler deasserts the timer;
    // disabling it here leaves the periodic DXE timer permanently stopped
    // because ArmGenericTimerReenableTimer() is intentionally a no-op.
    if ((Source == 17) || (Source == 18) || (Source == 19))
    {
        return EFI_SUCCESS;
    }

    //reading the interrupt source in the event register acks and masks it at the same time
    //all we need to do is unmask it here. (note that for hardware IRQs, this assumes the hardware interrupt source has been cleared)
    AppleAicUnmaskInterrupt(AicV2Base, Source, mAicV2IrqMaskClearOffset);

    return EFI_SUCCESS;
}


/**
 * Calculate the AIC register offsets on the platform.
 * 
 * @return EFI_SUCCESS if successful, EFI_ERROR(-1) if an error occurred.
 */
STATIC EFI_STATUS EFIAPI AppleAicV2CalculateRegisterOffsets(IN VOID)
{
    AURORA_AIC_STAGE (0x10, mAicVersion, 0, 0);
    dt_node_t *InterruptControllerNode = dt_get("aic");
    AURORA_AIC_STAGE (0x11, InterruptControllerNode, 0, 0);
    if (!InterruptControllerNode) {
        DEBUG((EFI_D_INFO | EFI_D_LOAD | EFI_D_ERROR, "no ADT supplied, exiting\n"));
        ASSERT(FALSE);
    }




    UINT64 StartOffset;
    UINT64 CurrentOffset;
    /**
     * Start with calculations that can be done with purely MMIO reads.
     * (AIC base is static, and the other values are easily derived through MMIO reads and bitwise ANDs.)
     * 
     */
    
    dt_node_reg(InterruptControllerNode, 0, &AicV2Base, NULL);

    // AICv3 relocates its capability registers and stores their offsets in the
    // ADT rather than at the fixed AICv2 positions (matches m1n1 aic23_init()).
    // Discover cap0/maxnumirq offsets and publish them to AppleAicLib BEFORE
    // reading NumIrqs/MaxIrqs. They default to the AICv2 positions, so a machine
    // that omits the properties still reads correctly.
    UINT32 V3Cap0Offset      = AIC_V2_INFO_REG1;
    UINT32 V3MaxNumIrqOffset = AIC_V2_INFO_REG3;
    if (mAicVersion == APPLE_AIC_VERSION_3) {
        // AppleDTLib treats an absent property as a contract violation even
        // when a caller supplies a fallback. Never ask an AICv1/v2 ADT for
        // AICv3-only fields: J414s is AICv2 and intentionally has neither.
        V3Cap0Offset = (UINT32)dt_node_u32(
            InterruptControllerNode, "cap0-offset", AIC_V2_INFO_REG1);
        V3MaxNumIrqOffset = (UINT32)dt_node_u32(
            InterruptControllerNode, "maxnumirq-offset", AIC_V2_INFO_REG3);
        AppleAicV3SetDynamicOffsets(V3Cap0Offset, V3MaxNumIrqOffset);
    }

    AicInfoStruct->NumIrqs = AppleAicGetNumInterrupts(AicV2Base);
    AURORA_AIC_STAGE (0x13, AicV2Base, AicInfoStruct->NumIrqs, 0);
    AicInfoStruct->MaxIrqs = AppleAicGetMaxInterrupts(AicV2Base);
    AURORA_AIC_STAGE (0x14, AicInfoStruct->NumIrqs, AicInfoStruct->MaxIrqs, 0);

    if(mAicVersion == APPLE_AIC_VERSION_1){
        AicInfoStruct->MaxCpuDies = 1;
        AicInfoStruct->NumCpuDies = 1;
    }
    else if(mAicVersion == APPLE_AIC_VERSION_2){
        AicInfoStruct->MaxCpuDies = FIELD_GET(AIC_V2_INFO_REG3_MAX_DIE_COUNT_BITFIELD, MmioRead32(AicV2Base + AIC_V2_INFO_REG3));
        AicInfoStruct->NumCpuDies = (FIELD_GET(AIC_V2_INFO_REG1_LAST_CPU_DIE_BITFIELD, MmioRead32(AicV2Base + AIC_V2_INFO_REG1))) + 1;
    }
    else if(mAicVersion == APPLE_AIC_VERSION_3){
        // AICv3 shares the AICv2 INFO1/INFO3 die bitfields, read at the
        // ADT-published capability offsets. Die width is treated as 4 bits as on
        // AICv2; revisit if a multi-die M3-class part reports otherwise.
        AicInfoStruct->MaxCpuDies = FIELD_GET(AIC_V2_INFO_REG3_MAX_DIE_COUNT_BITFIELD, MmioRead32(AicV2Base + V3MaxNumIrqOffset));
        AicInfoStruct->NumCpuDies = (FIELD_GET(AIC_V2_INFO_REG1_LAST_CPU_DIE_BITFIELD, MmioRead32(AicV2Base + V3Cap0Offset))) + 1;
    }

    
    /**
     * Calculate the offsets of registers that are dependent on the maximum number of IRQs supported.
     * 
     * The event register offset must be pulled from the devicetree, as it's the only place
     * where the value is stored, it cannot be read from MMIO.
     * 
     */

    /**
     * From IRQ_CFG + sizeof(UINT32) * MaxIrqs, the AIC registers are separated
     * by an offset of (sizeof(UINT32) * (MaxIrqs >> 5)).
     * 
     */
    StartOffset = mAicV2IrqCfgOffset = AIC_V2_IRQ_CFG_REG;

    if(mAicVersion == APPLE_AIC_VERSION_1){
        // AICv1 has no IRQ_CFG block; its per-die register banks follow the
        // TARGET_CPU array (one u32 per IRQ). Seeding the layout from TARGET_CPU
        // reproduces the Asahi/m1n1 v1 offsets (SW_SET=0x4000, SW_CLR=0x4080,
        // MASK_SET=0x4100, MASK_CLR=0x4180, die_stride=0x1200).
        StartOffset = mAicV2IrqCfgOffset = AIC_TARGET_CPU;
        // The AICv1 event/ack register lives at a fixed MMIO offset (0x2004),
        // not in a separate reg entry. The previous `AicV2Base` (base+0) read
        // acknowledged the wrong register on AICv1 hardware.
        mAicV2EventReg = AicV2Base + AIC_V1_EVENT_REG;
    }
    else if(mAicVersion == APPLE_AIC_VERSION_2){
        mAicV2EventReg = AicV2Base + dt_node_u32(InterruptControllerNode, "aic-iack-offset", 0);
        AURORA_AIC_STAGE (0x16, mAicV2EventReg, 0, 0);

        /**
         * The external interrupt config block is RELOCATABLE on AICv3.
         *
         * AppleArmGetAicVersion() maps "aic,3" onto APPLE_AIC_VERSION_2, so this
         * path also runs on AICv3 hardware -- but there the config base is not
         * the AICv2 constant. On T8142 (Apple M5) the ADT reports
         * extint-baseaddress = 0x10000, whereas AIC_V2_IRQ_CFG_REG is 0x2000.
         *
         * This matters a great deal, because SW_SET, SW_CLEAR, MASK_SET,
         * MASK_CLEAR and HW_STATE are all derived from StartOffset below. Left
         * hardcoded, every mask and unmask would land 0xE000 low, in the middle
         * of unrelated AIC registers, and no interrupt would ever be delivered.
         *
         * m1n1 treats this property as mandatory on AICv3 and optional on AICv2
         * (see its aic23_init()). Mirror that: prefer the ADT value, fall back to
         * the AICv2 constant so genuine AICv2 parts that omit the property keep
         * their existing behaviour.
         *
         * NOTE: dt_node_u32() takes an INDEX, not a default, and asserts when the
         * property is absent -- so the presence check has to go through
         * dt_node_prop().
         */
        size_t ExtIntBaseSize = 0;
        UINT32 *ExtIntBase = dt_node_prop(InterruptControllerNode, "extint-baseaddress", &ExtIntBaseSize);

        if ((ExtIntBase != NULL) && (ExtIntBaseSize >= sizeof(UINT32))) {
            StartOffset = mAicV2IrqCfgOffset = *ExtIntBase;
            DEBUG((DEBUG_INFO, "AIC: external interrupt config base from ADT: 0x%lx\n", StartOffset));
        } else {
            DEBUG((DEBUG_INFO, "AIC: no extint-baseaddress, using AICv2 default 0x%lx\n", StartOffset));
        }
        AURORA_AIC_STAGE (0x17, StartOffset, ExtIntBaseSize, 0);
    }
    else if(mAicVersion == APPLE_AIC_VERSION_3){
        // AICv3 moves the IRQ_CFG block to 0x10000 (ADT-relocatable via
        // extint-baseaddress). Its event register is a separate page named by
        // aic-iack-offset, exactly as on AICv2.
        StartOffset = mAicV2IrqCfgOffset =
            (UINT64)dt_node_u32(InterruptControllerNode, "extint-baseaddress", AIC_V3_IRQ_CFG_REG);
        mAicV2EventReg = AicV2Base + dt_node_u32(InterruptControllerNode, "aic-iack-offset", 0);
    }

    CurrentOffset = StartOffset + sizeof(UINT32) * AicInfoStruct->MaxIrqs;

    //SW_SET
    mAicV2SoftwareSetRegOffset = CurrentOffset;
    CurrentOffset += sizeof(UINT32) * (AicInfoStruct->MaxIrqs >> 5);
    //SW_CLEAR
    mAicV2SoftwareClearRegOffset = CurrentOffset;
    CurrentOffset += sizeof(UINT32) * (AicInfoStruct->MaxIrqs >> 5);
    //MASK_SET
    mAicV2IrqMaskSetOffset = CurrentOffset;
    CurrentOffset += sizeof(UINT32) * (AicInfoStruct->MaxIrqs >> 5);
    //MASK_CLEAR
    mAicV2IrqMaskClearOffset = CurrentOffset;
    CurrentOffset += sizeof(UINT32) * (AicInfoStruct->MaxIrqs >> 5);
    CurrentOffset += sizeof(UINT32) * (AicInfoStruct->MaxIrqs >> 5);
    //HW_STATE (TODO: what is this reg meant for?)
    mAicV2HwStateOffset = CurrentOffset;

    /**
     * Per-die register stride.
     *
     * AICv3 publishes the strides in the ADT rather than implying them from the
     * register layout. T8142 reports extintrcfg-stride / intmaskset-stride /
     * intmaskclear-stride, all 0x4a00, which is not what the computation below
     * produces.
     *
     * m1n1 tracks all three separately; this driver has only one DieStride, so
     * take extintrcfg-stride when present. That is correct as long as the three
     * agree -- they do on T8142. If a future part reports differing strides this
     * needs splitting up the way m1n1 does.
     *
     * This is currently LATENT rather than active: DieStride is only ever
     * multiplied by (Source / MaxIrqs), which is 0 on a single-die part, and the
     * base M5 is single-die. It would bite on a multi-die chip.
     */
    AicInfoStruct->DieStride = CurrentOffset - StartOffset;
    // AICv1's register banks occupy a fixed 0x8000 window and its event register
    // (base+0x2004) sits BELOW them, so the v2 "event page" size formula would
    // undersize the region. AICv2/v3 keep the event-page-relative size because
    // their event register is a separate, higher page.
    if (mAicVersion == APPLE_AIC_VERSION_1) {
        AicInfoStruct->RegSize = AIC_REG_SIZE;
    } else {
        AicInfoStruct->RegSize = (mAicV2EventReg - AicV2Base) + 4;
    }


    size_t ExtIntrCfgStrideSize = 0;
    UINT32 *ExtIntrCfgStride = dt_node_prop(InterruptControllerNode, "extintrcfg-stride", &ExtIntrCfgStrideSize);

    if ((ExtIntrCfgStride != NULL) && (ExtIntrCfgStrideSize >= sizeof(UINT32))) {
        AicInfoStruct->DieStride = *ExtIntrCfgStride;
        DEBUG((DEBUG_INFO, "AIC: die stride from ADT: 0x%lx\n", AicInfoStruct->DieStride));
    }
    AicInfoStruct->RegSize = (mAicV2EventReg - AicV2Base) + 4;
    AURORA_AIC_STAGE (0x19, AicInfoStruct->DieStride, AicInfoStruct->RegSize, 0);
    
    return EFI_SUCCESS;

}

/**
 * EFI_CPU_INTERRUPT_HANDLER entered when a processor interrupt is taken.
 * 
 * Note that this handler handles both FIQs and IRQs. As SErrors are very bad at this stage of boot, let
 * the default exception handler deal with those and panic the system.
 * 
 * @param InterruptType - type of interrupt taken. will be EXCEPT_AARCH64_{IRQ, FIQ} for IRQs and FIQs respectively.
 * @param SystemContext - CPU context snapshot when interrupt was taken
 * @return Nothing. 
 */
STATIC VOID EFIAPI AppleAicV2InterruptHandler(
    IN EFI_EXCEPTION_TYPE InterruptType,
    IN EFI_SYSTEM_CONTEXT SystemContext
)
{
    UINT32 AicEvent;
    UINT32 AicEventType;
    UINT32 AicInterrupt;
    UINT32 TimerPhysBase;
    UINT32 TimerVirtBase;
    HARDWARE_INTERRUPT_HANDLER HwInterruptHandler;
    HARDWARE_INTERRUPT_HANDLER TimerInterruptHandlerPhys;
    HARDWARE_INTERRUPT_HANDLER TimerInterruptHandlerVirt;
    UINT64 PmcStatus;
    UINT64 UncorePmcStatus;

    // Keep the last interrupt-path operation in guest RAM.  DEBUG output is
    // not dependable before the serial stack is up on J813, while the EL2
    // watchdog can still recover this trace from a halted guest.
    AURORA_AIC_STAGE (0x40, InterruptType, SystemContext.SystemContextAArch64, 0);
    AicEvent = AppleAicAcknowledgeInterrupt(mAicV2EventReg);
    AURORA_AIC_STAGE (0x41, InterruptType, AicEvent, 0);
    // The event register is not a bare interrupt number: bits 31:24 are the
    // die and bits 23:16 are the event type. Indexing the handler table with
    // the raw word walks far beyond the allocation for every ordinary IRQ.
    AicEventType = FIELD_GET (AIC_EVENT_INTERRUPT_TYPE, AicEvent);
    AicInterrupt = FIELD_GET (AIC_EVENT_IRQ_NUM, AicEvent);
    AURORA_AIC_STAGE (0x42, AicEventType, AicInterrupt, 0);
    HwInterruptHandler = NULL;
    if (AicInterrupt < AicInfoStruct->MaxIrqs) {
        HwInterruptHandler = AicRegisteredInterruptHandlers[AicInterrupt];
    }

    TimerPhysBase = AicInfoStruct->NumIrqs - (2 * AIC_TIMER_REFLECT_CPU_SLOTS);
    TimerVirtBase = AicInfoStruct->NumIrqs - AIC_TIMER_REFLECT_CPU_SLOTS;

    // Dispatch reflected timers from the AIC event itself. Its type and
    // number are authoritative; on J414s the event was acknowledged while the
    // CPU exception-type split below failed to classify it as an ordinary IRQ.
    if (AicEventType == 1) {
        if ((AicInterrupt >= TimerPhysBase) && (AicInterrupt < TimerVirtBase)) {
            AURORA_AIC_STAGE (0x43, AicInterrupt, TimerPhysBase, 0);
            AppleAicV2ClearSoftwareInterrupt (AicInterrupt);
            TimerInterruptHandlerPhys = AicRegisteredInterruptHandlers[17];
            if (TimerInterruptHandlerPhys != NULL) {
                TimerInterruptHandlerPhys (17, SystemContext);
                AppleAicV2CompleteReflectedTimer (17);
                // Reading AIC_EVENT acknowledged and masked the physical
                // reflected source. TimerDxe EOIs logical source 17, whose
                // EOI is intentionally a no-op, so explicitly unmask the
                // backing software IRQ after clearing and servicing it.
                AppleAicUnmaskInterrupt (
                    AicV2Base,
                    AicInterrupt,
                    mAicV2IrqMaskClearOffset
                    );
            }
            return;
        }

        if ((AicInterrupt >= TimerVirtBase) && (AicInterrupt < AicInfoStruct->NumIrqs)) {
            AURORA_AIC_STAGE (0x44, AicInterrupt, TimerVirtBase, 0);
            AppleAicV2ClearSoftwareInterrupt (AicInterrupt);
            TimerInterruptHandlerVirt = AicRegisteredInterruptHandlers[18];
            if (TimerInterruptHandlerVirt != NULL) {
                TimerInterruptHandlerVirt (18, SystemContext);
                AppleAicV2CompleteReflectedTimer (18);
                AppleAicUnmaskInterrupt (
                    AicV2Base,
                    AicInterrupt,
                    mAicV2IrqMaskClearOffset
                    );
            }
            return;
        }
    }

    /**
     * In the FIQ case, every possible FIQ source must be checked to avoid an interrupt storm.
     * (Fast IPIs, timers, performance counters)
     * 
     * In the case of timers, both paths are checked, but in practice only one set of timers (phys or virt)
     * will be acted on due to how this firmware is configured.
     * 
     */
    if (InterruptType == EXCEPT_AARCH64_FIQ) {
        AURORA_AIC_STAGE (0x45, AicEvent, AicInterrupt, 0);
        /**
         * 
         * Fast IPIs are not implemented yet, acknowledge but do not act upon them.
         * (Ideally, they won't be necessary at all given the nature of the firmware)
         * 
         */
        if (AppleAicV2ReadIpiStatusRegister() & APPLE_FAST_IPI_STATUS_PENDING) {
            DEBUG((DEBUG_INFO, "Fast IPIs not supported yet, acking\n"));
            AppleAicV2WriteIpiStatusRegister(APPLE_FAST_IPI_STATUS_PENDING);
        }
        AURORA_AIC_STAGE (0x46, ArmReadCntpCtl(), ArmReadCntvCtl(), 0);

        /**
         * Timers
         * 
         * Apple Silicon uses standard ARM timers, but they are wired to FIQ. 
         * Hand off the FIQ as if it were a standard timer IRQ.
         * 
         * (Note that the interrupt number is software defined as timers do not have an associated hardware number)
         * 
         */
        if (
            ((ArmReadCntpCtl()) & 
            (ARM_ARCH_TIMER_ENABLE | ARM_ARCH_TIMER_IMASK | ARM_ARCH_TIMER_ISTATUS)) == (ARM_ARCH_TIMER_ENABLE | ARM_ARCH_TIMER_ISTATUS))
        {
            TimerInterruptHandlerPhys = AicRegisteredInterruptHandlers[17];
            //ArmGenericTimerDisableTimer();
            if(TimerInterruptHandlerPhys != NULL)
            {

                //for now we hardcode the timer interrupt to 17.
                TimerInterruptHandlerPhys(17, SystemContext);
            }
            else
            {
                // A stale architectural timer can already be enabled and
                // expired when CpuDxe restores the pre-DXE interrupt state.
                // TimerDxe has not necessarily registered source 17 yet, so
                // asserting here deadlocks the entire dispatcher.  Mask the
                // unowned timer until TimerDxe programs its compare value and
                // writes a clean ENABLE control value after registration.
                UINTN TimerCtrlReg = ArmReadCntpCtl ();
                AURORA_AIC_STAGE (0x4B, TimerCtrlReg, 17, 0);
                ArmWriteCntpCtl (TimerCtrlReg | ARM_ARCH_TIMER_IMASK);
                return;
            }

        }
        else if ((ArmReadCntvCtl() & (ARM_ARCH_TIMER_ENABLE | ARM_ARCH_TIMER_IMASK | ARM_ARCH_TIMER_ISTATUS)) == (ARM_ARCH_TIMER_ENABLE | ARM_ARCH_TIMER_ISTATUS))
        {
            TimerInterruptHandlerVirt = AicRegisteredInterruptHandlers[18];
            if(TimerInterruptHandlerVirt != NULL)
            {
                //ditto for the virtual timer.
                TimerInterruptHandlerVirt(18, SystemContext);
            }
            else
            {
                // Apply the same deferral to an early virtual timer.  The
                // selected TimerDxe library clears IMASK when it publishes the
                // timer protocol, so this does not suppress the real clock.
                UINTN TimerCtrlReg = ArmReadCntvCtl ();
                AURORA_AIC_STAGE (0x4C, TimerCtrlReg, 18, 0);
                ArmWriteCntvCtl (TimerCtrlReg | ARM_ARCH_TIMER_IMASK);
                return;
            }
        }

        /**
         * 
         * PMC and Uncore PMC are both not implemented at this time, like Fast IPIs.
         * As with those, ack the interrupts if they come but don't act on them.
         * 
         */
        if (PcdGet32 (PcdAppleSocIdentifier) != 0x8142) {
            PmcStatus = AppleAicV2ReadPmcControlRegister();
            AURORA_AIC_STAGE (0x47, PmcStatus, 0, 0);
            UncorePmcStatus = AppleAicV2ReadUncorePmcControlRegister();
            AURORA_AIC_STAGE (0x48, PmcStatus, UncorePmcStatus, 0);
            if (PmcStatus & BIT11) {
                DEBUG((DEBUG_INFO, "PMCR0 FIQ asserted, unsupported, acking\n"));
                PmcStatus = PmcStatus & ~(BIT18 | BIT17 | BIT16);
                PmcStatus |= (BIT18 | BIT17 | BIT16 | BIT0);
                AppleAicV2WritePmcControlRegister(PmcStatus);
            }
            else if (FIELD_GET(APPLE_UPMCR0_IMODE, UncorePmcStatus) == APPLE_UPMCR_FIQ_IMODE && (AppleAicV2ReadUncorePmcStatusRegister() & APPLE_UPMSR_IACT))
            {
                DEBUG((DEBUG_INFO, "Uncore PMC FIQ asserted, unsupported, acking\n"));
                UncorePmcStatus = UncorePmcStatus & ~(APPLE_UPMCR0_IMODE);
                UncorePmcStatus |= APPLE_UPMCR_OFF_IMODE;
                AppleAicV2WriteUncorePmcControlRegister(UncorePmcStatus);
            }
        } else {
            // T8142 does not expose these Apple implementation-defined PMC
            // registers safely to EL1.  This driver has no PMC consumer yet,
            // so probing them only traps the DXE interrupt dispatcher.
            AURORA_AIC_STAGE (0x4D, 0x8142, 0, 0);
        }
    }


    /**
     * The IRQ case is much simpler, in all cases, read the event register, figure out what device
     * the IRQ originated from, jump to the IRQ handler assigned for that device.
     * 
     * The software interrupt numbers have a one to one relationship with the hardware's understanding
     * of the interrupt numbers.
     * 
     */
    else if (InterruptType == EXCEPT_AARCH64_IRQ) {
        AURORA_AIC_STAGE (0x49, AicInterrupt, HwInterruptHandler, 0);
        if(HwInterruptHandler != NULL) {
            HwInterruptHandler(AicInterrupt, SystemContext);
            AURORA_AIC_STAGE (0x4A, AicInterrupt, HwInterruptHandler, 0);
        }
        else
        {
            //if an interrupt is unassigned, ack it and exit.
            DEBUG((DEBUG_ERROR, "Unassigned AIC IRQ: 0x%x\n", AicInterrupt));
            AppleAicV2EndOfInterrupt(&gHardwareInterruptAicV2Protocol, AicInterrupt);
        }

    }
}

// AIC protocol instance
EFI_HARDWARE_INTERRUPT_PROTOCOL gHardwareInterruptAicV2Protocol = {
  RegisterInterruptSource,
  AppleAicV2UnmaskInterrupt,
  AppleAicV2MaskInterrupt,
  AppleAicV2GetInterruptState,
  AppleAicV2EndOfInterrupt
};


/**
 * Gets the type of trigger for a given IRQ.
 * 
 * @param This 
 * @param Source 
 * @param TriggerType 
 * @return STATIC 
 */
STATIC EFI_STATUS EFIAPI AppleAicV2GetIrqTriggerType(
    IN EFI_HARDWARE_INTERRUPT2_PROTOCOL *This,
    IN HARDWARE_INTERRUPT_SOURCE Source,
    OUT EFI_HARDWARE_INTERRUPT2_TRIGGER_TYPE *TriggerType
)
{
    dt_node_t *ApcieNode = dt_get("apcie");
    if (!ApcieNode) {
        DEBUG((EFI_D_ERROR, "no ADT supplied, exiting\n"));
        ASSERT(FALSE);
    }


    UINT32 EdgeTriggeredIrqNumStart = dt_node_u32(ApcieNode, "msi-vector-offset", 0);
    UINT32 EdgeTriggeredIrqNums = dt_node_u32(ApcieNode, "msi-vectors", 0);
    BOOLEAN IsEdgeTriggeredIrq = FALSE;
    /**
     * AIC does not have a facility to see if a given IRQ is level or edge triggered,
     * however, other than PCIe MSIs, every other IRQ is a level triggered interrupt.
     * 
     * As such, get the PCIe MSI IRQ number range from the DT, compare it to Source, and if it matches any of the IRQ numbers,
     * state that this is a edge rising triggered interrupt. In all other cases, unconditionally indicate
     * a level triggered interrupt.
     * 
     * TODO: some interrupts are level low triggered (namely I2C interrupts), account for these.
     */

    DEBUG((DEBUG_VERBOSE, "Edge triggered IRQ number start: %d", EdgeTriggeredIrqNumStart));
    for(UINT64 IrqNum = EdgeTriggeredIrqNumStart; IrqNum < (EdgeTriggeredIrqNumStart + EdgeTriggeredIrqNums); IrqNum++)
    {
        if(Source == IrqNum)
        {
            DEBUG((DEBUG_VERBOSE, "%d is an edge triggered IRQ\n", (UINT32)Source));
            IsEdgeTriggeredIrq = TRUE;
            break;
        }
    }
    if(IsEdgeTriggeredIrq)
    {
        *TriggerType = EFI_HARDWARE_INTERRUPT2_TRIGGER_EDGE_RISING;
    }
    else {
        *TriggerType = EFI_HARDWARE_INTERRUPT2_TRIGGER_LEVEL_HIGH;
    }
    return EFI_SUCCESS;

}

STATIC EFI_STATUS EFIAPI AppleAicV2SetIrqTriggerType(
    IN EFI_HARDWARE_INTERRUPT2_PROTOCOL *This,
    IN HARDWARE_INTERRUPT_SOURCE Source,
    IN EFI_HARDWARE_INTERRUPT2_TRIGGER_TYPE TriggerType
)
{
    //unsupported for now, just return EFI_SUCCESS
    return EFI_SUCCESS;
}

// AIC HardwareInterrupt2 protocol instance
EFI_HARDWARE_INTERRUPT2_PROTOCOL gHardwareInterrupt2AicV2Protocol = {
  (HARDWARE_INTERRUPT2_REGISTER)RegisterInterruptSource,
  (HARDWARE_INTERRUPT2_ENABLE)AppleAicV2UnmaskInterrupt,
  (HARDWARE_INTERRUPT2_DISABLE)AppleAicV2MaskInterrupt,
  (HARDWARE_INTERRUPT2_INTERRUPT_STATE)AppleAicV2GetInterruptState,
  (HARDWARE_INTERRUPT2_END_OF_INTERRUPT)AppleAicV2EndOfInterrupt,
  AppleAicV2GetIrqTriggerType,
  AppleAicV2SetIrqTriggerType
};



/**
 * The ExitBootServices event. Will disable interrupts and shut down AIC hardware in handoff from DXE core to OS.
 * 
 * @param Event (input) - event being processed 
 * @param Context (input) - event context.
 * @return VOID 
 */
VOID EFIAPI AppleAicV2ExitBootServicesEvent(
    IN EFI_EVENT Event,
    IN VOID *Context
)
{
    UINTN InterruptIndex;
    UINT32 AicConfigValue = (UINT32)(AIC_V2_CFG_ENABLE);
    AicConfigValue = ~AicConfigValue;

    //acknowledge any outstanding interrupts by reading the event register.
    //(this will mask those interrupts at the same time)
    AppleAicAcknowledgeInterrupt(mAicV2EventReg);

    //mask all other interrupts by writing to MASK_SET
    for(InterruptIndex = 0; InterruptIndex < AicInfoStruct->NumIrqs; InterruptIndex++)
    {
        AppleAicV2MaskInterrupt(&gHardwareInterruptAicV2Protocol, InterruptIndex);
    }

    //disable the AIC controller (AICv2/v3 CONFIG.ENABLE only; AICv1 has no such bit)
    if (mAicVersion == APPLE_AIC_VERSION_2 || mAicVersion == APPLE_AIC_VERSION_3) {
        MmioAnd32(AicV2Base + AIC_V2_CONFIG, AicConfigValue);
    }

}

/**
 * Prepares the AIC protocol for use by the DXE environment.
 * 
 * @param ImageHandle
 * @param SystemTable 
 * @return EFI_STATUS 
 */

EFI_STATUS AppleAicV2DxeInit(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable, APPLE_AIC_VERSION aicVersion)
{

    AURORA_AIC_STAGE (1, ImageHandle, aicVersion, 0);
    mAicVersion = aicVersion;

    UINTN InterruptIndex;
    EFI_STATUS Status;
    UINT32 AicV2NumInterrupts;
    UINT32 AicV2MaxInterrupts;

    //DEBUG((DEBUG_INFO, "%a: AIC driver start\n", __FUNCTION__));
    AicInfoStruct = AllocatePool(sizeof(AIC_INFO_STRUCT));
    AURORA_AIC_STAGE (2, AicInfoStruct, sizeof(AIC_INFO_STRUCT), 0);
    //multi-core support not added yet
    //UINT32 CoreID;
    //Assert that the Hardware Interrupt protocol is not installed already.
    ASSERT_PROTOCOL_ALREADY_INSTALLED (NULL, &gHardwareInterruptProtocolGuid);
    AURORA_AIC_STAGE (3, 0, 0, 0);

    //set up and collect variables in global variables that will get passed to functions that need them.
    AURORA_AIC_STAGE (4, 0, 0, 0);
    Status = AppleAicV2CalculateRegisterOffsets();
    AURORA_AIC_STAGE (5, AicV2Base, AicInfoStruct->MaxIrqs, Status);
    AicV2NumInterrupts = AicInfoStruct->NumIrqs;
    AicV2MaxInterrupts = AicInfoStruct->MaxIrqs;
    
    DEBUG((DEBUG_INFO, "AICv2 with %u/%u configured IRQs, at 0x%llx\n", AicV2NumInterrupts, AicV2MaxInterrupts, AicV2Base));
    DEBUG((DEBUG_INFO, "%u/%u CPU dies, Die Stride: 0x%lx\n", AicInfoStruct->NumCpuDies, AicInfoStruct->MaxCpuDies, AicInfoStruct->DieStride));
    DEBUG((DEBUG_VERBOSE, "AIC register addresses: \n"
    "\tAIC_V2_BASE: 0x%llx\n"
    "\tAIC_V2_IRQ_CFG: 0x%llx\n"
    "\tAIC_V2_SW_SET: 0x%llx\n"
    "\tAIC_V2_SW_CLR: 0x%llx\n"
    "\tAIC_V2_MASK_SET: 0x%llx\n"
    "\tAIC_V2_MASK_CLR: 0x%llx\n"
    "\tAIC_V2_HW_STATE: 0x%llx\n"
    "\tAIC_V2_EVENT: 0x%llx\n\n",
    AicV2Base,
    (AicV2Base + mAicV2IrqCfgOffset),
    (AicV2Base + mAicV2SoftwareSetRegOffset),
    (AicV2Base + mAicV2SoftwareClearRegOffset),
    (AicV2Base + mAicV2IrqMaskSetOffset),
    (AicV2Base + mAicV2IrqMaskClearOffset),
    (AicV2Base + mAicV2HwStateOffset),
    (mAicV2EventReg)
    ));

    //enable the AIC. AICv2/v3 have a global CONFIG.ENABLE bit at offset 0x14;
    //AICv1 has no such register (it is always enabled) and 0x14 is not its
    //config-enable, so the write is skipped there.
    if (mAicVersion == APPLE_AIC_VERSION_2 || mAicVersion == APPLE_AIC_VERSION_3) {
        DEBUG((DEBUG_VERBOSE, "%a: enabling AIC\n", __FUNCTION__));
        MmioOr32(AicV2Base + AIC_V2_CONFIG, AIC_V2_CFG_ENABLE);
    }

    //start from a clean state by disabling all interrupts
    for(InterruptIndex = 0; InterruptIndex < AicV2NumInterrupts; InterruptIndex++)
    {
        AURORA_AIC_STAGE (8, InterruptIndex, AicV2NumInterrupts, 0);
        AppleAicV2MaskInterrupt(&gHardwareInterruptAicV2Protocol, InterruptIndex);
    }
    AURORA_AIC_STAGE (9, AicV2NumInterrupts, AicV2MaxInterrupts, 0);

    //AICv1 has a real per-IRQ TARGET_CPU register (one u32 per IRQ, value is a
    //one-hot CPU bitmask). Default every line to CPU0 during firmware, matching
    //Asahi/m1n1 init. AICv2/v3 have no OS-programmable per-IRQ target and are
    //left to firmware defaults; the native-AIC HAL owns steering there.
    if (mAicVersion == APPLE_AIC_VERSION_1) {
        for(InterruptIndex = 0; InterruptIndex < AicV2NumInterrupts; InterruptIndex++)
        {
            MmioWrite32(AicV2Base + AIC_TARGET_CPU + (InterruptIndex * sizeof(UINT32)), BIT0);
        }
    }

    /**
     * 
     * On Apple CPUs, IRQs can be opted out of by writing 
     * to a implementation defined MSR per-core. (S3_4_C15_C10_4)
     * 
     * Typically used when a core is in a state where it's undesirable for it to service IRQs.
     * (sleep for example)
     * 
     * For now we're only using 1 core (the others are in a spin loop, or 100% inactive) but when multi-core support is turned on,
     * if it turns out the hardware heuristic is sending IRQs to inactive cores, uncomment the below code to disable IRQs on
     * those other cores.
     * 
     */

    //CoreID = (ArmReadMpidr()) & (ARM_CORE_AFF0 | ARM_CORE_AFF1 | ARM_CORE_AFF2 | ARM_CORE_AFF3);
    // any result other than 0 implies that this is not core 0
    //if (CoreID != 0) {
    //    AppleArmDisableIrqsAndFiqs();
    //}

    //register the interrupt controller now that setup is done.

    AURORA_AIC_STAGE (10, 0, 0, 0);
    Status = InstallAndRegisterInterruptService(
        &gHardwareInterruptAicV2Protocol,
        &gHardwareInterrupt2AicV2Protocol,
        AppleAicV2InterruptHandler,
        AppleAicV2ExitBootServicesEvent
    );
    AURORA_AIC_STAGE (11, 0, 0, Status);
    return Status;
}
