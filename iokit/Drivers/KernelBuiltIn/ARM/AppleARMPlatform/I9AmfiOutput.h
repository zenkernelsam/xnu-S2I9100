#ifndef I9_AMFI_OUTPUT_H
#define I9_AMFI_OUTPUT_H
/* Deliver the existing entitlement bytes through the output form selected by
 * IOConnectCallMethod. This helper changes no entitlement keys or values. */
static IOReturn i9_amfi_output(IOExternalMethodArguments *args,
    const unsigned char *bytes, size_t length, unsigned *copied)
{
    *copied = 0;
    if (!args || !bytes || !length || length == (size_t)-1) return kIOReturnBadArgument;
    size_t needed = length + 1;
    const unsigned char zero = 0;
    if (args->structureVariableOutputData) {
        if (*args->structureVariableOutputData) return kIOReturnBadArgument;
        OSData *data = OSData::withCapacity(needed);
        if (!data) return kIOReturnNoMemory;
        if (!data->appendBytes(bytes, length) || !data->appendBytes(&zero, 1)) {
            data->release(); return kIOReturnNoMemory;
        }
        *args->structureVariableOutputData = data;
        args->structureOutputSize = 0;
        *copied = needed;
        return kIOReturnSuccess;
    }
    if (args->structureOutputDescriptor) {
        size_t capacity = args->structureOutputDescriptorSize;
        if (capacity > args->structureOutputDescriptor->getLength() || needed > capacity) {
            args->structureOutputDescriptorSize = 0; return kIOReturnNoSpace;
        }
        args->structureOutputDescriptorSize = 0;
        if (args->structureOutputDescriptor->writeBytes(0, bytes, length) != length ||
            args->structureOutputDescriptor->writeBytes(length, &zero, 1) != 1)
            return kIOReturnIOError;
        args->structureOutputDescriptorSize = needed;
        args->structureOutputSize = 0;
        *copied = needed;
        return kIOReturnSuccess;
    }
    if (!args->structureOutput) return kIOReturnBadArgument;
    if (needed > args->structureOutputSize) {
        args->structureOutputSize = 0; return kIOReturnNoSpace;
    }
    memcpy(args->structureOutput, bytes, length);
    ((unsigned char *)args->structureOutput)[length] = 0;
    args->structureOutputSize = needed;
    *copied = needed;
    return kIOReturnSuccess;
}
#endif
