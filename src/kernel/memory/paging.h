#ifndef PAGING_H
#define PAGING_H

int paging_init();
void interrupts_init();
int paging_page_present(unsigned int addr);
int paging_unmap_page(unsigned int addr);
unsigned int* paging_get_kernel_directory(void);
void paging_activate_directory(unsigned int* pd);
int paging_build_app_directory(unsigned int* out_pd, unsigned int* out_lowmem_pt0, unsigned int app_phys_base, unsigned int app_bytes);

#endif
