/**
 * @file AppleAicCommonDxe.c
 * @author amarioguy (Arminder Singh)
 * 
 * AIC DXE Driver code common to both AICv1 and AICv2
 * @version 1.0
 * @date 2022-10-06
 * 
 * @copyright Copyright (c) amarioguy (Arminder Singh), 2022.
 * 
 * Copyright (c) 2026 Aurora Silicon
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 * 
 */

#include "AppleAicDxe.h"

EFI_HANDLE  gHardwareInterruptHandle = NULL;

EFI_EVENT  EfiExitBootServicesEvent = (EFI_EVENT)NULL;

HARDWARE_INTERRUPT_HANDLER  *AicRegisteredInterruptHandlers = NULL;

STATIC VOID  *mCpuArchProtocolNotifyEventRegistration;

typedef struct {
  CHAR8           Magic[8];
  volatile UINT64 Stage;
  volatile UINT64 Value0;
  volatile UINT64 Value1;
  volatile UINT64 Status;
} AURORA_AIC_COMMON_TRACE;

STATIC volatile AURORA_AIC_COMMON_TRACE mAuroraAicCommonTrace = {
  { 'A', 'U', 'R', 'A', 'I', 'C', 'C', '1' },
  0,
  0,
  0,
  0
};

#define AURORA_AIC_COMMON_STAGE(StageValue, FirstValue, SecondValue, StatusValue) \
  do {                                                                            \
    mAuroraAicCommonTrace.Value0 = (UINT64)(FirstValue);                           \
    mAuroraAicCommonTrace.Value1 = (UINT64)(SecondValue);                          \
    mAuroraAicCommonTrace.Status = (UINT64)(StatusValue);                          \
    mAuroraAicCommonTrace.Stage  = (UINT64)(StageValue);                           \
  } while (0)

VOID
EFIAPI
ExitBootServicesEvent (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  );

