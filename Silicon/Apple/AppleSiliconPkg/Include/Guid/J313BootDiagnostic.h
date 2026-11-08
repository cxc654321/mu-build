/** Boot-services lifetime snapshot. No pointers; never persisted to NVRAM. */
#ifndef J313_BOOT_DIAGNOSTIC_H
#define J313_BOOT_DIAGNOSTIC_H
#include <Uefi.h>
#define J313_BOOT_DIAGNOSTIC_GUID {0x81ab946e,0x3c72,0x46fa,{0xa7,0x19,0xc5,0x80,0x33,0x13,0x62,0x91}}
#define J313_DIAG_VERSION 4
#define J313_DIAG_HID_VALID 32U
/* HID operations: tick=1 ready=2 read=3 packet=4 write=5 entry-wait=6.
 * Domains: Core js_domain 1..6, transport=7, outer deadline=8. */
#define J313_DIAG_PMGR_VALID 16U
#define J313_DIAG_RESOURCE_VALID 1U
#define J313_DIAG_IDENTITY_VALID 2U
#define J313_DIAG_BUS_VALID 4U
#define J313_DIAG_CLEANUP_VALID 8U
/* Stage values are ABI: append, do not reorder. */
typedef enum {
 J313StageEntry=1, J313StageAlreadyStarted, J313StageBoard, J313StageLoadedImage,
 J313StageContext, J313StageKeyboard, J313StageExitEvent, J313StageBootTimer,
 J313StageSpiInit, J313StageStartTimer, J313StageWait, J313StageTimeout,
 J313StageHandshake, J313StageCancelTimer, J313StageCloseTimer,
 J313StagePublish, J313StageConsolePath, J313StageConIn, J313StageConnect,
 J313StagePollEvent, J313StagePollTimer, J313StageReady, J313StagePump,
 J313StageUnload
} J313_DIAG_STAGE;
typedef struct {
 UINT32 Version, Size, Valid, Stage;
 UINT64 Status, CleanupStatus;
 UINT32 ResourceCode, IdentityMask, BusCode, LastFailureStage;
 UINT64 LastFailureStatus;
 /* PMGR values are observational; ffffffff denotes unavailable. */
 UINT32 PmgrReason, PmgrSource, PmgrGate, PmgrId, PmgrIdWidth;
 UINT32 PmgrDevicesLength, PmgrPsLength, PmgrRecordIndex, PmgrPsIndex;
 UINT32 PmgrRegIndex, PmgrGateLength;
 UINT32 HidOp, HidPhase, HidDomain;
 INT32 HidDetail;
 UINT32 HidBus, HidReject, HidRejectMask, HidFrozen;
 UINT32 HidPolls, HidReady, HidReads, HidWrites, HidDrops;
 UINT32 HidPioStatus, HidPioSegments, HidPioFlags, HidAck;
 UINT64 HidTxQueued, HidTxCompleted, HidRxCompleted;
 /* v4: last rejected reply and its request, not current pending request. */
 UINT32 HidRejectIdValid, HidExpectedId, HidActualId, HidRequestType, HidRequestIface;
} J313_BOOT_DIAGNOSTIC;
#endif
