#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <errno.h>

/*
 * For debug option. If you want to debug, set 1.
 * If not, set 0.
 */
#define DEBUG 0

#define MAX_SYMBOL_TABLE_SIZE   1024
#define MEM_TEXT_START          0x00400000
#define MEM_DATA_START          0x10000000
#define BYTES_PER_WORD          4
#define INST_LIST_LEN           20

/******************************************************
 * Structure Declaration
 *******************************************************/

typedef struct inst_struct {
    char *name;
    char *op;
    char type;
    char *funct;
} inst_t;

typedef struct symbol_struct {
    char name[32];
    uint32_t address;
} symbol_t;

enum section { 
    DATA = 0,
    TEXT,
    MAX_SIZE
};

/******************************************************
 * Global Variable Declaration
 *******************************************************/

inst_t inst_list[INST_LIST_LEN] = {       //  idx
    {"addiu",   "001001", 'I', ""},       //    0
    {"addu",    "000000", 'R', "100001"}, //    1
    {"and",     "000000", 'R', "100100"}, //    2
    {"andi",    "001100", 'I', ""},       //    3
    {"beq",     "000100", 'I', ""},       //    4
    {"bne",     "000101", 'I', ""},       //    5
    {"j",       "000010", 'J', ""},       //    6
    {"jal",     "000011", 'J', ""},       //    7
    {"jr",      "000000", 'R', "001000"}, //    8
    {"lui",     "001111", 'I', ""},       //    9
    {"lw",      "100011", 'I', ""},       //   10
    {"nor",     "000000", 'R', "100111"}, //   11
    {"or",      "000000", 'R', "100101"}, //   12
    {"ori",     "001101", 'I', ""},       //   13
    {"sltiu",   "001011", 'I', ""},       //   14
    {"sltu",    "000000", 'R', "101011"}, //   15
    {"sll",     "000000", 'R', "000000"}, //   16
    {"srl",     "000000", 'R', "000010"}, //   17
    {"sw",      "101011", 'I', ""},       //   18
    {"subu",    "000000", 'R', "100011"}  //   19
};

symbol_t SYMBOL_TABLE[MAX_SYMBOL_TABLE_SIZE]; // Global Symbol Table

uint32_t symbol_table_cur_index = 0; // For indexing of symbol table

/* Temporary file stream pointers */
FILE *data_seg;
FILE *text_seg;

/* Size of each section */
uint32_t data_section_size = 0;
uint32_t text_section_size = 0;

// ===================

char* change_file_ext_2(char *str, char ext) {
    char *dot = strrchr(str, '.');

    if (!dot || dot == str || (strcmp(dot, ".s") != 0))
        return NULL;

    str[strlen(str) - 1] = ext;

    return str;
}

int find_instruction(const char *cmd) {

    for(int i = 0; i < INST_LIST_LEN; i++) {
        if (strcmp(inst_list[i].name, cmd) == 0) {
            return i;
        }
    }

    return -1;
}

char *get_num_str(const char *str) {
    char *result = malloc(64);

    bzero(result, 64);

    char *p = (char *)str;
    char *q = result;

    for( ; *p ; p++) {
        if ( *p == '$' ) {
            continue;
        }

        if ( *p == ',' ) {
            continue;
        }

        *q = *p;
        q++;
    }

    return result;
}

/******************************************************
 * Function Declaration
 *******************************************************/

int ends_with(const char *str, const char *suffix) {
    int len = strlen(suffix);
    int j = strlen(str) - len;

    for(int i = 0; i < len; i++, j++) {
        if (suffix[i] != str[j]) {
            return 0;
        }
    }

    return 1;
}

/* Get only symbol name */
char *get_symbol_label(const char *name) {
    char *ptr = strchr(name, ':');
    if (ptr != NULL) {
        *ptr = 0;
    }
    
    return name;
}

/* Add name and address to symbol table */
void add_symbol(const char *name, uint32_t address) {
    char *label = get_symbol_label(name);
    symbol_t symbol;
    
    strcpy(symbol.name, label);
    symbol.address = address;

    symbol_table_add_entry(symbol);
}

/* Search symbol */
int search_symbol(const char *label) {
    for (int i = 0; i < symbol_table_cur_index; i++) {
        symbol_t symbol = SYMBOL_TABLE[i];

        if (strcmp(symbol.name, label) == 0) {
            return i;
        }
    }

    return -1;
}

symbol_t get_symbol(const int index) {
    return SYMBOL_TABLE[index];
}


int is_equals(const char *str1, const char* str2) {
    return strcmp(str1, str2) == 0;
}

char* get_outer(const char *str) {

    char *result = strdup(str);

    char *ptr = strchr(result, '(');
    if (ptr != NULL) {
        *ptr = 0;
    }

    return result;
}

