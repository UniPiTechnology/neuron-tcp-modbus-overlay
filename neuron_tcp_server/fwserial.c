/**********************
 *
 * Programming utility via ModBus
 *
 * Michal Petrilak 2016
 * Miroslav Ondra  2017
 *
 **********************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <errno.h>
#include <modbus.h>

#include "armutil.h"


/* Hardware constants */
#define PAGE_SIZE   1024            
#define REG_SIZE    64

#define MAX_FW_SIZE (64*PAGE_SIZE)
#define MAX_RW_SIZE (PAGE_SIZE)
#define RW_START_PAGE ((0xE000) / PAGE_SIZE)

/* Default parameters */
char* PORT = NULL;
int   BAUD = 19200;
int   DEVICE_ID = 15;
char* firmwaredir = "/opt/fw";
int upboard;
int verbose = 0;
int do_verify = 0;
int do_prog   = 0;
int do_resetrw= 0;
int do_calibrate= 0;
int do_final= 0;


int load_fw(char *path, uint8_t* prog_data, const size_t len)
{
    FILE* fd;
    int red, i;
    fd = fopen(path, "rb");
    if (!fd) {
        printf("error opening firmware file \"%s\"\n", path);
        return -1;
    }
    memset(prog_data, 0xff, len);

    red = fread(prog_data, 1, MAX_FW_SIZE, fd);
    fclose(fd);
    return red;
}


int verify(modbus_t *ctx, uint8_t* prog_data, uint8_t* rw_data, int last_prog_page, int last_page)
{
    uint16_t* pd;
    int ret, chunk, page;
    uint16_t val, reg;

            modbus_set_response_timeout(ctx, 2, 999999);
            pd = (uint16_t*) prog_data;
            for (page=0; page < last_page; page++) {
                printf("Verifying page %.2d ...", page);
                fflush(stdout);
                if (modbus_write_register(ctx, 0x7705, page) != 1) {   // set page address in Neuron
                    fprintf(stderr, "Verifying failed: %s\n", modbus_strerror(errno));
                    break;
                }
                for (chunk=0; chunk < 8; chunk++) {
                    if (modbus_write_registers(ctx, 0x7700+chunk, REG_SIZE, pd) != REG_SIZE) {; // send chunk of data
                        fprintf(stderr, "Sending data failed: %s\n", modbus_strerror(errno));
                    }
                    pd += REG_SIZE;
                }
                if (modbus_read_registers(ctx, 0x7707, 1, &val) == 1) {
                    if (val == 0x100) {
                        printf(" OK\n");
                    } else {
                        printf(" NOT OK. errors = %d.\n", 0x100-val);
                    }
                } else {
                    fprintf(stderr, "Verifying failed: %s\n", modbus_strerror(errno));
                    break;
                }
                if (page == last_prog_page-1) {
                    page = RW_START_PAGE-1;
                    pd = (uint16_t*) rw_data;
                }
            }
}

int flashit(modbus_t *ctx, uint8_t* prog_data, uint8_t* rw_data, int last_prog_page, int last_page)
{
    uint16_t* pd;
    int ret, chunk, page;
            // Programming
            modbus_set_response_timeout(ctx, 1, 0);
            page = 0;
            int errors = 0;
            while (page < last_page) {
                printf("Programming page %.2d ...", page);
                fflush(stdout);
                if (page < last_prog_page) {
                    pd = (uint16_t*) (prog_data + page*PAGE_SIZE);
                } else {
                    pd = (uint16_t*) (rw_data + ((page-RW_START_PAGE)*PAGE_SIZE));
                }
                if (modbus_write_register(ctx, 0x7705, page) == 1) {   // set page address in Neuron
                    for (chunk=0; chunk < 8; chunk++) {
                        if (modbus_write_registers(ctx, 0x7700+chunk, REG_SIZE, pd) == -1) // send chunk of data (64*2 B)
                            errors++;
                        pd += REG_SIZE;
                    }
                    if (modbus_write_register(ctx, 0x7707, 1) == 1) {  // write page to flash
                        printf(" OK.\n");
                        page++;
                        if (page == last_prog_page) {
                            page = RW_START_PAGE;
                        }
                    } else {
                        errors++;
                        printf(" Trying again.\n");
                        fprintf(stderr, "Flashing page failed: %s\n", modbus_strerror(errno));
                    }
                } else {
                    errors++;
                    printf(" Trying again.\n");
                }
                if (errors > 200) break;
            }
    
}


