// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/pmap.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "Display stack backtrace", mon_backtrace },
	{ "showmappings", "Display Physical page mappings", mon_mappings},
	{ "setmappings", "set permissions for a gicen address space", set_mappings},
	{ "dump", "Dump contents given a virtual address space", dump},
};

/***** Implementations of basic kernel monitor commands *****/
int dump(int argc, char **argv, struct Trapframe *tf){
	if(argc < 3){
		cprintf("Usage: dump <addr> <no. of entries>\n");
		return 0;
	}

	uintptr_t start = strtol(argv[1], NULL, 16), end;
	size_t num = strtol(argv[2], NULL, 16);
	end = start+num;
	cprintf("start 0x%x end 0x%x\n", start, end);
	do{
		pte_t *pte = pgdir_walk(kern_pgdir, (void*)start, false);
		if(pte == NULL || !(*pte & PTE_P)){
			cprintf("va: 0x%x - 0x%x not mapped\n", start, ROUNDUP(start+1, PGSIZE));
			start = ROUNDUP(start+1, PGSIZE);
			continue;
		}
		uintptr_t* addr = (uintptr_t*)start;
		for(long stop = MIN(end, ROUNDUP(start+1, PGSIZE)); (long)addr < stop; addr++){
			cprintf("Value at 0x%x is 0x%x\n", (uintptr_t)addr, *addr);
		}
		start = (uintptr_t)addr;
	}while (start < end);

	return 0;
}

int set_mappings(int argc, char **argv, struct Trapframe *tf)
{
	if(argc < 4){
		cprintf("Usage: set_mappings <start> <end> <permissions> (start <= end)\n\
permissions will be applied to all pages within the range [start, end]\n");
		return 0;
	}
	long start, end;
	uint16_t perm;
	start = strtol(argv[1], NULL, 16);
	end = strtol(argv[2], NULL, 16);
	perm = strtol(argv[3], NULL, 16);
	perm &= 0xFFF;

	for(int i = start; i <= end - end%PGSIZE; i += PGSIZE){
		pte_t* pte  = pgdir_walk(kern_pgdir, (void*)i, false);
		if(pte == NULL)
			continue;
		*pte = ((*pte) & ~0xFFF) | perm;
		cprintf("va:0x%04x page addr:0x%x offset:0x%x User:%d Writable:%d Dirty:%d PSE:%d\n",
			i, PTE_ADDR(*pte),
			i & ((*pte & PTE_PS)?0x3FFFFF:0xFFF),
			(*pte & PTE_U)?1:0,
			(*pte & PTE_W)?1:0,
			(*pte & PTE_D)?1:0,
			(*pte & PTE_PS)?1:0);
	}
	return 0;
}

int mon_mappings(int argc, char **argv, struct Trapframe *tf)
{
	if(argc == 1){
		cprintf("Need a range of virtual addresses/a single virtual address\n");
		return 0;
	}
	long start,end;
	start = strtol(argv[1], NULL, 16);
	if(argc == 2){
		end=start;
	}
	else if(argc == 3){
		end=strtol(argv[2], NULL, 16);
		if( end < start){
			cprintf("Usage: showmappings <start> <end>\n   (start <= end)\n");
			return 0;
		}
	}
	else{
		cprintf("Usage: showmappings <start_va_addr> <end_va_addr>\n");
		return 0;
	}
	for(long i=start; i <= end; i+=(1<<12)){
		pte_t* pte = pgdir_walk(kern_pgdir, (void *)i, false);
		if(pte == NULL || !(*pte & PTE_P)){
			cprintf("va: 0x%04x  Not Mapped\n", i);
		}
		else{
			cprintf("va:0x%04x page addr:0x%x offset:0x%x User:%d Writable:%d Dirty:%d PSE:%d\n",
			i, PTE_ADDR(*pte),
			i & ((*pte & PTE_PS)?0x3FFFFF:0xFFF),
			(*pte & PTE_U)?1:0,
			(*pte & PTE_W)?1:0,
			(*pte & PTE_D)?1:0,
			(*pte & PTE_PS)?1:0);
		}
	}
	return 0;
}


int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	// Your code here.
	uint32_t* ebp;
	struct Eipdebuginfo eipinfo;
	
	ebp = (uint32_t*)read_ebp();
	cprintf("Stack backtrace:\n");
	for(; ebp != 0; ){
		cprintf("ebp %08x eip %08x args", ebp, ebp[1]);
		for(int i = 0; i < 5; i++){
			cprintf(" %08x", ebp[i+2]);
		}
		cprintf("\n");
		if(debuginfo_eip(ebp[1], &eipinfo) == 0){
			cprintf("%s:%d: %.*s+%d\n", eipinfo.eip_file, eipinfo.eip_line,
					eipinfo.eip_fn_namelen, eipinfo.eip_fn_name, ebp[1] - eipinfo.eip_fn_addr);
		}
		ebp = (uint32_t*)*ebp;
	}
	return 0;
}



/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");


	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
