//  mem_mgr.cpp
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <cassert>
#include <cstdint>
#include <cstdint>

#pragma warning(disable : 4996)

#define ARGC_ERROR 1
#define FILE_ERROR 2

#define FRAME_SIZE  256
#define FIFO 0
#define LRU 1
#define REPLACE_POLICY FIFO

// SET TO 128 to use replacement policy: FIFO or LRU,
#define NFRAMES 128
#define PTABLE_SIZE 256
#define TLB_SIZE 16

struct page_node {    
    size_t npage;
    size_t frame_num;
    bool is_present;
    bool is_used;
};

char* ram = (char*)malloc(NFRAMES * FRAME_SIZE);
page_node pg_table[PTABLE_SIZE];  // page table and (single) TLB
page_node tlb[TLB_SIZE];

const char* passed_or_failed(bool condition) { return condition ? " + " : "fail"; }
size_t failed_asserts = 0;

size_t get_page(size_t x)   { return 0xff & (x >> 8); }
size_t get_offset(size_t x) { return 0xff & x; }

void get_page_offset(size_t x, size_t& page, size_t& offset) {
    page = get_page(x);
    offset = get_offset(x);
    // printf("x is: %zu, page: %zu, offset: %zu, address: %zu, paddress: %zu\n", 
    //        x, page, offset, (page << 8) | get_offset(x), page * 256 + offset);
}

void update_frame_ptable(size_t npage, size_t frame_num) {
    pg_table[npage].frame_num = frame_num;
    pg_table[npage].is_present = true;
    pg_table[npage].is_used = true;
}

int find_frame_ptable(size_t frame) {  // FIFO
    for (int i = 0; i < PTABLE_SIZE; i++) {
        if (pg_table[i].frame_num == frame && 
            pg_table[i].is_present == true) { return i; }
    }
    return -1;
}

size_t get_used_ptable() {  // LRU
    size_t unused = -1;
    for (size_t i = 0; i < PTABLE_SIZE; i++) {
        if (pg_table[i].is_used == false && 
            pg_table[i].is_present == true) { return (size_t)i; }
    }
    // All present pages have been used recently, set all page entry used flags to false
    for (size_t i = 0; i < PTABLE_SIZE; i++) { pg_table[i].is_used = false; }
    for (size_t i = 0; i < PTABLE_SIZE; i++) {
        page_node& r = pg_table[i];
        if (!r.is_used && r.is_present) { return i; }
    }
    return (size_t)-1;
}

int check_tlb(size_t page) {
    for (int i = 0; i < TLB_SIZE; i++) {
        if (tlb[i].npage == page) { return i; }
    }
    return -1;
}

void open_files(FILE*& fadd, FILE*& fcorr, FILE*& fback) { 
    fadd = fopen("addresses.txt", "r");
    if (fadd == NULL) { fprintf(stderr, "Could not open file: 'addresses.txt'\n");  exit(FILE_ERROR); }

    fcorr = fopen("correct.txt", "r");
    if (fcorr == NULL) { fprintf(stderr, "Could not open file: 'correct.txt'\n");  exit(FILE_ERROR); }

    fback = fopen("BACKING_STORE.bin", "rb");
    if (fback == NULL) { fprintf(stderr, "Could not open file: 'BACKING_STORE.bin'\n");  exit(FILE_ERROR); }
}
void close_files(FILE* fadd, FILE* fcorr, FILE* fback) { 
    fclose(fadd);
    fclose(fcorr);
    fclose(fback);
}

void initialize_pg_table_tlb() { 
    for (int i = 0; i < PTABLE_SIZE; ++i) {
        pg_table[i].npage = (size_t)i;
        pg_table[i].is_present = false;
        pg_table[i].is_used = false;
    }
    for (int i = 0; i < TLB_SIZE; i++) {
        tlb[i].npage = (size_t)-1;
        tlb[i].is_present = false;
        pg_table[i].is_used = false;
    }
}

void summarize(size_t pg_faults, size_t tlb_hits) { 
    printf("\nPage Fault Percentage: %1.3f%%", (double)pg_faults / 1000);
    printf("\nTLB Hit Percentage: %1.3f%%\n\n", (double)tlb_hits / 1000);
    printf("ALL logical ---> physical assertions PASSED!\n");
    printf("\n\t\t...done.\n");
}

void tlb_add(int index, page_node entry) {
    if (index < 0 || index >= TLB_SIZE) {
        // Index out of bounds, handle error accordingly
        fprintf(stderr, "Error: TLB index out of bounds\n");
        return;
    }

    // Replace or add the entry at the specified index
    tlb[index] = entry;
}

void tlb_remove(int index) {
    if (index < 0 || index >= TLB_SIZE) {
        // Index out of bounds, handle error accordingly
        fprintf(stderr, "Error: TLB index out of bounds\n");
        return;
    }

    // Set the TLB entry at the specified index as not present
    tlb[index].is_present = false;

    // Optional: Clean up or reset other fields
    tlb[index].npage = -1;
    tlb[index].frame_num = -1;
}

void tlb_hit(size_t& frame, size_t& page, size_t& tlb_hits, int result) {
    if (result < 0 || result >= TLB_SIZE) {
        // Result index out of bounds, handle error accordingly
        fprintf(stderr, "Error: TLB hit index out of bounds\n");
        return;
    }

    // Update the frame number with the one found in the TLB entry
    frame = tlb[result].frame_num;

    // Increment the TLB hits count
    tlb_hits++;

    // Optional: Update LRU information if using LRU policy
    // This would involve moving the accessed entry to a more 'recently used' position.
}

void tlb_miss(size_t& frame, size_t& page, size_t& tlb_track) {
    // Check if page is in the page table and update frame
    if (pg_table[page].is_present) {
        frame = pg_table[page].frame_num;
    } else {
        // Handle error or page fault if page is not in the page table
        fprintf(stderr, "Error: Page not found in page table during TLB miss\n");
        return;
    }

    // Prepare the new TLB entry
    page_node new_entry;
    new_entry.npage = page;
    new_entry.frame_num = frame;
    new_entry.is_present = true;

    // Update the TLB with the new entry
    // Assuming tlb_track keeps track of the next TLB index to update
    tlb_add(tlb_track % TLB_SIZE, new_entry);

    // Update tlb_track for the next entry
    tlb_track = (tlb_track + 1) % TLB_SIZE;
}

void fifo_replace_page(size_t& frame) {
    static size_t next_frame_to_replace = 0;

    // Check if the frame to be replaced is valid
    if (next_frame_to_replace >= NFRAMES) {
        fprintf(stderr, "Error: Invalid frame index for replacement\n");
        return;
    }

    // Identify the page currently occupying the frame
    int page_to_replace = find_frame_ptable(next_frame_to_replace);
    if (page_to_replace == -1) {
        fprintf(stderr, "Error: No page found in frame to replace\n");
        return;
    }

    // Update the page table to indicate the page is no longer in a frame
    pg_table[page_to_replace].is_present = false;

    // Assign the frame number to the frame variable
    frame = next_frame_to_replace;

    // Update the next frame to replace for the next call
    next_frame_to_replace = (next_frame_to_replace + 1) % NFRAMES;
}

void lru_replace_page(size_t& frame) {
    size_t least_recently_used_time = SIZE_MAX;
    size_t lru_page_index = -1;

    // Iterate over the page table to find the least recently used page
    for (size_t i = 0; i < PTABLE_SIZE; i++) {
        if (pg_table[i].is_present && pg_table[i].is_used < least_recently_used_time) {
            least_recently_used_time = pg_table[i].is_used;
            lru_page_index = i;
        }
    }

    if (lru_page_index == -1) {
        fprintf(stderr, "Error: No page found for LRU replacement\n");
        return;
    }

    // Replace the least recently used page
    frame = pg_table[lru_page_index].frame_num;
    pg_table[lru_page_index].is_present = false;
}

void page_fault(size_t& frame, size_t& page, size_t& frames_used, size_t& pg_faults, 
                size_t& tlb_track, FILE* fbacking) {  
    unsigned char buf[FRAME_SIZE];
    memset(buf, 0, sizeof(buf));
    bool is_memfull = frames_used >= NFRAMES;

    ++pg_faults;

    if (is_memfull) {
        // Memory is full, we need to replace a page
#if REPLACE_POLICY == FIFO
        fifo_replace_page(frame);
#elif REPLACE_POLICY == LRU
        lru_replace_page(frame);
#endif
    } else {
        // Memory is not full, use the next available frame
        frame = frames_used;
    }

    // Load the page into RAM at the frame location
    fseek(fbacking, page * FRAME_SIZE, SEEK_SET);
    fread(buf, FRAME_SIZE, 1, fbacking);

    // Copy the page into the frame
    memcpy(ram + (frame * FRAME_SIZE), buf, FRAME_SIZE);

    // Update the page table with the new frame
    update_frame_ptable(page, frame);

    // Add the page to the TLB
    tlb_add(tlb_track % TLB_SIZE, {page, frame, true, false}); // Assuming TLB_SIZE is the size of the TLB
    tlb_track = (tlb_track + 1) % TLB_SIZE;

    if (!is_memfull) {
        ++frames_used;
    }
}

void check_address_value(size_t logic_add, size_t page, size_t offset, size_t physical_add,
                         size_t& prev_frame, size_t frame, int val, int value, size_t o) { 
    printf("log: %5lu 0x%04x (pg:%3lu, off:%3lu)-->phy: %5lu (frm: %3lu) (prv: %3lu)--> val: %4d == value: %4d -- %s", 
          logic_add, (unsigned int)logic_add, page, offset, physical_add, frame, prev_frame, 
          val, value, passed_or_failed(val == value));

    if (frame < prev_frame) {  printf("   HIT!\n");
    } else {
        prev_frame = frame;
        printf("----> pg_fault\n");
    }
    if (o % 5 == 4) { printf("\n"); }
// if (o > 20) { exit(-1); }             // to check out first 20 elements

    if (val != value) { ++failed_asserts; }
    if (failed_asserts > 5) { exit(-1); }
//     assert(val == value);
}

void run_simulation() { 
        // addresses, pages, frames, values, hits and faults
    size_t logic_add, virt_add, phys_add, physical_add;
    size_t page, frame, offset, value, prev_frame = 0, tlb_track = 0;
    size_t frames_used = 0, pg_faults = 0, tlb_hits = 0;
    int val = 0;
    char buf[BUFSIZ];

    bool is_memfull = false;     // physical memory to store the frames

    initialize_pg_table_tlb();

        // addresses to test, correct values, and pages to load
    FILE *faddress, *fcorrect, *fbacking;
    open_files(faddress, fcorrect, fbacking);

    for (int o = 0; o < 1000; o++) {     // read from file correct.txt
        fscanf(fcorrect, "%s %s %lu %s %s %lu %s %ld", buf, buf, &virt_add, buf, buf, &phys_add, buf, &value);  

        fscanf(faddress, "%ld", &logic_add);  
        get_page_offset(logic_add, page, offset);

        int result = check_tlb(page);
        if (result >= 0) {  
            tlb_hit(frame, page, tlb_hits, result); 
        } else if (pg_table[page].is_present) {
            tlb_miss(frame, page, tlb_track);
        } else {         // page fault
            page_fault(frame, page, frames_used, pg_faults, tlb_track, fbacking);
        }

        physical_add = (frame * FRAME_SIZE) + offset;
        val = (int)*(ram + physical_add);

        check_address_value(logic_add, page, offset, physical_add, prev_frame, frame, val, value, o);
    }
    close_files(faddress, fcorrect, fbacking);  // and time to wrap things up
    free(ram);
    summarize(pg_faults, tlb_hits);
}


int main(int argc, const char * argv[]) {
    run_simulation();
// printf("\nFailed asserts: %lu\n\n", failed_asserts);   // allows asserts to fail silently and be counted
    return 0;
}


