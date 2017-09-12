/**********************
 *
 * Programming utility via SPI
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
//#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "armspi.h"
#include "armutil.h"


/* Hardware constants */
#define PAGE_SIZE   1024            
#define REG_SIZE    64

#define MAX_FW_SIZE (64*PAGE_SIZE)
#define MAX_RW_SIZE (PAGE_SIZE)
#define RW_START_PAGE ((0xE000) / PAGE_SIZE)

/* Default parameters */
char* PORT = NULL;
int   BAUD = 10000000;
char* firmwaredir = "/opt/fw";
int upboard;
int verbose = 0;
int do_verify = 0;
int do_prog   = 0;
int do_resetrw= 0;
int do_calibrate= 0;
int do_final= 0;


#define vprintf( ... ) if (verbose > 0) printf( __VA_ARGS__ )
#define vvprintf( ... ) if (verbose > 1) printf( __VA_ARGS__ )

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


int arm_flash_file(void* fwctx, const char* fwname)
{
    /* Firmware programming */
    int fd, ret;
    void * data;
    int min_len = 1024;

    if((fd = open(fwname, O_RDONLY)) >= 0) {
        struct stat st;
        if((ret=fstat(fd,&st)) >= 0) {
            size_t len_file = st.st_size;
            vprintf("Sending firmware file %s length=%d\n", fwname, len_file);
            if (len_file > min_len) { 
                if((data=mmap(NULL, len_file, PROT_READ,MAP_PRIVATE, fd, 0)) != MAP_FAILED) {
                    send_firmware(fwctx, (uint8_t*) data, len_file, 0);
                    munmap(data, len_file);
                } else {
                    vprintf("Error mapping firmware file %s to memory\n", fwname);
                }
            } else {
                vprintf("Damaged firmware file %s\n", fwname);
            }
        }
        close(fd);
    } else {
        vprintf("Error opening firmware file %s\n", fwname);
    }
}

int arm_flash_rw_file(arm_handle* arm, void* fwctx, const char* fwname, int overwrite)
{
    /* Nvram programming */
    int fd, ret;
    void * data;
    int min_len = 6;
    uint16_t buffer[128];

    if((fd = open(fwname, O_RDONLY)) >= 0) {
        struct stat st;
        if((ret=fstat(fd,&st)) >= 0) {
            size_t len_file = st.st_size;
            vprintf("Sending nvram file %s length=%d\n", fwname, len_file);
            if ((len_file > min_len)&&(len_file < 2*128 /*1024*/)) { 
                int n2000 = read_regs(arm, 2000, len_file/2 ,buffer);
                if ((n2000 > 0)|| overwrite) {
                    if((data=mmap(NULL, len_file, PROT_READ,MAP_PRIVATE, fd, 0)) != MAP_FAILED) {
                        if (overwrite) {
                            send_firmware(fwctx, (uint8_t*) data, len_file,0xe000);
                        } else {
                            memcpy(buffer+n2000-1, ((uint8_t*) data) + 2*(n2000-1), len_file - 2*(n2000-1));
                            send_firmware(fwctx, (uint8_t*) buffer, len_file,0xe000);
                        }
                        munmap(data, len_file);
                    } else {
                        vprintf("Error mapping nvram file %s to memory\n", fwname);
                    }
                } else {
                    vprintf("Can't read original nvram\n");
                }
            } else {
                vprintf("Damaged nvram file %s\n", fwname);
            }
        }
        close(fd);
    } else {
        vprintf("Error opening nvram file %s\n", fwname);
    }
}


static struct option long_options[] = {
  {"verbose", no_argument,      0, 'v'},
  {"programm", no_argument,     0, 'P'},
  {"resetrw", no_argument,      0, 'R'},
  {"calibrate", no_argument,    0, 'C'},
  {"final", required_argument,  0, 'F'},
  {"spidev",  no_argument,      0, 's'},
  {"baud",  required_argument,  0, 'b'},
  {"dir", required_argument,    0, 'd'},
  {0, 0, 0, 0}
};