//borrowed from ArmGicDxe as it does the job well enough
/**
  Register Handler for the specified interrupt source.

  @param This     Instance pointer for this protocol
  @param Source   Hardware source of the interrupt
  @param Handler  Callback for interrupt. NULL to unregister

  @retval EFI_SUCCESS Source was updated to support Handler.
  @retval EFI_DEVICE_ERROR  Hardware could not be programmed.

**/
EFI_STATUS
EFIAPI
RegisterInterruptSource (
  IN EFI_HARDWARE_INTERRUPT_PROTOCOL  *This,
  IN HARDWARE_INTERRUPT_SOURCE        Source,
  IN HARDWARE_INTERRUPT_HANDLER       Handler
  )
{
  EFI_STATUS Status;

  if (Source >= AicInfoStruct->MaxIrqs) {
    ASSERT (FALSE);
    return EFI_UNSUPPORTED;
  }

  if ((Handler == NULL) && (AicRegisteredInterruptHandlers[Source] == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if ((Handler != NULL) && (AicRegisteredInterruptHandlers[Source] != NULL)) {
    return EFI_ALREADY_STARTED;
  }

  AicRegisteredInterruptHandlers[Source] = Handler;

  // If the interrupt handler is unregistered then disable the interrupt
  if (NULL == Handler) {
    return This->DisableInterruptSource (This, Source);
  } else {
    Status = This->EnableInterruptSource (This, Source);
    if (!EFI_ERROR (Status)) {
      // AIC reset/init keeps m1n1's private timer-reflection slots masked.
      // Source 17/18 registration is the first point at which their callback
      // exists, so clear stale software state and expose the matching block.
      AppleAicV2PrepareTimerInterrupt (Source);
    }
    return Status;
  }
}


//inspired/borrowed from ArmGicCommonDxe
/**
 * Notifies the Interrupt Service registration task when the CPU protocol becomes available.
 * 
 * @param Event 
 * @param Context 
 */
STATIC VOID EFIAPI CpuArchProtocolNotify(IN EFI_EVENT Event, IN VOID *Context)
{
  EFI_CPU_ARCH_PROTOCOL  *CpuProtocol;
  EFI_STATUS             Status;

  AURORA_AIC_COMMON_STAGE (0x30, Event, Context, 0);

  // Get the required CPU protocol
  Status = gBS->LocateProtocol (&gEfiCpuArchProtocolGuid, NULL, (VOID **)&CpuProtocol);
  AURORA_AIC_COMMON_STAGE (0x31, CpuProtocol, 0, Status);
  if (EFI_ERROR (Status)) {
    return;
  }

  //unregister the default FIQ and IRQ handler for the CPU protocol
  AURORA_AIC_COMMON_STAGE (0x32, CpuProtocol, ARM_ARCH_EXCEPTION_IRQ, 0);
  Status = CpuProtocol->RegisterInterruptHandler (CpuProtocol, ARM_ARCH_EXCEPTION_IRQ, NULL);
  AURORA_AIC_COMMON_STAGE (0x33, CpuProtocol, ARM_ARCH_EXCEPTION_IRQ, Status);
  AURORA_AIC_COMMON_STAGE (0x34, CpuProtocol, EXCEPT_AARCH64_FIQ, 0);
  Status = CpuProtocol->RegisterInterruptHandler (CpuProtocol, EXCEPT_AARCH64_FIQ, NULL);
  AURORA_AIC_COMMON_STAGE (0x35, CpuProtocol, EXCEPT_AARCH64_FIQ, Status);
  if (EFI_ERROR (Status)) {
    DEBUG((DEBUG_ERROR, "%a: Unregistering default exception handlers failed!! Status: 0x%llx\n", __FUNCTION__, Status));
    return;
  }

  //now register the new handlers
  AURORA_AIC_COMMON_STAGE (0x36, CpuProtocol, ARM_ARCH_EXCEPTION_IRQ, 0);
  Status = CpuProtocol->RegisterInterruptHandler (CpuProtocol, ARM_ARCH_EXCEPTION_IRQ, (EFI_CPU_INTERRUPT_HANDLER)Context);
  AURORA_AIC_COMMON_STAGE (0x37, CpuProtocol, ARM_ARCH_EXCEPTION_IRQ, Status);
  AURORA_AIC_COMMON_STAGE (0x38, CpuProtocol, EXCEPT_AARCH64_FIQ, 0);
  Status = CpuProtocol->RegisterInterruptHandler (CpuProtocol, EXCEPT_AARCH64_FIQ, (EFI_CPU_INTERRUPT_HANDLER)Context);
  AURORA_AIC_COMMON_STAGE (0x39, CpuProtocol, EXCEPT_AARCH64_FIQ, Status);
  if (EFI_ERROR (Status)) {
    DEBUG((DEBUG_ERROR, "%a: Registering new exception handler failed!! Status: 0x%llx\n", __FUNCTION__, Status));
    return;
  }
  AURORA_AIC_COMMON_STAGE (0x3A, Event, 0, 0);
  gBS->CloseEvent (Event);
  AURORA_AIC_COMMON_STAGE (0x3B, Event, 0, 0);

}


EFI_STATUS
InstallAndRegisterInterruptService (
  IN EFI_HARDWARE_INTERRUPT_PROTOCOL   *InterruptProtocol,
  IN EFI_HARDWARE_INTERRUPT2_PROTOCOL  *Interrupt2Protocol,
  IN EFI_CPU_INTERRUPT_HANDLER         InterruptHandler,
  IN EFI_EVENT_NOTIFY                  ExitBootServicesEvent
  )
{
    EFI_STATUS Status;
    // Interrupt events carry an index in the controller's full per-die IRQ
    // namespace. Software IRQs (including m1n1's reflected timers) can live
    // above NumIrqs, so sizing this table to NumIrqs permits an out-of-bounds
    // access even though RegisterInterruptSource accepts values up to MaxIrqs.
    CONST UINTN InterruptHandlersSize = (sizeof(HARDWARE_INTERRUPT_HANDLER) * AicInfoStruct->MaxIrqs);
    AURORA_AIC_COMMON_STAGE (0x20, InterruptHandlersSize, AicInfoStruct->MaxIrqs, 0);

    //set up RAM for IRQ handlers
    AicRegisteredInterruptHandlers = AllocateZeroPool(InterruptHandlersSize);
    AURORA_AIC_COMMON_STAGE (0x21, AicRegisteredInterruptHandlers, InterruptHandlersSize, 0);
    if(AicRegisteredInterruptHandlers == NULL)
    {
        return EFI_OUT_OF_RESOURCES;
    }

  // Register the CPU-side IRQ/FIQ handlers before publishing the hardware
  // interrupt protocols. CpuDxe already has a notify event on the first of
  // those protocols and may enable CPU interrupts synchronously from inside
  // InstallMultipleProtocolInterfaces(). Publishing first therefore creates a
  // window where live AIC events reach the default exception handler. The ARM
  // GIC driver establishes its CPU handler before protocol publication; keep
  // the Apple AIC path in the same safe order.
  AURORA_AIC_COMMON_STAGE (0x24, InterruptHandler, 0, 0);
  EfiCreateProtocolNotifyEvent(
    &gEfiCpuArchProtocolGuid,
    TPL_CALLBACK,
    CpuArchProtocolNotify,
    (VOID *)InterruptHandler,
    &mCpuArchProtocolNotifyEventRegistration
    );
  AURORA_AIC_COMMON_STAGE (0x25, mCpuArchProtocolNotifyEventRegistration, 0, 0);

    AURORA_AIC_COMMON_STAGE (0x22, InterruptProtocol, Interrupt2Protocol, 0);
    Status = gBS->InstallMultipleProtocolInterfaces (
                  &gHardwareInterruptHandle,
                  &gHardwareInterruptProtocolGuid,
                  InterruptProtocol,
                  &gHardwareInterrupt2ProtocolGuid,
                  Interrupt2Protocol,
                  NULL
                  );
  AURORA_AIC_COMMON_STAGE (0x23, gHardwareInterruptHandle, 0, Status);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  
  //set up the ExitBootServices event
  AURORA_AIC_COMMON_STAGE (0x26, ExitBootServicesEvent, 0, 0);
  Status = gBS->CreateEvent (
                  EVT_SIGNAL_EXIT_BOOT_SERVICES,
                  TPL_NOTIFY,
                  ExitBootServicesEvent,
                  NULL,
                  &EfiExitBootServicesEvent
                  );
  AURORA_AIC_COMMON_STAGE (0x27, EfiExitBootServicesEvent, 0, Status);
  
  return Status;
    
}
