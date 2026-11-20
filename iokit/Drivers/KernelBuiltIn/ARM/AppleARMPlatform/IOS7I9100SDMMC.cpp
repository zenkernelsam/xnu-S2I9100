/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Samsung Galaxy S2 (GT-I9100) SDHCI MicroSD block storage device.
 * Exposes SD card as IOBlockStorageDevice so IOS7LeoHFSFile
 * can locate IOS7/ROOTFS.HFS via fat_locator.
 */

#if defined(BOARD_CONFIG_EXYNOS4210_I9100)

#include <IOKit/storage/IOBlockStorageDevice.h>
#include <IOKit/storage/IOStorage.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOCatalogue.h>
#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSUnserialize.h>
#include <kern/debug.h>
#include "i9100_sdmmc.h"
#include "IOS7LabStoragePersonalities.h"

extern "C" {
#include <mach/mach_time.h>
}

/* C ABI counters for the panel performance strip. Single writer under mutex;
 * 32-bit snapshots avoid torn 64-bit loads on this ARMv7 bring-up target. */
extern "C" {
volatile uint32_t i9100_perf_sd_kib, i9100_perf_sd_ms;
volatile uint32_t i9100_perf_sd_div, i9100_perf_sd_errors;
}

class IOS7I9100SDMMC : public IOBlockStorageDevice {
	OSDeclareDefaultStructors(IOS7I9100SDMMC)

	IOMemoryDescriptor *mmio;
	IOMemoryMap *map;
	volatile UInt8 *regs;
	IOLock *mutex;
	struct i9100_sd_card card;
	bool initialized;

	static uint32_t readMMIO32(void *, unsigned);
	static void writeMMIO32(void *, unsigned, uint32_t);
	static uint16_t readMMIO16(void *, unsigned);
	static void writeMMIO16(void *, unsigned, uint16_t);
	static uint8_t readMMIO8(void *, unsigned);
	static void writeMMIO8(void *, unsigned, uint8_t);
	static int delayUS(void *, uint32_t);
	static uint64_t nowNS(void *, int);

public:
	virtual bool init(OSDictionary *properties = 0);
	virtual bool start(IOService *provider);
	virtual void free();
	virtual IOReturn doAsyncReadWrite(IOMemoryDescriptor *, UInt64, UInt64,
	                                  IOStorageAttributes *, IOStorageCompletion *);
	virtual IOReturn doEjectMedia() { return kIOReturnUnsupported; }
	virtual IOReturn doFormatMedia(UInt64) { return kIOReturnUnsupported; }
	virtual UInt32 doGetFormatCapacities(UInt64 *, UInt32) const { return 0; }
	virtual IOReturn doLockUnlockMedia(bool) { return kIOReturnUnsupported; }
	virtual IOReturn doSynchronizeCache() { return kIOReturnUnsupported; }
	virtual char *getVendorString();
	virtual char *getProductString();
	virtual char *getRevisionString();
	virtual char *getAdditionalDeviceInfoString();
	virtual IOReturn reportBlockSize(UInt64 *);
	virtual IOReturn reportMaxValidBlock(UInt64 *);
	virtual IOReturn reportEjectability(bool *);
	virtual IOReturn reportLockability(bool *);
	virtual IOReturn reportRemovability(bool *);
	virtual IOReturn reportMediaState(bool *, bool *);
	virtual IOReturn reportPollRequirements(bool *, bool *);
	virtual IOReturn reportWriteProtection(bool *);
};

OSDefineMetaClassAndStructors(IOS7I9100SDMMC, IOBlockStorageDevice)

uint32_t IOS7I9100SDMMC::readMMIO32(void *o, unsigned off)
{
	IOS7I9100SDMMC *s = (IOS7I9100SDMMC *)o;
	__asm__ __volatile__("dsb sy" ::: "memory");
	UInt32 v = *(volatile UInt32 *)(s->regs + off);
	__asm__ __volatile__("dsb sy" ::: "memory");
	return v;
}

void IOS7I9100SDMMC::writeMMIO32(void *o, unsigned off, uint32_t v)
{
	IOS7I9100SDMMC *s = (IOS7I9100SDMMC *)o;
	__asm__ __volatile__("dsb sy" ::: "memory");
	*(volatile UInt32 *)(s->regs + off) = v;
	__asm__ __volatile__("dsb sy" ::: "memory");
}