void print_usage(char *argv0)
{
    printf("\nUtility for Programming Neuron via ModBus RTU\n");
    printf("%s [-vPRC] -s <spidevice> [-b <baudrate>] [-d <firmware dir>] [-F <upper board id>]\n", argv0);
    printf("\n");
    printf("--spidev <spidev>\t\t /dev/spidev[1,2,3,0] \n");
    printf("--baud <baudrate>\t default 10000000\n");
    printf("--dir <firmware dir>\t default /opt/fw\n");
    printf("--verbose\t show more messages\n");
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
    arm_handle *ctx;
    FILE* fdx;
    
    // Parse command line options
    int c;
    char *endptr;
    while (1) {
       int option_index = 0;
       c = getopt_long(argc, argv, "vPRCs:b:d:F:", long_options, &option_index);
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
       case 's':
           PORT = strdup(optarg);
           break;
       case 'b':
           BAUD = atoi(optarg);
           if (BAUD==0) {
               printf("Baud must be non-zero integer (given %s)\n", optarg);
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
    ctx = malloc(sizeof(arm_handle));

    if ( arm_init(ctx, PORT , BAUD, 0, NULL) < 0) {
        fprintf(stderr, "Unable to create the spi context\n");
        free(ctx);
        return -1;
    }

    if (verbose > 1) arm_verbose = verbose;

    // get FW & HW version
    uint16_t r1000[5];
    Tboard_version bv;
    //int hw_version, sw_version, base_version;
    if (read_regs(ctx, 1000, 5, r1000) == 5) {
        parse_version(&bv, r1000);
        printf("Boardset:   %3d %-30s (v%d.%d%s)\n",
               HW_BOARD(bv.hw_version), arm_name(bv.hw_version),
               HW_MAJOR(bv.hw_version), HW_MINOR(bv.hw_version),
               IS_CALIB(bv.hw_version)?" CAL":"");
        printf("Baseboard:  %3d %-30s (v%d.%d)\n",
               HW_BOARD(bv.base_hw_version),  arm_name(bv.base_hw_version),
               HW_MAJOR(bv.base_hw_version), HW_MINOR(bv.base_hw_version));
        printf("Firmware: v%d.%d\n", SW_MAJOR(bv.sw_version), SW_MINOR(bv.sw_version));
    } else {
        fprintf(stderr, "Read version failed\n");
        close(ctx->fd);
        free(ctx);
        return -1;
    }

    
    if (do_prog) {
        // FW manipulation
        if (do_calibrate) {
            bv.hw_version = bv.base_hw_version | 0x8;
            do_resetrw = 1;
        } else if (do_final) {
            if (!(bv.hw_version & 0x8)) {
                fprintf(stderr, "Only calibrating version can be reprogrammed to final\n");
                close(ctx->fd);
                free(ctx);
                return -1;
            }
            bv.hw_version = check_compatibility(bv.base_hw_version, upboard);
            if (bv.hw_version == 0) {
                fprintf(stderr, "Incompatible base and upper boards. Use one of:\n");
                print_upboards(bv.base_hw_version);
                close(ctx->fd);
                free(ctx);
                return -1;
            }
        }
        // load firmware file
        char* fwname = firmware_name(bv.hw_version, bv.base_hw_version, firmwaredir, ".bin");
        prog_data = malloc(MAX_FW_SIZE);
        if (verbose) printf("Opening firmware file: %s\n", fwname);
        int red = load_fw(fwname, prog_data, MAX_FW_SIZE);
        int rwred = RW_START_PAGE;
        int rwlen = 0;
        free(fwname);
        if (red <= 0) {
            if (red == 0) {
                fprintf(stderr, "Firmware file is empty!\n");
            } 
            free(prog_data);
            close(ctx->fd);
            free(ctx);
            return -1;
        }
        //red = (red + (PAGE_SIZE - 1)) / PAGE_SIZE;
        if (verbose) printf("Program size: %d\n", red);

        if (do_resetrw) {
            // load rw consts file
            rw_data = malloc(MAX_RW_SIZE);
            char* rwname = firmware_name( bv.hw_version, bv.base_hw_version, firmwaredir, ".rw");
            if (verbose) printf("Opening RW settings file: %s\n", rwname);
            rwlen = load_fw(rwname, rw_data, MAX_RW_SIZE);
            free(rwname);
            // calc page count of firmware file
            rwred += ((rwlen + (PAGE_SIZE - 1)) / PAGE_SIZE);
            if (verbose) printf("Final page: %d\n", rwred);
        }
        
        // init FW programmer
        //if (write_bit(ctx, 1006, 1) != 1) {
        //    fprintf(stderr, "Program mode setting failed\n");
        //    close(ctx->fd);
        //    free(ctx);
        //    return -1;
        //}
        
        if (do_prog || do_calibrate) {
            void * fwctx = start_firmware(ctx);
            if (fwctx != NULL) {
                send_firmware(fwctx, prog_data, red, 0);
                if (do_resetrw) send_firmware(fwctx, rw_data, rwlen, 0xe000);
                finish_firmware(fwctx);
            }
            //flashit(ctx,prog_data, rw_data, red, rwred);
        }
        free(prog_data);
    }
    close(ctx->fd);
    free(ctx);
    return 0;
}
