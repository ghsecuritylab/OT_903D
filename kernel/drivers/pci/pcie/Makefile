obj-$(CONFIG_PCIEASPM)		+= aspm.o

pcieportdrv-y			:= portdrv_core.o portdrv_pci.o portdrv_bus.o

obj-$(CONFIG_PCIEPORTBUS)	+= pcieportdrv.o

# Build PCI Express AER if needed
obj-$(CONFIG_PCIEAER)		+= aer/

obj-$(CONFIG_PCIE_PME) += pme/
