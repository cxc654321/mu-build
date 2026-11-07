/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 J313HidKeyboard contributors
 */
#ifndef J313_KEYBOARD_ADAPTER_H
#define J313_KEYBOARD_ADAPTER_H
#include <Uefi.h>
#include "Core/key_queue.h"

typedef struct J313_KEYBOARD J313_KEYBOARD;

/* Boot-services only, BSP serialized through TPL_NOTIFY (not an SMP lock).
 * Create/Publish/Unpublish/Destroy: TPL_APPLICATION, no transport callbacks live
 * during Destroy. Create installs nothing. Parent must call Publish ONLY after
 * real transport handshake and keyboard descriptor validation. No fake input.
 * Feed: <= TPL_NOTIFY, each successful hk_decode result exactly once.
 * Queue drops newest on overflow (EFI_OUT_OF_RESOURCES); state still advances.
 * Unpublish failure retains every protocol/event/context; caller must not free
 * or unload driver. Destroy failure likewise retains *Context for retry.
 * ResetQueue and protocol Reset(FALSE) clear software queues only; no bus reset
 * or hardware diagnostic is implied. Reset(TRUE) returns EFI_UNSUPPORTED.
 */
EFI_STATUS J313KeyboardCreate (OUT J313_KEYBOARD **Context);
EFI_STATUS J313KeyboardPublish (IN J313_KEYBOARD *Context);
EFI_HANDLE J313KeyboardHandle (IN J313_KEYBOARD *Context);
EFI_HANDLE J313KeyboardGetHandle (IN J313_KEYBOARD *Context);
EFI_STATUS J313KeyboardUnpublish (IN J313_KEYBOARD *Context);
EFI_STATUS J313KeyboardFeed (IN J313_KEYBOARD *Context, IN CONST hk_result *Result);
EFI_STATUS J313KeyboardResetQueue (IN J313_KEYBOARD *Context);
EFI_STATUS J313KeyboardDestroy (IN OUT J313_KEYBOARD **Context);
#endif
