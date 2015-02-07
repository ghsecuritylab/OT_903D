

#ifndef _ASM_IA64_PARPORT_H
#define _ASM_IA64_PARPORT_H 1

static int __devinit parport_pc_find_isa_ports (int autoirq, int autodma);

static int __devinit
parport_pc_find_nonpci_ports (int autoirq, int autodma)
{
	return parport_pc_find_isa_ports(autoirq, autodma);
}

#endif /* _ASM_IA64_PARPORT_H */