uint16_t IOS7I9100SDMMC::readMMIO16(void *o, unsigned off)
{
	IOS7I9100SDMMC *s = (IOS7I9100SDMMC *)o;
	__asm__ __volatile__("dsb sy" ::: "memory");
	UInt16 v = *(volatile UInt16 *)(s->regs + off);
	__asm__ __volatile__("dsb sy" ::: "memory");
	return v;
}

void IOS7I9100SDMMC::writeMMIO16(void *o, unsigned off, uint16_t v)
{
	IOS7I9100SDMMC *s = (IOS7I9100SDMMC *)o;
	__asm__ __volatile__("dsb sy" ::: "memory");
	*(volatile UInt16 *)(s->regs + off) = v;
	__asm__ __volatile__("dsb sy" ::: "memory");
}

uint8_t IOS7I9100SDMMC::readMMIO8(void *o, unsigned off)
{
	IOS7I9100SDMMC *s = (IOS7I9100SDMMC *)o;
	__asm__ __volatile__("dsb sy" ::: "memory");
	UInt8 v = *(volatile UInt8 *)(s->regs + off);
	__asm__ __volatile__("dsb sy" ::: "memory");
	return v;
}

void IOS7I9100SDMMC::writeMMIO8(void *o, unsigned off, uint8_t v)
{
	IOS7I9100SDMMC *s = (IOS7I9100SDMMC *)o;
	__asm__ __volatile__("dsb sy" ::: "memory");
	*(volatile UInt8 *)(s->regs + off) = v;
	__asm__ __volatile__("dsb sy" ::: "memory");
}

uint64_t IOS7I9100SDMMC::nowNS(void *, int)
{
	return mach_absolute_time();
}

int IOS7I9100SDMMC::delayUS(void *, uint32_t us)
{
	IODelay(us);
	return 1;
}

bool IOS7I9100SDMMC::init(OSDictionary *p)
{
	mmio = 0; map = 0; regs = 0; mutex = 0; initialized = false;
	struct i9100_sd_io io = {
		this,
		readMMIO32, writeMMIO32,
		readMMIO16, writeMMIO16,
		readMMIO8,  writeMMIO8,
		delayUS, nowNS, 100000U
	};
	i9100_sd_init(&card, &io);
	return IOBlockStorageDevice::init(p);
}

/* Only the known MAX8997 PMIC bus is accessed. Preserve the pin mux and
 * never send regulator register writes to devices discovered by scanning. */
class I9100SDPowerBus {
 volatile UInt32 *gpb;
 void low(unsigned pin) {
  gpb[1] &= ~(1U << pin);
  gpb[0] = (gpb[0] & ~(15U << (pin * 4))) | (1U << (pin * 4));
  OSSynchronizeIO();
 }
 void release(unsigned pin) {
  gpb[0] &= ~(15U << (pin * 4));
  OSSynchronizeIO();
 }
 bool clockHigh() {
  release(7);
  for (unsigned n = 0; n < 1000; n++) {
   if (gpb[1] & (1U << 7)) { IODelay(5); return true; }
   IODelay(1);
  }
  return false;
 }
 bool start() {
  release(6);
  if (!clockHigh()) return false;
  low(6); IODelay(5); low(7); IODelay(5);
  return true;
 }
 void stop() {
  low(7); low(6); IODelay(5);
  clockHigh(); release(6); IODelay(5);
 }
 bool send(UInt8 byte) {
  for (int i = 7; i >= 0; i--) {
   if (byte & (1U << i)) release(6); else low(6);
   IODelay(5);
   if (!clockHigh()) return false;
   low(7); IODelay(5);
  }
  release(6); IODelay(5);
  if (!clockHigh()) return false;
  bool ack = !(gpb[1] & (1U << 6));
  low(7); IODelay(5);
  return ack;
 }
public:
 I9100SDPowerBus(volatile UInt32 *bank) : gpb(bank) {}
 bool recoverBus() {
  release(6);
  for (unsigned n = 0; n < 9; n++) {
   low(7); IODelay(5);
   if (!clockHigh()) { stop(); return false; }
  }
  stop();
  return (gpb[1] & 0xc0U) == 0xc0U;
 }
 bool read(UInt8 reg, UInt8 *value) {
  if (!start() || !send(0xcc) || !send(reg) || !start() || !send(0xcd)) {
   stop(); return false;
  }
  UInt8 byte = 0;
  release(6);
  for (int i = 7; i >= 0; i--) {
   IODelay(5);
   if (!clockHigh()) { stop(); return false; }
   if (gpb[1] & (1U << 6)) byte |= (1U << i);
   low(7); IODelay(5);
  }
  /* NACK the single received byte. */
  release(6); IODelay(5);
  if (!clockHigh()) { stop(); return false; }
  low(7); stop(); *value = byte; return true;
 }
 bool write(UInt8 reg, UInt8 value) {
  bool ok = start() && send(0xcc) && send(reg) && send(value);
  stop(); return ok;
 }
};

