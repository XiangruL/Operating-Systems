#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <cpu.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <elf.h>

#define DUMBVM_STACKPAGES    18

/*
 * Wrap ram_stealmem in a spinlock.
 */
static struct spinlock coremap_lock = SPINLOCK_INITIALIZER;
void
vm_bootstrap(void)
{
	/* Do nothing. */
}


vaddr_t
alloc_kpages(unsigned npages)
{
	paddr_t pa = 0;
    unsigned tmp = 0;
    spinlock_acquire(&coremap_lock);
    for(unsigned i = cm_addr / PAGE_SIZE ; i < ram_getsize()/PAGE_SIZE; i++){
        if(coremap[i].cm_status == Free){
            tmp++;
        }else{
            tmp = 0;
        }
        if(tmp == npages){
            pa = (i - npages + 1) * PAGE_SIZE;
            for(unsigned k = i - npages + 1; k <= i;k++){
                coremap[k].cm_status = Dirty;
                // coremap[k].cm_size = PAGE_SIZE;
                if(k == i - npages + 1){
                    coremap[k].cm_len = npages;
                }
            }
            spinlock_release(&coremap_lock);
            return PADDR_TO_KVADDR(pa);
        }
    }
    spinlock_release(&coremap_lock);
	return 0;// no
}

void
free_kpages(vaddr_t addr)
{
	// (void)addr;
    paddr_t paddr1 = addr - MIPS_KSEG0;
    if(paddr1 > ram_getsize()){
        kprintf("vm.c free_kpages - invalid addr");
    }
    int index = paddr1 / PAGE_SIZE;

    // synchronization
    spinlock_acquire(&coremap_lock);
    if(coremap[index].cm_len >= 1){
        for(unsigned i = 0; i < coremap[index].cm_len; i++){
            coremap[index + i].cm_status = Free;
            // coremap[index + i].cm_size = 0;
        }
        coremap[index].cm_len = 0;
    }
    spinlock_release(&coremap_lock);

}

unsigned
int
coremap_used_bytes() {

	/* dumbvm doesn't track page allocations. Return 0 so that khu works. */
    unsigned int res = 0;
    spinlock_acquire(&coremap_lock);
    for(unsigned i = 0; i < ram_getsize()/PAGE_SIZE; i++){
        if(coremap[i].cm_status != Free){
            res++;
        }
    }
    spinlock_release(&coremap_lock);
    return res*PAGE_SIZE;
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	vaddr_t vbase, vtop, stackbase, stacktop;
	paddr_t paddr1 = 0x0;
	int i;
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl;

	faultaddress &= PAGE_FRAME;

	DEBUG(DB_VM, "3.2 vm: fault: 0x%x\n", faultaddress);

	switch (faulttype) {
	    case VM_FAULT_READONLY:
		/* We always create pages read-write, so we can't get this */
		panic("dumbvm: got VM_FAULT_READONLY\n");
	    case VM_FAULT_READ://0x0
	    case VM_FAULT_WRITE://0x1
		break;
	    default:
		return EINVAL;
	}

	if (curproc == NULL) {
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	as = proc_getas();
	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}

	/* Assert that the address space has been set up properly. */


	// stack:

	KASSERT(as->as_stackpbase != 0);
	KASSERT((as->as_stackpbase & PAGE_FRAME) == as->as_stackpbase);
	stackbase = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK;

	if(faultaddress >= stackbase && faultaddress < stacktop){
		//do ..
		paddr1 = (faultaddress - stackbase) + as->as_stackpbase;


		//TLB update
		/* make sure it's page-aligned */
		KASSERT((paddr1 & PAGE_FRAME) == paddr1);

		/* Disable interrupts on this CPU while frobbing the TLB. */
		spl = splhigh();

		for (i=0; i<NUM_TLB; i++) {
			tlb_read(&ehi, &elo, i);
			if (elo & TLBLO_VALID) {
				continue;
			}
			ehi = faultaddress;
			elo = paddr1 | TLBLO_DIRTY | TLBLO_VALID;
			// DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
			tlb_write(ehi, elo, i);
			splx(spl);
			return 0;
		}
		// fault or do sth..
		// kprintf("dumbvm: Ran out of TLB entries - cannot handle page fault\n");
		splx(spl);
		return EFAULT;
	}

	// non-stack:

	struct regionInfoNode * tmp = as->regionInfo;
	while(tmp != NULL){
		//KASSERT
		KASSERT(tmp->as_vbase != 0);
		KASSERT((tmp->as_vbase & PAGE_FRAME) == tmp->as_vbase);
		//base & bounds
		// if...do...else ...continue
		vbase = tmp->as_vbase;
		vtop = vbase + tmp->as_npages * PAGE_SIZE;
		if(faultaddress >= vbase && faultaddress < vtop){
			//workflow:......
			//skip regionInfo permission
			//
			struct pageTableNode * ptTmp = as->pageTable;
			bool found = 0;
			while(ptTmp != NULL){
				if(ptTmp->pt_vas == faultaddress){
					found = 1;
					break;
				}
				ptTmp = ptTmp->next;
			}
			if(found == 1){
				if(((ptTmp->pt_pas & PF_R) == PF_R && faulttype == VM_FAULT_READ) || ((ptTmp->pt_pas & PF_W) == PF_W && faulttype == VM_FAULT_WRITE)){
					paddr1 = ptTmp->pt_pas & PAGE_FRAME;
					//update TLB;
				}else{
					return EFAULT;
				}
			}else{
				//create
				struct pageTableNode * newpt;
				newpt = (struct pageTableNode *)kmalloc(sizeof(struct pageTableNode));
				if(newpt == NULL){
			        return ENOMEM;
			    }
				newpt->pt_vas = alloc_kpages(1);
				newpt->pt_pas = newpt->pt_vas - MIPS_KSEG0;
				paddr1 = newpt->pt_pas;
				newpt->pt_pas |= tmp->as_tmp_permission;
				newpt->next = as->pageTable;
				as->pageTable = newpt;
			}

		}else{
			tmp = tmp->next;
			continue;
		}

		//TLB update
		/* make sure it's page-aligned */
		KASSERT((paddr1 & PAGE_FRAME) == paddr1);

		/* Disable interrupts on this CPU while frobbing the TLB. */
		spl = splhigh();
		for (i=0; i<NUM_TLB; i++) {
			tlb_read(&ehi, &elo, i);
			if (elo & TLBLO_VALID) {
				continue;
			}
			ehi = faultaddress;
			elo = paddr1 | TLBLO_DIRTY | TLBLO_VALID;
			// DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr1);
			tlb_write(ehi, elo, i);
			splx(spl);
			// kprintf("%d\n",i);
			return 0;
		}
		// fault or do sth..
		panic("3.2 vm: Ran out of TLB entries - cannot handle page fault");
		splx(spl);
		return EFAULT;
	}

	return EFAULT;//segment fault
}
