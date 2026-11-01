# @file
# Shared stuart settings for every Apple silicon platform package.
#
# A platform's PlatformBuild.py differs from its neighbours in one thing: the
# platform name. Everything else -- package list, architectures, submodules,
# scopes, build environment -- is identical. Carrying five copies of it meant
# five chances to drift, and they had:
#
#   - MacBookAirMid2020 and MacBookProLate2020 wrote ArchSupported = ("AARCH64"),
#     a string rather than a tuple, so SetArchitectures(["AARCH64"]) computed
#     {"AARCH64"} - {"A","R","C","H","6","4"} and raised "Unsupported
#     Architecture Requested" on the only architecture they support.
#   - MacBookProLate2020's GetName() returned "MacBookProLate2020" where every
#     other platform returned "<name>Pkg", which is what names the build log.
#   - MacMini2020 and MacStudio2022 set EMPTY_DRIVE, RUN_TESTS and
#     SHUTDOWN_AFTER_RUN; the others left them commented out. They also imported
#     ParseSettingsManager without using it, and skipped RetrieveCommandLineOptions'
#     TARGET_ARCH / ACTIVE_PLATFORM assignment.
#
# The mixins below carry no stuart base class, so stuart's
# locate_class_in_module() cannot pick them up by mistake: it only ever sees the
# real SettingsManager and PlatformBuilder each platform defines.
#
# Copyright (c) Microsoft Corporation.
# Copyright (c) 2026 Aurora Silicon
# SPDX-License-Identifier: BSD-2-Clause-Patent
##
import logging
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from edk2toolext.environment import shell_environment

import Features
from edk2toolext.invocables.edk2_setup import RequiredSubmodule

WORKSPACE_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

ARCH_SUPPORTED = ("AARCH64",)
TARGETS_SUPPORTED = ("DEBUG", "RELEASE", "NOOPT")

#
# Order matters: the first entry containing a package name wins.
#
# Silicon/ARM/TIANO comes before MU_BASECORE because ArmPkg, ArmPlatformPkg and
# DynamicTablesPkg now exist in both. They did not when this list was written --
# the MU_BASECORE that main pins has no ArmPkg at all, so those three always
# resolved to TIANO. The M5-Dev bump added them, which silently moved ArmPkg to
# a copy that does not declare gArmMmuReplaceLiveTranslationEntryFuncGuid, and
# AppleSiliconPkg.dsc.inc points ArmMmuLib at
# ArmPkg/Library/ArmMmuLib/ArmMmuPeiLib.inf, which needs it:
#
#   ArmMmuPeiLib.inf(51): error 4000: Value of Guid
#   [gArmMmuReplaceLiveTranslationEntryFuncGuid] is not found under [Guids]
#
# Tests/test_package_path.py pins the three.
#
PACKAGES_PATH = (
    "Platform",
    "Silicon/ARM/TIANO",
    "MU_BASECORE",
    "Common/MU",
    "Common/TIANO",
    "Common/MU_OEM_SAMPLE",
    "Silicon/Apple",
    "Common/MU_DFCI",
    "mu_feature_debugger",
)

SUBMODULES = (
    "MU_BASECORE",
    "Common/MU",
    "Common/TIANO",
    "Common/MU_OEM_SAMPLE",
    "Silicon/ARM/TIANO",
    "Common/MU_DFCI",
    "mu_feature_debugger",
)


class ApplePlatformSettings:
    """Mixin for a platform's SettingsManager. Set PLATFORM to the name."""

    PLATFORM = None

    @property
    def _dsc(self):
        return f"{self.PLATFORM}Pkg/{self.PLATFORM}.dsc"

    def GetPackagesSupported(self):
        return (f"{self.PLATFORM}Pkg",)

    def GetArchitecturesSupported(self):
        return ARCH_SUPPORTED

    def GetTargetsSupported(self):
        return TARGETS_SUPPORTED

    def GetRequiredSubmodules(self):
        return [RequiredSubmodule(path, True) for path in SUBMODULES]

    def SetArchitectures(self, list_of_requested_architectures):
        unsupported = set(list_of_requested_architectures) - set(self.GetArchitecturesSupported())
        if unsupported:
            message = "Unsupported Architecture Requested: " + " ".join(unsupported)
            logging.critical(message)
            raise Exception(message)
        self.ActualArchitectures = list_of_requested_architectures

    def GetWorkspaceRoot(self):
        return WORKSPACE_ROOT

    def GetActiveScopes(self):
        return (self.PLATFORM, "gcc_aarch64_linux", "edk2-build", "cibuild")

    def FilterPackagesToTest(self, changedFilesList: list, potentialPackagesList: list) -> list:
        """Build everything when a change could affect the build itself.

        Dependency tracking cannot see BaseTools or the pipeline template.
        """
        for f in changedFilesList:
            if "BaseTools" in f and os.path.splitext(f) not in [".txt", ".md"]:
                return potentialPackagesList.copy()
            if "platform-build-run-steps.yml" in f:
                return potentialPackagesList.copy()
        return []

    def GetPlatformDscAndConfig(self) -> tuple:
        return (self._dsc, {})

    def GetName(self):
        return self.PLATFORM

    def GetPackagesPath(self):
        return PACKAGES_PATH


