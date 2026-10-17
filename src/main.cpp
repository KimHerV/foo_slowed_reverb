// SPDX-License-Identifier: MIT
// Copyright (c) 2026 KimHerV

#include "stdafx.h"
#include "version.h"

DECLARE_COMPONENT_VERSION(
	"Slowed & Reverb",
	SR_VERSION_STR,
	"Slowed & Reverb DSP and exporter.\n"
	"by " SR_AUTHOR "\n\n"
	"- DSP: Preferences > Playback > DSP Manager > \"Slowed & Reverb\"\n"
	"- Panel: Layout editing mode > add \"Slowed & Reverb\", or View > Slowed & Reverb (right-click it for options)\n"
	"- Columns UI: add the \"Slowed & Reverb\" panel in its layout or toolbar area\n"
	"- Sliders: double-click a value to type an exact one\n"
	"- Export: right-click tracks > Slowed & Reverb > Export..., or the panel's Export button\n\n"
	"Reverb based on Freeverb by Jezar at Dreampoint (public domain).\n"
	"Columns UI support uses the Columns UI SDK by Reupen Shah.\n"
	"MP3/FLAC/OGG/M4A export uses ffmpeg.exe.\n\n"
	"Copyright (c) 2026 " SR_AUTHOR ". Released under the MIT License."
);

VALIDATE_COMPONENT_FILENAME("foo_slowed_reverb.dll");

FOOBAR2000_IMPLEMENT_CFG_VAR_DOWNGRADE;
