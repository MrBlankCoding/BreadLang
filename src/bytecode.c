#include <stdlib.h>
#include <string.h>

#include "../include/bytecode.h"

static int bc_grow_u8(uint8_t** data, int* cap, int need) {
    if (*cap >= need) return 1;
    int new_cap = (*cap == 0) ? 64 : *cap;
    while (new_cap < need) new_cap *= 2;
    uint8_t* new_data = realloc(*data, (size_t)new_cap);
    if (!new_data) return 0;
    *data = new_data;
    *cap = new_cap;
    return 1;
}

static int bc_grow_constants(BreadValue** data, int* cap, int need) {
    if (*cap >= need) return 1;
    int new_cap = (*cap == 0) ? 16 : *cap;
    while (new_cap < need) new_cap *= 2;
    BreadValue* new_data = realloc(*data, sizeof(BreadValue) * (size_t)new_cap);
    if (!new_data) return 0;
    *data = new_data;
    *cap = new_cap;
    return 1;
}

void bc_chunk_init(BytecodeChunk* chunk) {
    if (!chunk) return;
    memset(chunk, 0, sizeof(*chunk));
}

void bc_chunk_free(BytecodeChunk* chunk) {
    if (!chunk) return;
    free(chunk->code);
    chunk->code = NULL;
    chunk->count = 0;
    chunk->capacity = 0;

    for (int i = 0; i < chunk->constants_count; i++) {
        bread_value_release(&chunk->constants[i]);
    }
    free(chunk->constants);
    chunk->constants = NULL;
    chunk->constants_count = 0;
    chunk->constants_capacity = 0;
}

int bc_chunk_write(BytecodeChunk* chunk, uint8_t byte) {
    if (!chunk) return 0;
    if (!bc_grow_u8(&chunk->code, &chunk->capacity, chunk->count + 1)) return 0;
    chunk->code[chunk->count++] = byte;
    return 1;
}

int bc_chunk_write_u16(BytecodeChunk* chunk, uint16_t v) {
    if (!chunk) return 0;
    if (!bc_chunk_write(chunk, (uint8_t)((v >> 8) & 0xff))) return 0;
    if (!bc_chunk_write(chunk, (uint8_t)(v & 0xff))) return 0;
    return 1;
}

uint16_t bc_chunk_add_constant(BytecodeChunk* chunk, BreadValue v) {
    if (!chunk) return 0;

    if (!bc_grow_constants(&chunk->constants, &chunk->constants_capacity, chunk->constants_count + 1)) return 0;

    BreadValue cloned = bread_value_clone(v);
    chunk->constants[chunk->constants_count] = cloned;
    uint16_t idx = (uint16_t)chunk->constants_count;
    chunk->constants_count++;
    return idx;
}