static struct option long_options[] = {
  {"verbose", no_argument,      0, 'v'},
  {"verify", no_argument,       0, 'V'},
  {"programm", no_argument,     0, 'P'},
  {"resetrw", no_argument,      0, 'R'},
  {"calibrate", no_argument,    0, 'C'},
  {"final", required_argument,  0, 'F'},
  {"port",  no_argument,        0, 'p'},
  {"baud",  required_argument,  0, 'b'},
  {"unit",    required_argument,0, 'u'},
  {"dir", required_argument,    0, 'd'},
  {0, 0, 0, 0}
};

void print_usage(char *argv0)
{
    printf("\nUtility for Programming Neuron via ModBus RTU\n");
    printf("%s [-vVPRC] -p <port> [-u <mb address>] [-b <baudrate>] [-d <firmware dir>] [-F <upper board id>]\n", argv0);
    printf("\n");
    printf("--port <port>\t\t /dev/extcomm/1/0 or COM3\n");
    printf("--unit <mb address>\t default 15\n");
    printf("--baud <baudrate>\t default 19200\n");
    printf("--dir <firmware dir>\t default /opt/fw\n");
    printf("--verbose\t show more messages\n");
    printf("--verify\t compare flash with file\n");
    printf("--programm\t write firmware to flash\n");
    printf("--resetrw\t check/rewrite also rw settings\n");
    printf("--calibrate\t write calibrating firmware to flash\n");
    printf("--final <upper board id or ?>\t write final firmware over calibrating\n");
    printf("\n");
}

