#ifndef PAGING_H
#define PAGING_H

int paging_init();
void interrupts_init();
int paging_page_present(unsigned int addr);
int paging_unmap_page(unsigned int addr);

#endif
