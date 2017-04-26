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
//file:
#include <vnode.h>
#include <vfs.h>
#include <kern/fcntl.h>
#include <uio.h>
//bitmap
#include <bitmap.h>
#include <kern/stat.h>
//lock
#include <synch.h>


/*
 * Wrap ram_stealmem in a spinlock.
 */
static struct spinlock coremap_lock = SPINLOCK_INITIALIZER;
// static int fifo = 0;
static struct lock * swap_lock;
static struct vnode * swap_vnode;

void
vm_bootstrap(void)
{
	/* Do nothing. */
	//1 vfs open
	if(vfs_open((char *)SWAP_FILENAME, O_RDWR, 0, &swap_vnode)){
		vm_swapenabled = false;
	}else{
		vm_swapenabled = true;
		//2 bitmap create
		struct stat st;
		VOP_STAT(swap_vnode, &st);
		// kprintf("%d", (int)(st.st_size));
		vm_bitmap = bitmap_create(st.st_size / PAGE_SIZE);
		KASSERT(vm_bitmap != NULL);
		swap_lock = lock_create("swap_lock");
		KASSERT(swap_lock != NULL);
	}
}

int
block_write(void *buffer, off_t offset/*default size: PAGE_SIZE*/){
	struct iovec iov;
	struct uio u;

	uio_kinit(&iov, &u, buffer, PAGE_SIZE, offset, UIO_WRITE);
	lock_acquire(swap_lock);
	int result = VOP_WRITE(swap_vnode, &u);
	lock_release(swap_lock);
	if(result){
		return result;
	}

	return 0;
}

int
block_read(void * buffer, off_t offset){
	struct iovec iov;
	struct uio u;

	uio_kinit(&iov, &u, buffer, PAGE_SIZE, offset, UIO_READ);
	lock_acquire(swap_lock);
	int result = VOP_READ(swap_vnode, &u);
	lock_release(swap_lock);
	if(result){
		return result;
	}
	return 0;
}



static
paddr_t
swap_out(enum cm_status_t status, unsigned npages){

	//try swap out:(check vm_swapenabled at very first)
	if(vm_swapenabled){
		paddr_t pa;
		spinlock_acquire(&coremap_lock);
		//1. select a coremap index to evict as a victim
		unsigned victim = 0;
		if(status == Fixed){
			unsigned tmp = 0;
			for(unsigned i = cm_addr / PAGE_SIZE ; i < cm_num; i++){
		        if(coremap[i].cm_status != Fixed){
		            tmp++;
		        }else{
		            tmp = 0;
		        }
		        if(tmp == npages){
		            victim = (i - npages + 1);
		        }
		    }
		}else{
			while(1){
				victim = random() % cm_num;
				if(coremap[victim].cm_status != Fixed){
					break;
				}
			}
		}
		spinlock_release(&coremap_lock);

		if(victim == 0){
			return 0;
		}

		pa = victim * PAGE_SIZE;

		for(unsigned k = victim; k < victim + npages ;k++){
			//2. check its status and block_write(may not need)
			//2.1 find pid and pagetablenode
			spinlock_acquire(&coremap_lock);
			pid_t tmp_pid = coremap[k].cm_pid;
			spinlock_release(&coremap_lock);

			struct pageTableNode * tmp_ptNode = procTable[tmp_pid]->p_addrspace->pageTable;
			while(tmp_ptNode != NULL){
				if(tmp_ptNode->pt_pas == k * PAGE_SIZE){
					break;
				}
				tmp_ptNode = tmp_ptNode->next;
			}
			KASSERT(tmp_ptNode->pt_pas == k * PAGE_SIZE);
			//2.2 check status
			if(tmp_ptNode->pt_isDirty){
				unsigned index;
				if(bitmap_alloc(vm_bitmap, &index)){
					return 0;
				}
				KASSERT(bitmap_isset(vm_bitmap, index) != 0);		//block_write
				if(block_write((void *)PADDR_TO_KVADDR(k * PAGE_SIZE), index * PAGE_SIZE)){
					return 0;
				}
				tmp_ptNode->pt_bm_index = index;
			}


			//3. modify its status
			tmp_ptNode->pt_inDisk = true;
			tmp_ptNode->pt_isDirty = true;

			spinlock_acquire(&coremap_lock);
			coremap[k].cm_pid = curproc->p_PID;
			// coremap[victim].cm_inPTE = true;
			coremap[k].cm_len = 1;
			coremap[k].cm_status = status;

			spinlock_release(&coremap_lock);
			//4. tlbshootdown

			int spl;
			uint32_t ehi, elo;
			spl = splhigh();
			ehi = tmp_ptNode->pt_vas;
			elo = k * PAGE_SIZE;
			int i = tlb_probe(ehi, elo);
			if(i >= 0){
				tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
			}
			splx(spl);
		}
		//5. return physical addrspace
		return pa;
	}
	return 0;
}


vaddr_t
alloc_kpages(unsigned npages)
{
	paddr_t pa = 0;
    unsigned tmp = 0;
    spinlock_acquire(&coremap_lock);

    for(unsigned i = cm_addr / PAGE_SIZE ; i < cm_num; i++){
        if(coremap[i].cm_status == Free){
            tmp++;
        }else{
            tmp = 0;
        }
        if(tmp == npages){
            pa = (i - npages + 1) * PAGE_SIZE;
            for(unsigned k = i - npages + 1; k <= i;k++){
                coremap[k].cm_status = Fixed;
                if(k == i - npages + 1){
                    coremap[k].cm_len = npages;
                }
            }
            spinlock_release(&coremap_lock);
            return PADDR_TO_KVADDR(pa);
        }
    }
    spinlock_release(&coremap_lock);

	pa = swap_out(Fixed, npages);
	if(pa == 0){
		return 0;
	}

	return PADDR_TO_KVADDR(pa);// no
}


