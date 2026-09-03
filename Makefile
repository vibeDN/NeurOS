# NeurOS build wrapper around Buildroot (out-of-tree, BR2_EXTERNAL = this repo).
#
#   make config        # load the x86_64 dev defconfig
#   make               # build (nice'd + ionice'd, capped parallelism)
#   make menuconfig     # tweak config
#   make savedefconfig  # write changes back to configs/neuros_x86_64_defconfig
#   make vm             # (re)create the VirtualBox VM from output/images/disk.img
#   make clean          # drop build/ and target/, keep toolchain + dl cache
#   make distclean      # nuke output/ entirely

BR      := $(CURDIR)/buildroot
O       := $(CURDIR)/output
DL      := $(HOME)/.cache/neuros/dl
EXT     := $(CURDIR)
JLEVEL  ?= 8

BRMAKE = $(MAKE) -C $(BR) O=$(O) BR2_EXTERNAL=$(EXT) BR2_DL_DIR=$(DL) BR2_JLEVEL=$(JLEVEL)

.PHONY: all config build menuconfig linux-menuconfig savedefconfig clean distclean vm run sdk

all: build

config:
	@mkdir -p $(DL)
	$(BRMAKE) neuros_x86_64_defconfig

build:
	@mkdir -p $(DL)
	nice -n 15 ionice -c3 $(BRMAKE)

menuconfig:
	$(BRMAKE) menuconfig

linux-menuconfig:
	$(BRMAKE) linux-menuconfig

savedefconfig:
	$(BRMAKE) savedefconfig

clean:
	$(BRMAKE) clean

# force-rebuild NeurOS's own (local) packages + reassemble the image. Buildroot's
# local-package change detection is unreliable while iterating.
comp:
	nice -n 15 ionice -c3 $(BRMAKE) neuros-comp-rebuild all

ours:
	nice -n 15 ionice -c3 $(BRMAKE) neuros-comp-rebuild neuros-agentd-rebuild all

distclean:
	rm -rf $(O)

vm:
	$(CURDIR)/scripts/make-vm.sh

run:
	sg vboxusers -c 'VBoxManage startvm NeurOS-dev --type gui'

run-headless:
	sg vboxusers -c 'VBoxManage startvm NeurOS-dev --type headless'

kill-vm:
	sg vboxusers -c 'VBoxManage controlvm NeurOS-dev poweroff' || true

progress:
	$(CURDIR)/scripts/build-progress.sh
