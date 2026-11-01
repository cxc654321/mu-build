/** @file
  J414s media device tables: MCA0 audio, AOPA microphones, ISP0 camera.

  WHY THIS IS A SEPARATE FILE, AND WHY IT NAMES A MACHINE

  These are twenty-one MMIO windows read off one live MacBook Pro. They used to
  sit in AcpiPlatformDxe/AcpiPlatform.c, which is in AppleSiliconPkg -- the
  package every machine compiles. The three flags that select them
  (NTASI_ENABLE_MCA/AOP/ISP_PUBLICATION) are build build flags, not machine
  flags, so a different Mac enabling media publication would have published
  J414s addresses for its own audio hardware.

  The static assertion below makes that a build error instead.

  Adding another machine's media devices means another header like this one and
  another arm in the #if. See Docs/PLATFORMS.md for which of these numbers
  should come from the ADT instead (the windows) and which should not (the
  GSIVs, the _HIDs and the _DSD names, all of which are ours).

  Copyright (c) Aurora Silicon.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef NTASI_MEDIA_J414S_H_
#define NTASI_MEDIA_J414S_H_

#include <Platform/NtasiJ414sWindows.h>

#if (NTASI_ENABLE_MCA_PUBLICATION || NTASI_ENABLE_AOP_PUBLICATION || \
     NTASI_ENABLE_ISP_PUBLICATION)
//
// Every address below was measured on J414s. Publishing them anywhere else
// would hand Windows an audio DMA engine at the wrong physical address.
//
STATIC_ASSERT (
  FixedPcdGet32 (PcdAppleSocIdentifier) == 0x6020,
  "media publication carries J414s addresses; see Include/Platform/NtasiMediaJ414s.h"
  );
#endif

#if NTASI_ENABLE_MCA_PUBLICATION
//
// MCA0 -- NTAS0080, speakers and headset jack.  Order per MCA.asl.
//
STATIC CONST NTASI_MEDIA_WINDOW  mNtasiMcaWindows[] = {
  //
  // _CRS order. The seven NTASI_J414S_W_* entries are generated from Asahi's
  // device tree by Tools/dtwindows.py -- they were hand-transcribed before, and
  // all seven reproduce byte for byte. The two literals are sub-page slices
  // that no device-tree node describes, so they stay authored here.
  //
  NTASI_J414S_W_MCA_CLUSTER,        // 0
  NTASI_J414S_W_MCA_SWITCH,         // 1
  NTASI_J414S_W_ADMAC,              // 2
  NTASI_J414S_W_NCO,                // 3
  { 0x290280000ULL, 0x1000ULL },    // 4: pmgr_east PS page (overlaps KBL0)
  NTASI_J414S_W_I2C1,               // 5
  NTASI_J414S_W_I2C3,               // 6
  NTASI_J414S_W_PINCTRL_AP,         // 7
  { 0x28E03807CULL, 0x18ULL },      // 8: mca-switch clock mux, sub-page by design
};


//
// MCA0's PUBLISHED GSIVs -- not its physical AIC lines.  Every line in this
// subsystem (1211-1231) is above the GIC carrier's 1019 limit and is illegal
// as a GSIV, so the CSRT's ALI2 tail translates them:
//
//   40 -> 1218 admac-sio   41 -> 1211 mca0   42 -> 1213 mca2
//   43 -> 1221 i2c2        45 -> 1231 dart-sio
//
// This is exactly why a build with NTASI_ENABLE_MCA_PUBLICATION must carry the
// "m2-pro-media" CSRT (8 aliases, 296 bytes) rather than the ordinary "m2-pro"
// (3 aliases, 256 bytes); CSRT.aslc selects it on that same flag, and on that
// flag ALONE -- the AOP and ISP flags add no alias and are not consulted there.
//
// NOT 44: AIC 44 belongs to /arm-io/i2c0/hpmBusManager in the live ADT.
//
STATIC CONST UINT32  mNtasiMcaInterrupts[] = { 40, 41, 42, 43, 45 };

STATIC CONST NTASI_MEDIA_PROPERTY  mNtasiMcaProperties[] = {
  { "ntasp,mca-cluster-count",              4          },
  //
  // KNOWN WRONG, DELIBERATELY LEFT ALONE -- DO NOT "CORRECT" THIS BACK.
  //
  // mca-speaker-cluster-right = 1 is inherited from Asahi's device tree, whose
  // Speakers dai-link is cpu = <&mca 0>, <&mca 1>.  THE LIVE J414s ADT SAYS
  // OTHERWISE, and it was re-read first-hand on 2026-08-05 (ADT 0def1b70,
  // 188 checks, tools/verify-j414s-av-adt.py in the AuroraSilicon tree):
  //
  //   /arm-io/mca0/mca0a/audio-speaker    audio-data,sn012776
  //                                       tx=6 (mask 0x3f), rx=12 (mask 0xfff)
  //   /arm-io/mca1/mca1a/audio-loopback   audio-data,audio-loopback
  //                                       tx=2 (mask 0x3),  rx=2  (mask 0x3)
  //   /arm-io/mca2/mca2a/audio-codec-output   audio-data,cs42l84
  //
  // ALL SIX AMPLIFIERS AND ALL TWELVE I/V SENSE CHANNELS ARE ON CLUSTER 0.
  // Cluster 1 is a two-channel internal loopback with no speaker on it.  Two
  // further facts agree: mca0a carries `internal-bclk-loopback` (meaningful
  // only when its RX and TX are the same physical bus) and mca1a does not, and
  // twelve sense channels is exactly six amplifiers' worth of I and V -- which
  // cluster 0 could not see if the amps were split across two clusters.
  //
  // It is left wrong ON PURPOSE.  No Windows driver reads this property today,
  // so the error is latent, and correcting it is a change to the SPEAKER
  // topology -- the one path on this machine that can physically destroy
  // hardware.  It gets fixed as part of the render work, where it can be
  // tested against a running I/V sense capture, not as a drive-by edit to a
  // constant nobody consumes.  Until then MCA publication stays off.
  //
  { "ntasp,mca-speaker-cluster-left",       0          },
  { "ntasp,mca-speaker-cluster-right",      1          },
  { "ntasp,mca-jack-cluster",               2          },
  { "ntasp,nco-ref-hz",                     1068000000 },
  { "ntasp,mca-slot-width",                 32         },
  { "ntasp,mca-bclk-ratio-speakers",        256        },
  { "ntasp,mca-bclk-ratio-jack",            64         },
  { "ntasp,admac-irq-output-index",         1          },
  { "ntasp,speaker-amp-count",              6          },
  { "ntasp,speaker-sdz-gpio",               57         },
  { "ntasp,speaker-irq-gpio",               58         },
  { "ntasp,jack-irq-gpio",                  59         },
  { "ntasp,speaker-safe-dvc-floor",         40         },
  { "ntasp,speaker-resting-dvc",            200        },
  { "ntasp,speaker-amp-gain-ceiling",       15         },
  { "ntasp,preboot-handoff-required",       0          },
  { "ntasp,clk-mux-window-published",       1          },
  { "ntasp,clk-mux-register-count",         6          },
  { "ntasp,mca-interrupts-published",       5          },
  { "ntasp,mca-csrt-ali2-required",         1          },
  { "ntasp,mca-capture-windows-published",  0          },
  { "ntasp,mca-clusters-instantiated",      3          },
  { "ntasp,adt-speaker-cluster",            0          },
  { "ntasp,adt-loopback-cluster",           1          },
  { "ntasp,admac-channel-jack-capture",     11         },
  { "ntasp,admac-channel-speaker-play",     0          },
};

#endif // NTASI_ENABLE_MCA_PUBLICATION

#if NTASI_ENABLE_AOP_PUBLICATION
//
// AOPA -- NTAS0081, the Always-On Processor.  Order per AOPA.asl.
//
// ONE DEVICE, NOT TWO.  The AOP coprocessor driver enumerates its services --
// the internal PDM microphone array and the lid-angle sensor -- as PnP children
// of this devnode.  There is deliberately no second _HID for the lid angle:
// firmware declares the coprocessor, the driver declares what it hosts.
//
STATIC CONST NTASI_MEDIA_WINDOW  mNtasiAopWindows[] = {
  { 0x2A6400000ULL, 0x6C000ULL  },  // 0: aop ASC control (mailbox at +0x8000)
  { 0x2A6C00000ULL, 0x250000ULL },  // 1: aop SRAM / mmio window
  { 0x2A6808000ULL, 0x4000ULL   },  // 2: aop_dart (T8110)
  { 0x2A6980000ULL, 0x34000ULL  },  // 3: aop_admac
};

//
// AIC 631 (admac-aop-audio).  Below 1019, so identity-mapped with no ALI2
// entry.  613/614/615/616 (mailbox) and 628 (dart-aop) are equally legal and
// deliberately not published: each is another descriptor the arbiter must
// satisfy for a devnode that reads none of them.
//
STATIC CONST UINT32  mNtasiAopInterrupts[] = { 631 };

STATIC CONST NTASI_MEDIA_PROPERTY  mNtasiAopProperties[] = {
  { "ntasp,aop-mic-rate-hz",              48000     },
  { "ntasp,aop-mic-channels",             3         },
  { "ntasp,aop-mic-sample-bits",          32        },
  { "ntasp,aop-mic-period-bytes-min",     256       },
  { "ntasp,aop-mic-period-bytes-max",     16384     },
  { "ntasp,aop-admac-channel",            1         },
  { "ntasp,aop-admac-irq-output-index",   2         },
  { "ntasp,aop-dart-stream-aop",          0         },
  { "ntasp,aop-dart-stream-admac",        10        },
  { "ntasp,aop-dart-page-size",           16384     },
  { "ntasp,aop-aic-mailbox-0",            613       },
  { "ntasp,aop-aic-mailbox-1",            614       },
  { "ntasp,aop-aic-mailbox-2",            615       },
  { "ntasp,aop-aic-mailbox-3",            616       },
  { "ntasp,aop-aic-dart",                 628       },
  { "ntasp,aop-aic-admac",                631       },
  { "ntasp,aop-interrupts-published",     1         },
  { "ntasp,aop-csrt-ali2-required",       0         },
  { "ntasp,aop-mailbox-offset",           0x8000    },
  { "ntasp,aop-cpu-control-offset",       0x44      },
  { "ntasp,aop-cpu-run-bit",              0x10      },
  { "ntasp,aop-bootargs-ptr-offset",      0x22C     },
  { "ntasp,aop-bootargs-size-offset",     0x230     },
  { "ntasp,aop-firmware-preloaded",       1         },
  { "ntasp,aop-pdm-frequency-hz",         2400000   },
  { "ntasp,aop-pdmc-frequency-hz",        24000000  },
  { "ntasp,aop-pdm-bytes-per-sample",     2         },
  { "ntasp,aop-pdm-filter-lengths",       0x00542C47},
  { "ntasp,aop-pdm-ratio1",               15        },
  { "ntasp,aop-pdm-ratio2",               5         },
  { "ntasp,aop-pdm-ratio3",               2         },
  { "ntasp,aop-decimator-latency",        15        },
  { "ntasp,aop-mic-turn-on-time-ms",      20        },
  { "ntasp,aop-mic-settle-time-ms",       50        },
  { "ntasp,aop-pdm-coefficient-taps",     100       },
  { "ntasp,aop-pdm-coefficient-slots",    120       },
};

#endif // NTASI_ENABLE_AOP_PUBLICATION

#if NTASI_ENABLE_ISP_PUBLICATION
//
// ISP0 -- NTAS0090, FaceTime camera.  Order per ISP.asl.
//
STATIC CONST NTASI_MEDIA_WINDOW  mNtasiIspWindows[] = {
  { 0x384000000ULL, 0x2000000ULL },  // 0: ISP coprocessor
  { 0x386104000ULL, 0x100ULL     },  // 1: ISP mailbox
  { 0x386104170ULL, 0x100ULL     },  // 2: ISP scratch words ("gpio", not GPIO)
  { 0x3861043F0ULL, 0x100ULL     },  // 3: ISP mailbox 2
  { 0x290280000ULL, 0x4034ULL    },  // 4: pmgr_east, length verbatim, overlaps KBL0
  { 0x3860E8000ULL, 0x4000ULL    },  // 5: dart-isp0 DARTLLT
  { 0x3860F4000ULL, 0x4000ULL    },  // 6: dart-isp0 DARTBULK
  { 0x3860FC000ULL, 0x4000ULL    },  // 7: dart-isp0 DARTRT
};

//
// AIC 569.  Below 1019, identity-mapped, no ALI2 entry.  The ADT also lists
// 570/571/572; Linux wires only 569.  Do not add them without changing the
// driver: AppleIsp ASSIGNS Device->Gsiv per descriptor rather than
// accumulating, so it keeps the last one it sees.
//
STATIC CONST UINT32  mNtasiIspInterrupts[] = { 569 };

STATIC CONST NTASI_MEDIA_PROPERTY  mNtasiIspProperties[] = {
  { "ntasp,isp-camera-config-index",        0             },
  { "ntasp,isp-camera-config-index-pinned", 1             },
  { "ntasp,isp-platform-id",                7             },
  { "ntasp,isp-sensor-native-dim",          1920          },
  { "ntasp,isp-mode-count",                 10            },
  { "ntasp,isp-published-mode-count",       5             },
  { "ntasp,isp-stride-alignment",           64            },
  { "ntasp,isp-frame-rate-max",             30            },
  { "ntasp,isp-frame-rate-min",             15            },
  { "ntasp,isp-firmware-preloaded",         1             },
  { "ntasp,isp-firmware-carveout-base",     0x100009FC000 },
  { "ntasp,isp-firmware-carveout-size",     0x1284000     },
  { "ntasp,isp-firmware-text-iova",         0x0           },
  { "ntasp,isp-firmware-data-iova",         0x934000      },
  { "ntasp,isp-firmware-iova-span",         0xC48000      },
  { "ntasp,isp-dart-node-count",            1             },
  { "ntasp,isp-dart-window-count",          6             },
  { "ntasp,isp-dart-translation-count",     3             },
  { "ntasp,isp-dart-windows-published",     3             },
  { "ntasp,isp-dart-sid",                   0             },
  { "ntasp,isp-dart-page-shift",            14            },
  { "ntasp,isp-dart-pa-width",              42            },
  { "ntasp,isp-dart-vm-size",               0xA0000000    },
  { "ntasp,isp-dart-adopt-inherited-table", 1             },
  { "ntasp,isp-gsiv",                       569           },
  { "ntasp,isp-gsiv-needs-ali2",            0             },
  { "ntasp,isp-interrupts-published",       1             },
  { "ntasp,isp-adt-irq-count",              4             },
  { "ntasp,isp-bringup-is-polled",          1             },
  { "ntasp,isp-delivers-frames",            0             },
  { "ntasp,isp-power-domain-count",         7             },
};

#endif // NTASI_ENABLE_ISP_PUBLICATION

//
// One SSDT per SELECTED device, matching the three DefinitionBlocks in the ASL
// specs.  OEM ID and OEM table ID are byte-identical to what iasl emits for
// those files (both fields are NUL-padded by iasl, and CopyMem() copies the
// same 6 and 8 bytes from these literals).
//
// Each entry is guarded by its own flag, and the tables it names are guarded by
// the same one, so a build selecting a subset has neither an entry pointing at
// a table that was not compiled nor a table nothing references.  This array can
// never be zero-length: it is inside the outer OR-guard, so at least one flag
// is on wherever this text is compiled at all.
//

#endif // NTASI_MEDIA_J414S_H_
