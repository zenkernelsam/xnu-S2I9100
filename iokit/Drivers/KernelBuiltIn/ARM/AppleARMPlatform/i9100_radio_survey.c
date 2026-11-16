/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_radio_survey.h"
#include <string.h>
int i9100_radio_survey_read(struct i9100_power_read_client *client,struct i9100_radio_survey *out)
{
 if(!client||!out)return I9100_SHARED_ARGUMENT;
 uintptr_t a=(uintptr_t)client,b=(uintptr_t)out;
 if(a>UINTPTR_MAX-sizeof(*client)||b>UINTPTR_MAX-sizeof(*out)||(a+sizeof(*client)>b&&b+sizeof(*out)>a))return I9100_SHARED_ARGUMENT;
 struct i9100_radio_survey report;memset(&report,0,sizeof(report));
 int e=i9100_power_read_pmic(client,0,&report.pmic_id);if(e)return e;
 e=i9100_power_read_pmic(client,1,&report.pmic_version);if(e)return e;
 e=i9100_power_read_pmic(client,0x13,&report.control1);if(e)return e;
 e=i9100_power_read_pmic(client,0x4b,&report.ldo17);if(e)return e;
 uint8_t control=0,ldo=0;
 e=i9100_power_read_pmic(client,0x13,&control);if(e)return e;
 e=i9100_power_read_pmic(client,0x4b,&ldo);if(e)return e;
 if(control!=report.control1||ldo!=report.ldo17)return I9100_SHARED_BUSY;
 report.ap_clock_programmed=!!(control&1U);report.cp_clock_programmed=!!(control&2U);
 report.vtf_mode=ldo>>6;report.vtf_programmed_mv=800U+(ldo&63U)*50U;
 *out=report;return 0;
}
