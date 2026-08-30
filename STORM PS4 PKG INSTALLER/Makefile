# Package metadata
TITLE       := STORM PS4 PKG INSTALLER
VERSION     := 1.44
TITLE_ID    := SPPI01313
CONTENT_ID  := IV0000-SPPI01313_00-STORMPKGINSTALL0

# Libraries linked into the ELF
LIBS        := -lc -lkernel -lc++ -lSceSysmodule -lSceNet -lSceNetCtl -lSceVideoOut -lSceUserService -lSceBgft -lSceHttp -lSceSsl -lSceAppInstUtil -lScePosix -lScePad

# You likely won't need to touch anything below this point.

# Root vars
TOOLCHAIN   := $(OO_PS4_TOOLCHAIN)
PROJDIR     := storm
INTDIR      := STORM_PKG/x64/Debug

# Define objects to build - Back to minimal_main (stable, no pthread)
CPPFILES    := source/minimal_main.cpp source/Graphics.cpp source/Installer.cpp source/HttpHelper.cpp source/WebServer.cpp source/JsonHelper.cpp source/ThreadHelper.cpp source/libjbc.cpp source/PkgUtils.cpp
OBJS        := $(patsubst source/%.cpp, $(INTDIR)/%.o, $(CPPFILES))

# Define final C/C++ flags
CFLAGS      := --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -c -isysroot $(TOOLCHAIN) -isystem $(TOOLCHAIN)/include
CXXFLAGS    := $(CFLAGS) -isystem $(TOOLCHAIN)/include/c++/v1
LDFLAGS     := -m elf_x86_64 -pie --script $(TOOLCHAIN)/link.x --eh-frame-hdr -L$(TOOLCHAIN)/lib $(LIBS) $(TOOLCHAIN)/lib/crt1.o

# Create intermediate directory
_unused     := $(shell mkdir -p $(INTDIR) 2>nul)

# Windows detection
ifeq ($(OS),Windows_NT)
	CC      := clang
	CCX     := clang++
	LD      := ld.lld
	CDIR    := windows
endif

all: $(CONTENT_ID).pkg

$(CONTENT_ID).pkg: pkg.gp4
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core pkg_build $< .

pkg.gp4: eboot.bin sce_sys/param.sfo sce_sys/icon0.png sce_sys/pic0.png sce_sys/pic1.png sce_sys/keystone sce_sys/about/right.sprx sce_module/libc.prx sce_module/libjbc.prx sce_module/libSceFios2.prx assets/centurygothic.ttf assets/centurygothic_bold.ttf assets/bg.png
	$(TOOLCHAIN)/bin/$(CDIR)/create-gp4 -out $@ --content-id=$(CONTENT_ID) --files "$^"
	@echo "GP4 Created. Verify assets match target paths in pkg.gp4"

sce_sys/param.sfo: Makefile
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_new $@
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ APP_TYPE --type Integer --maxsize 4 --value 1
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ APP_VER --type Utf8 --maxsize 8 --value '$(VERSION)'
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ ATTRIBUTE --type Integer --maxsize 4 --value 1
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ CATEGORY --type Utf8 --maxsize 4 --value 'gd'
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ CONTENT_ID --type Utf8 --maxsize 48 --value '$(CONTENT_ID)'
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ SYSTEM_VER --type Integer --maxsize 4 --value 0
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ TITLE --type Utf8 --maxsize 128 --value '$(TITLE)'
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ TITLE_ID --type Utf8 --maxsize 12 --value '$(TITLE_ID)'
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ VERSION --type Utf8 --maxsize 8 --value '$(VERSION)'

eboot.bin: $(INTDIR) $(OBJS)
	$(LD) $(OBJS) -o $(INTDIR)/$(PROJDIR).elf $(LDFLAGS)
	$(TOOLCHAIN)/bin/$(CDIR)/create-fself -in=$(INTDIR)/$(PROJDIR).elf -out=$(INTDIR)/$(PROJDIR).oelf --eboot "eboot.bin" --paid 0x3800000000000011

$(INTDIR)/%.o: source/%.cpp | $(INTDIR)
	$(CCX) $(CXXFLAGS) -o $@ $<

$(INTDIR):
	@echo Creating directory: $(INTDIR)
	@powershell -Command "New-Item -ItemType Directory -Force -Path '$(INTDIR)'" > nul

clean:
	@echo Cleaning...
	@del /F /Q $(CONTENT_ID).pkg pkg.gp4 eboot.bin 2>NUL || echo ""
	@del /F /Q pkg\\sce_sys\\param.sfo 2>NUL || echo ""
	@del /F /Q $(INTDIR)\\*.o $(INTDIR)\\*.elf $(INTDIR)\\*.oelf 2>NUL || echo ""