static void i9100_sd_slot_power()
{
 IOMemoryDescriptor *desc = IOMemoryDescriptor::withPhysicalAddress(0x11400000U, 0x1000, kIODirectionInOut);
 if (!desc) return;
 IOMemoryMap *mapping = desc->map(kIOMapInhibitCache);
 if (mapping && mapping->getAddress()) {
  volatile UInt32 *gpb = (volatile UInt32 *)(mapping->getAddress() + 0x40U);
  UInt32 con = gpb[0], dat = gpb[1], pud = gpb[2];
  gpb[2] = (pud & ~0xf000U) | 0xf000U;
  I9100SDPowerBus bus(gpb);
  UInt8 before = 0, after = 0;
  bool ok = bus.recoverBus() && bus.read(0x4b, &before);
  /* MAX8997 LDO17 is VTF: 3.0V, normal enabled mode. */
  if (ok && before != 0xecU) ok = bus.write(0x4b, 0xecU);
  if (ok) ok = bus.read(0x4b, &after) && after == 0xecU;
  /* Restore just the two pins used by this transaction. */
  gpb[1] = (gpb[1] & ~0xc0U) | (dat & 0xc0U);
  gpb[2] = (gpb[2] & ~0xf000U) | (pud & 0xf000U);
  gpb[0] = (gpb[0] & ~0xff000000U) | (con & 0xff000000U);
  OSSynchronizeIO();
  IOLog("i9100 SD: MAX8997 LDO17 before=%02x after=%02x verified=%u\n", before, after, (unsigned)ok);
  if (ok) IODelay(25000);
  else IOLog("i9100 SD: PMIC unavailable, using inherited slot power\n");
 }
 if (mapping) mapping->release();
 desc->release();
}

