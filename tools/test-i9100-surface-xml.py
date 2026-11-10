#!/usr/bin/env python3
"""Exercise production surface XML bounds/property gates with libkern objects modeled.

OSUnserializeXML is modeled; the native parser is verified by the ARM build
and real-device captures. Geometry and the wrapper below are actual source.
"""
import os
from pathlib import Path
import subprocess
import sys
from datetime import datetime

if os.environ.get('I9100_CPU_CAPPED') != '1':
    os.execvpe('taskset', ['taskset', '-c', '0-13', 'nice', '-n10', 'ionice', '-c2', '-n7',
                         sys.executable, __file__], dict(os.environ, I9100_CPU_CAPPED='1'))
repo = Path(__file__).resolve().parents[1]
source = (repo/'iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/IOS7LeoDisplay.cpp').read_text()
start = source.index('static bool ios7lab_surface_number(')
functions = source[start:source.index('\n#endif', start)]
legacy = source.split('static const char ios7lab_fullscreen_surface_request[] =', 1)[1].split(';', 1)[0]
out = repo/'out'/('surface-xml-check-'+datetime.now().strftime('%Y%m%d-%H%M%S'))
out.mkdir(parents=True, exist_ok=False)
prefix = r'''
#include <stdint.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include "IOS7LabSurfaceGeometry.h"
static unsigned parserCalls, releaseCalls, checks;
class OSObject { public: virtual ~OSObject() {} void release() { ++releaseCalls; } };
class OSNumber : public OSObject { public: uint64_t value; OSNumber(uint64_t v):value(v){}
 uint64_t unsigned64BitValue() const { return value; } };
class OSBoolean : public OSObject { public: bool value; OSBoolean(bool v):value(v){}
 bool isTrue() const { return value; } };
class OSString : public OSObject { public: std::string value; OSString(const char *v):value(v){}
 bool isEqualTo(const char *v) const { return value == v; } };
class OSDictionary : public OSObject { public: std::map<std::string,OSObject*> values;
 unsigned getCount() const { return values.size(); }
 OSObject *getObject(const char *key) { auto p=values.find(key); return p==values.end()?0:p->second; } };
#define OSDynamicCast(type,object) dynamic_cast<type*>(object)
static OSObject *parserRoot;
static OSObject *OSUnserializeXML(const char *) { ++parserCalls; return parserRoot; }
'''
suffix = r'''
static void check(bool ok, const char *why) { ++checks; if(!ok) { std::fprintf(stderr,"FAIL %s\n",why); std::exit(1); } }
int main() {
 OSNumber width(480),height(800),row(1920),format(0x42475241),element(4),allocation(1536000),cache(0x400);
 OSString region("PurpleGfxMem"); OSBoolean global(true); OSDictionary dict;
 dict.values={{"IOSurfaceWidth",&width},{"IOSurfaceHeight",&height},{"IOSurfaceBytesPerRow",&row},
  {"IOSurfacePixelFormat",&format},{"IOSurfaceBytesPerElement",&element},{"IOSurfaceAllocSize",&allocation},
  {"IOSurfaceCacheMode",&cache},{"IOSurfaceMemoryRegion",&region},{"IOSurfaceIsGlobal",&global}};
 parserRoot=&dict; IOS7LabSimpleSurfaceRequest g;
 unsigned before=parserCalls;
 check(ios7lab_surface_xml_geometry(xml,sizeof(xml),&g),"captured fullscreen properties accepted");
 check(g.width==480 && g.height==800 && g.bytesPerRow==1920 && g.allocationSize==1536000,"geometry retained");
 check(parserCalls==before+1 && releaseCalls==1,"parsed root released");
 width.value=640; height.value=480; row.value=2560; allocation.value=1228800;
 check(ios7lab_surface_xml_geometry(xml,sizeof(xml),&g) && g.width==640 && g.height==480,"other supported dimensions");
 format.value=0x52474241; check(ios7lab_surface_xml_geometry(xml,sizeof(xml),&g),"RGBA CPU backing"); format.value=0x42475241;
 auto accepts=[&](){return ios7lab_surface_xml_geometry(xml,sizeof(xml),&g);};
 width.value=UINT64_C(0x100000000); check(!accepts(),"wide integer cannot truncate"); width.value=640;
 width.value=0; check(!accepts(),"zero width"); width.value=2049; check(!accepts(),"width capacity"); width.value=640;
 height.value=2049; check(!accepts(),"height capacity"); height.value=480;
 row.value=2556; check(!accepts(),"undersized row"); row.value=2561; check(!accepts(),"unaligned row"); row.value=16388; check(!accepts(),"row capacity"); row.value=2560;
 allocation.value=1228799; check(!accepts(),"last row allocation"); allocation.value=16777217; check(!accepts(),"allocation capacity"); allocation.value=1228800;
 element.value=3; check(!accepts(),"unsupported element"); element.value=4;
 format.value=0x3231564e; check(!accepts(),"unsupported planar format"); format.value=0x42475241;
 cache.value=0; check(!accepts(),"cache semantics preserved"); cache.value=0x400;
 region.value="OtherMemory"; check(!accepts(),"unimplemented region rejected"); region.value="PurpleGfxMem";
 global.value=false; check(!accepts(),"unimplemented private semantics rejected"); global.value=true;
 dict.values["IOSurfacePlaneInfo"]=&width; check(!accepts(),"unimplemented planes rejected"); dict.values.erase("IOSurfacePlaneInfo");
 dict.values.erase("IOSurfaceWidth"); check(!accepts(),"required property missing"); dict.values["OtherWidth"]=&width; check(!accepts(),"unknown key cannot replace width"); dict.values.erase("OtherWidth"); dict.values["IOSurfaceWidth"]=&width;
 dict.values["IOSurfaceWidth"]=&region; check(!accepts(),"wrong numeric type"); dict.values["IOSurfaceWidth"]=&width;
 before=parserCalls; const char interior[]={'x',0,'y',0}; char oversized[2049]={0};
 check(!ios7lab_surface_xml_geometry(interior,sizeof(interior),&g),"embedded NUL rejected");
 check(!ios7lab_surface_xml_geometry(xml,sizeof(xml)-1,&g),"unterminated range rejected");
 check(!ios7lab_surface_xml_geometry(oversized,sizeof(oversized),&g),"bounded XML input");
 check(!ios7lab_surface_xml_geometry(0,4,&g) && !ios7lab_surface_xml_geometry(xml,0,&g),"null and empty input");
 check(!ios7lab_surface_xml_geometry(xml,sizeof(xml),0),"output required");
 check(parserCalls==before,"invalid spans never reach parser");
 parserRoot=0; check(!accepts(),"parser failure"); parserRoot=&width; check(!accepts(),"non-dictionary root");
 std::printf("%u production property/span checks passed; OSUnserializeXML modeled\n",checks);
}
'''
cpp = out/'surface-xml.cpp'
cpp.write_text(prefix+functions+'\nstatic const char xml[] = '+legacy+';\n'+suffix)
binary = out/'surface-xml'
subprocess.run(['clang++', '-std=c++11', '-O1', '-g', '-fsanitize=address,undefined',
                '-fno-sanitize-recover=all', '-fno-omit-frame-pointer',
                '-I', str(repo/'iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform'),
                str(cpp), '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
print(out, flush=True)
