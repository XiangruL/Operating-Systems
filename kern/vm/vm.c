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
#include <wchan.h>
//sec
#include <clock.h>

static struct vnode * swap_vnode;
static bool booted = false;
static struct wchan * tlb_wchan;

struct spinlock cm_lock = SPINLOCK_INITIALIZER;

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
		vm_bitmap = bitmap_create(st.st_size / PAGE_SIZE);
		//KASSERT(vm_bitmap != NULL);
		swap_lock = lock_create("swap_lock");
		//KASSERT(swap_lock != NULL);
	}
	// spinlock_init(&cm_lock);
	booted = true;
	tlb_wchan = wchan_create("tlb_sem");
	cm_wchan = wchan_create("cm_wchan");
}

void
wait_page_if_busy(unsigned index) {
	while(coremap[index].cm_isbusy){
		wchan_sleep(cm_wchan, &cm_lock);
	}
}

int
block_write(void *buffer, off_t offset/*default size: PAGE_SIZE*/){
	struct iovec iov;
	struct uio u;

	uio_kinit(&iov, &u, buffer, PAGE_SIZE, offset, UIO_WRITE);
	// kprintf("Write: %d, kvaddr: %x\n", (int)offset/PAGE_SIZE, (int)buffer);
	spinlock_release(&cm_lock);
	int result = VOP_WRITE(swap_vnode, &u);
	if(result){
		spinlock_acquire(&cm_lock);
		return result;
	}
	spinlock_acquire(&cm_lock);
	return 0;
}

int
block_read(void * buffer, off_t offset){
	struct iovec iov;
	struct uio u;

	uio_kinit(&iov, &u, buffer, PAGE_SIZE, offset, UIO_READ);
	// kprintf("Read: %d, kvaddr: %x\n", (int)offset/PAGE_SIZE, (int)buffer);
	spinlock_release(&cm_lock);
	int result = VOP_READ(swap_vnode, &u);
	if(result){
		spinlock_acquire(&cm_lock);
		return result;
	}
	spinlock_acquire(&cm_lock);
	return 0;
}



