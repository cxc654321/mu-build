# @file
# stuart settings for the MacMini2020 platform.
#
# Everything except the platform name lives in Platform/PlatformBuildCommon.py.
#
# Copyright (c) Microsoft Corporation.
# Copyright (c) 2026 Aurora Silicon
# SPDX-License-Identifier: BSD-2-Clause-Patent
##
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from PlatformBuildCommon import ApplePlatformBuilder, ApplePlatformSettings, stuart_main

from edk2toolext.environment.uefi_build import UefiBuilder
from edk2toolext.invocables.edk2_platform_build import BuildSettingsManager
from edk2toolext.invocables.edk2_pr_eval import PrEvalSettingsManager
from edk2toolext.invocables.edk2_setup import SetupSettingsManager
from edk2toolext.invocables.edk2_update import UpdateSettingsManager

PLATFORM = "MacMini2020"


class SettingsManager(ApplePlatformSettings, UpdateSettingsManager, SetupSettingsManager,
                      PrEvalSettingsManager):
    PLATFORM = PLATFORM


class PlatformBuilder(ApplePlatformBuilder, UefiBuilder, BuildSettingsManager):
    PLATFORM = PLATFORM

    def __init__(self):
        UefiBuilder.__init__(self)


if __name__ == "__main__":
    stuart_main(__file__)