int main(int argc, char **argv)
{
    uint8_t *prog_data;   // buffer containing firmware
    uint8_t* rw_data;     // buffer containing firmware rw data
    uint16_t* pd;
    int ret, chunk, page;
    uint16_t val, reg;
    modbus_t *ctx;
    FILE* fdx;
    
    // Parse command line options
    int c;
    char *endptr;
    while (1) {
       int option_index = 0;
       c = getopt_long(argc, argv, "vVPRCp:b:u:d:F:", long_options, &option_index);
       if (c == -1) {
           if (optind < argc)  {
               printf ("non-option ARGV-element: %s\n", argv[optind]);
               exit(EXIT_FAILURE);
            }
            break;
       }

       switch (c) {
       case 'v':
           verbose++;
           break;
       case 'V':
           do_verify = 1;
           break;
       case 'P':
           do_prog = 1;
           break;
       case 'R':
           do_resetrw = 1;
           break;
       case 'C':
           do_calibrate = 1; do_prog = 1; do_resetrw = 1;
           break;
       case 'F':
           upboard = strtol(optarg, &endptr, 10);
           if ((endptr==optarg) || (!upboard_exists(upboard))) {
               printf("Available upper board ids:\n");
               print_upboards(-1);
               exit(EXIT_FAILURE);
           }
           do_final = 1; do_prog = 1; do_resetrw = 1;
           break;
       case 'p':
           PORT = strdup(optarg);
           break;
       case 'b':
           BAUD = atoi(optarg);
           if (BAUD==0) {
               printf("Baud must be non-zero integer (given %s)\n", optarg);
               exit(EXIT_FAILURE);
           }
           break;
       case 'u':
           DEVICE_ID = atoi(optarg);
           if (DEVICE_ID==0) {
               printf("Unit must be non-zero integer (given %s)\n", optarg);
               exit(EXIT_FAILURE);
           }
           break;
       case 'd':
           firmwaredir = strdup(optarg);
           break;

       default:
           print_usage(argv[0]);
           exit(EXIT_FAILURE);
           break;
       }
    }

    if (PORT == NULL) {
        printf("Port device must be specified\n", optarg);
        print_usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    // Open port
    ctx = modbus_new_rtu(PORT , BAUD, 'N', 8, 1);
    if (ctx == NULL) {
        fprintf(stderr, "Unable to create the libmodbus context\n");
        return -1;
    }
    if ( verbose > 1) modbus_set_debug(ctx,verbose-1);
    modbus_set_slave(ctx, DEVICE_ID);

    if (modbus_connect(ctx) == -1) {
        fprintf(stderr, "Connection failed: %s\n", modbus_strerror(errno));
        modbus_free(ctx);
        return -1;
    }

    // get FW & HW version
    uint16_t r1000[5];
    Tboard_version bv;
    //int hw_version, sw_version, base_version;
    if (modbus_read_registers(ctx, 1000, 5, r1000) == 5) {
        parse_version(&bv, r1000);
        printf("Boardset:   %3d %-30s (v%d.%d%s)\n",
               HW_BOARD(bv.hw_version),  arm_name(bv.hw_version),
               HW_MAJOR(bv.hw_version), HW_MINOR(bv.hw_version),
               IS_CALIB(bv.hw_version)?" CAL":"");
        printf("Baseboard:  %3d %-30s (v%d.%d)\n",
               HW_BOARD(bv.base_hw_version),  arm_name(bv.base_hw_version),
               HW_MAJOR(bv.base_hw_version), HW_MINOR(bv.base_hw_version));
        printf("Firmware: v%d.%d\n", SW_MAJOR(bv.sw_version), SW_MINOR(bv.sw_version));
    } else {
        fprintf(stderr, "Read version failed: %s\n", modbus_strerror(errno));
        modbus_free(ctx);
        return -1;
    }

    if (do_prog || do_verify) {
        // FW manipulation
        if (do_calibrate) {
            bv.hw_version = bv.base_hw_version | 0x8;
            do_resetrw = 1;
        } else if (do_final) {
            if (!IS_CALIB(bv.hw_version)) {
                fprintf(stderr, "Only calibrating version can be reprogrammed to final\n");
                modbus_free(ctx);
                return -1;
            }
            bv.hw_version = check_compatibility(bv.base_hw_version, upboard);
            if (bv.hw_version == 0) {
                fprintf(stderr, "Incompatible base and upper boards. Use one of:\n");
                print_upboards(bv.base_hw_version);
                modbus_free(ctx);
                return -1;
            }
        }
        // load firmware file
        char* fwname = firmware_name(bv.hw_version, bv.base_hw_version, firmwaredir, ".bin");
        prog_data = malloc(MAX_FW_SIZE);
        if (verbose) printf("Opening firmware file: %s\n", fwname);
        int red = load_fw(fwname, prog_data, MAX_FW_SIZE);
        int rwred = RW_START_PAGE;
        free(fwname);
        if (red <= 0) {
            if (red == 0) {
                fprintf(stderr, "Firmware file is empty!\n");
            } 
            free(prog_data);
            modbus_free(ctx);
            return -1;
        }
        red = (red + (PAGE_SIZE - 1)) / PAGE_SIZE;
        if (verbose) printf("Program pages: %d\n", red);

        if (do_resetrw) {
            // load rw consts file
            rw_data = malloc(MAX_RW_SIZE);
            char* rwname = firmware_name(bv.hw_version, bv.base_hw_version, firmwaredir, ".rw");
            if (verbose) printf("Opening RW settings file: %s\n", rwname);
            int rwlen = load_fw(rwname, rw_data, MAX_RW_SIZE);
            free(rwname);
            // calc page count of firmware file
            rwred += ((rwlen + (PAGE_SIZE - 1)) / PAGE_SIZE);
            if (verbose) printf("Final page: %d\n", rwred);
        }
        
        // init FW programmer
        if (modbus_write_bit(ctx, 1006, 1) != 1) {
            fprintf(stderr, "Program mode setting failed: %s\n", modbus_strerror(errno));
            modbus_free(ctx);
            return -1;
        }
        /*
        if (modbus_read_registers(ctx, 1000, 5, version) == 5) {
        if (modbus_read_registers(ctx, 1000, 5, r1000) == 5) {
            parse_version(&xbv, r1000);
            if (xbv.hw_version != 0xffff) {
                printf("Boot boardset:   %3d (v%d.%d%s)\n", xhw_version >> 8, (xhw_version & 0xff)>>4, xhw_version & 0x7, (xhw_version & 0x8)?" CAL":"");
            } else {
                printf("Boot boardset:   Undefined\n");
            }
            printf("Boot baseboard:  %3d (v%d.%d)\n", xbase_version>> 8, (xbase_version & 0xff)>>4, xbase_version & 0x7);
            printf("Boot firmware:  v%d.%d\n", xsw_version >> 8, xsw_version & 0xff);
        }
        */
        if (do_prog || do_calibrate) {
            flashit(ctx,prog_data, rw_data, red, rwred);
        }
        if (do_verify) { 
            verify(ctx,prog_data, rw_data, red, rwred);
        }
        modbus_write_register(ctx, 0x7707, 3); // reboot
        free(prog_data);
    }
    modbus_free(ctx);
    return 0;
}
