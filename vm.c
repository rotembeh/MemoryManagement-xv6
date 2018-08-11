#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "elf.h"

extern char data[];  // defined by kernel.ld
pde_t *kpgdir;  // for use in scheduler()

// Set up CPU's kernel segment descriptors.
// Run once on entry on each CPU.
void
seginit(void)
{
  struct cpu *c;

  // Map "logical" addresses to virtual addresses using identity map.
  // Cannot share a CODE descriptor for both kernel and user
  // because it would have to have DPL_USR, but the CPU forbids
  // an interrupt from CPL=0 to DPL=3.
  c = &cpus[cpuid()];
  c->gdt[SEG_KCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, 0);
  c->gdt[SEG_KDATA] = SEG(STA_W, 0, 0xffffffff, 0);
  c->gdt[SEG_UCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, DPL_USER);
  c->gdt[SEG_UDATA] = SEG(STA_W, 0, 0xffffffff, DPL_USER);
  lgdt(c->gdt, sizeof(c->gdt));
}

// Return the address of the PTE in page table pgdir
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages.
static pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;

  pde = &pgdir[PDX(va)]; //#define PDXSHIFT 22 // offset of PDX in a linear address
                         //#define PDX(va) (((uint)(va) >> PDXSHIFT) & 0x3FF), 3FF is 1111 1111 11
  //When seeking an address, the first 10 bits will be used to locate the correct entry within
  //the page directory (extracted using the macro PDX(va)).
  
  if(*pde & PTE_P){  //PTE_P indicates whether the PTE is present
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde)); //#define PTE_ADDR(pte) ((uint)(pte) & ~0xFFF). ~FFF is 0000 0000 0000
  } else {
    if(!alloc || (pgtab = (pte_t*)kalloc()) == 0)
      return 0;
    // Make sure all those PTE_P bits are zero.
    memset(pgtab, 0, PGSIZE);
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table
    // entries, if necessary.
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
  }
  return &pgtab[PTX(va)]; //#define PTXSHIFT 12 // offset of PTX in a linear address
                          //#define PTX(va) (((uint)(va) >> PTXSHIFT) & 0x3FF)
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. //(va) a will point to dir entry that point to pte that points to pa.
static int
mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)
{
  char *a, *last;
  pte_t *pte;

  a = (char*)PGROUNDDOWN((uint)va); //#define PGROUNDDOWN(a) (((a)) & ~(PGSIZE-1)), PGSIZE-1 = 1111 1111 1111 (4095)
  last = (char*)PGROUNDDOWN(((uint)va) + size - 1);
  for(;;){
    if((pte = walkpgdir(pgdir, a, 1)) == 0) //makes the entry- just the PPN
      return -1;
    if(*pte & PTE_P)
      panic("remap");
    *pte = pa | perm | PTE_P; //adds the FLAGS to the PPN
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// There is one page table per process, plus one that's used when
// a CPU is not running any process (kpgdir). The kernel uses the
// current process's page table during system calls and interrupts;
// page protection bits prevent user code from using the kernel's
// mappings.
//
// setupkvm() and exec() set up every page table like this:
//
//   0..KERNBASE: user memory (text+data+stack+heap), mapped to
//                phys memory allocated by the kernel
//   KERNBASE..KERNBASE+EXTMEM: mapped to 0..EXTMEM (for I/O space)
//   KERNBASE+EXTMEM..data: mapped to EXTMEM..V2P(data)
//                for the kernel's instructions and r/o data
//   data..KERNBASE+PHYSTOP: mapped to V2P(data)..PHYSTOP,
//                                  rw data + free physical memory
//   0xfe000000..0: mapped direct (devices such as ioapic)
//
// The kernel allocates physical memory for its heap and for user memory
// between V2P(end) and the end of physical memory (PHYSTOP)
// (directly addressable from end..P2V(PHYSTOP)).

// This table defines the kernel's mappings, which are present in
// every process's page table.
static struct kmap {
  void *virt;
  uint phys_start;
  uint phys_end;
  int perm;
} kmap[] = {
 { (void*)KERNBASE, 0,             EXTMEM,    PTE_W}, // I/O space
 { (void*)KERNLINK, V2P(KERNLINK), V2P(data), 0},     // kern text+rodata
 { (void*)data,     V2P(data),     PHYSTOP,   PTE_W}, // kern data+memory
 { (void*)DEVSPACE, DEVSPACE,      0,         PTE_W}, // more devices
};

// Set up kernel part of a page table.
pde_t*
setupkvm(void)
{
  pde_t *pgdir;
  struct kmap *k;

  if((pgdir = (pde_t*)kalloc()) == 0)
    return 0;
  memset(pgdir, 0, PGSIZE);
  if (P2V(PHYSTOP) > (void*)DEVSPACE)
    panic("PHYSTOP too high");
  for(k = kmap; k < &kmap[NELEM(kmap)]; k++)
    if(mappages(pgdir, k->virt, k->phys_end - k->phys_start,
                (uint)k->phys_start, k->perm) < 0) {
      freevm(pgdir);
      return 0;
    }
  return pgdir;
}

// Allocate one page table for the machine for the kernel address
// space for scheduler processes.
void
kvmalloc(void)
{
  kpgdir = setupkvm();
  switchkvm();
}

// Switch h/w page table register to the kernel-only page table,
// for when no process is running.
void
switchkvm(void)
{
  lcr3(V2P(kpgdir));   // switch to the kernel page table
}

// Switch TSS and h/w page table to correspond to process p.
void
switchuvm(struct proc *p)
{
  if(p == 0)
    panic("switchuvm: no process");
  if(p->kstack == 0)
    panic("switchuvm: no kstack");
  if(p->pgdir == 0)
    panic("switchuvm: no pgdir");

  pushcli();
  mycpu()->gdt[SEG_TSS] = SEG16(STS_T32A, &mycpu()->ts,
                                sizeof(mycpu()->ts)-1, 0);
  mycpu()->gdt[SEG_TSS].s = 0;
  mycpu()->ts.ss0 = SEG_KDATA << 3;
  mycpu()->ts.esp0 = (uint)p->kstack + KSTACKSIZE;
  // setting IOPL=0 in eflags *and* iomb beyond the tss segment limit
  // forbids I/O instructions (e.g., inb and outb) from user space
  mycpu()->ts.iomb = (ushort) 0xFFFF;
  ltr(SEG_TSS << 3);
  lcr3(V2P(p->pgdir));  // switch to process's address space
  popcli();
}

// Load the initcode into address 0 of pgdir.
// sz must be less than a page.
void
inituvm(pde_t *pgdir, char *init, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pgdir, 0, PGSIZE, V2P(mem), PTE_W|PTE_U);
  memmove(mem, init, sz);
}

// Load a program segment into pgdir.  addr must be page-aligned
// and the pages from addr to addr+sz must already be mapped.
int
loaduvm(pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz)
{
  uint i, pa, n;
  pte_t *pte;

  if((uint) addr % PGSIZE != 0)
    panic("loaduvm: addr must be page aligned");
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, addr+i, 0)) == 0)
      panic("loaduvm: address should exist");
    pa = PTE_ADDR(*pte);
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if(readi(ip, P2V(pa), offset+i, n) != n)
      return -1;
  }
  return 0;
}

