
//#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#include "armutil.h"

/*
const char* hwnames[HW_COUNT] = {
    "B-1000-1",
    "E-8Di8Ro-1",
    "E-14Ro-1",
    "E-16Di-1",
    "E-8Di8Ro-1_P-11DiMb485",
    "E-14Ro-1_P-11DiR485-1",
    "E-16Di-1_P-11DiR485-1",
    "E-14Ro-1_U-14Ro-1",
    "E-16Di-1_U-14Ro-1",
    "E-14Ro-1_U-14Di-1",
    "E-16Di-1_U-14Di-1",
    "E-4Ai4Ao-1",
    "E-4Ai4Ao-1_P-6Di5Ro-1",
    "B-485-1",
};
*/


typedef struct {
  uint8_t board;
  uint8_t baseboard;
  uint8_t upboard;
  const char*   name;
} Tcompatibility_map;

typedef struct {
  uint8_t board;
  uint8_t subver;
  const char*  name; 
} Tboards_map;

/*char* base_boards[] {
    {0, "B-1000"},
    {1, "E-8Di8Ro"},
    {2, "E-14Ro"},
    {3, "E-16Di"},
    {11,"E-4Ai4Ao"},
};*/

#define UP_COUNT 7
Tboards_map up_boards[] = {
    {0, 0, ""}, 
    {1, 16, "P-11DiR485"},
    {2, 16, "U-14Ro"},
    {3, 16, "U-14Di"},
    {4, 16, "P-6Di5Ro"},
    {5, 16, "U-6Di5Ro"},
    {13,16, "B-485"},
};

Tboards_map* get_umap(int board)
{
    int i;
    for (i=0; i<UP_COUNT; i++) {
            if (up_boards[i].board == board) {
                return up_boards + i;
            }
    }
    return NULL;
}

#define HW_COUNT 16
Tcompatibility_map compatibility_map[HW_COUNT] = {
    {0,  0, 0, "B-1000",},
    {1,  1, 0, "E-8Di8Ro",},
    {2,  2, 0, "E-14Ro",},
    {3,  3, 0, "E-16Di",},
    {4,  1, 1, "E-8Di8Ro_P-11DiR485", },        //"E-8Di8Ro_P-11DiR485"
    {5,  2, 1, "E-14Ro_P-11DiR485",},         //"E-14Ro_P-11DiR485"
    {6,  3, 1, "E-16Di_P-11DiR485",},         // "E-16Di_P-11DiR485"
    {7,  2, 2, "E-14Ro_U-14Ro",},         //"E-14Ro_U-14Ro"
    {8,  3, 2, "E-16Di_U-14Ro",},         //"E-16Di_U-14Ro"
    {9,  2, 3, "E-14Ro_U-14Di",},         //"E-14Ro_U-14Di"
    {10, 3, 3, "E-16Di_U-14Di",},         //"E-16Di_U-14Di"
    {11, 11,0, "E-4Ai4Ao"},
    {12, 11,4, "E-4Ai4Ao_P-6Di5Ro",},         //"E-4Ai4Ao_P-6Di5Ro"},
    {13, 0,13, "B-485"},
    {14, 14,0, "E-4Dali"},
    {15, 11,5, "E-4Ai4Ao_U-6Di5Ro"},
};

Tcompatibility_map* get_map(int board)
{
    //uint8_t board = hw_version >> 8;
    int i;
    for (i=0; i<HW_COUNT; i++) {
        if (compatibility_map[i].board == board) {
            return compatibility_map + i;
        }
    }
        /*if (board < HW_COUNT) {
            return hwnames[board];
        }*/
    return NULL;
}


const char* arm_name(uint16_t hw_version)
{
    Tcompatibility_map* map = get_map(HW_BOARD(hw_version));
    if (map == NULL)
        return "UNKNOWN BOARD";
    return map->name;
}

