#ifndef I9_AKS_DEVELOPMENT_H
#define I9_AKS_DEVELOPMENT_H
/* Explicit S2 development state, not a secure key store. The iOS6 native
 * MKB wrappers use scalar selector7(flags),17(lock state),14(system bag).
 * No wrapped keys, credentials or persistent encryption are implemented. */
static inline bool i9_aks_development(uint32_t selector,IOExternalMethodArguments *a,
 bool enabled,IOReturn *result)
{
 if(!enabled || (selector!=7 && selector!=17 && selector!=14))return false;
 *result=kIOReturnBadArgument;
 if(!a || a->structureInputSize || a->structureOutputSize ||
  a->structureInputDescriptor || a->structureOutputDescriptor ||
  a->structureVariableOutputData || !a->scalarOutput || a->scalarOutputCount!=1)return true;
 if(selector==7){
  if(a->scalarInputCount!=1 || !a->scalarInput || a->scalarInput[0])return true;
  a->scalarOutput[0]=6; /* no passcode + unlocked since boot */
 }else{
  if(a->scalarInputCount)return true;
  a->scalarOutput[0]=selector==17?3U:0U; /* disabled passcode / implicit system handle */
 }
 a->scalarOutputCount=1;*result=kIOReturnSuccess;return true;
}
#endif