// Allocate page tables and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
int
allocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  char *mem;
  struct proc* proc=myproc();
/*    if (proc->pid > 2){
    cprintf("allocuvm START: oldsz=%d, newsz=%d. proc->pid: %d, pages in RAM: %d, pages in DISK: %d\n",oldsz, newsz, proc->pid, proc->rnp, proc->snp);

    for(int i = 0; i < MAX_PSYC_PAGES; i++) {
        cprintf("RAM ::: [%d]: %d, \n",i, (int)proc->pgsInRam[i]);    
        cprintf("DISK ::: [%d]: %d, \n",i, (int)proc->pgsInMem[i]);    
    }
}*/
  if((proc->tf->cs&3) == 0)
    panic("allocuvm not dpl user"); //just to understand. should't be happend.

  uint a;
  int i;
  int ramIndex = -1;
  if(newsz >= KERNBASE)
    return 0;
  if(newsz < oldsz)
    return oldsz;
  a = PGROUNDUP(oldsz);

  int notNone = 0;
    #ifndef NONE
      notNone = 1;
    #endif

  for(; a < newsz; a += PGSIZE){

    if(proc && proc->pid > 2 && notNone) {
        if(proc->rnp >= MAX_PSYC_PAGES ) { //ram is full
            if(proc->snp >= MAX_PSYC_PAGES) { //proccess is full
                panic("Not Enough Space"); //we can assume that that case isn't real
                return 0;
            }
            //take page from RAM and put in swapFile
            ramIndex = pageToSwap(proc, pgdir);
            proc->AQupdateAbleFlag = 0;
            swapOut(pgdir, ramIndex);
        } 
        else {
            for(i = 0; i < MAX_PSYC_PAGES; i++) {
                if((int)proc->pgsInRam[i] == -1) {
                    ramIndex = i;
                    break;
                }
            }
        }
      mem = kalloc();
      if(mem == 0){
          cprintf("allocuvm out of memory\n");
          deallocuvm(pgdir, newsz, oldsz);
          return 0;
      }
 
      memset(mem, 0, PGSIZE);
      mappages(pgdir, (char*)a, PGSIZE, V2P(mem), PTE_W|PTE_U);
      lcr3(V2P (proc->pgdir));
      #ifndef AQ
        proc->pgsInRam[ramIndex] = (char*)a;
      #endif
        initPageStruct((char*)a, ramIndex, proc);
      proc->rnp++;
      proc->AQupdateAbleFlag = 1;
    }

    else { //SELECTION == NONE, like before
    mem = kalloc();
    if(mem == 0){
      cprintf("allocuvm out of memory\n");
      deallocuvm(pgdir, newsz, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pgdir, (char*)a, PGSIZE, V2P(mem), PTE_W|PTE_U) < 0){
      cprintf("allocuvm out of memory (2)\n");
      deallocuvm(pgdir, newsz, oldsz);
      kfree(mem);
      return 0;
    }
    }
  }
  /*
  if(notNone && proc->pid > 2) {
    cprintf("allocuvm END: proc->pid: %d, pages in RAM: %d, pages in DISK: %d\n",proc->pid, proc->rnp, proc->snp);
    for(i = 0; i < MAX_PSYC_PAGES; i++) {
        cprintf("RAM :: [%d]: %d, \n",i, (int)proc->pgsInRam[i]); 
  //      cprintf("DISK :: [%d]: %d, \n",i, (int)proc->pgsInMem[i]);   
        cprintf("FIFO :: [%d]: %d, \n",i, (int)proc->FIFOorder[i]);   
        cprintf("AGING :: [%d]: %d, \n",i, (int)proc->agingOrder[i]);   
    }
      cprintf("\n");
  }*/
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
int
deallocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  pte_t *pte;
  uint a, pa;

  if(newsz >= oldsz)
    return oldsz;
  a = PGROUNDUP(newsz);
  for(; a  < oldsz; a += PGSIZE){
    pte = walkpgdir(pgdir, (char*)a, 0);
    if(!pte)
      a = PGADDR(PDX(a) + 1, 0, 0) - PGSIZE;
    else if((*pte & PTE_P) != 0){
      pa = PTE_ADDR(*pte);
      if(pa == 0)
        panic("kfree");
      char *v = P2V(pa);
      #ifndef NONE
        struct proc * proc = myproc();
        for(int i = 0; i < MAX_PSYC_PAGES; i++) {
            if(proc->pgsInRam[i] == v) {
                proc->pgsInRam[i] = (char*)-1;
                proc->rnp--;
                break;
            }
        }
      #endif
      kfree(v);
      *pte = 0;
    }
  }
  return newsz;
}