char* firmware_name(int hw_version, int hw_base, const char* fwdir, const char* ext)
{
    uint8_t calibrate = IS_CALIB(hw_version);
    uint8_t board_version = HW_MAJOR(hw_version);
    Tcompatibility_map* map = get_map(HW_BOARD(hw_version));
    if (map  == NULL) return NULL;
    if (map->baseboard == map->board) {
        const char* armname = map->name;
        char* fwname = malloc(strlen(fwdir) + strlen(armname) + strlen(ext) + 2 + 4);
        strcpy(fwname, fwdir);
        if (strlen(fwname) && (fwname[strlen(fwname)-1] != '/')) strcat(fwname, "/");
        sprintf(fwname+strlen(fwname), "%s-%d%s%s", armname, board_version, calibrate?"C":"", ext);
        return fwname;

    } else {
        Tcompatibility_map* basemap = get_map(HW_BOARD(hw_base));
        if (basemap == NULL) return NULL;
        uint8_t base_version = HW_MAJOR(hw_base);
        if (basemap->board != map->baseboard) {
            // Incorrent parameters
            return NULL;
        }
        const char* basename = basemap->name;
        Tboards_map* umap = get_umap(map->upboard);
        const char* uname = umap->name;
        char* fwname = malloc(strlen(fwdir) + strlen(basename) + strlen(uname) + strlen(ext) + 2 + 4 + +1 + 4);
        strcpy(fwname, fwdir);
        if (strlen(fwname) && (fwname[strlen(fwname)-1] != '/')) strcat(fwname, "/");
        sprintf(fwname+strlen(fwname), "%s-%d_%s-%d%s%s", basename, base_version, uname, board_version, calibrate?"C":"", ext);
        return fwname;
    }
}

int check_compatibility(int hw_base, int upboard)
{
    uint8_t board = hw_base >> 8;
    int i;
    for (i=0; i<HW_COUNT; i++) {
        if ((compatibility_map[i].baseboard == board) && (compatibility_map[i].upboard == upboard)) {
            Tboards_map* umap = get_umap(upboard);
            if (umap->subver == 0) {
                return (compatibility_map[i].board << 8) | (hw_base & 0xff);
            } else {
                return (compatibility_map[i].board << 8) | (umap->subver & 0xff);
            }
        }
    }
    return 0;
}

int get_board_speed(Tboard_version* bv)
{
    // E-4Ai4Ao* - used Digital Isolator on SPI - speed max 8MHz
    if (HW_BOARD(bv->base_hw_version) == 11) return 8000000;
    // Default speed 12MHz
    return 12000000;
}

void print_upboards(int filter)
{
    int i;
    for (i=0; i<UP_COUNT; i++) {
        if ((filter == -1) || (check_compatibility(filter,up_boards[i].board)))
            printf("%3d - %s\n", up_boards[i].board, up_boards[i].name);
    }
}

int upboard_exists(int board) 
{
    return get_umap(board) != NULL;
}

int parse_version(Tboard_version* bv, uint16_t *r1000)
{
    bv->sw_version = r1000[0];
    bv->hw_version = r1000[3];
    bv->base_hw_version = r1000[4];

    bv->di_count    = (r1000[1]       ) >> 8;
    bv->do_count   = (r1000[1] & 0xff);
    bv->ai_count   = (r1000[2]       ) >> 8;
    bv->ao_count   = (r1000[2] & 0xff) >> 4;
    bv->uart_count = (r1000[2] & 0x0f);
    bv->uled_count = 0;
    bv->int_mask_register = 1007;
    if (SW_MAJOR(bv->sw_version) < 4) {
        bv->hw_version = (SW_MINOR(bv->sw_version) & 0xff) << 4 \
                       | (SW_MINOR(bv->sw_version) & 0x0f);
        bv->sw_version = bv->sw_version & 0xff00;
        bv->int_mask_register = 1003;
    } else {
        if ((bv->sw_version < 0x0403)) {  // devel version
           bv->int_mask_register = 1004;
        }
        if (HW_BOARD(bv->hw_version) == 0) {
            if (bv->sw_version != 0x0400)
                bv->uled_count = 4;
        }
    }
    if ((HW_BOARD(bv->base_hw_version) == 0x0b) && (HW_MAJOR(bv->base_hw_version) <= 1)) bv->int_mask_register = 0;   // 4Ai4Ao has not interrupt
}