class ApplePlatformBuilder:
    """Mixin for a platform's PlatformBuilder.

    Set PLATFORM to the package name. Set DEVICE as well if the machine has a
    feature set in Platform/Features.py; leaving it None means the platform
    builds one way and no feature defines are emitted.
    """

    PLATFORM = None
    DEVICE = None

    @property
    def _dsc(self):
        return f"{self.PLATFORM}Pkg/{self.PLATFORM}.dsc"

    def AddCommandLineOptions(self, parserObj):
        parserObj.add_argument(
            "-a", "--arch", dest="build_arch", type=str, default="AARCH64",
            help="Optional - CSV of architecture to build. AARCH64 is used for PEI and "
                 "DXE and is the only valid option for this platform.")

    def RetrieveCommandLineOptions(self, args):
        if args.build_arch.upper() != "AARCH64":
            raise Exception(
                "Invalid Arch Specified. See PlatformBuildCommon.ApplePlatformBuilder.")
        shell_environment.GetBuildVars().SetValue(
            "TARGET_ARCH", args.build_arch.upper(), "From CmdLine")
        shell_environment.GetBuildVars().SetValue("ACTIVE_PLATFORM", self._dsc, "From CmdLine")

    def GetWorkspaceRoot(self):
        return WORKSPACE_ROOT

    def GetPackagesPath(self):
        paths = [shell_environment.GetBuildVars().GetValue("FEATURE_CONFIG_PATH", "")]
        paths.extend(PACKAGES_PATH)
        return paths

    def GetActiveScopes(self):
        return (self.PLATFORM, "gcc_aarch64_linux", "edk2-build", "cibuild")

    def GetName(self):
        # Not "<PLATFORM>Pkg". stuart names the build log after this, and the
        # sealing step looks for BUILDLOG_<PLATFORM>.txt, so the two have to
        # agree. Every platform returned the bare name before this file existed.
        return self.PLATFORM

    def GetLoggingLevel(self, loggerType):
        return logging.DEBUG

    def SetPlatformEnv(self):
        logging.debug("PlatformBuilder SetPlatformEnv")
        self.env.SetValue("PRODUCT_NAME", self.PLATFORM, "Platform Hardcoded")
        self.env.SetValue("ACTIVE_PLATFORM", self._dsc, "Platform Hardcoded")
        self.env.SetValue("TARGET_ARCH", "AARCH64", "Platform Hardcoded")
        self.env.SetValue("TOOL_CHAIN_TAG", "CLANGPDB", "set default to clangpdb")
        self.env.SetValue("BUILDREPORTING", "TRUE", "Enabling build report")
        self.env.SetValue(
            "BUILDREPORT_TYPES",
            "PCD DEPEX FLASH BUILD_FLAGS LIBRARY FIXED_ADDRESS HASH",
            "Setting build report types")
        # The MFCI test cert is the default; pass BLD_*_SHIP_MODE=TRUE for retail.
        self.env.SetValue("BLD_*_SHIP_MODE", "FALSE", "Default")
        self._set_feature_defines()
        return 0

    def _set_feature_defines(self):
        """Turn the requested feature set into -D NTASI_* defines.

        NTASI_MU_DEVICE / NTASI_MU_WITH / NTASI_MU_WITHOUT are what Tools/mu-build
        exports, and re-resolving them here rather than passing the defines
        through means a manual stuart_build with the same three variables
        produces the same image.
        """
        if self.DEVICE is None:
            return
        device = os.environ.get("NTASI_MU_DEVICE", self.DEVICE)
        with_ = os.environ.get("NTASI_MU_WITH", "").split()
        without = os.environ.get("NTASI_MU_WITHOUT", "").split()
        logging.info("%s", Features.summary(device, with_, without))
        for define, value in Features.defines(device, with_, without):
            self.env.SetValue(define, value, "Selected by feature set")

    def PlatformPreBuild(self):
        return 0

    def PlatformPostBuild(self):
        return 0

    def FlashRomImage(self):
        return 0


def stuart_main(script_path):
    """The --setup / --update / build dispatch every PlatformBuild.py ends with."""
    import argparse
    import sys
    from edk2toolext.invocables.edk2_update import Edk2Update
    from edk2toolext.invocables.edk2_setup import Edk2PlatformSetup
    from edk2toolext.invocables.edk2_platform_build import Edk2PlatformBuild

    print("Invoking Stuart")
    print("     ) _     _")
    print("    ( (^)-~-(^)")
    print("__,-.\\_( 0 0 )__,-.___")
    print("  'W'   \\   /   'W'")
    print("         >o<")

    rel = os.path.relpath(script_path)
    parser = argparse.ArgumentParser(add_help=False)
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--update", "--UPDATE", action="store_true", help="Invokes stuart_update")
    group.add_argument("--setup", "--SETUP", action="store_true", help="Invokes stuart_setup")
    args, remaining = parser.parse_known_args()
    sys.argv = ["stuart", "-c", rel] + remaining
    if args.setup:
        print("Running stuart_setup -c " + rel)
        Edk2PlatformSetup().Invoke()
    elif args.update:
        print("Running stuart_update -c " + rel)
        Edk2Update().Invoke()
    else:
        print("Running stuart_build -c " + rel)
        Edk2PlatformBuild().Invoke()
