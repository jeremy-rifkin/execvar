#define _GNU_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <dlfcn.h>
#include <elf.h>
#include <link.h>

extern _start;

// needs at least 1 non-zero member in order to be put in .data
char count_data[sizeof(int)+1] = {0, 0, 0, 0, 1};
const char *read_error = "Could not open executable file for reading";
const char *write_error = "Could not open executable file for writing";

static void read_all(char *path, char *buffer);
static size_t file_size(char *path);
static void write_file(char *path, char *buffer, size_t size);

static void error(const char *message) __attribute__((noreturn));

int main(int argc, char **argv) {
	char *file_path = argv[0];
	size_t size = file_size(file_path);
	char *buffer = (char*)calloc(size + 4, sizeof(char));
	read_all(file_path, buffer);

	// get mapping base address and data's address in ELF virtual memory
	Dl_info info;
	void *extra = NULL;
	dladdr1(&_start, &info, &extra, RTLD_DL_LINKMAP);
	struct link_map *map = extra;
	uintptr_t base = (uintptr_t)map->l_addr;
	uintptr_t count_addr = (uintptr_t)count_data - base;
	int *count = (int*) count_data;
	printf("count: %d\n", *count);

	// parse elf header
	Elf64_Ehdr header;
	memcpy(&header, buffer, sizeof(header));
	if(memcmp(header.e_ident, ELFMAG, SELFMAG) != 0) {
		error("Executable is not ELF");
	}
	// parse section headers
	Elf64_Off e_shoff = header.e_shoff;
	uint16_t e_shentsize = header.e_shentsize;
	uint16_t e_shnum = header.e_shnum;
	Elf64_Shdr block_header;
	bool did_find_block = false;
	for(int i = 0; i < e_shnum; i++) {
		Elf64_Shdr sheader;
		memcpy(&sheader, buffer + e_shoff + i * e_shentsize, sizeof(sheader));
		if(sheader.sh_type == SHT_PROGBITS || sheader.sh_type == SHT_NOBITS) { // data segment or bss
			// if this section could contain the data we're looking for and it does contain the data...
			if(count_addr > sheader.sh_addr && count_addr - sheader.sh_addr < sheader.sh_size) {
				block_header = sheader;
				did_find_block = true;
				assert(sheader.sh_type != SHT_NOBITS); // we rely on the object being in .data; this is a development check
				break;
			}
		}
	}
	if(!did_find_block) {
		error("internal error: unable to find object in program data");
	}
	// sanity check
	int dummy;
	memcpy(&dummy, buffer + block_header.sh_offset + (count_addr - block_header.sh_addr), sizeof(int));
	assert(dummy == *count);
	// update count and write back
	(*count)++;
	memcpy(buffer + block_header.sh_offset + (count_addr - block_header.sh_addr), count, sizeof(int));

	// We must unlink the file, because otherwise Linux will not let us write to it.
	unlink(file_path);
	write_file(file_path, buffer, size);
	chmod(file_path, 0755);
	free(buffer);
	
	return EXIT_SUCCESS;
}

static void read_all(char *path, char *buffer) {
	size_t size = file_size(path);
	FILE *file = fopen(path, "rb");
	if(!file)
		error(read_error);
	if(fread(buffer, 1, size, file) != size)
		error("Error while reading executable");
	fclose(file);
}

static size_t file_size(char *path) {
	FILE *file = fopen(path, "rb");
	if (!file)
		error(read_error);
	fseek(file, 0, SEEK_END);
	size_t size = ftell(file);
	rewind(file);
	fclose(file);
	return size;
}

static void write_file(char *path, char *buffer, size_t size) {
	FILE *file = fopen(path, "wb");
	if (!file)
		error(write_error);
	if(fwrite(buffer, 1, size, file) != size)
		error("Error while writing executable");
	fclose(file);
}

static void error(const char *message) {
	fprintf(stderr, "Error: %s\n", message);
	exit(EXIT_FAILURE);
}