char *get_inner(const char *str) {

    char *result = malloc(64);
    bzero(result, 64);

    char *p = strchr(str, '$');
    char *q = strchr(str, ')');

    strncpy(result, p, (q - p));

    return result;
}


/* Change file extension from ".s" to ".o" */
char* change_file_ext(char *str) {
    char *dot = strrchr(str, '.');

    if (!dot || dot == str || (strcmp(dot, ".s") != 0))
        return NULL;

    str[strlen(str) - 1] = 'o';
    return "";
}

/* Add symbol to global symbol table */
void symbol_table_add_entry(symbol_t symbol)
{
    SYMBOL_TABLE[symbol_table_cur_index++] = symbol;
#if DEBUG
    printf("%s: 0x%08x\n", symbol.name, symbol.address);
#endif
}

/* Convert integer number to binary string */
char* num_to_bits(unsigned int num, int len) 
{
    char* bits = (char *) malloc(len+1);
    int idx = len-1, i;
    while (num > 0 && idx >= 0) {
        if (num % 2 == 1) {
            bits[idx--] = '1';
        } else {
            bits[idx--] = '0';
        }
        num /= 2;
    }
    for (i = idx; i >= 0; i--){
        bits[i] = '0';
    }
    bits[len] = '\0';
    return bits;
}

/* Record .text section to output file */
void record_text_section(FILE *output) 
{
    uint32_t cur_addr = MEM_TEXT_START;
    char line[1024];
    const char* delimeter = ", \t\n";

    /* Point to text_seg stream */
    rewind(text_seg);

    /* Print .text section */
    while (fgets(line, 1024, text_seg) != NULL) {
        char inst[0x1000] = {0};
        char op[32] = {0};
        char label[32] = {0};
        char type = '0';
        int i, idx = 0;
        int rs, rt, rd, imm, shamt;
        int addr;

        rs = rt = rd = imm = shamt = addr = 0;
#if DEBUG
        printf("0x%08x: ", cur_addr);
#endif
        /* Find the instruction type that matches the line */
        char *temp;
        char _line[1024] = {0};
        strcpy(_line, line);
        temp = strtok(_line, delimeter);

        idx = find_instruction(temp);
        if(idx > -1) {
            type = inst_list[idx].type;
        }

        switch (type) {

            case 'R':

                strcpy(op , inst_list[idx].op);

                if( is_equals(inst_list[idx].name, "sll") 
                    || is_equals(inst_list[idx].name, "srl") 
                ) {

                    temp = strtok(NULL, delimeter);
                    rd = strtol(get_num_str(temp), NULL, 0);

                    temp = strtok(NULL, delimeter);
                    rt = strtol(get_num_str(temp), NULL, 0);

                    temp = strtok(NULL, delimeter);
                    shamt = strtol(get_num_str(temp), NULL, 0);

                    fprintf(output, "%s%s%s%s%s%s",
                            op, 
                            num_to_bits(0,5), 
                            num_to_bits(rt,5), 
                            num_to_bits(rd,5),
                            num_to_bits(shamt, 5),
                            inst_list[idx].funct);

                } else if (is_equals(inst_list[idx].name, "jr")) {

                    temp = strtok(NULL, delimeter);
                    rs = strtol(get_num_str(temp), NULL, 0);

                    fprintf(output, "%s%s%s%s",
                            op, 
                            num_to_bits(rs,5), 
                            num_to_bits(0 ,15),
                            inst_list[idx].funct);

                } else {

                    temp = strtok(NULL, delimeter);
                    rd = strtol(get_num_str(temp), NULL, 0);

                    temp = strtok(NULL, delimeter);
                    rs = strtol(get_num_str(temp), NULL, 0);

                    temp = strtok(NULL, delimeter);
                    rt = strtol(get_num_str(temp), NULL, 0);

                    fprintf(output, "%s%s%s%s%s%s",
                            op, 
                            num_to_bits(rs,5), 
                            num_to_bits(rt,5), 
                            num_to_bits(rd,5),
                            num_to_bits(0, 5),
                            inst_list[idx].funct);

                }

#if DEBUG
                printf("op:%s rs:$%d rt:$%d rd:$%d shamt:%d funct:%s\n",
                        op, rs, rt, rd, shamt, inst_list[idx].funct);
#endif
                break;

            case 'I':

                strcpy(op , inst_list[idx].op);

                if(is_equals(inst_list[idx].name, "lui")) {

                    temp = strtok(NULL, delimeter);
                    rt = strtol(get_num_str(temp), NULL, 0);

                    temp = strtok(NULL, delimeter);
                    imm = strtol(temp, NULL, 0);

                    fprintf(output, "%s%s%s%s", 
                    op,
                    num_to_bits(0, 5),
                    num_to_bits(rt, 5),
                    num_to_bits(imm, 16));

                } else if (is_equals(inst_list[idx].name, "bne") || is_equals(inst_list[idx].name, "beq"))  {

                    temp = strtok(NULL, delimeter);
                    rs = strtol(get_num_str(temp), NULL, 0);

                    temp = strtok(NULL, delimeter);
                    rt = strtol(get_num_str(temp), NULL, 0);

                    temp = strtok(NULL, delimeter);
                    int sym_idx = search_symbol(temp);
                    if (sym_idx>-1) {
                        imm = SYMBOL_TABLE[sym_idx].address;
                    }
                    else {
                        imm = strtol(temp, NULL, 0);
                    }

                    int offset = (imm - (cur_addr + 4)) / 4;

                    fprintf(output, "%s%s%s%s", 
                    op,
                    num_to_bits(rs, 5),
                    num_to_bits(rt, 5),
                    num_to_bits(offset, 16));

                } else if (is_equals(inst_list[idx].name, "lw") || is_equals(inst_list[idx].name, "sw")) {

                    temp = strtok(NULL, delimeter);
                    rt = strtol(get_num_str(temp), NULL, 0);

                    temp = strtok(NULL, delimeter);
                    imm = strtol(temp, NULL, 0);

                    temp = strtok(NULL, delimeter);
                    rs = strtol(get_num_str(temp), NULL, 0);

                    fprintf(output, "%s%s%s%s", 
                    op,
                    num_to_bits(rs, 5),
                    num_to_bits(rt, 5),
                    num_to_bits(imm, 16));

                } else {

                    temp = strtok(NULL, delimeter);
                    rt = strtol(get_num_str(temp), NULL, 0);

                    temp = strtok(NULL, delimeter);
                    rs = strtol(get_num_str(temp), NULL, 0);

                    temp = strtok(NULL, delimeter);
                    imm = strtol(temp, NULL, 0);

                    fprintf(output, "%s%s%s%s", 
                    op,
                    num_to_bits(rs, 5),
                    num_to_bits(rt, 5),
                    num_to_bits(imm, 16));

                }


#if DEBUG
                printf("op:%s rs:$%d rt:$%d imm:0x%x\n",
                        op, rs, rt, imm);
#endif
                break;

            case 'J':
            
                strcpy(op, inst_list[idx].op);

                temp = strtok(NULL, delimeter);

                int sym_idx = search_symbol(temp);
                if (sym_idx > -1) {
                    addr = SYMBOL_TABLE[sym_idx].address;
                }
                else {
                    addr = strtol(temp, NULL, 0);
                }
                
                fprintf(output, "%s%s", op, num_to_bits((addr >> 2), 26));

#if DEBUG
                printf("op:%s addr:%i\n", op, addr);
#endif
                break;

            default:
                break;
        }
        fprintf(output, "\n");

        cur_addr += BYTES_PER_WORD;
    }
}

