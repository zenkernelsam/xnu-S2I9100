/* SPDX-License-Identifier: BSD-2-Clause; shared read endpoint stand-in, real survey parser. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "i9100_radio_survey.h"
static unsigned reads,fail_read,change_control,change_ldo;
static uint8_t registers[256];
int i9100_power_read_pmic(struct i9100_power_read_client *c,unsigned reg,uint8_t *out)
{
 assert(c->owned&&reg<=255U);reads++;
 if(reads==fail_read)return I9100_SHARED_IO;
 if((reg==0x13U&&reads==5U&&change_control)||(reg==0x4bU&&reads==6U&&change_ldo))*out=registers[reg]^1U;
 else *out=registers[reg];return 0;
}
int main(void)
{
 struct i9100_power_read_client client={0};client.owned=1;
 registers[0]=0x97;registers[1]=3;registers[0x13]=3;registers[0x4b]=0xe8;
 struct i9100_radio_survey out;memset(&out,0x5a,sizeof(out));
 assert(!i9100_radio_survey_read(&client,&out)&&reads==6U&&out.pmic_id==0x97&&out.pmic_version==3);
 assert(out.ap_clock_programmed&&out.cp_clock_programmed&&out.vtf_mode==3U&&out.vtf_programmed_mv==2800U);
 assert(!out.wiring_verified&&!out.electrical_verified);
 for(unsigned value=0;value<256U;value++){
  reads=0;registers[0x4b]=(uint8_t)value;assert(!i9100_radio_survey_read(&client,&out));
  assert(out.vtf_mode==value/64U&&out.vtf_programmed_mv==800U+(value%64U)*50U&&!out.electrical_verified);
 }
 registers[0x4b]=0xe8;
 for(unsigned n=1;n<=6U;n++){
  reads=0;fail_read=n;memset(&out,0x5a,sizeof(out));struct i9100_radio_survey before=out;
  assert(i9100_radio_survey_read(&client,&out)==I9100_SHARED_IO&&!memcmp(&out,&before,sizeof(out)));
 }
 fail_read=0;reads=0;change_control=1;assert(i9100_radio_survey_read(&client,&out)==I9100_SHARED_BUSY);
 change_control=0;reads=0;change_ldo=1;assert(i9100_radio_survey_read(&client,&out)==I9100_SHARED_BUSY);
 puts("PASS actual PMIC survey identity/CONTROL1 clock bits/LDO17 mode+voltage decode/all values/all partial failures/changed config/no electrical-proof fabrication; shared read endpoint stand-in only");return 0;
}
