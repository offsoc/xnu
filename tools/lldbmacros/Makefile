export MakeInc_cmd=${SRCROOT}/makedefs/MakeInc.cmd
export MakeInc_def=${SRCROOT}/makedefs/MakeInc.def
export MakeInc_rule=${SRCROOT}/makedefs/MakeInc.rule
export MakeInc_dir=${SRCROOT}/makedefs/MakeInc.dir

include $(MakeInc_cmd)
include $(MakeInc_def)

.PHONY: lldbmacros_install

do_config_all:: lldbmacros_install

LLDBMACROS_SOURCE:=$(SRCROOT)/tools/lldbmacros/
LLDBMACROS_BOOTSTRAP_DEST:=$(OBJPATH)/$(KERNEL_FILE_NAME).dSYM/$(DSYMLLDBMACROSDIR)
LLDBMACROS_DEST:=$(LLDBMACROS_BOOTSTRAP_DEST)/lldbmacros/
LLDBMACROS_USERDEBUG_FILES=
ifeq ($(BUILD_STATIC_LINK),1)
ifneq ($(BUILD_XNU_LIBRARY),1)
KERNEL_STATIC_DSYM_LLDBMACROS := $(OBJPATH)/$(KERNEL_FILE_NAME).link/$(KERNEL_FILE_NAME).dSYM/$(DSYMLLDBMACROSDIR)/lldbmacros/
endif
endif

LLDBMACROS_USERDEBUG_FILES:= \
	usertaskdebugging/__init__.py \
	usertaskdebugging/gdbserver.py \
	usertaskdebugging/interface.py \
	usertaskdebugging/rsprotocol.py \
	usertaskdebugging/target.py \
	usertaskdebugging/userprocess.py

LLDBMACROS_PYTHON_FILES = $(LLDBMACROS_USERDEBUG_FILES) \
	core/__init__.py \
	core/caching.py \
	core/iterators.py \
	core/configuration.py \
	core/cvalue.py \
	core/io.py \
	core/kernelcore.py \
	core/lazytarget.py \
	core/lldbwrap.py \
	core/operating_system.py \
	core/pointer.py \
	core/standard.py \
	core/xnu_lldb_init.py \
	kmemory/__init__.py \
	kmemory/btlog.py \
	kmemory/kmem.py \
	kmemory/vm.py \
	kmemory/whatis.py \
	kmemory/zone.py \
	plugins/__init__.py \
	plugins/zprint_perf_log.py \
	sysregdoc/AArch64-esr_el1.xml \
	bank.py \
	turnstile.py \
	kevent.py \
	workqueue.py \
	ulock.py \
	xnu.py \
	xnudefines.py \
	ktrace.py \
	macho.py \
	mbufdefines.py \
	netdefines.py \
	ntstat.py \
	routedefines.py \
	ipc.py \
	ipcimportancedetail.py \
	kcdata.py \
	scheduler.py \
	structanalyze.py \
	pmap.py \
	memory.py \
	memorystatus.py \
	mbufs.py \
	microstackshot.py \
	net.py \
	skywalk.py \
	ioreg.py \
	utils.py \
	kdp.py \
	process.py \
	userspace.py \
	pci.py \
	misc.py \
	apic.py \
	kasan.py \
	usertaskgdbserver.py \
	waitq.py \
	xnutriage.py \
	kmtriage.py \
	zonetriage.py \
	recount.py \
	sysreg.py \
	counter.py \
	refgrp.py \
	workload.py \
	log.py \
	kext.py \
	nvram.py \
	taskinfo.py \
	exclaves.py \
	vm_pageout.py

ifneq ($(PLATFORM),MacOSX)
	LLDBMACROS_PYTHON_FILES+= \
		plugins/iosspeedtracer.py \
		plugins/iosspeedtracer.sh
endif


# Adding plugin with macros to test all LLDB macros.
LLDBMACROS_PYTHON_FILES += \
	tests/integration_smoke/__init__.py \
	tests/integration_smoke/lldb_session.py \
	tests/integration_smoke/conftest.py \
	tests/integration_smoke/test_lldb_macros.py

include $(MakeInc_rule)
include $(MakeInc_dir)

# Strip macros during install (default)
ifeq ($(SKIP_LLDBMACROS_UNIFDEF),1)
	LLDBMACROS_UNIFDEF :=
else
	LLDBMACROS_UNIFDEF := $(DATA_UNIFDEF)
endif

INSTALL_LLDBMACROS_PYTHON_FILES=$(addprefix $(LLDBMACROS_DEST), $(LLDBMACROS_PYTHON_FILES))
$(eval $(call INSTALLPYTHON_RULE_template,$(INSTALL_LLDBMACROS_PYTHON_FILES),$(LLDBMACROS_SOURCE)%,pydir,$(LLDBMACROS_UNIFDEF),$(LLDBMACROS_DEST)))
$(eval $(call INSTALLPYTHON_RULE_template,$(LLDBMACROS_BOOTSTRAP_DEST)/$(KERNEL_LLDBBOOTSTRAP_NAME),$(LLDBMACROS_SOURCE)/core/xnu_lldb_init.py,kbpydir,$(LLDBMACROS_UNIFDEF),$(LLDBMACROS_BOOTSTRAP_DEST)/))

ifeq ($(BUILD_STATIC_LINK),1)
ifneq ($(BUILD_XNU_LIBRARY),1)
INSTALL_STATIC_DSYM_LLDBMACROS_PYTHON_FILES=$(addprefix $(KERNEL_STATIC_DSYM_LLDBMACROS), $(LLDBMACROS_PYTHON_FILES))
$(eval $(call INSTALLPYTHON_RULE_template,$(INSTALL_STATIC_DSYM_LLDBMACROS_PYTHON_FILES),$(LLDBMACROS_SOURCE)%,sdpydir,$(LLDBMACROS_UNIFDEF),$(KERNEL_STATIC_DSYM_LLDBMACROS)))
$(eval $(call INSTALLPYTHON_RULE_template,$(KERNEL_STATIC_DSYM_LLDBMACROS)/../$(KERNEL_LLDBBOOTSTRAP_NAME),$(LLDBMACROS_SOURCE)/core/xnu_lldb_init.py,kbsdpydir,$(LLDBMACROS_UNIFDEF),$(KERNEL_STATIC_DSYM_LLDBMACROS)/../))
endif
endif

ifeq ($(BUILD_STATIC_LINK),1)
ifneq ($(BUILD_XNU_LIBRARY),1)
STATIC_DSYM_LLDBMACROS_INSTALL_TARGETS := \
	$(INSTALL_STATIC_DSYM_LLDBMACROS_PYTHON_FILES) \
	$(KERNEL_STATIC_DSYM_LLDBMACROS)/../$(KERNEL_LLDBBOOTSTRAP_NAME)
endif
endif

lldbmacros_install: $(INSTALL_LLDBMACROS_PYTHON_FILES) $(LLDBMACROS_BOOTSTRAP_DEST)/$(KERNEL_LLDBBOOTSTRAP_NAME) $(STATIC_DSYM_LLDBMACROS_INSTALL_TARGETS)
	$(_v)$(MKDIR) $(LLDBMACROS_DEST)/builtinkexts
ifeq ($(BUILD_STATIC_LINK),1)
ifneq ($(BUILD_XNU_LIBRARY),1)
	$(_v)$(MKDIR) $(KERNEL_STATIC_DSYM_LLDBMACROS)/builtinkexts
endif
endif
