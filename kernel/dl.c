/*
 * dl.c – Dynamic Linker (ld.so) Stub for RISC OS Phoenix
 * Supports dlopen/dlsym/dlclose for ELF64 shared libraries
 * Called from execve after PT_DYNAMIC processing
 * Author: Grok 4 – 06 Feb 2026
 */

#include "kernel.h"
#include "vfs.h"
#include "elf64.h"
#include <string.h>

#define MAX_LIBS        32

typedef struct loaded_lib {
    char path[256];
    Elf64_Dyn *dynamic;
    Elf64_Sym *symtab;
    char *strtab;
    Elf64_Rela *rela;
    uint64_t *got;
    // ... other fields (hash, init/fini)
} loaded_lib_t;

static loaded_lib_t loaded_libs[MAX_LIBS];
static int num_libs = 0;
static spinlock_t dl_lock = SPINLOCK_INIT;

/* dlopen – load shared library */
void *dlopen(const char *filename, int flags) {
    if (!filename) return NULL;  // RTLD_DEFAULT stub

    file_t *file = vfs_open(filename, O_RDONLY);
    if (!file) return NULL;

    Elf64_Ehdr ehdr;
    if (vfs_read(file, &ehdr, sizeof(ehdr)) != sizeof(ehdr)) {
        vfs_close(file);
        return NULL;
    }

    if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0 ||
        ehdr.e_type != ET_DYN) {
        vfs_close(file);
        return NULL;
    }

    loaded_lib_t *lib = kmalloc(sizeof(loaded_lib_t));
    if (!lib) {
        vfs_close(file);
        return NULL;
    }

    strncpy(lib->path, filename, 255);

    // Load dynamic section
    Elf64_Phdr phdr;
    vfs_seek(file, ehdr.e_phoff, SEEK_SET);
    for (int i = 0; i < ehdr.e_phnum; i++) {
        vfs_read(file, &phdr, sizeof(phdr));
        if (phdr.p_type == PT_DYNAMIC) {
            lib->dynamic = kmalloc(phdr.p_filesz);
            vfs_seek(file, phdr.p_offset, SEEK_SET);
            vfs_read(file, lib->dynamic, phdr.p_filesz);
            break;
        }
    }

    vfs_close(file);

    if (!lib->dynamic) {
        kfree(lib);
        return NULL;
    }

    // Parse dynamic tags
    Elf64_Dyn *dyn = lib->dynamic;
    while (dyn->d_tag != DT_NULL) {
        switch (dyn->d_tag) {
            case DT_SYMTAB:
                lib->symtab = (Elf64_Sym*)(dyn->d_un.d_ptr);
                break;
            case DT_STRTAB:
                lib->strtab = (char*)(dyn->d_un.d_ptr);
                break;
            case DT_RELA:
                lib->rela = (Elf64_Rela*)(dyn->d_un.d_ptr);
                break;
            case DT_RELASZ:
                lib->rela_size = dyn->d_un.d_val / sizeof(Elf64_Rela);
                break;
            case DT_PLTGOT:
                lib->got = (uint64_t*)(dyn->d_un.d_ptr);
                break;
            // ... DT_HASH, DT_INIT, DT_FINI, etc.
        }
        dyn++;
    }

    // Perform relocations (stub – resolve symbols)
    for (int i = 0; i < lib->rela_size; i++) {
        Elf64_Rela *r = &lib->rela[i];
        Elf64_Sym *sym = &lib->symtab[ELF64_R_SYM(r->r_info)];
        if (ELF64_ST_TYPE(sym->st_info) == STT_FUNC) {
            // Resolve function symbol (stub – lookup in global symtab)
            uint64_t addr = resolve_symbol(sym->st_name);
            *lib->got[i] = addr + r->r_addend;
        }
    }

    // Call init function if present
    // ... (stub)

    unsigned long flags;
    spin_lock_irqsave(&dl_lock, flags);
    if (num_libs < MAX_LIBS) {
        loaded_libs[num_libs++] = *lib;
    }
    spin_unlock_irqrestore(&dl_lock, flags);

    debug_print("dlopen: Loaded %s\n", filename);
    return lib;  // Return handle
}

/* dlsym – lookup symbol */
void *dlsym(void *handle, const char *symbol) {
    loaded_lib_t *lib = handle ? (loaded_lib_t*)handle : &loaded_libs[0];  // Default lib

    Elf64_Sym *sym = lib->symtab;
    while (sym->st_name) {
        if (strcmp(lib->strtab + sym->st_name, symbol) == 0) {
            return (void*)(sym->st_value);
        }
        sym++;
    }

    return NULL;
}

/* dlclose – unload library */
int dlclose(void *handle) {
    loaded_lib_t *lib = (loaded_lib_t*)handle;
    if (!lib) return -1;

    // Call fini, free memory (stub)
    kfree(lib->dynamic);
    kfree(lib);

    debug_print("dlclose: Unloaded library\n");
    return 0;
}

/* Stub symbol resolver (global symtab) */
uint64_t resolve_symbol(const char *name) {
    // Lookup in kernel symtab or loaded libs (stub)
    if (strcmp(name, "printf") == 0) return (uint64_t)debug_print;
    return 0;
}