vaddr_t
user_alloc_onepage()
{
	paddr_t pa = 0;
    spinlock_acquire(&coremap_lock);

    for(unsigned i = cm_addr / PAGE_SIZE ; i < cm_num; i++){
        if(coremap[i].cm_status == Free){
			pa = i * PAGE_SIZE;
            coremap[i].cm_status = Dirty;
			// coremap[i].cm_inPTE = true;
            coremap[i].cm_len = 1;
			coremap[i].cm_pid = curproc->p_PID;
			spinlock_release(&coremap_lock);
	        return PADDR_TO_KVADDR(pa);
        }
    }

	spinlock_release(&coremap_lock);

	//try swap_out
	pa = swap_out(Dirty, 1);
	if(pa == 0){
		return 0;
	}
	return PADDR_TO_KVADDR(pa);
}

void
free_kpages(vaddr_t addr)
{
	// (void)addr;
    paddr_t paddr1 = addr - MIPS_KSEG0;
    if(paddr1 > ram_getsize()){
        return;
    }
    int index = paddr1 / PAGE_SIZE;

    // synchronization
    spinlock_acquire(&coremap_lock);
    if(coremap[index].cm_len >= 1){
        for(unsigned i = 0; i < coremap[index].cm_len; i++){
            coremap[index + i].cm_status = Free;
			coremap[index + i].cm_pid = -1;
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
	struct addrspace *as;

	faultaddress &= PAGE_FRAME;

	switch (faulttype) {
	    case VM_FAULT_READONLY:
		/* We always create pages read-write, so we can't get this */
		panic("vm: got VM_FAULT_READONLY\n");
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

	// faultaddress should belong to one regions

	stackbase = USERSTACK - VM_STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK;
	if(faultaddress >= stacktop){
		return EFAULT;
	}
	if(faultaddress >= as->heap_vbase + as->heap_vbound * PAGE_SIZE && faultaddress < stackbase){
		return EFAULT;
	}

	// stack or heap:
	// do nothing in this step and skip to create new pte.
	// non-stack:

	struct regionInfoNode * tmp = as->regionInfo;
	//faultaddress should be only in non-stack non-heap regions.
	if(faultaddress < as->heap_vbase){
		while(tmp != NULL){
			//KASSERT
			KASSERT(tmp->as_vbase != 0);
			KASSERT((tmp->as_vbase & PAGE_FRAME) == tmp->as_vbase);
			//base & bounds
			vbase = tmp->as_vbase;
			vtop = vbase + tmp->as_npages * PAGE_SIZE;
			if(faultaddress >= vbase && faultaddress < vtop){
				break;
			}else{
				tmp = tmp->next;
			}
		}
		if(tmp == NULL){
			// kprintf("vm.c invalid faultaddress");
			return EFAULT;// invalid faultaddress
		}
	}

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

		//1. check ptTmp status
		if(ptTmp->pt_inDisk){
			//1.1 if in disk, user_alloc_onepage, then swap in
			vaddr_t vaddr_tmp = user_alloc_onepage();//alloc_kpages(1);
			if(vaddr_tmp == 0){
				return ENOMEM;
			}
			if(block_read((void *)vaddr_tmp, ptTmp->pt_bm_index * PAGE_SIZE)){
				panic("block_read error in vm_fault\n");
			}

			//2 change status
			ptTmp->pt_pas = vaddr_tmp - MIPS_KSEG0;
			paddr1 = ptTmp->pt_pas;
			ptTmp->pt_inDisk = false;
			KASSERT(coremap[paddr1 / PAGE_SIZE].cm_pid == curproc->p_PID);
			KASSERT((paddr1 & PAGE_FRAME) == paddr1);

			ptTmp->pt_isDirty = true;
			KASSERT(bitmap_isset(vm_bitmap, ptTmp->pt_bm_index) != 0);
			bitmap_unmark(vm_bitmap, ptTmp->pt_bm_index);
		}else{
			//1.2 if in memory
			paddr1 = ptTmp->pt_pas;
		}
	}else{
		//create
		struct pageTableNode * newpt;
		newpt = (struct pageTableNode *)kmalloc(sizeof(struct pageTableNode));
		if(newpt == NULL){
			return ENOMEM;
		}
		newpt->pt_vas = faultaddress;
		newpt->pt_isDirty = true;
		newpt->pt_inDisk = false;
		newpt->pt_bm_index = 0;
		vaddr_t vaddr_tmp = user_alloc_onepage();//alloc_kpages(1);
		// kprintf("%x\n", vaddr_tmp);
		if(vaddr_tmp == 0){
			kfree(newpt);
			return ENOMEM;
		}
		newpt->pt_pas = vaddr_tmp - MIPS_KSEG0;
		paddr1 = newpt->pt_pas;
		bzero((void *)PADDR_TO_KVADDR(paddr1), 1 * PAGE_SIZE);
		// newpt->pt_pas |= tmp->as_permission;

		newpt->next = as->pageTable;
		as->pageTable = newpt;

	}
	//update TLB
	/* make sure it's page-aligned */
	KASSERT((paddr1 & PAGE_FRAME) == paddr1);
	int spl;
	uint32_t ehi, elo;
	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();
	ehi = faultaddress;
	elo = paddr1;
	int i = tlb_probe(ehi, elo);
	if(i >= 0){
		tlb_write(ehi, elo | TLBLO_DIRTY | TLBLO_VALID, i);
		splx(spl);
		return 0;
	}else{
		tlb_random(ehi, elo | TLBLO_DIRTY | TLBLO_VALID);
		splx(spl);
		return 0;
	}
}
