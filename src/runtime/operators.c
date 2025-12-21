#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "runtime/runtime.h"
#include "runtime/error.h"
#include "core/value.h"

int bread_index_op(const BreadValue* target, const BreadValue* idx, BreadValue* out) {
    if (!target || !idx || !out) return 0;

    BreadValue real_target = *target;
    int target_owned = 0;
    if (real_target.type == TYPE_OPTIONAL) {
        BreadOptional* o = real_target.value.optional_val;
        if (!o || !o->is_some) {
            bread_value_set_nil(out);
            return 1;
        }
        real_target = bread_value_clone(o->value);
        target_owned = 1;
    }

    bread_value_set_nil(out);

    if (real_target.type == TYPE_STRING) {
        if (idx->type != TYPE_INT) {
            BREAD_ERROR_SET_TYPE_MISMATCH("String index must be Int");
            if (target_owned) bread_value_release(&real_target);
            return 0;
        }
        
        int index = idx->value.int_val;
        size_t len = bread_string_len(real_target.value.string_val);
        
        // Handle negative indices (Python-style)
        if (index < 0) {
            index = (int)len + index;
        }
        
        if (index < 0 || index >= (int)len) {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), 
                    "String index %d out of bounds (length %zu)", idx->value.int_val, len);
            BREAD_ERROR_SET_INDEX_OUT_OF_BOUNDS(error_msg);
            if (target_owned) bread_value_release(&real_target);
            return 0;
        }
        
        char ch = bread_string_get_char(real_target.value.string_val, (size_t)index);
        char ch_str[2] = {ch, '\0'};
        bread_value_set_string(out, ch_str);
        if (target_owned) bread_value_release(&real_target);
        return 1;
    }

    if (real_target.type == TYPE_ARRAY) {
        if (idx->type != TYPE_INT) {
            printf("Error: Array index must be Int\n");
            if (target_owned) bread_value_release(&real_target);
            return 0;
        }
        
        int index = idx->value.int_val;
        int length = bread_array_length(real_target.value.array_val);
        
        if (index < 0) {
            index = length + index;
        }
        
        if (index < 0 || index >= length) {
            printf("Error: Array index %d out of bounds (length %d)\n", idx->value.int_val, length);
            if (target_owned) bread_value_release(&real_target);
            return 0;
        }
        
        BreadValue* at = bread_array_get(real_target.value.array_val, index);
        if (at) *out = bread_value_clone(*at);
        if (target_owned) bread_value_release(&real_target);
        return 1;
    }

    if (real_target.type == TYPE_DICT) {
        if (idx->type != TYPE_STRING) {
            printf("Error: Dictionary key must be String\n");
            if (target_owned) bread_value_release(&real_target);
            return 0;
        }
        BreadValue* v = bread_dict_get(real_target.value.dict_val, bread_string_cstr(idx->value.string_val));
        if (v) *out = bread_value_clone(*v);
        if (target_owned) bread_value_release(&real_target);
        return 1;
    }

    printf("Error: Type does not support indexing\n");
    if (target_owned) bread_value_release(&real_target);
    return 0;
}

int bread_member_op(const BreadValue* target, const char* member, int is_opt, BreadValue* out) {
    if (!target || !out) return 0;

    BreadValue real_target = *target;
    int target_owned = 0;

    if (is_opt) {
        if (real_target.type == TYPE_NIL) {
            bread_value_set_nil(out);
            return 1;
        }
        if (real_target.type == TYPE_OPTIONAL) {
            BreadOptional* o = real_target.value.optional_val;
            if (!o || !o->is_some) {
                bread_value_set_nil(out);
                return 1;
            }
            real_target = bread_value_clone(o->value);
            target_owned = 1;
        }
    }

    bread_value_set_nil(out);

    if (member && strcmp(member, "length") == 0) {
        if (real_target.type == TYPE_ARRAY) {
            bread_value_set_int(out, real_target.value.array_val ? real_target.value.array_val->count : 0);
            if (target_owned) bread_value_release(&real_target);
            return 1;
        }
        if (real_target.type == TYPE_STRING) {
            bread_value_set_int(out, (int)bread_string_len(real_target.value.string_val));
            if (target_owned) bread_value_release(&real_target);
            return 1;
        }
        if (real_target.type == TYPE_DICT) {
            bread_value_set_int(out, real_target.value.dict_val ? real_target.value.dict_val->count : 0);
            if (target_owned) bread_value_release(&real_target);
            return 1;
        }

        printf("Error: Unsupported member access\n");
        if (target_owned) bread_value_release(&real_target);
        return 0;
    }

    if (real_target.type == TYPE_DICT) {
        BreadValue* v = bread_dict_get(real_target.value.dict_val, member ? member : "");
        if (v) *out = bread_value_clone(*v);
        if (target_owned) bread_value_release(&real_target);
        return 1;
    }

    if (is_opt) {
        bread_value_set_nil(out);
        if (target_owned) bread_value_release(&real_target);
        return 1;
    }

    printf("Error: Unsupported member access\n");
    if (target_owned) bread_value_release(&real_target);
    return 0;
}

int bread_method_call_op(const BreadValue* target, const char* name, int argc, const BreadValue* args, int is_opt, BreadValue* out) {
    if (!target || !out) return 0;

    BreadValue real_target = *target;
    int target_owned = 0;

    if (is_opt) {
        if (real_target.type == TYPE_NIL) {
            bread_value_set_nil(out);
            return 1;
        }
        if (real_target.type == TYPE_OPTIONAL) {
            BreadOptional* o = real_target.value.optional_val;
            if (!o || !o->is_some) {
                bread_value_set_nil(out);
                return 1;
            }
            real_target = bread_value_clone(o->value);
            target_owned = 1;
        }
    }

    bread_value_set_nil(out);

    if (name && strcmp(name, "append") == 0) {
        if (real_target.type != TYPE_ARRAY) {
            printf("Error: append() is only supported on arrays\n");
            if (target_owned) bread_value_release(&real_target);
            return 0;
        }
        if (argc != 1 || !args) {
            printf("Error: append() expects 1 argument\n");
            if (target_owned) bread_value_release(&real_target);
            return 0;
        }
        if (!bread_array_append(real_target.value.array_val, args[0])) {
            printf("Error: Out of memory\n");
            if (target_owned) bread_value_release(&real_target);
            return 0;
        }
        if (target_owned) bread_value_release(&real_target);
        bread_value_set_nil(out);
        return 1;
    }

    printf("Error: Unsupported method call\n");
    if (target_owned) bread_value_release(&real_target);
    return 0;
}

int bread_dict_set_value(struct BreadDict* d, const BreadValue* key, const BreadValue* val) {
    if (!d || !key || !val) return 0;
    if (key->type != TYPE_STRING) {
        printf("Error: Dictionary keys must be strings\n");
        return 0;
    }
    return bread_dict_set((BreadDict*)d, bread_string_cstr(key->value.string_val), *val);
}

int bread_array_append_value(struct BreadArray* a, const BreadValue* v) {
    if (!a || !v) return 0;
    return bread_array_append((BreadArray*)a, *v);
}

int bread_array_set_value(struct BreadArray* a, int index, const BreadValue* v) {
    if (!a || !v) return 0;
    
    int length = bread_array_length((BreadArray*)a);
    if (index < 0) {
        index = length + index;
    }
    
    if (index < 0 || index >= length) {
        printf("Error: Array index %d out of bounds (length %d)\n", index, length);
        return 0;
    }
    
    return bread_array_set((BreadArray*)a, index, *v);
}