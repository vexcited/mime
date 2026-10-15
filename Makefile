# mime build. requires llvm clang, lld-link and llvm-nm.
#   make            everything below
#   make host       the mime command, build/mime, with the guest files embedded
#   make guest      the guest system files, build/guest/system32
#   make examples   the example programs, build/examples
#
# the guest runs the host's processor: arm64 on macos, x64 on linux, where the
# x64 system dlls of build/guest/sysx64 become C:\Windows\System32.

BUILD := build
CC := clang
LLD_LINK := lld-link
NM := llvm-nm
RC := llvm-rc
STRIP := llvm-strip

ifeq ($(filter clean,$(MAKECMDGOALS)),)
$(foreach tool,$(CC) $(LLD_LINK) $(NM) $(RC) $(STRIP),$(if $(shell command -v $(tool)),,\
    $(error $(tool) not found: install llvm, clang and lld, see README.md)))
endif
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
GUEST_ARCH ?= arm64
HOST_CFLAGS := -std=c17 -O2 -g -Wall -Wextra -Wno-unused-parameter -Iinclude -MMD -MP
HOST_LIBS := -framework Hypervisor -framework AppKit -framework QuartzCore -framework CoreGraphics -framework ImageIO -framework Metal -framework IOSurface -framework Carbon -framework AudioToolbox -framework CoreAudio -lobjc
else
GUEST_ARCH ?= x64
HOST_CFLAGS := -std=gnu17 -D_GNU_SOURCE -O2 -g -Wall -Wextra -Wno-unused-parameter -Iinclude -MMD -MP
HOST_LIBS := -lpthread -lm $(shell pkg-config --libs sdl3 wayland-client)
HOST_CFLAGS += $(shell pkg-config --cflags sdl3 wayland-client)
endif
CXX := clang++

ifeq ($(GUEST_ARCH),x64)
GUEST_TARGET := --target=x86_64-pc-windows-msvc -march=x86-64 -mcx16
GUEST_MACHINE := x64
else
GUEST_TARGET := --target=aarch64-pc-windows-msvc -march=armv8.2-a
GUEST_MACHINE := arm64
endif
GUEST_CFLAGS := $(GUEST_TARGET) -std=c17 -O2 -g -gdwarf -ffreestanding -fms-extensions -fno-stack-protector \
	-Iinclude -Iguest/include -Iguest/rtl -I$(BUILD)/gen \
	-Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -MMD -MP
GUEST_ASFLAGS := $(GUEST_TARGET) -Iinclude -Iguest/include -MMD -MP
# the header fields of windows' own binaries: version 10.0, reproducible timestamps, a valid checksum.
GUEST_LDFLAGS := /nologo /debug:dwarf /machine:$(GUEST_MACHINE) /nodefaultlib /opt:ref /incremental:no /filealign:0x1000 \
	/version:10.0 /osversion:10.0 /Brepro /release

GEN := $(BUILD)/gen
OBJ := $(BUILD)/obj
SYSTEM32 := $(BUILD)/guest/system32

GUEST_DLLS := ntdll kernelbase kernel32 win32u gdi32 user32 mimex86

# the dlls beyond the core, as name:preferred base, each built for arm64 into System32 and x64 into SysX64.
# guest/<name>/*.c with guest/<name>/arm64/*.S and guest/<name>/x64/*.S for each processor, exported
# with dllexport or guest/<name>/<name>.def, entry point DllMain. they import
# from ntdll, kernel32, user32, gdi32 and win32u only, and reach each other with LoadLibrary.
EXTRA_DLLS := msvcrt:0x7FF6F0000000 advapi32:0x7FF6E0000000 ws2_32:0x7FF6D0000000 iphlpapi:0x7FF6C0000000 \
	dnsapi:0x7FF6B0000000 ole32:0x7FF6A0000000 oleaut32:0x7FF690000000 rpcrt4:0x7FF680000000 \
	shell32:0x7FF670000000 shlwapi:0x7FF660000000 comdlg32:0x7FF650000000 comctl32:0x7FF640000000 \
	crypt32:0x7FF630000000 schannel:0x7FF620000000 secur32:0x7FF610000000 uxtheme:0x7FF600000000 \
	imm32:0x7FF5F0000000 usp10:0x7FF5E0000000 dwmapi:0x7FF5D0000000 winmm:0x7FF5C0000000 \
	version:0x7FF5B0000000 userenv:0x7FF5A0000000 wtsapi32:0x7FF590000000 netapi32:0x7FF580000000 \
	odbc32:0x7FF570000000 winspool:0x7FF560000000 msi:0x7FF550000000 dbghelp:0x7FF540000000 \
	bcrypt:0x7FF530000000 avrt:0x7FF520000000 dinput8:0x7FF510000000 dwrite:0x7FF500000000 \
	dxgi:0x7FF4F0000000 wsock32:0x7FF4E0000000 ucrtbase:0x7FF4D0000000 mpr:0x7FF4C0000000 wintrust:0x7FF4B0000000 mmdevapi:0x7FF4A0000000 \
	gdiplus:0x7FF490000000 wininet:0x7FF480000000 urlmon:0x7FF470000000 msimg32:0x7FF460000000 \
	oleacc:0x7FF450000000 oledlg:0x7FF440000000 combase:0x7FF430000000 d3d11:0x7FF420000000
EXTRA_NAMES := $(foreach d,$(EXTRA_DLLS),$(firstword $(subst :, ,$(d))))
# the file name of each, winspool being a driver file.
extra_file = $(if $(filter winspool,$(1)),$(1).drv,$(1).dll)
# the x64 builds of the system dlls, for programs run by the translator, in C:\Windows\SysX64.
SYSX64 := $(BUILD)/guest/sysx64
X64_DLLS := ntdll kernelbase kernel32 win32u gdi32 user32
X64_FILES := $(patsubst %,$(SYSX64)/%.dll,$(X64_DLLS)) $(foreach n,$(EXTRA_NAMES),$(SYSX64)/$(call extra_file,$(n)))
X64_FILES += $(SYSX64)/opengl32.dll
ifeq ($(GUEST_ARCH),x64)
X64_FILES += $(patsubst %,$(SYSX64)/%.dll,wow64 wow64win wow64cpu)
endif
ifeq ($(GUEST_ARCH),x64)
# the kernel files built for x64, the dlls being the x64 set.
GUEST_FILES := $(SYSTEM32)/ntoskrnl.exe $(SYSTEM32)/win32k.sys $(X64_FILES)
# the copies embedded in mime, without the debug information that stays in build/guest for symbolizing.
ROM_FILES := $(BUILD)/guest/rom/ntoskrnl.exe $(BUILD)/guest/rom/win32k.sys \
	$(patsubst $(SYSX64)/%,$(BUILD)/guest/rom/%,$(X64_FILES))
else
GUEST_FILES := $(SYSTEM32)/ntoskrnl.exe $(SYSTEM32)/win32k.sys $(patsubst %,$(SYSTEM32)/%.dll,$(GUEST_DLLS)) \
	$(foreach n,$(EXTRA_NAMES),$(SYSTEM32)/$(call extra_file,$(n))) $(SYSTEM32)/opengl32.dll
ROM_FILES := $(patsubst $(SYSTEM32)/%,$(BUILD)/guest/rom/%,$(GUEST_FILES)) \
	$(patsubst $(SYSX64)/%,$(BUILD)/guest/rom/sysx64/%,$(X64_FILES))
endif

