# Generate config.h.py with translated defines for easy use within Python

CONFIG_H_PY = config_h.py
PLUGINS =

if WITH_BTRFS
PLUGINS += btrfs
endif

if WITH_CRYPTO
PLUGINS += crypto
endif

if WITH_DM
PLUGINS += dm
endif

if WITH_LOOP
PLUGINS += loop
endif

if WITH_LVM
PLUGINS += lvm
endif

if WITH_LVM_DBUS
PLUGINS += lvm-dbus
endif

if WITH_MDRAID
PLUGINS += mdraid
endif

if WITH_MPATH
PLUGINS += mpath
endif

if WITH_SWAP
PLUGINS += swap
endif

if WITH_PART
PLUGINS += part
endif

if WITH_FS
PLUGINS += fs
endif

if WITH_NVDIMM
PLUGINS += nvdimm
endif

if WITH_NVME
PLUGINS += nvme
endif

if WITH_SMART
PLUGINS += smart
endif

if WITH_SMARTMONTOOLS
PLUGINS += smartmontools
endif

if WITH_TOOLS
PLUGINS += tools
endif

$(CONFIG_H_PY):
	echo -n 'ENABLED_PLUGINS = [ ' > $(CONFIG_H_PY)
	for i in $(PLUGINS); do \
		echo -n "'$$i', " >> $(CONFIG_H_PY); \
	done
	echo ']' >> $(CONFIG_H_PY)

all: $(CONFIG_H_PY)

EXTRA_DIST =

clean-local:
	rm -f *~ $(CONFIG_H_PY)