/* Record .data section to output file */
void record_data_section(FILE *output)
{
    uint32_t cur_addr = MEM_DATA_START;
    char line[1024];
    const char* delimeter = ", \t\n";

    /* Point to data segment stream */
    rewind(data_seg);

    /* Print .data section */
    while (fgets(line, 1024, data_seg) != NULL) {
        char *temp;
        char _line[1024] = {0};
        strcpy(_line, line);
        temp = strtok(_line, delimeter);

        char *ptr = strtok(NULL, delimeter);
        long int x = strtol(ptr, NULL, 0);

        fprintf(output, "%s\n", num_to_bits(x, 32));

#if DEBUG
        printf("0x%08x: ", cur_addr);
        printf("%s", line);
#endif
        cur_addr += BYTES_PER_WORD;
    }
}

/* Fill the blanks */
void make_binary_file(FILE *output)
{
#if DEBUG
    char line[1024] = {0};
    rewind(text_seg);
    /* Print line of text segment */
    while (fgets(line, 1024, text_seg) != NULL) {
        printf("%s",line);
    }
    printf("text section size: %d, data section size: %d\n",
            text_section_size, data_section_size);
#endif

    /* Print text section size and data section size */
    fprintf(output, "%s\n", num_to_bits(text_section_size, 32));
    fprintf(output, "%s\n", num_to_bits(data_section_size, 32));

    /* Print .text section */
    record_text_section(output);

    /* Print .data section */
    record_data_section(output);
}

/* Fill the blanks */