SHADERC_CXXFLAGS := -std=c++17 -O2 -w -Ithird_party/glslang -Ithird_party/glslang/glslang -Ithird_party \
	-DSPIRV_CROSS_C_API_MSL=1 -DSPIRV_CROSS_C_API_GLSL=1 -MMD -MP
SHADERC_SRC := $(wildcard third_party/glslang/glslang/GenericCodeGen/*.cpp third_party/glslang/glslang/MachineIndependent/*.cpp \
	third_party/glslang/glslang/MachineIndependent/preprocessor/*.cpp third_party/glslang/glslang/ResourceLimits/*.cpp \
	third_party/glslang/glslang/CInterface/*.cpp third_party/glslang/SPIRV/CInterface/*.cpp) \
	third_party/glslang/glslang/OSDependent/Unix/ossource.cpp \
	$(patsubst %,third_party/glslang/SPIRV/%.cpp,GlslangToSpv InReadableOrder Logger SpvBuilder SpvPostProcess SpvTools disassemble doc) \
	$(patsubst %,third_party/SPIRV-Cross/%.cpp,spirv_cfg spirv_cross spirv_cross_c spirv_cross_parsed_ir spirv_cross_util spirv_glsl \
	spirv_msl spirv_parser)
SHADERC_OBJ := $(SHADERC_SRC:%.cpp=$(OBJ)/%.o)

.PHONY: all host guest examples tools clean sysx64
all: host guest examples

# ---------------------------------------------------------------- host

ifeq ($(UNAME_S),Darwin)
HOST_SRC := $(wildcard host/*.c host/setup/*.c host/vmm/*.c host/ui/*.c host/gpu/*.c host/audio/*.c)
HOST_OBJ := $(HOST_SRC:%.c=$(OBJ)/%.o) $(OBJ)/gen/rom.o $(SHADERC_OBJ)
else
# kvm, and the linux ui, gpu and audio, in place of the macos frameworks.
HOST_SRC := $(wildcard host/*.c host/setup/*.c host/linux/*.c) \
	$(filter-out host/vmm/vm.c host/vmm/vcpu.c host/vmm/lazy.c host/vmm/loader.c host/vmm/window.c,$(wildcard host/vmm/*.c))
HOST_OBJ := $(HOST_SRC:%.c=$(OBJ)/%.o) $(OBJ)/gen/rom.o $(OBJ)/host/gen/vulkan/mvk_structs.o \
	$(OBJ)/host/gen/vulkan/mvk_host_commands.o
endif

# ---------------------------------------------------------------- the vulkan wire

VK_GEN := $(GEN)/vulkan
VK_CFLAGS := -Ithird_party/khronos -I$(VK_GEN)

$(VK_GEN)/stamp: tools/genvk.py third_party/khronos/registry/vk.xml
	@mkdir -p $(VK_GEN)
	python3 tools/genvk.py third_party/khronos/registry/vk.xml $(VK_GEN)
	touch $@

$(VK_GEN)/mvk_protocol.h $(VK_GEN)/mvk_structs.c $(VK_GEN)/mvk_host_commands.c $(VK_GEN)/mvk_guest_commands.c \
	$(VK_GEN)/mvk_guest_out.c $(VK_GEN)/mvk_guest_procs.c $(VK_GEN)/mvk_dispatch.h $(VK_GEN)/mvk_exports.def: $(VK_GEN)/stamp ;

$(OBJ)/host/gen/vulkan/%.o: $(VK_GEN)/%.c $(VK_GEN)/stamp
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) $(VK_CFLAGS) -Ihost/linux -Wno-unused-variable -Wno-unused-function -c $< -o $@

$(OBJ)/host/linux/vulkan.o $(OBJ)/host/linux/vkwsi.o: $(VK_GEN)/stamp
$(OBJ)/host/linux/vulkan.o $(OBJ)/host/linux/vkwsi.o: HOST_CFLAGS += $(VK_CFLAGS)

host: $(BUILD)/mime

$(BUILD)/mime: $(HOST_OBJ) host/mime.entitlements
	$(CXX) -o $@ $(HOST_OBJ) $(HOST_LIBS)
ifeq ($(UNAME_S),Darwin)
	codesign -s - -f --entitlements host/mime.entitlements $@ 2>/dev/null
endif

# the host gpu translates glsl through glslang and SPIRV-Cross, kept in third_party with their licenses.
$(OBJ)/host/gpu/%.o: host/gpu/%.c
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) -fblocks -Ithird_party -Ithird_party/glslang -c $< -o $@

$(OBJ)/host/%.o: host/%.c
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) -c $< -o $@

$(OBJ)/third_party/%.o: third_party/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(SHADERC_CXXFLAGS) -c $< -o $@

# the guest files are embedded in mime, which maps them into the guest.
$(GEN)/rom.S: $(ROM_FILES) tools/genrom.sh
	@mkdir -p $(dir $@)
	tools/genrom.sh $@ $(UNAME_S) $(abspath $(ROM_FILES))

$(BUILD)/guest/rom/sysx64/%: $(SYSX64)/% $(BUILD)/tools/pechecksum
	@mkdir -p $(dir $@)
	$(STRIP) --strip-debug $< -o $@
	$(BUILD)/tools/pechecksum $@

$(BUILD)/guest/rom/%: $(SYSTEM32)/% $(BUILD)/tools/pechecksum
	@mkdir -p $(dir $@)
	$(STRIP) --strip-debug $< -o $@
	$(BUILD)/tools/pechecksum $@

ifeq ($(GUEST_ARCH),x64)
$(BUILD)/guest/rom/%.dll: $(SYSX64)/%.dll $(BUILD)/tools/pechecksum
	@mkdir -p $(dir $@)
	$(STRIP) --strip-debug $< -o $@
	$(BUILD)/tools/pechecksum $@

$(BUILD)/guest/rom/%.drv: $(SYSX64)/%.drv $(BUILD)/tools/pechecksum
	@mkdir -p $(dir $@)
	$(STRIP) --strip-debug $< -o $@
	$(BUILD)/tools/pechecksum $@
endif

# the version resource of each system file.
$(GEN)/res/%.res: tools/genversion.sh
	@mkdir -p $(dir $@)
	tools/genversion.sh $* $(GEN)/res/$*.rc
	$(RC) -no-preprocess -c 1252 -fo $@ $(GEN)/res/$*.rc

$(OBJ)/gen/rom.o: $(GEN)/rom.S
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@

# ---------------------------------------------------------------- tools

tools: $(BUILD)/tools/gensyscalls $(BUILD)/tools/genservices $(BUILD)/tools/pechecksum

$(BUILD)/tools/test_pe_icon: tools/test_pe_icon.c host/ui/pe_icon.c host/ui/pe_icon.h
	@mkdir -p $(dir $@)
	$(CC) -std=c17 -O2 -Wall -Wextra -o $@ tools/test_pe_icon.c host/ui/pe_icon.c

$(BUILD)/tools/test_input_queue: tools/test_input_queue.c host/vmm/input_queue.h include/mime/abi.h
	@mkdir -p $(dir $@)
	$(CC) -std=c17 -O2 -Wall -Wextra -o $@ tools/test_input_queue.c

$(BUILD)/tools/test_fs: tools/test_fs.c host/vmm/fs.c host/vmm/vmm.h include/mime/abi.h host/util.c host/log.c
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -MMD -MP,$(HOST_CFLAGS)) -o $@ tools/test_fs.c host/util.c host/log.c

WIM_TEST_SRC := tools/test_wim.c host/setup/wim.c host/setup/lzx.c host/setup/lzms.c host/setup/xpress.c \
	host/util.c host/log.c host/sha1.c
$(BUILD)/tools/test_wim: $(WIM_TEST_SRC) host/setup/wim.h
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -MMD -MP,$(HOST_CFLAGS)) -o $@ $(WIM_TEST_SRC)

$(BUILD)/tools/test_registry_journal: tools/test_registry_journal.c guest/ntos/cm/journal_format.h
	@mkdir -p $(dir $@)
	$(CC) -std=c17 -O2 -Wall -Wextra -o $@ tools/test_registry_journal.c

$(BUILD)/tools/test_registry_journal_io: tools/test_registry_journal_io.c guest/ntos/cm/journal.c guest/ntos/cm/journal_format.h
	@mkdir -p $(dir $@)
	$(CC) -std=c17 -O2 -Wall -Wextra -o $@ tools/test_registry_journal_io.c

UI_TEST_SRC := tools/uitest.c host/ui/app_identity.c host/ui/pe_icon.c host/util.c host/log.c host/paths.c host/sha1.c
$(BUILD)/tools/uitest: $(UI_TEST_SRC) host/ui/cocoa.c host/ui/ui.h host/ui/app_identity.h include/mime/abi.h
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) -o $@ $(UI_TEST_SRC) $(HOST_LIBS)

$(BUILD)/tools/test_trackpad: tools/test_trackpad.c $(filter-out tools/uitest.c,$(UI_TEST_SRC)) \
	host/ui/cocoa.c host/ui/ui.h host/ui/app_identity.h host/ui/pe_icon.h host/mime_host.h include/mime/abi.h
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -MMD -MP,$(HOST_CFLAGS)) -o $@ tools/test_trackpad.c \
		$(filter-out tools/uitest.c,$(UI_TEST_SRC)) $(HOST_LIBS)

APP_IDENTITY_TEST_SRC := tools/test_app_identity.c host/ui/app_identity.c host/ui/pe_icon.c \
	host/util.c host/paths.c host/log.c host/sha1.c
$(BUILD)/tools/test_app_identity: $(APP_IDENTITY_TEST_SRC) host/ui/app_identity.h host/ui/pe_icon.h host/mime_host.h
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -MMD -MP,$(HOST_CFLAGS)) -o $@ $(APP_IDENTITY_TEST_SRC) \
		-framework AppKit -framework CoreFoundation -framework CoreGraphics -framework ImageIO -lobjc

$(BUILD)/tools/%: tools/%.c
	@mkdir -p $(dir $@)
	$(CC) -std=gnu17 -O2 -Wall -Wextra -o $@ $<

# ---------------------------------------------------------------- generated sources

SYSCALLS_NT := tools/syscalls/ntdll_26300_arm64.txt
SYSCALLS_WIN32K := tools/syscalls/win32u_26300_arm64.txt tools/syscalls/win32u_mime.txt

$(GEN)/mime_svc_nt.h: $(SYSCALLS_NT) $(BUILD)/tools/gensyscalls
	@mkdir -p $(GEN)
	$(BUILD)/tools/gensyscalls $(SYSCALLS_NT) MIME_NT_SERVICES $@ $(GEN)/ntdll_syscalls.S $(GEN)/ntdll_syscalls.def \
		$(GEN)/ntdll_syscalls_x64.S

$(GEN)/ntdll_syscalls.S $(GEN)/ntdll_syscalls.def $(GEN)/ntdll_syscalls_x64.S: $(GEN)/mime_svc_nt.h ;

# the real win32u table plus the private mime services numbered above it.
$(GEN)/mime_svc_win32k.h: $(SYSCALLS_WIN32K) $(BUILD)/tools/gensyscalls
	@mkdir -p $(GEN)
	cat $(SYSCALLS_WIN32K) > $(GEN)/win32u_syscalls.txt
	$(BUILD)/tools/gensyscalls $(GEN)/win32u_syscalls.txt MIME_WIN32K_SERVICES $@ $(GEN)/win32u_syscalls.S $(GEN)/win32u_syscalls.def \
		$(GEN)/win32u_syscalls_x64.S

$(GEN)/win32u_syscalls.S $(GEN)/win32u_syscalls.def $(GEN)/win32u_syscalls_x64.S: $(GEN)/mime_svc_win32k.h ;

GEN_HEADERS := $(GEN)/mime_svc_nt.h $(GEN)/mime_svc_win32k.h

# ---------------------------------------------------------------- guest runtime

RTL_SRC := $(wildcard guest/rtl/*.c)
CRT_SRC := $(wildcard guest/crt/*.c)
ifeq ($(GUEST_ARCH),x64)
CRT_ASM := $(wildcard guest/crt/x64/*.S)
else
CRT_ASM := $(wildcard guest/crt/*.S)
endif

$(OBJ)/guest/crt/%.o: guest/crt/%.c
	@mkdir -p $(dir $@)
	$(CC) $(GUEST_CFLAGS) -fno-builtin -c $< -o $@

$(OBJ)/guest/%.o: guest/%.S
	@mkdir -p $(dir $@)
	$(CC) $(GUEST_ASFLAGS) -c $< -o $@

# ---------------------------------------------------------------- ntoskrnl.exe

NTOS_SRC := $(shell find guest/ntos -name '*.c' -not -path 'guest/ntos/arch/*') $(wildcard guest/ntos/arch/$(GUEST_ARCH)/*.c)
NTOS_ASM := $(wildcard guest/ntos/arch/$(GUEST_ARCH)/*.S)
NTOS_OBJ := $(NTOS_SRC:%.c=$(OBJ)/%.o) $(NTOS_ASM:%.S=$(OBJ)/%.o) \
	$(RTL_SRC:%.c=$(OBJ)/ntos/%.o) $(CRT_SRC:%.c=$(OBJ)/%.o) $(CRT_ASM:%.S=$(OBJ)/%.o) \
	$(OBJ)/gen/ntos_services.o
NTOS_CFLAGS := $(GUEST_CFLAGS) -DMIME_KERNEL=1 -Iguest/ntos

# exported: public kernel routines, the crt and the object types drivers use.
NTOS_EXPORT_CODE := ^(Nt|Zw|Ke|Ex|Io|Ob|Ps|Mm|Rtl|Hal|Dbg|vDbg|Se|Po|Kd|Fs|Cc|Wmi|Mime)[A-Z]|^(Obf|Iof|Kef|Kf|Exf)[A-Z]|^(_?str|_?wcs|mem|_vsn|_sn|sprintf|swprintf|to|tow|__chkstk)
# exports private to mime's own drivers: by ordinal only, so no Mime name shows in the export table.
NTOS_PRIVATE := ^(Mime|HalMime)
NTOS_EXPORT_DATA := ^(Ex|Io|Ps|Mm|Se|Ob)[A-Za-z]*(Type|TypeObject)$$|^SharedUserData$$|^HalBootInfo$$

$(OBJ)/guest/ntos/%.o: guest/ntos/%.c $(GEN_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(NTOS_CFLAGS) -c $< -o $@

$(OBJ)/ntos/guest/rtl/%.o: guest/rtl/%.c
	@mkdir -p $(dir $@)
	$(CC) $(NTOS_CFLAGS) -c $< -o $@

$(GEN)/ntos_services.c: $(NTOS_SRC) $(BUILD)/tools/genservices
	@mkdir -p $(GEN)
	$(BUILD)/tools/genservices $@ $(NTOS_SRC)

$(OBJ)/gen/ntos_services.o: $(GEN)/ntos_services.c $(GEN_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(NTOS_CFLAGS) -Wno-unused-function -c $< -o $@

$(GEN)/ntoskrnl.def: $(filter-out $(OBJ)/gen/ntos_services.o,$(NTOS_OBJ))
	@mkdir -p $(GEN)
	{ echo "LIBRARY ntoskrnl.exe"; echo "EXPORTS"; \
	  $(NM) -g --defined-only $^ 2>/dev/null | \
	  awk '$$2 == "T" && $$3 ~ /$(NTOS_EXPORT_CODE)/ { print "    " $$3 } \
	       $$2 ~ /^[DBR]$$/ && $$3 ~ /$(NTOS_EXPORT_DATA)/ { print "    " $$3 " DATA" }' | sort -u | \
	  awk '$$1 ~ /$(NTOS_PRIVATE)/ { print $$0 " @" ++n " NONAME"; next } { print }'; } > $@

$(SYSTEM32)/ntoskrnl.exe: $(NTOS_OBJ) $(GEN)/ntoskrnl.def $(GEN)/res/ntoskrnl.exe.res
	@mkdir -p $(dir $@) $(BUILD)/lib
	$(LLD_LINK) $(GUEST_LDFLAGS) /subsystem:native,10.0 /driver /entry:KiSystemStartup /base:0xFFFFF80000200000 \
		/stack:0x80000,0x2000 /def:$(GEN)/ntoskrnl.def /implib:$(BUILD)/lib/ntoskrnl.lib /map:$(BUILD)/guest/ntoskrnl.map \
		/out:$@ $(NTOS_OBJ) $(GEN)/res/ntoskrnl.exe.res

# ---------------------------------------------------------------- win32k.sys

WIN32K_SRC := $(shell find guest/win32k -name '*.c') $(wildcard guest/lib/ttf/*.c guest/lib/png/*.c guest/lib/msstyles/*.c)
WIN32K_OBJ := $(WIN32K_SRC:%.c=$(OBJ)/win32k/%.o) $(OBJ)/gen/win32k_services.o
WIN32K_CFLAGS := $(GUEST_CFLAGS) -Iguest/win32k -Iguest/lib -Ithird_party/khronos

$(OBJ)/win32k/%.o: %.c $(GEN_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(WIN32K_CFLAGS) -c $< -o $@

$(GEN)/win32k_services.c: $(shell find guest/win32k -name '*.c') $(BUILD)/tools/genservices
	@mkdir -p $(GEN)
	$(BUILD)/tools/genservices --win32k $@ $(shell find guest/win32k -name '*.c')

$(OBJ)/gen/win32k_services.o: $(GEN)/win32k_services.c $(GEN_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(WIN32K_CFLAGS) -c $< -o $@

$(SYSTEM32)/win32k.sys: $(WIN32K_OBJ) $(BUILD)/lib/ntoskrnl.lib $(GEN)/res/win32k.sys.res
	@mkdir -p $(dir $@)
	$(LLD_LINK) $(GUEST_LDFLAGS) /subsystem:native,10.0 /driver /entry:DriverEntry /base:0xFFFFF80040000000 \
		/stack:0x40000,0x1000 /map:$(BUILD)/guest/win32k.map /out:$@ $(WIN32K_OBJ) $(BUILD)/lib/ntoskrnl.lib \
		$(GEN)/res/win32k.sys.res

$(BUILD)/lib/ntoskrnl.lib: $(SYSTEM32)/ntoskrnl.exe

# ---------------------------------------------------------------- user mode

USER_CFLAGS := $(GUEST_CFLAGS)
USER_DLL_LDFLAGS := $(GUEST_LDFLAGS) /dll /subsystem:console,10.0 /highentropyva /dynamicbase /stack:0x40000,0x1000

$(OBJ)/user/%.o: %.c $(GEN_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) $(USER_CFLAGS_$(firstword $(subst /, ,$(patsubst guest/%,%,$<)))) -c $< -o $@

$(OBJ)/user/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(GUEST_ASFLAGS) -c $< -o $@

$(OBJ)/user/guest/crt/%.o: guest/crt/%.c
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -fno-builtin -c $< -o $@

USER_RT_OBJ := $(RTL_SRC:%.c=$(OBJ)/user/%.o) $(CRT_SRC:%.c=$(OBJ)/user/%.o) $(CRT_ASM:%.S=$(OBJ)/user/%.o)

# prints the exported code symbols of objects $(1) matching the awk regex $(2).
EXPORTS_FROM_OBJECTS = $(NM) -g --defined-only $(1) 2>/dev/null | awk '$$2 == "T" && $$3 ~ /$(2)/ { print "    " $$3 }' | sort -u

# ---------------------------------------------------------------- ntdll.dll

NTDLL_SRC := $(wildcard guest/ntdll/*.c) $(wildcard guest/ntdll/*.S)
NTDLL_OBJ := $(patsubst %,$(OBJ)/user/%.o,$(basename $(NTDLL_SRC))) $(OBJ)/user/$(GEN)/ntdll_syscalls.o $(USER_RT_OBJ)
USER_CFLAGS_ntdll := -Iguest/ntdll
NTDLL_EXPORT_CODE := ^(Rtl|Ldr|Dbg|vDbg|Etw|Tp|Nls|Csr|Wer)[A-Z]|^KiUser(Exception|Apc|Callback)Dispatcher$$|^LdrInitializeThunk$$|^VerSetConditionMask$$|^(_?str|_?wcs|mem|_vsn|_sn|sprintf|swprintf|to|tow|__chkstk|_i64tow|_itow|_ultow|_wtoi|atoi|_wcs|_str)

$(GEN)/ntdll.def: $(NTDLL_OBJ) $(GEN)/ntdll_syscalls.def
	{ echo "LIBRARY ntdll.dll"; echo "EXPORTS"; cat $(GEN)/ntdll_syscalls.def; \
	  $(call EXPORTS_FROM_OBJECTS,$(NTDLL_OBJ),$(NTDLL_EXPORT_CODE)); } > $@

$(SYSTEM32)/ntdll.dll: $(NTDLL_OBJ) $(GEN)/ntdll.def $(GEN)/res/ntdll.dll.res
	@mkdir -p $(dir $@) $(BUILD)/lib
	$(LLD_LINK) $(USER_DLL_LDFLAGS) /noentry /base:0x7FFC00000000 /def:$(GEN)/ntdll.def \
		/implib:$(BUILD)/lib/ntdll.lib /out:$@ $(NTDLL_OBJ) $(GEN)/res/ntdll.dll.res

# ---------------------------------------------------------------- mimex86.dll

# the system dll of x64 processes: the x86 core and its native side, without imports.
MIMEX86_SRC := $(wildcard guest/mimex86/*.c) $(wildcard guest/mimex86/*.S)
MIMEX86_OBJ := $(patsubst %,$(OBJ)/user/%.o,$(basename $(MIMEX86_SRC))) $(OBJ)/user/$(GEN)/ntdll_syscalls.o $(USER_RT_OBJ)
USER_CFLAGS_mimex86 := -Iguest/mimex86 -fno-math-errno

$(OBJ)/user/guest/mimex86/entry.o: guest/mimex86/entry.S $(GEN_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(GUEST_ASFLAGS) -I$(GEN) -c $< -o $@

$(SYSTEM32)/mimex86.dll: $(MIMEX86_OBJ) $(GEN)/res/mimex86.dll.res
	@mkdir -p $(dir $@)
	$(LLD_LINK) $(USER_DLL_LDFLAGS) /noentry /base:0x7FFD00000000 /export:RtlUserThreadStart=MxUserThreadStart /out:$@ $(MIMEX86_OBJ) $(GEN)/res/mimex86.dll.res

# ---------------------------------------------------------------- win32u.dll

WIN32U_OBJ := $(OBJ)/user/$(GEN)/win32u_syscalls.o

$(GEN)/win32u.def: $(GEN)/win32u_syscalls.def
	{ echo "LIBRARY win32u.dll"; echo "EXPORTS"; cat $<; } > $@

$(SYSTEM32)/win32u.dll: $(WIN32U_OBJ) $(GEN)/win32u.def $(GEN)/res/win32u.dll.res
	@mkdir -p $(dir $@) $(BUILD)/lib
	$(LLD_LINK) $(USER_DLL_LDFLAGS) /subsystem:windows,10.0 /noentry /base:0x7FF700000000 /def:$(GEN)/win32u.def \
		/implib:$(BUILD)/lib/win32u.lib /out:$@ $(WIN32U_OBJ) $(GEN)/res/win32u.dll.res

$(BUILD)/lib/win32u.lib: $(SYSTEM32)/win32u.dll

# ---------------------------------------------------------------- dlls exporting with dllexport

# $(1) = name, $(2) = preferred base, $(3) = import libraries, $(4) = entry point flag.
define USER_DLL
$(1)_SRC := $$(wildcard guest/$(1)/*.c guest/$(1)/*.S)
$(1)_OBJ := $$(patsubst %,$(OBJ)/user/%.o,$$(basename $$($(1)_SRC))) $(OBJ)/user/guest/crt/chkstk.o $$(EXTRA_OBJ_$(1))
USER_CFLAGS_$(1) := -Iguest/$(1) -DMIME_BUILD_$(shell echo $(1) | tr a-z A-Z)
$(SYSTEM32)/$(1).dll: $$($(1)_OBJ) $$(patsubst %,$(BUILD)/lib/%.lib,$(3)) $$(EXTRA_DEPS_$(1)) $(GEN)/res/$(1).dll.res
	@mkdir -p $$(dir $$@) $(BUILD)/lib
	$(LLD_LINK) $(USER_DLL_LDFLAGS) $(4) /base:$(2) /implib:$(BUILD)/lib/$(1).lib $$(EXTRA_LDFLAGS_$(1)) \
		/out:$$@ $$($(1)_OBJ) $$(patsubst %,$(BUILD)/lib/%.lib,$(3)) $(GEN)/res/$(1).dll.res
$(BUILD)/lib/$(1).lib: $(SYSTEM32)/$(1).dll
endef

$(BUILD)/lib/ntdll.lib: $(SYSTEM32)/ntdll.dll

$(eval $(call USER_DLL,kernelbase,0x7FFB00000000,ntdll,/entry:DllMain))

# kernel32 re-exports kernelbase through forwarders, like windows does, and a few ntdll functions.
KERNEL32_NTDLL := RtlPcToFileHeader VerSetConditionMask
$(GEN)/kernel32.def: $(SYSTEM32)/kernelbase.dll
	{ echo "LIBRARY kernel32.dll"; echo "EXPORTS"; \
	  llvm-readobj --coff-exports $< | awk '$$1 == "Name:" && $$2 != "DllMain" { print "    " $$2 "=kernelbase." $$2 }'; \
	  for f in $(KERNEL32_NTDLL); do echo "    $$f=ntdll.$$f"; done; } > $@
EXTRA_DEPS_kernel32 := $(GEN)/kernel32.def
EXTRA_LDFLAGS_kernel32 := /def:$(GEN)/kernel32.def
$(eval $(call USER_DLL,kernel32,0x7FFA00000000,ntdll kernelbase,/entry:DllMain))

$(eval $(call USER_DLL,gdi32,0x7FF800000000,ntdll kernel32 win32u,/entry:DllMain))

# user32 parses the visual style and decodes png icons itself.
USER_CFLAGS_lib := -Iguest/lib
GUEST_LIB_UI_OBJ := $(patsubst %.c,$(OBJ)/user/%.o,$(wildcard guest/lib/png/*.c guest/lib/msstyles/*.c))
EXTRA_OBJ_user32 := $(GUEST_LIB_UI_OBJ)
EXTRA_LDFLAGS_user32 := /subsystem:windows,10.0
$(eval $(call USER_DLL,user32,0x7FF900000000,ntdll kernel32 win32u gdi32,/entry:DllMain))
USER_CFLAGS_user32 += -Iguest/lib

# ---------------------------------------------------------------- x64 system dlls

# the same sources as the arm64 dlls, with x64 assembly where the processor matters.
X64_TARGET := --target=x86_64-pc-windows-msvc -march=x86-64 -mcx16
# -ffreestanding drops the x64 unwind tables, which exception unwinding walks through every dll.
X64_CFLAGS := $(filter-out $(GUEST_TARGET) --target=% -march=%,$(GUEST_CFLAGS)) $(X64_TARGET) -fasynchronous-unwind-tables
# on an x64 machine these are the system's own dlls, run natively, not by the translator.
ifeq ($(GUEST_ARCH),x64)
X64_CFLAGS += -DMIME_X64_NATIVE=1
endif
X64_ASFLAGS := $(X64_TARGET) -Iinclude -Iguest/include -MMD -MP
X64_LDFLAGS := $(subst /machine:arm64,/machine:x64,$(GUEST_LDFLAGS))
X64_DLL_LDFLAGS := $(X64_LDFLAGS) /dll /subsystem:console,10.0 /highentropyva /dynamicbase /stack:0x40000,0x1000
X64_OBJ := $(OBJ)/x64

$(X64_OBJ)/%.o: %.c $(GEN_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(X64_CFLAGS) $(USER_CFLAGS_$(firstword $(subst /, ,$(patsubst guest/%,%,$<)))) -c $< -o $@

$(X64_OBJ)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(X64_ASFLAGS) -c $< -o $@

$(X64_OBJ)/guest/crt/%.o: guest/crt/%.c
	@mkdir -p $(dir $@)
	$(CC) $(X64_CFLAGS) -fno-builtin -c $< -o $@

X64_RT_OBJ := $(RTL_SRC:%.c=$(X64_OBJ)/%.o) $(CRT_SRC:%.c=$(X64_OBJ)/%.o) $(X64_OBJ)/guest/crt/x64/chkstk.o

X64_NTDLL_SRC := $(wildcard guest/ntdll/*.c) $(wildcard guest/ntdll/x64/*.c) $(wildcard guest/ntdll/x64/*.S)
X64_NTDLL_OBJ := $(patsubst %,$(X64_OBJ)/%.o,$(basename $(X64_NTDLL_SRC))) $(X64_OBJ)/$(GEN)/ntdll_syscalls_x64.o $(X64_RT_OBJ)
X64_NTDLL_EXPORT_CODE := $(NTDLL_EXPORT_CODE)|^__C_specific_handler$$

$(GEN)/x64/ntdll.def: $(X64_NTDLL_OBJ) $(GEN)/ntdll_syscalls.def
	@mkdir -p $(dir $@)
	{ echo "LIBRARY ntdll.dll"; echo "EXPORTS"; cat $(GEN)/ntdll_syscalls.def; \
	  $(call EXPORTS_FROM_OBJECTS,$(X64_NTDLL_OBJ),$(X64_NTDLL_EXPORT_CODE)); } > $@

$(SYSX64)/ntdll.dll: $(X64_NTDLL_OBJ) $(GEN)/x64/ntdll.def $(GEN)/res/ntdll.dll.res
	@mkdir -p $(dir $@) $(BUILD)/lib/x64
	$(LLD_LINK) $(X64_DLL_LDFLAGS) /noentry /base:0x7FFC00000000 /def:$(GEN)/x64/ntdll.def \
		/implib:$(BUILD)/lib/x64/ntdll.lib /out:$@ $(X64_NTDLL_OBJ) $(GEN)/res/ntdll.dll.res
$(BUILD)/lib/x64/ntdll.lib: $(SYSX64)/ntdll.dll

X64_WIN32U_OBJ := $(X64_OBJ)/$(GEN)/win32u_syscalls_x64.o
$(SYSX64)/win32u.dll: $(X64_WIN32U_OBJ) $(GEN)/win32u.def $(GEN)/res/win32u.dll.res
	@mkdir -p $(dir $@) $(BUILD)/lib/x64
	$(LLD_LINK) $(X64_DLL_LDFLAGS) /subsystem:windows,10.0 /noentry /base:0x7FF700000000 /def:$(GEN)/win32u.def \
		/implib:$(BUILD)/lib/x64/win32u.lib /out:$@ $(X64_WIN32U_OBJ) $(GEN)/res/win32u.dll.res
$(BUILD)/lib/x64/win32u.lib: $(SYSX64)/win32u.dll

# $(1) = name, $(2) = preferred base, $(3) = import libraries, $(4) = entry point flag.
define X64_DLL
x64_$(1)_SRC := $$(filter-out %.S,$$(wildcard guest/$(1)/*.c)) $$(wildcard guest/$(1)/x64/*.S)
x64_$(1)_OBJ := $$(patsubst %,$(X64_OBJ)/%.o,$$(basename $$(x64_$(1)_SRC))) $(X64_OBJ)/guest/crt/x64/chkstk.o $$(X64_EXTRA_OBJ_$(1))
$(SYSX64)/$(1).dll: $$(x64_$(1)_OBJ) $$(patsubst %,$(BUILD)/lib/x64/%.lib,$(3)) $$(X64_EXTRA_DEPS_$(1)) $(GEN)/res/$(1).dll.res
	@mkdir -p $$(dir $$@) $(BUILD)/lib/x64
	$(LLD_LINK) $(X64_DLL_LDFLAGS) $(4) /base:$(2) /implib:$(BUILD)/lib/x64/$(1).lib $$(X64_EXTRA_LDFLAGS_$(1)) \
		/out:$$@ $$(x64_$(1)_OBJ) $$(patsubst %,$(BUILD)/lib/x64/%.lib,$(3)) $(GEN)/res/$(1).dll.res
$(BUILD)/lib/x64/$(1).lib: $(SYSX64)/$(1).dll
endef

$(eval $(call X64_DLL,kernelbase,0x7FFB00000000,ntdll,/entry:DllMain))

# like the arm64 one, plus the unwinding functions windows' kernel32 forwards to ntdll.
X64_KERNEL32_NTDLL := RtlCaptureContext RtlLookupFunctionEntry RtlUnwindEx RtlVirtualUnwind RtlAddFunctionTable \
	RtlDeleteFunctionTable RtlInstallFunctionTableCallback RtlRestoreContext RtlUnwind __C_specific_handler \
	RtlCaptureStackBackTrace $(KERNEL32_NTDLL)
$(GEN)/x64/kernel32.def: $(SYSX64)/kernelbase.dll
	@mkdir -p $(dir $@)
	{ echo "LIBRARY kernel32.dll"; echo "EXPORTS"; \
	  llvm-readobj --coff-exports $< | awk '$$1 == "Name:" && $$2 != "DllMain" { print "    " $$2 "=kernelbase." $$2 }'; \
	  for f in $(X64_KERNEL32_NTDLL); do echo "    $$f=ntdll.$$f"; done; } > $@
X64_EXTRA_DEPS_kernel32 := $(GEN)/x64/kernel32.def
X64_EXTRA_LDFLAGS_kernel32 := /def:$(GEN)/x64/kernel32.def
$(eval $(call X64_DLL,kernel32,0x7FFA00000000,ntdll kernelbase,/entry:DllMain))

$(eval $(call X64_DLL,gdi32,0x7FF800000000,ntdll kernel32 win32u,/entry:DllMain))

# wow64, 32-bit programs on x64: wow64.dll and wow64win.dll share the conversions of the translator, and
# wow64cpu.dll sits below 4 GiB, where 32-bit code can jump back to it.
ifeq ($(GUEST_ARCH),x64)
USER_CFLAGS_wow64 := -Iguest/mimex86
USER_CFLAGS_wow64win := -Iguest/mimex86
X64_EXTRA_OBJ_wow64 := $(X64_OBJ)/guest/mimex86/wow32.o
X64_EXTRA_OBJ_wow64win := $(X64_OBJ)/guest/mimex86/wow32_win32k.o $(X64_OBJ)/guest/mimex86/wow32_callback.o
$(X64_OBJ)/guest/mimex86/wow32_win32k.o: $(GEN)/x86/win32k_calls.h
$(X64_OBJ)/guest/wow64win/x64/svc.o: guest/wow64win/x64/svc.S $(GEN_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(X64_ASFLAGS) -I$(GEN) -c $< -o $@
$(eval $(call X64_DLL,wow64,0x7FF420000000,ntdll,/entry:DllMain))
$(eval $(call X64_DLL,wow64win,0x7FF410000000,ntdll wow64,/entry:DllMain))
$(eval $(call X64_DLL,wow64cpu,0x78000000,ntdll wow64,/entry:DllMain))
endif

X64_EXTRA_OBJ_user32 := $(patsubst %.c,$(X64_OBJ)/%.o,$(wildcard guest/lib/png/*.c guest/lib/msstyles/*.c))
X64_EXTRA_LDFLAGS_user32 := /subsystem:windows,10.0
$(eval $(call X64_DLL,user32,0x7FF900000000,ntdll kernel32 win32u gdi32,/entry:DllMain))

# ---------------------------------------------------------------- the other dlls, both processors

# $(1) = name, $(2) = preferred base, $(3) = file name.
EXTRA_OBJ_wintrust := $(BUILD)/lib/crypt32.lib
X64_EXTRA_OBJ_wintrust := $(BUILD)/lib/x64/crypt32.lib

EXTRA_OBJ_msi := $(OBJ)/user/guest/lib/compound/compound.o

EXTRA_OBJ_gdiplus := $(patsubst %.c,$(OBJ)/user/%.o,$(wildcard guest/lib/png/*.c))
X64_EXTRA_OBJ_gdiplus := $(patsubst %.c,$(X64_OBJ)/%.o,$(wildcard guest/lib/png/*.c))
X64_EXTRA_OBJ_msi := $(X64_OBJ)/guest/lib/compound/compound.o

define EXTRA_DLL
USER_CFLAGS_$(1) := -Iguest/$(1)
$(1)_DEF := $$(wildcard guest/$(1)/$(1).def)
$(1)_SRC := $$(wildcard guest/$(1)/*.c)
$(1)_OBJ := $$(patsubst %.c,$(OBJ)/user/%.o,$$($(1)_SRC)) $$(patsubst %.S,$(OBJ)/user/%.o,$$(wildcard guest/$(1)/arm64/*.S)) \
	$(OBJ)/user/guest/crt/chkstk.o $$(EXTRA_OBJ_$(1))
x64_$(1)_OBJ := $$(patsubst %.c,$(X64_OBJ)/%.o,$$($(1)_SRC)) $$(patsubst %.S,$(X64_OBJ)/%.o,$$(wildcard guest/$(1)/x64/*.S)) \
	$(X64_OBJ)/guest/crt/x64/chkstk.o $$(X64_EXTRA_OBJ_$(1))
$(SYSTEM32)/$(3): $$($(1)_OBJ) $$($(1)_DEF) $(patsubst %,$(BUILD)/lib/%.lib,ntdll kernel32 user32 gdi32 win32u) $(GEN)/res/$(3).res
	@mkdir -p $$(dir $$@) $(BUILD)/lib
	$(LLD_LINK) $(USER_DLL_LDFLAGS) /entry:DllMain /base:$(2) /implib:$(BUILD)/lib/$(1).lib \
		$$(if $$($(1)_DEF),/def:$$($(1)_DEF)) /out:$$@ $$($(1)_OBJ) \
		$(patsubst %,$(BUILD)/lib/%.lib,ntdll kernel32 user32 gdi32 win32u) $(GEN)/res/$(3).res
$(SYSX64)/$(3): $$(x64_$(1)_OBJ) $$($(1)_DEF) $(patsubst %,$(BUILD)/lib/x64/%.lib,ntdll kernel32 user32 gdi32 win32u) $(GEN)/res/$(3).res
	@mkdir -p $$(dir $$@) $(BUILD)/lib/x64
	$(LLD_LINK) $(X64_DLL_LDFLAGS) /entry:DllMain /base:$(2) /implib:$(BUILD)/lib/x64/$(1).lib \
		$$(if $$($(1)_DEF),/def:$$($(1)_DEF)) /out:$$@ $$(x64_$(1)_OBJ) \
		$(patsubst %,$(BUILD)/lib/x64/%.lib,ntdll kernel32 user32 gdi32 win32u) $(GEN)/res/$(3).res
$(BUILD)/lib/$(1).lib: $(SYSTEM32)/$(3)
$(BUILD)/lib/x64/$(1).lib: $(SYSX64)/$(3)
endef

# dxgi and direct3d reach vulkan-1.dll with LoadLibrary, and share the private data of their objects.
DX_LIB_SRC := guest/lib/privdata/privdata.c
DXBC_SRC := $(wildcard guest/lib/dxbc/*.c)
EXTRA_OBJ_dxgi := $(patsubst %.c,$(OBJ)/user/%.o,$(DX_LIB_SRC))
X64_EXTRA_OBJ_dxgi := $(patsubst %.c,$(X64_OBJ)/%.o,$(DX_LIB_SRC))
EXTRA_OBJ_d3d11 := $(patsubst %.c,$(OBJ)/user/%.o,$(DX_LIB_SRC) $(DXBC_SRC))
X64_EXTRA_OBJ_d3d11 := $(patsubst %.c,$(X64_OBJ)/%.o,$(DX_LIB_SRC) $(DXBC_SRC))

$(foreach d,$(EXTRA_DLLS),$(eval $(call EXTRA_DLL,$(firstword $(subst :, ,$(d))),$(lastword $(subst :, ,$(d))),$(call extra_file,$(firstword $(subst :, ,$(d)))))))

USER_CFLAGS_dxgi += -Ithird_party/khronos
USER_CFLAGS_d3d11 += -Ithird_party/khronos
USER_CFLAGS_lib += -Ithird_party/SPIRV-Cross

sysx64: $(X64_FILES)

# ---------------------------------------------------------------- opengl32.dll

# opengl on the host gpu: a native dll without imports, so the translator can map it into x64 processes
# too, where an x64 opengl32.dll of stubs forwards each call to it (mime/mx64.h, tools/gengl.c).
OPENGL32_SRC := $(wildcard guest/opengl32/*.c)
OPENGL32_CFLAGS := -Iguest/opengl32 -Ithird_party/khronos -Wno-ignored-attributes -DNTSYSCALLAPI= -DW32KAPI=
USER_CFLAGS_opengl32 := $(OPENGL32_CFLAGS)
OPENGL32_OBJ := $(patsubst %.c,$(OBJ)/user/%.o,$(OPENGL32_SRC)) $(OBJ)/user/gen/opengl32/procs.o \
	$(OBJ)/user/gen/opengl32/x64_thunks.o $(OBJ)/user/$(GEN)/ntdll_syscalls.o $(OBJ)/user/$(GEN)/win32u_syscalls.o \
	$(USER_RT_OBJ)

$(GEN)/opengl32/stamp: $(OPENGL32_SRC) $(BUILD)/tools/gengl
	@mkdir -p $(dir $@)
	$(BUILD)/tools/gengl $(GEN)/opengl32 $(OPENGL32_SRC)
	touch $@

$(GEN)/opengl32/exports.def $(GEN)/opengl32/procs.c $(GEN)/opengl32/x64_thunks.c $(GEN)/opengl32/x64_stubs.S \
	$(GEN)/opengl32/x64.def: $(GEN)/opengl32/stamp ;

$(OBJ)/user/gen/opengl32/%.o: $(GEN)/opengl32/%.c $(GEN_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) $(OPENGL32_CFLAGS) -c $< -o $@

$(SYSTEM32)/opengl32.dll: $(OPENGL32_OBJ) $(GEN)/opengl32/exports.def $(GEN)/res/opengl32.dll.res
	@mkdir -p $(dir $@)
	$(LLD_LINK) $(USER_DLL_LDFLAGS) /noentry /base:0x7FF550000000 /def:$(GEN)/opengl32/exports.def /out:$@ \
		$(OPENGL32_OBJ) $(GEN)/res/opengl32.dll.res

X64_OPENGL32_OBJ := $(X64_OBJ)/guest/opengl32/x64/main.o $(X64_OBJ)/guest/opengl32/x64/native.o \
	$(X64_OBJ)/gen/opengl32/x64_stubs.o $(X64_OBJ)/guest/crt/x64/chkstk.o

$(X64_OBJ)/gen/opengl32/x64_stubs.o: $(GEN)/opengl32/x64_stubs.S
	@mkdir -p $(dir $@)
	$(CC) $(X64_ASFLAGS) -c $< -o $@

ifeq ($(GUEST_ARCH),x64)
# an x64 machine runs the whole implementation natively, like the arm64 one.
X64_OPENGL32_OBJ := $(patsubst %.c,$(X64_OBJ)/%.o,$(OPENGL32_SRC)) $(X64_OBJ)/gen/opengl32/procs.o \
	$(X64_OBJ)/$(GEN)/ntdll_syscalls_x64.o $(X64_OBJ)/$(GEN)/win32u_syscalls_x64.o $(X64_RT_OBJ)

$(X64_OBJ)/gen/opengl32/%.o: $(GEN)/opengl32/%.c $(GEN_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(X64_CFLAGS) $(OPENGL32_CFLAGS) -c $< -o $@

# without the table of thunks the translator calls.
$(GEN)/opengl32/native.def: $(GEN)/opengl32/exports.def
	grep -v MimeX64Thunks $< > $@

$(SYSX64)/opengl32.dll: $(X64_OPENGL32_OBJ) $(GEN)/opengl32/native.def $(GEN)/res/opengl32.dll.res
	@mkdir -p $(dir $@)
	$(LLD_LINK) $(X64_DLL_LDFLAGS) /noentry /base:0x7FF550000000 /def:$(GEN)/opengl32/native.def /out:$@ \
		$(X64_OPENGL32_OBJ) $(GEN)/res/opengl32.dll.res
else
$(SYSX64)/opengl32.dll: $(X64_OPENGL32_OBJ) $(GEN)/opengl32/x64.def $(patsubst %,$(BUILD)/lib/x64/%.lib,ntdll kernel32) \
	$(GEN)/res/opengl32.dll.res
	@mkdir -p $(dir $@)
	$(LLD_LINK) $(X64_DLL_LDFLAGS) /entry:DllMain /base:0x7FF540000000 /def:$(GEN)/opengl32/x64.def /out:$@ \
		$(X64_OPENGL32_OBJ) $(patsubst %,$(BUILD)/lib/x64/%.lib,ntdll kernel32) $(GEN)/res/opengl32.dll.res
endif

# ---------------------------------------------------------------- vulkan-1.dll

# the vulkan api over the wire to the host's driver, x64 for now.
VULKAN_SRC := $(wildcard guest/vulkan-1/*.c)
VULKAN_GEN_SRC := mvk_structs mvk_guest_commands mvk_guest_out mvk_guest_procs
VULKAN_CFLAGS := -Iguest/vulkan-1 $(VK_CFLAGS) -DMVK_GUEST=1 -Wno-unused-variable -Wno-unused-function \
	-Wno-unused-but-set-variable
X64_VULKAN_OBJ := $(patsubst %.c,$(X64_OBJ)/%.o,$(VULKAN_SRC)) $(patsubst %,$(X64_OBJ)/gen/vulkan/%.o,$(VULKAN_GEN_SRC)) \
	$(X64_OBJ)/guest/crt/x64/chkstk.o

$(X64_OBJ)/guest/vulkan-1/%.o: guest/vulkan-1/%.c $(VK_GEN)/stamp $(GEN_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(X64_CFLAGS) $(VULKAN_CFLAGS) -c $< -o $@

$(X64_OBJ)/gen/vulkan/%.o: $(VK_GEN)/%.c $(VK_GEN)/stamp
	@mkdir -p $(dir $@)
	$(CC) $(X64_CFLAGS) $(VULKAN_CFLAGS) -c $< -o $@

$(SYSX64)/vulkan-1.dll: $(X64_VULKAN_OBJ) $(VK_GEN)/stamp $(patsubst %,$(BUILD)/lib/x64/%.lib,ntdll kernel32 win32u user32) \
	$(GEN)/res/vulkan-1.dll.res
	@mkdir -p $(dir $@)
	$(LLD_LINK) $(X64_DLL_LDFLAGS) /entry:DllMain /base:0x7FF420000000 /def:$(VK_GEN)/mvk_exports.def \
		/implib:$(BUILD)/lib/x64/vulkan-1.lib /out:$@ $(X64_VULKAN_OBJ) \
		$(patsubst %,$(BUILD)/lib/x64/%.lib,ntdll kernel32 win32u user32) $(GEN)/res/vulkan-1.dll.res
$(BUILD)/lib/x64/vulkan-1.lib: $(SYSX64)/vulkan-1.dll

ifeq ($(GUEST_ARCH),x64)
X64_FILES += $(SYSX64)/vulkan-1.dll
GUEST_FILES += $(SYSX64)/vulkan-1.dll
ROM_FILES += $(BUILD)/guest/rom/vulkan-1.dll
$(GEN)/rom.S: $(BUILD)/guest/rom/vulkan-1.dll
guest: $(SYSX64)/vulkan-1.dll
endif

# ---------------------------------------------------------------- examples

# each examples/<name>.c becomes a windows program linked against the mime dlls, entry point `entry`.
EXAMPLE_LIBS := $(patsubst %,$(BUILD)/lib/%.lib,ntdll kernel32 user32 gdi32)
EXAMPLES := $(patsubst examples/%.c,$(BUILD)/examples/%.exe,$(wildcard examples/*.c))

$(BUILD)/examples/%.exe: $(OBJ)/user/examples/%.o $(EXAMPLE_LIBS)
	@mkdir -p $(dir $@)
	$(LLD_LINK) $(GUEST_LDFLAGS) /subsystem:windows /entry:entry /out:$@ $< $(EXAMPLE_LIBS)

# the same programs built for x64, run by the translator.
X64_EXAMPLE_LIBS := $(patsubst %,$(BUILD)/lib/x64/%.lib,ntdll kernel32 user32 gdi32)
X64_EXAMPLES := $(patsubst examples/%.c,$(BUILD)/examples/x64/%.exe,$(wildcard examples/*.c))

$(BUILD)/examples/x64/%.exe: $(X64_OBJ)/examples/%.o $(X64_EXAMPLE_LIBS)
	@mkdir -p $(dir $@)
	$(LLD_LINK) $(X64_LDFLAGS) /subsystem:windows /entry:entry /out:$@ $< $(X64_EXAMPLE_LIBS)

# ---------------------------------------------------------------- guest

guest: $(GUEST_FILES)

ifeq ($(GUEST_ARCH),x64)
examples: guest $(X64_EXAMPLES)
else
examples: guest $(EXAMPLES) $(X64_EXAMPLES)
endif

clean:
	rm -rf $(BUILD)

-include $(shell find $(BUILD)/obj -name '*.d' 2>/dev/null)

include tools/x86.mk

# 32-bit programs run through the translator on arm64 and natively through wow64 on x64.
X86_ROM_FILES = $(patsubst %,$(BUILD)/guest/rom/syswow64/%.dll,ntdll kernelbase kernel32 win32u gdi32 user32 shlwapi shell32 $(X86_MORE_DLLS)) \
	$(BUILD)/guest/rom/syswow64/winspool.drv
$(GEN)/rom.S: $(X86_ROM_FILES)
ROM_FILES += $(X86_ROM_FILES)

$(BUILD)/guest/rom/syswow64/%: $(BUILD)/guest/syswow64/% $(BUILD)/tools/pechecksum
	@mkdir -p $(dir $@)
	$(STRIP) --strip-debug $< -o $@
	$(BUILD)/tools/pechecksum $@

.PHONY: test-compound
test-compound: $(BUILD)/tools/test_compound
	$<

$(BUILD)/tools/test_compound: tools/test_compound.c guest/lib/compound/compound.c guest/lib/compound/compound.h
	@mkdir -p $(dir $@)
	$(CC) -std=c17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined tools/test_compound.c guest/lib/compound/compound.c -o $@

.PHONY: test-msi-build
test-msi-build: $(foreach arch,arm64 x64 x86,$(foreach name,msi msidatabase msiaction,$(BUILD)/tests/$(arch)/$(name).exe))

$(BUILD)/tests/arm64/msi.exe $(BUILD)/tests/arm64/msidatabase.exe $(BUILD)/tests/arm64/msiaction.exe: $(BUILD)/tests/arm64/%.exe: $(OBJ)/user/tools/runtime32/%.o $(BUILD)/lib/msi.lib $(BUILD)/lib/kernel32.lib $(BUILD)/lib/ntdll.lib
	@mkdir -p $(dir $@)
	$(LLD_LINK) $(GUEST_LDFLAGS) /subsystem:console /entry:entry /out:$@ $^

$(BUILD)/tests/x64/msi.exe $(BUILD)/tests/x64/msidatabase.exe $(BUILD)/tests/x64/msiaction.exe: $(BUILD)/tests/x64/%.exe: $(X64_OBJ)/tools/runtime32/%.o $(BUILD)/lib/x64/msi.lib $(BUILD)/lib/x64/kernel32.lib $(BUILD)/lib/x64/ntdll.lib
	@mkdir -p $(dir $@)
	$(LLD_LINK) $(X64_LDFLAGS) /subsystem:console /entry:entry /out:$@ $^

include tools/compat-tests.mk