bool IOS7I9100SDMMC::start(IOService *provider)
{
 if (!IOBlockStorageDevice::start(provider)) return false;
 mutex = IOLockAlloc();
 if (!mutex) return false;
 mmio = IOMemoryDescriptor::withPhysicalAddress(I9100_SDMMC_PA, I9100_SDMMC_SZ, kIODirectionInOut);
 if (!mmio) return false;
 map = mmio->map(kIOMapInhibitCache);
 if (!map || !map->getAddress()) return false;
 regs = (volatile UInt8 *)map->getAddress();
 IOMemoryDescriptor *cmuDesc = IOMemoryDescriptor::withPhysicalAddress(0x10030000U, 0x10000, kIODirectionInOut);
 if (!cmuDesc) return false;
 IOMemoryMap *cmuMap = cmuDesc->map(kIOMapInhibitCache);
 bool clockOK = false;
 if (cmuMap && cmuMap->getAddress()) {
  volatile UInt32 *cmu = (volatile UInt32 *)cmuMap->getAddress();
  cmu[0xc940 / 4] |= (1U << 7); /* CLK_GATE_IP_FSYS: HSMMC2 */
  OSSynchronizeIO();
  /* Enable the register bus before accessing HSMMC, then stop SDCLK. */
  writeMMIO16(this, 0x2c, 0);
  cmu[0xc240 / 4] = (cmu[0xc240 / 4] & ~(15U << 8)) | (6U << 8); /* MPLL */
  /* MMC2 ratio is 4 bits; pre-ratio is 8 bits, not 4.
   * MPLL/(7+1)/(1+1) ~= 50MHz, then SDHCI /256 ~= 200kHz. */
  cmu[0xc548 / 4] = (cmu[0xc548 / 4] & ~0xff0fU) | 0x0107U;
  OSSynchronizeIO();
  for (unsigned n = 0; n < 10000; n++) {
   if (!(cmu[0xc648 / 4] & 0x101U)) { clockOK = true; break; }
   IODelay(1);
  }
  cmu[0xc340 / 4] |= (1U << 8); /* CLK_SRC_MASK_FSYS: sclk_mmc2 */
  OSSynchronizeIO();
  IOLog("i9100 SD: CMU src=%08x div=%08x mask=%08x stable=%u\n",
        (unsigned)cmu[0xc240 / 4], (unsigned)cmu[0xc548 / 4],
        (unsigned)cmu[0xc340 / 4], (unsigned)clockOK);
 }
 if (cmuMap) cmuMap->release();
 cmuDesc->release();
 if (!clockOK) return false;

 IOMemoryDescriptor *gpioDesc = IOMemoryDescriptor::withPhysicalAddress(0x11000000U, 0x1000, kIODirectionInOut);
 if (!gpioDesc) return false;
 IOMemoryMap *gpioMap = gpioDesc->map(kIOMapInhibitCache);
 bool pinsOK = gpioMap && gpioMap->getAddress();
 if (pinsOK) {
  volatile UInt32 *gpio = (volatile UInt32 *)gpioMap->getAddress();
  gpio[0x80 / 4] = (gpio[0x80 / 4] & ~0x0ffff0ffU) | 0x02222022U;
  /* Linux Exynos4 routing: no pull on CLK/CMD, pull-up on DAT[0..3]. */
  gpio[0x88 / 4] = (gpio[0x88 / 4] & ~0x3fcfU) | 0x3fc0U;
  gpio[0x8c / 4] = (gpio[0x8c / 4] & ~0x3fcfU) | 0x3fcfU;
  OSSynchronizeIO();
  IOLog("i9100 SD: GPK2 con=%08x pud=%08x drv=%08x card-detect=%u\n",
        (unsigned)gpio[0x80 / 4], (unsigned)gpio[0x88 / 4],
        (unsigned)gpio[0x8c / 4], (unsigned)((gpio[0xc64 / 4] >> 4) & 1U));
 }
 if (gpioMap) gpioMap->release();
 gpioDesc->release();
 if (!pinsOK) return false;
 i9100_sd_slot_power();

 int rc = i9100_sd_probe(&card);
 if (rc != I9100_SD_OK) {
  IOLog("i9100 SD: probe failed rc=%d int=%08x state=%08x\n", rc,
        (unsigned)readMMIO32(this, 0x30), (unsigned)readMMIO32(this, 0x24));
  return false;
 }
 initialized = true;
 i9100_perf_sd_div = card.clock_div;
 IOLog("i9100 SD: ready, sectors=%llu capacity=%llu MB divider=%u fallbacks=%u\n",
       (unsigned long long)card.sectors, (unsigned long long)(card.sectors / 2048ULL),
       card.clock_div, card.speed_fallbacks);
 OSObject *dec = OSUnserializeXML(ios7labStoragePersonalities);
 OSArray *drivers = OSDynamicCast(OSArray, dec);
 bool added = drivers && gIOCatalogue->addDrivers(drivers, false);
 if (dec) dec->release();
 return added;
}

void IOS7I9100SDMMC::free()
{
	if (mutex) IOLockFree(mutex);
	if (map) map->release();
	if (mmio) mmio->release();
	IOBlockStorageDevice::free();
}

char *IOS7I9100SDMMC::getVendorString() { static char t[] = "Samsung"; return t; }
char *IOS7I9100SDMMC::getProductString() { static char t[] = "i9100 SDHCI MicroSD"; return t; }
char *IOS7I9100SDMMC::getRevisionString() { static char t[] = "PIO"; return t; }
char *IOS7I9100SDMMC::getAdditionalDeviceInfoString() { static char t[] = "Exynos 4210 SDHCI2"; return t; }

IOReturn IOS7I9100SDMMC::reportBlockSize(UInt64 *v)
{
	if (!v) return kIOReturnBadArgument;
	*v = 512;
	return kIOReturnSuccess;
}

IOReturn IOS7I9100SDMMC::reportMaxValidBlock(UInt64 *v)
{
	if (!v || !initialized) return kIOReturnNotReady;
	*v = card.sectors - 1;
	return kIOReturnSuccess;
}

IOReturn IOS7I9100SDMMC::reportEjectability(bool *v)
{
	if (!v) return kIOReturnBadArgument;
	*v = false;
	return kIOReturnSuccess;
}

IOReturn IOS7I9100SDMMC::reportLockability(bool *v)
{
	if (!v) return kIOReturnBadArgument;
	*v = false;
	return kIOReturnSuccess;
}