static
paddr_t
swap_out(enum cm_status_t status, unsigned npages){

	//try swap out:(check vm_swapenabled at very first)
	if(npages > 1){
		panic("npages > 1");
	}
	if(!vm_swapenabled){
		return 0;
	}

	paddr_t pa;

	bool cm_lk_hold_before = false;
	if(!spinlock_do_i_hold(&cm_lock)){
		spinlock_acquire(&cm_lock);
	}else{
		cm_lk_hold_before = true;
	}
	//1. select a coremap index to evict as a victim
	unsigned victim = 0;

	struct timespec ts;
    gettime(&ts);
	time_t tmp_sec;
	tmp_sec = ts.tv_sec;
	for(unsigned i = cm_addr / PAGE_SIZE ; i < cm_num; i++){
		if(coremap[i].cm_status != Fixed && !coremap[i].cm_isbusy){
			if(coremap[i].cm_sec == 0){
				victim = i;
				break;
			}else if(coremap[i].cm_sec <= tmp_sec){
				tmp_sec  = coremap[i].cm_sec;
				victim = i;
			}
		}

	}
	// while(1){
	// 	victim = random() % cm_num;
	// 	if(coremap[victim].cm_status != Fixed && !coremap[victim].cm_isbusy){
	// 		coremap[victim].cm_status = Fixed;
	// 		break;
	// 	}
	// }

	if(victim == 0){
		if(!cm_lk_hold_before){
			spinlock_release(&cm_lock);
		}
		return 0;
	}
	coremap[victim].cm_status = Fixed;
	pa = victim * PAGE_SIZE;
	unsigned k = victim;
	//2. check its status and block_write(may not need)
	//2.1 find pid and related ptnode
	coremap[k].cm_isbusy = true;
	pid_t tmp_pid = coremap[k].cm_pid;
	struct pageTableNode * tmp_ptNode = coremap[k].cm_pte;
	KASSERT(tmp_ptNode != NULL);

	//2.2 check isDirty and swap_out

	unsigned index;
	if(bitmap_alloc(vm_bitmap, &index)){
		panic("bitmap_alloc(vm_bitmap, &index)");
	}
	//KASSERT(bitmap_isset(vm_bitmap, index) != 0);
	//block_write
	if(block_write((void *)PADDR_TO_KVADDR(k * PAGE_SIZE), index * PAGE_SIZE)){
		panic("block_write((void *)PADDR_TO_KVADDR(k * PAGE_SIZE), index * PAGE_SIZE");
	}
	tmp_ptNode->pt_bm_index = index;


	//3. modify its status
	tmp_ptNode->pt_inDisk = true;
	// tmp_ptNode->pt_isDirty = true;
	tmp_ptNode->pt_pas = 0;


	if(k == victim){
		coremap[k].cm_len = npages;
	}else{
		coremap[k].cm_len = 0;
	}
	coremap[k].cm_status = status;
	if(status == Dirty){
		coremap[k].cm_pid = curproc->p_PID;
	}else{
		coremap[k].cm_pid = -1;
	}
	coremap[k].cm_pte = NULL;
	coremap[k].cm_isbusy = false;
	coremap[k].cm_intlb = false;
	coremap[k].cm_sec = 0;

	wchan_wakeall(cm_wchan, &cm_lock);

	//tlbshootdown
	int spl;
	uint32_t ehi, elo;
	ehi = tmp_ptNode->pt_vas;

	if(coremap[k].cm_intlb){
		if(curcpu == procTable[tmp_pid]->p_thread->t_cpu){
			// kprintf("S\n");
			spl = splhigh();
			elo = k * PAGE_SIZE;
			int i = tlb_probe(ehi, elo);
			if(i >= 0){
				tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
			}
			coremap[k].cm_intlb = false;
			splx(spl);
		}else{
			// kprintf("tlbshootdown\n");
			struct tlbshootdown ts; //= (struct tlbshootdown *)kmalloc(sizeof(struct tlbshootdown));
			ts.ts_placeholder = k * PAGE_SIZE;
			ts.ts_cmindex = k;
			ipi_tlbshootdown(procTable[tmp_pid]->p_thread->t_cpu, &ts);
			while (coremap[k].cm_intlb) {
				wchan_sleep(tlb_wchan, &cm_lock);
			}
		}
	}

	if(!cm_lk_hold_before){
		spinlock_release(&cm_lock);
	}

	return pa;

}


vaddr_t
alloc_kpages(unsigned npages)
{
	paddr_t pa = 0;
    unsigned tmp = 0;

	//spinlock_acquire
	//1. &cm_lock:
	bool cm_lk_hold_before = false;
	if(booted){
		if(!spinlock_do_i_hold(&cm_lock)){
			spinlock_acquire(&cm_lock);
		}else{
			cm_lk_hold_before = true;
		}
	}

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
				coremap[k].cm_isbusy = false;
				coremap[k].cm_pid = -1;
				coremap[k].cm_len = 0;
				coremap[k].cm_intlb = false;
				coremap[i].cm_sec = 0;
                if(k == i - npages + 1){
                    coremap[k].cm_len = npages;
                }
            }
			if(booted){
				if(!cm_lk_hold_before){
					spinlock_release(&cm_lock);
				}
			}
            return PADDR_TO_KVADDR(pa);
        }
    }
	//all bootstrap steps must not take up all of memory,
	//so don't worry about "booted" variable not used in swap_out().
	pa = swap_out(Fixed, npages);
	if(pa == 0){
		if(booted){
			if(!cm_lk_hold_before){
				spinlock_release(&cm_lock);
			}
		}
		return 0;
	}

	if(booted){
		if(!cm_lk_hold_before){
			spinlock_release(&cm_lock);
		}
	}
	return PADDR_TO_KVADDR(pa);// no
}


