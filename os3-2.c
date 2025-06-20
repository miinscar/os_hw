#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAGESIZE (32)
#define PAS_FRAMES (256) 
#define PAS_SIZE (PAGESIZE * PAS_FRAMES)
#define VAS_PAGES (64)
#define PTE_SIZE (4)
#define PAGE_INVALID (0)
#define PAGE_VALID (1)
#define MAX_REFERENCES (256)
#define MAX_PROCESSES (10)
#define L1_PT_ENTRIES (8)
#define L2_PT_ENTRIES (8)

typedef struct {
    unsigned char frame;
    unsigned char vflag;
    unsigned char ref;
    unsigned char pad;
} pte;

typedef struct {
    int pid;
    int ref_len;
    unsigned char *references;
    pte *L1_page_table;
    int page_faults;
    int ref_count;
} process;

unsigned char pas[PAS_SIZE];
int allocated_frame_count = 0;

int allocate_frame() {
    if (allocated_frame_count >= PAS_FRAMES) 
        return -1;
    return allocated_frame_count++;
}

// 페이지 테이블 프레임을 하나 할당하고, 해당 프레임을 0으로 초기화하여 반환하는 함수
// 2단계 페이지 테이블 구조에서 1단계/2단계 모두 8개 엔트리만 필요하므로 프레임 하나만 할당
// 반환값: 할당된 페이지 테이블의 시작 주소(실패 시 NULL)
pte *allocate_pagetable_frame() {
    int frame = allocate_frame(); // 사용 가능한 프레임 번호 할당
    if (frame == -1) 
        return NULL; // 프레임 할당 실패 시 NULL 반환
    pte *page_table_ptr = (pte *)&pas[frame * PAGESIZE]; // 프레임 시작 주소를 pte 포인터로 변환
    memset(page_table_ptr, 0, PAGESIZE); // 해당 프레임(32B)을 0으로 초기화
    return page_table_ptr; // 페이지 테이블 포인터 반환
}

int load_process(FILE *fp, process *proc) {
    if (fread(&proc->pid, sizeof(int), 1, fp) != 1) 
        return 0;
    if (fread(&proc->ref_len, sizeof(int), 1, fp) != 1) 
        return 0;
    proc->references = malloc(proc->ref_len);
    if (fread(proc->references, 1, proc->ref_len, fp) != proc->ref_len) 
        return 0;

    printf("%d %d\n", proc->pid, proc->ref_len);
    for (int i = 0; i < proc->ref_len; i++) {
        printf("%02d ", proc->references[i]);
    }
    printf("\n");

    proc->page_faults = 0;
    proc->ref_count = 0;
    if ((proc->L1_page_table = allocate_pagetable_frame()) == NULL)
        return -1;
    return 1;
}

void simulate(process *procs, int proc_count) {
    printf("simulate() start\n");

    while (1) {
        int finished = 1;

        for (int i = 0; i < proc_count; i++) {
            process *p = &procs[i];
            if (p->ref_count >= p->ref_len) continue;
            finished = 0;

            int page = p->references[p->ref_count++];
            int l1_index = page / L2_PT_ENTRIES;
            int l2_index = page % L2_PT_ENTRIES;

            pte *l1_entry = &p->L1_page_table[l1_index];

            // L1 page fault
            if (l1_entry->vflag == PAGE_INVALID) {
                pte *new_L2 = allocate_pagetable_frame();
                if (!new_L2) {
                    printf("Out of memory!!\n");
                    return;
                }
                l1_entry->frame = (new_L2 - (pte *)pas) / PAGESIZE;
                l1_entry->vflag = PAGE_VALID;
                printf("[PID %02d IDX:%03d] Page access %03d: (L1PT) PF -> Allocated Frame %03d\n",
                       p->pid, p->ref_count - 1, page, l1_entry->frame);
            }

            // L2 접근
            pte *L2_base = (pte *)&pas[l1_entry->frame * PAGESIZE];
            pte *l2_entry = &L2_base[l2_index];

            if (l2_entry->vflag == PAGE_INVALID) {
                int f = allocate_frame();
                if (f == -1) {
                    printf("Out of memory!!\n");
                    return;
                }
                l2_entry->frame = f;
                l2_entry->vflag = PAGE_VALID;
                l2_entry->ref = 1;
                p->page_faults++;
                printf("[PID %02d IDX:%03d] Page access %03d: (L1PT) Frame %03d,(L2PT) PF -> Allocated Frame %03d\n",
                       p->pid, p->ref_count - 1, page, l1_entry->frame, f);
            } else {
                l2_entry->ref++;
                printf("[PID %02d IDX:%03d] Page access %03d: (L1PT) Frame %03d, (L2PT) Frame %03d\n",
                       p->pid, p->ref_count - 1, page, l1_entry->frame, l2_entry->frame);
            }
        }

        if (finished) break;
    }

    printf("simulate() end\n");
}


void print_page_tables(process *procs, int proc_count) {
    int total_refs = 0, total_faults = 0;
    int total_allocated = allocated_frame_count;

    for (int i = 0; i < proc_count; i++) {
        process *p = &procs[i];
        printf("** Process %03d: Allocated Frames=%03d PageFaults/References=%03d/%03d\n",
               p->pid, 1 + p->page_faults, p->page_faults, p->ref_count);

        for (int j = 0; j < L1_PT_ENTRIES; j++) {
            pte *l1_entry = &p->L1_page_table[j];
            if (l1_entry->vflag == PAGE_VALID) {
                printf("(L1PT) PTE %03d -> [FRAME] %03d\n", j, l1_entry->frame);
                pte *L2_base = (pte *)&pas[l1_entry->frame * PAGESIZE];

                for (int k = 0; k < L2_PT_ENTRIES; k++) {
                    pte *l2_entry = &L2_base[k];
                    if (l2_entry->vflag == PAGE_VALID) {
                        int page = j * L2_PT_ENTRIES + k;
                        printf("(L2PT) [PAGE] %03d -> [FRAME] %03d REF=%03d\n",
                               page, l2_entry->frame, l2_entry->ref);
                    }
                }
            }
        }

        total_faults += p->page_faults;
        total_refs += p->ref_count;
    }

    printf("Total: Allocated Frames=%03d Page Faults/References=%03d/%03d\n",
           total_allocated, total_faults, total_refs);
}


int main() {
    process procs[MAX_PROCESSES];
    int count = 0;

    printf("load_process() start\n");
    while (count < MAX_PROCESSES) {
        int ret = load_process(stdin, &procs[count]);
        if (ret == 0) 
            break;
        if (ret == -1) {
            printf("Out of memory!!\n");
            return 1;
        }
        count++;
    }
    printf("load_process() end\n");

    simulate(procs, count);
    print_page_tables(procs, count);

    for (int i = 0; i < count; i++) {
        free(procs[i].references);
    }

    return 0;
}
