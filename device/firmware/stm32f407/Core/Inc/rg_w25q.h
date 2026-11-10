#ifndef RG_W25Q_H
#define RG_W25Q_H
#include "rg_journal.h"
bool rg_w25q_init(bool permit_writes);
uint32_t rg_w25q_jedec(void);
uint32_t rg_w25q_capacity(void);
rg_journal_io_t rg_w25q_io(void);
bool rg_w25q_read(void *,uint32_t,void *,uint32_t);
#endif