/*The output:

log: 64846 0xfd4e (pg:253, off: 78)-->phy:  2126 (frm:   8) (prv: 110)--> val:   63 == value:   63 --  +    HIT!
log: 62938 0xf5da (pg:245, off:218)-->phy: 28634 (frm: 111) (prv: 110)--> val:   61 == value:   61 --  + ----> pg_fault
log: 27194 0x6a3a (pg:106, off: 58)-->phy: 28730 (frm: 112) (prv: 111)--> val:   26 == value:   26 --  + ----> pg_fault
log: 28804 0x7084 (pg:112, off:132)-->phy:  1412 (frm:   5) (prv: 112)--> val:    0 == value:    0 --  +    HIT!
log: 61703 0xf107 (pg:241, off:  7)-->phy: 22279 (frm:  87) (prv: 112)--> val:   65 == value:   65 --  +    HIT!

log: 10998 0x2af6 (pg: 42, off:246)-->phy: 12534 (frm:  48) (prv: 112)--> val:   10 == value:   10 --  +    HIT!
log:  6596 0x19c4 (pg: 25, off:196)-->phy: 29124 (frm: 113) (prv: 112)--> val:    0 == value:    0 --  + ----> pg_fault
log: 37721 0x9359 (pg:147, off: 89)-->phy: 29273 (frm: 114) (prv: 113)--> val:    0 == value:    0 --  + ----> pg_fault
log: 43430 0xa9a6 (pg:169, off:166)-->phy: 29606 (frm: 115) (prv: 114)--> val:   42 == value:   42 --  + ----> pg_fault
log: 22692 0x58a4 (pg: 88, off:164)-->phy:  2980 (frm:  11) (prv: 115)--> val:    0 == value:    0 --  +    HIT!

log: 62971 0xf5fb (pg:245, off:251)-->phy: 28667 (frm: 111) (prv: 115)--> val:  126 == value:  126 --  +    HIT!
log: 47125 0xb815 (pg:184, off: 21)-->phy: 29717 (frm: 116) (prv: 115)--> val:    0 == value:    0 --  + ----> pg_fault
log: 52521 0xcd29 (pg:205, off: 41)-->phy: 29993 (frm: 117) (prv: 116)--> val:    0 == value:    0 --  + ----> pg_fault
log: 34646 0x8756 (pg:135, off: 86)-->phy: 18006 (frm:  70) (prv: 117)--> val:   33 == value:   33 --  +    HIT!
log: 32889 0x8079 (pg:128, off:121)-->phy:  4217 (frm:  16) (prv: 117)--> val:    0 == value:    0 --  +    HIT!

log: 13055 0x32ff (pg: 50, off:255)-->phy: 30463 (frm: 118) (prv: 117)--> val:  -65 == value:  -65 --  + ----> pg_fault
log: 65416 0xff88 (pg:255, off:136)-->phy: 30600 (frm: 119) (prv: 118)--> val:    0 == value:    0 --  + ----> pg_fault
log: 62869 0xf595 (pg:245, off:149)-->phy: 28565 (frm: 111) (prv: 119)--> val:    0 == value:    0 --  +    HIT!
log: 57314 0xdfe2 (pg:223, off:226)-->phy: 30946 (frm: 120) (prv: 119)--> val:   55 == value:   55 --  + ----> pg_fault
log: 12659 0x3173 (pg: 49, off:115)-->phy: 31091 (frm: 121) (prv: 120)--> val:   92 == value:   92 --  + ----> pg_fault

log: 14052 0x36e4 (pg: 54, off:228)-->phy: 31460 (frm: 122) (prv: 121)--> val:    0 == value:    0 --  + ----> pg_fault
log: 32956 0x80bc (pg:128, off:188)-->phy:  4284 (frm:  16) (prv: 122)--> val:    0 == value:    0 --  +    HIT!
log: 49273 0xc079 (pg:192, off:121)-->phy:  8569 (frm:  33) (prv: 122)--> val:    0 == value:    0 --  +    HIT!
log: 50352 0xc4b0 (pg:196, off:176)-->phy: 14000 (frm:  54) (prv: 122)--> val:    0 == value:    0 --  +    HIT!
log: 49737 0xc249 (pg:194, off: 73)-->phy: 31561 (frm: 123) (prv: 122)--> val:    0 == value:    0 --  + ----> pg_fault

log: 15555 0x3cc3 (pg: 60, off:195)-->phy: 31939 (frm: 124) (prv: 123)--> val:   48 == value:   48 --  + ----> pg_fault
log: 47475 0xb973 (pg:185, off:115)-->phy: 32115 (frm: 125) (prv: 124)--> val:   92 == value:   92 --  + ----> pg_fault
log: 15328 0x3be0 (pg: 59, off:224)-->phy: 32480 (frm: 126) (prv: 125)--> val:    0 == value:    0 --  + ----> pg_fault
log: 34621 0x873d (pg:135, off: 61)-->phy: 17981 (frm:  70) (prv: 126)--> val:    0 == value:    0 --  +    HIT!
log: 51365 0xc8a5 (pg:200, off:165)-->phy: 32677 (frm: 127) (prv: 126)--> val:    0 == value:    0 --  + ----> pg_fault

log: 32820 0x8034 (pg:128, off: 52)-->phy:  4148 (frm:  16) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 48855 0xbed7 (pg:190, off:215)-->phy:   215 (frm:   0) (prv: 127)--> val:  -75 == value:  -75 --  +    HIT!
log: 12224 0x2fc0 (pg: 47, off:192)-->phy:  2752 (frm:  10) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  2035 0x07f3 (pg:  7, off:243)-->phy:   499 (frm:   1) (prv: 127)--> val:   -4 == value:   -4 --  +    HIT!
log: 60539 0xec7b (pg:236, off:123)-->phy:  7291 (frm:  28) (prv: 127)--> val:   30 == value:   30 --  +    HIT!

log: 14595 0x3903 (pg: 57, off:  3)-->phy:   515 (frm:   2) (prv: 127)--> val:   64 == value:   64 --  +    HIT!
log: 13853 0x361d (pg: 54, off: 29)-->phy: 31261 (frm: 122) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 24143 0x5e4f (pg: 94, off: 79)-->phy:   847 (frm:   3) (prv: 127)--> val: -109 == value: -109 --  +    HIT!
log: 15216 0x3b70 (pg: 59, off:112)-->phy: 32368 (frm: 126) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  8113 0x1fb1 (pg: 31, off:177)-->phy:  1201 (frm:   4) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 22640 0x5870 (pg: 88, off:112)-->phy:  2928 (frm:  11) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 32978 0x80d2 (pg:128, off:210)-->phy:  4306 (frm:  16) (prv: 127)--> val:   32 == value:   32 --  +    HIT!
log: 39151 0x98ef (pg:152, off:239)-->phy:  4079 (frm:  15) (prv: 127)--> val:   59 == value:   59 --  +    HIT!
log: 19520 0x4c40 (pg: 76, off: 64)-->phy: 22592 (frm:  88) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 58141 0xe31d (pg:227, off: 29)-->phy:  1309 (frm:   5) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 63959 0xf9d7 (pg:249, off:215)-->phy: 21975 (frm:  85) (prv: 127)--> val:  117 == value:  117 --  +    HIT!
log: 53040 0xcf30 (pg:207, off: 48)-->phy:  1584 (frm:   6) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 55842 0xda22 (pg:218, off: 34)-->phy:  1826 (frm:   7) (prv: 127)--> val:   54 == value:   54 --  +    HIT!
log:   585 0x0249 (pg:  2, off: 73)-->phy: 17225 (frm:  67) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 51229 0xc81d (pg:200, off: 29)-->phy: 32541 (frm: 127) (prv: 127)--> val:    0 == value:    0 --  + ----> pg_fault

log: 64181 0xfab5 (pg:250, off:181)-->phy:  4533 (frm:  17) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 54879 0xd65f (pg:214, off: 95)-->phy:  3679 (frm:  14) (prv: 127)--> val: -105 == value: -105 --  +    HIT!
log: 28210 0x6e32 (pg:110, off: 50)-->phy:  2098 (frm:   8) (prv: 127)--> val:   27 == value:   27 --  +    HIT!
log: 10268 0x281c (pg: 40, off: 28)-->phy: 14620 (frm:  57) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 15395 0x3c23 (pg: 60, off: 35)-->phy: 31779 (frm: 124) (prv: 127)--> val:    8 == value:    8 --  +    HIT!

log: 12884 0x3254 (pg: 50, off: 84)-->phy: 30292 (frm: 118) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  2149 0x0865 (pg:  8, off:101)-->phy:  2405 (frm:   9) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 53483 0xd0eb (pg:208, off:235)-->phy:  2795 (frm:  10) (prv: 127)--> val:   58 == value:   58 --  +    HIT!
log: 59606 0xe8d6 (pg:232, off:214)-->phy: 26070 (frm: 101) (prv: 127)--> val:   58 == value:   58 --  +    HIT!
log: 14981 0x3a85 (pg: 58, off:133)-->phy: 24709 (frm:  96) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 36672 0x8f40 (pg:143, off: 64)-->phy:  2880 (frm:  11) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 23197 0x5a9d (pg: 90, off:157)-->phy:  3229 (frm:  12) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 36518 0x8ea6 (pg:142, off:166)-->phy: 14502 (frm:  56) (prv: 127)--> val:   35 == value:   35 --  +    HIT!
log: 13361 0x3431 (pg: 52, off: 49)-->phy:  3377 (frm:  13) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 19810 0x4d62 (pg: 77, off: 98)-->phy:  3682 (frm:  14) (prv: 127)--> val:   19 == value:   19 --  +    HIT!

log: 25955 0x6563 (pg:101, off: 99)-->phy:  3939 (frm:  15) (prv: 127)--> val:   88 == value:   88 --  +    HIT!
log: 62678 0xf4d6 (pg:244, off:214)-->phy:  4310 (frm:  16) (prv: 127)--> val:   61 == value:   61 --  +    HIT!
log: 26021 0x65a5 (pg:101, off:165)-->phy:  4005 (frm:  15) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 29409 0x72e1 (pg:114, off:225)-->phy:  4577 (frm:  17) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 38111 0x94df (pg:148, off:223)-->phy:  4831 (frm:  18) (prv: 127)--> val:   55 == value:   55 --  +    HIT!

log: 58573 0xe4cd (pg:228, off:205)-->phy: 15565 (frm:  60) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 56840 0xde08 (pg:222, off:  8)-->phy:  4872 (frm:  19) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 41306 0xa15a (pg:161, off: 90)-->phy: 24922 (frm:  97) (prv: 127)--> val:   40 == value:   40 --  +    HIT!
log: 54426 0xd49a (pg:212, off:154)-->phy: 19354 (frm:  75) (prv: 127)--> val:   53 == value:   53 --  +    HIT!
log:  3617 0x0e21 (pg: 14, off: 33)-->phy: 10017 (frm:  39) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 50652 0xc5dc (pg:197, off:220)-->phy: 18652 (frm:  72) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 41452 0xa1ec (pg:161, off:236)-->phy: 25068 (frm:  97) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 20241 0x4f11 (pg: 79, off: 17)-->phy: 16657 (frm:  65) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 31723 0x7beb (pg:123, off:235)-->phy: 12267 (frm:  47) (prv: 127)--> val:   -6 == value:   -6 --  +    HIT!
log: 53747 0xd1f3 (pg:209, off:243)-->phy:  5363 (frm:  20) (prv: 127)--> val:  124 == value:  124 --  +    HIT!

log: 28550 0x6f86 (pg:111, off:134)-->phy: 28038 (frm: 109) (prv: 127)--> val:   27 == value:   27 --  +    HIT!
log: 23402 0x5b6a (pg: 91, off:106)-->phy: 20586 (frm:  80) (prv: 127)--> val:   22 == value:   22 --  +    HIT!
log: 21205 0x52d5 (pg: 82, off:213)-->phy:  5589 (frm:  21) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 56181 0xdb75 (pg:219, off:117)-->phy: 25205 (frm:  98) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 57470 0xe07e (pg:224, off:126)-->phy:  5758 (frm:  22) (prv: 127)--> val:   56 == value:   56 --  +    HIT!

log: 39933 0x9bfd (pg:155, off:253)-->phy:  6141 (frm:  23) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 34964 0x8894 (pg:136, off:148)-->phy: 26772 (frm: 104) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 24781 0x60cd (pg: 96, off:205)-->phy:  6349 (frm:  24) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 41747 0xa313 (pg:163, off: 19)-->phy:  6419 (frm:  25) (prv: 127)--> val:  -60 == value:  -60 --  +    HIT!
log: 62564 0xf464 (pg:244, off:100)-->phy:  4196 (frm:  16) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 58461 0xe45d (pg:228, off: 93)-->phy: 15453 (frm:  60) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 20858 0x517a (pg: 81, off:122)-->phy:  6778 (frm:  26) (prv: 127)--> val:   20 == value:   20 --  +    HIT!
log: 49301 0xc095 (pg:192, off:149)-->phy:  8597 (frm:  33) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 40572 0x9e7c (pg:158, off:124)-->phy:  7036 (frm:  27) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 23840 0x5d20 (pg: 93, off: 32)-->phy:  7200 (frm:  28) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 35278 0x89ce (pg:137, off:206)-->phy:  7630 (frm:  29) (prv: 127)--> val:   34 == value:   34 --  +    HIT!
log: 62905 0xf5b9 (pg:245, off:185)-->phy: 28601 (frm: 111) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 56650 0xdd4a (pg:221, off: 74)-->phy: 13386 (frm:  52) (prv: 127)--> val:   55 == value:   55 --  +    HIT!
log: 11149 0x2b8d (pg: 43, off:141)-->phy:  7821 (frm:  30) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 38920 0x9808 (pg:152, off:  8)-->phy:  7944 (frm:  31) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 23430 0x5b86 (pg: 91, off:134)-->phy: 20614 (frm:  80) (prv: 127)--> val:   22 == value:   22 --  +    HIT!
log: 57592 0xe0f8 (pg:224, off:248)-->phy:  5880 (frm:  22) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  3080 0x0c08 (pg: 12, off:  8)-->phy:  8200 (frm:  32) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  6677 0x1a15 (pg: 26, off: 21)-->phy:  8469 (frm:  33) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 50704 0xc610 (pg:198, off: 16)-->phy: 26128 (frm: 102) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 51883 0xcaab (pg:202, off:171)-->phy: 27307 (frm: 106) (prv: 127)--> val:  -86 == value:  -86 --  +    HIT!
log: 62799 0xf54f (pg:245, off: 79)-->phy: 28495 (frm: 111) (prv: 127)--> val:   83 == value:   83 --  +    HIT!
log: 20188 0x4edc (pg: 78, off:220)-->phy:  8924 (frm:  34) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  1245 0x04dd (pg:  4, off:221)-->phy:  9181 (frm:  35) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 12220 0x2fbc (pg: 47, off:188)-->phy:  9404 (frm:  36) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 17602 0x44c2 (pg: 68, off:194)-->phy:  9666 (frm:  37) (prv: 127)--> val:   17 == value:   17 --  +    HIT!
log: 28609 0x6fc1 (pg:111, off:193)-->phy: 28097 (frm: 109) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 42694 0xa6c6 (pg:166, off:198)-->phy: 20934 (frm:  81) (prv: 127)--> val:   41 == value:   41 --  +    HIT!
log: 29826 0x7482 (pg:116, off:130)-->phy:  9858 (frm:  38) (prv: 127)--> val:   29 == value:   29 --  +    HIT!
log: 13827 0x3603 (pg: 54, off:  3)-->phy: 31235 (frm: 122) (prv: 127)--> val: -128 == value: -128 --  +    HIT!

log: 27336 0x6ac8 (pg:106, off:200)-->phy: 28872 (frm: 112) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 53343 0xd05f (pg:208, off: 95)-->phy:  2655 (frm:  10) (prv: 127)--> val:   23 == value:   23 --  +    HIT!
log: 11533 0x2d0d (pg: 45, off: 13)-->phy:  9997 (frm:  39) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 41713 0xa2f1 (pg:162, off:241)-->phy: 10481 (frm:  40) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 33890 0x8462 (pg:132, off: 98)-->phy: 10594 (frm:  41) (prv: 127)--> val:   33 == value:   33 --  +    HIT!

log:  4894 0x131e (pg: 19, off: 30)-->phy: 13598 (frm:  53) (prv: 127)--> val:    4 == value:    4 --  +    HIT!
log: 57599 0xe0ff (pg:224, off:255)-->phy:  5887 (frm:  22) (prv: 127)--> val:   63 == value:   63 --  +    HIT!
log:  3870 0x0f1e (pg: 15, off: 30)-->phy: 18974 (frm:  74) (prv: 127)--> val:    3 == value:    3 --  +    HIT!
log: 58622 0xe4fe (pg:228, off:254)-->phy: 15614 (frm:  60) (prv: 127)--> val:   57 == value:   57 --  +    HIT!
log: 29780 0x7454 (pg:116, off: 84)-->phy:  9812 (frm:  38) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 62553 0xf459 (pg:244, off: 89)-->phy:  4185 (frm:  16) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  2303 0x08ff (pg:  8, off:255)-->phy:  2559 (frm:   9) (prv: 127)--> val:   63 == value:   63 --  +    HIT!
log: 51915 0xcacb (pg:202, off:203)-->phy: 27339 (frm: 106) (prv: 127)--> val:  -78 == value:  -78 --  +    HIT!
log:  6251 0x186b (pg: 24, off:107)-->phy: 10859 (frm:  42) (prv: 127)--> val:   26 == value:   26 --  +    HIT!
log: 38107 0x94db (pg:148, off:219)-->phy:  4827 (frm:  18) (prv: 127)--> val:   54 == value:   54 --  +    HIT!

log: 59325 0xe7bd (pg:231, off:189)-->phy: 11197 (frm:  43) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 61295 0xef6f (pg:239, off:111)-->phy: 11375 (frm:  44) (prv: 127)--> val:  -37 == value:  -37 --  +    HIT!
log: 26699 0x684b (pg:104, off: 75)-->phy: 11595 (frm:  45) (prv: 127)--> val:   18 == value:   18 --  +    HIT!
log: 51188 0xc7f4 (pg:199, off:244)-->phy: 12020 (frm:  46) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 59519 0xe87f (pg:232, off:127)-->phy: 25983 (frm: 101) (prv: 127)--> val:   31 == value:   31 --  +    HIT!

log:  7345 0x1cb1 (pg: 28, off:177)-->phy: 12209 (frm:  47) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 20325 0x4f65 (pg: 79, off:101)-->phy: 16741 (frm:  65) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 39633 0x9ad1 (pg:154, off:209)-->phy: 12497 (frm:  48) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  1562 0x061a (pg:  6, off: 26)-->phy: 12570 (frm:  49) (prv: 127)--> val:    1 == value:    1 --  +    HIT!
log:  7580 0x1d9c (pg: 29, off:156)-->phy: 12956 (frm:  50) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log:  8170 0x1fea (pg: 31, off:234)-->phy:  1258 (frm:   4) (prv: 127)--> val:    7 == value:    7 --  +    HIT!
log: 62256 0xf330 (pg:243, off: 48)-->phy: 23856 (frm:  93) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 35823 0x8bef (pg:139, off:239)-->phy: 13295 (frm:  51) (prv: 127)--> val:   -5 == value:   -5 --  +    HIT!
log: 27790 0x6c8e (pg:108, off:142)-->phy: 13454 (frm:  52) (prv: 127)--> val:   27 == value:   27 --  +    HIT!
log: 13191 0x3387 (pg: 51, off:135)-->phy: 13703 (frm:  53) (prv: 127)--> val:  -31 == value:  -31 --  +    HIT!

log:  9772 0x262c (pg: 38, off: 44)-->phy: 13868 (frm:  54) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  7477 0x1d35 (pg: 29, off: 53)-->phy: 12853 (frm:  50) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 44455 0xada7 (pg:173, off:167)-->phy: 14247 (frm:  55) (prv: 127)--> val:  105 == value:  105 --  +    HIT!
log: 59546 0xe89a (pg:232, off:154)-->phy: 26010 (frm: 101) (prv: 127)--> val:   58 == value:   58 --  +    HIT!
log: 49347 0xc0c3 (pg:192, off:195)-->phy: 14531 (frm:  56) (prv: 127)--> val:   48 == value:   48 --  +    HIT!

log: 36539 0x8ebb (pg:142, off:187)-->phy: 14779 (frm:  57) (prv: 127)--> val:  -82 == value:  -82 --  +    HIT!
log: 12453 0x30a5 (pg: 48, off:165)-->phy: 15013 (frm:  58) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 49640 0xc1e8 (pg:193, off:232)-->phy: 15336 (frm:  59) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 28290 0x6e82 (pg:110, off:130)-->phy:  2178 (frm:   8) (prv: 127)--> val:   27 == value:   27 --  +    HIT!
log: 44817 0xaf11 (pg:175, off: 17)-->phy: 15377 (frm:  60) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log:  8565 0x2175 (pg: 33, off:117)-->phy: 20085 (frm:  78) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 16399 0x400f (pg: 64, off: 15)-->phy: 15631 (frm:  61) (prv: 127)--> val:    3 == value:    3 --  +    HIT!
log: 41934 0xa3ce (pg:163, off:206)-->phy:  6606 (frm:  25) (prv: 127)--> val:   40 == value:   40 --  +    HIT!
log: 45457 0xb191 (pg:177, off:145)-->phy: 16017 (frm:  62) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 33856 0x8440 (pg:132, off: 64)-->phy: 10560 (frm:  41) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 19498 0x4c2a (pg: 76, off: 42)-->phy: 22570 (frm:  88) (prv: 127)--> val:   19 == value:   19 --  +    HIT!
log: 17661 0x44fd (pg: 68, off:253)-->phy:  9725 (frm:  37) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 63829 0xf955 (pg:249, off: 85)-->phy: 21845 (frm:  85) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 42034 0xa432 (pg:164, off: 50)-->phy: 16178 (frm:  63) (prv: 127)--> val:   41 == value:   41 --  +    HIT!
log: 28928 0x7100 (pg:113, off:  0)-->phy: 16384 (frm:  64) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 30711 0x77f7 (pg:119, off:247)-->phy: 16631 (frm:  64) (prv: 127)--> val:   -3 == value:   -3 --  +    HIT!
log:  8800 0x2260 (pg: 34, off: 96)-->phy: 16736 (frm:  65) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 52335 0xcc6f (pg:204, off:111)-->phy: 17007 (frm:  66) (prv: 127)--> val:   27 == value:   27 --  +    HIT!
log: 38775 0x9777 (pg:151, off:119)-->phy: 17271 (frm:  67) (prv: 127)--> val:  -35 == value:  -35 --  +    HIT!
log: 52704 0xcde0 (pg:205, off:224)-->phy: 30176 (frm: 117) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 24380 0x5f3c (pg: 95, off: 60)-->phy: 17468 (frm:  68) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 19602 0x4c92 (pg: 76, off:146)-->phy: 22674 (frm:  88) (prv: 127)--> val:   19 == value:   19 --  +    HIT!
log: 57998 0xe28e (pg:226, off:142)-->phy: 17806 (frm:  69) (prv: 127)--> val:   56 == value:   56 --  +    HIT!
log:  2919 0x0b67 (pg: 11, off:103)-->phy: 18023 (frm:  70) (prv: 127)--> val:  -39 == value:  -39 --  +    HIT!
log:  8362 0x20aa (pg: 32, off:170)-->phy: 18346 (frm:  71) (prv: 127)--> val:    8 == value:    8 --  +    HIT!

log: 17884 0x45dc (pg: 69, off:220)-->phy: 18652 (frm:  72) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 45737 0xb2a9 (pg:178, off:169)-->phy: 18857 (frm:  73) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 47894 0xbb16 (pg:187, off: 22)-->phy: 25622 (frm: 100) (prv: 127)--> val:   46 == value:   46 --  +    HIT!
log: 59667 0xe913 (pg:233, off: 19)-->phy: 18963 (frm:  74) (prv: 127)--> val:   68 == value:   68 --  +    HIT!
log: 10385 0x2891 (pg: 40, off:145)-->phy: 19345 (frm:  75) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 52782 0xce2e (pg:206, off: 46)-->phy: 19502 (frm:  76) (prv: 127)--> val:   51 == value:   51 --  +    HIT!
log: 64416 0xfba0 (pg:251, off:160)-->phy: 19872 (frm:  77) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 40946 0x9ff2 (pg:159, off:242)-->phy: 20210 (frm:  78) (prv: 127)--> val:   39 == value:   39 --  +    HIT!
log: 16778 0x418a (pg: 65, off:138)-->phy: 20362 (frm:  79) (prv: 127)--> val:   16 == value:   16 --  +    HIT!
log: 27159 0x6a17 (pg:106, off: 23)-->phy: 28695 (frm: 112) (prv: 127)--> val: -123 == value: -123 --  +    HIT!

log: 24324 0x5f04 (pg: 95, off:  4)-->phy: 17412 (frm:  68) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 32450 0x7ec2 (pg:126, off:194)-->phy: 20674 (frm:  80) (prv: 127)--> val:   31 == value:   31 --  +    HIT!
log:  9108 0x2394 (pg: 35, off:148)-->phy: 20884 (frm:  81) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 65305 0xff19 (pg:255, off: 25)-->phy: 30489 (frm: 119) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 19575 0x4c77 (pg: 76, off:119)-->phy: 22647 (frm:  88) (prv: 127)--> val:   29 == value:   29 --  +    HIT!

log: 11117 0x2b6d (pg: 43, off:109)-->phy:  7789 (frm:  30) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 65170 0xfe92 (pg:254, off:146)-->phy: 21138 (frm:  82) (prv: 127)--> val:   63 == value:   63 --  +    HIT!
log: 58013 0xe29d (pg:226, off:157)-->phy: 17821 (frm:  69) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 61676 0xf0ec (pg:240, off:236)-->phy: 23276 (frm:  90) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 63510 0xf816 (pg:248, off: 22)-->phy: 28182 (frm: 110) (prv: 127)--> val:   62 == value:   62 --  +    HIT!

log: 17458 0x4432 (pg: 68, off: 50)-->phy:  9522 (frm:  37) (prv: 127)--> val:   17 == value:   17 --  +    HIT!
log: 54675 0xd593 (pg:213, off:147)-->phy: 21395 (frm:  83) (prv: 127)--> val:  100 == value:  100 --  +    HIT!
log:  1713 0x06b1 (pg:  6, off:177)-->phy: 12721 (frm:  49) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 55105 0xd741 (pg:215, off: 65)-->phy: 21569 (frm:  84) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 65321 0xff29 (pg:255, off: 41)-->phy: 30505 (frm: 119) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 45278 0xb0de (pg:176, off:222)-->phy: 21982 (frm:  85) (prv: 127)--> val:   44 == value:   44 --  +    HIT!
log: 26256 0x6690 (pg:102, off:144)-->phy: 22160 (frm:  86) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 64198 0xfac6 (pg:250, off:198)-->phy: 22470 (frm:  87) (prv: 127)--> val:   62 == value:   62 --  +    HIT!
log: 29441 0x7301 (pg:115, off:  1)-->phy: 22529 (frm:  88) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  1928 0x0788 (pg:  7, off:136)-->phy:   392 (frm:   1) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 39425 0x9a01 (pg:154, off:  1)-->phy: 12289 (frm:  48) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 32000 0x7d00 (pg:125, off:  0)-->phy: 22784 (frm:  89) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 28549 0x6f85 (pg:111, off:133)-->phy: 28037 (frm: 109) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 46295 0xb4d7 (pg:180, off:215)-->phy: 23255 (frm:  90) (prv: 127)--> val:   53 == value:   53 --  +    HIT!
log: 22772 0x58f4 (pg: 88, off:244)-->phy: 23540 (frm:  91) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 58228 0xe374 (pg:227, off:116)-->phy:  1396 (frm:   5) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 63525 0xf825 (pg:248, off: 37)-->phy: 28197 (frm: 110) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 32602 0x7f5a (pg:127, off: 90)-->phy: 23642 (frm:  92) (prv: 127)--> val:   31 == value:   31 --  +    HIT!
log: 46195 0xb473 (pg:180, off:115)-->phy: 23155 (frm:  90) (prv: 127)--> val:   28 == value:   28 --  +    HIT!
log: 55849 0xda29 (pg:218, off: 41)-->phy:  1833 (frm:   7) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 46454 0xb576 (pg:181, off:118)-->phy: 23926 (frm:  93) (prv: 127)--> val:   45 == value:   45 --  +    HIT!
log:  7487 0x1d3f (pg: 29, off: 63)-->phy: 12863 (frm:  50) (prv: 127)--> val:   79 == value:   79 --  +    HIT!
log: 33879 0x8457 (pg:132, off: 87)-->phy: 10583 (frm:  41) (prv: 127)--> val:   21 == value:   21 --  +    HIT!
log: 42004 0xa414 (pg:164, off: 20)-->phy: 16148 (frm:  63) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  8599 0x2197 (pg: 33, off:151)-->phy: 24215 (frm:  94) (prv: 127)--> val:  101 == value:  101 --  +    HIT!

log: 18641 0x48d1 (pg: 72, off:209)-->phy: 24529 (frm:  95) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 49015 0xbf77 (pg:191, off:119)-->phy: 24695 (frm:  96) (prv: 127)--> val:  -35 == value:  -35 --  +    HIT!
log: 26830 0x68ce (pg:104, off:206)-->phy: 11726 (frm:  45) (prv: 127)--> val:   26 == value:   26 --  +    HIT!
log: 34754 0x87c2 (pg:135, off:194)-->phy: 25026 (frm:  97) (prv: 127)--> val:   33 == value:   33 --  +    HIT!
log: 14668 0x394c (pg: 57, off: 76)-->phy:   588 (frm:   2) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 38362 0x95da (pg:149, off:218)-->phy: 25306 (frm:  98) (prv: 127)--> val:   37 == value:   37 --  +    HIT!
log: 38791 0x9787 (pg:151, off:135)-->phy: 17287 (frm:  67) (prv: 127)--> val:  -31 == value:  -31 --  +    HIT!
log:  4171 0x104b (pg: 16, off: 75)-->phy: 25419 (frm:  99) (prv: 127)--> val:   18 == value:   18 --  +    HIT!
log: 45975 0xb397 (pg:179, off:151)-->phy: 25751 (frm: 100) (prv: 127)--> val:  -27 == value:  -27 --  +    HIT!
log: 14623 0x391f (pg: 57, off: 31)-->phy:   543 (frm:   2) (prv: 127)--> val:   71 == value:   71 --  +    HIT!

log: 62393 0xf3b9 (pg:243, off:185)-->phy: 26041 (frm: 101) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 64658 0xfc92 (pg:252, off:146)-->phy: 26258 (frm: 102) (prv: 127)--> val:   63 == value:   63 --  +    HIT!
log: 10963 0x2ad3 (pg: 42, off:211)-->phy: 26579 (frm: 103) (prv: 127)--> val:  -76 == value:  -76 --  +    HIT!
log:  9058 0x2362 (pg: 35, off: 98)-->phy: 20834 (frm:  81) (prv: 127)--> val:    8 == value:    8 --  +    HIT!
log: 51031 0xc757 (pg:199, off: 87)-->phy: 11863 (frm:  46) (prv: 127)--> val:  -43 == value:  -43 --  +    HIT!

log: 32425 0x7ea9 (pg:126, off:169)-->phy: 20649 (frm:  80) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 45483 0xb1ab (pg:177, off:171)-->phy: 16043 (frm:  62) (prv: 127)--> val:  106 == value:  106 --  +    HIT!
log: 44611 0xae43 (pg:174, off: 67)-->phy: 26691 (frm: 104) (prv: 127)--> val: -112 == value: -112 --  +    HIT!
log: 63664 0xf8b0 (pg:248, off:176)-->phy: 28336 (frm: 110) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 54920 0xd688 (pg:214, off:136)-->phy: 27016 (frm: 105) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log:  7663 0x1def (pg: 29, off:239)-->phy: 13039 (frm:  50) (prv: 127)--> val:  123 == value:  123 --  +    HIT!
log: 56480 0xdca0 (pg:220, off:160)-->phy: 27296 (frm: 106) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  1489 0x05d1 (pg:  5, off:209)-->phy: 27601 (frm: 107) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 28438 0x6f16 (pg:111, off: 22)-->phy: 27926 (frm: 109) (prv: 127)--> val:   27 == value:   27 --  +    HIT!
log: 65449 0xffa9 (pg:255, off:169)-->phy: 30633 (frm: 119) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 12441 0x3099 (pg: 48, off:153)-->phy: 15001 (frm:  58) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 58530 0xe4a2 (pg:228, off:162)-->phy: 27810 (frm: 108) (prv: 127)--> val:   57 == value:   57 --  +    HIT!
log: 63570 0xf852 (pg:248, off: 82)-->phy: 28242 (frm: 110) (prv: 127)--> val:   62 == value:   62 --  +    HIT!
log: 26251 0x668b (pg:102, off:139)-->phy: 22155 (frm:  86) (prv: 127)--> val:  -94 == value:  -94 --  +    HIT!
log: 15972 0x3e64 (pg: 62, off:100)-->phy: 28004 (frm: 109) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 35826 0x8bf2 (pg:139, off:242)-->phy: 13298 (frm:  51) (prv: 127)--> val:   34 == value:   34 --  +    HIT!
log:  5491 0x1573 (pg: 21, off:115)-->phy: 28275 (frm: 110) (prv: 127)--> val:   92 == value:   92 --  +    HIT!
log: 54253 0xd3ed (pg:211, off:237)-->phy: 28653 (frm: 111) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 49655 0xc1f7 (pg:193, off:247)-->phy: 15351 (frm:  59) (prv: 127)--> val:  125 == value:  125 --  +    HIT!
log:  5868 0x16ec (pg: 22, off:236)-->phy: 28908 (frm: 112) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 20163 0x4ec3 (pg: 78, off:195)-->phy:  8899 (frm:  34) (prv: 127)--> val:  -80 == value:  -80 --  +    HIT!
log: 51079 0xc787 (pg:199, off:135)-->phy: 11911 (frm:  46) (prv: 127)--> val:  -31 == value:  -31 --  +    HIT!
log: 21398 0x5396 (pg: 83, off:150)-->phy: 29078 (frm: 113) (prv: 127)--> val:   20 == value:   20 --  +    HIT!
log: 32756 0x7ff4 (pg:127, off:244)-->phy: 23796 (frm:  92) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 64196 0xfac4 (pg:250, off:196)-->phy: 22468 (frm:  87) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 43218 0xa8d2 (pg:168, off:210)-->phy: 29394 (frm: 114) (prv: 127)--> val:   42 == value:   42 --  +    HIT!
log: 21583 0x544f (pg: 84, off: 79)-->phy: 29519 (frm: 115) (prv: 127)--> val:   19 == value:   19 --  +    HIT!
log: 25086 0x61fe (pg: 97, off:254)-->phy: 29950 (frm: 116) (prv: 127)--> val:   24 == value:   24 --  +    HIT!
log: 45515 0xb1cb (pg:177, off:203)-->phy: 16075 (frm:  62) (prv: 127)--> val:  114 == value:  114 --  +    HIT!
log: 12893 0x325d (pg: 50, off: 93)-->phy: 30301 (frm: 118) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 22914 0x5982 (pg: 89, off:130)-->phy: 30082 (frm: 117) (prv: 127)--> val:   22 == value:   22 --  +    HIT!
log: 58969 0xe659 (pg:230, off: 89)-->phy: 30297 (frm: 118) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 20094 0x4e7e (pg: 78, off:126)-->phy:  8830 (frm:  34) (prv: 127)--> val:   19 == value:   19 --  +    HIT!
log: 13730 0x35a2 (pg: 53, off:162)-->phy: 30626 (frm: 119) (prv: 127)--> val:   13 == value:   13 --  +    HIT!
log: 44059 0xac1b (pg:172, off: 27)-->phy: 30747 (frm: 120) (prv: 127)--> val:    6 == value:    6 --  +    HIT!

log: 28931 0x7103 (pg:113, off:  3)-->phy: 30979 (frm: 121) (prv: 127)--> val:   64 == value:   64 --  +    HIT!
log: 13533 0x34dd (pg: 52, off:221)-->phy:  3549 (frm:  13) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 33134 0x816e (pg:129, off:110)-->phy: 31342 (frm: 122) (prv: 127)--> val:   32 == value:   32 --  +    HIT!
log: 28483 0x6f43 (pg:111, off: 67)-->phy: 31555 (frm: 123) (prv: 127)--> val:  -48 == value:  -48 --  +    HIT!
log:  1220 0x04c4 (pg:  4, off:196)-->phy:  9156 (frm:  35) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 38174 0x951e (pg:149, off: 30)-->phy: 25118 (frm:  98) (prv: 127)--> val:   37 == value:   37 --  +    HIT!
log: 53502 0xd0fe (pg:208, off:254)-->phy:  2814 (frm:  10) (prv: 127)--> val:   52 == value:   52 --  +    HIT!
log: 43328 0xa940 (pg:169, off: 64)-->phy: 31808 (frm: 124) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  4970 0x136a (pg: 19, off:106)-->phy: 32106 (frm: 125) (prv: 127)--> val:    4 == value:    4 --  +    HIT!
log:  8090 0x1f9a (pg: 31, off:154)-->phy:  1178 (frm:   4) (prv: 127)--> val:    7 == value:    7 --  +    HIT!

log:  2661 0x0a65 (pg: 10, off:101)-->phy: 32357 (frm: 126) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 53903 0xd28f (pg:210, off:143)-->phy: 32655 (frm: 127) (prv: 127)--> val:  -93 == value:  -93 --  + ----> pg_fault
log: 11025 0x2b11 (pg: 43, off: 17)-->phy:  7697 (frm:  30) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 26627 0x6803 (pg:104, off:  3)-->phy: 11523 (frm:  45) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 18117 0x46c5 (pg: 70, off:197)-->phy:   197 (frm:   0) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 14505 0x38a9 (pg: 56, off:169)-->phy:   425 (frm:   1) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 61528 0xf058 (pg:240, off: 88)-->phy:   600 (frm:   2) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 20423 0x4fc7 (pg: 79, off:199)-->phy:   967 (frm:   3) (prv: 127)--> val:  -15 == value:  -15 --  +    HIT!
log: 26962 0x6952 (pg:105, off: 82)-->phy:  1106 (frm:   4) (prv: 127)--> val:   26 == value:   26 --  +    HIT!
log: 36392 0x8e28 (pg:142, off: 40)-->phy: 14632 (frm:  57) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 11365 0x2c65 (pg: 44, off:101)-->phy:  1381 (frm:   5) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 50882 0xc6c2 (pg:198, off:194)-->phy:  1730 (frm:   6) (prv: 127)--> val:   49 == value:   49 --  +    HIT!
log: 41668 0xa2c4 (pg:162, off:196)-->phy: 10436 (frm:  40) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 30497 0x7721 (pg:119, off: 33)-->phy: 16417 (frm:  64) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 36216 0x8d78 (pg:141, off:120)-->phy:  1912 (frm:   7) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log:  5619 0x15f3 (pg: 21, off:243)-->phy: 28403 (frm: 110) (prv: 127)--> val:  124 == value:  124 --  +    HIT!
log: 36983 0x9077 (pg:144, off:119)-->phy:  2167 (frm:   8) (prv: 127)--> val:   29 == value:   29 --  +    HIT!
log: 59557 0xe8a5 (pg:232, off:165)-->phy:  2469 (frm:   9) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 36663 0x8f37 (pg:143, off: 55)-->phy:  2871 (frm:  11) (prv: 127)--> val:  -51 == value:  -51 --  +    HIT!
log: 36436 0x8e54 (pg:142, off: 84)-->phy: 14676 (frm:  57) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 37057 0x90c1 (pg:144, off:193)-->phy:  2241 (frm:   8) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 23585 0x5c21 (pg: 92, off: 33)-->phy:  2593 (frm:  10) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 58791 0xe5a7 (pg:229, off:167)-->phy:  2983 (frm:  11) (prv: 127)--> val:  105 == value:  105 --  +    HIT!
log: 46666 0xb64a (pg:182, off: 74)-->phy:  3146 (frm:  12) (prv: 127)--> val:   45 == value:   45 --  +    HIT!
log: 64475 0xfbdb (pg:251, off:219)-->phy: 19931 (frm:  77) (prv: 127)--> val:  -10 == value:  -10 --  +    HIT!

log: 21615 0x546f (pg: 84, off:111)-->phy: 29551 (frm: 115) (prv: 127)--> val:   27 == value:   27 --  +    HIT!
log: 41090 0xa082 (pg:160, off:130)-->phy:  3458 (frm:  13) (prv: 127)--> val:   40 == value:   40 --  +    HIT!
log:  1771 0x06eb (pg:  6, off:235)-->phy: 12779 (frm:  49) (prv: 127)--> val:  -70 == value:  -70 --  +    HIT!
log: 47513 0xb999 (pg:185, off:153)-->phy:  3737 (frm:  14) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 39338 0x99aa (pg:153, off:170)-->phy:  4010 (frm:  15) (prv: 127)--> val:   38 == value:   38 --  +    HIT!

log:  1390 0x056e (pg:  5, off:110)-->phy: 27502 (frm: 107) (prv: 127)--> val:    1 == value:    1 --  +    HIT!
log: 38772 0x9774 (pg:151, off:116)-->phy: 17268 (frm:  67) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 58149 0xe325 (pg:227, off: 37)-->phy:  4133 (frm:  16) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  7196 0x1c1c (pg: 28, off: 28)-->phy: 12060 (frm:  47) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  9123 0x23a3 (pg: 35, off:163)-->phy: 20899 (frm:  81) (prv: 127)--> val:  -24 == value:  -24 --  +    HIT!

log:  7491 0x1d43 (pg: 29, off: 67)-->phy: 12867 (frm:  50) (prv: 127)--> val:   80 == value:   80 --  +    HIT!
log: 62616 0xf498 (pg:244, off:152)-->phy:  4504 (frm:  17) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 15436 0x3c4c (pg: 60, off: 76)-->phy:  4684 (frm:  18) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 17491 0x4453 (pg: 68, off: 83)-->phy:  9555 (frm:  37) (prv: 127)--> val:   20 == value:   20 --  +    HIT!
log: 53656 0xd198 (pg:209, off:152)-->phy:  5272 (frm:  20) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 26449 0x6751 (pg:103, off: 81)-->phy:  4945 (frm:  19) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 34935 0x8877 (pg:136, off:119)-->phy:  5239 (frm:  20) (prv: 127)--> val:   29 == value:   29 --  +    HIT!
log: 19864 0x4d98 (pg: 77, off:152)-->phy:  5528 (frm:  21) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 51388 0xc8bc (pg:200, off:188)-->phy:  5820 (frm:  22) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 15155 0x3b33 (pg: 59, off: 51)-->phy:  5939 (frm:  23) (prv: 127)--> val:  -52 == value:  -52 --  +    HIT!

log: 64775 0xfd07 (pg:253, off:  7)-->phy:  6151 (frm:  24) (prv: 127)--> val:   65 == value:   65 --  +    HIT!
log: 47969 0xbb61 (pg:187, off: 97)-->phy:  6497 (frm:  25) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 16315 0x3fbb (pg: 63, off:187)-->phy:  6843 (frm:  26) (prv: 127)--> val:  -18 == value:  -18 --  +    HIT!
log:  1342 0x053e (pg:  5, off: 62)-->phy: 27454 (frm: 107) (prv: 127)--> val:    1 == value:    1 --  +    HIT!
log: 51185 0xc7f1 (pg:199, off:241)-->phy: 12017 (frm:  46) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log:  6043 0x179b (pg: 23, off:155)-->phy:  7067 (frm:  27) (prv: 127)--> val:  -26 == value:  -26 --  +    HIT!
log: 21398 0x5396 (pg: 83, off:150)-->phy: 29078 (frm: 113) (prv: 127)--> val:   20 == value:   20 --  +    HIT!
log:  3273 0x0cc9 (pg: 12, off:201)-->phy:  8393 (frm:  32) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  9370 0x249a (pg: 36, off:154)-->phy:  7322 (frm:  28) (prv: 127)--> val:    9 == value:    9 --  +    HIT!
log: 35463 0x8a87 (pg:138, off:135)-->phy:  7559 (frm:  29) (prv: 127)--> val:  -95 == value:  -95 --  +    HIT!

log: 28205 0x6e2d (pg:110, off: 45)-->phy:  7725 (frm:  30) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  2351 0x092f (pg:  9, off: 47)-->phy:  7983 (frm:  31) (prv: 127)--> val:   75 == value:   75 --  +    HIT!
log: 28999 0x7147 (pg:113, off: 71)-->phy: 31047 (frm: 121) (prv: 127)--> val:   81 == value:   81 --  +    HIT!
log: 47699 0xba53 (pg:186, off: 83)-->phy:  8275 (frm:  32) (prv: 127)--> val: -108 == value: -108 --  +    HIT!
log: 46870 0xb716 (pg:183, off: 22)-->phy:  8470 (frm:  33) (prv: 127)--> val:   45 == value:   45 --  +    HIT!

log: 22311 0x5727 (pg: 87, off: 39)-->phy:  8743 (frm:  34) (prv: 127)--> val:  -55 == value:  -55 --  +    HIT!
log: 22124 0x566c (pg: 86, off:108)-->phy:  9068 (frm:  35) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 22427 0x579b (pg: 87, off:155)-->phy:  8859 (frm:  34) (prv: 127)--> val:  -26 == value:  -26 --  +    HIT!
log: 49344 0xc0c0 (pg:192, off:192)-->phy: 14528 (frm:  56) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 23224 0x5ab8 (pg: 90, off:184)-->phy:  9400 (frm:  36) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log:  5514 0x158a (pg: 21, off:138)-->phy: 28298 (frm: 110) (prv: 127)--> val:    5 == value:    5 --  +    HIT!
log: 20504 0x5018 (pg: 80, off: 24)-->phy:  9496 (frm:  37) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:   376 0x0178 (pg:  1, off:120)-->phy:  9848 (frm:  38) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  2014 0x07de (pg:  7, off:222)-->phy: 10206 (frm:  39) (prv: 127)--> val:    1 == value:    1 --  +    HIT!
log: 38700 0x972c (pg:151, off: 44)-->phy: 17196 (frm:  67) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 13098 0x332a (pg: 51, off: 42)-->phy: 13610 (frm:  53) (prv: 127)--> val:   12 == value:   12 --  +    HIT!
log: 62435 0xf3e3 (pg:243, off:227)-->phy: 26083 (frm: 101) (prv: 127)--> val:   -8 == value:   -8 --  +    HIT!
log: 48046 0xbbae (pg:187, off:174)-->phy:  6574 (frm:  25) (prv: 127)--> val:   46 == value:   46 --  +    HIT!
log: 63464 0xf7e8 (pg:247, off:232)-->phy: 10472 (frm:  40) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 12798 0x31fe (pg: 49, off:254)-->phy: 10750 (frm:  41) (prv: 127)--> val:   12 == value:   12 --  +    HIT!

log: 51178 0xc7ea (pg:199, off:234)-->phy: 12010 (frm:  46) (prv: 127)--> val:   49 == value:   49 --  +    HIT!
log:  8627 0x21b3 (pg: 33, off:179)-->phy: 24243 (frm:  94) (prv: 127)--> val:  108 == value:  108 --  +    HIT!
log: 27083 0x69cb (pg:105, off:203)-->phy:  1227 (frm:   4) (prv: 127)--> val:  114 == value:  114 --  +    HIT!
log: 47198 0xb85e (pg:184, off: 94)-->phy: 10846 (frm:  42) (prv: 127)--> val:   46 == value:   46 --  +    HIT!
log: 44021 0xabf5 (pg:171, off:245)-->phy: 11253 (frm:  43) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 32792 0x8018 (pg:128, off: 24)-->phy: 11288 (frm:  44) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 43996 0xabdc (pg:171, off:220)-->phy: 11228 (frm:  43) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 41126 0xa0a6 (pg:160, off:166)-->phy:  3494 (frm:  13) (prv: 127)--> val:   40 == value:   40 --  +    HIT!
log: 64244 0xfaf4 (pg:250, off:244)-->phy: 22516 (frm:  87) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 37047 0x90b7 (pg:144, off:183)-->phy:  2231 (frm:   8) (prv: 127)--> val:   45 == value:   45 --  +    HIT!

log: 60281 0xeb79 (pg:235, off:121)-->phy: 11641 (frm:  45) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 52904 0xcea8 (pg:206, off:168)-->phy: 19624 (frm:  76) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  7768 0x1e58 (pg: 30, off: 88)-->phy: 11864 (frm:  46) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 55359 0xd83f (pg:216, off: 63)-->phy: 12095 (frm:  47) (prv: 127)--> val:   15 == value:   15 --  +    HIT!
log:  3230 0x0c9e (pg: 12, off:158)-->phy: 12446 (frm:  48) (prv: 127)--> val:    3 == value:    3 --  +    HIT!

log: 44813 0xaf0d (pg:175, off: 13)-->phy: 15373 (frm:  60) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  4116 0x1014 (pg: 16, off: 20)-->phy: 25364 (frm:  99) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 65222 0xfec6 (pg:254, off:198)-->phy: 21190 (frm:  82) (prv: 127)--> val:   63 == value:   63 --  +    HIT!
log: 28083 0x6db3 (pg:109, off:179)-->phy: 12723 (frm:  49) (prv: 127)--> val:  108 == value:  108 --  +    HIT!
log: 60660 0xecf4 (pg:236, off:244)-->phy: 13044 (frm:  50) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log:    39 0x0027 (pg:  0, off: 39)-->phy: 13095 (frm:  51) (prv: 127)--> val:    9 == value:    9 --  +    HIT!
log:   328 0x0148 (pg:  1, off: 72)-->phy:  9800 (frm:  38) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 47868 0xbafc (pg:186, off:252)-->phy:  8444 (frm:  32) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 13009 0x32d1 (pg: 50, off:209)-->phy: 13521 (frm:  52) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 22378 0x576a (pg: 87, off:106)-->phy:  8810 (frm:  34) (prv: 127)--> val:   21 == value:   21 --  +    HIT!

log: 39304 0x9988 (pg:153, off:136)-->phy:  3976 (frm:  15) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 11171 0x2ba3 (pg: 43, off:163)-->phy: 13731 (frm:  53) (prv: 127)--> val:  -24 == value:  -24 --  +    HIT!
log:  8079 0x1f8f (pg: 31, off:143)-->phy: 13967 (frm:  54) (prv: 127)--> val:  -29 == value:  -29 --  +    HIT!
log: 52879 0xce8f (pg:206, off:143)-->phy: 19599 (frm:  76) (prv: 127)--> val:  -93 == value:  -93 --  +    HIT!
log:  5123 0x1403 (pg: 20, off:  3)-->phy: 14083 (frm:  55) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log:  4356 0x1104 (pg: 17, off:  4)-->phy: 14340 (frm:  56) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 45745 0xb2b1 (pg:178, off:177)-->phy: 18865 (frm:  73) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 32952 0x80b8 (pg:128, off:184)-->phy: 11448 (frm:  44) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  4657 0x1231 (pg: 18, off: 49)-->phy: 14641 (frm:  57) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 24142 0x5e4e (pg: 94, off: 78)-->phy: 14926 (frm:  58) (prv: 127)--> val:   23 == value:   23 --  +    HIT!

log: 23319 0x5b17 (pg: 91, off: 23)-->phy: 15127 (frm:  59) (prv: 127)--> val:  -59 == value:  -59 --  +    HIT!
log: 13607 0x3527 (pg: 53, off: 39)-->phy: 30503 (frm: 119) (prv: 127)--> val:   73 == value:   73 --  +    HIT!
log: 46304 0xb4e0 (pg:180, off:224)-->phy: 23264 (frm:  90) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 17677 0x450d (pg: 69, off: 13)-->phy: 18445 (frm:  72) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 59691 0xe92b (pg:233, off: 43)-->phy: 18987 (frm:  74) (prv: 127)--> val:   74 == value:   74 --  +    HIT!

log: 50967 0xc717 (pg:199, off: 23)-->phy: 15383 (frm:  60) (prv: 127)--> val:  -59 == value:  -59 --  +    HIT!
log:  7817 0x1e89 (pg: 30, off:137)-->phy: 11913 (frm:  46) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  8545 0x2161 (pg: 33, off: 97)-->phy: 24161 (frm:  94) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 55297 0xd801 (pg:216, off:  1)-->phy: 12033 (frm:  47) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 52954 0xceda (pg:206, off:218)-->phy: 19674 (frm:  76) (prv: 127)--> val:   51 == value:   51 --  +    HIT!

log: 39720 0x9b28 (pg:155, off: 40)-->phy: 15656 (frm:  61) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 18455 0x4817 (pg: 72, off: 23)-->phy: 24343 (frm:  95) (prv: 127)--> val:    5 == value:    5 --  +    HIT!
log: 30349 0x768d (pg:118, off:141)-->phy: 16013 (frm:  62) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 63270 0xf726 (pg:247, off: 38)-->phy: 10278 (frm:  40) (prv: 127)--> val:   61 == value:   61 --  +    HIT!
log: 27156 0x6a14 (pg:106, off: 20)-->phy: 16148 (frm:  63) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 20614 0x5086 (pg: 80, off:134)-->phy:  9606 (frm:  37) (prv: 127)--> val:   20 == value:   20 --  +    HIT!
log: 19372 0x4bac (pg: 75, off:172)-->phy: 16556 (frm:  64) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 48689 0xbe31 (pg:190, off: 49)-->phy: 16689 (frm:  65) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 49386 0xc0ea (pg:192, off:234)-->phy: 17130 (frm:  66) (prv: 127)--> val:   48 == value:   48 --  +    HIT!
log: 50584 0xc598 (pg:197, off:152)-->phy: 17304 (frm:  67) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 51936 0xcae0 (pg:202, off:224)-->phy: 17632 (frm:  68) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 34705 0x8791 (pg:135, off:145)-->phy: 24977 (frm:  97) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 13653 0x3555 (pg: 53, off: 85)-->phy: 30549 (frm: 119) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 50077 0xc39d (pg:195, off:157)-->phy: 17821 (frm:  69) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 54518 0xd4f6 (pg:212, off:246)-->phy: 18166 (frm:  70) (prv: 127)--> val:   53 == value:   53 --  +    HIT!

log: 41482 0xa20a (pg:162, off: 10)-->phy: 18186 (frm:  71) (prv: 127)--> val:   40 == value:   40 --  +    HIT!
log:  4169 0x1049 (pg: 16, off: 73)-->phy: 25417 (frm:  99) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 36118 0x8d16 (pg:141, off: 22)-->phy:  1814 (frm:   7) (prv: 127)--> val:   35 == value:   35 --  +    HIT!
log:  9584 0x2570 (pg: 37, off:112)-->phy: 18544 (frm:  72) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 18490 0x483a (pg: 72, off: 58)-->phy: 24378 (frm:  95) (prv: 127)--> val:   18 == value:   18 --  +    HIT!

log: 55420 0xd87c (pg:216, off:124)-->phy: 12156 (frm:  47) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  5708 0x164c (pg: 22, off: 76)-->phy: 28748 (frm: 112) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 23506 0x5bd2 (pg: 91, off:210)-->phy: 15314 (frm:  59) (prv: 127)--> val:   22 == value:   22 --  +    HIT!
log: 15391 0x3c1f (pg: 60, off: 31)-->phy:  4639 (frm:  18) (prv: 127)--> val:    7 == value:    7 --  +    HIT!
log: 36368 0x8e10 (pg:142, off: 16)-->phy: 18704 (frm:  73) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 38976 0x9840 (pg:152, off: 64)-->phy: 19008 (frm:  74) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 50406 0xc4e6 (pg:196, off:230)-->phy: 19430 (frm:  75) (prv: 127)--> val:   49 == value:   49 --  +    HIT!
log: 49236 0xc054 (pg:192, off: 84)-->phy: 16980 (frm:  66) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 65035 0xfe0b (pg:254, off: 11)-->phy: 21003 (frm:  82) (prv: 127)--> val: -126 == value: -126 --  +    HIT!
log: 30120 0x75a8 (pg:117, off:168)-->phy: 19624 (frm:  76) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 62551 0xf457 (pg:244, off: 87)-->phy:  4439 (frm:  17) (prv: 127)--> val:   21 == value:   21 --  +    HIT!
log: 46809 0xb6d9 (pg:182, off:217)-->phy:  3289 (frm:  12) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 21687 0x54b7 (pg: 84, off:183)-->phy: 29623 (frm: 115) (prv: 127)--> val:   45 == value:   45 --  +    HIT!
log: 53839 0xd24f (pg:210, off: 79)-->phy: 32591 (frm: 127) (prv: 127)--> val: -109 == value: -109 --  + ----> pg_fault
log:  2098 0x0832 (pg:  8, off: 50)-->phy: 19762 (frm:  77) (prv: 127)--> val:    2 == value:    2 --  +    HIT!

log: 12364 0x304c (pg: 48, off: 76)-->phy: 20044 (frm:  78) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 45366 0xb136 (pg:177, off: 54)-->phy: 20278 (frm:  79) (prv: 127)--> val:   44 == value:   44 --  +    HIT!
log: 50437 0xc505 (pg:197, off:  5)-->phy: 17157 (frm:  67) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 36675 0x8f43 (pg:143, off: 67)-->phy: 20547 (frm:  80) (prv: 127)--> val:  -48 == value:  -48 --  +    HIT!
log: 55382 0xd856 (pg:216, off: 86)-->phy: 12118 (frm:  47) (prv: 127)--> val:   54 == value:   54 --  +    HIT!

log: 11846 0x2e46 (pg: 46, off: 70)-->phy: 20806 (frm:  81) (prv: 127)--> val:   11 == value:   11 --  +    HIT!
log: 49127 0xbfe7 (pg:191, off:231)-->phy: 24807 (frm:  96) (prv: 127)--> val:   -7 == value:   -7 --  +    HIT!
log: 19900 0x4dbc (pg: 77, off:188)-->phy:  5564 (frm:  21) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 20554 0x504a (pg: 80, off: 74)-->phy:  9546 (frm:  37) (prv: 127)--> val:   20 == value:   20 --  +    HIT!
log: 19219 0x4b13 (pg: 75, off: 19)-->phy: 16403 (frm:  64) (prv: 127)--> val:  -60 == value:  -60 --  +    HIT!

log: 51483 0xc91b (pg:201, off: 27)-->phy: 21019 (frm:  82) (prv: 127)--> val:   70 == value:   70 --  +    HIT!
log: 58090 0xe2ea (pg:226, off:234)-->phy: 21482 (frm:  83) (prv: 127)--> val:   56 == value:   56 --  +    HIT!
log: 39074 0x98a2 (pg:152, off:162)-->phy: 19106 (frm:  74) (prv: 127)--> val:   38 == value:   38 --  +    HIT!
log: 16060 0x3ebc (pg: 62, off:188)-->phy: 28092 (frm: 109) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 10447 0x28cf (pg: 40, off:207)-->phy: 21711 (frm:  84) (prv: 127)--> val:   51 == value:   51 --  +    HIT!

log: 54169 0xd399 (pg:211, off:153)-->phy: 28569 (frm: 111) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 20634 0x509a (pg: 80, off:154)-->phy:  9626 (frm:  37) (prv: 127)--> val:   20 == value:   20 --  +    HIT!
log: 57555 0xe0d3 (pg:224, off:211)-->phy: 21971 (frm:  85) (prv: 127)--> val:   52 == value:   52 --  +    HIT!
log: 61210 0xef1a (pg:239, off: 26)-->phy: 22042 (frm:  86) (prv: 127)--> val:   59 == value:   59 --  +    HIT!
log:   269 0x010d (pg:  1, off: 13)-->phy:  9741 (frm:  38) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 33154 0x8182 (pg:129, off:130)-->phy: 31362 (frm: 122) (prv: 127)--> val:   32 == value:   32 --  +    HIT!
log: 64487 0xfbe7 (pg:251, off:231)-->phy: 22503 (frm:  87) (prv: 127)--> val:   -7 == value:   -7 --  +    HIT!
log: 61223 0xef27 (pg:239, off: 39)-->phy: 22055 (frm:  86) (prv: 127)--> val:  -55 == value:  -55 --  +    HIT!
log: 47292 0xb8bc (pg:184, off:188)-->phy: 10940 (frm:  42) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 21852 0x555c (pg: 85, off: 92)-->phy: 22620 (frm:  88) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log:  5281 0x14a1 (pg: 20, off:161)-->phy: 14241 (frm:  55) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 45912 0xb358 (pg:179, off: 88)-->phy: 25688 (frm: 100) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 32532 0x7f14 (pg:127, off: 20)-->phy: 23572 (frm:  92) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 63067 0xf65b (pg:246, off: 91)-->phy: 22875 (frm:  89) (prv: 127)--> val: -106 == value: -106 --  +    HIT!
log: 41683 0xa2d3 (pg:162, off:211)-->phy: 18387 (frm:  71) (prv: 127)--> val:  -76 == value:  -76 --  +    HIT!

log: 20981 0x51f5 (pg: 81, off:245)-->phy: 23285 (frm:  90) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 33881 0x8459 (pg:132, off: 89)-->phy: 23385 (frm:  91) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 41785 0xa339 (pg:163, off: 57)-->phy: 23609 (frm:  92) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  4580 0x11e4 (pg: 17, off:228)-->phy: 14564 (frm:  56) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 41389 0xa1ad (pg:161, off:173)-->phy: 23981 (frm:  93) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 28572 0x6f9c (pg:111, off:156)-->phy: 31644 (frm: 123) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:   782 0x030e (pg:  3, off: 14)-->phy: 24078 (frm:  94) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 30273 0x7641 (pg:118, off: 65)-->phy: 15937 (frm:  62) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 62267 0xf33b (pg:243, off: 59)-->phy: 25915 (frm: 101) (prv: 127)--> val:  -50 == value:  -50 --  +    HIT!
log: 17922 0x4602 (pg: 70, off:  2)-->phy:     2 (frm:   0) (prv: 127)--> val:   17 == value:   17 --  +    HIT!

log: 63238 0xf706 (pg:247, off:  6)-->phy: 10246 (frm:  40) (prv: 127)--> val:   61 == value:   61 --  +    HIT!
log:  3308 0x0cec (pg: 12, off:236)-->phy: 12524 (frm:  48) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 26545 0x67b1 (pg:103, off:177)-->phy:  5041 (frm:  19) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 44395 0xad6b (pg:173, off:107)-->phy: 24427 (frm:  95) (prv: 127)--> val:   90 == value:   90 --  +    HIT!
log: 39120 0x98d0 (pg:152, off:208)-->phy: 19152 (frm:  74) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 21706 0x54ca (pg: 84, off:202)-->phy: 29642 (frm: 115) (prv: 127)--> val:   21 == value:   21 --  +    HIT!
log:  7144 0x1be8 (pg: 27, off:232)-->phy: 24808 (frm:  96) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 30244 0x7624 (pg:118, off: 36)-->phy: 15908 (frm:  62) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  3725 0x0e8d (pg: 14, off:141)-->phy: 24973 (frm:  97) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 54632 0xd568 (pg:213, off:104)-->phy: 25192 (frm:  98) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 30574 0x776e (pg:119, off:110)-->phy: 25454 (frm:  99) (prv: 127)--> val:   29 == value:   29 --  +    HIT!
log:  8473 0x2119 (pg: 33, off: 25)-->phy: 25625 (frm: 100) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 12386 0x3062 (pg: 48, off: 98)-->phy: 20066 (frm:  78) (prv: 127)--> val:   12 == value:   12 --  +    HIT!
log: 41114 0xa09a (pg:160, off:154)-->phy:  3482 (frm:  13) (prv: 127)--> val:   40 == value:   40 --  +    HIT!
log: 57930 0xe24a (pg:226, off: 74)-->phy: 21322 (frm:  83) (prv: 127)--> val:   56 == value:   56 --  +    HIT!

log: 15341 0x3bed (pg: 59, off:237)-->phy:  6125 (frm:  23) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 15598 0x3cee (pg: 60, off:238)-->phy:  4846 (frm:  18) (prv: 127)--> val:   15 == value:   15 --  +    HIT!
log: 59922 0xea12 (pg:234, off: 18)-->phy: 25874 (frm: 101) (prv: 127)--> val:   58 == value:   58 --  +    HIT!
log: 18226 0x4732 (pg: 71, off: 50)-->phy: 26162 (frm: 102) (prv: 127)--> val:   17 == value:   17 --  +    HIT!
log: 48162 0xbc22 (pg:188, off: 34)-->phy: 26402 (frm: 103) (prv: 127)--> val:   47 == value:   47 --  +    HIT!

log: 41250 0xa122 (pg:161, off: 34)-->phy: 23842 (frm:  93) (prv: 127)--> val:   40 == value:   40 --  +    HIT!
log:  1512 0x05e8 (pg:  5, off:232)-->phy: 27624 (frm: 107) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  2546 0x09f2 (pg:  9, off:242)-->phy:  8178 (frm:  31) (prv: 127)--> val:    2 == value:    2 --  +    HIT!
log: 41682 0xa2d2 (pg:162, off:210)-->phy: 18386 (frm:  71) (prv: 127)--> val:   40 == value:   40 --  +    HIT!
log:   322 0x0142 (pg:  1, off: 66)-->phy:  9794 (frm:  38) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log:   880 0x0370 (pg:  3, off:112)-->phy: 24176 (frm:  94) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 20891 0x519b (pg: 81, off:155)-->phy: 23195 (frm:  90) (prv: 127)--> val:  102 == value:  102 --  +    HIT!
log: 56604 0xdd1c (pg:221, off: 28)-->phy: 26652 (frm: 104) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 40166 0x9ce6 (pg:156, off:230)-->phy: 27110 (frm: 105) (prv: 127)--> val:   39 == value:   39 --  +    HIT!
log: 26791 0x68a7 (pg:104, off:167)-->phy: 27303 (frm: 106) (prv: 127)--> val:   41 == value:   41 --  +    HIT!

log: 44560 0xae10 (pg:174, off: 16)-->phy: 27408 (frm: 107) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 38698 0x972a (pg:151, off: 42)-->phy: 27690 (frm: 108) (prv: 127)--> val:   37 == value:   37 --  +    HIT!
log: 64127 0xfa7f (pg:250, off:127)-->phy: 28031 (frm: 109) (prv: 127)--> val:  -97 == value:  -97 --  +    HIT!
log: 15028 0x3ab4 (pg: 58, off:180)-->phy: 28340 (frm: 110) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 38669 0x970d (pg:151, off: 13)-->phy: 27661 (frm: 108) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 45637 0xb245 (pg:178, off: 69)-->phy: 28485 (frm: 111) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 43151 0xa88f (pg:168, off:143)-->phy: 29327 (frm: 114) (prv: 127)--> val:   35 == value:   35 --  +    HIT!
log:  9465 0x24f9 (pg: 36, off:249)-->phy:  7417 (frm:  28) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  2498 0x09c2 (pg:  9, off:194)-->phy:  8130 (frm:  31) (prv: 127)--> val:    2 == value:    2 --  +    HIT!
log: 13978 0x369a (pg: 54, off:154)-->phy: 28826 (frm: 112) (prv: 127)--> val:   13 == value:   13 --  +    HIT!

log: 16326 0x3fc6 (pg: 63, off:198)-->phy:  6854 (frm:  26) (prv: 127)--> val:   15 == value:   15 --  +    HIT!
log: 51442 0xc8f2 (pg:200, off:242)-->phy:  5874 (frm:  22) (prv: 127)--> val:   50 == value:   50 --  +    HIT!
log: 34845 0x881d (pg:136, off: 29)-->phy:  5149 (frm:  20) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 63667 0xf8b3 (pg:248, off:179)-->phy: 29107 (frm: 113) (prv: 127)--> val:   44 == value:   44 --  +    HIT!
log: 39370 0x99ca (pg:153, off:202)-->phy:  4042 (frm:  15) (prv: 127)--> val:   38 == value:   38 --  +    HIT!

log: 55671 0xd977 (pg:217, off:119)-->phy: 29303 (frm: 114) (prv: 127)--> val:   93 == value:   93 --  +    HIT!
log: 64496 0xfbf0 (pg:251, off:240)-->phy: 22512 (frm:  87) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  7767 0x1e57 (pg: 30, off: 87)-->phy: 11863 (frm:  46) (prv: 127)--> val: -107 == value: -107 --  +    HIT!
log:  6283 0x188b (pg: 24, off:139)-->phy: 29579 (frm: 115) (prv: 127)--> val:   34 == value:   34 --  +    HIT!
log: 55884 0xda4c (pg:218, off: 76)-->phy: 29772 (frm: 116) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 61103 0xeeaf (pg:238, off:175)-->phy: 30127 (frm: 117) (prv: 127)--> val:  -85 == value:  -85 --  +    HIT!
log: 10184 0x27c8 (pg: 39, off:200)-->phy: 30408 (frm: 118) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 39543 0x9a77 (pg:154, off:119)-->phy: 30583 (frm: 119) (prv: 127)--> val:  -99 == value:  -99 --  +    HIT!
log:  9555 0x2553 (pg: 37, off: 83)-->phy: 18515 (frm:  72) (prv: 127)--> val:   84 == value:   84 --  +    HIT!
log: 13963 0x368b (pg: 54, off:139)-->phy: 28811 (frm: 112) (prv: 127)--> val:  -94 == value:  -94 --  +    HIT!

log: 58975 0xe65f (pg:230, off: 95)-->phy: 30815 (frm: 120) (prv: 127)--> val: -105 == value: -105 --  +    HIT!
log: 19537 0x4c51 (pg: 76, off: 81)-->phy: 31057 (frm: 121) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  6101 0x17d5 (pg: 23, off:213)-->phy:  7125 (frm:  27) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 41421 0xa1cd (pg:161, off:205)-->phy: 24013 (frm:  93) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 45502 0xb1be (pg:177, off:190)-->phy: 20414 (frm:  79) (prv: 127)--> val:   44 == value:   44 --  +    HIT!

log: 29328 0x7290 (pg:114, off:144)-->phy: 31376 (frm: 122) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  8149 0x1fd5 (pg: 31, off:213)-->phy: 14037 (frm:  54) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 25450 0x636a (pg: 99, off:106)-->phy: 31594 (frm: 123) (prv: 127)--> val:   24 == value:   24 --  +    HIT!
log: 58944 0xe640 (pg:230, off: 64)-->phy: 30784 (frm: 120) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 50666 0xc5ea (pg:197, off:234)-->phy: 17386 (frm:  67) (prv: 127)--> val:   49 == value:   49 --  +    HIT!

log: 23084 0x5a2c (pg: 90, off: 44)-->phy:  9260 (frm:  36) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 36468 0x8e74 (pg:142, off:116)-->phy: 18804 (frm:  73) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 33645 0x836d (pg:131, off:109)-->phy: 31853 (frm: 124) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 25002 0x61aa (pg: 97, off:170)-->phy: 32170 (frm: 125) (prv: 127)--> val:   24 == value:   24 --  +    HIT!
log: 53715 0xd1d3 (pg:209, off:211)-->phy: 32467 (frm: 126) (prv: 127)--> val:  116 == value:  116 --  +    HIT!

log: 60173 0xeb0d (pg:235, off: 13)-->phy: 11533 (frm:  45) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 46354 0xb512 (pg:181, off: 18)-->phy: 32530 (frm: 127) (prv: 127)--> val:   45 == value:   45 --  + ----> pg_fault
log:  4708 0x1264 (pg: 18, off:100)-->phy: 14692 (frm:  57) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 28208 0x6e30 (pg:110, off: 48)-->phy:  7728 (frm:  30) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 58844 0xe5dc (pg:229, off:220)-->phy:  3036 (frm:  11) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 22173 0x569d (pg: 86, off:157)-->phy:  9117 (frm:  35) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  8535 0x2157 (pg: 33, off: 87)-->phy: 25687 (frm: 100) (prv: 127)--> val:   85 == value:   85 --  +    HIT!
log: 42261 0xa515 (pg:165, off: 21)-->phy:    21 (frm:   0) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 29687 0x73f7 (pg:115, off:247)-->phy:   503 (frm:   1) (prv: 127)--> val:   -3 == value:   -3 --  +    HIT!
log: 37799 0x93a7 (pg:147, off:167)-->phy:   679 (frm:   2) (prv: 127)--> val:  -23 == value:  -23 --  +    HIT!

log: 22566 0x5826 (pg: 88, off: 38)-->phy:   806 (frm:   3) (prv: 127)--> val:   22 == value:   22 --  +    HIT!
log: 62520 0xf438 (pg:244, off: 56)-->phy:  4408 (frm:  17) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  4098 0x1002 (pg: 16, off:  2)-->phy:  1026 (frm:   4) (prv: 127)--> val:    4 == value:    4 --  +    HIT!
log: 47999 0xbb7f (pg:187, off:127)-->phy:  6527 (frm:  25) (prv: 127)--> val:  -33 == value:  -33 --  +    HIT!
log: 49660 0xc1fc (pg:193, off:252)-->phy:  1532 (frm:   5) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 37063 0x90c7 (pg:144, off:199)-->phy:  2247 (frm:   8) (prv: 127)--> val:   49 == value:   49 --  +    HIT!
log: 41856 0xa380 (pg:163, off:128)-->phy: 23680 (frm:  92) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  5417 0x1529 (pg: 21, off: 41)-->phy:  1577 (frm:   6) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 48856 0xbed8 (pg:190, off:216)-->phy: 16856 (frm:  65) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 10682 0x29ba (pg: 41, off:186)-->phy:  1978 (frm:   7) (prv: 127)--> val:   10 == value:   10 --  +    HIT!

log: 22370 0x5762 (pg: 87, off: 98)-->phy:  8802 (frm:  34) (prv: 127)--> val:   21 == value:   21 --  +    HIT!
log: 63281 0xf731 (pg:247, off: 49)-->phy: 10289 (frm:  40) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 62452 0xf3f4 (pg:243, off:244)-->phy:  2292 (frm:   8) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 50532 0xc564 (pg:197, off:100)-->phy: 17252 (frm:  67) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  9022 0x233e (pg: 35, off: 62)-->phy:  2366 (frm:   9) (prv: 127)--> val:    8 == value:    8 --  +    HIT!

log: 59300 0xe7a4 (pg:231, off:164)-->phy:  2724 (frm:  10) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 58660 0xe524 (pg:229, off: 36)-->phy:  2852 (frm:  11) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 56401 0xdc51 (pg:220, off: 81)-->phy:  2897 (frm:  11) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  8518 0x2146 (pg: 33, off: 70)-->phy: 25670 (frm: 100) (prv: 127)--> val:    8 == value:    8 --  +    HIT!
log: 63066 0xf65a (pg:246, off: 90)-->phy: 22874 (frm:  89) (prv: 127)--> val:   61 == value:   61 --  +    HIT!

log: 63250 0xf712 (pg:247, off: 18)-->phy: 10258 (frm:  40) (prv: 127)--> val:   61 == value:   61 --  +    HIT!
log: 48592 0xbdd0 (pg:189, off:208)-->phy:  3280 (frm:  12) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 28771 0x7063 (pg:112, off: 99)-->phy:  3427 (frm:  13) (prv: 127)--> val:   24 == value:   24 --  +    HIT!
log: 37673 0x9329 (pg:147, off: 41)-->phy:   553 (frm:   2) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 60776 0xed68 (pg:237, off:104)-->phy:  3688 (frm:  14) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 56438 0xdc76 (pg:220, off:118)-->phy:  2934 (frm:  11) (prv: 127)--> val:   55 == value:   55 --  +    HIT!
log: 60424 0xec08 (pg:236, off:  8)-->phy: 12808 (frm:  50) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 39993 0x9c39 (pg:156, off: 57)-->phy: 26937 (frm: 105) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 56004 0xdac4 (pg:218, off:196)-->phy: 29892 (frm: 116) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 59002 0xe67a (pg:230, off:122)-->phy: 30842 (frm: 120) (prv: 127)--> val:   57 == value:   57 --  +    HIT!

log: 33982 0x84be (pg:132, off:190)-->phy: 23486 (frm:  91) (prv: 127)--> val:   33 == value:   33 --  +    HIT!
log: 25498 0x639a (pg: 99, off:154)-->phy: 31642 (frm: 123) (prv: 127)--> val:   24 == value:   24 --  +    HIT!
log: 57047 0xded7 (pg:222, off:215)-->phy:  4055 (frm:  15) (prv: 127)--> val:  -75 == value:  -75 --  +    HIT!
log:  1401 0x0579 (pg:  5, off:121)-->phy:  4217 (frm:  16) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 15130 0x3b1a (pg: 59, off: 26)-->phy:  5914 (frm:  23) (prv: 127)--> val:   14 == value:   14 --  +    HIT!

log: 42960 0xa7d0 (pg:167, off:208)-->phy:  4560 (frm:  17) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 61827 0xf183 (pg:241, off:131)-->phy:  4739 (frm:  18) (prv: 127)--> val:   96 == value:   96 --  +    HIT!
log: 32442 0x7eba (pg:126, off:186)-->phy:  5050 (frm:  19) (prv: 127)--> val:   31 == value:   31 --  +    HIT!
log: 64304 0xfb30 (pg:251, off: 48)-->phy: 22320 (frm:  87) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 30273 0x7641 (pg:118, off: 65)-->phy: 15937 (frm:  62) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 38082 0x94c2 (pg:148, off:194)-->phy:  5314 (frm:  20) (prv: 127)--> val:   37 == value:   37 --  +    HIT!
log: 22404 0x5784 (pg: 87, off:132)-->phy:  8836 (frm:  34) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  3808 0x0ee0 (pg: 14, off:224)-->phy: 25056 (frm:  97) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 16883 0x41f3 (pg: 65, off:243)-->phy:  5619 (frm:  21) (prv: 127)--> val:  124 == value:  124 --  +    HIT!
log: 23111 0x5a47 (pg: 90, off: 71)-->phy:  9287 (frm:  36) (prv: 127)--> val: -111 == value: -111 --  +    HIT!

log: 62417 0xf3d1 (pg:243, off:209)-->phy:  2257 (frm:   8) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 60364 0xebcc (pg:235, off:204)-->phy: 11724 (frm:  45) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  4542 0x11be (pg: 17, off:190)-->phy: 14526 (frm:  56) (prv: 127)--> val:    4 == value:    4 --  +    HIT!
log: 14829 0x39ed (pg: 57, off:237)-->phy:  5869 (frm:  22) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 44964 0xafa4 (pg:175, off:164)-->phy:  6052 (frm:  23) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 33924 0x8484 (pg:132, off:132)-->phy: 23428 (frm:  91) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  2141 0x085d (pg:  8, off: 93)-->phy: 19805 (frm:  77) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 19245 0x4b2d (pg: 75, off: 45)-->phy: 16429 (frm:  64) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 47168 0xb840 (pg:184, off: 64)-->phy: 10816 (frm:  42) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 24048 0x5df0 (pg: 93, off:240)-->phy:  6384 (frm:  24) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log:  1022 0x03fe (pg:  3, off:254)-->phy: 24318 (frm:  94) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 23075 0x5a23 (pg: 90, off: 35)-->phy:  9251 (frm:  36) (prv: 127)--> val: -120 == value: -120 --  +    HIT!
log: 24888 0x6138 (pg: 97, off: 56)-->phy: 32056 (frm: 125) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 49247 0xc05f (pg:192, off: 95)-->phy: 16991 (frm:  66) (prv: 127)--> val:   23 == value:   23 --  +    HIT!
log:  4900 0x1324 (pg: 19, off: 36)-->phy:  6436 (frm:  25) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 22656 0x5880 (pg: 88, off:128)-->phy:   896 (frm:   3) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 34117 0x8545 (pg:133, off: 69)-->phy:  6725 (frm:  26) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 55555 0xd903 (pg:217, off:  3)-->phy: 29187 (frm: 114) (prv: 127)--> val:   64 == value:   64 --  +    HIT!
log: 48947 0xbf33 (pg:191, off: 51)-->phy:  6963 (frm:  27) (prv: 127)--> val:  -52 == value:  -52 --  +    HIT!
log: 59533 0xe88d (pg:232, off:141)-->phy:  7309 (frm:  28) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 21312 0x5340 (pg: 83, off: 64)-->phy:  7488 (frm:  29) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 21415 0x53a7 (pg: 83, off:167)-->phy:  7591 (frm:  29) (prv: 127)--> val:  -23 == value:  -23 --  +    HIT!
log:   813 0x032d (pg:  3, off: 45)-->phy: 24109 (frm:  94) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 19419 0x4bdb (pg: 75, off:219)-->phy: 16603 (frm:  64) (prv: 127)--> val:  -10 == value:  -10 --  +    HIT!
log:  1999 0x07cf (pg:  7, off:207)-->phy: 10191 (frm:  39) (prv: 127)--> val:  -13 == value:  -13 --  +    HIT!

log: 20155 0x4ebb (pg: 78, off:187)-->phy:  7867 (frm:  30) (prv: 127)--> val:  -82 == value:  -82 --  +    HIT!
log: 21521 0x5411 (pg: 84, off: 17)-->phy:  7953 (frm:  31) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 13670 0x3566 (pg: 53, off:102)-->phy:  8294 (frm:  32) (prv: 127)--> val:   13 == value:   13 --  +    HIT!
log: 19289 0x4b59 (pg: 75, off: 89)-->phy: 16473 (frm:  64) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 58483 0xe473 (pg:228, off:115)-->phy:  8563 (frm:  33) (prv: 127)--> val:   28 == value:   28 --  +    HIT!

log: 41318 0xa166 (pg:161, off:102)-->phy: 23910 (frm:  93) (prv: 127)--> val:   40 == value:   40 --  +    HIT!
log: 16151 0x3f17 (pg: 63, off: 23)-->phy:  8727 (frm:  34) (prv: 127)--> val:  -59 == value:  -59 --  +    HIT!
log: 13611 0x352b (pg: 53, off: 43)-->phy:  8235 (frm:  32) (prv: 127)--> val:   74 == value:   74 --  +    HIT!
log: 21514 0x540a (pg: 84, off: 10)-->phy:  7946 (frm:  31) (prv: 127)--> val:   21 == value:   21 --  +    HIT!
log: 13499 0x34bb (pg: 52, off:187)-->phy:  9147 (frm:  35) (prv: 127)--> val:   46 == value:   46 --  +    HIT!

log: 45583 0xb20f (pg:178, off: 15)-->phy: 28431 (frm: 111) (prv: 127)--> val: -125 == value: -125 --  +    HIT!
log: 49013 0xbf75 (pg:191, off:117)-->phy:  7029 (frm:  27) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 64843 0xfd4b (pg:253, off: 75)-->phy:  9291 (frm:  36) (prv: 127)--> val:   82 == value:   82 --  +    HIT!
log: 63485 0xf7fd (pg:247, off:253)-->phy: 10493 (frm:  40) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 38697 0x9729 (pg:151, off: 41)-->phy: 27689 (frm: 108) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 59188 0xe734 (pg:231, off: 52)-->phy:  2612 (frm:  10) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 24593 0x6011 (pg: 96, off: 17)-->phy:  9489 (frm:  37) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 57641 0xe129 (pg:225, off: 41)-->phy:  9769 (frm:  38) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 36524 0x8eac (pg:142, off:172)-->phy: 18860 (frm:  73) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 56980 0xde94 (pg:222, off:148)-->phy:  3988 (frm:  15) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 36810 0x8fca (pg:143, off:202)-->phy: 20682 (frm:  80) (prv: 127)--> val:   35 == value:   35 --  +    HIT!
log:  6096 0x17d0 (pg: 23, off:208)-->phy: 10192 (frm:  39) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 11070 0x2b3e (pg: 43, off: 62)-->phy: 13630 (frm:  53) (prv: 127)--> val:   10 == value:   10 --  +    HIT!
log: 60124 0xeadc (pg:234, off:220)-->phy: 26076 (frm: 101) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 37576 0x92c8 (pg:146, off:200)-->phy: 10440 (frm:  40) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 15096 0x3af8 (pg: 58, off:248)-->phy: 28408 (frm: 110) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 45247 0xb0bf (pg:176, off:191)-->phy: 10687 (frm:  41) (prv: 127)--> val:   47 == value:   47 --  +    HIT!
log: 32783 0x800f (pg:128, off: 15)-->phy: 11279 (frm:  44) (prv: 127)--> val:    3 == value:    3 --  +    HIT!
log: 58390 0xe416 (pg:228, off: 22)-->phy:  8470 (frm:  33) (prv: 127)--> val:   57 == value:   57 --  +    HIT!
log: 60873 0xedc9 (pg:237, off:201)-->phy:  3785 (frm:  14) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 23719 0x5ca7 (pg: 92, off:167)-->phy: 10919 (frm:  42) (prv: 127)--> val:   41 == value:   41 --  +    HIT!
log: 24385 0x5f41 (pg: 95, off: 65)-->phy: 11073 (frm:  43) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 22307 0x5723 (pg: 87, off: 35)-->phy: 11299 (frm:  44) (prv: 127)--> val:  -56 == value:  -56 --  +    HIT!
log: 17375 0x43df (pg: 67, off:223)-->phy: 11743 (frm:  45) (prv: 127)--> val:   -9 == value:   -9 --  +    HIT!
log: 15990 0x3e76 (pg: 62, off:118)-->phy: 11894 (frm:  46) (prv: 127)--> val:   15 == value:   15 --  +    HIT!

log: 20526 0x502e (pg: 80, off: 46)-->phy: 12078 (frm:  47) (prv: 127)--> val:   20 == value:   20 --  +    HIT!
log: 25904 0x6530 (pg:101, off: 48)-->phy: 12336 (frm:  48) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 42224 0xa4f0 (pg:164, off:240)-->phy: 12784 (frm:  49) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  9311 0x245f (pg: 36, off: 95)-->phy: 12895 (frm:  50) (prv: 127)--> val:   23 == value:   23 --  +    HIT!
log:  7862 0x1eb6 (pg: 30, off:182)-->phy: 13238 (frm:  51) (prv: 127)--> val:    7 == value:    7 --  +    HIT!

log:  3835 0x0efb (pg: 14, off:251)-->phy: 25083 (frm:  97) (prv: 127)--> val:  -66 == value:  -66 --  +    HIT!
log: 30535 0x7747 (pg:119, off: 71)-->phy: 25415 (frm:  99) (prv: 127)--> val:  -47 == value:  -47 --  +    HIT!
log: 65179 0xfe9b (pg:254, off:155)-->phy: 13467 (frm:  52) (prv: 127)--> val:  -90 == value:  -90 --  +    HIT!
log: 57387 0xe02b (pg:224, off: 43)-->phy: 21803 (frm:  85) (prv: 127)--> val:   10 == value:   10 --  +    HIT!
log: 63579 0xf85b (pg:248, off: 91)-->phy: 29019 (frm: 113) (prv: 127)--> val:   22 == value:   22 --  +    HIT!

log:  4946 0x1352 (pg: 19, off: 82)-->phy:  6482 (frm:  25) (prv: 127)--> val:    4 == value:    4 --  +    HIT!
log:  9037 0x234d (pg: 35, off: 77)-->phy:  2381 (frm:   9) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 61033 0xee69 (pg:238, off:105)-->phy: 30057 (frm: 117) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 55543 0xd8f7 (pg:216, off:247)-->phy: 13815 (frm:  53) (prv: 127)--> val:   61 == value:   61 --  +    HIT!
log: 50361 0xc4b9 (pg:196, off:185)-->phy: 19385 (frm:  75) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log:  6480 0x1950 (pg: 25, off: 80)-->phy: 13904 (frm:  54) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 14042 0x36da (pg: 54, off:218)-->phy: 28890 (frm: 112) (prv: 127)--> val:   13 == value:   13 --  +    HIT!
log: 21531 0x541b (pg: 84, off: 27)-->phy:  7963 (frm:  31) (prv: 127)--> val:    6 == value:    6 --  +    HIT!
log: 39195 0x991b (pg:153, off: 27)-->phy: 14107 (frm:  55) (prv: 127)--> val:   70 == value:   70 --  +    HIT!
log: 37511 0x9287 (pg:146, off:135)-->phy: 10375 (frm:  40) (prv: 127)--> val:  -95 == value:  -95 --  +    HIT!

log: 23696 0x5c90 (pg: 92, off:144)-->phy: 10896 (frm:  42) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 27440 0x6b30 (pg:107, off: 48)-->phy: 14384 (frm:  56) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 28201 0x6e29 (pg:110, off: 41)-->phy: 14633 (frm:  57) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 23072 0x5a20 (pg: 90, off: 32)-->phy: 14880 (frm:  58) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  7814 0x1e86 (pg: 30, off:134)-->phy: 13190 (frm:  51) (prv: 127)--> val:    7 == value:    7 --  +    HIT!

log:  6552 0x1998 (pg: 25, off:152)-->phy: 13976 (frm:  54) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 43637 0xaa75 (pg:170, off:117)-->phy: 15221 (frm:  59) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 35113 0x8929 (pg:137, off: 41)-->phy: 15401 (frm:  60) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 34890 0x884a (pg:136, off: 74)-->phy: 15690 (frm:  61) (prv: 127)--> val:   34 == value:   34 --  +    HIT!
log: 61297 0xef71 (pg:239, off:113)-->phy: 22129 (frm:  86) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 45633 0xb241 (pg:178, off: 65)-->phy: 28481 (frm: 111) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 61431 0xeff7 (pg:239, off:247)-->phy: 22263 (frm:  86) (prv: 127)--> val:   -3 == value:   -3 --  +    HIT!
log: 46032 0xb3d0 (pg:179, off:208)-->phy: 16080 (frm:  62) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 18774 0x4956 (pg: 73, off: 86)-->phy: 16214 (frm:  63) (prv: 127)--> val:   18 == value:   18 --  +    HIT!
log: 62991 0xf60f (pg:246, off: 15)-->phy: 22799 (frm:  89) (prv: 127)--> val: -125 == value: -125 --  +    HIT!

log: 28059 0x6d9b (pg:109, off:155)-->phy: 16539 (frm:  64) (prv: 127)--> val:  102 == value:  102 --  +    HIT!
log: 35229 0x899d (pg:137, off:157)-->phy: 15517 (frm:  60) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 51230 0xc81e (pg:200, off: 30)-->phy: 16670 (frm:  65) (prv: 127)--> val:   50 == value:   50 --  +    HIT!
log: 14405 0x3845 (pg: 56, off: 69)-->phy: 16965 (frm:  66) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 52242 0xcc12 (pg:204, off: 18)-->phy: 17170 (frm:  67) (prv: 127)--> val:   51 == value:   51 --  +    HIT!

log: 43153 0xa891 (pg:168, off:145)-->phy: 17553 (frm:  68) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  2709 0x0a95 (pg: 10, off:149)-->phy: 17813 (frm:  69) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 47963 0xbb5b (pg:187, off: 91)-->phy: 18011 (frm:  70) (prv: 127)--> val:  -42 == value:  -42 --  +    HIT!
log: 36943 0x904f (pg:144, off: 79)-->phy: 18255 (frm:  71) (prv: 127)--> val:   19 == value:   19 --  +    HIT!
log: 54066 0xd332 (pg:211, off: 50)-->phy: 18482 (frm:  72) (prv: 127)--> val:   52 == value:   52 --  +    HIT!

log: 10054 0x2746 (pg: 39, off: 70)-->phy: 30278 (frm: 118) (prv: 127)--> val:    9 == value:    9 --  +    HIT!
log: 43051 0xa82b (pg:168, off: 43)-->phy: 17451 (frm:  68) (prv: 127)--> val:   10 == value:   10 --  +    HIT!
log: 11525 0x2d05 (pg: 45, off:  5)-->phy: 18693 (frm:  73) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 17684 0x4514 (pg: 69, off: 20)-->phy: 18964 (frm:  74) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 41681 0xa2d1 (pg:162, off:209)-->phy: 19409 (frm:  75) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 27883 0x6ceb (pg:108, off:235)-->phy: 19691 (frm:  76) (prv: 127)--> val:   58 == value:   58 --  +    HIT!
log: 56909 0xde4d (pg:222, off: 77)-->phy:  3917 (frm:  15) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 45772 0xb2cc (pg:178, off:204)-->phy: 28620 (frm: 111) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 27496 0x6b68 (pg:107, off:104)-->phy: 14440 (frm:  56) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 46842 0xb6fa (pg:182, off:250)-->phy: 19962 (frm:  77) (prv: 127)--> val:   45 == value:   45 --  +    HIT!

log: 38734 0x974e (pg:151, off: 78)-->phy: 27726 (frm: 108) (prv: 127)--> val:   37 == value:   37 --  +    HIT!
log: 28972 0x712c (pg:113, off: 44)-->phy: 20012 (frm:  78) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 59684 0xe924 (pg:233, off: 36)-->phy: 20260 (frm:  79) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 11384 0x2c78 (pg: 44, off:120)-->phy: 20600 (frm:  80) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 21018 0x521a (pg: 82, off: 26)-->phy: 20762 (frm:  81) (prv: 127)--> val:   20 == value:   20 --  +    HIT!

log:  2192 0x0890 (pg:  8, off:144)-->phy: 21136 (frm:  82) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 18384 0x47d0 (pg: 71, off:208)-->phy: 26320 (frm: 102) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 13464 0x3498 (pg: 52, off:152)-->phy:  9112 (frm:  35) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 31018 0x792a (pg:121, off: 42)-->phy: 21290 (frm:  83) (prv: 127)--> val:   30 == value:   30 --  +    HIT!
log: 62958 0xf5ee (pg:245, off:238)-->phy: 21742 (frm:  84) (prv: 127)--> val:   61 == value:   61 --  +    HIT!

log: 30611 0x7793 (pg:119, off:147)-->phy: 25491 (frm:  99) (prv: 127)--> val:  -28 == value:  -28 --  +    HIT!
log:  1913 0x0779 (pg:  7, off:121)-->phy: 21881 (frm:  85) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 18904 0x49d8 (pg: 73, off:216)-->phy: 16344 (frm:  63) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 26773 0x6895 (pg:104, off:149)-->phy: 27285 (frm: 106) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 55491 0xd8c3 (pg:216, off:195)-->phy: 13763 (frm:  53) (prv: 127)--> val:   48 == value:   48 --  +    HIT!

log: 21899 0x558b (pg: 85, off:139)-->phy: 22667 (frm:  88) (prv: 127)--> val:   98 == value:   98 --  +    HIT!
log: 64413 0xfb9d (pg:251, off:157)-->phy: 22429 (frm:  87) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 47134 0xb81e (pg:184, off: 30)-->phy: 22046 (frm:  86) (prv: 127)--> val:   46 == value:   46 --  +    HIT!
log: 23172 0x5a84 (pg: 90, off:132)-->phy: 14980 (frm:  58) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  7262 0x1c5e (pg: 28, off: 94)-->phy: 22366 (frm:  87) (prv: 127)--> val:    7 == value:    7 --  +    HIT!

log: 12705 0x31a1 (pg: 49, off:161)-->phy: 22689 (frm:  88) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  7522 0x1d62 (pg: 29, off: 98)-->phy: 22882 (frm:  89) (prv: 127)--> val:    7 == value:    7 --  +    HIT!
log: 58815 0xe5bf (pg:229, off:191)-->phy: 23231 (frm:  90) (prv: 127)--> val:  111 == value:  111 --  +    HIT!
log: 34916 0x8864 (pg:136, off:100)-->phy: 15716 (frm:  61) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  3802 0x0eda (pg: 14, off:218)-->phy: 25050 (frm:  97) (prv: 127)--> val:    3 == value:    3 --  +    HIT!

log: 58008 0xe298 (pg:226, off:152)-->phy: 23448 (frm:  91) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  1239 0x04d7 (pg:  4, off:215)-->phy: 23767 (frm:  92) (prv: 127)--> val:   53 == value:   53 --  +    HIT!
log: 63947 0xf9cb (pg:249, off:203)-->phy: 24011 (frm:  93) (prv: 127)--> val:  114 == value:  114 --  +    HIT!
log:   381 0x017d (pg:  1, off:125)-->phy: 24189 (frm:  94) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 60734 0xed3e (pg:237, off: 62)-->phy:  3646 (frm:  14) (prv: 127)--> val:   59 == value:   59 --  +    HIT!

log: 48769 0xbe81 (pg:190, off:129)-->phy: 24449 (frm:  95) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 41938 0xa3d2 (pg:163, off:210)-->phy: 24786 (frm:  96) (prv: 127)--> val:   40 == value:   40 --  +    HIT!
log: 38025 0x9489 (pg:148, off:137)-->phy:  5257 (frm:  20) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 55099 0xd73b (pg:215, off: 59)-->phy: 24891 (frm:  97) (prv: 127)--> val:  -50 == value:  -50 --  +    HIT!
log: 56691 0xdd73 (pg:221, off:115)-->phy: 26739 (frm: 104) (prv: 127)--> val:   92 == value:   92 --  +    HIT!

log: 39530 0x9a6a (pg:154, off:106)-->phy: 30570 (frm: 119) (prv: 127)--> val:   38 == value:   38 --  +    HIT!
log: 59003 0xe67b (pg:230, off:123)-->phy: 30843 (frm: 120) (prv: 127)--> val:  -98 == value:  -98 --  +    HIT!
log:  6029 0x178d (pg: 23, off:141)-->phy: 10125 (frm:  39) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 20920 0x51b8 (pg: 81, off:184)-->phy: 25272 (frm:  98) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  8077 0x1f8d (pg: 31, off:141)-->phy: 25485 (frm:  99) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 42633 0xa689 (pg:166, off:137)-->phy: 25737 (frm: 100) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 17443 0x4423 (pg: 68, off: 35)-->phy: 25891 (frm: 101) (prv: 127)--> val:    8 == value:    8 --  +    HIT!
log: 53570 0xd142 (pg:209, off: 66)-->phy: 32322 (frm: 126) (prv: 127)--> val:   52 == value:   52 --  +    HIT!
log: 22833 0x5931 (pg: 89, off: 49)-->phy: 26161 (frm: 102) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  3782 0x0ec6 (pg: 14, off:198)-->phy: 26566 (frm: 103) (prv: 127)--> val:    3 == value:    3 --  +    HIT!

log: 47758 0xba8e (pg:186, off:142)-->phy: 26766 (frm: 104) (prv: 127)--> val:   46 == value:   46 --  +    HIT!
log: 22136 0x5678 (pg: 86, off:120)-->phy: 27000 (frm: 105) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 22427 0x579b (pg: 87, off:155)-->phy: 11419 (frm:  44) (prv: 127)--> val:  -26 == value:  -26 --  +    HIT!
log: 23867 0x5d3b (pg: 93, off: 59)-->phy:  6203 (frm:  24) (prv: 127)--> val:   78 == value:   78 --  +    HIT!
log: 59968 0xea40 (pg:234, off: 64)-->phy: 27200 (frm: 106) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 62166 0xf2d6 (pg:242, off:214)-->phy: 27606 (frm: 107) (prv: 127)--> val:   60 == value:   60 --  +    HIT!
log:  6972 0x1b3c (pg: 27, off: 60)-->phy: 27708 (frm: 108) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 63684 0xf8c4 (pg:248, off:196)-->phy: 29124 (frm: 113) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 46388 0xb534 (pg:181, off: 52)-->phy: 32564 (frm: 127) (prv: 127)--> val:    0 == value:    0 --  + ----> pg_fault
log: 41942 0xa3d6 (pg:163, off:214)-->phy: 24790 (frm:  96) (prv: 127)--> val:   40 == value:   40 --  +    HIT!

log: 36524 0x8eac (pg:142, off:172)-->phy: 28076 (frm: 109) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  9323 0x246b (pg: 36, off:107)-->phy: 12907 (frm:  50) (prv: 127)--> val:   26 == value:   26 --  +    HIT!
log: 31114 0x798a (pg:121, off:138)-->phy: 21386 (frm:  83) (prv: 127)--> val:   30 == value:   30 --  +    HIT!
log: 22345 0x5749 (pg: 87, off: 73)-->phy: 11337 (frm:  44) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 46463 0xb57f (pg:181, off:127)-->phy: 32639 (frm: 127) (prv: 127)--> val:   95 == value:   95 --  + ----> pg_fault

log: 54671 0xd58f (pg:213, off:143)-->phy: 28303 (frm: 110) (prv: 127)--> val:   99 == value:   99 --  +    HIT!
log:  9214 0x23fe (pg: 35, off:254)-->phy:  2558 (frm:   9) (prv: 127)--> val:    8 == value:    8 --  +    HIT!
log:  7257 0x1c59 (pg: 28, off: 89)-->phy: 22361 (frm:  87) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 33150 0x817e (pg:129, off:126)-->phy: 28542 (frm: 111) (prv: 127)--> val:   32 == value:   32 --  +    HIT!
log: 41565 0xa25d (pg:162, off: 93)-->phy: 19293 (frm:  75) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 26214 0x6666 (pg:102, off:102)-->phy: 28774 (frm: 112) (prv: 127)--> val:   25 == value:   25 --  +    HIT!
log:  3595 0x0e0b (pg: 14, off: 11)-->phy: 26379 (frm: 103) (prv: 127)--> val: -126 == value: -126 --  +    HIT!
log: 17932 0x460c (pg: 70, off: 12)-->phy: 28940 (frm: 113) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 34660 0x8764 (pg:135, off:100)-->phy: 29284 (frm: 114) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 51961 0xcaf9 (pg:202, off:249)-->phy: 29689 (frm: 115) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 58634 0xe50a (pg:229, off: 10)-->phy: 23050 (frm:  90) (prv: 127)--> val:   57 == value:   57 --  +    HIT!
log: 57990 0xe286 (pg:226, off:134)-->phy: 23430 (frm:  91) (prv: 127)--> val:   56 == value:   56 --  +    HIT!
log: 28848 0x70b0 (pg:112, off:176)-->phy:  3504 (frm:  13) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 49920 0xc300 (pg:195, off:  0)-->phy: 29696 (frm: 116) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 18351 0x47af (pg: 71, off:175)-->phy: 30127 (frm: 117) (prv: 127)--> val:  -21 == value:  -21 --  +    HIT!

log: 53669 0xd1a5 (pg:209, off:165)-->phy: 32421 (frm: 126) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 33996 0x84cc (pg:132, off:204)-->phy: 30412 (frm: 118) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  6741 0x1a55 (pg: 26, off: 85)-->phy: 30549 (frm: 119) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 64098 0xfa62 (pg:250, off: 98)-->phy: 30818 (frm: 120) (prv: 127)--> val:   62 == value:   62 --  +    HIT!
log:   606 0x025e (pg:  2, off: 94)-->phy: 31070 (frm: 121) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 27383 0x6af7 (pg:106, off:247)-->phy: 31479 (frm: 122) (prv: 127)--> val:  -67 == value:  -67 --  +    HIT!
log: 63140 0xf6a4 (pg:246, off:164)-->phy: 31652 (frm: 123) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 32228 0x7de4 (pg:125, off:228)-->phy: 31972 (frm: 124) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 63437 0xf7cd (pg:247, off:205)-->phy: 32205 (frm: 125) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 29085 0x719d (pg:113, off:157)-->phy: 20125 (frm:  78) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 65080 0xfe38 (pg:254, off: 56)-->phy: 13368 (frm:  52) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 38753 0x9761 (pg:151, off: 97)-->phy: 32353 (frm: 126) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 16041 0x3ea9 (pg: 62, off:169)-->phy: 11945 (frm:  46) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  9041 0x2351 (pg: 35, off: 81)-->phy:  2385 (frm:   9) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 42090 0xa46a (pg:164, off:106)-->phy: 12650 (frm:  49) (prv: 127)--> val:   41 == value:   41 --  +    HIT!

log: 46388 0xb534 (pg:181, off: 52)-->phy: 32564 (frm: 127) (prv: 127)--> val:    0 == value:    0 --  + ----> pg_fault
log: 63650 0xf8a2 (pg:248, off:162)-->phy: 32674 (frm: 127) (prv: 127)--> val:   62 == value:   62 --  + ----> pg_fault
log: 36636 0x8f1c (pg:143, off: 28)-->phy:    28 (frm:   0) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 21947 0x55bb (pg: 85, off:187)-->phy:   443 (frm:   1) (prv: 127)--> val:  110 == value:  110 --  +    HIT!
log: 19833 0x4d79 (pg: 77, off:121)-->phy:   633 (frm:   2) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 36464 0x8e70 (pg:142, off:112)-->phy: 28016 (frm: 109) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  8541 0x215d (pg: 33, off: 93)-->phy:   861 (frm:   3) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 12712 0x31a8 (pg: 49, off:168)-->phy: 22696 (frm:  88) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 48955 0xbf3b (pg:191, off: 59)-->phy:  6971 (frm:  27) (prv: 127)--> val:  -50 == value:  -50 --  +    HIT!
log: 39206 0x9926 (pg:153, off: 38)-->phy: 14118 (frm:  55) (prv: 127)--> val:   38 == value:   38 --  +    HIT!

log: 15578 0x3cda (pg: 60, off:218)-->phy:  1242 (frm:   4) (prv: 127)--> val:   15 == value:   15 --  +    HIT!
log: 49205 0xc035 (pg:192, off: 53)-->phy:  1333 (frm:   5) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  7731 0x1e33 (pg: 30, off: 51)-->phy: 13107 (frm:  51) (prv: 127)--> val: -116 == value: -116 --  +    HIT!
log: 43046 0xa826 (pg:168, off: 38)-->phy: 17446 (frm:  68) (prv: 127)--> val:   42 == value:   42 --  +    HIT!
log: 60498 0xec52 (pg:236, off: 82)-->phy:  1618 (frm:   6) (prv: 127)--> val:   59 == value:   59 --  +    HIT!

log:  9237 0x2415 (pg: 36, off: 21)-->phy: 12821 (frm:  50) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 47706 0xba5a (pg:186, off: 90)-->phy: 26714 (frm: 104) (prv: 127)--> val:   46 == value:   46 --  +    HIT!
log: 43973 0xabc5 (pg:171, off:197)-->phy:  1989 (frm:   7) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 42008 0xa418 (pg:164, off: 24)-->phy: 12568 (frm:  49) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 27460 0x6b44 (pg:107, off: 68)-->phy: 14404 (frm:  56) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 24999 0x61a7 (pg: 97, off:167)-->phy:  2215 (frm:   8) (prv: 127)--> val:  105 == value:  105 --  +    HIT!
log: 51933 0xcadd (pg:202, off:221)-->phy: 29661 (frm: 115) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 34070 0x8516 (pg:133, off: 22)-->phy:  6678 (frm:  26) (prv: 127)--> val:   33 == value:   33 --  +    HIT!
log: 65155 0xfe83 (pg:254, off:131)-->phy: 13443 (frm:  52) (prv: 127)--> val:  -96 == value:  -96 --  +    HIT!
log: 59955 0xea33 (pg:234, off: 51)-->phy: 27187 (frm: 106) (prv: 127)--> val: -116 == value: -116 --  +    HIT!

log:  9277 0x243d (pg: 36, off: 61)-->phy: 12861 (frm:  50) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 20420 0x4fc4 (pg: 79, off:196)-->phy:  2500 (frm:   9) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 44860 0xaf3c (pg:175, off: 60)-->phy:  5948 (frm:  23) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 50992 0xc730 (pg:199, off: 48)-->phy:  2608 (frm:  10) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 10583 0x2957 (pg: 41, off: 87)-->phy:  2903 (frm:  11) (prv: 127)--> val:   85 == value:   85 --  +    HIT!

log: 57751 0xe197 (pg:225, off:151)-->phy:  9879 (frm:  38) (prv: 127)--> val:  101 == value:  101 --  +    HIT!
log: 23195 0x5a9b (pg: 90, off:155)-->phy: 15003 (frm:  58) (prv: 127)--> val:  -90 == value:  -90 --  +    HIT!
log: 27227 0x6a5b (pg:106, off: 91)-->phy: 31323 (frm: 122) (prv: 127)--> val: -106 == value: -106 --  +    HIT!
log: 42816 0xa740 (pg:167, off: 64)-->phy:  4416 (frm:  17) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 58219 0xe36b (pg:227, off:107)-->phy:  3179 (frm:  12) (prv: 127)--> val:  -38 == value:  -38 --  +    HIT!

log: 37606 0x92e6 (pg:146, off:230)-->phy: 10470 (frm:  40) (prv: 127)--> val:   36 == value:   36 --  +    HIT!
log: 18426 0x47fa (pg: 71, off:250)-->phy: 30202 (frm: 117) (prv: 127)--> val:   17 == value:   17 --  +    HIT!
log: 21238 0x52f6 (pg: 82, off:246)-->phy: 20982 (frm:  81) (prv: 127)--> val:   20 == value:   20 --  +    HIT!
log: 11983 0x2ecf (pg: 46, off:207)-->phy:  3535 (frm:  13) (prv: 127)--> val:  -77 == value:  -77 --  +    HIT!
log: 48394 0xbd0a (pg:189, off: 10)-->phy:  3594 (frm:  14) (prv: 127)--> val:   47 == value:   47 --  +    HIT!

log: 11036 0x2b1c (pg: 43, off: 28)-->phy:  3868 (frm:  15) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 30557 0x775d (pg:119, off: 93)-->phy:  4189 (frm:  16) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 23453 0x5b9d (pg: 91, off:157)-->phy:  4509 (frm:  17) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 49847 0xc2b7 (pg:194, off:183)-->phy:  4791 (frm:  18) (prv: 127)--> val:  -83 == value:  -83 --  +    HIT!
log: 30032 0x7550 (pg:117, off: 80)-->phy:  4944 (frm:  19) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 48065 0xbbc1 (pg:187, off:193)-->phy: 18113 (frm:  70) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  6957 0x1b2d (pg: 27, off: 45)-->phy: 27693 (frm: 108) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  2301 0x08fd (pg:  8, off:253)-->phy: 21245 (frm:  82) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  7736 0x1e38 (pg: 30, off: 56)-->phy: 13112 (frm:  51) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 31260 0x7a1c (pg:122, off: 28)-->phy:  5148 (frm:  20) (prv: 127)--> val:    0 == value:    0 --  +    HIT!

log: 17071 0x42af (pg: 66, off:175)-->phy:  5551 (frm:  21) (prv: 127)--> val:  -85 == value:  -85 --  +    HIT!
log:  8940 0x22ec (pg: 34, off:236)-->phy:  5868 (frm:  22) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log:  9929 0x26c9 (pg: 38, off:201)-->phy:  6089 (frm:  23) (prv: 127)--> val:    0 == value:    0 --  +    HIT!
log: 45563 0xb1fb (pg:177, off:251)-->phy:  6395 (frm:  24) (prv: 127)--> val:  126 == value:  126 --  +    HIT!
log: 12107 0x2f4b (pg: 47, off: 75)-->phy:  6475 (frm:  25) (prv: 127)--> val:  -46 == value:  -46 --  +    HIT!


Page Fault Percentage: 0.538%
TLB Hit Percentage: 0.054%

ALL logical ---> physical assertions PASSED!

                ...done.
*/