void make_symbol_table(FILE *input)
{
    char line[1024] = {0};
    uint32_t address = 0;
    enum section cur_section = MAX_SIZE;
    const char* delimeter = ", \t\n";

    /* Read each section and put the stream */
    while (fgets(line, 1024, input) != NULL) {
        char *temp;
        char _line[1024] = {0};
        strcpy(_line, line);
        temp = strtok(_line, "\t\n");

        /* Check section type */
        if (!strcmp(temp, ".data")) {
            cur_section = DATA;
            address = MEM_DATA_START;
            data_seg = tmpfile();
            continue;
        }
        else if (!strcmp(temp, ".text")) {
            cur_section = TEXT;
            address = MEM_TEXT_START;
            text_seg = tmpfile();
            continue;
        }

        /* Put the line into each segment stream */
        if (cur_section == DATA) {
            if (ends_with(temp, ":")) {
                add_symbol(temp, address);
                temp = strtok(NULL, delimeter);
            }

            fprintf(data_seg, "%s", temp);

            while ((temp = strtok(NULL, delimeter)) != NULL) {
                fprintf(data_seg, "\t%s", temp);
            }

            fprintf(data_seg, "\n");

            data_section_size += BYTES_PER_WORD;
        }
        else if (cur_section == TEXT) {
            if (ends_with(temp, ":")) {
                add_symbol(temp, address);
                continue;
            } else {

                char *instruction = strdup(temp);

                if (is_equals(temp, "la")) {
                    char *ptr1 = strtok(NULL, delimeter);
                    char *ptr2 = strtok(NULL, delimeter);

                    char str1[64] = {0};
                    char str2[64] = {0};
                    int findIdx = search_symbol(ptr2);
                    if (findIdx > -1) {
                        uint32_t found_address = get_symbol(findIdx).address;
                        
                        char str3[64] = {0};
                        sprintf(str3, "%08x", found_address);
                        
                        strncpy(str1, str3, 4);
                        strncpy(str2, &str3[4], 4);

                    }
                    
                    fprintf(text_seg, "lui\t%s\t0x%s\n", ptr1, str1);

                    if(strcmp(str2, "0000") != 0) {
                        fprintf(text_seg, "ori\t%s\t%s\t0x%s\n", ptr1, ptr1, str2);
                        text_section_size += BYTES_PER_WORD;
                        address += BYTES_PER_WORD;
                    }

                } else if(is_equals(temp, "lw") || is_equals(temp, "sw")) {

                    char *ptr1 = strtok(NULL, delimeter);
                    char *ptr2 = strtok(NULL, delimeter);

                    char *ptr3 = get_outer(ptr2);
                    char *ptr4 = get_inner(ptr2);

                    fprintf(text_seg, "%s\t%s\t%s\t%s\n", instruction, ptr1, ptr3, ptr4);

                } else {
                    fprintf(text_seg, "%s", temp);

                    while((temp = strtok(NULL, delimeter)) != NULL) {
                        int findIdx = search_symbol(temp);
                        if (findIdx > -1) {
                            uint32_t found_address = get_symbol(findIdx).address;
                            
                            fprintf(text_seg, "\t0x%08x", found_address);
                        } else {
                            fprintf(text_seg, "\t%s", temp);
                        }
                    }

                    fprintf(text_seg, "\n");

                }

            }
            
            text_section_size += BYTES_PER_WORD;
        }

        address += BYTES_PER_WORD;
    }
}

/******************************************************
 * Function: main
 *
 * Parameters:
 *  int
 *      argc: the number of argument
 *  char*
 *      argv[]: array of a sting argument
 *
 * Return:
 *  return success exit value
 *
 * Info:
 *  The typical main function in C language.
 *  It reads system arguments from terminal (or commands)
 *  and parse an assembly file(*.s).
 *  Then, it converts a certain instruction into
 *  object code which is basically binary code.
 *
 *******************************************************/

int main(int argc, char* argv[])
{
    FILE *input, *output;
    char *filename;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <*.s>\n", argv[0]);
        fprintf(stderr, "Example: %s sample_input/example?.s\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    /* Read the input file */
    input = fopen(argv[1], "r");
    if (input == NULL) {
        perror("ERROR");
        exit(EXIT_FAILURE);
    }

    /* Create the output file (*.o) */
    filename = strdup(argv[1]); // strdup() is not a standard C library but fairy used a lot.
    if(change_file_ext(filename) == NULL) {
        fprintf(stderr, "'%s' file is not an assembly file.\n", filename);
        exit(EXIT_FAILURE);
    }

    output = fopen(filename, "w");
    if (output == NULL) {
        perror("ERROR");
        exit(EXIT_FAILURE);
    }

    /******************************************************
     *  Let's complete the below functions!
     *
     *  make_symbol_table(FILE *input)
     *  make_binary_file(FILE *output)
     *  ????????? record_text_section(FILE *output)
     *  ????????? record_data_section(FILE *output)
     *
     *******************************************************/
    make_symbol_table(input);
    make_binary_file(output);

    fclose(input);
    fclose(output);

    return 0;
}