// Free a page table and all the physical memory pages
// in the user part.
void
freevm(pde_t *pgdir)
{
  uint i;

  if(pgdir == 0)
    panic("freevm: no pgdir");
  deallocuvm(pgdir, KERNBASE, 0);
  for(i = 0; i < NPDENTRIES; i++){
    if(pgdir[i] & PTE_P){
      char * v = P2V(PTE_ADDR(pgdir[i]));
      kfree(v);
    }
  }
  kfree((char*)pgdir);
}

// Clear PTE_U on a page. Used to create an inaccessible
// page beneath the user stack.
void
clearpteu(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if(pte == 0)
    panic("clearpteu");
  *pte &= ~PTE_U;
}

// Given a parent process's page table, create a copy
// of it for a child.
pde_t*
copyuvm(pde_t *pgdir, uint sz)
{
  pde_t *d;
  pte_t *pte;
  uint pa, i, flags;
  char *mem;

  if((d = setupkvm()) == 0) //d is the new pgdir
    return 0;
  for(i = 0; i < sz; i += PGSIZE){ //find all the pages of the father (if coming from fork)
//    cprintf("copyuvm iteration: %d\n", i/PGSIZE); 
    if((pte = walkpgdir(pgdir, (void *) i, 0)) == 0)
      panic("copyuvm: pte should exist");
    if((!(*pte & PTE_P)) && (!(*pte & PTE_PG))) //not present in ram and not in disk
      panic("copyuvm: page not present");
    if((!(*pte & PTE_P)) && (*pte & PTE_PG)) //in disk
    { //we are not copying the disk pages, just making the page tables for our new pgdir (d)
      pte_t* p = walkpgdir(d, (void *) i, 1); //make page table
 //     cprintf("***PDX i::: (as void*)=%p, as dig=%d ||| PDX(i) = %d, %p\n", ((void*)i), i, PDX(i), PDX((void*)i));
      int f = PTE_FLAGS(*pte); 
      *p = *p | f; //with same flags
    }
    else{ //Present flag on
//    cprintf("Copy ELSE case\n");
    pa = PTE_ADDR(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto bad;
    memmove(mem, (char*)P2V(pa), PGSIZE);
    if(mappages(d, (void*)i, PGSIZE, V2P(mem), flags) < 0)
      goto bad;
    }
  }
  return d;

bad:
  cprintf("###COPYUVM BAD###\n");
  freevm(d);
  return 0;
}

//PAGEBREAK!
// Map user virtual address to kernel address.
char*
uva2ka(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if((*pte & PTE_P) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  return (char*)P2V(PTE_ADDR(*pte));
}

// Copy len bytes from p to user address va in page table pgdir.
// Most useful when pgdir is not the current page table.
// uva2ka ensures this only works for PTE_U pages.
int
copyout(pde_t *pgdir, uint va, void *p, uint len)
{
  char *buf, *pa0;
  uint n, va0;

  buf = (char*)p;
  while(len > 0){
    va0 = (uint)PGROUNDDOWN(va);
    pa0 = uva2ka(pgdir, (char*)va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (va - va0);
    if(n > len)
      n = len;
    memmove(pa0 + (va - va0), buf, n);
    len -= n;
    buf += n;
    va = va0 + PGSIZE;
  }
  return 0;
}

//---------------------------------------------------------------------------------------------------------------
//swap in cr2 va into the ram. also delete it from the swapfile. returns 0 if not succeded.
int swapIn(pde_t* pgdir, int cr2) {
    struct proc* proc = myproc();
    cprintf("PID: %d, SwapIn\n",proc->pid);

    pte_t* page;
    int i;
    int ramIndex = -1;
    pte_t *pte = walkpgdir(pgdir, (char*)PGROUNDDOWN(cr2), 0); //find the page table of cr2.
    if((*pte & PTE_PG) == 0){
      return 0; 
    } //PTE_PG mean that the page swaped out, *pte means that the page table exists.

    int swapIdx; //searching the va to swap in
    for(swapIdx = 0 ; swapIdx < MAX_PSYC_PAGES; swapIdx++) { //searching the va in the mem array, that equals to cr2.
        if((int)proc->pgsInMem[swapIdx] == PGROUNDDOWN(cr2)) { 
   //       cprintf("FOUND cr2= %d, cr2RoundDown= %d in disk[%d]\n",cr2, PGROUNDDOWN(cr2), swapIdx);
            break;
        }
    }
    struct proc tempProc;
    tempProc.pid = 1;
    createSwapFile(&tempProc);
    char buffer[PGSIZE/2];
    readFromSwapFile(proc, buffer, swapIdx * PGSIZE, PGSIZE/2); //reads the wanted page to temp swapfile from swapFile. 
    writeToSwapFile(&tempProc, buffer, 0, PGSIZE/2);
    readFromSwapFile(proc, buffer, swapIdx * PGSIZE + PGSIZE/2, PGSIZE/2);
    writeToSwapFile(&tempProc, buffer, PGSIZE/2, PGSIZE/2);

    if(proc->rnp < MAX_PSYC_PAGES) { //not needed to get out page from ram. -in our implemntation we are never in that case.
        for(i = 0; i < MAX_PSYC_PAGES; i++) {
            if((int)proc->pgsInRam[i] == -1) {
                ramIndex = i; //later we will put here the relevant va.
                break;
            }
        }
    }
    else{ //(swap out:)
        //clear place in ram
        proc->AQupdateAbleFlag=0; //important. if we dont do that, we are freeing a page from ram and
        //and remember that we did it to the last one. we get trap between here and where we insert to the 
        //queue. the queue is updating on that trap and we are keeping address that we free here, and also
        //we keep the one that we free here. thats why we block updates in that part of the code.
        //in other selections (not aq), we are putting the new addrress on the same ramIndex we free here,
        //so it's ok.
        ramIndex = pageToSwap(proc, pgdir);
        writeToSwapFile(proc, proc->pgsInRam[ramIndex], swapIdx * PGSIZE, PGSIZE);

        proc->pgsInMem[swapIdx] = proc->pgsInRam[ramIndex];

        page = walkpgdir(pgdir, proc->pgsInRam[ramIndex], 0); //page table to update (to not present)
        
        *page = ((*page) & ~PTE_P) | PTE_PG; //flags-> not present and swaped out
        kfree(P2V(PGROUNDDOWN(*page))); //free the page from ram

        *page=(*page)&0xFFF; //clear the PPN from that entry, keep only the flags!
         proc->pageOuts++;
    }

    //allocate place in ram and copy page
    char* mem = kalloc();
    if(mem == 0){
        cprintf("allocuvm out of memory\n");
        return 0;
    }
    readFromSwapFile(&tempProc, buffer, 0, PGSIZE/2); //+++123 //reads the wanted page to buf from swapFile.
    memmove(mem, buffer, PGSIZE/2); //copy the page we loaded from swapFile into mem (into the ram)
    readFromSwapFile(&tempProc, buffer, PGSIZE/2, PGSIZE/2);
    memmove(mem + PGSIZE/2, buffer, PGSIZE/2);

    removeSwapFile(&tempProc);
    
    mappages(pgdir, (char*)PGROUNDDOWN(cr2), PGSIZE, V2P(mem), PTE_W|PTE_U); //puts the page in the physical memory (in ram), in va cr2
    #ifndef AQ
    proc->pgsInRam[ramIndex] = (char*)PGROUNDDOWN(cr2); 
    #endif
    initPageStruct((char*)PGROUNDDOWN(cr2), ramIndex, proc); //after loading page, updating the orders.

    proc->pageFaults++; //swap in called only from trap on pageFault
    
    lcr3(V2P (proc->pgdir)); //To refresh the TLB we refresh the cr3 register. //why we have to do that
    proc->AQupdateAbleFlag=1;
    return 1; 
}

void swapOut(pde_t* pgdir, int ramIndex) {
    struct proc *proc = myproc();
    cprintf("PID: %d, SwapOut\n",proc->pid);

    int swapIndex = proc->snp;

    writeToSwapFile(proc, proc->pgsInRam[ramIndex], swapIndex * PGSIZE, PGSIZE);

    proc->pgsInMem[swapIndex] = proc->pgsInRam[ramIndex];
    proc->snp++;

    pte_t* page = walkpgdir(pgdir, proc->pgsInRam[ramIndex], 0);
    if (page == 0)
        panic("SwapOut panic");
    kfree(P2V(PTE_ADDR(*page))); //without flags

    *page = ((*page) & ~PTE_P) | PTE_PG;
    *page=(*page)&0xFFF; //clear the PPN from that entry, keep only the flags! (or that is to keep the offset?!)
    proc->pgsInRam[ramIndex] = (char*)-1;

    proc->rnp--;
    proc->pageOuts++;
}
 //page to swap out from ram. returns the relevant index in pgsInRam
int pageToSwap(struct proc * proc, pde_t *pgdir){
  //starting from 4 because 0,1,2,3 made in exec and they are user stack, text and etc. 
  //should not out from the ram. *not always 4. the exact index saved in exec in proc->execPagesLastIdx.
#ifdef NFUA
          int i;
          uint min;
          int index = -1; 
          for(i = proc->execPagesLastIdx; i < MAX_PSYC_PAGES; i++)  //initial with any user page
            if(checkIfUserPage(i, pgdir, proc) > 0){
              index = i;
              break;
            }
          if (index == -1)
            panic("pageToSwap panic 1");
          min = proc->agingOrder[index];
          for(i = proc->execPagesLastIdx; i < MAX_PSYC_PAGES; i++) {
            if(proc->agingOrder[i] < min && checkIfUserPage(i, pgdir, proc) > 0){
              min=proc->agingOrder[i];
              index=i;
            }
          }
     //     cprintf("nfua chose: %d\n",index);
          return index;
        #else
#ifdef LAPA
          int i;
          uint min;
          int index=-1;
          int num;
          for(i = proc->execPagesLastIdx; i < MAX_PSYC_PAGES; i++){ //initial with any user page
            if(checkIfUserPage(i, pgdir, proc) > 0){
              index = i;
              break;
            }}
          if (index == -1) //user page not exist. should not happen.
            panic("pageToSwap panic 1");
          min = numOfOnes(proc->agingOrder[index]);
          for(i = proc->execPagesLastIdx; i < MAX_PSYC_PAGES; i++) {
              if(checkIfUserPage(i, pgdir, proc) > 0){
                  num = numOfOnes(proc->agingOrder[i]);
                  if(num < min){
                    min = num;
                    index = i;
                  }
                  if(num == min){
                    if (proc->agingOrder[i] < proc->agingOrder[index])
                      index = i;
                  }
              }
            }
      return index;
        #else
        #ifdef SCFIFO
          int i;
          int index = -1;
          for(i = proc->execPagesLastIdx; i < MAX_PSYC_PAGES; i++)  //initial with any user page
            if(checkIfUserPage(i, pgdir, proc) > 0){
              index = i;
              break;
            }
          if (index == -1) //user page not exist. should not happen.
            panic("pageToSwap panic 1");
          uint minTime = proc->FIFOorder[index];
          int flag = 0;
          do{ //updateFifoOrder returns 1 if index should swapout
            minTime = ticks + 1;
            for(i = proc->execPagesLastIdx; i < MAX_PSYC_PAGES; i++){
              if (proc -> FIFOorder[i] != -1 && proc->FIFOorder[i] < minTime && checkIfUserPage(i, pgdir, proc) > 0){
                minTime = proc->FIFOorder[i]; //minTime, finds the oldest page in ram
                index = i;
                flag = 1;
              } 
            }
          if (flag == 0)
              panic("choosed no one in fifo choose loop");
          }
          while(updateFifoOrder(index, proc, pgdir) != 1);
              return index;
        #else
        #ifdef AQ
          if (proc->rnp != MAX_PSYC_PAGES)
            panic("AQ pageToSwap panic 1");
          if (checkIfUserPage(MAX_PSYC_PAGES-1, pgdir, proc) <= 0)
            panic("AQ pageToSwap panic 2");
          return MAX_PSYC_PAGES-1;
        #else
        #ifdef NONE
            panic("NONE pageToSwap panic");
            return 0;
        #endif
        #endif
        #endif
        #endif
        #endif
    return 0;
}

int numOfOnes(uint num){
  int count=0;
  int p=0;
  while(num>0){
    p=num%2;
    num=num/2;
    if(p==1)
      count+=1;
  }
  return count;
}

//gets index of the oldest page, checks if PTE_A on. if no- return 1 -> swap out that page. PTE_A on-> move 
//the page to the end of the line and return 0.
int updateFifoOrder(int index,struct proc * p, pde_t *pgdir){
  pte_t* pte = walkpgdir(p->pgdir, p->pgsInRam[index], 0);  //why 1
  if((*pte & PTE_A) != 0) { //page got accessed
          *pte = *pte & ~PTE_A;
          p->FIFOorder[index] = ticks;
  //        cprintf("%d accessed\n",index);
          return 0;
  }
  else 
    return 1;
}

void updateAgingOrder(struct proc* p){
  int i;
  uint temp;
  for(i = p->execPagesLastIdx; i < MAX_PSYC_PAGES; i++) {
      if((int)p->pgsInRam[i] != -1) {
        //shiftRight:
        p->agingOrder[i] = p->agingOrder[i] >> (uint)1;
        temp = 2147483647; //temp = 01111...1111 (31 ones)
        p->agingOrder[i] &= temp;
        pte_t* pte = walkpgdir(p->pgdir, p->pgsInRam[i], 0);      
        if((*pte & PTE_A) != 0) { //page got accessed
   //       cprintf("(pid=%d)###i=%d page accessed on tick: %d###\n",p->pid, i, ticks);
          p->agingOrder[i] |= 1 << 31;; //turn on the leftmostbit
       //   p->agingOrder[i] = p->agingOrder[i] | (uint)2147483648; //the same
          *pte = *pte & ~PTE_A;    
        }
      } 
  }
}

void updateAqOrder(struct proc* p){
  int i;
  for(i = p->execPagesLastIdx + 1; i < MAX_PSYC_PAGES; i++) { //if i=0 accessed we ignore that
      if((int)p->pgsInRam[i] != -1) {
        pte_t* pte = walkpgdir(p->pgdir, p->pgsInRam[i], 0);      
        if((*pte & PTE_A) != 0) { //page got accessed
          char* temp=p->pgsInRam[i];
          p->pgsInRam[i]=p->pgsInRam[i-1];
          p->pgsInRam[i-1]=temp;
          *pte = *pte & ~PTE_A;    
        }
      }  
  }
  }

int checkIfUserPage(int index, pde_t *pgdir, struct proc * proc){
    pte_t* page = walkpgdir(pgdir, proc->pgsInRam[index], 0);
    if ((*page & PTE_P) == 0){
      cprintf("not present: pgsInRam[%d]=%d\n",index, proc->pgsInRam[index]);
       panic("checkIfUserPage not present panic");
     }
    if ((*page & PTE_U) != 0)
  //    if ((*page & PTE_W) != 0)
  //      if ((*page & PTE_P) != 0)
      return 1;
 //   cprintf("\n###Found Not User Page###\n\n"); //some of the first pages are not user's (made in exec)
    return -1;
}

void checkPresent(int i, pde_t *pgdir, struct proc * proc){ //for debbuging
      pte_t* page = walkpgdir(pgdir, proc->pgsInRam[i], 0);
        if (!(*page & PTE_P))
          panic("not present");
      //    cprintf("\n#check present:##i= %d, %d, %d, ###\n\n", i, proc->pgsInRam[i]); //seems to not happend
}

//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.