vaddr_t
user_alloc_onepage()
{
	paddr_t pa = 0;
	//used in as_copy and vm_fault, they acquire &cm_lock first.
	bool cm_lk_hold_before = false;
	if(!spinlock_do_i_hold(&cm_lock)){
		spinlock_acquire(&cm_lock);
	}else{
		cm_lk_hold_before = true;
	}

    for(unsigned i = cm_addr / PAGE_SIZE ; i < cm_num; i++){
        if(coremap[i].cm_status == Free){
			pa = i * PAGE_SIZE;
            coremap[i].cm_status = Dirty;
            coremap[i].cm_len = 1;
			coremap[i].cm_pid = curproc->p_PID;
			coremap[i].cm_isbusy = false;
			coremap[i].cm_intlb = false;
			coremap[i].cm_sec = 0;
			bzero((void *)PADDR_TO_KVADDR(pa), 1 * PAGE_SIZE);
			if(!cm_lk_hold_before){
				spinlock_release(&cm_lock);
			}
	        return PADDR_TO_KVADDR(pa);
        }
    }
	//try swap_out
	pa = swap_out(Dirty, 1);
	if(pa == 0){
		if(!cm_lk_hold_before){
			spinlock_release(&cm_lock);
		}
		return 0;
	}
	bzero((void *)PADDR_TO_KVADDR(pa), 1 * PAGE_SIZE);
	if(!cm_lk_hold_before){
		spinlock_release(&cm_lock);
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
	//spinlock_acquire
	//1. &cm_lock:
	bool cm_lk_hold_before = false;
	if(booted){
		if(!spinlock_do_i_hold(&cm_lock)){
			spinlock_acquire(&cm_lock);
		}else{
			cm_lk_hold_before = true;
		}
	}

	unsigned len = coremap[index].cm_len;
    if(coremap[index].cm_len >= 1){
        for(unsigned i = 0; i < len; i++){
            coremap[index + i].cm_status = Free;
			coremap[index + i].cm_pid = -1;
			coremap[index + i].cm_isbusy = false;
			coremap[index + i].cm_len = 0;
			coremap[index + i].cm_intlb = false;
			coremap[index + i].cm_pte = NULL;
			coremap[index + i].cm_sec = 0;
        }
    }

	if(booted){
		if(!cm_lk_hold_before){
			spinlock_release(&cm_lock);
		}
	}

}

void
user_free_onepage(vaddr_t addr)
{
	// (void)addr;
    paddr_t paddr1 = addr - MIPS_KSEG0;
    if(paddr1 > ram_getsize()){
        return;
    }
    int index = paddr1 / PAGE_SIZE;

    // synchronization
	// used in as_destroy, as_destroy acquires &cm_lock first.
	bool cm_lk_hold_before = false;
	if(!spinlock_do_i_hold(&cm_lock)){
		spinlock_acquire(&cm_lock);
	}else{
		cm_lk_hold_before = true;
	}


	coremap[index].cm_status = Free;
	coremap[index].cm_pid = -1;
	coremap[index].cm_isbusy = false;
	coremap[index].cm_len = 0;
	coremap[index].cm_intlb = false;
	coremap[index].cm_pte = NULL;
	coremap[index].cm_sec = 0;
	if(!cm_lk_hold_before){
		spinlock_release(&cm_lock);
	}
}
unsigned
int
coremap_used_bytes() {

	/* dumbvm doesn't track page allocations. Return 0 so that khu works. */
    unsigned int res = 0;
	//spinlock_acquire
	//1. &cm_lock:
	bool cm_lk_hold_before = false;
	if(booted){
		if(!spinlock_do_i_hold(&cm_lock)){
			spinlock_acquire(&cm_lock);
		}else{
			cm_lk_hold_before = true;
		}
	}
    for(unsigned i = 0; i < ram_getsize()/PAGE_SIZE; i++){
        if(coremap[i].cm_status != Free){
            res++;
        }
    }
	if(booted){
		if(!cm_lk_hold_before){
			spinlock_release(&cm_lock);
		}
	}
    return res*PAGE_SIZE;
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{

	int spl;
	uint32_t ehi, elo;
	bool cm_lk_hold_before = false;
	if(!spinlock_do_i_hold(&cm_lock)){
		spinlock_acquire(&cm_lock);
	}else{
		cm_lk_hold_before = true;
	}

	spl = splhigh();
	elo = ts->ts_placeholder;
	ehi = 0;
	int i = tlb_probe(ehi, elo);
	if(i >= 0){
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}
	coremap[ts->ts_cmindex].cm_intlb = false;
	splx(spl);
	wchan_wakeall(tlb_wchan, &cm_lock);
	if(!cm_lk_hold_before){
		spinlock_release(&cm_lock);
	}
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
			////KASSERT
			//KASSERT(tmp->as_vbase != 0);
			//KASSERT((tmp->as_vbase & PAGE_FRAME) == tmp->as_vbase);
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

	//spinlock_acquire
	//1. &cm_lock:
	bool cm_lk_hold_before = false;
	if(!spinlock_do_i_hold(&cm_lock)){
		spinlock_acquire(&cm_lock);
	}else{
		cm_lk_hold_before = true;
	}
	//2. as_ptLock:
	// bool pt_lk_hold_before = false;
	// if(!spinlock_do_i_hold(as->as_ptLock)){
	// 	spinlock_acquire(as->as_ptLock);
	// }else{
	// 	pt_lk_hold_before = true;
	// }

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
		wait_page_if_busy(ptTmp->pt_pas / PAGE_SIZE);

		if(ptTmp->pt_inDisk){
			//1.1 if in disk, user_alloc_onepage, then swap in
			vaddr_t vaddr_tmp = user_alloc_onepage();
			if(vaddr_tmp == 0){
				if(!cm_lk_hold_before){
					spinlock_release(&cm_lock);
				}
				return ENOMEM;
			}


			ptTmp->pt_pas = vaddr_tmp - MIPS_KSEG0;
			paddr1 = ptTmp->pt_pas;

			coremap[paddr1 / PAGE_SIZE].cm_isbusy = true;

			//KASSERT((paddr1 & PAGE_FRAME) == paddr1);

			KASSERT(bitmap_isset(vm_bitmap, ptTmp->pt_bm_index) != 0);

			if(block_read((void *)PADDR_TO_KVADDR(paddr1), ptTmp->pt_bm_index * PAGE_SIZE)){
				panic("block_read error in vm_fault\n");
			}

			//2 change status
			bitmap_unmark(vm_bitmap, ptTmp->pt_bm_index);
			// ptTmp->pt_isDirty = true;
			ptTmp->pt_bm_index = 0;

			ptTmp->pt_inDisk = false;

			KASSERT(coremap[paddr1 / PAGE_SIZE].cm_pid == curproc->p_PID);
			KASSERT((paddr1 & PAGE_FRAME) == paddr1);
			coremap[paddr1 / PAGE_SIZE].cm_pte = ptTmp;
			coremap[paddr1 / PAGE_SIZE].cm_isbusy = false;
			wchan_wakeall(cm_wchan, &cm_lock);


		}else{
			//1.2 if in memory
			paddr1 = ptTmp->pt_pas;
		}
	}else{
		//create
		struct pageTableNode * newpt;
		newpt = (struct pageTableNode *)kmalloc(sizeof(struct pageTableNode));
		if(newpt == NULL){
			if(!cm_lk_hold_before){
				spinlock_release(&cm_lock);
			}
			return ENOMEM;
		}
		newpt->pt_vas = faultaddress;
		// newpt->pt_isDirty = true;
		newpt->pt_inDisk = false;
		newpt->pt_bm_index = 0;
		vaddr_t vaddr_tmp = user_alloc_onepage();//alloc_kpages(1);
		// kprintf("%x\n", vaddr_tmp);
		if(vaddr_tmp == 0){
			kfree(newpt);
			if(!cm_lk_hold_before){
				spinlock_release(&cm_lock);
			}
			return ENOMEM;
		}
		newpt->pt_pas = vaddr_tmp - MIPS_KSEG0;
		paddr1 = newpt->pt_pas;
		coremap[paddr1 / PAGE_SIZE].cm_pte = newpt;
		newpt->next = as->pageTable;
		as->pageTable = newpt;

	}



	//update TLB
	/* make sure it's page-aligned */
	//KASSERT((paddr1 & PAGE_FRAME) == paddr1);
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
	}else{
		tlb_random(ehi, elo | TLBLO_DIRTY | TLBLO_VALID);
		splx(spl);
	}

	coremap[paddr1 / PAGE_SIZE].cm_intlb = true;
	struct timespec ts;
	gettime(&ts);
	coremap[paddr1 / PAGE_SIZE].cm_sec = ts.tv_sec;
	// spinlock_release
	// if(!pt_lk_hold_before){
	// 	spinlock_release(as->as_ptLock);
	// }
	if(!cm_lk_hold_before){
		// wchan_wakeall(cm_wchan, &cm_lock);
		spinlock_release(&cm_lock);
	}
	return 0;
}