IOReturn IOS7I9100SDMMC::reportRemovability(bool *v)
{
	if (!v) return kIOReturnBadArgument;
	*v = false;
	return kIOReturnSuccess;
}

IOReturn IOS7I9100SDMMC::reportMediaState(bool *p, bool *c)
{
	if (!p || !c) return kIOReturnBadArgument;
	IOLockLock(mutex);
	*p = initialized && card.ready;
	*c = false;
	IOLockUnlock(mutex);
	return kIOReturnSuccess;
}

IOReturn IOS7I9100SDMMC::reportPollRequirements(bool *r, bool *e)
{
	if (!r || !e) return kIOReturnBadArgument;
	*r = false;
	*e = false;
	return kIOReturnSuccess;
}

IOReturn IOS7I9100SDMMC::reportWriteProtection(bool *v)
{
	if (!v) return kIOReturnBadArgument;
	*v = true; /* Read-only base media; COW handles volatile writes in RAM */
	return kIOReturnSuccess;
}

IOReturn IOS7I9100SDMMC::doAsyncReadWrite(IOMemoryDescriptor *buffer, UInt64 block, UInt64 count,
                                         IOStorageAttributes *attributes, IOStorageCompletion *completion)
{
 if (!buffer || !completion || !completion->action || !initialized ||
     !count || block >= card.sectors || count > card.sectors - block ||
     count > UINT64_MAX / 512U || count * 512U != buffer->getLength())
  return kIOReturnBadArgument;
 if (buffer->getDirection() != kIODirectionIn) return kIOReturnNotWritable;
 if (attributes && (attributes->options || attributes->reserved0032 || attributes->reserved0064))
  return kIOReturnUnsupported;
 IOStorageCompletion saved = *completion;
 UInt64 completed = 0;
 IOReturn result = kIOReturnSuccess;
 /* Bounded scratch space; never allocate the entire request under pressure. */
 UInt8 *tmp = (UInt8 *)IOMalloc(512);
 if (!tmp) return kIOReturnNoMemory;
 IOLockLock(mutex);
 uint64_t readStart = mach_absolute_time();
 for (UInt64 i = 0; i < count; i++) {
  if (i9100_sd_read512(&card, block + i, tmp) != I9100_SD_OK) {
   result = kIOReturnIOError; break;
  }
  if (buffer->writeBytes(completed, tmp, 512) != 512) {
   result = kIOReturnUnderrun; break;
  }
  completed += 512;
 }
 /* Include buffer delivery in elapsed read-path time, exclude lock wait. */
 static uint64_t totalReadBytes, totalReadNS;
 totalReadBytes += completed;
 totalReadNS += mach_absolute_time() - readStart;
 i9100_perf_sd_kib = (uint32_t)(totalReadBytes / 1024ULL);
 i9100_perf_sd_ms = (uint32_t)(totalReadNS / 1000000ULL);
 i9100_perf_sd_div = card.clock_div;
 if (result != kIOReturnSuccess) i9100_perf_sd_errors++;
 IOLockUnlock(mutex);
 IOFree(tmp, 512);

	/* Heartbeat every 4096th read block */
	static unsigned long i9100_sdrw_n;
	if (((++i9100_sdrw_n) & 0xFFFUL) == 1UL) {
		IOLog("i9100 SDRW: blk=%llu n=%llu done=%llu (rd#%lu)\n",
		      (unsigned long long)block, (unsigned long long)count,
		      (unsigned long long)completed, i9100_sdrw_n);
        IOLog("i9100 SDPERF: KiB=%u ms=%u div=%u errors=%u\n",
              i9100_perf_sd_kib, i9100_perf_sd_ms,
              i9100_perf_sd_div, i9100_perf_sd_errors);
	}

	IOStorage::complete(&saved, result, completed);
	return kIOReturnSuccess;
}

void ios7i9100_register_sd(IOService *platform)
{
	IOS7I9100SDMMC *device = new IOS7I9100SDMMC;
	if (!device) {
		panic("i9100 SDMMC: mandatory object allocation failed");
		return;
	}
	bool attached = false, success = false;
	if (device->init())
		attached = device->attach(platform);
	if (attached && device->start(platform)) {
		device->registerService();
		success = true;
	}
	if (!success)
		IOLog("i9100 SDMMC: object init/attach/map/start failed\n");
	if (attached && !success)
		device->detach(platform);
	device->release();
}

#endif /* BOARD_CONFIG_EXYNOS4210_I9100 */
