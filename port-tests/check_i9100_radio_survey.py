#!/usr/bin/env python3
"""Read-only radio supply survey parsing, error propagation and ARM compilation."""
import argparse
from i9100_offline_build import Evidence
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("--cross-compile",action="store_true")
a=p.parse_args()
e=Evidence("radio-survey-offline")
e.wait_idle_seconds=60
e.model("i9100_radio_survey_test.c",["i9100_radio_survey.c"])
if a.cross_compile:e.cross_compile(["i9100_radio_survey.c"])
e.finish(["i9100_radio_survey.c"],"Actual survey decode/report no partial publication; shared-read endpoint stand-in, no electrical/real radio proof")
